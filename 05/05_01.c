#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>

typedef struct {
    char *name;
    int delete;
    int create;
} Arguments;

void print_help(const char *program_name) {
    printf("Использование: 05_01 [ОПЦИИ] <Название файла>\n\n");
    printf("Опции:\n");
    printf("  -h, --help                        Показать эту справку\n");
    printf("  [-с, --create|-d, --delete]       Создать или удалить файл\n");
}

int parse_arguments(int argc, char *argv[], Arguments *args) {
    args->name = NULL;
    args->delete = 0;
    args->create = 0;

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
                    case 'c':
                        args->create = 1;
                        break;
                    case 'd':
                        args->delete = 1;
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
            } else if (strcmp(argv[i], "--create") == 0) {
                args->create = 1;
            } else if (strcmp(argv[i], "--delete") == 0) {
                args->delete = 1;
            } else {
                printf("ERR: Неизвестная опцияn");
                return -1;
            }
        }
        else {
            positional_count++;

            if (positional_count == 1) {
                args->name = argv[i];
            } else {
                printf("ERR: Лишний аргумент\n");
                return -1;
            }
        }
    }

    if (args->name == NULL ||
        (args->create == 0 && args->delete == 0) ||
        (args->create == 1 && args->delete == 1)) {
        return -1;
    }

    return 0;
}

void create_file(FILE **file_ptr, char *filename) {
    *file_ptr = fopen(filename, "w");

    if (*file_ptr == NULL) {
        printf("ERR: Не удалось создать файл\n");
    } else {
        printf("FILE: файл успешно создан\n");
    }
}

void delete_file(char *filename) {
    if (remove(filename) == 0) {
        printf("FILE: Файл удалён\n");
    } else {
        printf("Ошибка удаления файла\n");
    }
}

char* get_tild_path(const char *path) {
    if (path == NULL) return NULL;

    if (path[0] != '~' || (path[1] != '/' && path[1] != '\0')) {
        char *result = malloc(strlen(path) + 1);
        if (result) strcpy(result, path);
        return result;
    }

    char *home = getenv("HOME");
    if (home == NULL) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }

    if (home == NULL) {
        return NULL;
    }

    size_t home_len = strlen(home);
    size_t path_len = strlen(path + 1); // путь после ~
    size_t total_len = home_len + path_len + 1; // +1 для завершающего нуля

    char *full_path = malloc(total_len);
    if (full_path == NULL) {
        return NULL;
    }

    strcpy(full_path, home);

    if (path_len > 0 && path[1] == '/' && home[home_len - 1] == '/') {
        strcat(full_path, path + 2);
    } else if (path_len > 0) {
        strcat(full_path, path + 1);
    }

    return full_path;
}

int check_spec_symb(char *path) {
    if (path == NULL) return 0;

    for (size_t i = 0; i < strlen(path) ; i++) {
        char c = path[i];

        if (c == '\0' && i < strlen(path)) return 0;

        if (c == '\n' || c == '\t' || c == '\r') return 0;
    }

    return 1;
}

static char* normalize_path(const char* path) {
    if (path == NULL) {
        return NULL;
    }

    size_t len = strlen(path);
    char* normalized = malloc(len + 1);
    if (normalized == NULL) {
        return NULL;
    }

    char** components = malloc((len + 1) * sizeof(char*));
    if (components == NULL) {
        free(normalized);
        return NULL;
    }

    int depth = 0;
    const char* start = path;
    const char* end = path;

    while (*end != '\0') {
        if (*start == '/') {
            start++;
            end++;
            continue;
        }

        end = start;
        while (*end != '\0' && *end != '/') {
            end++;
        }

        size_t comp_len = end - start;
        if (comp_len == 0) {
            start = end;
            continue;
        }

        char* component = malloc(comp_len + 1);
        if (component == NULL) {
            for (int i = 0; i < depth; i++) {
                free(components[i]);
            }
            free(components);
            free(normalized);
            return NULL;
        }

        strncpy(component, start, comp_len);
        component[comp_len] = '\0';

        if (strcmp(component, ".") == 0) {
            free(component);
        } else if (strcmp(component, "..") == 0) {
            if (depth > 0) {
                depth--;
                free(components[depth]);
            }
            free(component);
        } else {
            components[depth++] = component;
        }

        start = end;
    }

    normalized[0] = '\0';

    if (path[0] == '/' || depth == 0) {
        strcpy(normalized, "/");
    }

    for (int i = 0; i < depth; i++) {
        if (i > 0 || path[0] == '/') {
            strcat(normalized, "/");
        }
        strcat(normalized, components[i]);
        free(components[i]);
    }

    free(components);

    if (normalized[0] == '\0') {
        char* root = malloc(2);
        if (root) {
            strcpy(root, "/");
        }
        free(normalized);
        return root;
    }

    return normalized;
}

