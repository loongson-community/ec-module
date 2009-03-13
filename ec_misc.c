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
/* read ec rom id from flash chip */
#define	NO_ROM_ID_NEEDED	// we need no rom id for read operation
#ifndef	NO_ROM_ID_NEEDED
#define	EC_ROM_ID_SIZE	3
unsigned char  ec_rom_id[EC_ROM_ID_SIZE];
#endif

struct ec_info	ecinfo;


/***************************************************************/
#if 	1
DEFINE_SPINLOCK(access_port_lock);

/*
 * ec_query_seq
 * this function is used for ec command writing and the corresponding status query 
 */
int ec_query_seq(unsigned char cmd)
{
	int timeout;
	unsigned char status;
	unsigned long flags;

	spin_lock_irqsave(&access_port_lock, flags);

	/* make chip goto reset mode */
	udelay(EC_REG_DELAY);
	outb(cmd, EC_CMD_PORT);
	udelay(EC_REG_DELAY);

	/* check if the command is received by ec */
	timeout = 0x1000;
	status = inb(EC_STS_PORT);
	while(timeout--){
		if(status & (1 << 1)){
			status = inb(EC_STS_PORT);
			udelay(EC_REG_DELAY);
			continue;
		}
		break;
	}
	
	if(timeout <= 0){
		printk(KERN_ERR "EC QUERY SEQ : deadable error : timeout...\n");
		return -EINVAL;
	}else{
		PRINTK_DBG(KERN_INFO "(%x/%d)ec issued command %d status : 0x%x\n", timeout, 0x1000 - timeout, cmd, status);
	}

	spin_unlock_irqrestore(&access_port_lock, flags);

	return 0;
}

EXPORT_SYMBOL(ec_query_seq);
#endif

/* enable the chip reset mode */
int ec_init_reset_mode(void)
{
	int timeout;
	unsigned char status = 0;
	int ret;
	
	/* make chip goto reset mode */
	ret = ec_query_seq(CMD_INIT_RESET_MODE);
	if(ret < 0){
		printk(KERN_ERR "ec init reset mode failed.\n");
		return ret;
	}

	/* make the action take active */
	timeout = 1000;
	status = ec_read(REG_POWER_MODE) & FLAG_RESET_MODE;
	while(timeout--){
		if(status){
			udelay(EC_REG_DELAY);
			break;
		}
		status = ec_read(REG_POWER_MODE) & FLAG_RESET_MODE;
		udelay(EC_REG_DELAY);
	}
	if(timeout <= 0){
		printk(KERN_ERR "ec rom fixup : can't check reset status.\n");
		return -EINVAL;
	}else{
		PRINTK_DBG(KERN_INFO "(%d/%d)reset 0xf710 :  0x%x\n", timeout, 1000 - timeout, status);		
	}

	/* set MCU to reset mode */
	udelay(EC_REG_DELAY);
	status = ec_read(REG_PXCFG);
	status |= (1 << 0);
	ec_write(REG_PXCFG, status);
	udelay(EC_REG_DELAY);//

	/* disable FWH/LPC */
	udelay(EC_REG_DELAY);
	status = ec_read(REG_LPCCFG);
	status &= ~(1 << 7);
	ec_write(REG_LPCCFG, status);
	udelay(EC_REG_DELAY);//

	PRINTK_DBG(KERN_INFO "entering reset mode ok..............\n");

	return 0;
}

/* make ec exit from reset mode */
void ec_exit_reset_mode(void)
{
	unsigned char regval;

	udelay(EC_REG_DELAY);
	regval = ec_read(REG_LPCCFG);
	regval |= (1 << 7);
	ec_write(REG_LPCCFG, regval);
	regval = ec_read(REG_PXCFG);
	regval &= ~(1 << 0);
	ec_write(REG_PXCFG, regval);
	PRINTK_DBG(KERN_INFO "exit reset mode ok..................\n");

	return;
}

/* make ec goto idle mode */
static int ec_init_idle_mode(void)
{
	int timeout;
	unsigned char status = 0;

	ec_query_seq(CMD_INIT_IDLE_MODE);

	/* make the action take active */
	timeout = 1000;
	status = ec_read(REG_POWER_MODE) & FLAG_IDLE_MODE;
	while(timeout--){
		if(status){
			udelay(EC_REG_DELAY);
			break;
		}
		status = ec_read(REG_POWER_MODE) & FLAG_IDLE_MODE;
		udelay(EC_REG_DELAY);
	}
	if(timeout <= 0){
		printk(KERN_ERR "ec rom fixup : can't check out the status.\n");
		return -EINVAL;
	}else{
		PRINTK_DBG(KERN_INFO "(%d/%d)0xf710 :  0x%x\n", timeout, 1000 - timeout, ec_read(REG_POWER_MODE));
	}

	PRINTK_DBG(KERN_INFO "entering idle mode ok...................\n");

	return 0;
}

