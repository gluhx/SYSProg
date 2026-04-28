// SPDX-License-Identifier: GPL-2.0+
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/string.h>

// Define's
#define DEV_NAME "crypto_wallet"
#define DEV_CLASS "crypto_wallet_class"
#define MAX_NOTE 100
#define KEY_LEN 256
#define NAME_LEN 256
#define BUFFER_SIZE 4096

// Настройки модуля
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gluhx");
MODULE_DESCRIPTION("Crypto Wallet with authentication");

// Прототипы функций
static int dev_open(struct inode *inode, struct file *filp);
static int dev_release(struct inode *inode, struct file *filp);
static ssize_t dev_read(struct file *filp, char __user *buff, size_t count, loff_t *f_pos);
static ssize_t dev_write(struct file *filp, const char __user *buff, size_t count, loff_t *f_pos);
void handler(int error);
static char *get_next_token(char *str, char **saveptr);
static void process_command(const char *cmd_str);
static void cmd_add(char *arg1, char *arg2);
static void cmd_edit(char *id_str, char *arg1, char *arg2);
static void cmd_rm(char *id_str);
static void cmd_read(char *id_str);
static void cmd_list(void);
static void cmd_exit(void);
static void cmd_help(void);

// Структура записи
typedef struct {
    int id;
    char name[NAME_LEN];
    char key[KEY_LEN];
} Note;

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = dev_open,
    .release = dev_release,
    .read = dev_read,
    .write = dev_write,
};

// Глобальные переменные
static struct class* wallet_class = NULL;
static struct cdev wallet_cdev;
static struct mutex dev_mutex;
static dev_t dev_num;

static Note notes[MAX_NOTE];
static int next_id = 1;
static int record_count = 0;

static char *dev_buf;
static int buf_len = 0;

// ---------- Реализация основных функций устройства ----------
static int dev_open(struct inode *inode, struct file *filp)
{
    try_module_get(THIS_MODULE);
    printk(KERN_INFO "Crypto_Wallet: device opened\n");
    return 0;
}

static int dev_release(struct inode *inode, struct file *filp)
{
    module_put(THIS_MODULE);
    printk(KERN_INFO "Crypto_Wallet: device closed\n");
    return 0;
}

static ssize_t dev_read(struct file *filp, char __user *buff, size_t count, loff_t *f_pos)
{
    ssize_t bytes_to_copy;
    ssize_t bytes_copied;

    if (mutex_lock_interruptible(&dev_mutex))
        return -ERESTARTSYS;

    if (*f_pos >= buf_len) {
        mutex_unlock(&dev_mutex);
        return 0;
    }

    bytes_to_copy = min_t(ssize_t, count, buf_len - *f_pos);
    if (copy_to_user(buff, dev_buf + *f_pos, bytes_to_copy)) {
        mutex_unlock(&dev_mutex);
        return -EFAULT;
    }

    *f_pos += bytes_to_copy;
    bytes_copied = bytes_to_copy;

    mutex_unlock(&dev_mutex);
    return bytes_copied;
}

static ssize_t dev_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    char *kernel_buf;

    if (count >= BUFFER_SIZE) {
        printk(KERN_WARNING "Crypto_Wallet: command too long\n");
        return -EINVAL;
    }

    kernel_buf = kmalloc(count + 1, GFP_KERNEL);
    if (!kernel_buf)
        return -ENOMEM;

    if (copy_from_user(kernel_buf, buf, count)) {
        kfree(kernel_buf);
        return -EFAULT;
    }
    kernel_buf[count] = '\0';

    printk(KERN_INFO "Crypto_Wallet: received command: %s\n", kernel_buf);
    process_command(kernel_buf);

    kfree(kernel_buf);
    return count;
}

// ---------- Парсер команд ----------
static char *get_next_token(char *str, char **saveptr)
{
    if (str != NULL) {
        *saveptr = str;
    } else {
        if (*saveptr == NULL)
            return NULL;
        str = *saveptr;
    }

    // Пропускаем пробелы и табуляции
    while (*str == ' ' || *str == '\t')
        str++;
    if (*str == '\0') {
        *saveptr = NULL;
        return NULL;
    }

    char *start = str;
    while (*str != ' ' && *str != '\t' && *str != '\0' && *str != '\n')
        str++;

    if (*str != '\0') {
        *str = '\0';
        *saveptr = str + 1;
    } else {
        *saveptr = NULL;
    }
    return start;
}

