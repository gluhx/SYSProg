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
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_BUFF 1024
#define TIMEOUT_MS 100

typedef struct {
    char ip[256];           // IP адрес (если указан)
    int port;               // Порт
    int is_server;          // 1 - сервер, 0 - клиент
} Arguments;

static struct termios old_cmd_settings;
static int flag_old_cmd_settings = 0;
static int sock_fd = -1;
static struct sockaddr_in peer_addr;  // Адрес собеседника
static unsigned char encryption_key = 0;

void print_help() {
    printf("Используйте: 03_udp [options]\n\n");
    printf("Режимы работы:\n");
    printf("  03_udp <port>              # UDP сервер (ожидание клиента)\n");
    printf("  03_udp <ip> <port>         # UDP клиент (подключение к серверу)\n\n");
    printf("Примеры:\n");
    printf("  03_udp 8888                 # UDP сервер на порту 8888\n");
    printf("  03_udp 127.0.0.1 8888       # UDP клиент для подключения к 127.0.0.1:8888\n");
}

int parse_arguments(int argc, char *argv[], Arguments *args) {
    if (argc < 2) {
        printf("Ошибка: недостаточно аргументов\n");
        return -1;
    }

    memset(args, 0, sizeof(Arguments));
    args->port = -1;

    if (argc == 2) {
        // Формат: 03_udp port (сервер)
        args->port = atoi(argv[1]);
        if (args->port <= 0 || args->port > 65535) {
            printf("Ошибка: некорректный порт '%s'\n", argv[1]);
            return -1;
        }
        args->is_server = 1;
    }
    else if (argc == 3) {
        // Формат: 03_udp ip port (клиент)
        strncpy(args->ip, argv[1], sizeof(args->ip) - 1);
        args->ip[sizeof(args->ip) - 1] = '\0';

        args->port = atoi(argv[2]);
        if (args->port <= 0 || args->port > 65535) {
            printf("Ошибка: некорректный порт '%s'\n", argv[2]);
            return -1;
        }
        args->is_server = 0;
    }
    else {
        printf("Ошибка: неверное количество аргументов\n");
        return -1;
    }

    return 0;
}

void cleanup_cmd() {
    if (flag_old_cmd_settings)
        tcsetattr(0, TCSANOW, &old_cmd_settings);
}

void signal_handler(int sig) {
    cleanup_cmd();
    if (sock_fd > 0) close(sock_fd);
    exit(0);
}

// Генерация случайного ключа (от 33 до 126 - печатные символы ASCII)
unsigned char generate_key() {
    srand(time(NULL) ^ getpid());
    return 33 + (rand() % 94);
}

// Функция XOR шифрования (не шифрует последний символ - \n)
void xor_encrypt(char *data, size_t len, unsigned char key) {
    // Не шифруем последний символ (должен быть \n)
    for (size_t i = 0; i < len - 1; i++) {
        data[i] = data[i] ^ key;
    }
}

int create_simple_udp_socket(int port) {
    int sock;
    struct sockaddr_in addr;

    // Создаем сокет
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    // Настраиваем адрес
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;  // Слушаем на всех интерфейсах

    // Привязываем сокет к порту
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    return sock;
}

// Функция ожидания подключения клиента
int wait_for_client(int sock, struct sockaddr_in *client_addr, unsigned char *key) {
    socklen_t addr_len = sizeof(*client_addr);
    char buffer[1];  // Нам не нужны данные, просто ждем первый пакет

    printf("Ожидание подключения клиента...\n");

    // Ждем первый пакет от клиента
    int bytes = recvfrom(sock, buffer, sizeof(buffer), 0,
                         (struct sockaddr*)client_addr, &addr_len);

    if (bytes < 0) {
        perror("Ошибка при ожидании клиента");
        return -1;
    }

    // Генерируем ключ для клиента
    *key = generate_key();

    // Отправляем ключ клиенту
    if (sendto(sock, key, 1, 0,
               (struct sockaddr*)client_addr, addr_len) < 0) {
        perror("Ошибка при отправке ключа");
        return -1;
    }

    printf("Клиент подключен: %s:%d\n",
           inet_ntoa(client_addr->sin_addr),
           ntohs(client_addr->sin_port));
    printf("Сгенерированный ключ: %d ('%c')\n", *key, *key);

    return 0;
}

// ИСПРАВЛЕННАЯ ФУНКЦИЯ: отправка зашифрованного сообщения
int send_encrypted_message(int sock, struct sockaddr_in *client_addr,
                           const char *message, unsigned char key) {
    size_t len = strlen(message);
    char *buffer = malloc(len + 1);  // +1 для \n, но не для лишнего нуля
    if (!buffer) return -1;

    // Копируем сообщение
    memcpy(buffer, message, len);
    
    // Добавляем \n в конец
    buffer[len] = '\n';
    
    // Увеличиваем длину для отправки (включая \n)
    len++;

    // Шифруем всё, кроме последнего символа (\n)
    for (size_t i = 0; i < len - 1; i++) {
        buffer[i] = buffer[i] ^ key;
    }

    // Отправляем
    int sent = sendto(sock, buffer, len, 0,
                      (struct sockaddr*)client_addr, sizeof(*client_addr));

    free(buffer);
    return sent;
}

