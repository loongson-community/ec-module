#KERNEL_VERSION	:= `uname -r`
#KERNEL_DIR	:= /lib/modules/$(KERNEL_VERSION)/build
#KERNEL_DIR	:= /home/users/jlliu/git-linux-kernel
#KERNEL_DIR	:= /home/huangw/MyWorks/git_works/linux-2.6.27-89inch
#KERNEL_DIR	:= /home/huangw/MyWorks/git-works/linux_loongson
KERNEL_DIR	:= /home/yh/linux-2.6.27.1-lemote
#KERNEL_DIR	:= /home/yh/Work/linux-2.6-lemote-64
INSTALL_MOD_DIR	:= char/

PWD		:= $(shell pwd)
CROSS_COMPILE	:= mipsel-linux-

obj-m		:= ec_batd.o ec_miscd.o ec_ftd.o pmon_flash.o
ec_batd-objs	:= ec_bat.o 
ec_ftd-objs	:= ec_ft.o
ec_miscd-objs	:= ec_misc.o
pmon_flash-objs = pmon.o

all: ec_batd ec_ftd ec_miscd pmon_flash

ec_batd:
	@echo "Building Embedded Controller KB3310 driver..."
	@(cd $(KERNEL_DIR) && make -C $(KERNEL_DIR) SUBDIRS=$(PWD) CROSS_COMPILE=$(CROSS_COMPILE) modules)

ec_ftd:
	@(cd $(KERNEL_DIR) && make -C $(KERNEL_DIR) SUBDIRS=$(PWD) CROSS_COMPILE=$(CROSS_COMPILE) modules)

ec_miscd:
	@(cd $(KERNEL_DIR) && make -C $(KERNEL_DIR) SUBDIRS=$(PWD) CROSS_COMPILE=$(CROSS_COMPILE) modules)

pmon_flash:
	@(cd $(KERNEL_DIR) && make -C $(KERNEL_DIR) SUBDIRS=$(PWD) CROSS_COMPILE=$(CROSS_COMPILE) modules)

install:
	@echo "Installing Embeded Controller KB3310 ..."
	@(cd $(KERNEL_DIR) && make -C $(KERNEL_DIR) SUBDIRS=$(PWD) INSTALL_MOD_DIR=$(INSTALL_MOD_DIR) INSTALL_MOD_PATH=$(INSTALL_MOD_PATH) modules_install)
	depmod -ae $(KERNEL_VERSION)

clean:
	-rm -f *.o *.ko .*.cmd .*.flags *.mod.c Module.symvers modules.order version.h
	-rm -rf .tmp_versions

