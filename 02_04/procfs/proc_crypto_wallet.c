// SPDX-License-Identifier: GPL-2.0+
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/string.h>

#define PROC_NAME "crypto_wallet"
#define MAX_NOTE 100
#define KEY_LEN 256
#define NAME_LEN 256
#define BUFFER_SIZE 4096

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gluhx");
MODULE_DESCRIPTION("Crypto Wallet with procfs interface");

typedef struct {
    int id;
    char name[NAME_LEN];
    char key[KEY_LEN];
} Note;

static struct mutex dev_mutex;
static Note notes[MAX_NOTE];
static int next_id = 1;
static int record_count = 0;
static char g_result[BUFFER_SIZE];
static struct proc_dir_entry *proc_entry;

// Прототипы
static int proc_open(struct inode *inode, struct file *file);
static ssize_t proc_write(struct file *file, const char __user *buf, size_t count, loff_t *f_pos);
static int proc_release(struct inode *inode, struct file *file);
static int show_result(struct seq_file *m, void *v);

static char *get_next_token(char *str, char **saveptr);
static void process_command(const char *cmd_str);
static void cmd_add(char *arg1, char *arg2);
static void cmd_edit(char *id_str, char *arg1, char *arg2);
static void cmd_rm(char *id_str);
static void cmd_read(char *id_str);
static void cmd_list(void);
static void cmd_exit(void);
static void cmd_help(void);

// proc_ops
static const struct proc_ops proc_fops = {
    .proc_open    = proc_open,
    .proc_write   = proc_write,
    .proc_release = proc_release,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
};

static int show_result(struct seq_file *m, void *v)
{
    seq_printf(m, "%s", g_result);
    return 0;
}

static int proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, show_result, NULL);
}

static int proc_release(struct inode *inode, struct file *file)
{
    return single_release(inode, file);
}

