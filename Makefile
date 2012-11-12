obj-m += tpm_i2c_atmel.o
KDIR := /usr/local/src/linux/
PWD := $(shell pwd)
CROSS := ARCH=arm CROSS_COMPILE=/usr/local/armv7-hard/bin/arm-unknown-linux-gnueabi-
all: 
		$(MAKE) $(CROSS) -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
		$(MAKE) $(CROSS) -C $(KDIR) SUBDIRS=$(PWD) clean