static void process_command(const char *cmd_str)
{
    char *cmd_copy;
    char *saveptr = NULL;
    char command[16] = {0};

    cmd_copy = kstrdup(cmd_str, GFP_KERNEL);
    if (!cmd_copy) {
        snprintf(dev_buf, BUFFER_SIZE, "Error: memory allocation failed\n");
        buf_len = strlen(dev_buf);
        return;
    }

    // Убираем символ новой строки
    size_t len = strlen(cmd_copy);
    if (len > 0 && cmd_copy[len-1] == '\n')
        cmd_copy[len-1] = '\0';

    char *token_cmd = get_next_token(cmd_copy, &saveptr);
    if (token_cmd)
        strncpy(command, token_cmd, sizeof(command) - 1);

    // Параметры команд
    char *arg1 = get_next_token(NULL, &saveptr);
    char *arg2 = get_next_token(NULL, &saveptr);

    mutex_lock(&dev_mutex);

    if (strcmp(command, "ADD") == 0) {
        cmd_add(arg1, arg2);
    } else if (strcmp(command, "EDIT") == 0) {
        char *arg3 = get_next_token(NULL, &saveptr);
        cmd_edit(arg1, arg2, arg3);
    } else if (strcmp(command, "RM") == 0) {
        cmd_rm(arg1);
    } else if (strcmp(command, "READ") == 0) {
        cmd_read(arg1);
    } else if (strcmp(command, "LIST") == 0) {
        cmd_list();
    } else if (strcmp(command, "EXIT") == 0) {
        cmd_exit();
    } else if (strcmp(command, "HELP") == 0) {
        cmd_help();
    } else {
        cmd_help();
    }

    mutex_unlock(&dev_mutex);
    kfree(cmd_copy);
}

// ---------- Реализация команд ----------
static void cmd_add(char *arg1, char *arg2)
{
    dev_buf[0] = '\0';
    // arg1 = name, arg2 = key
    // Если какой-то аргумент отсутствует или превышает размер, id = -1, но запись создаётся
    int id_to_assign = next_id;
    int invalid = 0;

    if (record_count >= MAX_NOTE) {
        snprintf(dev_buf, BUFFER_SIZE, "Error: wallet is full\n");
        buf_len = strlen(dev_buf);
        return;
    }

    // Проверка имени
    if (arg1 == NULL || strlen(arg1) == 0) {
        invalid = 1;
        snprintf(dev_buf, BUFFER_SIZE, "Warning: name is empty, setting ID = -1\n");
    } else if (strlen(arg1) >= NAME_LEN) {
        invalid = 1;
        snprintf(dev_buf, BUFFER_SIZE, "Warning: name too long (max %d), setting ID = -1\n", NAME_LEN - 1);
    } else {
        strncpy(notes[record_count].name, arg1, NAME_LEN - 1);
        notes[record_count].name[NAME_LEN - 1] = '\0';
    }

    // Проверка ключа
    if (arg2 == NULL || strlen(arg2) == 0) {
        invalid = 1;
        strncat(dev_buf, "Warning: key is empty\n", BUFFER_SIZE - strlen(dev_buf) - 1);
    } else if (strlen(arg2) >= KEY_LEN) {
        invalid = 1;
        strncat(dev_buf, "Warning: key too long (max %d)\n", BUFFER_SIZE - strlen(dev_buf) - 1);
    } else {
        strncpy(notes[record_count].key, arg2, KEY_LEN - 1);
        notes[record_count].key[KEY_LEN - 1] = '\0';
    }

    if (invalid) {
        notes[record_count].id = -1;
        // next_id не увеличиваем
    } else {
        notes[record_count].id = id_to_assign;
        next_id++;
    }

    record_count++;
    // Дописываем сообщение о результате
    char msg[100];
    snprintf(msg, sizeof(msg), "Add note with ID: %d\n", invalid ? -1 : id_to_assign);
    strncat(dev_buf, msg, BUFFER_SIZE - strlen(dev_buf) - 1);
    buf_len = strlen(dev_buf);
}

