#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_BUFF 1024
#define TIMEOUT_MS 100

typedef struct {
    int protocol;           // 1 - TCP, 2 - UDP, 3 - ICMP
    char ip[256];           // IP адрес (если указан)
    int port;               // Порт (для TCP/UDP)
    int is_server;          // 1 - сервер, 0 - клиент
} Arguments;

static struct termios old_cmd_settings;
static int flag_old_cmd_settings = 0;
static int sock_fd = -1;
static int server_fd = -1;
static unsigned char encryption_key = 0;

void print_help() {
    printf("Используйте: program <protocol> [options]\n\n");
    printf("Протоколы:\n");
    printf("  tcp [ip] port    - TCP режим (сервер если нет ip, клиент если ip указан)\n");
    printf("  udp [ip] port    - UDP режим (сервер если нет ip, клиент если ip указан)\n");
    printf("  icmp [ip]        - ICMP режим (ожидание если нет ip, отправка если ip указан)\n\n");
    printf("Примеры:\n");
    printf("  program tcp 8888              # TCP сервер\n");
    printf("  program tcp 127.0.0.1 8888    # TCP клиент\n");
    printf("  program udp 8889              # UDP сервер\n");
    printf("  program udp 127.0.0.1 8889    # UDP клиент\n");
    printf("  sudo program icmp             # ICMP ожидание\n");
    printf("  sudo program icmp 127.0.0.1   # ICMP отправка\n");
}

int parse_arguments(int argc, char *argv[], Arguments *args) {
    // Проверка минимального количества аргументов
    if (argc < 2) {
        printf("Ошибка: недостаточно аргументов\n");
        return -1;
    }

    // Инициализация структуры
    memset(args, 0, sizeof(Arguments));
    args->port = -1;  // Порт не указан

    // Определение протокола
    if (strcmp(argv[1], "tcp") == 0) {
        args->protocol = 1;  // TCP
    }
    else if (strcmp(argv[1], "udp") == 0) {
        args->protocol = 2;  // UDP
    }
    else if (strcmp(argv[1], "icmp") == 0) {
        args->protocol = 3;  // ICMP
    }
    else {
        printf("Ошибка: неизвестный протокол '%s'\n", argv[1]);
        return -1;
    }

    // Парсинг аргументов в зависимости от протокола
    if (args->protocol == 1 || args->protocol == 2) {  // TCP или UDP
        if (argc == 3) {
            // Формат: program <protocol> port (сервер)
            args->port = atoi(argv[2]);
            if (args->port <= 0 || args->port > 65535) {
                printf("Ошибка: некорректный порт '%s'\n", argv[2]);
                return -1;
            }
            args->is_server = 1;  // Сервер (свой IP не указан)
        }
        else if (argc == 4) {
            // Формат: program <protocol> ip port (клиент)
            strncpy(args->ip, argv[2], sizeof(args->ip) - 1);
            args->ip[sizeof(args->ip) - 1] = '\0';

            args->port = atoi(argv[3]);
            if (args->port <= 0 || args->port > 65535) {
                printf("Ошибка: некорректный порт '%s'\n", argv[3]);
                return -1;
            }
            args->is_server = 0;  // Клиент (IP указан)
        }
        else {
            printf("Ошибка: неверное количество аргументов для %s\n",
                   args->protocol == 1 ? "TCP" : "UDP");
            return -1;
        }
    }
    else if (args->protocol == 3) {  // ICMP
        if (argc == 2) {
            // Формат: program icmp (сервер/ожидание)
            args->is_server = 1;  // Режим ожидания
        }
        else if (argc == 3) {
            // Формат: program icmp ip (клиент/отправка)
            strncpy(args->ip, argv[2], sizeof(args->ip) - 1);
            args->ip[sizeof(args->ip) - 1] = '\0';
            args->is_server = 0;  // Режим отправки
        }
        else {
            printf("Ошибка: неверное количество аргументов для ICMP\n");
            return -1;
        }
    }

    return 0;  // Успех
}

// Установка неблокирующего режима
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void cleanup_cmd() {
    if (flag_old_cmd_settings) 
        tcsetattr(0, TCSANOW, &old_cmd_settings);
}

void signal_handler(int sig) {
    cleanup_cmd();
    if (sock_fd > 0) close(sock_fd);
    if (server_fd > 0) close(server_fd);
    exit(0);
}

// Генерация случайного ключа
unsigned char generate_key() {
    srand(time(NULL) ^ getpid());
    return 33 + (rand() % 94);
}