/* make ec exit from idle mode */
static int ec_exit_idle_mode(void)
{

	ec_query_seq(CMD_EXIT_IDLE_MODE);

	PRINTK_DBG(KERN_INFO "exit idle mode ok...................\n");
	
	return 0;
}

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
static int ec_read_byte(unsigned int addr, unsigned char *byte)
{
	unsigned int timeout;
	unsigned char val;

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
#ifndef	NO_ROM_ID_NEEDED
	switch(ec_rom_id[0]){
		case EC_ROM_PRODUCT_ID_SPANSION :
			ec_write(REG_XBISPICMD, SPICMD_READ_BYTE);
			//ec_write(REG_XBISPICMD, SPICMD_HIGH_SPEED_READ);
			break;
		case EC_ROM_PRODUCT_ID_MXIC : 
			ec_write(REG_XBISPICMD, SPICMD_HIGH_SPEED_READ);
			break;
		case EC_ROM_PRODUCT_ID_AMIC :
			ec_write(REG_XBISPICMD, SPICMD_HIGH_SPEED_READ);
			break;
		case EC_ROM_PRODUCT_ID_EONIC :
			ec_write(REG_XBISPICMD, SPICMD_HIGH_SPEED_READ);
			break;
		default :
			printk("EC : not supported flash chip type, using default read action instead.\n");
			ec_write(REG_XBISPICMD, SPICMD_READ_BYTE);
			break;
	}
#else
	ec_write(REG_XBISPICMD, SPICMD_HIGH_SPEED_READ);
#endif
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

/* write one byte to ec rom */
static int ec_write_byte(unsigned int addr, unsigned char byte)
{
	unsigned int timeout;
	unsigned char val;

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

	if(ec_flash_busy()){
			printk(KERN_ERR "flash : flash busy.\n");
			return -EINVAL;
	}

	return 0;
}

static int ec_unit_erase(unsigned char erase_cmd, unsigned int addr)
{
	unsigned int timeout;
	unsigned char val;

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

static int ec_program_piece(struct ec_info *info)
{
	unsigned int addr = info->start_addr + IE_START_ADDR;
	unsigned long size = info->size;
	unsigned int pieces;
	unsigned char *ptr;
	unsigned long timeout;
	int i, j;
	// int k;
	unsigned char status;
	int ret = 0;

	/* calculate the number of pieces and set the rest buffer with 0xff by default */
	ptr = info->buf;
	pieces = size / PIECE_SIZE;	

//	printk("pieces : %d\n", pieces);
retry :
	for(i = 0; i < pieces; i++){
	
		/* check status for chip ready */
		timeout = EC_FLASH_TIMEOUT;
		while(timeout-- > 0){
			status = ec_read(PIECE_STATUS_REG);
			if( (status & PIECE_STATUS_PROGRAM_DONE) == 1 ){
				if( (status & PIECE_STATUS_PROGRAM_ERROR) == 0 ){
					break;
				}else{
					goto retry;
				}
			}		
			udelay(EC_REG_DELAY);
		}
		if(timeout <= 0){
			printk(KERN_ERR "timeout for check piece status.\n");
			return -EINVAL;
		}
		
		/* program piece action */
		if( (addr == IE_START_ADDR) && (i == 0) ){
			ec_write(PIECE_START_ADDR, FIRST_PIECE_YES);
		}else{
			ec_write(PIECE_START_ADDR, FIRST_PIECE_NO);
		}
		ec_write(PIECE_START_ADDR + 1, (addr & 0xff) + i * PIECE_SIZE);
		ec_write(PIECE_START_ADDR + 2, (addr & 0xff00) >> 8);
		ec_write(PIECE_START_ADDR + 3, (addr & 0xff0000) >> 16);
		for(j = 0; j < PIECE_SIZE; j++){
			ec_write(PIECE_START_ADDR + j + 4, ptr[j + i * PIECE_SIZE]);
		}

	//	for(k = 0; k < PIECE_SIZE + 4; k++){
	//		printk("internal %d, value 0x%x\n", k, ec_read(PIECE_START_ADDR + k));
	//	}

		/* make chip goto program bytes */
		ret = ec_query_seq(CMD_PROGRAM_PIECE);
		if(ret < 0){
			printk(KERN_ERR "ec issue program byte failed.\n");
			return ret;
		}

	}

	return 0;	
}

static int ec_program_rom(struct ec_info *info)
{
	unsigned int addr = 0;
	unsigned long size = 0;
	unsigned char *ptr = NULL;
	unsigned char data;
	unsigned char val = 0;
	int ret = 0;
	int i;

	ret = ec_init_reset_mode();
	if(ret < 0){
		return ret;
	}

	size = info->size;
	ptr  = info->buf;
	addr = info->start_addr + EC_START_ADDR;
    PRINTK_DBG(KERN_INFO "starting update ec ROM..............\n");

	ret = ec_unit_erase(SPICMD_BLK_ERASE, addr);
	if(ret){
		printk(KERN_ERR "program ec : erase block failed.\n");
		return -EINVAL;
	}

	i = 0;
	while(i < size){
		data = *(ptr + i);
		ec_write_byte(addr, data);
		ec_read_byte(addr, &val);
		if(val != data){
			ec_write_byte(addr, data);
			ec_read_byte(addr, &val);
			if(val != data){
				printk("EC : Second flash program failed at:\t");
				printk("addr : 0x%x, source : 0x%x, dest: 0x%x\n", addr, data, val);
				printk("This should not happened... STOP\n");
				break;				
			}
		}
		i++;
		addr++;
	}

	/* for security */
	udelay(100000);
	/* exit from the reset mode */
	ec_exit_reset_mode();

	return 0;
}

static int misc_ioctl(struct inode * inode, struct file *filp, u_int cmd, u_long arg)
{
	void __user *ptr = (void __user *)arg;
	struct ec_reg *ecreg = (struct ec_reg *)(filp->private_data);
	int ret = 0;
	int i;
	u32 temp_size = 0;

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
			if( (ecinfo.size > EC_CONTENT_MAX_SIZE) 
				|| (ecinfo.start_addr > EC_CONTENT_MAX_SIZE) ){
				printk(KERN_ERR "program ie : size OR start_addr is out of 64KB, we only support with 64KB, too big...\n");
				return -EINVAL;
			}

			if(ecinfo.size % PIECE_SIZE){
				temp_size = ecinfo.size;
				ecinfo.size = (ecinfo.size / PIECE_SIZE + 1) * PIECE_SIZE;
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
		
			/* fill the PIECE pad */	
			for(i = temp_size; i < ecinfo.size; i++){
				*(u8 *)((u8 *)ecinfo.buf + i) = 0xff;
			}
			
			ec_program_piece(&ecinfo);
			
			kfree(ecinfo.buf);
			ecinfo.buf = NULL;
			break;
		case IOCTL_PROGRAM_EC :
			ecinfo.start_addr = EC_START_ADDR;
			if(get_user( (ecinfo.size), (u32 *)ptr) ){
				printk(KERN_ERR "program ec : get user error.\n");
				return -EFAULT;
			}
			if( (ecinfo.size) > EC_CONTENT_MAX_SIZE ){
				printk(KERN_ERR "program ec : size out of limited.\n");
				return -EINVAL;
			}
			ecinfo.buf = (u8 *)kmalloc(ecinfo.size, GFP_KERNEL);
			if(ecinfo.buf == NULL){
				printk(KERN_ERR "program ec : kmalloc failed.\n");
				return -ENOMEM;
			}
			ret = copy_from_user(ecinfo.buf, ((u8 *)ptr + 4), ecinfo.size);
			if(ret){
				printk(KERN_ERR "program ec : copy from user error.\n");
				kfree(ecinfo.buf);
				ecinfo.buf = NULL;
				return -EFAULT;
			}
	
			ec_program_rom(&ecinfo);
	
			kfree(ecinfo.buf);
			ecinfo.buf = NULL;
			break;

		default :
			break;
	}

	return 0;
}

static long misc_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
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
	.compat_ioctl = misc_compat_ioctl,
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

#ifndef	NO_ROM_ID_NEEDED
static int misc_get_ec_rom_id(void)
{
	unsigned char regval, i;
	int ret = 0;
	
	/* entering ec idle mode */
	ret = ec_init_idle_mode();
	if(ret < 0){
		return ret;
	}

	/* get product id from ec rom */
	udelay(EC_REG_DELAY);
	regval = ec_read(REG_XBISPICFG);
	regval |= 0x18;
	ec_write(REG_XBISPICFG, regval);
	udelay(EC_REG_DELAY);
	
	ec_write(REG_XBISPICMD, 0x9f);
	while( (ec_read(REG_XBISPICFG)) & (1 << 1) );
	
	for(i = 0; i < EC_ROM_ID_SIZE; i++){
		ec_write(REG_XBISPICMD, 0x00);
		while( (ec_read(REG_XBISPICFG)) & (1 << 1) );
		ec_rom_id[i] = ec_read(REG_XBISPIDAT);
	}
	udelay(EC_REG_DELAY);
	regval = ec_read(REG_XBISPICFG);
	regval &= 0xE7;
	ec_write(REG_XBISPICFG, regval);
	udelay(EC_REG_DELAY);

	/* ec exit from idle mode */
	ret = ec_exit_idle_mode();
	if(ret < 0){
		return ret;
	}

	PRINTK_DBG(KERN_INFO "EC ROM ID : 0x%x, 0x%x, 0x%x\n", ec_rom_id[0], ec_rom_id[1], ec_rom_id[2]);

	return 0;
}
#endif

static int __init ecmisc_init(void)
{
	int ret;

#ifndef	NO_ROM_ID_NEEDED
	ret = misc_get_ec_rom_id();
	if(ret){
		return ret;
	}
#endif
	printk(KERN_INFO "EC misc device init.\n");
	ret = misc_register(&ecmisc_device);

	return ret;
}

static void __exit ecmisc_exit(void)
{
	printk(KERN_INFO "EC misc device exit.\n");
	misc_deregister(&ecmisc_device);
}

module_init(ecmisc_init);
module_exit(ecmisc_exit);

MODULE_AUTHOR("liujl <liujl@lemote.com>");
MODULE_DESCRIPTION("KB3310 resources misc Management");
MODULE_LICENSE("GPL");
