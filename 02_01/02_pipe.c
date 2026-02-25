#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>

#define MAX_BUFF 1024
#define SOCKET_PATH "/tmp/pipe_chat_socket"
#define MAX_CONNECTIONS 1
#define TIMEOUT_MS 100

typedef struct {
    int mode;          // 0 - unname, 1 - name
    int target_pid;    // PID собеседника
    int flag_pid;      // Флаг наличия PID
} Arguments;

static struct termios old_tio_global;
static int old_tio_saved = 0;
static int socket_fd = -1;           // Сокет для передачи fd
static int pipe_fd = -1;              // Файловый дескриптор pipe для чтения/записи
static int is_server = 0;              // Флаг: являемся ли мы сервером

void signal_handler(int sig) {
    if (old_tio_saved) tcsetattr(0, TCSANOW, &old_tio_global);
    if (socket_fd > 0) close(socket_fd);
    if (pipe_fd > 0) close(pipe_fd);
    unlink(SOCKET_PATH);
    exit(0);
}

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return (flags < 0) ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int parse_arguments(int argc, char *argv[], Arguments *args) {
    args->mode = 0;
    args->target_pid = 0;
    args->flag_pid = 0;

    if (argc == 1) return -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            return -1;
        } else if (strcmp(argv[i], "--name") == 0) {
            args->mode = 1;
        } else if (strcmp(argv[i], "--unname") == 0) {
            args->mode = 0;
        } else if (strcmp(argv[i], "--pid") == 0) {
            args->flag_pid = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                args->target_pid = atoi(argv[i + 1]);
                i++;
            } else {
                printf("ERR: Для --pid нужно указать PID\n");
                return -1;
            }
        } else {
            printf("ERR: Неизвестная опция %s\n", argv[i]);
            return -1;
        }
    }

    if (args->flag_pid == 1 && args->target_pid == 0) {
        printf("ERR: Указан --pid, но не указан корректный PID\n");
        return -1;
    }

    return 0;
}

void print_help() {
    printf("Используйте: ./02_pipe [ОПЦИИ]\n\n");
    printf("\t--name\t\t- Передача через именованный pipe\n");
    printf("\t--unname\t- Передача через ненаименованный pipe (с передачей fd через сокет)\n");
    printf("\t--pid <PID>\t- PID собеседника для ненаименованного pipe\n");
    printf("\t--help\t\t- Вывод этой справки\n\n");
    printf("Примеры:\n");
    printf("\t./02_pipe --unname\t\t\t- Создать сервер\n");
    printf("\t./02_pipe --unname --pid 1234\t\t- Подключиться к серверу 1234\n");
    printf("\t./02_pipe --name\t\t\t- Создать именованный pipe (будет запрошено имя)\n");
}

// Функция для отправки файлового дескриптора через сокет
int send_fd(int socket, int fd_to_send) {
    struct msghdr msg = {0};                    // Заголовок сообщения
    struct cmsghdr *cmsg;                        // Указатель на управляющее сообщение
    char buf[CMSG_SPACE(sizeof(int))] = {0};    // Буфер для управляющих данных
    char data = 'x';                              // Пустые данные (можно что угодно)
    struct iovec io = { .iov_base = &data, .iov_len = sizeof(data) }; // Вектор ввода-вывода

    // Настройка заголовка сообщения
    msg.msg_iov = &io;          // Указываем данные
    msg.msg_iovlen = 1;         // Один вектор
    msg.msg_control = buf;       // Буфер для управляющих данных
    msg.msg_controllen = sizeof(buf); // Размер буфера

    // Получаем указатель на управляющее сообщение
    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;     // Уровень сокета
    cmsg->cmsg_type = SCM_RIGHTS;      // Тип: передача прав (файловых дескрипторов)
    cmsg->cmsg_len = CMSG_LEN(sizeof(int)); // Длина данных

    // Копируем файловый дескриптор в управляющее сообщение
    // CMSG_DATA - макрос для получения указателя на данные в cmsg
    *((int *)CMSG_DATA(cmsg)) = fd_to_send;

    // Отправляем сообщение
    return sendmsg(socket, &msg, 0);
}

