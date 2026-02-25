#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>

#define MAX_BUFF 1024
#define TIMEOUT_MS 100
#define TCP_PORT 8888
#define UDP_PORT 8889
#define ICMP_ID 12345

// Глобальные переменные для очистки
static struct termios old_tio_global;
static int old_tio_saved = 0;
static int sock_fd = -1;
static int server_fd = -1;
static unsigned char global_key = 0;
static int key_exchanged = 0;

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

// Обработчик сигналов
void signal_handler(int sig) {
    if (old_tio_saved) {
        tcsetattr(0, TCSANOW, &old_tio_global);
    }
    if (sock_fd > 0) close(sock_fd);
    if (server_fd > 0) close(server_fd);
    exit(0);
}

// Установка неблокирующего режима
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Генерация случайного ключа
unsigned char generate_key() {
    srand(time(NULL) ^ getpid());
    return 33 + (rand() % 94);
}

// XOR шифрование
void xor_cipher(unsigned char *data, int len) {
    for (int i = 0; i < len; i++) {
        data[i] ^= global_key;
    }
}

// Расчет контрольной суммы ICMP
unsigned short icmp_checksum(unsigned short *buf, int len) {
    unsigned long sum = 0;
    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(unsigned char *)buf;
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return ~sum;
}

// Функция для чтения строки из сокета
char* read_line(int fd, char *buffer, int *buffer_len, 
                struct sockaddr *addr, socklen_t *addr_len, int is_udp) {
    static char leftover[MAX_BUFF * 2] = {0};
    static int leftover_len = 0;
    char temp[MAX_BUFF * 2];
    
    if (leftover_len > 0) {
        memcpy(temp, leftover, leftover_len);
    }
    
    int bytes;
    if (is_udp && addr) {
        bytes = recvfrom(fd, temp + leftover_len, MAX_BUFF - 1, 0, addr, addr_len);
    } else {
        bytes = recv(fd, temp + leftover_len, MAX_BUFF - 1, 0);
    }
    
    if (bytes <= 0) {
        if (bytes == 0 || (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            // Соединение закрыто или ошибка
            return (char*)-1;  // Специальное значение для обозначения закрытия
        }
        return NULL;
    }
    
    int total_len = leftover_len + bytes;
    temp[total_len] = '\0';
    
    // Расшифровываем если ключ есть
    if (key_exchanged) {
        xor_cipher((unsigned char*)temp, total_len);
    }
    
    char *newline_ptr = strchr(temp, '\n');
    if (newline_ptr == NULL) {
        // Нет полной строки - сохраняем в leftover
        memcpy(leftover, temp, total_len);
        leftover_len = total_len;
        return NULL;
    }
    
    // Есть полная строка
    int line_len = newline_ptr - temp;
    if (line_len >= *buffer_len) line_len = *buffer_len - 1;
    
    memcpy(buffer, temp, line_len);
    buffer[line_len] = '\0';
    *buffer_len = line_len;
    
    // Обрабатываем остаток после новой строки
    int rest_len = total_len - (line_len + 1);
    if (rest_len > 0) {
        memcpy(leftover, newline_ptr + 1, rest_len);
        leftover_len = rest_len;
    } else {
        leftover_len = 0;
    }
    
    return buffer;
}

// Отправка сообщения
int send_message(int fd, const char *msg, 
                 struct sockaddr *addr, socklen_t addr_len, int is_udp) {
    unsigned char buffer[MAX_BUFF * 2];
    int msg_len = strlen(msg);
    
    // Копируем сообщение
    memcpy(buffer, msg, msg_len);
    
    // Добавляем символ новой строки
    buffer[msg_len] = '\n';
    msg_len++;
    
    // Шифруем (включая новую строку)
    if (key_exchanged) {
        xor_cipher(buffer, msg_len);
    }
    
    int sent;
    if (is_udp && addr) {
        sent = sendto(fd, buffer, msg_len, 0, addr, addr_len);
    } else {
        sent = send(fd, buffer, msg_len, 0);
    }
    
    if (sent < 0 && (errno == EPIPE || errno == ECONNRESET)) {
        return -1;  // Соединение закрыто
    }
    
    return sent;
}

// Обмен ключом
int exchange_key(int fd, int is_server, 
                 struct sockaddr *peer_addr, socklen_t peer_addr_len, int is_udp) {
    char key_msg[32];
    
    if (is_server) {
        // Сервер генерирует и отправляет ключ
        global_key = generate_key();
        snprintf(key_msg, sizeof(key_msg), "KEY:%c\n", global_key);
        
        if (is_udp && peer_addr) {
            sendto(fd, key_msg, strlen(key_msg), 0, peer_addr, peer_addr_len);
        } else {
            send(fd, key_msg, strlen(key_msg), 0);
        }
        
        printf("[Ключ: %c]\n", global_key);
        key_exchanged = 1;
        return 0;
    } else {
        // Клиент ждет ключ
        char buffer[32];
        int bytes;
        struct timeval tv = {5, 0};
        fd_set read_fds;
        
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);
        
        if (select(fd + 1, &read_fds, NULL, NULL, &tv) > 0) {
            if (is_udp) {
                struct sockaddr_in from;
                socklen_t from_len = sizeof(from);
                bytes = recvfrom(fd, buffer, sizeof(buffer) - 1, 0, 
                                (struct sockaddr*)&from, &from_len);
                if (peer_addr && bytes > 0) {
                    memcpy(peer_addr, &from, sizeof(struct sockaddr_in));
                }
            } else {
                bytes = recv(fd, buffer, sizeof(buffer) - 1, 0);
            }
            
            if (bytes > 0) {
                buffer[bytes] = '\0';
                if (strncmp(buffer, "KEY:", 4) == 0) {
                    global_key = buffer[4];
                    printf("[Ключ: %c]\n", global_key);
                    key_exchanged = 1;
                    return 0;
                }
            }
        }
        printf("Ошибка обмена ключом\n");
        return -1;
    }
}