// Функция для отправки зашифрованного сообщения
int send_encrypted_msg(char *msg, int fd, unsigned char key) {
    if (msg == NULL || fd < 0) return 1;
    
    int msg_len = strlen(msg);
    char *encrypted = malloc(msg_len + 2); // +1 для \n и +1 для \0
    if (encrypted == NULL) return 1;
    
    // Шифруем сообщение
    for (int i = 0; i < msg_len; i++) {
        encrypted[i] = msg[i] ^ key;
    }
    encrypted[msg_len] = '\n';  // Добавляем \n без шифрования
    encrypted[msg_len + 1] = '\0';
    
    ssize_t bytes_sent = send(fd, encrypted, msg_len + 1, 0);
    free(encrypted);
    
    return (bytes_sent < 0) ? 1 : 0;
}

// Функция для чтения строки из сокета с расшифровкой
char* read_line(int fd, char *buffer, int *buffer_len, unsigned char key) {
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

    // Расшифровываем строку (без \n)
    for (int i = 0; i < line_len; i++) {
        buffer[i] = temp[i] ^ key;
    }
    buffer[line_len] = '\0';
    *buffer_len = line_len;

    int rest_len = total_len - (line_len + 1);
    if (rest_len > 0) {
        // Остаток сохраняем как есть (зашифрованным)
        memcpy(leftover, newline_ptr + 1, rest_len);
        leftover_len = rest_len;
    } else {
        leftover_len = 0;
    }

    return buffer;
}

// Функция чата по TCP
int tcp_chat(int fd, int is_server) {
    char input_buffer[MAX_BUFF] = {0};
    char read_buffer[MAX_BUFF] = {0};
    int pos = 0;
    char peer_name[20];

    // Определяем имя собеседника
    if (is_server) {
        strcpy(peer_name, "Клиент");
    } else {
        strcpy(peer_name, "Сервер");
    }

    // Устанавливаем неблокирующий режим для сокета
    set_nonblocking(fd);

    printf("\n=== Чат начат ===\n");
    printf("> ");
    fflush(stdout);

    while (1) {
        fd_set read_fds;
        struct timeval tv = {0, TIMEOUT_MS * 1000};

        FD_ZERO(&read_fds);
        FD_SET(0, &read_fds);      // stdin
        FD_SET(fd, &read_fds);      // сокет

        int max_fd = (fd > 0) ? fd : 0;

        if (select(max_fd + 1, &read_fds, NULL, NULL, &tv) < 0) {
            if (errno == EINTR) continue;
            break;
        }

        // Проверяем данные от собеседника
        if (FD_ISSET(fd, &read_fds)) {
            int buf_len = sizeof(read_buffer);
            char *line = read_line(fd, read_buffer, &buf_len, encryption_key);
            
            while (line != NULL) {
                if (strlen(line) > 0) {
                    printf("\r[%s] %s\n> %s", peer_name, line, input_buffer);
                    fflush(stdout);
                }
                buf_len = sizeof(read_buffer);
                line = read_line(fd, read_buffer, &buf_len, encryption_key);
            }
            
            // Проверяем, не закрыл ли собеседник соединение
            if (buf_len == 0) {
                printf("\nСобеседник закрыл соединение. Выход...\n");
                break;
            }
        }

        // Проверяем ввод с клавиатуры
        if (FD_ISSET(0, &read_fds)) {
            char c;
            if (read(0, &c, 1) == 1) {
                if (c == '\n' && pos > 0) {
                    // Отправляем зашифрованное сообщение
                    if (send_encrypted_msg(input_buffer, fd, encryption_key) == 0) {
                        printf("\r[Вы] %s\n> ", input_buffer);
                    } else {
                        printf("\r[Ошибка отправки]\n> ");
                    }
                    
                    memset(input_buffer, 0, MAX_BUFF);
                    pos = 0;
                } 
                else if ((c == 127 || c == '\b') && pos > 0) {
                    pos--;
                    input_buffer[pos] = '\0';
                    printf("\b \b");
                } 
                else if (c != '\n' && pos < MAX_BUFF - 1) {
                    input_buffer[pos++] = c;
                    putchar(c);
                }
                fflush(stdout);
            }
        }
    }

    return 0;
}

