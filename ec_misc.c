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

#include "ec.h"
#include "ec_misc.h"

/*******************************************************************/
struct ec_info	ecinfo;

/* To see if the ec is in busy state or not. */
static inline int ec_flash_busy(void)
{
	unsigned char count = 0;

	while(count < 10){
		ec_write(REG_XBISPICMD, SPICMD_READ_STATUS);
		while( (ec_read(REG_XBISPICFG)) & SPICFG_SPI_BUSY );
		if((ec_read(REG_XBISPIDAT) & 0x01) == 0x00){
			return EC_STATE_IDLE;
		}
		count++;
	}

	return EC_STATE_BUSY;
}

/* read one byte from xbi interface */
static int ec_read_byte(u32 addr, u8 *byte)
{
	u32 timeout;
	u8 val;

	/* enable spicmd writing. */
	val = ec_read(REG_XBISPICFG);
	ec_write(REG_XBISPICFG, val | SPICFG_EN_SPICMD | SPICFG_AUTO_CHECK);

	/* check is it busy. */
	if(ec_flash_busy() == EC_STATE_BUSY){
			printk(KERN_ERR "flash : flash busy while enable spicmd.\n");
			return -EINVAL;
	}

	/* enable write spi flash */
	ec_write(REG_XBISPICMD, SPICMD_WRITE_ENABLE);
	timeout = EC_FLASH_TIMEOUT;
	while(timeout-- >= 0){
		if( !(ec_read(REG_XBISPICFG) & SPICFG_SPI_BUSY) )
				break;
	}
	if(timeout <= 0){
		printk(KERN_ERR "flash : flash busy while enable write spi.\n");
		return -EINVAL;
	}

	/* write the address */
	ec_write(REG_XBISPIA2, (addr & 0xff0000) >> 16);
	ec_write(REG_XBISPIA1, (addr & 0x00ff00) >> 8);
	ec_write(REG_XBISPIA0, (addr & 0x0000ff) >> 0);
	/* start action */
	ec_write(REG_XBISPICMD, SPICMD_READ_BYTE);
	timeout = EC_FLASH_TIMEOUT;
	while(timeout-- >= 0){
		if( !(ec_read(REG_XBISPICFG) & SPICFG_SPI_BUSY) )
				break;
	}
	if(timeout <= 0){
		printk(KERN_ERR "flash : start action timeout.\n");
		return -EINVAL;
	}
	*byte = ec_read(REG_XBISPIDAT);

	/* disable spicmd writing. */
	val = ec_read(REG_XBISPICFG) & (~(SPICFG_EN_SPICMD | SPICFG_AUTO_CHECK));
	ec_write(REG_XBISPICFG, val);

	return 0;
}

static int ec_write_byte(u32 addr, u8 byte)
{
	u32 timeout;
	u8 val;

	/* enable spicmd writing. */
	val = ec_read(REG_XBISPICFG);
	ec_write(REG_XBISPICFG, val | SPICFG_EN_SPICMD | SPICFG_AUTO_CHECK);

	/* check is it busy. */
	if(ec_flash_busy() == EC_STATE_BUSY){
			printk(KERN_ERR "flash : flash busy while enable spicmd.\n");
			return -EINVAL;
	}

	/* enable write spi flash */
	ec_write(REG_XBISPICMD, SPICMD_WRITE_ENABLE);
	timeout = EC_FLASH_TIMEOUT;
	while(timeout-- >= 0){
		if( !(ec_read(REG_XBISPICFG) & SPICFG_SPI_BUSY) )
				break;
	}
	if(timeout <= 0){
		printk(KERN_ERR "flash : flash busy while enable write spi.\n");
		return -EINVAL;
	}

	/* write the address */
	ec_write(REG_XBISPIA2, (addr & 0xff0000) >> 16);
	ec_write(REG_XBISPIA1, (addr & 0x00ff00) >> 8);
	ec_write(REG_XBISPIA0, (addr & 0x0000ff) >> 0);
	ec_write(REG_XBISPIDAT, byte);
	/* start action */
	ec_write(REG_XBISPICMD, SPICMD_BYTE_PROGRAM);
	timeout = EC_FLASH_TIMEOUT;
	while(timeout-- >= 0){
		if( !(ec_read(REG_XBISPICFG) & SPICFG_SPI_BUSY) )
				break;
	}
	if(timeout <= 0){
		printk(KERN_ERR "flash : start action timeout.\n");
		return -EINVAL;
	}

	/* disable spicmd writing. */
	val = ec_read(REG_XBISPICFG) & (~(SPICFG_EN_SPICMD | SPICFG_AUTO_CHECK));
	ec_write(REG_XBISPICFG, val);

	return 0;
}

