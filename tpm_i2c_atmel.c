/*
 * Copyright (C) 2012 V Lab Technologies
 *
 * Authors:
 * Teddy Reed <teddy.reed@gmail.com>
 *
 * Device driver for TCG/TCPA TPM (trusted platform module).
 * Specifications at www.trustedcomputinggroup.org
 *
 * This device driver implements the TPM interface as defined in
 * the TCG TPM Interface Spec version 1.2.
 *
 * It is based on the original tpm_tis device driver from Leendert van
 * Dorn and Kyleen Hall, the Infineon driver from Peter Huewe and the
 * Atmel AVR-based TPM development board firmware.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

/*
 * Beaglebone R5 testing:
 * MUX MODE for i2c2
 * P9 19: Mode 2 (SLC)
 * P9 20: Mode 2 (SDA)
 * echo tpm_i2c_atmel 0x29 > /sys/bus/i2c/devices/i2c-3/new_device
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/i2c.h>
#include <generated/asm/errno.h>

#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/delay.h>

#include "tpm.h"
#include "tpm_i2c.h"

/** Found in AVR code and in Max's implementation **/
#define TPM_BUFSIZE 1024

struct tpm_i2c_atmel_dev {
	struct i2c_client *client;
	u8 buf[TPM_BUFSIZE];
	struct tpm_chip *chip;
};

struct tpm_i2c_atmel_dev tpm_dev;
static struct i2c_driver tpm_tis_i2c_driver;

static int tpm_i2c_read(u8 *buffer, size_t len);
static int tpm_tis_i2c_read (struct file *file, char __user *buf, size_t count, loff_t *offp);
static int tpm_tis_i2c_write (struct file *file, const char __user *buf, size_t count, loff_t *offp);
static int tpm_tis_i2c_open (struct inode *inode, struct file *pfile);
static int tpm_tis_i2c_release (struct inode *inode, struct file *pfile);

static int __devinit tpm_tis_i2c_probe (struct i2c_client *client, const struct i2c_device_id *id);
static int __devexit tpm_tis_i2c_remove(struct i2c_client *client);
static int __devinit tpm_tis_i2c_init (void);
static void __devexit tpm_tis_i2c_exit (void);


static int tpm_i2c_read(u8 *buffer, size_t len)
{
	int rc;
	u32 trapdoor = 0;
	const u32 trapdoor_limit = 60000; /* 5min with base 5mil seconds */

	struct i2c_msg msg1 = { tpm_dev.client->addr, I2C_M_RD, len, buffer };

	/** should lock the device **/
	printk(KERN_INFO "tpm_i2c_atmel: read length requested %i\n", len);

	do {
		rc = i2c_transfer(tpm_dev.client->adapter, &msg1, 1);
		if (rc > 0x00)
			break;
		trapdoor++;
		msleep(5);
	} while (trapdoor < trapdoor_limit);

	if (trapdoor >= trapdoor_limit)
		return -EFAULT;

	/** should unlock device **/
	return rc;
}

static int tpm_tis_i2c_read (struct file *file, char __user *buf, size_t count, loff_t *offp)
{
	int rc = 0;
	int i;
	int expected;

	printk(KERN_INFO "tpm_i2c_atmel: read count %i\n", count);

	memset(tpm_dev.buf, 0x00, TPM_BUFSIZE);
	rc = tpm_i2c_read(tpm_dev.buf, TPM_HEADER_SIZE); /* returns status of read */

	//expected = be32_to_cpu(*(__be32 *)(buf + 2));
	expected = tpm_dev.buf[4];
	expected = expected << 8;
	expected += tpm_dev.buf[5];

	printk(KERN_INFO "tpm_i2c_atmel: read dump:");
	for (i = 0; i < 30; i++) {
		printk("0x%X ", tpm_dev.buf[i]);
	}

	printk(KERN_INFO "tpm_i2c_atmel: read expected %i\n", expected);
	if (expected <= TPM_HEADER_SIZE) {
		/* finished here */
		goto to_user;
	}

	printk(KERN_INFO "tpm_i2c_atmel: need to read %i\n", expected);

	/* looks like it reads the entire expected, into the base of the buffer (from Max's code) */
	rc = tpm_i2c_read(tpm_dev.buf, expected);
	/** signal ready optional? **/

	printk(KERN_INFO "tpm_i2c_atmel: read dump:");
	for (i = 0; i < 30; i++) {
		printk("0x%X ", tpm_dev.buf[i]);
	}


to_user:
	if (copy_to_user(buf, tpm_dev.buf, expected)) {
		return -EFAULT;
	}

	//rc = i2c_master_recv(tpm_dev.client, tpm_dev.buf, expected);
	printk(KERN_INFO "tpm_i2c_atmel: read finished\n");

	return expected;
}

