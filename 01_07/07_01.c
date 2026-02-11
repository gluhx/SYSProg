#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>
#include <ctype.h>
#include <errno.h>

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
    size_t path_len = strlen(path + 1);
    size_t total_len = home_len + path_len + 1;

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

int check_spec_symb(const char *path) {
    if (path == NULL) return 0;
    for (size_t i = 0; path[i] != '\0'; i++) {
        char c = path[i];
        if (c == '\n' || c == '\t' || c == '\r') return 0;
    }
    return 1;
}

static char* normalize_path(const char* path) {
    if (path == NULL) return NULL;

    size_t len = strlen(path);
    char* normalized = malloc(len + 1);
    if (!normalized) return NULL;

    char** components = malloc((len + 1) * sizeof(char*));
    if (!components) {
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
        while (*end != '\0' && *end != '/') end++;

        size_t comp_len = end - start;
        if (comp_len == 0) {
            start = end;
            continue;
        }

        char* component = malloc(comp_len + 1);
        if (!component) {
            for (int i = 0; i < depth; i++) free(components[i]);
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
                free(components[--depth]);
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
        if (i > 0 || path[0] == '/') strcat(normalized, "/");
        strcat(normalized, components[i]);
        free(components[i]);
    }

    free(components);

    if (normalized[0] == '\0') {
        free(normalized);
        char* root = malloc(2);
        if (root) strcpy(root, "/");
        return root;
    }

    return normalized;
}

char* make_absolute_path(const char* filepath) {
    if (filepath == NULL) return NULL;

    char *path = get_tild_path(filepath);
    if (!path) return NULL;

    if (path[0] == '/') {
        char* result = malloc(strlen(path) + 1);
        if (result) strcpy(result, path);
        free(path);
        return result;
    }

    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) {
        free(path);
        return NULL;
    }

    size_t cwd_len = strlen(cwd);
    size_t path_len = strlen(path);
    char* full_path = malloc(cwd_len + path_len + 2);
    if (!full_path) {
        free(path);
        return NULL;
    }

    strcpy(full_path, cwd);
    if (cwd[cwd_len - 1] != '/' && path[0] != '/') strcat(full_path, "/");
    strcat(full_path, path);
    free(path);

    char* result = normalize_path(full_path);
    free(full_path);
    return result;
}

int check_file(const char *path) {
    return path && (access(path, F_OK) == 0);
}

void swap_char(char *a, char *b) {
    char t = *a;
    *a = *b;
    *b = t;
}

void heapify_min_char(char arr[], int n, int i) {
    int smallest = i, l = 2*i+1, r = 2*i+2;
    if (l < n && arr[l] < arr[smallest]) smallest = l;
    if (r < n && arr[r] < arr[smallest]) smallest = r;
    if (smallest != i) {
        swap_char(&arr[i], &arr[smallest]);
        heapify_min_char(arr, n, smallest);
    }
}

void heapify_max_char(char arr[], int n, int i) {
    int largest = i, l = 2*i+1, r = 2*i+2;
    if (l < n && arr[l] > arr[largest]) largest = l;
    if (r < n && arr[r] > arr[largest]) largest = r;
    if (largest != i) {
        swap_char(&arr[i], &arr[largest]);
        heapify_max_char(arr, n, largest);
    }
}

void heap_sort_char(char arr[], int n, int is_max_sort) {
    for (int i = n/2 - 1; i >= 0; i--) {
        if (is_max_sort) heapify_max_char(arr, n, i);
        else heapify_min_char(arr, n, i);
    }
    for (int i = n-1; i > 0; i--) {
        swap_char(&arr[0], &arr[i]);
        if (is_max_sort) heapify_max_char(arr, i, 0);
        else heapify_min_char(arr, i, 0);
    }
}

