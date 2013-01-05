obj-m += tpm_i2c_atmel.o

E_CFLAGS := 
PWD 	 := $(shell pwd)

ifneq ($(KERNEL_DIR),)
KDIR := $(KERNEL_DIR)
else
KDIR := /usr/local/src/linux/
endif

ifeq ($(CROSS_COMPILE),)
CROSS_COMPILE := /usr/local/armv7-hard/bin/arm-unknown-linux-gnueabi-
endif

CROSS := ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE)

ifneq ($(DEBUG),)
E_CFLAGS += -DEBUG
endif

all: 
		$(MAKE) $(CROSS) -C $(KDIR) SUBDIRS=$(PWD) EXTRA_CFLAGS="$(E_CFLAGS)" modules

clean:
		$(MAKE) $(CROSS) -C $(KDIR) SUBDIRS=$(PWD) clean
