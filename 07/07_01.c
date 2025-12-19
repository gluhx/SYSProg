#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Функция для обмена элементов
void swap(int *a, int *b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

// Функция для преобразования в пирамиду (для сортировки по возрастанию)
void heapify_min(int arr[], int n, int i) {
    int smallest = i;
    int left = 2 * i + 1;
    int right = 2 * i + 2;

    if (left < n && arr[left] < arr[smallest])
        smallest = left;

    if (right < n && arr[right] < arr[smallest])
        smallest = right;

    if (smallest != i) {
        swap(&arr[i], &arr[smallest]);
        heapify_min(arr, n, smallest);
    }
}

// Функция для преобразования в пирамиду (для сортировки по убыванию)
void heapify_max(int arr[], int n, int i) {
    int largest = i;
    int left = 2 * i + 1;
    int right = 2 * i + 2;

    if (left < n && arr[left] > arr[largest])
        largest = left;

    if (right < n && arr[right] > arr[largest])
        largest = right;

    if (largest != i) {
        swap(&arr[i], &arr[largest]);
        heapify_max(arr, n, largest);
    }
}

// Пирамидальная сортировка (Heap Sort)
void heap_sort(int arr[], int n, int is_max_sort) {
    // Построение кучи (перегруппировка массива)
    for (int i = n / 2 - 1; i >= 0; i--) {
        if (is_max_sort)
            heapify_max(arr, n, i);
        else
            heapify_min(arr, n, i);
    }

    // Извлечение элементов из кучи
    for (int i = n - 1; i >= 0; i--) {
        swap(&arr[0], &arr[i]);
        
        if (is_max_sort)
            heapify_max(arr, i, 0);
        else
            heapify_min(arr, i, 0);
    }
}

// Функция для чтения массива из файла
int* read_array_from_file(const char *filename, int *size) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Ошибка: не удалось открыть файл %s\n", filename);
        return NULL;
    }

    // Сначала подсчитаем количество чисел
    int count = 0;
    int temp;
    while (fscanf(file, "%d", &temp) == 1) {
        count++;
    }
    
    if (count == 0) {
        printf("Ошибка: файл пуст или не содержит чисел\n");
        fclose(file);
        return NULL;
    }

    // Выделяем память под массив
    int *arr = (int*)malloc(count * sizeof(int));
    if (!arr) {
        printf("Ошибка выделения памяти\n");
        fclose(file);
        return NULL;
    }

    // Возвращаемся в начало файла и читаем числа
    rewind(file);
    for (int i = 0; i < count; i++) {
        fscanf(file, "%d", &arr[i]);
    }

    fclose(file);
    *size = count;
    return arr;
}

// Функция для записи массива в файл
void write_array_to_file(const char *filename, int arr[], int size) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        printf("Ошибка: не удалось создать файл %s\n", filename);
        return;
    }

    for (int i = 0; i < size; i++) {
        fprintf(file, "%d", arr[i]);
        if (i < size - 1) {
            fprintf(file, " ");
        }
    }

    fclose(file);
    printf("Результат записан в файл %s\n", filename);
}

// Функция для вывода массива на экран
void print_array(int arr[], int size) {
    printf("Массив (%d элементов): ", size);
    for (int i = 0; i < size; i++) {
        printf("%d ", arr[i]);
    }
    printf("\n");
}

// Функция для чтения массива из строки
int* read_array_from_string(const char *str, int *size) {
    // Копируем строку для безопасной работы с strtok
    char *input_copy = strdup(str);
    if (!input_copy) {
        printf("Ошибка выделения памяти\n");
        return NULL;
    }

    // Подсчитываем количество чисел
    int count = 0;
    char *token = strtok(input_copy, " ");
    while (token != NULL) {
        count++;
        token = strtok(NULL, " ");
    }

    if (count == 0) {
        free(input_copy);
        printf("Ошибка: строка не содержит чисел\n");
        return NULL;
    }

    // Выделяем память под массив
    int *arr = (int*)malloc(count * sizeof(int));
    if (!arr) {
        free(input_copy);
        printf("Ошибка выделения памяти\n");
        return NULL;
    }

    // Читаем числа из строки
    strcpy(input_copy, str); // Восстанавливаем исходную строку
    token = strtok(input_copy, " ");
    for (int i = 0; i < count && token != NULL; i++) {
        arr[i] = atoi(token);
        token = strtok(NULL, " ");
    }

    free(input_copy);
    *size = count;
    return arr;
}

int main(int argc, char *argv[]) {
    int *arr = NULL;
    int size = 0;
    int is_max_sort = 1; // По умолчанию сортировка по убыванию
    char *filename = NULL;
    char *input_string = NULL;

    // Обработка аргументов командной строки
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            filename = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--max") == 0) {
            is_max_sort = 1; // Сортировка по убыванию
        } else if (strcmp(argv[i], "--min") == 0) {
            is_max_sort = 0; // Сортировка по возрастанию
        } else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            input_string = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Использование:\n");
            printf("  %s -f <filename> [--max|--min]\n", argv[0]);
            printf("  %s --input \"числа через пробел\" [--max|--min]\n", argv[0]);
            printf("\nПараметры:\n");
            printf("  -f <filename>    Чтение массива из файла\n");
            printf("  --input \"строка\" Чтение массива из строки\n");
            printf("  --max            Сортировка по убыванию (по умолчанию)\n");
            printf("  --min            Сортировка по возрастанию\n");
            printf("  --help           Вывод этой справки\n");
            return 0;
        }
    }

    // Чтение данных
    if (filename) {
        arr = read_array_from_file(filename, &size);
    } else if (input_string) {
        arr = read_array_from_string(input_string, &size);
    } else {
        printf("Ошибка: не указан источник данных\n");
        printf("Используйте -f для чтения из файла или --input для чтения из строки\n");
        printf("Используйте --help для получения справки\n");
        return 1;
    }

    if (!arr) {
        return 1;
    }

    // Вывод исходного массива
    printf("Исходный массив:\n");
    print_array(arr, size);

    // Сортировка
    printf("Сортировка %s\n", is_max_sort ? "по убыванию (--max)" : "по возрастанию (--min)");
    heap_sort(arr, size, is_max_sort);

    // Вывод отсортированного массива
    printf("Отсортированный массив:\n");
    print_array(arr, size);

    // Запись результата в файл
    write_array_to_file("output.txt", arr, size);

    // Освобождение памяти
    free(arr);

    return 0;
}
