#!/bin/bash

#set -x

# Используем несколько сервисов для надежности. Первый в списке — основной.
SERVICE="https://ifconfig.me"

IP=$(curl -s --max-time 5 "$SERVICE" 2>/dev/null)
if [ -z "$IP" ]; then
    echo "Ошибка: не удалось получить внешний IP-адрес ни от одного сервиса." >&2
    exit 1
fi

echo "Внешний IP-адрес (выходная нода): $IP"
