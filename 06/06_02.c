#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct user {
    int *id;
    char *login;
    char *password;
    struct user *next;
} user;

typedef struct node {
    user *current;
    struct node *next;
    struct node *prev;
} node;

typedef struct {
    node *head;
    node *tail;
    int count;
} reag_list;

reag_list * init() {
    reag_list *list = malloc(sizeof(reag_list));
    if (!list) {
        printf("Ошибка выделения памяти для структуры\n");
        return NULL;
    }
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
    return list;
}

// Вспомогательная функция для поиска максимального ID в списке
int find_max_id(reag_list *list) {
    if (list == NULL || list->count == 0) {
        return 0;
    }

    int max_id = 0;
    node *current = list->head;

    while (current != NULL) {
        if (current->current != NULL && current->current->id != NULL) {
            int current_id = *(current->current->id);
            if (current_id > max_id) {
                max_id = current_id;
            }
        }
        current = current->next;
    }

    return max_id;
}

// Вспомогательная функция для нахождения минимального свободного ID
int find_next_available_id(reag_list *list) {
    if (list == NULL || list->count == 0) {
        return 1; // Если список пуст, начинаем с 1
    }

    int max_id = find_max_id(list);

    // Проверяем есть ли дыры в ID (удаленные пользователи)
    for (int id = 1; id <= max_id; id++) {
        int found = 0;
        node *current = list->head;

        while (current != NULL) {
            if (current->current != NULL && current->current->id != NULL) {
                if (*(current->current->id) == id) {
                    found = 1;
                    break;
                }
            }
            current = current->next;
        }

        if (!found) {
            return id; // Нашли свободный ID
        }
    }

    return max_id + 1; // Все ID заняты, возвращаем следующий
}

// Функция поиска узла по порядковому номеру (индексу)
node* find_node_by_index(reag_list *list, int index) {
    if (list == NULL || index < 0 || index >= list->count) {
        return NULL;
    }

    node *current = list->head;
    int current_index = 0;

    while (current != NULL && current_index < index) {
        current = current->next;
        current_index++;
    }

    return current;
}

// Функция добавления элемента по порядковому номеру (индексу)
// Теперь ID генерируется автоматически
int add_user_at_index(reag_list *list, const char *login, const char *password, int index) {
    if (list == NULL || login == NULL || password == NULL || index < 0) {
        return 0; // Неудача
    }

    // Если индекс больше количества элементов, добавляем в конец
    if (index > list->count) {
        index = list->count;
    }

    // Генерируем следующий доступный ID
    int new_id = find_next_available_id(list);

    // Создаем нового пользователя
    user *new_user = malloc(sizeof(user));
    if (new_user == NULL) {
        printf("Ошибка выделения памяти для пользователя\n");
        return 0;
    }

    new_user->id = malloc(sizeof(int));
    if (new_user->id == NULL) {
        free(new_user);
        return 0;
    }
    *(new_user->id) = new_id; // Используем сгенерированный ID

    new_user->login = malloc(strlen(login) + 1);
    if (new_user->login == NULL) {
        free(new_user->id);
        free(new_user);
        return 0;
    }
    strcpy(new_user->login, login);

    new_user->password = malloc(strlen(password) + 1);
    if (new_user->password == NULL) {
        free(new_user->login);
        free(new_user->id);
        free(new_user);
        return 0;
    }
    strcpy(new_user->password, password);

    new_user->next = NULL;

    // Создаем новый узел
    node *new_node = malloc(sizeof(node));
    if (new_node == NULL) {
        printf("Ошибка выделения памяти для узла\n");
        free(new_user->password);
        free(new_user->login);
        free(new_user->id);
        free(new_user);
        return 0;
    }

    new_node->current = new_user;
    new_node->next = NULL;
    new_node->prev = NULL;

    // Если список пустой
    if (list->count == 0) {
        list->head = new_node;
        list->tail = new_node;
    }
    // Вставка в начало
    else if (index == 0) {
        new_node->next = list->head;
        list->head->prev = new_node;
        list->head = new_node;
    }
    // Вставка в конец
    else if (index == list->count) {
        new_node->prev = list->tail;
        list->tail->next = new_node;
        list->tail = new_node;
    }
    // Вставка в середину
    else {
        node *current = find_node_by_index(list, index);
        if (current == NULL) {
            free(new_node);
            free(new_user->password);
            free(new_user->login);
            free(new_user->id);
            free(new_user);
            return 0;
        }

        new_node->prev = current->prev;
        new_node->next = current;
        current->prev->next = new_node;
        current->prev = new_node;
    }

    list->count++;
    return 1; // Успех
}

