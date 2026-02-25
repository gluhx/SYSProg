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
static int server_socket_fd = -1;
static int data_socket_fd = -1;

void signal_handler(int sig) {
    if (old_tio_saved) tcsetattr(0, TCSANOW, &old_tio_global);
    if (server_socket_fd > 0) close(server_socket_fd);
    if (data_socket_fd > 0) close(data_socket_fd);
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
    printf("\t--unname\t- Передача через ненаименованный pipe (с сокетами)\n");
    printf("\t--pid <PID>\t- PID собеседника для ненаименованного pipe\n");
    printf("\t--help\t\t- Вывод этой справки\n\n");
    printf("Примеры:\n");
    printf("\t./02_pipe --unname\t\t\t- Создать сервер\n");
    printf("\t./02_pipe --unname --pid 1234\t\t- Подключиться к серверу 1234\n");
    printf("\t./02_pipe --name\t\t\t- Создать именованный pipe (будет запрошено имя)\n");
}

int send_msg(pid_t my_pid, char *msg, int fd) {
    if (msg == NULL || fd < 0) return 1;
    char buffer[MAX_BUFF * 2];
    int len = snprintf(buffer, sizeof(buffer), "%d:%s\n", my_pid, msg);
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
    pid_t my_pid = getpid();
    
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
    printf("Чат через именованный pipe: %s (мой PID: %d)\n", pipe_name, my_pid);
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
                char *colon = strchr(line, ':');
                if (colon != NULL) {
                    *colon = '\0';
                    pid_t sender = atoi(line);
                    char *msg = colon + 1;
                    if (sender != my_pid && strlen(msg) > 0) {
                        printf("\r[Собеседник %d] %s\n> %s", sender, msg, input_buffer);
                        fflush(stdout);
                    }
                }
                buf_len = sizeof(read_buffer);
                line = read_line(pipe_fd, read_buffer, &buf_len);
            }
        }
        
        if (FD_ISSET(0, &read_fds)) {
            char c;
            if (read(0, &c, 1) == 1) {
                if (c == '\n' && pos > 0) {
                    send_msg(my_pid, input_buffer, pipe_fd);
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

int chat_over_socket(int fd, pid_t my_pid, const char *role) {
    char input_buffer[MAX_BUFF] = {0};
    char read_buffer[MAX_BUFF * 2] = {0};
    int pos = 0;
    
    set_nonblocking(fd);
    printf("%s (PID: %d) - Чат начат!\n", role, my_pid);
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
                char *colon = strchr(line, ':');
                if (colon != NULL) {
                    *colon = '\0';
                    pid_t sender = atoi(line);
                    char *msg = colon + 1;
                    if (sender != my_pid && strlen(msg) > 0) {
                        printf("\r[Собеседник %d] %s\n> %s", sender, msg, input_buffer);
                        fflush(stdout);
                    }
                }
                buf_len = sizeof(read_buffer);
                line = read_line(fd, read_buffer, &buf_len);
            }
        }
        
        if (FD_ISSET(0, &read_fds)) {
            char c;
            if (read(0, &c, 1) == 1) {
                if (c == '\n' && pos > 0) {
                    send_msg(my_pid, input_buffer, fd);
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
    
    server_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_socket_fd < 0) {
        perror("socket");
        return 1;
    }
    
    unlink(SOCKET_PATH);
    
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (bind(server_socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    
    if (listen(server_socket_fd, MAX_CONNECTIONS) < 0) {
        perror("listen");
        return 1;
    }
    
    set_nonblocking(server_socket_fd);
    
    printf("Сервер (PID: %d) запущен\n", my_pid);
    printf("Ожидание подключения клиента...\n");
    printf("Клиент должен запустить: ./02_pipe --unname --pid %d\n\n", my_pid);
    
    while (1) {
        fd_set read_fds;
        struct timeval tv = {0, TIMEOUT_MS * 1000};
        
        FD_ZERO(&read_fds);
        FD_SET(server_socket_fd, &read_fds);
        
        if (select(server_socket_fd + 1, &read_fds, NULL, NULL, &tv) < 0) break;
        
        if (FD_ISSET(server_socket_fd, &read_fds)) {
            data_socket_fd = accept(server_socket_fd, NULL, NULL);
            if (data_socket_fd >= 0) {
                printf("Клиент подключился!\n\n");
                break;
            }
        }
    }
    
    if (data_socket_fd < 0) {
        printf("Не удалось подключиться\n");
        return 1;
    }
    
    close(server_socket_fd);
    server_socket_fd = -1;
    
    return chat_over_socket(data_socket_fd, my_pid, "Сервер");
}

int client_mode(pid_t target_pid) {
    pid_t my_pid = getpid();
    struct sockaddr_un addr;
    
    printf("Клиент (PID: %d) подключается к серверу (PID: %d)\n", my_pid, target_pid);
    
    data_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (data_socket_fd < 0) {
        perror("socket");
        return 1;
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    set_nonblocking(data_socket_fd);
    printf("Подключение к серверу...\n");
    
    int connected = 0;
    for (int attempt = 0; attempt < 30; attempt++) {
        if (connect(data_socket_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            connected = 1;
            break;
        }
        
        if (errno == EINPROGRESS) {
            fd_set write_fds;
            struct timeval tv = {0, 100000};
            
            FD_ZERO(&write_fds);
            FD_SET(data_socket_fd, &write_fds);
            
            if (select(data_socket_fd + 1, NULL, &write_fds, NULL, &tv) > 0) {
                int err;
                socklen_t err_len = sizeof(err);
                if (getsockopt(data_socket_fd, SOL_SOCKET, SO_ERROR, &err, &err_len) == 0 && err == 0) {
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
        close(data_socket_fd);
        return 1;
    }
    
    int flags = fcntl(data_socket_fd, F_GETFL, 0);
    fcntl(data_socket_fd, F_SETFL, flags & ~O_NONBLOCK);
    
    printf("Подключено к серверу!\n\n");
    
    return chat_over_socket(data_socket_fd, my_pid, "Клиент");
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
    
    if (data_socket_fd > 0) close(data_socket_fd);
    if (server_socket_fd > 0) close(server_socket_fd);
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
