#!/bin/bash

#включаем режим отладки
#set -x

#функция для вывода справки
show_help() {
    echo "Использование: $0 [ОПЦИИ]"
    echo ""
    echo "  -t, --time <time>     Время: время через которое после старта будет остановлен процесс (по умолчанию 5 секунд)"
    echo "  -h, --help"           
    exit 1
}

#функция с процессом
process() {
    echo ""
    echo "Beging process"

    echo "I am doing something"

    PROCESS_TIME=1

    while true; do 
        echo "I am alive for $PROCESS_TIME seconds"
        ((PROCESS_TIME++))
        sleep 1
    done
}

#устанавливаем время по умолчанию
TIME=5

#выводим лог о начале работы скрипта
echo "START"

#парсим введённые аргументы
while [[ $# -gt 0 ]]; do
    case "$1" in
        -t|--time)
            TIME="$2"
            TIME_FLAG=false
            shift 2 ;;
        -h|--help) show_help ;;
        *) echo "ОШИБКА: неизвестная опция '$1'" >&2; show_help;;
    esac
done

if [[ "$TIME_FLAG" == true ]]; then
    echo ""
    echo "Вы не выбрали опцию time - по умолчанию 5 с"
else
    echo ""
    echo "Вы выбрали опцию time равной $TIME с"
fi

process &
PID=$!

sleep "$TIME"

echo ""
echo "Killing process with PID: $PID"

kill -15 "$PID"

echo ""
echo "FINISH"