char* make_absolute_path(const char* filepath) {
    if (filepath == NULL) {
        return NULL;
    }

    char *path = get_tild_path(filepath);
    if (path == NULL) {
        return NULL;
    }

    if (path[0] == '/') {
        // Замена strdup
        char* result = malloc(strlen(path) + 1);
        if (result == NULL) {
            free(path);
            return NULL;
        }
        strcpy(result, path);
        free(path);
        return result;
    }

    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        free(path);
        return NULL;
    }

    size_t cwd_len = strlen(cwd);
    size_t path_len = strlen(path);

    char* full_path = malloc(cwd_len + path_len + 2);
    if (full_path == NULL) {
        free(path);
        return NULL;
    }

    strcpy(full_path, cwd);
    if (cwd[cwd_len - 1] != '/' && path[0] != '/') {
        strcat(full_path, "/");
    }
    strcat(full_path, path);
    free(path);

    char* result = normalize_path(full_path);
    free(full_path);

    return result;
}

int check_file(char *path) {
    if (path == NULL) return 0;
    return (access(path, F_OK) == 0);
}

int check_dir(const char *path) {
    struct stat path_stat;
    return (path != NULL && stat(path, &path_stat) == 0 && S_ISDIR(path_stat.st_mode));
}

char* get_parent_dir(const char* path) {
    if (path == NULL) return NULL;
    
    char* last_slash = strrchr(path, '/');
    
    if (last_slash && last_slash != path) {
        size_t len = last_slash - path;
        char* result = malloc(len + 1);
        if (result == NULL) return NULL;
        memcpy(result, path, len);
        result[len] = '\0';
        return result;
    } else if (last_slash == path) {
        char* result = malloc(2);
        if (result == NULL) return NULL;
        result[0] = '/';
        result[1] = '\0';
        return result;
    }
}

int main(int argc, char *argv[]) {
    Arguments args;
    int result = parse_arguments(argc, argv, &args);

    if (result != 0) {
        print_help(argv[0]);
        return 0;
    }

    // Нормализуем путь
    if (check_spec_symb(args.name) == 0) {
        printf("ERR: Путь содержит запрещённые символы\n");
        return 0;
    }

    char *filepath = make_absolute_path(args.name);
    if (filepath == NULL) {
        printf("ERR: Не удалось преобразовать путь\n");
        return 0;
    }

    if (args.create == 1) {
        // Проверка существования файла
        char* parent_dir = get_parent_dir(filepath);
        if (parent_dir == NULL) {
            printf("ERR: Не удалось получить родительскую директорию\n");
            free(filepath);
            return 0;
        }
        
        if (check_dir(parent_dir) == 0) {
            printf("ERR: Нет родительской директории\n");
            free(parent_dir);
            free(filepath);
            return 0;
        }
        
        if (check_file(filepath) == 1) {
            printf("ERR: Файл уже существует\n");
            free(parent_dir);
            free(filepath);
            return 0;
        }
        
        FILE *new_file = NULL;
        create_file(&new_file, filepath);
        
        if (new_file != NULL) {
            fclose(new_file);
        }
        free(parent_dir);
        
    } else if (args.delete == 1) {
        if (check_file(filepath) == 0) {
            printf("ERR: Такого файла нет\n");
            free(filepath);
            return 0;
        }
        delete_file(filepath);
    }

    free(filepath);
    return 0;
}