static ssize_t proc_write(struct file *file, const char __user *buf, size_t count, loff_t *f_pos)
{
    char *kernel_buf;
    int ret = count;

    if (count >= BUFFER_SIZE) {
        pr_warn("crypto_wallet: command too long\n");
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

    if (count > 0 && kernel_buf[count-1] == '\n')
        kernel_buf[count-1] = '\0';

    pr_info("crypto_wallet: received command: %s\n", kernel_buf);

    mutex_lock(&dev_mutex);
    process_command(kernel_buf);
    mutex_unlock(&dev_mutex);

    kfree(kernel_buf);
    return ret;
}

// ---------- Парсер токенов ----------
static char *get_next_token(char *str, char **saveptr)
{
    if (str) {
        *saveptr = str;
    } else {
        if (!*saveptr)
            return NULL;
        str = *saveptr;
    }

    while (*str == ' ' || *str == '\t')
        str++;
    if (*str == '\0') {
        *saveptr = NULL;
        return NULL;
    }

    char *start = str;
    while (*str != ' ' && *str != '\t' && *str != '\0' && *str != '\n')
        str++;

    if (*str) {
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
        snprintf(g_result, BUFFER_SIZE, "Error: memory allocation failed\n");
        return;
    }

    char *token_cmd = get_next_token(cmd_copy, &saveptr);
    if (token_cmd)
        strncpy(command, token_cmd, sizeof(command) - 1);

    char *arg1 = get_next_token(NULL, &saveptr);
    char *arg2 = get_next_token(NULL, &saveptr);

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

    kfree(cmd_copy);
}

// ---------- Команды (без локальных больших буферов) ----------

static void cmd_add(char *arg1, char *arg2)
{
    if (record_count >= MAX_NOTE) {
        snprintf(g_result, BUFFER_SIZE, "Error: wallet is full\n");
        return;
    }

    int id_to_assign = next_id;
    int invalid = 0;
    // Временно собираем предупреждения в локальную строку небольшого размера,
    // так как их длина заведомо не превышает, скажем, 256 байт.
    char warnings[512] = {0};

    // Проверка имени
    if (!arg1 || strlen(arg1) == 0) {
        invalid = 1;
        snprintf(warnings, sizeof(warnings), "Warning: name is empty, setting ID = -1\n");
    } else if (strlen(arg1) >= NAME_LEN) {
        invalid = 1;
        snprintf(warnings, sizeof(warnings), "Warning: name too long (max %d), setting ID = -1\n", NAME_LEN - 1);
    } else {
        strncpy(notes[record_count].name, arg1, NAME_LEN - 1);
        notes[record_count].name[NAME_LEN - 1] = '\0';
    }

    // Проверка ключа
    if (!arg2 || strlen(arg2) == 0) {
        invalid = 1;
        strlcat(warnings, "Warning: key is empty\n", sizeof(warnings));
    } else if (strlen(arg2) >= KEY_LEN) {
        invalid = 1;
        strlcat(warnings, "Warning: key too long\n", sizeof(warnings));
    } else {
        strncpy(notes[record_count].key, arg2, KEY_LEN - 1);
        notes[record_count].key[KEY_LEN - 1] = '\0';
    }

    if (invalid) {
        notes[record_count].id = -1;
    } else {
        notes[record_count].id = id_to_assign;
        next_id++;
    }
    record_count++;

    snprintf(g_result, BUFFER_SIZE, "%sAdd note with ID: %d\n", warnings, invalid ? -1 : id_to_assign);
}

static void cmd_edit(char *id_str, char *new_name, char *new_key)
{
    int id;
    if (!id_str || kstrtoint(id_str, 10, &id) != 0) {
        snprintf(g_result, BUFFER_SIZE, "Error: invalid ID for EDIT\n");
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
        snprintf(g_result, BUFFER_SIZE, "Error: ID %d not found\n", id);
        return;
    }

    int invalid = 0;
    char warnings[512] = {0};

    // Редактирование имени
    if (!new_name || strlen(new_name) == 0) {
        invalid = 1;
        snprintf(warnings, sizeof(warnings), "Warning: new name is empty\n");
    } else if (strlen(new_name) >= NAME_LEN) {
        invalid = 1;
        snprintf(warnings, sizeof(warnings), "Warning: new name too long (max %d)\n", NAME_LEN - 1);
    } else {
        strncpy(notes[idx].name, new_name, NAME_LEN - 1);
        notes[idx].name[NAME_LEN - 1] = '\0';
    }

    // Редактирование ключа
    if (!new_key || strlen(new_key) == 0) {
        invalid = 1;
        strlcat(warnings, "Warning: new key is empty\n", sizeof(warnings));
    } else if (strlen(new_key) >= KEY_LEN) {
        invalid = 1;
        strlcat(warnings, "Warning: new key too long\n", sizeof(warnings));
    } else {
        strncpy(notes[idx].key, new_key, KEY_LEN - 1);
        notes[idx].key[KEY_LEN - 1] = '\0';
    }

    if (invalid) {
        notes[idx].id = -1;
    }

    snprintf(g_result, BUFFER_SIZE, "%sEdit note with ID: %d\n", warnings, invalid ? -1 : id);
}

static void cmd_rm(char *id_str)
{
    int id;
    if (!id_str || kstrtoint(id_str, 10, &id) != 0) {
        snprintf(g_result, BUFFER_SIZE, "Error: invalid ID for RM\n");
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
        snprintf(g_result, BUFFER_SIZE, "Error: ID %d not found\n", id);
        return;
    }

    // Сдвиг записей
    for (int i = idx; i < record_count - 1; i++)
        notes[i] = notes[i + 1];
    record_count--;

    snprintf(g_result, BUFFER_SIZE, "Delete note with ID: %d\n", id);
}

static void cmd_read(char *id_str)
{
    int id;
    if (!id_str || kstrtoint(id_str, 10, &id) != 0) {
        snprintf(g_result, BUFFER_SIZE, "Error: invalid ID for READ\n");
        return;
    }

    for (int i = 0; i < record_count; i++) {
        if (notes[i].id == id) {
            snprintf(g_result, BUFFER_SIZE, "ID: %d, NAME: %s, KEY: %s\n",
                     notes[i].id, notes[i].name, notes[i].key);
            return;
        }
    }
    snprintf(g_result, BUFFER_SIZE, "ID %d not found\n", id);
}

static void cmd_list(void)
{
    char *pos = g_result;
    size_t remain = BUFFER_SIZE;
    int len;

    len = snprintf(pos, remain, "--- Wallet contents ---\n");
    pos += len; remain -= len;

    for (int i = 0; i < record_count; i++) {
        len = snprintf(pos, remain, "ID: %d, NAME: %s, KEY: %s\n",
                       notes[i].id, notes[i].name, notes[i].key);
        if (len >= remain) break;
        pos += len; remain -= len;
    }
    if (record_count == 0) {
        snprintf(pos, remain, "Empty\n");
    }
}

static void cmd_exit(void)
{
    snprintf(g_result, BUFFER_SIZE, "Session ended. Use 'rmmod' to unload module.\n");
}

static void cmd_help(void)
{
    snprintf(g_result, BUFFER_SIZE,
             "Available commands:\n"
             "  ADD <name> <key>      - add a new record\n"
             "  EDIT <id> <name> <key>- edit existing record\n"
             "  RM <id>               - remove record by ID\n"
             "  READ <id>             - show one record\n"
             "  LIST                  - show all records\n"
             "  EXIT                  - finish session\n"
             "  HELP                  - this help\n");
}

// ---------- Инициализация и выход ----------
static int __init proc_init(void)
{
    mutex_init(&dev_mutex);
    memset(notes, 0, sizeof(notes));
    next_id = 1;
    record_count = 0;
    g_result[0] = '\0';

    proc_entry = proc_create(PROC_NAME, 0666, NULL, &proc_fops);
    if (!proc_entry) {
        pr_err("crypto_wallet: failed to create /proc/%s\n", PROC_NAME);
        return -ENOMEM;
    }

    pr_info("crypto_wallet: module loaded, use /proc/%s\n", PROC_NAME);
    return 0;
}

static void __exit proc_exit(void)
{
    proc_remove(proc_entry);
    pr_info("crypto_wallet: module unloaded\n");
}

module_init(proc_init);
module_exit(proc_exit);
