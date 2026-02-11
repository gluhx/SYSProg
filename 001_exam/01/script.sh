#!/bin/bash

#set -x

#print_help{
#    echo "Команда: $0 <username> <message> [Опции]"
#    echo ""
#    echo "  ОПЦИИ:"
#    echo "  -h, --help  - вывод справки"
#}

#Проверка количества аргументов
if [ $# -ne 2 ]; then
    echo "Неверное количество аргументов\n"
    exit 1
fi 

USERNAME="$1"
MESSAGE="$2"

TERMINALS=$(who | grep "^$USERNAME " | awk '{print $2}')

if !TERMINALS; then
    echo "Пользователь в данный момент не в системе или ещё не создан\n"
    exit 1
fi 

for TERM in $TERMINALS; do
    echo "Отправка в $TERM..."
    echo "$MESSAGE" > "/dev/$TERM"
done
