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
	{ 0x754d539c, "strlen" },
	{ 0x9166fada, "strncpy" },
	{ 0xa916b694, "strnlen" },
	{ 0x656e4a6e, "snprintf" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x19dee613, "__fortify_panic" },
	{ 0x8c8569cb, "kstrtoint" },
	{ 0x6a5cc518, "__kmalloc_noprof" },
	{ 0x1c303cee, "validate_usercopy_range" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0x13c49cc2, "_copy_from_user" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x1ed88d6a, "kstrdup" },
	{ 0xe2d5255a, "strcmp" },
	{ 0x037a0cba, "kfree" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0x3292fda1, "single_open" },
	{ 0xeb3777a1, "seq_printf" },
	{ 0xd4981f91, "proc_remove" },
	{ 0x541fcbfc, "seq_read" },
	{ 0x8ab0149f, "seq_lseek" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0xfb578fc5, "memset" },
	{ 0x0321bc9b, "proc_create" },
	{ 0x122c3a7e, "_printk" },
	{ 0xb5c89adf, "single_release" },
	{ 0xdc50aae2, "__ref_stack_chk_guard" },
	{ 0x50222c0f, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "4B80848CFAB9B7B51C7F598");
