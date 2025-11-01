#!/bin/bash

# Функция обработки пришедшего сигнала
signal_catched(){
    echo "Signal $1 $2 catched."
    if [[ $1 == 5 ]]; then
        exit 0
    fi
}

# Проверяем количество сигналов в системе
SIG_COUNT=32
if kill -l | grep -q 128; then
    SIG_COUNT=128
elif kill -l | grep -q 64; then
    SIG_COUNT=64
fi

# Добавляем отслеживание всех сигналов кроме KILL и STOP
for (( i=1; i<=SIG_COUNT; i++ )); do
    SIG_CATCHED=$(kill -l "$i" 2>/dev/null)
    if [[ -n "$SIG_CATCHED" ]]; then
        if [[ "$SIG_CATCHED" != "KILL" && "$SIG_CATCHED" != "STOP"  && "$SIG_CATCHED" != "CHLD" ]]; then
            trap "signal_catched $i $SIG_CATCHED" "$SIG_CATCHED"
        fi
    fi 
done

#чтобы убрать ограмный вывод в начале
trap "signal_catched 17 CHLD" CHLD

#прочерка SIGCHLD
sleep 2&

echo "START. Script PID: $$"

while true; do :; done 