// Основная функция чата
int chat_loop(int fd, pid_t my_pid, const char *protocol,
              struct sockaddr *peer_addr, socklen_t peer_addr_len, int is_udp) {
    char input_buffer[MAX_BUFF] = {0};
    char read_buffer[MAX_BUFF * 2] = {0};
    int pos = 0;
    
    // Убеждаемся, что сокет в неблокирующем режиме
    set_nonblocking(fd);
    
    printf("\n=== Чат через %s === (PID: %d)\n", protocol, my_pid);
    if (key_exchanged) {
        printf("Ключ шифрования: '%c'\n", global_key);
    }
    printf("> ");
    fflush(stdout);
    
    while (1) {
        fd_set read_fds;
        struct timeval tv = {0, TIMEOUT_MS * 1000};
        
        FD_ZERO(&read_fds);
        FD_SET(0, &read_fds);
        FD_SET(fd, &read_fds);
        
        int max_fd = (fd > 0) ? fd : 0;
        if (select(max_fd + 1, &read_fds, NULL, NULL, &tv) < 0) {
            break;
        }
        
        // Чтение из сокета
        if (FD_ISSET(fd, &read_fds)) {
            struct sockaddr_in from;
            socklen_t from_len = sizeof(from);
            struct sockaddr *recv_addr = is_udp ? (struct sockaddr*)&from : NULL;
            socklen_t *recv_addr_len = is_udp ? &from_len : NULL;
            
            int buf_len = sizeof(read_buffer);
            char *line = read_line(fd, read_buffer, &buf_len, 
                                  recv_addr, recv_addr_len, is_udp);
            
            if (line == (char*)-1) {
                // Соединение закрыто
                printf("\nСоединение закрыто собеседником\n");
                break;
            }
            
            while (line != NULL && line != (char*)-1) {
                // Для UDP обновляем адрес собеседника
                if (is_udp && peer_addr && recv_addr) {
                    memcpy(peer_addr, recv_addr, sizeof(struct sockaddr_in));
                }
                
                // Пропускаем KEY сообщения
                if (strncmp(line, "KEY:", 4) != 0) {
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
                }
                
                buf_len = sizeof(read_buffer);
                line = read_line(fd, read_buffer, &buf_len, 
                               recv_addr, recv_addr_len, is_udp);
                
                if (line == (char*)-1) {
                    printf("\nСоединение закрыто собеседником\n");
                    break;
                }
            }
        }
        
        // Чтение с клавиатуры
        if (FD_ISSET(0, &read_fds)) {
            char c;
            if (read(0, &c, 1) == 1) {
                if (c == '\n') {
                    if (pos > 0) {
                        input_buffer[pos] = '\0';
                        
                        // Форматируем сообщение с PID
                        char msg_with_pid[MAX_BUFF * 2];
                        snprintf(msg_with_pid, sizeof(msg_with_pid), "%d:%s", my_pid, input_buffer);
                        
                        // Отправляем
                        int sent = send_message(fd, msg_with_pid, peer_addr, peer_addr_len, is_udp);
                        
                        if (sent < 0) {
                            printf("\r[Ошибка отправки: соединение закрыто]\n");
                            break;
                        }
                        
                        printf("\r[Вы] %s\n> ", input_buffer);
                        
                        memset(input_buffer, 0, MAX_BUFF);
                        pos = 0;
                        fflush(stdout);
                    } else {
                        printf("\n> ");
                        fflush(stdout);
                    }
                }
                else if ((c == 127 || c == '\b') && pos > 0) {
                    pos--;
                    input_buffer[pos] = '\0';
                    printf("\b \b");
                    fflush(stdout);
                }
                else if (c != '\n' && c != '\r' && pos < MAX_BUFF - 1) {
                    input_buffer[pos++] = c;
                    putchar(c);
                    fflush(stdout);
                }
            }
        }
    }
    
    return 0;
}

