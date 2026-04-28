#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xf7a46e21, "cdev_init" },
	{ 0x3b68b478, "cdev_add" },
	{ 0x7ca9495d, "class_create" },
	{ 0x06e9759e, "device_create" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0xfb578fc5, "memset" },
	{ 0xc8647c73, "cdev_del" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0x3af3d85f, "class_destroy" },
	{ 0x791a5282, "device_destroy" },
	{ 0xdc50aae2, "__ref_stack_chk_guard" },
	{ 0x89940875, "mutex_lock_interruptible" },
	{ 0x13c49cc2, "_copy_from_user" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0xa916b694, "strnlen" },
	{ 0x476b165a, "sized_strscpy" },
	{ 0x6b10bee1, "_copy_to_user" },
	{ 0x656e4a6e, "snprintf" },
	{ 0x19dee613, "__fortify_panic" },
	{ 0x1c303cee, "validate_usercopy_range" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0xa957e0e9, "module_put" },
	{ 0x122c3a7e, "_printk" },
	{ 0x11b50a4a, "try_module_get" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0x50222c0f, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "CCD9A85C85C35D554EDDB92");
