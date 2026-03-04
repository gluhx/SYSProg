#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gluhx");
MODULE_DESCRIPTION("A simple Hello World kernel module");
MODULE_VERSION("1.0");

static int __init hello_init(void)
{
    printk(KERN_INFO "Hello, world! May be beer? Module loaded.\n");
    return 0;  // 0 означает успешную загрузку
}

static void __exit hello_exit(void)
{
    printk(KERN_INFO "Goodbye, world! See yon in pub! Module unloaded.\n");
}

module_init(hello_init);
module_exit(hello_exit);
