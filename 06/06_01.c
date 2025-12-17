#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int *data;   
    int top;     
    int capacity;
} stack;

stack* init() {
    stack *stk = (stack*)malloc(sizeof(stack));
    if (stk == NULL) {
        printf("Ошибка выделения памяти для структуры стека\n");
        return NULL;
    }

    stk->capacity = 10;

    stk->data = (int*)malloc(stk->capacity * sizeof(int));
    if (stk->data == NULL) {
        printf("Ошибка выделения памяти для данных стека\n");
        free(stk);
        return NULL;
    }

    stk->top = -1;

    return stk;
}

int increaseCapacity(stack *stk) {
    if (stk == NULL) {
        return 0;
    }
    
    int newCapacity = stk->capacity + 10;
    int *newData = (int*)realloc(stk->data, newCapacity * sizeof(int));
    
    if (newData == NULL) {
        printf("Ошибка увеличения емкости стека\n");
        return 0;
    }
    
    stk->data = newData;
    stk->capacity = newCapacity;
    return 1;
}

int push(stack *stk, int value) {
    if (stk == NULL) {
        return 0;
    }
    
    if (stk->top >= stk->capacity - 1) {
        if (!increaseCapacity(stk)) {
            printf("Не удалось увеличить емкость стека\n");
            return 0;
        }
    }
    
    stk->top++;
    stk->data[stk->top] = value;
    return 1;
}

int pop(stack *stk, int *value) {
    if (stk == NULL || stk->top < 0) {
        printf("Стек пуст или не инициализирован\n");
        return 0;
    }
    
    *value = stk->data[stk->top];
    stk->top--;
    return 1;
}

int isEmpty(stack *stk) {
    return (stk == NULL || stk->top < 0);
}

int peek(stack *stk, int *value) {
    if (stk == NULL || stk->top < 0) {
        printf("Стек пуст или не инициализирован\n");
        return 0;
    }
    
    *value = stk->data[stk->top];
    return 1;
}

void freeStack(stack *stk) {
    if (stk != NULL) {
        if (stk->data != NULL) {
            free(stk->data);
        }
        free(stk);
    }
}


int main() {
    stack *stk = init();
    if (stk == NULL) {
        return 1;
    }

    char command[10];
    int value, result;

    printf("Стек инициализирован с емкостью %d\n", stk->capacity);
    printf("Доступные команды:\n");
    printf("  push <число>  - добавить число в стек\n");
    printf("  pop           - извлечь число из стека\n");
    printf("  peek          - посмотреть верхний элемент\n");
    printf("  isEmpty       - проверить, пуст ли стек\n");
    printf("  print         - вывести все элементы стека\n");
    printf("  exit          - завершить программу\n");
    printf("  help          - показать справку\n\n");

    while (1) {
        printf("> ");

        // Считываем команду
        if (scanf("%9s", command) != 1) {
            printf("Ошибка чтения команды\n");
            while (getchar() != '\n'); // Очистка буфера
            continue;
        }

        // Команда exit
        if (strcmp(command, "exit") == 0) {
            printf("Завершение программы...\n");
            break;
        }

        // Команда push
        else if (strcmp(command, "push") == 0) {
            if (scanf("%d", &value) != 1) {
                printf("Ошибка: после 'push' должно быть целое число\n");
                while (getchar() != '\n'); // Очистка буфера
                continue;
            }

            if (push(stk, value)) {
                printf("Добавлено: %d (в стеке %d элементов, емкость: %d)\n",
                       value, stk->top + 1, stk->capacity);
            } else {
                printf("Ошибка при добавлении элемента\n");
            }
        }

        // Команда pop
        else if (strcmp(command, "pop") == 0) {
            if (pop(stk, &value)) {
                printf("Извлечено: %d (в стеке осталось %d элементов)\n",
                       value, stk->top + 1);
            } else {
                printf("Не удалось извлечь элемент\n");
            }
        }

        // Команда peek
        else if (strcmp(command, "peek") == 0) {
            if (peek(stk, &value)) {
                printf("Верхний элемент: %d\n", value);
            } else {
                printf("Стек пуст\n");
            }
        }

        // Команда isEmpty
        else if (strcmp(command, "isEmpty") == 0) {
            if (isEmpty(stk)) {
                printf("Стек пуст\n");
            } else {
                printf("Стек не пуст (содержит %d элементов)\n", stk->top + 1);
            }
        }

        // Команда print
        else if (strcmp(command, "print") == 0) {
            if (isEmpty(stk)) {
                printf("Стек пуст\n");
            } else {
                printf("Содержимое стека (сверху вниз):\n");
                for (int i = stk->top; i >= 0; i--) {
                    printf("  [%d] = %d\n", stk->top - i, stk->data[i]);
                }
                printf("Всего элементов: %d\n", stk->top + 1);
            }
        }

        // Команда help
        else if (strcmp(command, "help") == 0) {
            printf("Доступные команды:\n");
            printf("  push <число>  - добавить число в стек\n");
            printf("  pop           - извлечь число из стека\n");
            printf("  peek          - посмотреть верхний элемент\n");
            printf("  isEmpty       - проверить, пуст ли стек\n");
            printf("  print         - вывести все элементы стека\n");
            printf("  exit          - завершить программу\n");
            printf("  help          - показать эту справку\n");
        }

        // Неизвестная команда
        else {
            printf("Неизвестная команда: '%s'\n", command);
            printf("Введите 'help' для просмотра доступных команд\n");
        }

        // Очистка буфера ввода
        while (getchar() != '\n');
    }

    freeStack(stk);
    printf("Память освобождена. Программа завершена.\n");

    return 0;
}
