#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

#ifdef CONFIG_UNWINDER_ORC
#include <asm/orc_header.h>
ORC_HEADER;
#endif

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
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

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

KSYMTAB_FUNC(nvme_fc_register_localport, "_gpl", "");
KSYMTAB_FUNC(nvme_fc_unregister_localport, "_gpl", "");
KSYMTAB_FUNC(nvme_fc_register_remoteport, "_gpl", "");
KSYMTAB_FUNC(nvme_fc_unregister_remoteport, "_gpl", "");
KSYMTAB_FUNC(nvme_fc_rescan_remoteport, "_gpl", "");
KSYMTAB_FUNC(nvme_fc_set_remoteport_devloss, "_gpl", "");
KSYMTAB_FUNC(nvme_fc_rcv_ls_req, "_gpl", "");
KSYMTAB_FUNC(nvme_fc_io_getuuid, "_gpl", "");

SYMBOL_CRC(nvme_fc_register_localport, 0x8cd671b8, "_gpl");
SYMBOL_CRC(nvme_fc_unregister_localport, 0x3884f8b8, "_gpl");
SYMBOL_CRC(nvme_fc_register_remoteport, 0x0d12e564, "_gpl");
SYMBOL_CRC(nvme_fc_unregister_remoteport, 0xfca9dc99, "_gpl");
SYMBOL_CRC(nvme_fc_rescan_remoteport, 0x3e33ac54, "_gpl");
SYMBOL_CRC(nvme_fc_set_remoteport_devloss, 0x8a9cf5a7, "_gpl");
SYMBOL_CRC(nvme_fc_rcv_ls_req, 0xbb0e18a6, "_gpl");
SYMBOL_CRC(nvme_fc_io_getuuid, 0xcc8a2d78, "_gpl");