// Функция для приема файлового дескриптора через сокет
int recv_fd(int socket) {
    struct msghdr msg = {0};
    struct cmsghdr *cmsg;
    char buf[CMSG_SPACE(sizeof(int))] = {0};
    char data;
    struct iovec io = { .iov_base = &data, .iov_len = sizeof(data) };

    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    // Принимаем сообщение
    if (recvmsg(socket, &msg, 0) < 0) return -1;

    // Получаем указатель на управляющее сообщение
    cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        // Извлекаем файловый дескриптор
        return *((int *)CMSG_DATA(cmsg));
    }
    return -1;
}

int send_msg(char *msg, int fd) {
    if (msg == NULL || fd < 0) return 1;
    char buffer[MAX_BUFF * 2];
    int len = snprintf(buffer, sizeof(buffer), "%s\n", msg);  // Без PID, т.к. это отдельный канал
    return (write(fd, buffer, len) < 0) ? 1 : 0;
}

int init_cmd(struct termios *old, struct termios *new) {
    tcgetattr(0, old);
    *new = *old;
    new->c_lflag &= ~(ICANON | ECHO);
    new->c_cc[VMIN] = 1;
    new->c_cc[VTIME] = 0;
    return tcsetattr(0, TCSANOW, new);
}

int stop_cmd(struct termios *old) {
    return tcsetattr(0, TCSANOW, old);
}

char* read_line(int fd, char *buffer, int *buffer_len) {
    static char leftover[MAX_BUFF * 2] = {0};
    static int leftover_len = 0;
    char temp[MAX_BUFF * 2];

    if (leftover_len > 0) memcpy(temp, leftover, leftover_len);

    int bytes = read(fd, temp + leftover_len, MAX_BUFF - 1);
    if (bytes <= 0) return NULL;

    int total_len = leftover_len + bytes;
    temp[total_len] = '\0';

    char *newline_ptr = strchr(temp, '\n');
    if (newline_ptr == NULL) {
        memcpy(leftover, temp, total_len);
        leftover_len = total_len;
        return NULL;
    }

    int line_len = newline_ptr - temp;
    if (line_len >= *buffer_len) line_len = *buffer_len - 1;

    memcpy(buffer, temp, line_len);
    buffer[line_len] = '\0';
    *buffer_len = line_len;

    int rest_len = total_len - (line_len + 1);
    if (rest_len > 0) {
        memcpy(leftover, newline_ptr + 1, rest_len);
        leftover_len = rest_len;
    } else {
        leftover_len = 0;
    }

    return buffer;
}

int named_pipe_cmd() {
    char pipe_name[256];
    char input_buffer[MAX_BUFF] = {0};
    char read_buffer[MAX_BUFF * 2] = {0};
    int pos = 0;

    printf("Введите имя для именованного pipe: ");
    fflush(stdout);
    if (fgets(pipe_name, sizeof(pipe_name), stdin) == NULL) return 1;
    pipe_name[strcspn(pipe_name, "\n")] = 0;

    if (mkfifo(pipe_name, 0666) != 0 && errno != EEXIST) {
        perror("mkfifo");
        return 1;
    }

    int pipe_fd = open(pipe_name, O_RDWR);
    if (pipe_fd < 0) {
        perror("open");
        return 1;
    }

    set_nonblocking(pipe_fd);
    printf("Чат через именованный pipe: %s\n", pipe_name);
    printf("> ");
    fflush(stdout);

    while (1) {
        fd_set read_fds;
        struct timeval tv = {0, TIMEOUT_MS * 1000};

        FD_ZERO(&read_fds);
        FD_SET(0, &read_fds);
        FD_SET(pipe_fd, &read_fds);

        if (select(pipe_fd + 1, &read_fds, NULL, NULL, &tv) < 0) break;

        if (FD_ISSET(pipe_fd, &read_fds)) {
            int buf_len = sizeof(read_buffer);
            char *line = read_line(pipe_fd, read_buffer, &buf_len);
            while (line != NULL) {
                if (strlen(line) > 0) {
                    printf("\r[Собеседник] %s\n> %s", line, input_buffer);
                    fflush(stdout);
                }
                buf_len = sizeof(read_buffer);
                line = read_line(pipe_fd, read_buffer, &buf_len);
            }
        }

        if (FD_ISSET(0, &read_fds)) {
            char c;
            if (read(0, &c, 1) == 1) {
                if (c == '\n' && pos > 0) {
                    send_msg(input_buffer, pipe_fd);
                    printf("\r[Вы] %s\n> ", input_buffer);
                    memset(input_buffer, 0, MAX_BUFF);
                    pos = 0;
                } else if ((c == 127 || c == '\b') && pos > 0) {
                    pos--;
                    input_buffer[pos] = '\0';
                    printf("\b \b");
                } else if (c != '\n' && pos < MAX_BUFF - 1) {
                    input_buffer[pos++] = c;
                    putchar(c);
                }
                fflush(stdout);
            }
        }
    }

    close(pipe_fd);
    unlink(pipe_name);
    return 0;
}

