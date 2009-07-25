#KERNEL_VERSION	:= `uname -r`
#KERNEL_DIR	:= /home/software/liujl/linux-2.6.27.1-89inch
KERNEL_DIR	:= /home/huangw/MyWorks/git-works/linux_2.6.27.1-lemote
INSTALL_MOD_DIR	:= char/

PWD		:= $(shell pwd)
CROSS_COMPILE	:= mipsel-linux-

obj-m			:= ec_miscd.o ec_batd.o ec_ftd.o ec_scid.o io_msr_debug.o pmon_flash.o ec_brightness.o

ec_miscd-objs	:= ec_misc.o
ec_batd-objs	:= ec_bat.o 
ec_ftd-objs		:= ec_ft.o
ec_scid-objs	:= ec_sci.o
#io_msr_debug-objs	:= io_msr_debug.o
pmon_flash-objs	:= pmon.o
#ec_brightness-objs := ec_brightness.o

all: ec_miscd ec_batd ec_ftd ec_scid io_msr_debug pmon_flash ec_brightness

ec_miscd:
	@echo "Building Embedded Controller KB3310 driver..."
	@(cd $(KERNEL_DIR) && make -C $(KERNEL_DIR) SUBDIRS=$(PWD) CROSS_COMPILE=$(CROSS_COMPILE) modules)

ec_batd:
	@(cd $(KERNEL_DIR) && make -C $(KERNEL_DIR) SUBDIRS=$(PWD) CROSS_COMPILE=$(CROSS_COMPILE) modules)

ec_ftd:
	@(cd $(KERNEL_DIR) && make -C $(KERNEL_DIR) SUBDIRS=$(PWD) CROSS_COMPILE=$(CROSS_COMPILE) modules)

ec_scid:
	@(cd $(KERNEL_DIR) && make -C $(KERNEL_DIR) SUBDIRS=$(PWD) CROSS_COMPILE=$(CROSS_COMPILE) modules)

io_msr_debug:
	@(cd $(KERNEL_DIR) && make -C $(KERNEL_DIR) SUBDIRS=$(PWD) CROSS_COMPILE=$(CROSS_COMPILE) modules)

pmon_flash:
	@(cd $(KERNEL_DIR) && make -C $(KERNEL_DIR) SUBDIRS=$(PWD) CROSS_COMPILE=$(CROSS_COMPILE) modules)

ec_brightness:
	@(cd $(KERNEL_DIR) && make -C $(KERNEL_DIR) SUBDIRS=$(PWD) CROSS_COMPILE=$(CROSS_COMPILE) modules)

install:
	@echo "Installing Embeded Controller KB3310 ..."
	@(cd $(KERNEL_DIR) && make -C $(KERNEL_DIR) SUBDIRS=$(PWD) INSTALL_MOD_DIR=$(INSTALL_MOD_DIR) INSTALL_MOD_PATH=$(INSTALL_MOD_PATH) modules_install)
	depmod -ae $(KERNEL_VERSION)

clean:
	-rm -f *.o *.ko .*.cmd .*.flags *.mod.c Module.symvers modules.order version.h
	-rm -rf .tmp_versions