static int tpm_tis_i2c_write (struct file *file, const char __user *buf, size_t count, loff_t *offp)
{
	int rc;
	int i;

	struct i2c_msg msg1 = { tpm_dev.client->addr, 0, count, tpm_dev.buf };

	rc = -EIO;
	if (count > TPM_BUFSIZE) {
		return -EINVAL;
	}

	/** should lock the device
	 * **/
	memset(tpm_dev.buf, 0x00, TPM_BUFSIZE);
	if (copy_from_user(tpm_dev.buf, buf, count)) {
		return -EFAULT;
	}

	printk(KERN_INFO "tpm_i2c_atmel: write dump:");
	for (i = 0; i < count; i++) {
		printk("0x%X ", tpm_dev.buf[i]);
	}

	rc = i2c_transfer(tpm_dev.client->adapter, &msg1, 1);

	printk(KERN_INFO "tpm_i2c_atmel: write status %i\n", rc);

	/** should unlock device **/
	if (rc <= 0)
		return -EIO;

	return count;
}

static int tpm_tis_i2c_open (struct inode *inode, struct file *pfile)
{
	return 0;
}

static int tpm_tis_i2c_release (struct inode *inode, struct file *pfile)
{
	return 0;
}

/* Board info modification
 * i2c2 on Beaglebone for 3.2.0 kernel is bus: i2c-3
static struct i2c_board_info __initdata beagle_i2c_devices[] = {
		{ I2C_BOARD_INFO("tpm_i2c_atmel", 0x29), }
};
*/

static const struct file_operations tis_ops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.open = tpm_tis_i2c_open,
	.read = tpm_tis_i2c_read,
	.write = tpm_tis_i2c_write,
	.release = tpm_tis_i2c_release,
};

static struct miscdevice tis_device = {
	.fops = &tis_ops,
	.name = "tpm0",
	.minor = MISC_DYNAMIC_MINOR,
};

static struct i2c_device_id tpm_tis_i2c_table[] = {
		{ "tpm_i2c_atmel", 0 },
		{ }
};

static struct i2c_driver tpm_tis_i2c_driver = {
	.driver = {
		.name = "tpm_i2c_atmel",
		.owner = THIS_MODULE,
	},
	.probe = tpm_tis_i2c_probe,
	.remove = tpm_tis_i2c_remove, /* __devexit_p() */
	.id_table = tpm_tis_i2c_table,
};

static int __devinit tpm_tis_i2c_probe (struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int rc;

	rc = i2c_smbus_read_byte(client);
	if (rc < 0x00) {
		return -ENODEV;
	}

	return rc;
}

static int __devexit tpm_tis_i2c_remove(struct i2c_client *client)
{
	return 0;
}

static int __devinit tpm_tis_i2c_init (void)
{
	int rc;
	struct i2c_adapter *adapter;
	struct i2c_board_info info;

	rc = i2c_add_driver(&tpm_tis_i2c_driver);
	if (rc) {
		printk (KERN_INFO "tpm_i2c_atmel: driver failue.");
		return rc;
	}

	adapter = i2c_get_adapter(0x03); /* beaglebone specific */
	if (!adapter) {
		printk (KERN_INFO "tpm_i2c_atmel: failed to get adapter.");
		i2c_del_driver(&tpm_tis_i2c_driver);
		rc = -ENODEV;
		return rc;
	}

	info.addr = 0x29; /* in atmel documentation */
	strlcpy(info.type, "tpm_i2c_atmel", I2C_NAME_SIZE);
	tpm_dev.client = i2c_new_device(adapter, &info);

	if (!tpm_dev.client) {
		printk (KERN_INFO "tpm_i2c_atmel: failed to create client.");
		i2c_del_driver(&tpm_tis_i2c_driver);
		/* why no ret change to -ENODEV? */
		return rc;
	}

	/* interesting */
	i2c_put_adapter(adapter);

	rc = misc_register(&tis_device);
	if (rc) {
		printk (KERN_INFO "tpm_i2c_atmel: failed to creat misc device.");
		return rc;
	}

	memset(tpm_dev.buf, 0x00, TPM_BUFSIZE);
	return rc;
}

static void __devexit tpm_tis_i2c_exit (void) {
	misc_deregister(&tis_device);
	i2c_unregister_device(tpm_dev.client);
	i2c_del_driver(&tpm_tis_i2c_driver);
	printk (KERN_INFO "tpm_tis_atmel: removed i2c driver.");
}

module_init(tpm_tis_i2c_init);
module_exit(tpm_tis_i2c_exit);

MODULE_AUTHOR("Teddy Reed <teddy.reed@gmail.com");
MODULE_DESCRIPTION("Driver for ATMEL's AT97SC3204T TPM");
MODULE_LICENSE("GPL");
