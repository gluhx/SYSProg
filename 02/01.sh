#!/bin/bash

#строгий режим
set -euo pipefail
#set -x

#функция для вывода справки
show_help() {
    echo "Использование: $0 <путь_к_конфигу> [ОПЦИИ]"
    echo ""
    echo "  -a, --algorithm ALG    Алгоритм: gzip, zstd или bzip2 (по умолчанию: gzip)"
    echo "  -h, --help"           
    exit 1
}

#проверяем естьли аргументы
if [[ $# -lt 1 ]]; then show_help; fi

#сохраняем первый аргумент - путь к config и удаляем его из списка
CONFIG_FILE="$1"; shift

#устанавливаем алгоритм для сжатия по умолчанию
COMPRESSION_CMD="gzip"


#парсим введённые аргументы
while [[ $# -gt 0 ]]; do
    case "$1" in
        -a|--algorithm)
            if [[ "$2" =~ ^(gzip|zstd|bzip2)$ ]]; then
                COMPRESSION_CMD="$2"
            else
                echo "ОШИБКА: неподдерживаемый алгоритм '$2'" >&2; show_help 1
            fi 
            shift 2 ;;
        -h|--help) show_help ;;
        *) echo "ОШИБКА: неизвестная опция '$1'" >&2; show_help;;
    esac
done


#проверяем конфигаруционный файл
if [[ ! -f "$CONFIG_FILE" ]]; then
    echo "ОШИБКА: файл конфигурации не найден: $CONFIG_FILE" >&2; exit 1
fi

if ! jq empty "$CONFIG_FILE" &>/dev/null; then
    echo "ОШИБКА: некорректный JSON в конфиге" >&2; exit 1
fi

#достаём параметры из конфига
ARCHIVE_NAME=$(jq -r '.archive_stored_name // "log_rotator"' "$CONFIG_FILE")
CLEANUP_POLICY=$(jq -r '.cleanup_policy // "keep_newest"' "$CONFIG_FILE")
NUM=$(jq -r '.n_to_keep // 10' "$CONFIG_FILE")
TARGET_DST_BASE=$(jq -r '.target_dst' "$CONFIG_FILE")

NUM_TO_KEEP=$(( NUM - 1 ))

#все архивы в директории сортируем согласно введённой политике
mapfile -t ALL_ARCHIVES < <(
    find "$TARGET_DST_BASE" -maxdepth 1 -type f \( -name "*.tar.gz" -o -name "*.tar.zst" -o -name "*.tar.bz2" \) -printf '%f\n' | \
    if [[ "$CLEANUP_POLICY" == "keep_newest" ]]; then
        sort 
    else
        sort -r
    fi
)

#вычисляем сколько архивов надо удалить
TOTAL=${#ALL_ARCHIVES[@]};
TO_DEL=$(( TOTAL - NUM_TO_KEEP > 0 ? TOTAL - NUM_TO_KEEP : 0 ))

#удаляем архивы
for ((i=0; i<TO_DEL; i++)); do rm -f "$TARGET_DST_BASE/${ALL_ARCHIVES[i]}"; done


#фрмируем временный архив .tar
TIMESTAMP=$(date '+%Y-%m-%d_%H-%M-%S')
ARCHIVE_DIR="${TARGET_DST_BASE}/${TIMESTAMP}_${ARCHIVE_NAME}"
ARCHIVE_TAR="${ARCHIVE_DIR}.tar"
#создаём временную директорию
mkdir -p "$ARCHIVE_DIR"

#проверяем, что целевая директория существует
if [[ ! -d "$TARGET_DST_BASE" ]]; then
    echo "ОШИБКА: директория target_dst не существует: $TARGET_DST_BASE" >&2; exit 1
fi

#преобразуем путь в абсолютный
TARGET_DST_BASE=$(realpath "$TARGET_DST_BASE")

#сохраняем пути до файлов в SOURCE_PATHS сразу проверяя их существование
mapfile -t SOURCE_PATHS < <(jq -r '.target_src[]' "$CONFIG_FILE" | while IFS= read -r p; do
    [[ -e "$p" ]] && realpath "$p"
done)

#проверяем существование хотя бы одного корректного пути
if [[ ${#SOURCE_PATHS[@]} -eq 0 ]]; then
    echo "ОШИБКА: не найдено ни одного корректного пути в target_src" >&2; exit 1
fi

#копируем во временную директорию каждый целевой файл
for src in "${SOURCE_PATHS[@]}"; do cp -rL "$src" "$ARCHIVE_DIR/"; done

#из подоболочки создаём .tar архив с содержимым временной папки
(
    cd "$(dirname "$ARCHIVE_DIR")" || exit 1
    tar -cf "$(basename "$ARCHIVE_TAR")" "$(basename "$ARCHIVE_DIR")"
)

#сжимаем архив по заданному алгоритму
"$COMPRESSION_CMD" "$ARCHIVE_TAR"
#удаляем временную папку
rm -rf "$ARCHIVE_DIR"


#очищаем файлы
for src in "${SOURCE_PATHS[@]}"; do
    if [[ -f "$src" ]]; then rm -f "$src"; touch "$src"
    elif [[ -d "$src" ]]; then find "$src" -mindepth 1 -delete; fi
done

echo "Ротация логов завершена. Архив: $(basename "${ARCHIVE_TAR}.${COMPRESSION_CMD}")"