static void cmd_edit(char *id_str, char *new_name, char *new_key)
{
    dev_buf[0] = '\0';
    int id;
    if (id_str == NULL || kstrtoint(id_str, 10, &id) != 0) {
        snprintf(dev_buf, BUFFER_SIZE, "Error: invalid ID for EDIT\n");
        buf_len = strlen(dev_buf);
        return;
    }

    // Ищем запись с таким id (активна, если id != -1? но мы храним все)
    int idx = -1;
    for (int i = 0; i < record_count; i++) {
        if (notes[i].id == id) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        snprintf(dev_buf, BUFFER_SIZE, "Error: ID %d not found\n", id);
        buf_len = strlen(dev_buf);
        return;
    }

    int invalid = 0;
    // Редактируем имя
    if (new_name == NULL || strlen(new_name) == 0) {
        invalid = 1;
        snprintf(dev_buf, BUFFER_SIZE, "Warning: new name is empty\n");
    } else if (strlen(new_name) >= NAME_LEN) {
        invalid = 1;
        snprintf(dev_buf, BUFFER_SIZE, "Warning: new name too long (max %d)\n", NAME_LEN - 1);
    } else {
        strncpy(notes[idx].name, new_name, NAME_LEN - 1);
        notes[idx].name[NAME_LEN - 1] = '\0';
    }

    // Редактируем ключ
    if (new_key == NULL || strlen(new_key) == 0) {
        invalid = 1;
        strncat(dev_buf, "Warning: new key is empty\n", BUFFER_SIZE - strlen(dev_buf) - 1);
    } else if (strlen(new_key) >= KEY_LEN) {
        invalid = 1;
        strncat(dev_buf, "Warning: new key too long (max %d)\n", BUFFER_SIZE - strlen(dev_buf) - 1);
    } else {
        strncpy(notes[idx].key, new_key, KEY_LEN - 1);
        notes[idx].key[KEY_LEN - 1] = '\0';
    }

    if (invalid) {
        notes[idx].id = -1;
        // id становится -1, но запись остаётся
    }
    // Иначе id остаётся прежним

    char msg[100];
    snprintf(msg, sizeof(msg), "Edit note with ID: %d\n", invalid ? -1 : id);
    strncat(dev_buf, msg, BUFFER_SIZE - strlen(dev_buf) - 1);
    buf_len = strlen(dev_buf);
}

static void cmd_rm(char *id_str)
{
    dev_buf[0] = '\0';
    int id;
    if (id_str == NULL || kstrtoint(id_str, 10, &id) != 0) {
        snprintf(dev_buf, BUFFER_SIZE, "Error: invalid ID for RM\n");
        buf_len = strlen(dev_buf);
        return;
    }

    int idx = -1;
    for (int i = 0; i < record_count; i++) {
        if (notes[i].id == id) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        snprintf(dev_buf, BUFFER_SIZE, "Error: ID %d not found\n", id);
        buf_len = strlen(dev_buf);
        return;
    }

    // Сдвигаем все записи выше на одну назад
    for (int i = idx; i < record_count - 1; i++) {
        notes[i] = notes[i + 1];
    }
    record_count--;
    // Примечание: id удалённой записи теряется, next_id не меняется

    snprintf(dev_buf, BUFFER_SIZE, "Delete note with ID: %d\n", id);
    buf_len = strlen(dev_buf);
}

static void cmd_read(char *id_str)
{
    dev_buf[0] = '\0';
    int id;
    if (id_str == NULL || kstrtoint(id_str, 10, &id) != 0) {
        snprintf(dev_buf, BUFFER_SIZE, "Error: invalid ID for READ\n");
        buf_len = strlen(dev_buf);
        return;
    }

    for (int i = 0; i < record_count; i++) {
        if (notes[i].id == id) {
            snprintf(dev_buf, BUFFER_SIZE, "ID: %d, NAME: %s, KEY: %s\n",
                     notes[i].id, notes[i].name, notes[i].key);
            buf_len = strlen(dev_buf);
            return;
        }
    }
    snprintf(dev_buf, BUFFER_SIZE, "ID %d not found\n", id);
    buf_len = strlen(dev_buf);
}

