/*
 * EC(Embedded Controller) KB3310B misc device driver on Linux
 * Author	: liujl <liujl@lemote.com>
 * Date		: 2008-04-20
 *
 * NOTE :
 * 		1, The EC resources accessing and programming are supported.
 */

/*******************************************************************/

#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <linux/apm_bios.h>
#include <linux/capability.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/apm-emulation.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/timer.h>

#include <asm/delay.h>
struct ec_reg {
	u32 addr;	/* the address of kb3310 registers */
	u8  val;	/* the register value for ec */
	u32 hi;
	u32 lo;
};

//struct msr_reg {
//	u32 addr;	/* the address of kb3310 registers */
//	u32	hi, lo;	/* the register value */
//};

#define	IOCTL_RDMSR	_IOR('F', 5, int)
#define	IOCTL_WRMSR	_IOR('F', 6, int)
#define	IOCTL_RDEC	_IOR('F', 7, int)
#define	IOCTL_WREC	_IOR('F', 8, int)

extern void _rdmsr(u32 msr, u32 *hi, u32 *lo);
extern void _wrmsr(u32 msr, u32 hi, u32 lo);
/*******************************************************************/

static int wrec_ioctl(struct inode * inode, struct file *filp, u_int cmd, u_long arg)
{
	void __user *ptr = (void __user *)arg;
	struct ec_reg *ecreg = (struct ec_reg *)(filp->private_data);
	//struct msr_reg *msrreg = (struct msr_reg *)(filp->private_data);
	int ret = 0;

	switch (cmd) {
		case IOCTL_RDEC :
			ret = copy_from_user(ecreg, ptr, sizeof(struct ec_reg));
			if(ret){
				printk(KERN_ERR "spi read : copy from user error.\n");
				return -EFAULT;
			}
			printk("debug ec : addr 0x%x\n", ecreg->addr);
#ifdef	CONFIG_64BIT
			ecreg->val = *((volatile unsigned char *)(ecreg->addr | 0xffffffff00000000));
#else
			ecreg->val = *((volatile unsigned char *)(ecreg->addr));
#endif
			ret = copy_to_user(ptr, ecreg, sizeof(struct ec_reg));
			if(ret){
				printk(KERN_ERR "spi read : copy to user error.\n");
				return -EFAULT;
			}
			break;
	case IOCTL_WREC :
			ret = copy_from_user(ecreg, ptr, sizeof(struct ec_reg));
			if(ret){
				printk(KERN_ERR "spi read : copy from user error.\n");
				return -EFAULT;
			}
			printk("debug ec : addr 0x%x, val %x\n", ecreg->addr, ecreg->val);
#ifdef	CONFIG_64BIT
			*((volatile unsigned char *)(ecreg->addr | 0xffffffff00000000)) = ecreg->val;
#else
			*((volatile unsigned char *)(ecreg->addr)) = ecreg->val;
#endif
			break;
		case IOCTL_RDMSR :
			ret = copy_from_user(ecreg, ptr, sizeof(struct ec_reg));
			if(ret){
				printk(KERN_ERR "spi read : copy from user error.\n");
				return -EFAULT;
			}
			printk("msr : addr 0x%x\n", ecreg->addr);
			_rdmsr(ecreg->addr, &(ecreg->hi), &(ecreg->lo));
			ret = copy_to_user(ptr, ecreg, sizeof(struct ec_reg));
			if(ret){
				printk(KERN_ERR "spi read : copy to user error.\n");
				return -EFAULT;
			}
			break;
		case IOCTL_WRMSR :
			ret = copy_from_user(ecreg, ptr, sizeof(struct ec_reg));
			if(ret){
				printk(KERN_ERR "spi read : copy from user error.\n");
				return -EFAULT;
			}
			printk("msr : addr 0x%x, hi 0x%x lo 0x%x\n", ecreg->addr, ecreg->hi, ecreg->lo);
			_wrmsr(ecreg->addr, ecreg->hi, ecreg->lo);
			break;

		default :
			break;
	}

	return 0;
}

static long wrec_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return wrec_ioctl(file->f_dentry->d_inode, file, cmd, arg);
}

static int wrec_open(struct inode * inode, struct file * filp)
{
	struct ec_reg *ecreg = NULL;
	ecreg = kmalloc(sizeof(struct ec_reg), GFP_KERNEL);
	if (ecreg) {
		filp->private_data = ecreg;
	}

	return ecreg ? 0 : -ENOMEM;
}

static int wrec_release(struct inode * inode, struct file * filp)
{
	struct ec_reg *ecreg = (struct ec_reg *)(filp->private_data);

	filp->private_data = NULL;
	kfree(ecreg);

	return 0;
}
/*
static int wrmsr_open(struct inode * inode, struct file * filp)
{
	struct msr_reg *msrreg = NULL;
	msrreg = kmalloc(sizeof(struct msr_reg), GFP_KERNEL);
	if (msrreg) {
		filp->private_data = msrreg;
	}

	return msrreg ? 0 : -ENOMEM;
}

static int wrmsr_release(struct inode * inode, struct file * filp)
{
	struct msr_reg *msrreg = (struct msr_reg *)(filp->private_data);

	filp->private_data = NULL;
	kfree(msrreg);

	return 0;
}
*/
static struct file_operations wrec_fops = {
	.owner		= THIS_MODULE,
	.open		= wrec_open,
//	.open		= wrmsr_open,
	.release	= wrec_release,
//	.release	= wrmsr_release,
	.read		= NULL,
	.write		= NULL,
#ifdef	CONFIG_64BIT
	.compat_ioctl = wrec_compat_ioctl,
#else
	.ioctl		= wrec_ioctl,
#endif
};

/*********************************************************/

static struct miscdevice wrec_device = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "io_msrdev",
	.fops		= &wrec_fops
};

static int __init wrec_init(void)
{
	int ret;

	printk(KERN_INFO "IO and MSR read/write device init.\n");
	ret = misc_register(&wrec_device);

	return ret;
}

static void __exit wrec_exit(void)
{
	printk(KERN_INFO "IO and MSR read/write device exit.\n");
	misc_deregister(&wrec_device);
}

module_init(wrec_init);
module_exit(wrec_exit);

MODULE_AUTHOR("liujl <liujl@lemote.com>");
MODULE_DESCRIPTION("KB3310 resources misc Management");
MODULE_LICENSE("GPL");