// TCP режим
int tcp_mode(char *ip, int port) {
    struct sockaddr_in addr;
    int is_server = (ip == NULL);
    pid_t my_pid = getpid();
    
    if (is_server) {
        // Сервер
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        
        if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("bind");
            return 1;
        }
        if (listen(server_fd, 1) < 0) {
            perror("listen");
            return 1;
        }
        
        printf("TCP сервер (PID: %d) на порту %d\n", my_pid, port);
        printf("Ожидание подключения...\n");
        
        struct sockaddr_in client;
        socklen_t client_len = sizeof(client);
        sock_fd = accept(server_fd, (struct sockaddr*)&client, &client_len);
        if (sock_fd < 0) {
            perror("accept");
            return 1;
        }
        
        printf("Клиент подключился: %s\n", inet_ntoa(client.sin_addr));
        
        if (exchange_key(sock_fd, 1, NULL, 0, 0) < 0) {
            return 1;
        }
        
        close(server_fd);
        server_fd = -1;
        
        return chat_loop(sock_fd, my_pid, "TCP", NULL, 0, 0);
    } else {
        // Клиент
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
            perror("inet_pton");
            return 1;
        }
        
        printf("Подключение к %s:%d...\n", ip, port);
        if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("connect");
            return 1;
        }
        
        printf("Подключено\n");
        
        if (exchange_key(sock_fd, 0, NULL, 0, 0) < 0) {
            return 1;
        }
        
        return chat_loop(sock_fd, my_pid, "TCP", NULL, 0, 0);
    }
}

