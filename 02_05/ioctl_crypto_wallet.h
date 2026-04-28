#ifndef CRYPTO_WALLET_IOCTL_H
#define CRYPTO_WALLET_IOCTL_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#include <stddef.h>
#endif

#define WALLET_MAGIC 'w'

#define MAX_NAME_LEN 256
#define MAX_KEY_LEN  256
#define MAX_LIST_SIZE 4096
#define WALLET_LIST_BUF_SIZE MAX_LIST_SIZE   // <-- добавлено

struct wallet_add {
    char name[MAX_NAME_LEN];
    char key[MAX_KEY_LEN];
    int result_id;
};

struct wallet_edit {
    int id;
    char new_name[MAX_NAME_LEN];
    char new_key[MAX_KEY_LEN];
    int result;
};

struct wallet_rm {
    int id;
    int result;
};

struct wallet_read {
    int id;
    char name[MAX_NAME_LEN];
    char key[MAX_KEY_LEN];
    int found;
};

#define WALLET_ADD   _IOWR(WALLET_MAGIC, 1, struct wallet_add)
#define WALLET_EDIT  _IOWR(WALLET_MAGIC, 2, struct wallet_edit)
#define WALLET_RM    _IOWR(WALLET_MAGIC, 3, struct wallet_rm)
#define WALLET_READ  _IOWR(WALLET_MAGIC, 4, struct wallet_read)
#define WALLET_LIST  _IOR (WALLET_MAGIC, 5, char[MAX_LIST_SIZE])
#define WALLET_EXIT  _IO  (WALLET_MAGIC, 6)

#endif