// Функция перемещения элемента влево
int move_left(reag_list *list, int index) {
    if (list == NULL || index < 0 || index >= list->count || list->count <= 1) {
        return 0; // Неудача
    }

    // Если элемент уже первый, перемещаем в конец
    if (index == 0) {
        node *node_to_move = list->head;
        
        // Отсоединяем от начала
        list->head = node_to_move->next;
        list->head->prev = NULL;
        
        // Присоединяем в конец
        node_to_move->prev = list->tail;
        node_to_move->next = NULL;
        list->tail->next = node_to_move;
        list->tail = node_to_move;
        
        return 1;
    }
    
    // Обычный случай - меняем местами с предыдущим элементом
    node *node_to_move = find_node_by_index(list, index);
    node *prev_node = node_to_move->prev;
    
    if (prev_node == NULL) {
        return 0;
    }
    
    // Если есть элемент перед предыдущим
    if (prev_node->prev != NULL) {
        prev_node->prev->next = node_to_move;
    } else {
        list->head = node_to_move;
    }
    
    // Если есть элемент после перемещаемого
    if (node_to_move->next != NULL) {
        node_to_move->next->prev = prev_node;
    } else {
        list->tail = prev_node;
    }
    
    // Меняем местами узлы
    prev_node->next = node_to_move->next;
    node_to_move->prev = prev_node->prev;
    prev_node->prev = node_to_move;
    node_to_move->next = prev_node;
    
    return 1;
}

// Функция перемещения элемента вправо
int move_right(reag_list *list, int index) {
    if (list == NULL || index < 0 || index >= list->count || list->count <= 1) {
        return 0; // Неудача
    }

    // Если элемент уже последний, перемещаем в начало
    if (index == list->count - 1) {
        node *node_to_move = list->tail;
        
        // Отсоединяем от конца
        list->tail = node_to_move->prev;
        list->tail->next = NULL;
        
        // Присоединяем в начало
        node_to_move->next = list->head;
        node_to_move->prev = NULL;
        list->head->prev = node_to_move;
        list->head = node_to_move;
        
        return 1;
    }
    
    // Обычный случай - меняем местами со следующим элементом
    node *node_to_move = find_node_by_index(list, index);
    node *next_node = node_to_move->next;
    
    if (next_node == NULL) {
        return 0;
    }
    
    // Если есть элемент перед перемещаемым
    if (node_to_move->prev != NULL) {
        node_to_move->prev->next = next_node;
    } else {
        list->head = next_node;
    }
    
    // Если есть элемент после следующего
    if (next_node->next != NULL) {
        next_node->next->prev = node_to_move;
    } else {
        list->tail = node_to_move;
    }
    
    // Меняем местами узлы
    node_to_move->next = next_node->next;
    next_node->prev = node_to_move->prev;
    next_node->next = node_to_move;
    node_to_move->prev = next_node;
    
    return 1;
}

