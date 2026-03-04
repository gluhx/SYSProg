#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>

#define MAX_BUFF 1024
#define TIMEOUT_MS 100
#define ICMP_DATA_SIZE 56

// Типы ICMP для разных направлений
#define ICMP_CLIENT_TO_SERVER 42  // Клиент -> Сервер
#define ICMP_SERVER_TO_CLIENT 43  // Сервер -> Клиент

typedef struct {
    char ip[256];
    int is_server;
} Arguments;

static struct termios old_cmd_settings;
static int flag_old_cmd_settings = 0;
static int send_sock = -1;      // Сокет для отправки
static int recv_sock = -1;      // Сокет для приёма
static struct sockaddr_in peer_addr;
static unsigned char encryption_key = 0;

void print_help() {
    printf("Использование: icmp_chat [OPTIONS] [IP_ADDRESS]\n\n");
    printf("Режимы работы:\n");
    printf("  icmp_chat              # ICMP сервер\n");
    printf("  icmp_chat <ip>         # ICMP клиент\n");
    printf("  icmp_chat --help       # Показать эту справку\n\n");
}

int parse_arguments(int argc, char *argv[], Arguments *args) {
    memset(args, 0, sizeof(Arguments));

    if (argc == 1) {
        args->is_server = 1;
    }
    else if (argc == 2) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            print_help();
            exit(0);
        } else {
            strncpy(args->ip, argv[1], sizeof(args->ip) - 1);
            args->ip[sizeof(args->ip) - 1] = '\0';
            args->is_server = 0;
        }
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
    if (send_sock > 0) close(send_sock);
    if (recv_sock > 0) close(recv_sock);
    exit(0);
}

unsigned char generate_key() {
    srand(time(NULL) ^ getpid());
    return 33 + (rand() % 94);
}

unsigned short checksum(void *b, int len) {
    unsigned short *buf = b;
    unsigned int sum = 0;

    for (sum = 0; len > 1; len -= 2)
        sum += *buf++;
    if (len == 1)
        sum += *(unsigned char*)buf;

    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return ~sum;
}

int create_icmp_socket() {
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        perror("socket creation failed");
        return -1;
    }
    return sock;
}

// Отправка сообщения (своим типом для каждого направления)
int send_message(int sock, struct sockaddr_in *dest_addr,
                 const char *message, unsigned char key, int msg_type) {
    size_t len = strlen(message);

    unsigned char data[ICMP_DATA_SIZE] = {0};
    data[0] = len;

    for (size_t i = 0; i < len && i < ICMP_DATA_SIZE - 1; i++) {
        data[i + 1] = message[i];
    }

    struct icmp packet;
    memset(&packet, 0, sizeof(packet));

    packet.icmp_type = msg_type;  // Используем тип для конкретного направления
    packet.icmp_code = 0;
    packet.icmp_id = htons(getpid());
    packet.icmp_seq = htons(rand() % 1000);
    memcpy(packet.icmp_data, data, ICMP_DATA_SIZE);

    packet.icmp_cksum = 0;
    packet.icmp_cksum = checksum(&packet, sizeof(packet));

    return sendto(sock, &packet, sizeof(packet), 0,
                  (struct sockaddr*)dest_addr, sizeof(*dest_addr));
}

// Приём сообщения (с фильтрацией по нужному типу)
int receive_message(int sock, struct sockaddr_in *src_addr,
                    char *buffer, size_t buf_size, unsigned char key, int expected_type) {
    socklen_t addr_len = sizeof(*src_addr);
    unsigned char recv_buffer[2048];
    struct ip *ip_header;
    struct icmp *icmp_packet;

    int bytes = recvfrom(sock, recv_buffer, sizeof(recv_buffer), 0,
                         (struct sockaddr*)src_addr, &addr_len);

    if (bytes <= 0) return -1;

    ip_header = (struct ip *)recv_buffer;
    int ip_header_len = ip_header->ip_hl * 4;

    if (bytes <= ip_header_len) return -1;

    icmp_packet = (struct icmp *)(recv_buffer + ip_header_len);

    // Принимаем только сообщения нужного типа
    if (icmp_packet->icmp_type != expected_type) return -1;

    unsigned char *data = icmp_packet->icmp_data;
    size_t len = data[0];

    if (len == 0 || len >= buf_size) return -1;

    for (size_t i = 0; i < len; i++) {
        buffer[i] = data[i + 1] ^ key;
    }
    buffer[len] = '\0';

    return len;
}