static void cmd_list(void)
{
    dev_buf[0] = '\0';
    int offset = 0;
    offset += snprintf(dev_buf + offset, BUFFER_SIZE - offset, "--- Wallet contents ---\n");
    for (int i = 0; i < record_count; i++) {
        offset += snprintf(dev_buf + offset, BUFFER_SIZE - offset,
                           "ID: %d, NAME: %s, KEY: %s\n",
                           notes[i].id, notes[i].name, notes[i].key);
        if (offset >= BUFFER_SIZE - 100) break;
    }
    if (record_count == 0) {
        offset += snprintf(dev_buf + offset, BUFFER_SIZE - offset, "Empty\n");
    }
    buf_len = offset;
}

static void cmd_exit(void)
{
    dev_buf[0] = '\0';
    snprintf(dev_buf, BUFFER_SIZE, "Session ended. Use 'rmmod' to unload module.\n");
    buf_len = strlen(dev_buf);
}

static void cmd_help(void)
{
    dev_buf[0] = '\0';
    snprintf(dev_buf, BUFFER_SIZE,
             "Available commands:\n"
             "  ADD <name> <key>      - add a new record\n"
             "  EDIT <id> <name> <key>- edit existing record\n"
             "  RM <id>               - remove record by ID\n"
             "  READ <id>             - show one record\n"
             "  LIST                  - show all records\n"
             "  EXIT                  - finish session\n"
             "  HELP                  - this help\n");
    buf_len = strlen(dev_buf);
}

// ---------- Инициализация и очистка модуля ----------
static int __init dev_init(void)
{
    int ret;

    dev_buf = kmalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!dev_buf) {
        printk(KERN_ERR "Crypto_Wallet: failed to allocate buffer\n");
        return -ENOMEM;
    }

    ret = alloc_chrdev_region(&dev_num, 0, 1, DEV_NAME); // Исправлено имя
    if (ret) {
        printk(KERN_ERR "Crypto_Wallet: failed to allocate device number\n");
        handler(1);
        return ret;
    }

    cdev_init(&wallet_cdev, &fops);
    wallet_cdev.owner = THIS_MODULE;

    ret = cdev_add(&wallet_cdev, dev_num, 1);
    if (ret) {
        printk(KERN_ERR "Crypto_Wallet: failed to add cdev\n");
        handler(3);
        return ret;
    }

    wallet_class = class_create(DEV_CLASS); // Исправлено имя класса
    if (IS_ERR(wallet_class)) {
        ret = PTR_ERR(wallet_class);
        printk(KERN_ERR "Crypto_Wallet: failed to create class\n");
        handler(3);
        return ret;
    }

    if (IS_ERR(device_create(wallet_class, NULL, dev_num, NULL, DEV_NAME))) {
        printk(KERN_ERR "Crypto_Wallet: failed to create device\n");
        ret = -ENODEV;
        handler(4);
        return ret;
    }

    mutex_init(&dev_mutex);
    memset(notes, 0, sizeof(notes));
    next_id = 1;
    record_count = 0;
    buf_len = 0;

    printk(KERN_INFO "Crypto_Wallet: module loaded successfully, device /dev/%s ready\n", DEV_NAME);
    return 0;
}

void handler(int error)
{
    while (error > 0) {
        if (error == 1)
            kfree(dev_buf);
        if (error == 2)
            unregister_chrdev_region(dev_num, 1);
        if (error == 3)
            cdev_del(&wallet_cdev);
        if (error == 4)
            class_destroy(wallet_class);
        error = error - 1;
    }
}

static void __exit dev_exit(void)
{
    handler(4);
    printk(KERN_INFO "Crypto_Wallet: module unloaded\n");
}

module_init(dev_init);
module_exit(dev_exit);
