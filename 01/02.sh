#!/bin/bash

# Проверка: запущен ли от root
if [ "$(id -u)" -ne 0 ]; then
    echo "Ошибка: скрипт должен запускаться от root." >&2
    exit 1
fi

# Инициализация переменных
LOGIN=""
PASSWORD=""
IS_ROOT=0

# Парсинг аргументов
while getopts "l:p:r" opt; do
    case $opt in
        l)
            LOGIN="$OPTARG"
            ;;
        p)
            PASSWORD="$OPTARG"
            ;;
        r)
            IS_ROOT=1
            ;;
        \?)
            echo "Неверный параметр: -$OPTARG" >&2
            exit 2
            ;;
        :)
            echo "Параметр -$OPTARG требует аргумент." >&2
            exit 2
            ;;
    esac
done

# Обязательно должен быть логин
if [ -z "$LOGIN" ]; then
    echo "Ошибка: укажите логин через -l" >&2
    echo "Пример: $0 -l user [-p пароль] [-r]" >&2
    exit 3
fi

# Для суперпользователя обязателен пароль
if [ "$IS_ROOT" -eq 1 ] && [ -z "$PASSWORD" ]; then
    echo "Ошибка: для -r требуется пароль (-p)." >&2
    exit 4
fi

# Проверка существования пользователя
if grep -q "^${LOGIN}:" /etc/passwd; then
    echo "Ошибка: пользователь '$LOGIN' уже существует." >&2
    exit 5
fi

# Получение следующего UID (>=1000)
get_next_uid() {
    local max_uid=$(awk -F: '$3 >= 1000 && $3 < 65534 {print $3}' /etc/passwd | sort -n | tail -1)
    if [ -z "$max_uid" ]; then
        echo 1000
    else
        echo $((max_uid + 1))
    fi
}

# Безопасное добавление в passwd и shadow
add_user_to_files() {
    local username="$1"
    local uid="$2"
    local gid="$3"
    local home_dir="$4"
    local shell="$5"
    local pass_hash="$6"  # пустой = без пароля

    # Создание домашней директории
    mkdir -p "$home_dir"

    # Копируем содержимое /etc/skel в домашнюю папку
    cp -r /etc/skel/. "$home_dir" 2>/dev/null

    # Создаём базовые файлы, если их нет
    if [ ! -f "$home_dir/.bashrc" ]; then
        echo "source /etc/bash.bashrc" >> "$home_dir/.bashrc"
    fi

    if [ ! -f "$home_dir/.profile" ]; then
        echo "if [ -n \"\$BASH_VERSION\" ]; then" > "$home_dir/.profile"
        echo "    . ~/.bashrc" >> "$home_dir/.profile"
        echo "fi" >> "$home_dir/.profile"
    fi

    if [ ! -f "$home_dir/.bash_profile" ]; then
        echo ". ~/.profile" > "$home_dir/.bash_profile"
    fi

    # Устанавливаем владельца и права
    chown -R "${uid}:${gid}" "$home_dir"
    find "$home_dir" -type f -exec chmod 644 {} \;
    find "$home_dir" -type d -exec chmod 755 {} \;
    chmod 700 "$home_dir"  # сама домашняя папка — 700

    # Убеждаемся, что оболочка существует
    if [ ! -x "$shell" ]; then
        echo "Ошибка: оболочка '$shell' не существует или не исполняема." >&2
        exit 6
    fi

    # Резервные копии
    cp /etc/passwd /etc/passwd.tmp.$$
    cp /etc/shadow /etc/shadow.tmp.$$

    # Добавление в passwd
    echo "${username}:x:${uid}:${gid}:${username} user:${home_dir}:${shell}" >> /etc/passwd.tmp.$$

    # 🔥 КРИТИЧЕСКИ ВАЖНО: дата в /etc/shadow — в ДНЯХ с 1970-01-01
    LAST_CHANGE=$(( $(date +%s) / 86400 ))

    # Добавление в shadow
    if [ -n "$pass_hash" ]; then
        # С паролем
        echo "${username}:${pass_hash}:${LAST_CHANGE}:0:99999:7:::" >> /etc/shadow.tmp.$$
    else
        # Без пароля (заблокирован)
        echo "${username}:!:${LAST_CHANGE}:0:99999:7:::" >> /etc/shadow.tmp.$$
    fi

    # Атомарная замена
    mv /etc/passwd.tmp.$$ /etc/passwd
    mv /etc/shadow.tmp.$$ /etc/shadow

    # Устанавливаем правильные права
    chmod 644 /etc/passwd
    chmod 600 /etc/shadow

    # Проверяем, что пользователь теперь существует
    if ! getent passwd "$username" > /dev/null 2>&1; then
        echo "Предупреждение: пользователь не распознаётся системой." >&2
    fi
}

# === Определение параметров ===
SHELL="/bin/bash"

if [ "$IS_ROOT" -eq 1 ]; then
    # Суперпользователь (UID=0, GID=0)
    TARGET_UID=0
    TARGET_GID=0
    HOME_DIR="/root"
    HASH=$(openssl passwd -6 "$PASSWORD")
    add_user_to_files "$LOGIN" "$TARGET_UID" "$TARGET_GID" "$HOME_DIR" "$SHELL" "$HASH"
    echo "✅ Суперпользователь '$LOGIN' (UID=0) создан."

elif [ -n "$PASSWORD" ]; then
    # Обычный пользователь с паролем
    TARGET_UID=$(get_next_uid)
    TARGET_GID=$TARGET_UID
    HOME_DIR="/home/$LOGIN"
    HASH=$(openssl passwd -6 "$PASSWORD")
    add_user_to_files "$LOGIN" "$TARGET_UID" "$TARGET_GID" "$HOME_DIR" "$SHELL" "$HASH"
    echo "✅ Пользователь '$LOGIN' создан с паролем."

else
    # Обычный пользователь без пароля
    TARGET_UID=$(get_next_uid)
    TARGET_GID=$TARGET_UID
    HOME_DIR="/home/$LOGIN"
    add_user_to_files "$LOGIN" "$TARGET_UID" "$TARGET_GID" "$HOME_DIR" "$SHELL" ""
    echo "✅ Пользователь '$LOGIN' создан без пароля (вход по паролю невозможен)."
fi

exit 0
