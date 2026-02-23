#include <stdio.h>


typedef struct {
    int flag_file;      //флаг передачи имени файла
    char *name_file;    //название переданного файла
    int mode;           //наименованный или ненаименованный pipe
} Arguments;

int parse_arguments(int argc, char *argv[], Arguments *args) {
    args->name_file = NULL;
    args->flag_file = 0;
    args->mode = 0;

    if (argc == 1) {
        return -1;
    }

    int positional_count = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '-') {
            for (int j = 1; argv[i][j] != '\0'; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        return -1;
                    case 'n':
                        args->mode = 1;
                        break;
                    case 'u':
                        args->mode = 0;
                        break;
                    case 'f':
                        args->flag_file = 1;
                        break;
                    default:
                        printf("ERR: Неизвестная опция\n");
                        return -1;
                }
            }
        }
        else if (argv[i][0] == '-' && argv[i][1] == '-') {
            if (strcmp(argv[i], "--help") == 0) {
                return -1;
            } else if (strcmp(argv[i], "--name") == 0) {
                args->mode = 1;
            } else if (strcmp(argv[i], "--unname") == 0) {
                args->mode = 0;
            } else if (strcmp(argv[i], "--file") == 0) {
                args->flag_file = 1;
            } else {
                printf("ERR: Неизвестная опцияn");
                return -1;
            }
        }
        else {
            positional_count++;

            if (positional_count == 1) {
                args->name_file = argv[i];
            } else {
                printf("ERR: Лишний аргумент\n");
                return -1;
            }
        }
    }

    if (args->name_file == NULL && args->flag_file == 1) {
        return -1;
    }

    return 0;
}

void print_help(){
    fprintf(stdout, "Используйте: 02_pipe [ОПЦИИ]\n\n");
    fprintf(stdout, "\t-n|--name\t\t\t- Передача через наименованный pipe\n");
    fprintf(stdout, "\t-u|--unname\t\t\t- Передача через ненаименованный pipe\n");
    fprintf(stdout, "\t-f|--file <pipe_name>\t\t\t- Название файла наименованного pipi\n");
    fprintf(stdout, "\t-h|--help\t\t\t- Вывод этой справки\n");
}