// Функция удаления элемента по порядковому номеру (индексу)
int remove_user_at_index(reag_list *list, int index) {
    if (list == NULL || index < 0 || index >= list->count) {
        return 0; // Неудача
    }

    node *node_to_remove = find_node_by_index(list, index);
    if (node_to_remove == NULL) {
        return 0;
    }

    int removed_id = -1;
    if (node_to_remove->current != NULL && node_to_remove->current->id != NULL) {
        removed_id = *(node_to_remove->current->id);
    }

    // Если удаляется единственный элемент
    if (list->count == 1) {
        list->head = NULL;
        list->tail = NULL;
    }
    // Если удаляется первый элемент
    else if (node_to_remove == list->head) {
        list->head = node_to_remove->next;
        list->head->prev = NULL;
    }
    // Если удаляется последний элемент
    else if (node_to_remove == list->tail) {
        list->tail = node_to_remove->prev;
        list->tail->next = NULL;
    }
    // Если удаляется элемент из середины
    else {
        node_to_remove->prev->next = node_to_remove->next;
        node_to_remove->next->prev = node_to_remove->prev;
    }

    // Освобождаем память пользователя
    if (node_to_remove->current != NULL) {
        if (node_to_remove->current->id != NULL) {
            free(node_to_remove->current->id);
        }
        if (node_to_remove->current->login != NULL) {
            free(node_to_remove->current->login);
        }
        if (node_to_remove->current->password != NULL) {
            free(node_to_remove->current->password);
        }
        free(node_to_remove->current);
    }

    free(node_to_remove);
    list->count--;

    if (removed_id != -1) {
        printf("Пользователь с ID %d удален\n", removed_id);
    }

    return 1; // Успех
}

// Функция поиска пользователя по логину
user* find_user_by_login(reag_list *list, const char *login) {
    if (list == NULL || login == NULL) {
        return NULL;
    }

    node *current = list->head;

    while (current != NULL) {
        if (current->current != NULL &&
            current->current->login != NULL &&
            strcmp(current->current->login, login) == 0) {
            return current->current;
        }
        current = current->next;
    }

    return NULL; // Пользователь не найден
}

// Функция получения пользователя по индексу
user* get_user_by_index(reag_list *list, int index) {
    node *node_ptr = find_node_by_index(list, index);
    if (node_ptr == NULL) {
        return NULL;
    }
    return node_ptr->current;
}

// Функция освобождения памяти пользователя
void free_user(user *u) {
    if (u != NULL) {
        if (u->id != NULL) free(u->id);
        if (u->login != NULL) free(u->login);
        if (u->password != NULL) free(u->password);
        free(u);
    }
}

// Функция освобождения всего списка
void free_list(reag_list *list) {
    if (list == NULL) return;

    node *current = list->head;
    while (current != NULL) {
        node *next = current->next;
        free_user(current->current);
        free(current);
        current = next;
    }

    free(list);
}

// Функция вывода одного пользователя (используется в print и search)
void print_user(user *u, int show_number) {
    if (u == NULL) {
        printf("NULL\n");
        return;
    }
    
    if (show_number) {
        printf("Пользователь #%d:\n", *(u->id));
    }
    
    if (u->id != NULL) {
        printf("  ID: %d\n", *(u->id));
    } else {
        printf("  ID: NULL\n");
    }

    if (u->login != NULL) {
        printf("  Логин: %s\n", u->login);
    } else {
        printf("  Логин: NULL\n");
    }

    if (u->password != NULL) {
        printf("  Пароль: %s\n", u->password);
    } else {
        printf("  Пароль: NULL\n");
    }
    
    printf("-------------------------------------\n");
}

// Функция вывода всего списка последовательно (без номеров пользователей)
void print_all_users(reag_list *list) {
    if (list == NULL) {
        printf("Список не инициализирован\n");
        return;
    }

    printf("Всего пользователей в списке: %d\n", list->count);
    printf("=====================================\n");

    node *current = list->head;

    while (current != NULL) {
        if (current->current != NULL) {
            print_user(current->current, 0); // 0 - не показывать номер пользователя
        } else {
            printf("NULL\n");
            printf("-------------------------------------\n");
        }
        current = current->next;
    }

    if (list->count == 0) {
        printf("Список пуст\n");
    }
    printf("---------------------------------\n");
}

// Измененная функция help для связанного списка
void print_help(void) {
    printf("Доступные команды для работы со связанным списком:\n");
    printf("  add <index> <login> <password> - добавить пользователя на указанную позицию\n");
    printf("                                  (ID присваивается автоматически)\n");
    printf("                                  если index > количества элементов - добавляется в конец\n");
    printf("  remove <index>                 - удалить пользователя с указанной позиции\n");
    printf("  get <index>                    - получить пользователя по индексу\n");
    printf("  search <login>                 - найти пользователя по логину\n");
    printf("  left <index>                   - переместить элемент влево (если крайний - в конец)\n");
    printf("  right <index>                  - переместить элемент вправо (если крайний - в начало)\n");
    printf("  print                          - вывести весь список\n");
    printf("  clear                          - очистить весь список\n");
    printf("  exit                           - завершить программу\n");
    printf("  help                           - показать справку\n\n");
}