// UDP режим
int udp_mode(char *ip, int port) {
    struct sockaddr_in addr;
    struct sockaddr_in peer_addr;
    socklen_t peer_len = sizeof(peer_addr);
    int is_server = (ip == NULL);
    pid_t my_pid = getpid();
    
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (is_server) {
        // Сервер
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("bind");
            return 1;
        }
        
        printf("UDP сервер (PID: %d) на порту %d\n", my_pid, port);
        printf("Ожидание первого сообщения...\n");
        
        // Ждем первое сообщение
        char buf[MAX_BUFF];
        peer_len = sizeof(peer_addr);
        int bytes = recvfrom(sock_fd, buf, sizeof(buf)-1, 0, 
                            (struct sockaddr*)&peer_addr, &peer_len);
        if (bytes < 0) {
            perror("recvfrom");
            return 1;
        }
        
        buf[bytes] = '\0';
        printf("Получено сообщение от %s:%d\n", 
               inet_ntoa(peer_addr.sin_addr), ntohs(peer_addr.sin_port));
        
        if (exchange_key(sock_fd, 1, (struct sockaddr*)&peer_addr, peer_len, 1) < 0) {
            return 1;
        }
        
        return chat_loop(sock_fd, my_pid, "UDP", 
                        (struct sockaddr*)&peer_addr, peer_len, 1);
    } else {
        // Клиент
        if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
            perror("inet_pton");
            return 1;
        }
        memcpy(&peer_addr, &addr, sizeof(peer_addr));
        
        printf("UDP клиент (PID: %d) готов к отправке на %s:%d\n", my_pid, ip, port);
        
        // Отправляем приветствие
        sendto(sock_fd, "HELLO", 5, 0, (struct sockaddr*)&peer_addr, peer_len);
        
        if (exchange_key(sock_fd, 0, (struct sockaddr*)&peer_addr, peer_len, 1) < 0) {
            return 1;
        }
        
        return chat_loop(sock_fd, my_pid, "UDP", 
                        (struct sockaddr*)&peer_addr, peer_len, 1);
    }
}