// Функция для серверного режима TCP
int tcp_server_mode(int port, unsigned char key) {
    struct sockaddr_in address;
    int opt = 1;
    
    // Создание сокета
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        fprintf(stderr, "Ошибка создания сокета\n");
        return -1;
    }
    
    // Настройка опций сокета
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        fprintf(stderr, "Ошибка setsockopt\n");
        close(server_fd);
        return -1;
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    // Привязка сокета
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        fprintf(stderr, "Ошибка bind\n");
        close(server_fd);
        return -1;
    }
    
    // Ожидание подключений
    if (listen(server_fd, 1) < 0) {
        fprintf(stderr, "Ошибка listen\n");
        close(server_fd);
        return -1;
    }
    
    printf("TCP сервер запущен на порту %d. Ожидание подключения...\n", port);
    
    // Принимаем подключение
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    sock_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
    if (sock_fd < 0) {
        fprintf(stderr, "Ошибка accept\n");
        close(server_fd);
        return -1;
    }
    
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    printf("Клиент подключился: %s:%d\n", client_ip, ntohs(client_addr.sin_port));
    
    // Отправляем ключ шифрования клиенту
    if (send(sock_fd, &key, 1, 0) < 0) {
        fprintf(stderr, "Ошибка отправки ключа\n");
        close(sock_fd);
        close(server_fd);
        return -1;
    }
    
    printf("Ключ шифрования отправлен клиенту\n");
    
    // Закрываем серверный сокет, он больше не нужен
    close(server_fd);
    server_fd = -1;
    
    // Запускаем чат
    encryption_key = key;
    tcp_chat(sock_fd, 1);
    
    close(sock_fd);
    sock_fd = -1;
    return 0;
}

// Функция для клиентского режима TCP
int tcp_client_mode(const char* server_ip, int port) {
    struct sockaddr_in server_addr;
    unsigned char key;
    
    // Создание сокета
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        fprintf(stderr, "Ошибка создания сокета\n");
        return -1;
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    // Преобразование IP адреса
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Ошибка: некорректный IP адрес %s\n", server_ip);
        close(sock_fd);
        return -1;
    }
    
    // Подключение к серверу
    printf("Подключение к серверу %s:%d...\n", server_ip, port);
    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Ошибка подключения к серверу\n");
        close(sock_fd);
        return -1;
    }
    
    printf("Успешно подключено к серверу\n");
    
    // Получение ключа от сервера
    if (recv(sock_fd, &key, 1, 0) < 0) {
        fprintf(stderr, "Ошибка при получении ключа\n");
        close(sock_fd);
        return -1;
    }
    
    printf("Получен ключ шифрования: %c (код %d)\n", key, key);
    
    // Запускаем чат
    encryption_key = key;
    tcp_chat(sock_fd, 0);
    
    close(sock_fd);
    sock_fd = -1;
    return 0;
}

// Основная функция для TCP режима
int handle_tcp_mode(Arguments *args) {
    unsigned char key;
    
    if (args->is_server) {
        // Серверный режим - генерируем ключ
        key = generate_key();
        printf("Сгенерирован ключ шифрования: %c (код %d)\n", key, key);
        return tcp_server_mode(args->port, key);
    } else {
        // Клиентский режим
        return tcp_client_mode(args->ip, args->port);
    }
}

// Заглушка для UDP режима
int handle_udp_mode(Arguments *args) {
    printf("UDP режим пока не реализован\n");
    return 1;
}

// Заглушка для ICMP режима
int handle_icmp_mode(Arguments *args) {
    printf("ICMP режим пока не реализован\n");
    return 1;
}

int main(int argc, char *argv[]) {
    Arguments args;
    
    // Установка обработчиков сигналов
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);
    
    if (parse_arguments(argc, argv, &args) < 0) {
        print_help();
        return 1;
    }
    
    // Настройка терминала для неканонического режима
    struct termios new_tio;
    tcgetattr(0, &old_cmd_settings);
    flag_old_cmd_settings = 1;
    
    new_tio = old_cmd_settings;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    new_tio.c_cc[VMIN] = 1;
    new_tio.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &new_tio);
    
    int result = 0;
    
    // Выбор режима работы на основе протокола
    if (args.protocol == 1) {  // TCP
        result = handle_tcp_mode(&args);
    } else if (args.protocol == 2) {  // UDP
        result = handle_udp_mode(&args);
    } else if (args.protocol == 3) {  // ICMP
        result = handle_icmp_mode(&args);
    }
    
    // Восстановление настроек терминала
    cleanup_cmd();
    
    // Закрытие открытых сокетов
    if (sock_fd > 0) close(sock_fd);
    if (server_fd > 0) close(server_fd);
    
    return result;
}