// Функция приема и расшифровки сообщения от клиента
int receive_encrypted_message(int sock, struct sockaddr_in *client_addr,
                              char *buffer, size_t buf_size, unsigned char key) {
    socklen_t addr_len = sizeof(*client_addr);

    int bytes = recvfrom(sock, buffer, buf_size - 1, 0,
                         (struct sockaddr*)client_addr, &addr_len);

    if (bytes > 0) {
        // Расшифровываем всё, кроме последнего символа (\n)
        for (int i = 0; i < bytes - 1; i++) {
            buffer[i] = buffer[i] ^ key;
        }
        // Убираем \n из конца строки для отображения
        if (bytes > 0 && buffer[bytes - 1] == '\n') {
            buffer[bytes - 1] = '\0';
        } else {
            buffer[bytes] = '\0';
        }
    }

    return bytes;
}

// Функция чата для UDP
int udp_chat(int sock, struct sockaddr_in *peer, int is_server, unsigned char key) {
    char input_buffer[MAX_BUFF] = {0};
    char read_buffer[MAX_BUFF] = {0};
    int pos = 0;
    char peer_name[20];

    if (is_server) {
        strcpy(peer_name, "Клиент");
    } else {
        strcpy(peer_name, "Сервер");
    }

    printf("\n=== Чат начат ===\n");
    printf("> ");
    fflush(stdout);

    while (1) {
        fd_set read_fds;
        struct timeval tv = {0, TIMEOUT_MS * 1000};

        FD_ZERO(&read_fds);
        FD_SET(0, &read_fds);      // stdin
        FD_SET(sock, &read_fds);    // сокет

        int max_fd = (sock > 0) ? sock : 0;

        if (select(max_fd + 1, &read_fds, NULL, NULL, &tv) < 0) {
            if (errno == EINTR) continue;
            break;
        }

        // Проверяем данные от собеседника
        if (FD_ISSET(sock, &read_fds)) {
            struct sockaddr_in temp_addr;
            int bytes = receive_encrypted_message(sock, &temp_addr, read_buffer, 
                                                  sizeof(read_buffer), key);
            
            if (bytes > 0) {
                // Проверяем, что сообщение от нужного собеседника
                if (peer->sin_addr.s_addr == temp_addr.sin_addr.s_addr &&
                    peer->sin_port == temp_addr.sin_port) {
                    
                    if (strlen(read_buffer) > 0) {
                        printf("\r[%s] %s\n> %s", peer_name, read_buffer, input_buffer);
                        fflush(stdout);
                    }
                }
            }
        }

        // Проверяем ввод с клавиатуры
        if (FD_ISSET(0, &read_fds)) {
            char c;
            if (read(0, &c, 1) == 1) {
                if (c == '\n' && pos > 0) {
                    // Отправляем зашифрованное сообщение
                    if (send_encrypted_message(sock, peer, input_buffer, key) > 0) {
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

// Функция для серверного режима
int udp_server_mode(int port) {
    unsigned char key;

    // Создаем UDP сокет
    sock_fd = create_simple_udp_socket(port);
    if (sock_fd < 0) {
        fprintf(stderr, "Ошибка создания UDP сокета на порту %d\n", port);
        return -1;
    }

    printf("UDP сервер запущен на порту %d. Ожидание подключения клиента...\n", port);

    // Ожидаем подключения клиента и получаем ключ
    if (wait_for_client(sock_fd, &peer_addr, &key) < 0) {
        fprintf(stderr, "Ошибка при подключении клиента\n");
        close(sock_fd);
        return -1;
    }

    // Запускаем чат
    udp_chat(sock_fd, &peer_addr, 1, key);

    close(sock_fd);
    sock_fd = -1;
    return 0;
}

// Функция для клиентского режима
int udp_client_mode(const char* server_ip, int port) {
    struct sockaddr_in server_addr;
    unsigned char key;

    // Создаем UDP сокет (порт выберется автоматически)
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        fprintf(stderr, "Ошибка создания сокета\n");
        return -1;
    }

    // Настраиваем адрес сервера
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Ошибка: некорректный IP адрес %s\n", server_ip);
        close(sock_fd);
        return -1;
    }

    printf("Подключение к серверу %s:%d...\n", server_ip, port);

    // Отправляем пустой пакет для инициализации соединения
    if (sendto(sock_fd, "", 0, 0, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Ошибка отправки запроса на сервер\n");
        close(sock_fd);
        return -1;
    }

    // Получаем ключ от сервера
    socklen_t addr_len = sizeof(peer_addr);
    int bytes = recvfrom(sock_fd, &key, 1, 0, 
                         (struct sockaddr*)&peer_addr, &addr_len);
    
    if (bytes < 0) {
        fprintf(stderr, "Ошибка при получении ключа\n");
        close(sock_fd);
        return -1;
    }

    printf("Успешно подключено к серверу\n");
    printf("Получен ключ шифрования: %d ('%c')\n", key, key);

    // Запускаем чат
    udp_chat(sock_fd, &peer_addr, 0, key);

    close(sock_fd);
    sock_fd = -1;
    return 0;
}

int main(int argc, char *argv[]) {
    Arguments args;
    int result = 0;

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

    // Запуск в соответствующем режиме
    if (args.is_server) {
        // Серверный режим
        result = udp_server_mode(args.port);
    } else {
        // Клиентский режим
        result = udp_client_mode(args.ip, args.port);
    }

    // Восстановление настроек терминала
    cleanup_cmd();

    // Закрытие открытых сокетов
    if (sock_fd > 0) close(sock_fd);

    return result;
}
