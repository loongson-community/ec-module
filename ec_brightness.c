/*
 * EC(Embedded Controller) KB3310B backlight brightness control management driver on Linux
 * Author	: huangw <huangw@lemote.com>
 * Date		: 2009-07-24
 *
 * NOTE : For the application layer to read the current value(level) of backlight brightness
 *        to provide interface
 *        Usage: cat /proc/brightness
 */

/**********************************************************/

#include <linux/module.h>
//#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
//#include <linux/capability.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <asm/delay.h>
#include "ec.h"
#include "ec_misc_fn.h"
/************************************************************************/

/* The EC Brightness device, here is his minor number. */
#define	ECBRG_MINOR_DEV		MISC_DYNAMIC_MINOR
#define	EC_BRG_DEV			"brightness"

static struct task_struct *brightness_tsk;

static DEFINE_MUTEX(brg_info_lock);
struct brg_info {
	unsigned int level;
	unsigned int curr_level;
}brg_info = {
	.level = 0,
	.curr_level = 0
};
static struct miscdevice brg_device = {
	.minor		= ECBRG_MINOR_DEV,
	.name		= "brg_bios",
	.fops		= NULL
};

#ifdef	CONFIG_PROC_FS
static int brg_proc_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static struct proc_dir_entry *brg_proc_entry;

static int brg_proc_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	struct brg_info info;
	int ret;

	mutex_lock(&brg_info_lock);
	info.curr_level = brg_info.curr_level;
	info.level = brg_info.level;
	PRINTK_DBG("info.curr_level : %x, info.level : %x\n", info.curr_level, info.level);
	mutex_unlock(&brg_info_lock);

	ret = sprintf(page, "%x\n", info.curr_level);

	PRINTK_DBG("page = %s, ret = %d, off = %ld\n", page, ret, off);
	ret -= off;
	if(ret < off + count){
		*eof = 1;
	}
	*start = page + off;
	if(ret > count){
		ret = count;}
	if(ret < 0){
		ret = 0;}

	return ret;
}
#endif

static int brightness_manager(void *arg)
{
	/* store old brightness value */
	brg_info.level = ec_read(REG_DISPLAY_BRIGHTNESS);
	PRINTK_DBG(KERN_DEBUG "Brightness manager thread started.\n");

	while(1){
		set_current_state(TASK_INTERRUPTIBLE);
		/* schedule every 1ns, it is better for changed to 1ns. */
		schedule_timeout(HZ/1000/1000/1000 * 1);
		//schedule_timeout(HZ * 1);

		if (kthread_should_stop())
			break;
		mutex_lock(&brg_info_lock);
		/* store new brightness value, as write to /proc/brightness file */
		brg_info.curr_level = ec_read(REG_DISPLAY_BRIGHTNESS);
		mutex_unlock(&brg_info_lock);
	}
	PRINTK_DBG(KERN_DEBUG "Brightness Management thread exit.\n");
	return 0;
}

static int __init brg_init(void)
{
	int ret;

	printk(KERN_ERR "Backlight Brightness control on KB3310B Embedded Controller init.\n");

	brightness_tsk = kthread_create(brightness_manager, NULL, "bklight_manager");
	if (IS_ERR(brightness_tsk)) {
		ret = PTR_ERR(brightness_tsk);
		kthread_stop(brightness_tsk);
		brightness_tsk = NULL;
		printk(KERN_ERR "ecbrg : backlight brightness management error.\n");
		return ret;
	}
	brightness_tsk->flags |= PF_NOFREEZE;
	wake_up_process(brightness_tsk);

#ifdef CONFIG_PROC_FS
	brg_proc_entry = NULL;
	brg_proc_entry = create_proc_entry(EC_BRG_DEV, S_IWUSR | S_IRUGO, NULL);
	if(brg_proc_entry == NULL){
		printk(KERN_ERR "EC BRG : register /proc/brightness failed.\n");
		return -EINVAL;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30)
	brg_proc_entry->owner = THIS_MODULE;
#endif
	brg_proc_entry->read_proc = brg_proc_read;
	brg_proc_entry->write_proc = NULL;
	brg_proc_entry->data = NULL;
#endif

	ret = misc_register(&brg_device);
	if (ret != 0) {
		remove_proc_entry(EC_BRG_DEV, NULL);
		kthread_stop(brightness_tsk);
		printk(KERN_ERR "ecbrg : misc register error.\n");
	}
	return ret;
}

static void __exit brg_exit(void)
{
	misc_deregister(&brg_device);
#ifdef	CONFIG_PROC_FS
	remove_proc_entry(EC_BRG_DEV, NULL);
#endif

	kthread_stop(brightness_tsk);
}

module_init(brg_init);
module_exit(brg_exit);

MODULE_AUTHOR("huangw <huangw@lemote.com>");
MODULE_DESCRIPTION("Backlight brightness control Management For Kb3310");
MODULE_LICENSE("GPL");