// ICMP режим
int icmp_mode(char *ip) {
    int is_server = (ip == NULL);
    pid_t my_pid = getpid();
    struct sockaddr_in peer_addr;
    socklen_t peer_len = sizeof(peer_addr);
    int seq = 1;
    int key_exchanged_local = 0;  // Локальная переменная для отслеживания обмена ключом

    // Создаем RAW сокет
    sock_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock_fd < 0) {
        perror("socket");
        printf("Для ICMP нужны root-права (запустите с sudo)\n");
        return 1;
    }

    // Устанавливаем таймаут для recvfrom
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (is_server) {
        // Сервер (ожидание)
        printf("ICMP сервер (PID: %d) ожидает...\n", my_pid);
        printf("Ожидание ICMP echo запроса...\n");

        // Ждем первый ping
        unsigned char buf[4096];
        struct sockaddr_in src;
        socklen_t src_len = sizeof(src);
        
        int bytes = recvfrom(sock_fd, buf, sizeof(buf), 0,
                            (struct sockaddr*)&src, &src_len);
        if (bytes < 0) {
            perror("recvfrom");
            return 1;
        }

        struct iphdr *ip_hdr = (struct iphdr *)buf;
        int ip_header_len = ip_hdr->ihl * 4;
        struct icmphdr *icmp = (struct icmphdr *)(buf + ip_header_len);

        // Проверяем, что это ICMP echo запрос
        if (icmp->type == ICMP_ECHO) {
            memcpy(&peer_addr, &src, sizeof(peer_addr));
            printf("Получен ping от %s\n", inet_ntoa(peer_addr.sin_addr));

            // Генерируем и отправляем ключ
            global_key = generate_key();
            key_exchanged = 1;
            key_exchanged_local = 1;
            printf("[Ключ шифрования: %c (ASCII: %d)]\n", global_key, global_key);

            // Отправляем ключ в ICMP echo reply
            unsigned char packet[4096];
            struct icmphdr *resp_icmp = (struct icmphdr *)packet;
            char *data = (char *)(packet + sizeof(struct icmphdr));

            memset(packet, 0, sizeof(packet));
            resp_icmp->type = ICMP_ECHOREPLY;
            resp_icmp->code = 0;
            resp_icmp->un.echo.id = htons(ICMP_ID);
            resp_icmp->un.echo.sequence = htons(seq++);

            // Формируем сообщение с ключом
            snprintf(data, 100, "KEY:%c", global_key);
            int data_len = strlen(data);
            
            // Вычисляем контрольную сумму
            resp_icmp->checksum = 0;
            resp_icmp->checksum = icmp_checksum((unsigned short *)resp_icmp,
                                               sizeof(struct icmphdr) + data_len);

            // Отправляем ответ
            int sent = sendto(sock_fd, packet, sizeof(struct icmphdr) + data_len, 0,
                            (struct sockaddr*)&peer_addr, sizeof(peer_addr));
            if (sent < 0) {
                perror("sendto");
                return 1;
            }
            printf("Ключ отправлен\n");
        }
    } else {
        // Клиент (отправка)
        memset(&peer_addr, 0, sizeof(peer_addr));
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_addr.s_addr = inet_addr(ip);

        printf("ICMP клиент (PID: %d) отправляет ping на %s\n", my_pid, ip);

        // Отправляем первый ping
        unsigned char packet[4096];
        struct icmphdr *icmp = (struct icmphdr *)packet;

        memset(packet, 0, sizeof(packet));
        icmp->type = ICMP_ECHO;
        icmp->code = 0;
        icmp->un.echo.id = htons(ICMP_ID);
        icmp->un.echo.sequence = htons(seq++);
        icmp->checksum = icmp_checksum((unsigned short *)icmp, sizeof(struct icmphdr));

        int sent = sendto(sock_fd, packet, sizeof(struct icmphdr), 0,
                         (struct sockaddr*)&peer_addr, sizeof(peer_addr));
        if (sent < 0) {
            perror("sendto");
            return 1;
        }
        printf("Ping отправлен, ожидание ответа с ключом...\n");

        // Ждем ключ
        int key_received = 0;
        for (int attempt = 0; attempt < 5 && !key_received; attempt++) {
            unsigned char buf[4096];
            struct sockaddr_in src;
            socklen_t src_len = sizeof(src);

            int bytes = recvfrom(sock_fd, buf, sizeof(buf), 0,
                                (struct sockaddr*)&src, &src_len);
            if (bytes > 0) {
                struct iphdr *ip_hdr = (struct iphdr *)buf;
                int ip_header_len = ip_hdr->ihl * 4;
                struct icmphdr *resp_icmp = (struct icmphdr *)(buf + ip_header_len);
                
                // Проверяем, что это ответ на наш ping
                if (resp_icmp->type == ICMP_ECHOREPLY && 
                    ntohs(resp_icmp->un.echo.id) == ICMP_ID) {
                    
                    char *data = (char *)(buf + ip_header_len + sizeof(struct icmphdr));
                    int data_len = bytes - ip_header_len - sizeof(struct icmphdr);
                    
                    if (data_len > 0 && strncmp(data, "KEY:", 4) == 0) {
                        global_key = data[4];
                        key_exchanged = 1;
                        key_exchanged_local = 1;
                        key_received = 1;
                        printf("[Ключ шифрования получен: %c (ASCII: %d)]\n", global_key, global_key);
                        break;
                    }
                }
            }
        }
        
        if (!key_received) {
            printf("Не удалось получить ключ от сервера\n");
            return 1;
        }
    }

    // Устанавливаем неблокирующий режим для сокета
    set_nonblocking(sock_fd);

    // Основной цикл чата
    char input_buffer[MAX_BUFF] = {0};
    char display_buffer[MAX_BUFF] = {0};  // Для отображения введенного текста
    int pos = 0;

    printf("\n=== Чат через ICMP === (PID: %d)\n", my_pid);
    printf("Ключ шифрования: '%c' (ASCII: %d)\n", global_key, global_key);
    printf("Для выхода используйте Ctrl+C\n");
    printf("> ");
    fflush(stdout);

    while (1) {
        fd_set read_fds;
        struct timeval tv_select;
        tv_select.tv_sec = 0;
        tv_select.tv_usec = TIMEOUT_MS * 1000;

        FD_ZERO(&read_fds);
        FD_SET(0, &read_fds);
        FD_SET(sock_fd, &read_fds);

        int max_fd = (sock_fd > 0) ? sock_fd : 0;
        if (select(max_fd + 1, &read_fds, NULL, NULL, &tv_select) < 0) {
            if (errno == EINTR) continue;
            break;
        }

        // Чтение ICMP пакетов
        if (FD_ISSET(sock_fd, &read_fds)) {
            unsigned char buf[4096];
            struct sockaddr_in src;
            socklen_t src_len = sizeof(src);

            int bytes = recvfrom(sock_fd, buf, sizeof(buf), 0,
                                (struct sockaddr*)&src, &src_len);

            if (bytes > 0) {
                struct iphdr *ip_hdr = (struct iphdr *)buf;
                int ip_header_len = ip_hdr->ihl * 4;
                struct icmphdr *icmp = (struct icmphdr *)(buf + ip_header_len);

                // Проверяем, что пакет принадлежит нашему чату
                if (ntohs(icmp->un.echo.id) == ICMP_ID) {
                    char *data = (char *)(buf + ip_header_len + sizeof(struct icmphdr));
                    int data_len = bytes - ip_header_len - sizeof(struct icmphdr);

                    if (data_len > 0) {
                        // Для сервера обновляем адрес собеседника при получении echo запроса
                        if (is_server && icmp->type == ICMP_ECHO) {
                            memcpy(&peer_addr, &src, sizeof(peer_addr));
                            peer_len = sizeof(peer_addr);
                        }

                        // Копируем данные во временный буфер для расшифровки
                        unsigned char temp_data[MAX_BUFF * 2];
                        memcpy(temp_data, data, data_len);
                        temp_data[data_len] = '\0';

                        // Расшифровываем если ключ есть
                        if (key_exchanged) {
                            xor_cipher(temp_data, data_len);
                        }

                        // Пропускаем KEY сообщения
                        if (strncmp((char*)temp_data, "KEY:", 4) != 0) {
                            char *colon = strchr((char*)temp_data, ':');
                            if (colon != NULL) {
                                *colon = '\0';
                                pid_t sender = atoi((char*)temp_data);
                                char *msg = colon + 1;

                                if (sender != my_pid && strlen(msg) > 0) {
                                    // Очищаем текущую строку ввода
                                    printf("\r\033[K");
                                    printf("[Собеседник %d] %s\n", sender, msg);
                                    printf("> %s", input_buffer);
                                    fflush(stdout);
                                }
                            }
                        }
                    }
                }
            }
        }

        // Чтение с клавиатуры
        if (FD_ISSET(0, &read_fds)) {
            char c;
            if (read(0, &c, 1) == 1) {
                if (c == '\n') {
                    if (pos > 0) {
                        input_buffer[pos] = '\0';

                        // Формируем сообщение с PID
                        char msg_with_pid[MAX_BUFF * 2];
                        int msg_len = snprintf(msg_with_pid, sizeof(msg_with_pid),
                                             "%d:%s", my_pid, input_buffer);

                        // Отображаем свое сообщение
                        printf("\r\033[K");
                        printf("[Вы] %s\n", input_buffer);

                        // Создаем ICMP пакет
                        unsigned char packet[4096];
                        struct icmphdr *icmp = (struct icmphdr *)packet;
                        char *data = (char *)(packet + sizeof(struct icmphdr));

                        memset(packet, 0, sizeof(packet));
                        
                        // Определяем тип ICMP пакета
                        if (is_server) {
                            icmp->type = ICMP_ECHOREPLY;  // Сервер отвечает
                        } else {
                            icmp->type = ICMP_ECHO;       // Клиент отправляет запрос
                        }
                        
                        icmp->code = 0;
                        icmp->un.echo.id = htons(ICMP_ID);
                        icmp->un.echo.sequence = htons(seq++);

                        // Копируем и шифруем сообщение
                        memcpy(data, msg_with_pid, msg_len);
                        if (key_exchanged) {
                            xor_cipher((unsigned char*)data, msg_len);
                        }

                        // Вычисляем контрольную сумму
                        icmp->checksum = 0;
                        icmp->checksum = icmp_checksum((unsigned short *)icmp,
                                                       sizeof(struct icmphdr) + msg_len);

                        // Отправляем пакет
                        int sent = sendto(sock_fd, packet, sizeof(struct icmphdr) + msg_len, 0,
                                        (struct sockaddr*)&peer_addr, sizeof(peer_addr));
                        
                        if (sent < 0) {
                            printf("Ошибка отправки ICMP пакета: %s\n", strerror(errno));
                        }

                        // Очищаем буфер ввода
                        memset(input_buffer, 0, MAX_BUFF);
                        pos = 0;
                        printf("> ");
                        fflush(stdout);
                    } else {
                        printf("\n> ");
                        fflush(stdout);
                    }
                }
                else if ((c == 127 || c == '\b') && pos > 0) {
                    pos--;
                    input_buffer[pos] = '\0';
                    printf("\b \b");
                    fflush(stdout);
                }
                else if (c >= 32 && c < 127 && pos < MAX_BUFF - 1) {  // Только печатные символы
                    input_buffer[pos++] = c;
                    putchar(c);
                    fflush(stdout);
                }
            }
        }
    }

    return 0;
}