int main() {
    reag_list *list = init();
    if (!list) return 1;

    printf("Связанный список инициализирован, элементов: %d\n", list->count);
    print_help();

    char command[16];
    char login_input[256];
    char pass_input[256];
    int index_input;

    while (1) {
        printf("> ");
        if (scanf("%15s", command) != 1) {
            while (getchar() != '\n');
            continue;
        }

        if (strcmp(command, "exit") == 0) {
            break;
        } else if (strcmp(command, "add") == 0) {
            if (scanf("%d %255s %255s", &index_input, login_input, pass_input) != 3) {
                printf("Ошибка: команда 'add' требует три аргумента: <index> <login> <password>\n");
                while (getchar() != '\n');
                continue;
            }
            if (!add_user_at_index(list, login_input, pass_input, index_input)) {
                printf("Ошибка при добавлении пользователя\n");
            }
        } else if (strcmp(command, "remove") == 0) {
            if (scanf("%d", &index_input) != 1) {
                printf("Ошибка: команда 'remove' требует один аргумент: <index>\n");
                while (getchar() != '\n');
                continue;
            }
            if (!remove_user_at_index(list, index_input)) {
                printf("Ошибка при удалении пользователя\n");
            }
        } else if (strcmp(command, "get") == 0) {
            if (scanf("%d", &index_input) != 1) {
                printf("Ошибка: команда 'get' требует один аргумент: <index>\n");
                while (getchar() != '\n');
                continue;
            }
            user *u = get_user_by_index(list, index_input);
            if (u != NULL && u->id && u->login && u->password) {
                printf("Пользователь #%d: ID: %d, login: %s, password: %s\n",
                       index_input, *(u->id), u->login, u->password);
            } else {
                printf("Пользователь с индексом %d не найден\n", index_input);
            }
        } else if (strcmp(command, "search") == 0) {
            if (scanf("%255s", login_input) != 1) {
                printf("Ошибка: после 'search' должен быть логин\n");
                while (getchar() != '\n');
                continue;
            }
            user *u = find_user_by_login(list, login_input);
            if (u != NULL && u->id && u->login && u->password) {
                printf("Найден пользователь:\n");
                print_user(u, 1); // 1 - показать номер пользователя (использует ID)
            } else {
                printf("Пользователь с логином '%s' не найден\n", login_input);
            }
        } else if (strcmp(command, "left") == 0) {
            if (scanf("%d", &index_input) != 1) {
                printf("Ошибка: команда 'left' требует один аргумент: <index>\n");
                while (getchar() != '\n');
                continue;
            }
            if (move_left(list, index_input)) {
                printf("Элемент с индексом %d перемещен влево\n", index_input);
            } else {
                printf("Ошибка при перемещении элемента\n");
            }
        } else if (strcmp(command, "right") == 0) {
            if (scanf("%d", &index_input) != 1) {
                printf("Ошибка: команда 'right' требует один аргумент: <index>\n");
                while (getchar() != '\n');
                continue;
            }
            if (move_right(list, index_input)) {
                printf("Элемент с индексом %d перемещен вправо\n", index_input);
            } else {
                printf("Ошибка при перемещении элемента\n");
            }
        } else if (strcmp(command, "print") == 0) {
            print_all_users(list);
        } else if (strcmp(command, "clear") == 0) {
            while (list->count > 0) {
                remove_user_at_index(list, 0);
            }
            printf("Список очищен\n");
        } else if (strcmp(command, "help") == 0) {
            print_help();
        } else {
            printf("Неизвестная команда: '%s'\n", command);
            printf("Введите 'help' для просмотра доступных команд\n");
        }

        while (getchar() != '\n');
    }

    free_list(list);
    printf("Память освобождена. Программа завершена.\n");
    return 0;
}
