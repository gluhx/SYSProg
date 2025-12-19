#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int *id;
    char *login;
    char *password;
} user;

typedef struct {
    user *top;
    int count;
} stack;


stack* init(void) {
    stack *stk = malloc(sizeof(stack));
    if (!stk) {
        printf("Ошибка выделения памяти для структуры стека\n");
        return NULL;
    }
    stk->top = NULL;
    stk->count = 0;
    return stk;
}

int free_user(user *u) {
    if (!u) return 0;
    free(u->id);
    free(u->login);
    free(u->password);
    u->id = NULL;
    u->login = NULL;
    u->password = NULL;
    return 1;
}

int push(stack *stk, int id, const char *login, const char *password) {
    if (!stk || !login || !password) return 0;

    user *new_top = realloc(stk->top, (stk->count + 1) * sizeof(user));
    if (!new_top) {
        printf("Ошибка выделения памяти при добавлении элемента\n");
        return 0;
    }
    stk->top = new_top;

    int i = stk->count;
    user *u = &stk->top[i];

    u->id = malloc(sizeof(int));
    if (!u->id) return 0;
    *u->id = id;

    u->login = strdup(login);
    if (!u->login) { free(u->id); return 0; }

    u->password = strdup(password);
    if (!u->password) { free(u->id); free(u->login); return 0; }

    stk->count++;
    return 1;
}

user copy_user(const user *src) {
    if (!src) {
        user empty = {NULL, NULL, NULL};
        return empty;
    }
    user dst = {NULL, NULL, NULL};

    if (src->id) {
        dst.id = malloc(sizeof(int));
        if (dst.id) *dst.id = *src->id;
    }
    if (src->login) {
        dst.login = strdup(src->login);
    }
    if (src->password) {
        dst.password = strdup(src->password);
    }

    if ((!src->id || dst.id) && (!src->login || dst.login) && (!src->password || dst.password)) {
        return dst;
    }

    free_user(&dst);
    user empty = {NULL, NULL, NULL};
    return empty;
}

user pop(stack *stk) {
    if (!stk || stk->count == 0) {
        user empty = {NULL, NULL, NULL};
        return empty;
    }

    stk->count--;
    user result = copy_user(&stk->top[stk->count]);
    free_user(&stk->top[stk->count]);

    if (stk->count == 0) {
        free(stk->top);
        stk->top = NULL;
    } else {
        user *new_top = realloc(stk->top, stk->count * sizeof(user));
        if (new_top) stk->top = new_top;
    }

    return result;
}

user peek(stack *stk) {
    if (!stk || stk->count == 0) {
        user empty = {NULL, NULL, NULL};
        return empty;
    }
    return copy_user(&stk->top[stk->count - 1]);
}

void freeStack(stack *stk) {
    if (!stk) return;
    for (int i = 0; i < stk->count; i++) {
        free_user(&stk->top[i]);
    }
    free(stk->top);
    free(stk);
}

user search(stack *stk, const char *login) {
    if (!stk || !login) {
        user empty = {NULL, NULL, NULL};
        return empty;
    }

    for (int i = 0; i < stk->count; i++) {
        if (stk->top[i].login && strcmp(stk->top[i].login, login) == 0) {
            return copy_user(&stk->top[i]);
        }
    }

    user empty = {NULL, NULL, NULL};
    return empty;
}

void print_help(void) {
    printf("Доступные команды:\n");
    printf("  push <login> <password> - добавить пользователя в стек\n");
    printf("  pop                     - извлечь пользователя из стека\n");
    printf("  peek                    - посмотреть верхний элемент\n");
    printf("  search <login>          - найти пользователя по логину\n");
    printf("  exit                    - завершить программу\n");
    printf("  help                    - показать справку\n\n");
}

int main() {
    stack *stk = init();
    if (!stk) return 1;

    print_help();

    char command[16];
    char login_input[256];
    char pass_input[256];

    while (1) {
        printf("> ");
        if (scanf("%15s", command) != 1) {
            while (getchar() != '\n');
            continue;
        }

        if (strcmp(command, "exit") == 0) {
            break;

        } else if (strcmp(command, "push") == 0) {
            if (scanf("%255s %255s", login_input, pass_input) != 2) {
                printf("Ошибка: команда 'push' требует два аргумента: <login> <password>\n");
                while (getchar() != '\n');
                continue;
            }
            if (!push(stk, stk->count, login_input, pass_input)) {
                printf("Ошибка при добавлении элемента\n");
            }

        } else if (strcmp(command, "pop") == 0) {
            user u = pop(stk);
            if (u.id && u.login && u.password) {
                printf("ID: %d, login: %s, password: %s\n", *u.id, u.login, u.password);
                free_user(&u);
            } else {
                printf("Стек пуст\n");
            }

        } else if (strcmp(command, "peek") == 0) {
            user u = peek(stk);
            if (u.id && u.login && u.password) {
                printf("ID: %d, login: %s, password: %s\n", *u.id, u.login, u.password);
                free_user(&u);
            } else {
                printf("Стек пуст\n");
            }

        } else if (strcmp(command, "search") == 0) {
            if (scanf("%255s", login_input) != 1) {
                printf("Ошибка: после 'search' должен быть логин\n");
                while (getchar() != '\n');
                continue;
            }
            user u = search(stk, login_input);
            if (u.id && u.login && u.password) {
                printf("ID: %d, login: %s, password: %s\n", *u.id, u.login, u.password);
                free_user(&u);
            } else {
                printf("Пользователь с логином '%s' не найден\n", login_input);
            }

        } else if (strcmp(command, "help") == 0) {
            print_help();

        } else {
            printf("Неизвестная команда: '%s'\n", command);
            printf("Введите 'help' для просмотра доступных команд\n");
        }

        while (getchar() != '\n');
    }

    freeStack(stk);
    printf("Память освобождена. Программа завершена.\n");
    return 0;
}