// Инициализация терминала
int init_terminal(struct termios *old, struct termios *new) {
    tcgetattr(0, old);
    *new = *old;
    new->c_lflag &= ~(ICANON | ECHO);
    new->c_cc[VMIN] = 1;
    new->c_cc[VTIME] = 0;
    return tcsetattr(0, TCSANOW, new);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
        return 0;
    }
    
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_help();
        return 0;
    }
    
    // Сохраняем настройки терминала
    tcgetattr(0, &old_tio_global);
    old_tio_saved = 1;
    
    // Устанавливаем обработчики сигналов
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);
    
    // Настройка терминала
    struct termios new_tio;
    if (init_terminal(&old_tio_global, &new_tio) != 0) {
        fprintf(stderr, "Ошибка инициализации терминала\n");
        return 1;
    }
    
    char *proto = argv[1];
    int result = 0;
    
    if (strcmp(proto, "tcp") == 0) {
        if (argc == 3) {
            result = tcp_mode(NULL, atoi(argv[2]));
        } else if (argc == 4) {
            result = tcp_mode(argv[2], atoi(argv[3]));
        } else {
            print_help();
            result = 1;
        }
    }
    else if (strcmp(proto, "udp") == 0) {
        if (argc == 3) {
            result = udp_mode(NULL, atoi(argv[2]));
        } else if (argc == 4) {
            result = udp_mode(argv[2], atoi(argv[3]));
        } else {
            print_help();
            result = 1;
        }
    }
    else if (strcmp(proto, "icmp") == 0) {
        if (argc == 2) {
            result = icmp_mode(NULL);
        } else if (argc == 3) {
            result = icmp_mode(argv[2]);
        } else {
            print_help();
            result = 1;
        }
    }
    else {
        print_help();
        result = 1;
    }
    
    // Восстанавливаем терминал
    if (old_tio_saved) {
        tcsetattr(0, TCSANOW, &old_tio_global);
    }
    
    if (sock_fd > 0) close(sock_fd);
    if (server_fd > 0) close(server_fd);
    
    return result;
}
