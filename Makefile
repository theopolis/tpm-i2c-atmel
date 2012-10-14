obj-m += atmel-i2c-tpm.o
KDIR := linux
PWD := $(shell pwd)
CROSS := ARCH=arm CROSS_COMPILE=arm-unknown-linux-gnueabi-
all: 
		$(MAKE) $(CROSS) -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
		$(MAKE) $(CROSS) -C $(KDIR) SUBDIRS=$(PWD) clean