static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x1000e51, "schedule" },
	{ 0xcffb592b, "nvme_remove_io_tag_set" },
	{ 0xd42d4a6f, "nvme_cleanup_cmd" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x296695f, "refcount_warn_saturate" },
	{ 0x6e4b8f75, "nvme_mpath_start_request" },
	{ 0xb2fcb56d, "queue_delayed_work_on" },
	{ 0xa4ff3c07, "nvmf_unregister_transport" },
	{ 0x2e0c271e, "put_device" },
	{ 0xa916b694, "strnlen" },
	{ 0x8403c960, "nvme_change_ctrl_state" },
	{ 0xe91534db, "_dev_info" },
	{ 0xd00c39ec, "nvme_init_ctrl" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0x7cd8d75e, "page_offset_base" },
	{ 0xd58bbbcb, "nvme_delete_wq" },
	{ 0x37155d7e, "nvme_alloc_io_tag_set" },
	{ 0x91d56882, "nvme_fail_nonready_command" },
	{ 0x4890e64b, "kobject_uevent_env" },
	{ 0xfe487975, "init_wait_entry" },
	{ 0x4d09cdb6, "_dev_err" },
	{ 0xb5b8a3ca, "device_create" },
	{ 0x9bae8c47, "blk_sync_queue" },
	{ 0x5a921311, "strncmp" },
	{ 0x4b750f53, "_raw_spin_unlock_irq" },
	{ 0x339897dc, "__blk_rq_map_sg" },
	{ 0x9166fada, "strncpy" },
	{ 0xffb7c514, "ida_free" },
	{ 0x8a53dddd, "nvmf_reg_read64" },
	{ 0x4c9d28b0, "phys_base" },
	{ 0xde6d505f, "class_unregister" },
	{ 0x49224181, "nvme_reset_wq" },
	{ 0xc44a3b3d, "nvmf_connect_admin_queue" },
	{ 0xd35cce70, "_raw_spin_unlock_irqrestore" },
	{ 0xdb57249e, "nvme_enable_ctrl" },
	{ 0xea5c1dc9, "nvme_unquiesce_admin_queue" },
	{ 0x44bad84, "dma_sync_single_for_cpu" },
	{ 0xd5fbe7ad, "_dev_warn" },
	{ 0x25974000, "wait_for_completion" },
	{ 0x9166fc03, "__flush_workqueue" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0xd9a5ea54, "__init_waitqueue_head" },
	{ 0xe2d5255a, "strcmp" },
	{ 0x15ba50a6, "jiffies" },
	{ 0x4398b8c4, "nvme_unquiesce_io_queues" },
	{ 0x6c17b8f7, "blk_mq_update_nr_hw_queues" },
	{ 0x97651e6c, "vmemmap_base" },
	{ 0xb9f6984, "nvmf_reg_write32" },
	{ 0x9fa7184a, "cancel_delayed_work_sync" },
	{ 0xc6f46339, "init_timer_key" },
	{ 0x3efb0d7c, "nvmf_should_reconnect" },
	{ 0xf8c3fb07, "nvme_complete_async_event" },
	{ 0xfbe215e4, "sg_next" },
	{ 0x29008d8c, "device_destroy" },
	{ 0xb87e7f57, "nvme_set_queue_count" },
	{ 0x3c12dfe, "cancel_work_sync" },
	{ 0x56470118, "__warn_printk" },
	{ 0xffeedf6a, "delayed_work_timer_fn" },
	{ 0xb2fa093e, "blk_mq_map_queues" },
	{ 0x8452d742, "nvmf_register_transport" },
	{ 0x75bafcee, "blk_mq_start_request" },
	{ 0x4560f6d7, "nvme_quiesce_io_queues" },
	{ 0x600cd7af, "blk_mq_tagset_busy_iter" },
	{ 0x150667e6, "nvme_stop_ctrl" },
	{ 0xe90893c2, "dma_unmap_sg_attrs" },
	{ 0xb52dd993, "kmalloc_trace" },
	{ 0x54b1fac6, "__ubsan_handle_load_invalid_value" },
	{ 0xa91adf29, "nvme_init_ctrl_finish" },
	{ 0xc29009e8, "nvme_reset_ctrl" },
	{ 0xb5b54b34, "_raw_spin_unlock" },
	{ 0x9e229c49, "sg_alloc_table_chained" },
	{ 0x1ad73634, "nvme_quiesce_admin_queue" },
	{ 0xbc138a9b, "class_register" },
	{ 0xeb233a45, "__kmalloc" },
	{ 0xa08e4f8f, "__nvme_check_ready" },
	{ 0x12406497, "kmalloc_caches" },
	{ 0x931de371, "dma_map_sg_attrs" },
	{ 0x2d3385d3, "system_wq" },
	{ 0x1e45297, "nvme_remove_admin_tag_set" },
	{ 0xab2fac3b, "nvme_sync_io_queues" },
	{ 0xc31db0ce, "is_vmalloc_addr" },
	{ 0xe7a02573, "ida_alloc_range" },
	{ 0x1439566f, "nvme_uninit_ctrl" },
	{ 0xa7d5f92e, "ida_destroy" },
	{ 0xdf88caf2, "nvme_complete_rq" },
	{ 0xc60d0620, "__num_online_cpus" },
	{ 0x656e4a6e, "snprintf" },
	{ 0xa6257a2f, "complete" },
	{ 0xc5b6f236, "queue_work_on" },
	{ 0xdd8af66e, "nvmf_free_options" },
	{ 0x520f5e52, "blk_mq_tagset_wait_completed_request" },
	{ 0x608741b5, "__init_swait_queue_head" },
	{ 0x92540fbf, "finish_wait" },
	{ 0xea8896f6, "blk_mq_complete_request_remote" },
	{ 0x5bf858ca, "dma_unmap_page_attrs" },
	{ 0x425ac504, "nvme_setup_cmd" },
	{ 0x679dea01, "nvme_start_ctrl" },
	{ 0x45927389, "dma_sync_single_for_device" },
	{ 0xa79a2c26, "nvme_alloc_admin_tag_set" },
	{ 0xf61d6d98, "nvmf_connect_io_queue" },
	{ 0xfba7ddd2, "match_u64" },
	{ 0x37a0cba, "kfree" },
	{ 0xa56e1a52, "sg_free_table_chained" },
	{ 0x8c26d495, "prepare_to_wait_event" },
	{ 0x64b62862, "nvme_wq" },
	{ 0xa7461b91, "nvmf_reg_read32" },
	{ 0xe2964344, "__wake_up" },
	{ 0x9c2e9b76, "nvme_delete_ctrl" },
	{ 0xa8f7a217, "get_device" },
	{ 0x34db050b, "_raw_spin_lock_irqsave" },
	{ 0xba8fbd64, "_raw_spin_lock" },
	{ 0xcbd4898c, "fortify_panic" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0xf705bd5b, "dev_driver_string" },
	{ 0xe513c668, "nvmf_get_address" },
	{ 0x65487097, "__x86_indirect_thunk_rax" },
	{ 0xa112d1f2, "dma_map_page_attrs" },
	{ 0xb5e73116, "flush_delayed_work" },
	{ 0x122c3a7e, "_printk" },
	{ 0x8427cc7b, "_raw_spin_lock_irq" },
	{ 0xe66c6f08, "module_layout" },
};

MODULE_INFO(depends, "nvme-core,nvme-fabrics");


MODULE_INFO(srcversion, "A76A60E300F1B68D4836CC5");