// Клиент: отправляет на ICMP_CLIENT_TO_SERVER, принимает на ICMP_SERVER_TO_CLIENT
int client_chat(struct sockaddr_in *server_addr, unsigned char key) {
    char input_buffer[MAX_BUFF] = {0};
    char read_buffer[MAX_BUFF] = {0};
    int pos = 0;

    printf("\n=== Чат с сервером ===\n");
    printf("> ");
    fflush(stdout);

    while (1) {
        fd_set read_fds;
        struct timeval tv = {0, TIMEOUT_MS * 1000};

        FD_ZERO(&read_fds);
        FD_SET(0, &read_fds);
        FD_SET(recv_sock, &read_fds);  // Слушаем входящие от сервера

        int max_fd = (recv_sock > 0) ? recv_sock : 0;

        if (select(max_fd + 1, &read_fds, NULL, NULL, &tv) < 0) {
            if (errno == EINTR) continue;
            break;
        }

        // Получение сообщения от сервера
        if (FD_ISSET(recv_sock, &read_fds)) {
            struct sockaddr_in temp_addr;
            int bytes = receive_message(recv_sock, &temp_addr, read_buffer,
                                        sizeof(read_buffer), key, ICMP_SERVER_TO_CLIENT);

            if (bytes > 0 && strlen(read_buffer) > 0) {
                printf("\r[Сервер] %s\n> %s", read_buffer, input_buffer);
                fflush(stdout);
            }
        }

        // Отправка сообщения серверу
        if (FD_ISSET(0, &read_fds)) {
            char c;
            if (read(0, &c, 1) == 1) {
                if (c == '\n' && pos > 0) {
                    input_buffer[pos] = '\0';

                    send_message(send_sock, server_addr, input_buffer,
                                 key, ICMP_CLIENT_TO_SERVER);

                    printf("\r[Вы] %s\n> ", input_buffer);

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

// Сервер: принимает на ICMP_CLIENT_TO_SERVER, отправляет на ICMP_SERVER_TO_CLIENT
int server_chat(struct sockaddr_in *client_addr, unsigned char key) {
    char input_buffer[MAX_BUFF] = {0};
    char read_buffer[MAX_BUFF] = {0};
    int pos = 0;

    printf("\n=== Чат с клиентом ===\n");
    printf("> ");
    fflush(stdout);

    while (1) {
        fd_set read_fds;
        struct timeval tv = {0, TIMEOUT_MS * 1000};

        FD_ZERO(&read_fds);
        FD_SET(0, &read_fds);
        FD_SET(recv_sock, &read_fds);  // Слушаем входящие от клиента

        int max_fd = (recv_sock > 0) ? recv_sock : 0;

        if (select(max_fd + 1, &read_fds, NULL, NULL, &tv) < 0) {
            if (errno == EINTR) continue;
            break;
        }

        // Получение сообщения от клиента
        if (FD_ISSET(recv_sock, &read_fds)) {
            struct sockaddr_in temp_addr;
            int bytes = receive_message(recv_sock, &temp_addr, read_buffer,
                                        sizeof(read_buffer), key, ICMP_CLIENT_TO_SERVER);

            if (bytes > 0 && strlen(read_buffer) > 0) {
                printf("\r[Клиент] %s\n> %s", read_buffer, input_buffer);
                fflush(stdout);
            }
        }

        // Отправка сообщения клиенту
        if (FD_ISSET(0, &read_fds)) {
            char c;
            if (read(0, &c, 1) == 1) {
                if (c == '\n' && pos > 0) {
                    input_buffer[pos] = '\0';

                    send_message(send_sock, client_addr, input_buffer,
                                 key, ICMP_SERVER_TO_CLIENT);

                    printf("\r[Вы] %s\n> ", input_buffer);

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

int wait_for_client(int sock, struct sockaddr_in *client_addr, unsigned char *key) {
    socklen_t addr_len = sizeof(*client_addr);
    unsigned char buffer[2048];
    struct ip *ip_header;
    struct icmp *icmp_packet;

    printf("Ожидание подключения клиента...\n");

    while (1) {
        int bytes = recvfrom(sock, buffer, sizeof(buffer), 0,
                            (struct sockaddr*)client_addr, &addr_len);

        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            perror("Ошибка при ожидании клиента");
            return -1;
        }

        ip_header = (struct ip *)buffer;
        int ip_header_len = ip_header->ip_hl * 4;

        if (bytes <= ip_header_len) continue;

        icmp_packet = (struct icmp *)(buffer + ip_header_len);

        // Ждём запрос от клиента
        if (icmp_packet->icmp_type == ICMP_CLIENT_TO_SERVER) {
            break;
        }
    }

    *key = generate_key();

    struct icmp reply_packet;
    memset(&reply_packet, 0, sizeof(reply_packet));

    // Отвечаем клиенту ключом (используем тип сервер->клиент)
    reply_packet.icmp_type = ICMP_SERVER_TO_CLIENT;
    reply_packet.icmp_code = 0;
    reply_packet.icmp_id = htons(getpid());
    reply_packet.icmp_seq = htons(1);
    reply_packet.icmp_data[0] = *key;

    reply_packet.icmp_cksum = 0;
    reply_packet.icmp_cksum = checksum(&reply_packet, sizeof(reply_packet));

    if (sendto(sock, &reply_packet, sizeof(reply_packet), 0,
               (struct sockaddr*)client_addr, addr_len) < 0) {
        perror("Ошибка при отправке ключа");
        return -1;
    }

    printf("Клиент подключен: %s\n", inet_ntoa(client_addr->sin_addr));
    printf("Сгенерированный ключ: %d ('%c')\n", *key, *key);

    return 0;
}

int icmp_server_mode() {
    unsigned char key;

    // Создаём два сокета
    send_sock = create_icmp_socket();
    recv_sock = create_icmp_socket();

    if (send_sock < 0 || recv_sock < 0) {
        fprintf(stderr, "Ошибка создания ICMP сокетов\n");
        return -1;
    }

    printf("ICMP сервер запущен (PID: %d)\n", getpid());

    // Ждём клиента на receive сокете
    if (wait_for_client(recv_sock, &peer_addr, &key) < 0) {
        fprintf(stderr, "Ошибка при подключении клиента\n");
        return -1;
    }

    // Запускаем чат
    server_chat(&peer_addr, key);

    close(send_sock);
    close(recv_sock);
    return 0;
}

int icmp_client_mode(const char* server_ip) {
    struct sockaddr_in server_addr;
    unsigned char key;
    int attempts = 0;
    const int max_attempts = 5;

    // Создаём два сокета
    send_sock = create_icmp_socket();
    recv_sock = create_icmp_socket();

    if (send_sock < 0 || recv_sock < 0) {
        fprintf(stderr, "Ошибка создания ICMP сокетов\n");
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(recv_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Ошибка: некорректный IP адрес %s\n", server_ip);
        return -1;
    }

    printf("Подключение к серверу %s (PID: %d)...\n", server_ip, getpid());

    // Отправляем запрос на подключение
    struct icmp packet;
    memset(&packet, 0, sizeof(packet));

    packet.icmp_type = ICMP_CLIENT_TO_SERVER;
    packet.icmp_code = 0;
    packet.icmp_id = htons(getpid());
    packet.icmp_seq = htons(1);

    packet.icmp_cksum = 0;
    packet.icmp_cksum = checksum(&packet, sizeof(packet));

    if (sendto(send_sock, &packet, sizeof(packet), 0,
               (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Ошибка отправки запроса на сервер\n");
        return -1;
    }

    printf("Ожидание ответа от сервера...\n");

    unsigned char recv_buffer[2048];
    struct ip *ip_header;
    struct icmp *icmp_reply;

    while (attempts < max_attempts) {
        socklen_t addr_len = sizeof(peer_addr);
        memset(recv_buffer, 0, sizeof(recv_buffer));

        int bytes = recvfrom(recv_sock, recv_buffer, sizeof(recv_buffer), 0,
                             (struct sockaddr*)&peer_addr, &addr_len);

        if (bytes > 0) {
            ip_header = (struct ip *)recv_buffer;
            int ip_header_len = ip_header->ip_hl * 4;

            if (bytes > ip_header_len) {
                icmp_reply = (struct icmp *)(recv_buffer + ip_header_len);

                // Ждём ответ от сервера нужного типа
                if (icmp_reply->icmp_type == ICMP_SERVER_TO_CLIENT) {
                    key = icmp_reply->icmp_data[0];

                    printf("Успешно подключено к серверу %s\n", server_ip);
                    printf("Получен ключ шифрования: %d ('%c')\n", key, key);

                    // Запускаем чат
                    client_chat(&peer_addr, key);

                    return 0;
                }
            }
        }

        attempts++;
        if (attempts < max_attempts) {
            printf("Повторная попытка %d/%d...\n", attempts + 1, max_attempts);

            packet.icmp_seq = htons(attempts + 1);
            packet.icmp_cksum = 0;
            packet.icmp_cksum = checksum(&packet, sizeof(packet));

            sendto(send_sock, &packet, sizeof(packet), 0,
                   (struct sockaddr*)&server_addr, sizeof(server_addr));
        }
    }

    fprintf(stderr, "Ошибка при получении ключа от сервера %s\n", server_ip);
    return -1;
}

int main(int argc, char *argv[]) {
    Arguments args;
    int result = 0;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);

    if (parse_arguments(argc, argv, &args) < 0) {
        print_help();
        return 1;
    }

    if (geteuid() != 0) {
        printf("Для работы ICMP сокета требуются root права.\n");
        printf("Запустите с sudo: sudo %s\n", argv[0]);
        return 1;
    }

    struct termios new_tio;
    tcgetattr(0, &old_cmd_settings);
    flag_old_cmd_settings = 1;

    new_tio = old_cmd_settings;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    new_tio.c_cc[VMIN] = 1;
    new_tio.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &new_tio);

    if (args.is_server) {
        result = icmp_server_mode();
    } else {
        result = icmp_client_mode(args.ip);
    }

    cleanup_cmd();

    if (send_sock > 0) close(send_sock);
    if (recv_sock > 0) close(recv_sock);

    return result;
}