// Функция чата через pipe (оба процесса используют один pipe)
int chat_over_pipe(int fd) {
    char input_buffer[MAX_BUFF] = {0};
    char read_buffer[MAX_BUFF] = {0};
    int pos = 0;

    set_nonblocking(fd);
    printf("Чат начат! (используется ненаименованный pipe)\n");
    printf("> ");
    fflush(stdout);

    while (1) {
        fd_set read_fds;
        struct timeval tv = {0, TIMEOUT_MS * 1000};

        FD_ZERO(&read_fds);
        FD_SET(0, &read_fds);
        FD_SET(fd, &read_fds);

        if (select(fd + 1, &read_fds, NULL, NULL, &tv) < 0) break;

        if (FD_ISSET(fd, &read_fds)) {
            int buf_len = sizeof(read_buffer);
            char *line = read_line(fd, read_buffer, &buf_len);
            while (line != NULL) {
                if (strlen(line) > 0) {
                    printf("\r[Собеседник] %s\n> %s", line, input_buffer);
                    fflush(stdout);
                }
                buf_len = sizeof(read_buffer);
                line = read_line(fd, read_buffer, &buf_len);
            }
        }

        if (FD_ISSET(0, &read_fds)) {
            char c;
            if (read(0, &c, 1) == 1) {
                if (c == '\n' && pos > 0) {
                    send_msg(input_buffer, fd);
                    printf("\r[Вы] %s\n> ", input_buffer);
                    memset(input_buffer, 0, MAX_BUFF);
                    pos = 0;
                } else if ((c == 127 || c == '\b') && pos > 0) {
                    pos--;
                    input_buffer[pos] = '\0';
                    printf("\b \b");
                } else if (c != '\n' && pos < MAX_BUFF - 1) {
                    input_buffer[pos++] = c;
                    putchar(c);
                }
                fflush(stdout);
            }
        }
    }

    return 0;
}

