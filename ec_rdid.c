/*
 * EC(Embedded Controller) KB3310B read EC ROM ID driver on Linux
 * Author	: huangw <huangw@lemote.com>
 * Date		: 2009-08-14
 * 
 * NOTE :
 * 		1, Read ec rom id from flash chip.
 * 			Manufacturer	Product ID
 *			  SPANSION		  0x01
 *			  MXIC	 		  0xC2
 *			  AMIC	 		  0x37
 *			  EONIC	 		  0x1C
 */ 

/*******************************************************************/

#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <asm/delay.h>

/*******************************************************************/
/* 
 * The following registers are determined by the EC index configuration.
 * 1, fill the PORT_HIGH as EC register high part.
 * 2, fill the PORT_LOW as EC register low part.
 * 3, fill the PORT_DATA as EC register write data or get the data from it.
 */
#define	EC_IO_PORT_HIGH	0x0381
#define	EC_IO_PORT_LOW	0x0382
#define	EC_IO_PORT_DATA	0x0383

/* read ec rom id from flash chip */
#define	EC_ROM_ID_SIZE	3
unsigned char  ec_rom_id[EC_ROM_ID_SIZE];
/* the ec flash rom id number */
#define	EC_ROM_PRODUCT_ID_SPANSION	0x01
#define	EC_ROM_PRODUCT_ID_MXIC		0xC2
#define	EC_ROM_PRODUCT_ID_AMIC		0x37
#define	EC_ROM_PRODUCT_ID_EONIC		0x1C

#define	CMD_INIT_IDLE_MODE	0xdd
#define	CMD_EXIT_IDLE_MODE	0xdf

/* command checkout timeout including cmd to port or state flag check */
#define	EC_CMD_TIMEOUT		0x1000 

/* EC access port for sci communication */
#define	EC_CMD_PORT		0x66
#define	EC_STS_PORT		0x66

/* ec internal register */
#define REG_XBISPIDAT   0xFEAB
#define REG_XBISPICMD   0xFEAC
#define REG_XBISPICFG   0xFEAD

#define	REG_POWER_MODE		0xF710
#define	FLAG_NORMAL_MODE	0x00
#define	FLAG_IDLE_MODE		0x01
#define	FLAG_RESET_MODE		0x02

/* ec delay time 500us for register and status access */
#define	EC_REG_DELAY	500	//unit : us

/* this spinlock is dedicated for ec_read & ec_write function */
DEFINE_SPINLOCK(index_access_lock);
/* this spinlock is dedicated for 62&66 ports access */
DEFINE_SPINLOCK(port_access_lock);

/* Read EC ROM ID device name */
//#define	RDECID_DEV		"ecromid"

/* Ec misc device minor number */
//#define	RDECID_MINOR_DEV	MISC_DYNAMIC_MINOR	

/*******************************************************************/

/* read a byte from EC registers throught index-io */
unsigned char ec_read(unsigned short addr)
{
	unsigned char value;
	unsigned long flags;

	spin_lock_irqsave(&index_access_lock, flags);
	outb( (addr & 0xff00) >> 8, EC_IO_PORT_HIGH );
	outb( (addr & 0x00ff), EC_IO_PORT_LOW );
	value = inb(EC_IO_PORT_DATA);
	spin_unlock_irqrestore(&index_access_lock, flags);

	return value;
}

/* write a byte to EC registers throught index-io */
void ec_write(unsigned short addr, unsigned char val)
{
	unsigned long flags;

	spin_lock_irqsave(&index_access_lock, flags);
	outb( (addr & 0xff00) >> 8, EC_IO_PORT_HIGH );
	outb( (addr & 0x00ff), EC_IO_PORT_LOW );
	outb( val, EC_IO_PORT_DATA );
	inb( EC_IO_PORT_DATA );	// flush the write action
	spin_unlock_irqrestore(&index_access_lock, flags);

	return;
}

int ec_query_seq(unsigned char cmd)
{
	int timeout;
	unsigned char status;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&port_access_lock, flags);

	/* make chip goto reset mode */
	udelay(EC_REG_DELAY);
	outb(cmd, EC_CMD_PORT);
	udelay(EC_REG_DELAY);

	/* check if the command is received by ec */
	timeout = EC_CMD_TIMEOUT;
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
		ret = -EINVAL;
	}

	spin_unlock_irqrestore(&port_access_lock, flags);

	return ret;
}

/* make ec goto idle mode */
int ec_init_idle_mode(void)
{
	int timeout;
	unsigned char status = 0;
	int ret = 0;

	ec_query_seq(CMD_INIT_IDLE_MODE);

	/* make the action take active */
	timeout = EC_CMD_TIMEOUT;
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
		ret = -EINVAL;
	}

	//PRINTK_DBG(KERN_INFO "entering idle mode ok...................\n");

	return ret;
}

/* make ec exit from idle mode */
int ec_exit_idle_mode(void)
{
	ec_query_seq(CMD_EXIT_IDLE_MODE);

	//PRINTK_DBG(KERN_INFO "exit idle mode ok...................\n");
	
	return 0;
}
int misc_get_ec_rom_id(void)
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

	printk("EC ROM ID : 0x%x, 0x%x, 0x%x\n", ec_rom_id[0], ec_rom_id[1], ec_rom_id[2]);
	switch(ec_rom_id[0]){
		case EC_ROM_PRODUCT_ID_SPANSION :
			printk("EC ROM manufacturer: SPANSION.\n");
			break;
		case EC_ROM_PRODUCT_ID_MXIC : 
			printk("EC ROM manufacturer: MXIC.\n");
			break;
		case EC_ROM_PRODUCT_ID_AMIC :
			printk("EC ROM manufacturer: AMIC.\n");
			break;
		case EC_ROM_PRODUCT_ID_EONIC :
			printk("EC ROM manufacturer: EONIC.\n");
			break;

		default :
			printk("EC : not supported flash chip type.\n");
			break;
	}

	return 0;
}

static int __init rdid_init(void)
{
	misc_get_ec_rom_id();

	return 0;
}

static void __exit rdid_exit(void)
{
	printk("Read EC ROM ID device exit.\n");
}

module_init(rdid_init);
module_exit(rdid_exit);

MODULE_AUTHOR("huangw <huangw@lemote.com>");
MODULE_DESCRIPTION("KB3310 ROM resources Management");
MODULE_LICENSE("GPL");
