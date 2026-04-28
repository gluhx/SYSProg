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

#include "ioctl_crypto_wallet.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gluhx");
MODULE_DESCRIPTION("Crypto Wallet using ioctl");

#define DEV_NAME "crypto_wallet"
#define DEV_CLASS "crypto_wallet_class"
#define MAX_NOTES 100

typedef struct {
    int id;
    char name[MAX_NAME_LEN];
    char key[MAX_KEY_LEN];
} Note;

static struct file_operations fops;
static struct class *wallet_class = NULL;
static struct cdev wallet_cdev;
static struct mutex dev_mutex;
static dev_t dev_num;

static Note notes[MAX_NOTES];
static int next_id = 1;
static int record_count = 0;

static int wallet_add_note(const char *name, const char *key, int *out_id);
static int wallet_edit_note(int id, const char *new_name, const char *new_key);
static int wallet_remove_note(int id);
static int wallet_read_note(int id, char *out_name, char *out_key);
static void wallet_list_to_string(char *buf, size_t buf_size);
static void wallet_reset(void);

static long wallet_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    void __user *argp = (void __user *)arg;
    int ret = 0;

    if (mutex_lock_interruptible(&dev_mutex))
        return -ERESTARTSYS;

    switch (cmd) {
        case WALLET_ADD: {
            struct wallet_add add_data;
            if (copy_from_user(&add_data, argp, sizeof(add_data))) {
                ret = -EFAULT;
                break;
            }
            add_data.name[MAX_NAME_LEN-1] = '\0';
            add_data.key[MAX_KEY_LEN-1] = '\0';
            int new_id;
            int err = wallet_add_note(add_data.name, add_data.key, &new_id);
            add_data.result_id = (err < 0) ? -1 : new_id;
            if (copy_to_user(argp, &add_data, sizeof(add_data)))
                ret = -EFAULT;
            break;
        }
        case WALLET_EDIT: {
            struct wallet_edit edit_data;
            if (copy_from_user(&edit_data, argp, sizeof(edit_data))) {
                ret = -EFAULT;
                break;
            }
            edit_data.new_name[MAX_NAME_LEN-1] = '\0';
            edit_data.new_key[MAX_KEY_LEN-1] = '\0';
            edit_data.result = wallet_edit_note(edit_data.id, edit_data.new_name, edit_data.new_key);
            if (copy_to_user(argp, &edit_data, sizeof(edit_data)))
                ret = -EFAULT;
            break;
        }
        case WALLET_RM: {
            struct wallet_rm rm_data;
            if (copy_from_user(&rm_data, argp, sizeof(rm_data))) {
                ret = -EFAULT;
                break;
            }
            rm_data.result = wallet_remove_note(rm_data.id);
            if (copy_to_user(argp, &rm_data, sizeof(rm_data)))
                ret = -EFAULT;
            break;
        }
        case WALLET_READ: {
            struct wallet_read read_data;
            if (copy_from_user(&read_data, argp, sizeof(read_data))) {
                ret = -EFAULT;
                break;
            }
            read_data.found = wallet_read_note(read_data.id, read_data.name, read_data.key);
            if (copy_to_user(argp, &read_data, sizeof(read_data)))
                ret = -EFAULT;
            break;
        }
        case WALLET_LIST: {
            char list_buf[WALLET_LIST_BUF_SIZE];
            wallet_list_to_string(list_buf, sizeof(list_buf));
            list_buf[sizeof(list_buf)-1] = '\0';
            size_t len = strlen(list_buf) + 1;
            if (copy_to_user(argp, list_buf, len))
                ret = -EFAULT;
            break;
        }
        case WALLET_EXIT:
            printk(KERN_INFO "Crypto_Wallet: EXIT\n");
            break;
        default:
            ret = -ENOTTY;
    }
    mutex_unlock(&dev_mutex);
    return ret;
}

static ssize_t wallet_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    return -ENOTTY;
}

static ssize_t wallet_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    return -ENOTTY;
}

static int wallet_open(struct inode *inode, struct file *filp)
{
    try_module_get(THIS_MODULE);
    printk(KERN_INFO "Crypto_Wallet: opened\n");
    return 0;
}

static int wallet_release(struct inode *inode, struct file *filp)
{
    module_put(THIS_MODULE);
    printk(KERN_INFO "Crypto_Wallet: closed\n");
    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = wallet_open,
    .release = wallet_release,
    .read = wallet_read,
    .write = wallet_write,
    .unlocked_ioctl = wallet_ioctl,
};

// ------------- НИЖЕ ИСПРАВЛЕННЫЕ ФУНКЦИИ -------------