int server_mode() {
    pid_t my_pid = getpid();
    struct sockaddr_un addr;
    int pipefds[2];  // Массив для двух концов pipe: [0] - чтение, [1] - запись

    // Шаг 1: Создаем ненаименованный pipe
    if (pipe(pipefds) < 0) {
        perror("pipe");
        return 1;
    }
    
    printf("Сервер (PID: %d) создал pipe: чтение=%d, запись=%d\n", 
           my_pid, pipefds[0], pipefds[1]);

    // Шаг 2: Создаем сокет для передачи файлового дескриптора
    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("socket");
        return 1;
    }

    unlink(SOCKET_PATH);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(socket_fd, MAX_CONNECTIONS) < 0) {
        perror("listen");
        return 1;
    }

    set_nonblocking(socket_fd);

    printf("Сервер (PID: %d) запущен\n", my_pid);
    printf("Ожидание подключения клиента для передачи файлового дескриптора...\n");
    printf("Клиент должен запустить: ./02_pipe --unname --pid %d\n\n", my_pid);

    // Шаг 3: Ждем подключения клиента
    int client_socket = -1;
    while (1) {
        fd_set read_fds;
        struct timeval tv = {0, TIMEOUT_MS * 1000};

        FD_ZERO(&read_fds);
        FD_SET(socket_fd, &read_fds);

        if (select(socket_fd + 1, &read_fds, NULL, NULL, &tv) < 0) break;

        if (FD_ISSET(socket_fd, &read_fds)) {
            client_socket = accept(socket_fd, NULL, NULL);
            if (client_socket >= 0) {
                printf("Клиент подключился к сокету!\n");
                break;
            }
        }
    }

    if (client_socket < 0) {
        printf("Не удалось подключиться\n");
        return 1;
    }

    // Шаг 4: Отправляем файловый дескриптор pipe клиенту
    // Отправляем оба конца pipe? Нет, отправляем только один конец для чтения/записи
    // Для двусторонней связи через pipe нужно два pipe или один pipe в обе стороны?
    // В классическом pipe данные идут только в одну сторону.
    // Но мы хотим двусторонний чат, поэтому создадим ДВА pipe или будем использовать один pipe в обе стороны?
    // Проще создать ДВА pipe: один для отправки, другой для приема.
    // Но для простоты примера создадим один pipe и будем использовать его в обе стороны?
    // Это не сработает, потому что pipe однонаправленный.
    // Давайте создадим ДВА pipe: один для передачи от сервера к клиенту, другой для передачи от клиента к серверу.
    
    // Пересоздаем pipe правильно: нам нужно два pipe для двусторонней связи
    int pipe_to_client[2];  // Сервер пишет, клиент читает
    int pipe_to_server[2];  // Клиент пишет, сервер читает
    
    if (pipe(pipe_to_client) < 0 || pipe(pipe_to_server) < 0) {
        perror("pipe");
        return 1;
    }
    
    printf("Сервер создал два pipe для двусторонней связи\n");
    
    // Отправляем клиенту:
    // - pipe_to_client[0] (конец для чтения) - чтобы клиент мог читать сообщения от сервера
    // - pipe_to_server[1] (конец для записи) - чтобы клиент мог писать сообщения серверу
    printf("Отправка файловых дескрипторов клиенту...\n");
    
    // Отправляем первый дескриптор (для чтения от сервера)
    if (send_fd(client_socket, pipe_to_client[0]) < 0) {
        perror("send_fd (read end)");
        return 1;
    }
    
    // Отправляем второй дескриптор (для записи серверу)
    if (send_fd(client_socket, pipe_to_server[1]) < 0) {
        perror("send_fd (write end)");
        return 1;
    }
    
    printf("Файловые дескрипторы отправлены клиенту\n");
    
    // Закрываем ненужные концы pipe на сервере
    close(pipe_to_client[0]);  // Этот конец будет использовать клиент для чтения
    close(pipe_to_server[1]);  // Этот конец будет использовать клиент для записи
    
    // Сервер будет:
    // - писать в pipe_to_client[1] (отправка сообщений клиенту)
    // - читать из pipe_to_server[0] (прием сообщений от клиента)
    
    // Но для чата нам нужен один файловый дескриптор для select()
    // Создадим его? Нет, нужно использовать два разных дескриптора.
    // Модифицируем chat_over_pipe для работы с двумя дескрипторами?
    
    close(client_socket);
    close(socket_fd);
    socket_fd = -1;
    
    // Запускаем чат с двумя pipe
    printf("\n=== Чат начат ===\n");
    printf("Сервер (PID: %d) готов к общению\n", my_pid);
    printf("> ");
    fflush(stdout);
    
    char input_buffer[MAX_BUFF] = {0};
    char read_buffer[MAX_BUFF] = {0};
    int pos = 0;
    
    set_nonblocking(pipe_to_server[0]);  // Неблокирующее чтение из pipe от клиента
    set_nonblocking(pipe_to_client[1]);  // Запись будет блокирующей, но это нормально
    
    while (1) {
        fd_set read_fds;
        struct timeval tv = {0, TIMEOUT_MS * 1000};
        
        FD_ZERO(&read_fds);
        FD_SET(0, &read_fds);                 // stdin
        FD_SET(pipe_to_server[0], &read_fds); // данные от клиента
        
        int max_fd = (pipe_to_server[0] > 0) ? pipe_to_server[0] : 0;
        
        if (select(max_fd + 1, &read_fds, NULL, NULL, &tv) < 0) break;
        
        // Проверяем данные от клиента
        if (FD_ISSET(pipe_to_server[0], &read_fds)) {
            int buf_len = sizeof(read_buffer);
            char *line = read_line(pipe_to_server[0], read_buffer, &buf_len);
            while (line != NULL) {
                if (strlen(line) > 0) {
                    printf("\r[Клиент] %s\n> %s", line, input_buffer);
                    fflush(stdout);
                }
                buf_len = sizeof(read_buffer);
                line = read_line(pipe_to_server[0], read_buffer, &buf_len);
            }
        }
        
        // Проверяем ввод с клавиатуры
        if (FD_ISSET(0, &read_fds)) {
            char c;
            if (read(0, &c, 1) == 1) {
                if (c == '\n' && pos > 0) {
                    // Отправляем сообщение клиенту
                    if (send_msg(input_buffer, pipe_to_client[1]) == 0) {
                        printf("\r[Вы] %s\n> ", input_buffer);
                    } else {
                        printf("\r[Ошибка отправки]\n> ");
                    }
                    memset(input_buffer, 0, MAX_BUFF);
                    pos = 0;
                } else if ((c == 127 || c == '\b') && pos > 0) {
                    pos--;
                    input_buffer[pos] = '\0';
                    printf("\b \b");
                } else if (c != '\n' && pos < MAX_BUFF - 1) {
                    input_buffer[pos++] = c;
                    putchar(c);
                }
                fflush(stdout);
            }
        }
    }
    
    close(pipe_to_client[1]);
    close(pipe_to_server[0]);
    
    return 0;
}