static int ec_unit_erase(u8 erase_cmd, u32 addr)
{
	u32 timeout;
	u8 val;

	/* enable spicmd writing. */
	val = ec_read(REG_XBISPICFG);
	ec_write(REG_XBISPICFG, val | SPICFG_EN_SPICMD | SPICFG_AUTO_CHECK);

	/* check is it busy. */
	if(ec_flash_busy() == EC_STATE_BUSY){
			printk(KERN_ERR "flash : busy while erase.\n");
			return -EINVAL;
	}

	/* unprotect the status register */
	ec_write(REG_XBISPIDAT, 2);
	/* write the status register*/
	ec_write(REG_XBISPICMD, SPICMD_WRITE_STATUS);
	timeout = EC_FLASH_TIMEOUT;
	while(timeout-- >= 0){
		if( !(ec_read(REG_XBISPICFG) & SPICFG_SPI_BUSY) )
				break;
	}
	if(timeout <= 0){
		printk(KERN_ERR "timeout while unprotect the status register.\n");
		return -EINVAL;
	}
	/* enable write spi flash */
	ec_write(REG_XBISPICMD, SPICMD_WRITE_ENABLE);
	timeout = EC_FLASH_TIMEOUT;
	while(timeout-- >= 0){
		if( !(ec_read(REG_XBISPICFG) & SPICFG_SPI_BUSY) )
				break;
	}
	if(timeout <= 0){
		printk(KERN_ERR "timeout while enable write spi flash.\n");
		return -EINVAL;
	}
	if(erase_cmd == SPICMD_BLK_ERASE){
		ec_write(REG_XBISPIA2, (addr & 0x00ff0000) >> 16);
		ec_write(REG_XBISPIA1, (addr & 0x0000ff00) >> 8);
		ec_write(REG_XBISPIA0, (addr & 0x000000ff) >> 0);
	}

	/* erase the whole chip first */
	ec_write(REG_XBISPICMD, erase_cmd);
	timeout = 256 * EC_FLASH_TIMEOUT;
	while(timeout-- >= 0){
		if( !(ec_read(REG_XBISPICFG) & SPICFG_SPI_BUSY) )
				break;
	}
	if(timeout <= 0){
		printk(KERN_ERR "timeout while erase flash.\n");
		return -EINVAL;
	}
	/* disable spicmd writing. */
	val = ec_read(REG_XBISPICFG) & (~(SPICFG_EN_SPICMD | SPICFG_AUTO_CHECK));
	ec_write(REG_XBISPICFG, val);

	return 0;
}

static ssize_t misc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	return 0;
}

static ssize_t misc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	return 0;
}