static int wallet_add_note(const char *name, const char *key, int *out_id)
{
    if (record_count >= MAX_NOTES)
        return -ENOSPC;

    int invalid = 0;
    if (!name || strlen(name) == 0 || strlen(name) >= MAX_NAME_LEN) {
        invalid = 1;
        printk(KERN_WARNING "Add: invalid name\n");
    }
    if (!key || strlen(key) == 0 || strlen(key) >= MAX_KEY_LEN) {
        invalid = 1;
        printk(KERN_WARNING "Add: invalid key\n");
    }

    int idx = record_count;
    if (invalid) {
        notes[idx].id = -1;
        *out_id = -1;
        // next_id не увеличивается
        notes[idx].name[0] = '\0';
        notes[idx].key[0] = '\0';
    } else {
        notes[idx].id = next_id;
        *out_id = next_id;
        next_id++;
        strscpy(notes[idx].name, name, MAX_NAME_LEN);
        strscpy(notes[idx].key, key, MAX_KEY_LEN);
    }
    record_count++;
    return 0;
}

static int wallet_edit_note(int id, const char *new_name, const char *new_key)
{
    int idx = -1;
    for (int i = 0; i < record_count; i++) {
        if (notes[i].id == id) {
            idx = i;
            break;
        }
    }
    if (idx == -1)
        return -ENOENT;

    int invalid = 0;
    // Редактируем имя, если оно передано и не пустое
    if (new_name && strlen(new_name) > 0) {
        if (strlen(new_name) >= MAX_NAME_LEN) {
            invalid = 1;
            printk(KERN_WARNING "Edit: name too long\n");
        } else {
            strscpy(notes[idx].name, new_name, MAX_NAME_LEN);
        }
    } else if (new_name && strlen(new_name) == 0) {
        invalid = 1;   // пустое имя – ошибка
        printk(KERN_WARNING "Edit: empty name\n");
    }

    if (new_key && strlen(new_key) > 0) {
        if (strlen(new_key) >= MAX_KEY_LEN) {
            invalid = 1;
            printk(KERN_WARNING "Edit: key too long\n");
        } else {
            strscpy(notes[idx].key, new_key, MAX_KEY_LEN);
        }
    } else if (new_key && strlen(new_key) == 0) {
        invalid = 1;
        printk(KERN_WARNING "Edit: empty key\n");
    }

    if (invalid) {
        notes[idx].id = -1;
    }
    return 0;
}

static int wallet_remove_note(int id)
{
    int idx = -1;
    for (int i = 0; i < record_count; i++) {
        if (notes[i].id == id) {
            idx = i;
            break;
        }
    }
    if (idx == -1)
        return -ENOENT;
    for (int i = idx; i < record_count - 1; i++)
        notes[i] = notes[i+1];
    record_count--;
    return 0;
}

static int wallet_read_note(int id, char *out_name, char *out_key)
{
    for (int i = 0; i < record_count; i++) {
        if (notes[i].id == id) {
            strscpy(out_name, notes[i].name, MAX_NAME_LEN);
            strscpy(out_key, notes[i].key, MAX_KEY_LEN);
            return 1;
        }
    }
    out_name[0] = '\0';
    out_key[0] = '\0';
    return 0;
}

static void wallet_list_to_string(char *buf, size_t buf_size)
{
    int off = 0;
    off += snprintf(buf + off, buf_size - off, "--- Wallet contents ---\n");
    for (int i = 0; i < record_count && off < buf_size - 100; i++) {
        off += snprintf(buf + off, buf_size - off,
                        "ID: %d, NAME: %s, KEY: %s\n",
                        notes[i].id, notes[i].name, notes[i].key);
    }
    if (record_count == 0)
        off += snprintf(buf + off, buf_size - off, "Empty\n");
    buf[buf_size-1] = '\0';
}

static void wallet_reset(void)
{
    memset(notes, 0, sizeof(notes));
    next_id = 1;
    record_count = 0;
}

static int __init wallet_init(void)
{
    int ret;
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEV_NAME);
    if (ret) return ret;

    cdev_init(&wallet_cdev, &fops);
    wallet_cdev.owner = THIS_MODULE;
    ret = cdev_add(&wallet_cdev, dev_num, 1);
    if (ret) {
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }

    wallet_class = class_create(DEV_CLASS);
    if (IS_ERR(wallet_class)) {
        ret = PTR_ERR(wallet_class);
        cdev_del(&wallet_cdev);
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }

    if (IS_ERR(device_create(wallet_class, NULL, dev_num, NULL, DEV_NAME))) {
        ret = -ENODEV;
        class_destroy(wallet_class);
        cdev_del(&wallet_cdev);
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }

    mutex_init(&dev_mutex);
    wallet_reset();
    printk(KERN_INFO "Crypto_Wallet: loaded, /dev/%s\n", DEV_NAME);
    return 0;
}

static void __exit wallet_exit(void)
{
    device_destroy(wallet_class, dev_num);
    class_destroy(wallet_class);
    cdev_del(&wallet_cdev);
    unregister_chrdev_region(dev_num, 1);
    printk(KERN_INFO "Crypto_Wallet: unloaded\n");
}

module_init(wallet_init);
module_exit(wallet_exit);
