#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "ioctl_crypto_wallet.h"

#define DEVICE_PATH "/dev/crypto_wallet"

static void print_help(void)
{
    printf("Available commands:\n");
    printf("  ADD <name> <key>      - add a new record\n");
    printf("  EDIT <id> <name> <key>- edit record\n");
    printf("  RM <id>               - remove record by ID\n");
    printf("  READ <id>             - show one record\n");
    printf("  LIST                  - show all records\n");
    printf("  EXIT                  - close program\n");
    printf("  HELP                  - this help\n");
}

// Разбивает строку на массив строк, разделитель пробел/табуляция.
// Возвращает количество аргументов (максимум 3). Аргументы без кавычек.
static int split_command(char *line, char *args[], int max_args)
{
    int count = 0;
    char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    while (*p && count < max_args) {
        args[count++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
        if (*p) {
            *p = '\0';
            p++;
            while (*p == ' ' || *p == '\t') p++;
        }
    }
    return count;
}

int main()
{
    int fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("open");
        fprintf(stderr, "Make sure module is loaded and /dev/%s exists\n", DEVICE_PATH);
        return 1;
    }

    char line[512];
    printf("Crypto Wallet client. Type HELP for commands.\n");
    while (1) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin))
            break;
        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) == 0) continue;

        // Разбор на аргументы
        char *args[4];
        int cnt = split_command(line, args, 4);
        if (cnt == 0) continue;

        if (strcmp(args[0], "ADD") == 0) {
            if (cnt < 3) {
                // Если аргументов меньше 3, недостающие считаем пустыми строками
                // Например: ADD name  -> key = ""
                //           ADD       -> name = "", key = ""
            }
            struct wallet_add data;
            memset(&data, 0, sizeof(data));
            // имя: если есть 2-й аргумент, берём его, иначе пустая строка
            if (cnt >= 2)
                strncpy(data.name, args[1], MAX_NAME_LEN - 1);
            else
                data.name[0] = '\0';
            // ключ: если есть 3-й аргумент, берём его, иначе пустая строка
            if (cnt >= 3)
                strncpy(data.key, args[2], MAX_KEY_LEN - 1);
            else
                data.key[0] = '\0';
            data.name[MAX_NAME_LEN-1] = '\0';
            data.key[MAX_KEY_LEN-1] = '\0';

            if (ioctl(fd, WALLET_ADD, &data) < 0) {
                perror("ioctl ADD");
            } else {
                if (data.result_id == -1)
                    printf("Added with errors, ID = -1\n");
                else
                    printf("Added successfully with ID: %d\n", data.result_id);
            }
        }
        else if (strcmp(args[0], "EDIT") == 0) {
            if (cnt < 2) {
                printf("Usage: EDIT <id> <new_name> <new_key>\n");
                continue;
            }
            struct wallet_edit data;
            memset(&data, 0, sizeof(data));
            data.id = atoi(args[1]);
            if (cnt >= 3)
                strncpy(data.new_name, args[2], MAX_NAME_LEN - 1);
            else
                data.new_name[0] = '\0';
            if (cnt >= 4)
                strncpy(data.new_key, args[3], MAX_KEY_LEN - 1);
            else
                data.new_key[0] = '\0';
            data.new_name[MAX_NAME_LEN-1] = '\0';
            data.new_key[MAX_KEY_LEN-1] = '\0';

            if (ioctl(fd, WALLET_EDIT, &data) < 0) {
                perror("ioctl EDIT");
            } else {
                if (data.result == -ENOENT)
                    printf("ID %d not found\n", data.id);
                else
                    printf("Edited successfully\n");
            }
        }
        else if (strcmp(args[0], "RM") == 0) {
            if (cnt < 2) {
                printf("Usage: RM <id>\n");
                continue;
            }
            struct wallet_rm data;
            data.id = atoi(args[1]);
            if (ioctl(fd, WALLET_RM, &data) < 0) {
                perror("ioctl RM");
            } else {
                if (data.result == -ENOENT)
                    printf("ID %d not found\n", data.id);
                else
                    printf("Removed ID %d\n", data.id);
            }
        }
        else if (strcmp(args[0], "READ") == 0) {
            if (cnt < 2) {
                printf("Usage: READ <id>\n");
                continue;
            }
            struct wallet_read data;
            memset(&data, 0, sizeof(data));
            data.id = atoi(args[1]);
            if (ioctl(fd, WALLET_READ, &data) < 0) {
                perror("ioctl READ");
            } else {
                if (data.found)
                    printf("ID: %d, NAME: %s, KEY: %s\n", data.id, data.name, data.key);
                else
                    printf("ID %d not found\n", data.id);
            }
        }
        else if (strcmp(args[0], "LIST") == 0) {
            char list_buf[WALLET_LIST_BUF_SIZE];
            if (ioctl(fd, WALLET_LIST, list_buf) < 0) {
                perror("ioctl LIST");
            } else {
                printf("%s", list_buf);
            }
        }
        else if (strcmp(args[0], "EXIT") == 0) {
            ioctl(fd, WALLET_EXIT);
            break;
        }
        else if (strcmp(args[0], "HELP") == 0) {
            print_help();
        }
        else {
            printf("Unknown command. Type HELP\n");
        }
    }
    close(fd);
    return 0;
}