static int misc_ioctl(struct inode * inode, struct file *filp, u_int cmd, u_long arg)
{
	void __user *ptr = (void __user *)arg;
	struct ec_reg *ecreg = (struct ec_reg *)(filp->private_data);
	u32 i, addr;
	//u32 size;
	//u8	*buf = NULL;
	u8 data, val;
	int ret = 0;
	u32 erase_addr;

//	printk(KERN_ERR "ec misc : command number 0x%x\n", cmd);
	switch (cmd) {
		case IOCTL_RDREG :
			ret = copy_from_user(ecreg, ptr, sizeof(struct ec_reg));
			if(ret){
				printk(KERN_ERR "reg read : copy from user error.\n");
				return -EFAULT;
			}
			if( (ecreg->addr > EC_MAX_REGADDR) || (ecreg->addr < EC_MIN_REGADDR) ){
				printk(KERN_ERR "reg read : out of register address range.\n");
				return -EINVAL;
			}
			ecreg->val = ec_read(ecreg->addr);
			ret = copy_to_user(ptr, ecreg, sizeof(struct ec_reg));
			if(ret){
				printk(KERN_ERR "reg read : copy to user error.\n");
				return -EFAULT;
			}
			break;
		case IOCTL_WRREG :
			ret = copy_from_user(ecreg, ptr, sizeof(struct ec_reg));
			if(ret){
				printk(KERN_ERR "reg write : copy from user error.\n");
				return -EFAULT;
			}
			if( (ecreg->addr > EC_MAX_REGADDR) || (ecreg->addr < EC_MIN_REGADDR) ){
				printk(KERN_ERR "reg write : out of register address range.\n");
				return -EINVAL;
			}
			ec_write(ecreg->addr, ecreg->val);
			break;
		case IOCTL_READ_EC :
			ret = copy_from_user(ecreg, ptr, sizeof(struct ec_reg));
			if(ret){
				printk(KERN_ERR "spi read : copy from user error.\n");
				return -EFAULT;
			}
			if( (ecreg->addr > EC_RAM_ADDR) && (ecreg->addr < EC_MAX_REGADDR) ){
				printk(KERN_ERR "spi read : out of register address range.\n");
				return -EINVAL;
			}
			ec_read_byte(ecreg->addr, &(ecreg->val));
			ret = copy_to_user(ptr, ecreg, sizeof(struct ec_reg));
			if(ret){
				printk(KERN_ERR "spi read : copy to user error.\n");
				return -EFAULT;
			}
			break;

		case IOCTL_PROGRAM_IE :
				if(get_user( (ecinfo.start_addr), (u32 *)ptr) ){
					printk(KERN_ERR "program ec : get user error.\n");
					return -EFAULT;
				}
				if(get_user( (ecinfo.size), (u32 *)((u32 *)ptr + 1)) ){
					printk(KERN_ERR "program ec : get user error.\n");
					return -EFAULT;
				}

				if( (ecinfo.size + ecinfo.start_addr) > IE_CONTENT_MAX_SIZE ){
					printk(KERN_ERR "program ie : size out of limited.\n");
					return -EINVAL;
				}
				if(ecinfo.size > 0x10000){
					printk(KERN_ERR "program ie : size is out of 64KB, too big...\n");
					return -EINVAL;
				}

				ecinfo.buf = (u8 *)kmalloc(ecinfo.size, GFP_KERNEL);
				if(ecinfo.buf == NULL){
					printk(KERN_ERR "program ie : kmalloc failed.\n");
					return -ENOMEM;
				}
				ret = copy_from_user(ecinfo.buf, ((u8 *)ptr + 8), ecinfo.size);
				if(ret){
					printk(KERN_ERR "program ie : copy from user error.\n");
					kfree(ecinfo.buf);
					ecinfo.buf = NULL;
					return -EFAULT;
				}

				erase_addr = IE_START_ADDR + ecinfo.start_addr;
				ret = ec_unit_erase(SPICMD_BLK_ERASE, erase_addr);
				if(ret){
					printk(KERN_ERR "program ie : erase block failed.\n");
					kfree(ecinfo.buf);
					ecinfo.buf = NULL;
					return -EINVAL;
				}

				i = 0;
				addr = IE_START_ADDR + ecinfo.start_addr;
				while(i < ecinfo.size){
					data = *(ecinfo.buf + i);
					ec_write_byte(addr, data);
					ec_read_byte(addr, &val);
					if(val != data){
						ec_write_byte(addr, data);
						ec_read_byte(addr, &val);
						if(val != data){
							printk("IE : Second flash program failed at:\t");
							printk("addr : 0x%x, source : 0x%x, dest: 0x%x\n", 
											addr, data, val);
							printk("This should not happened... STOP\n");
							break;						
						}
					}
					i++;
					addr++;
				}
				kfree(ecinfo.buf);
				ecinfo.buf = NULL;
				break;
		default :
				break;
	}

	return 0;
}

static int misc_compat_ioctl(struct file *file, unsigned long cmd, unsigned long arg)
{
	return misc_ioctl(file->f_dentry->d_inode, file, cmd, arg);
}

static int misc_open(struct inode * inode, struct file * filp)
{
	struct ec_reg *ecreg = NULL;
	ecreg = kmalloc(sizeof(struct ec_reg), GFP_KERNEL);
	if (ecreg) {
		filp->private_data = ecreg;
	}

	return ecreg ? 0 : -ENOMEM;
}

static int misc_release(struct inode * inode, struct file * filp)
{
	struct ec_reg *ecreg = (struct ec_reg *)(filp->private_data);

	filp->private_data = NULL;
	kfree(ecreg);

	return 0;
}

static struct file_operations ecmisc_fops = {
	.owner		= THIS_MODULE,
	.open		= misc_open,
	.release	= misc_release,
	.read		= NULL,
	.write		= NULL,
#ifdef	CONFIG_64BIT
	.compat_ioctl		= misc_compat_ioctl,
#else
	.ioctl		= misc_ioctl,
#endif
};

/*********************************************************/

static struct miscdevice ecmisc_device = {
	.minor		= ECMISC_MINOR_DEV,
	.name		= EC_MISC_DEV,
	.fops		= &ecmisc_fops
};

static int __init ecmisc_init(void)
{
	int ret;

	printk(KERN_INFO "EC misc device init.\n");
	ret = misc_register(&ecmisc_device);

	return ret;
}

static void __exit ecmisc_exit(void)
{
	misc_deregister(&ecmisc_device);
}

module_init(ecmisc_init);
module_exit(ecmisc_exit);

MODULE_AUTHOR("liujl <liujl@lemote.com>");
MODULE_DESCRIPTION("KB3310 resources misc Management");
MODULE_LICENSE("GPL");