char* read_full_file(const char *filename, int *len_out) {
    if (!len_out) {
        fprintf(stderr, "Ошибка: len_out не должен быть NULL\n");
        return NULL;
    }

    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Ошибка открытия файла '%s': %s\n", filename, strerror(errno));
        return NULL;
    }

    long total_chars = 0;
    char ch;
    while ((ch = (char)fgetc(f)) != EOF) {
        if (ch != '\n') {
            total_chars++;
        }
    }

    if (ferror(f)) {
        fclose(f);
        fprintf(stderr, "Ошибка чтения при подсчёте размера файла '%s'\n", filename);
        return NULL;
    }

    rewind(f);

    char *buf = malloc((size_t)total_chars + 1);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "Недостаточно памяти для буфера размером %ld байт\n", total_chars + 1);
        return NULL;
    }

    size_t i = 0;
    while ((ch = (char)fgetc(f)) != EOF) {
        if (ch != '\n') {
            buf[i++] = ch;
        }
    }

    if (ferror(f)) {
        free(buf);
        fclose(f);
        fprintf(stderr, "Ошибка чтения при копировании содержимого файла '%s'\n", filename);
        return NULL;
    }

    buf[i] = '\0';
    fclose(f);

    *len_out = (int)total_chars;
    return buf;
}

char* concat_remaining_args(int argc, char *argv[], int start, int *out_len) {
    if (start >= argc) {
        *out_len = 0;
        return strdup("");
    }

    int total = 0;
    for (int i = start; i < argc; i++) {
        total += strlen(argv[i]);
        if (i < argc - 1) total++;
    }

    char *res = malloc(total + 1);
    if (!res) {
        *out_len = 0;
        return NULL;
    }

    res[0] = '\0';
    for (int i = start; i < argc; i++) {
        if (i > start) strcat(res, " ");
        strcat(res, argv[i]);
    }

    *out_len = total;
    return res;
}

void print_and_save_result(const char *data, int len, int is_max_sort) {
    if (!data || len <= 0) return;

    char *sorted = malloc(len);
    if (!sorted) {
        fprintf(stderr, "Не удалось выделить память\n");
        return;
    }
    memcpy(sorted, data, len);
    heap_sort_char(sorted, len, is_max_sort);

    // Вывод в терминал: посимвольно
    for (int i = 0; i < len; i++) {
        putchar(sorted[i]);
    }
    putchar('\n');

    // Запись в файл
    FILE *f = fopen("output.txt", "w");
    if (f) {
        for (int i = 0; i < len; i++) {
            fputc(sorted[i], f);
        }
        fputc('\n', f);
        fclose(f);
        printf("Результат записан в output.txt\n");
    } else {
        perror("Не удалось записать output.txt");
    }

    free(sorted);
}


int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Использование:\n");
        printf("  %s [--max|--min] -f <путь>\n", argv[0]);
        printf("  %s [--max|--min] <аргументы...>\n", argv[0]);
        return 1;
    }

    int is_max_sort = 1;
    char *filepath = NULL;
    int data_start = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--max") == 0) {
            is_max_sort = 1;
            data_start = i + 1;
        } else if (strcmp(argv[i], "--min") == 0) {
            is_max_sort = 0;
            data_start = i + 1;
        } else if (strcmp(argv[i], "-f") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Ошибка: после -f должен идти путь\n");
                return 1;
            }
            filepath = argv[i + 1];
            data_start = -1;
            break;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Неизвестный параметр: %s\n", argv[i]);
            return 1;
        } else {
            data_start = i;
            break;
        }
    }

    char *data = NULL;
    int len = 0;

    if (filepath) {
        char *abs_path = make_absolute_path(filepath);
        if (!abs_path) {
            fprintf(stderr, "Не удалось построить абсолютный путь\n");
            return 1;
        }

        if (!check_spec_symb(abs_path)) {
            fprintf(stderr, "Путь содержит запрещённые символы (\\n, \\t и др.)\n");
            free(abs_path);
            return 1;
        }

        if (!check_file(abs_path)) {
            fprintf(stderr, "Файл не существует: %s\n", abs_path);
            free(abs_path);
            return 1;
        }

        data = read_full_file(abs_path, &len);
        free(abs_path);
        if (!data) return 1;

        printf("Считано %d байт из файла\n", len);

    } else {
        if (data_start < 0 || data_start >= argc) {
            fprintf(stderr, "Ошибка: не указаны данные\n");
            return 1;
        }

        data = concat_remaining_args(argc, argv, data_start, &len);
        if (!data) {
            fprintf(stderr, "Ошибка выделения памяти\n");
            return 1;
        }

        printf("Обработано %d символов из аргументов\n", len);
    }

    if (len == 0) {
        printf("(пусто)\n");
        free(data);
        return 0;
    }

    print_and_save_result(data, len, is_max_sort);

    free(data);
    return 0;
}