int client_mode(pid_t target_pid) {
    pid_t my_pid = getpid();
    struct sockaddr_un addr;

    printf("Клиент (PID: %d) подключается к серверу (PID: %d)\n", my_pid, target_pid);

    // Шаг 1: Создаем сокет для получения файлового дескриптора
    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("socket");
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    set_nonblocking(socket_fd);
    printf("Подключение к серверу для получения файловых дескрипторов pipe...\n");

    // Шаг 2: Подключаемся к серверу
    int connected = 0;
    for (int attempt = 0; attempt < 30; attempt++) {
        if (connect(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            connected = 1;
            break;
        }

        if (errno == EINPROGRESS) {
            fd_set write_fds;
            struct timeval tv = {0, 100000};

            FD_ZERO(&write_fds);
            FD_SET(socket_fd, &write_fds);

            if (select(socket_fd + 1, NULL, &write_fds, NULL, &tv) > 0) {
                int err;
                socklen_t err_len = sizeof(err);
                if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &err, &err_len) == 0 && err == 0) {
                    connected = 1;
                    break;
                }
            }
        }

        printf("Попытка подключения %d/30...\n", attempt + 1);
        usleep(100000);
    }

    if (!connected) {
        printf("Не удалось подключиться к серверу\n");
        close(socket_fd);
        return 1;
    }

    // Возвращаем блокирующий режим для сокета
    int flags = fcntl(socket_fd, F_GETFL, 0);
    fcntl(socket_fd, F_SETFL, flags & ~O_NONBLOCK);

    printf("Подключено к серверу! Ожидание файловых дескрипторов pipe...\n");

    // Шаг 3: Получаем два файловых дескриптора от сервера
    int pipe_read_fd = recv_fd(socket_fd);  // Для чтения от сервера
    if (pipe_read_fd < 0) {
        perror("recv_fd (read)");
        return 1;
    }
    
    int pipe_write_fd = recv_fd(socket_fd);  // Для записи серверу
    if (pipe_write_fd < 0) {
        perror("recv_fd (write)");
        return 1;
    }
    
    printf("Получены файловые дескрипторы pipe: чтение=%d, запись=%d\n", 
           pipe_read_fd, pipe_write_fd);
    
    close(socket_fd);
    socket_fd = -1;
    
    // Шаг 4: Запускаем чат
    printf("\n=== Чат начат ===\n");
    printf("Клиент (PID: %d) готов к общению\n", my_pid);
    printf("> ");
    fflush(stdout);
    
    char input_buffer[MAX_BUFF] = {0};
    char read_buffer[MAX_BUFF] = {0};
    int pos = 0;
    
    set_nonblocking(pipe_read_fd);  // Неблокирующее чтение
    
    while (1) {
        fd_set read_fds;
        struct timeval tv = {0, TIMEOUT_MS * 1000};
        
        FD_ZERO(&read_fds);
        FD_SET(0, &read_fds);               // stdin
        FD_SET(pipe_read_fd, &read_fds);     // данные от сервера
        
        int max_fd = (pipe_read_fd > 0) ? pipe_read_fd : 0;
        
        if (select(max_fd + 1, &read_fds, NULL, NULL, &tv) < 0) break;
        
        // Проверяем данные от сервера
        if (FD_ISSET(pipe_read_fd, &read_fds)) {
            int buf_len = sizeof(read_buffer);
            char *line = read_line(pipe_read_fd, read_buffer, &buf_len);
            while (line != NULL) {
                if (strlen(line) > 0) {
                    printf("\r[Сервер] %s\n> %s", line, input_buffer);
                    fflush(stdout);
                }
                buf_len = sizeof(read_buffer);
                line = read_line(pipe_read_fd, read_buffer, &buf_len);
            }
        }
        
        // Проверяем ввод с клавиатуры
        if (FD_ISSET(0, &read_fds)) {
            char c;
            if (read(0, &c, 1) == 1) {
                if (c == '\n' && pos > 0) {
                    // Отправляем сообщение серверу
                    if (send_msg(input_buffer, pipe_write_fd) == 0) {
                        printf("\r[Вы] %s\n> ", input_buffer);
                    } else {
                        printf("\r[Ошибка отправки]\n> ");
                    }
                    memset(input_buffer, 0, MAX_BUFF);
                    pos = 0;
                } else if ((c == 127 || c == '\b') && pos > 0) {
                    pos--;
                    input_buffer[pos] = '\0';
                    printf("\b \b");
                } else if (c != '\n' && pos < MAX_BUFF - 1) {
                    input_buffer[pos++] = c;
                    putchar(c);
                }
                fflush(stdout);
            }
        }
    }
    
    close(pipe_read_fd);
    close(pipe_write_fd);
    
    return 0;
}

int unname_pipe_cmd(Arguments *args) {
    return (args->flag_pid == 0) ? server_mode() : client_mode(args->target_pid);
}

int cmd(Arguments *args) {
    struct termios new_tio;
    int result = 0;

    tcgetattr(0, &old_tio_global);
    old_tio_saved = 1;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);

    if (init_cmd(&old_tio_global, &new_tio) != 0) {
        fprintf(stderr, "ERR: Не удалось инициализировать терминал\n");
        return 1;
    }

    if (args->mode == 1) {
        result = named_pipe_cmd();
    } else {
        result = unname_pipe_cmd(args);
    }

    stop_cmd(&old_tio_global);
    old_tio_saved = 0;

    if (socket_fd > 0) close(socket_fd);
    if (pipe_fd > 0) close(pipe_fd);
    unlink(SOCKET_PATH);

    return result;
}

int main(int argc, char *argv[]) {
    Arguments args;

    if (parse_arguments(argc, argv, &args) < 0) {
        print_help();
        return 1;
    }

    return cmd(&args);
}
