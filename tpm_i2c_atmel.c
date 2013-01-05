/*
 * ATMEL I2C TPM AT97SC3204T
 * Beaglebone rev A5 Linux Driver (Kernel 3.2+)
 *
 * Copyright (C) 2012 V Lab Technologies
 *
 * Authors:
 * Teddy Reed <teddy@prosauce.org>
 *
 * Device driver for TCG/TCPA TPM (trusted platform module).
 * Specifications at www.trustedcomputinggroup.org
 *
 * This device driver implements the TPM interface as defined in
 * the TCG TPM Interface Spec version 1.2.
 *
 * It is based on the AVR code in ATMEL's AT90USB128 TPM Development Board,
 * the Linux Infineon TIS 12C TPM driver from Peter Huewe, the original tpm_tis
 * device driver from Leendert van Dorn and Kyleen Hall, and code provided from
 * ATMEL's Application Group, Crypto Products Division and Max R. May.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

/*
 * Optional board info modification (am335-evm)
 * i2c2 on Beaglebone for 3.2.0 kernel is bus: i2c-3
		static struct i2c_board_info __initdata beagle_i2c_devices[] = {
			{ I2C_BOARD_INFO("tpm_i2c_atmel", 0x29), }
		};
 * This module has the i2c bus and device location including during init.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/i2c.h>
#include <asm-generic/errno.h>

#include "tpm.h"

/* second i2c bus on BeagleBone with >=3.2 kernel */
#define I2C_BUS_ID 0x02
/* Atmel-defined I2C bus ID */
#define ATMEL_I2C_TPM_ID 0x29

/** Found in AVR code and in Max's implementation **/
#define TPM_BUFSIZE 1024

struct tpm_i2c_atmel_dev {
	struct i2c_client *client;
	u8 buf[TPM_BUFSIZE];
	struct tpm_chip *chip;
};

struct tpm_i2c_atmel_dev tpm_dev;
static struct i2c_driver tpm_tis_i2c_driver;

static u8 tpm_i2c_read(u8 *buffer, size_t len);

static void tpm_tis_i2c_ready (struct tpm_chip *chip);
static u8 tpm_tis_i2c_status (struct tpm_chip *chip);
static int tpm_tis_i2c_recv (struct tpm_chip *chip, u8 *buf, size_t count);
static int tpm_tis_i2c_send (struct tpm_chip *chip, u8 *buf, size_t count);

static int __devinit tpm_tis_i2c_probe (struct i2c_client *client, const struct i2c_device_id *id);
static int __devexit tpm_tis_i2c_remove (struct i2c_client *client);
static int __devinit tpm_tis_i2c_init (void);
static void __devexit tpm_tis_i2c_exit (void);


static u8 tpm_i2c_read(u8 *buffer, size_t len)
{
	int rc;
	u32 trapdoor = 0;
	const u32 trapdoor_limit = 60000; /* 5min with base 5mil seconds */

	/** Read into buffer, of size len **/
	struct i2c_msg msg1 = { tpm_dev.client->addr, I2C_M_RD, len, buffer };

	/** should lock the device **/
	/** locking is performed by the i2c_transfer function **/

	if (!tpm_dev.client->adapter->algo->master_xfer)
		return -EOPNOTSUPP;

	do {
		rc = i2c_transfer(tpm_dev.client->adapter, &msg1, 1);
		if (rc > 0x00) /* successful read */
			break;
		trapdoor++;
		msleep(5);
	} while (trapdoor < trapdoor_limit);

	/** failed to read **/
	if (trapdoor >= trapdoor_limit)
		return -EFAULT;

	return rc;
}

static int tpm_tis_i2c_recv (struct tpm_chip *chip, u8 *buf, size_t count)
{
	int rc = 0;
	int expected;
#ifdef EBUG
	int i;
#endif

	memset(tpm_dev.buf, 0x00, TPM_BUFSIZE);
	rc = tpm_i2c_read(tpm_dev.buf, TPM_HEADER_SIZE); /* returns status of read */

	expected = tpm_dev.buf[4];
	expected = expected << 8;
	expected += tpm_dev.buf[5]; /* should never be > TPM_BUFSIZE */

	if (expected <= TPM_HEADER_SIZE) {
		/* finished here */
		goto to_user;
	}

	/* Looks like it reads the entire expected, into the base of the buffer (from Max's code).
	 * The AVR development board reads and additional expected - TPM_HEADER_SIZE.
	 */
	rc = tpm_i2c_read(tpm_dev.buf, expected);

to_user:
	memcpy(buf, tpm_dev.buf, expected);

#ifdef EBUG
	printk(KERN_INFO "[TPM]: Read (%d) bytes:\n0: \t", expected);
	for (i = 0; i < expected; ++i) {
		printk("%x ", tpm_dev.buf[i]);
		if ((i+1) % 20 == 0) {
			printk("\n%d:\t ", i);
		}
	}
	printk("\n");
#endif

	return expected;
}

static int tpm_tis_i2c_send (struct tpm_chip *chip, u8 *buf, size_t count)
{
	int rc;
#ifdef EBUG
	int i;
#endif

	/** Write to tpm_dev.buf, size count **/
	struct i2c_msg msg1 = { tpm_dev.client->addr, 0, count, tpm_dev.buf };

	rc = -EIO;
	if (count > TPM_BUFSIZE) {
		return -EINVAL;
	}

	/** should lock the device **/
	/** locking is performed by the i2c_transfer function **/

	memset(tpm_dev.buf, 0x00, TPM_BUFSIZE);
	/* should add sanitization */
	memcpy(tpm_dev.buf, buf, count);

#ifdef EBUG
	printk(KERN_INFO "[TPM]: Send (%d) bytes:\n0: \t", count);
	for (i = 0; i < count; ++i) {
		printk("%x ", tpm_dev.buf[i]);
		if ((i+1) % 20 == 0) {
			printk("\n%d:\t ", i);
		}
	}
	printk("\n");
#endif

	rc = i2c_transfer(tpm_dev.client->adapter, &msg1, 1);


	if (rc <= 0)
		return -EIO;

	return count;
}

static u8 tpm_tis_i2c_status (struct tpm_chip *chip)
{
	return 1; /* not a timeout */
}

static void tpm_tis_i2c_ready (struct tpm_chip *chip)
{
	/* nothing */
}

/* from Infineon driver */
static const struct file_operations tis_ops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.open = tpm_open,
	.read = tpm_read,
	.write = tpm_write,
	.release = tpm_release,
};

static DEVICE_ATTR(pubek, S_IRUGO, tpm_show_pubek, NULL);
static DEVICE_ATTR(pcrs, S_IRUGO, tpm_show_pcrs, NULL);
static DEVICE_ATTR(enabled, S_IRUGO, tpm_show_enabled, NULL);
static DEVICE_ATTR(active, S_IRUGO, tpm_show_active, NULL);
static DEVICE_ATTR(owned, S_IRUGO, tpm_show_owned, NULL);
static DEVICE_ATTR(temp_deactivated, S_IRUGO, tpm_show_temp_deactivated, NULL);
static DEVICE_ATTR(caps, S_IRUGO, tpm_show_caps_1_2, NULL);
static DEVICE_ATTR(cancel, S_IWUSR | S_IWGRP, NULL, tpm_store_cancel);
static DEVICE_ATTR(durations, S_IRUGO, tpm_show_durations, NULL);
static DEVICE_ATTR(timeouts, S_IRUGO, tpm_show_timeouts, NULL);

static struct attribute *tis_attrs[] = {
	&dev_attr_pubek.attr,
	&dev_attr_pcrs.attr,
	&dev_attr_enabled.attr,
	&dev_attr_active.attr,
	&dev_attr_owned.attr,
	&dev_attr_temp_deactivated.attr,
	&dev_attr_caps.attr,
	&dev_attr_cancel.attr,
	&dev_attr_durations.attr,
	&dev_attr_timeouts.attr,
	NULL,
};

static struct attribute_group tis_attr_grp = {
	.attrs = tis_attrs
};

static struct tpm_vendor_specific tpm_tis_i2c = {
	.status = tpm_tis_i2c_status,
	.recv = tpm_tis_i2c_recv,
	.send = tpm_tis_i2c_send,
	.cancel = tpm_tis_i2c_ready,
	/*.req_complete_mask = TPM_STS_DATA_AVAIL | TPM_STS_VALID,
	.req_complete_val = TPM_STS_DATA_AVAIL | TPM_STS_VALID,
	.req_canceled = TPM_STS_COMMAND_READY,*/
	.attr_group = &tis_attr_grp,
	.miscdev.fops = &tis_ops,
};
/* end from Infineon */

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

	/* not a good implementation, will match any i2c device that responds. */
	rc = i2c_smbus_read_byte(client);
	if (rc < 0x00) {
		return -ENODEV;
	}

	return rc;
}

static int __devexit tpm_tis_i2c_remove(struct i2c_client *client)
{
	struct tpm_chip *chip = tpm_dev.chip;

	/* close file handles */
	tpm_dev_vendor_release(chip);
	/* remove hardware */
	tpm_remove_hardware(chip->dev);

	/* reset pointers */
	chip->dev->release = NULL;
	chip->release = NULL;
	tpm_dev.client = NULL;

	return 0;
}

static int __devinit tpm_tis_i2c_init (void)
{
	int rc;
	struct i2c_adapter *adapter;
	struct i2c_board_info info;
	struct tpm_chip *chip;

	if (tpm_dev.client != NULL)
		return -EBUSY; /* only support one TPM */

	rc = i2c_add_driver(&tpm_tis_i2c_driver);
	if (rc) {
		printk (KERN_INFO "tpm_i2c_atmel: driver failure.");
		return rc;
	}

	/* could call probe here */

	adapter = i2c_get_adapter(I2C_BUS_ID); /* BeagleBone specific */
	if (!adapter) {
		printk (KERN_INFO "tpm_i2c_atmel: failed to get adapter.");
		i2c_del_driver(&tpm_tis_i2c_driver);
		return -ENODEV;
	}

	memset(&info, 0, sizeof(info));
	info.addr = ATMEL_I2C_TPM_ID; /* in Atmel documentation */
	strlcpy(info.type, "tpm_i2c_atmel", I2C_NAME_SIZE);

	tpm_dev.client = i2c_new_device(adapter, &info);

	if (!tpm_dev.client) {
		printk (KERN_INFO "tpm_i2c_atmel: failed to create client.");
		i2c_del_driver(&tpm_tis_i2c_driver);
		return -ENODEV;
	}

	/* interesting */
	i2c_put_adapter(adapter);

	tpm_dev.client->driver = &tpm_tis_i2c_driver;
	chip = tpm_register_hardware(&tpm_dev.client->dev, &tpm_tis_i2c);

	if (!chip) {
		i2c_del_driver(&tpm_tis_i2c_driver);
		return -ENODEV;
	}

	printk(KERN_INFO "tpm_i2c_atmel: registered Atmel TPM.");

	/* required, may not be done by u-boot */
	/* issue tpm_startup, tpm_selftest */

	tpm_dev.chip = chip;
	memset(tpm_dev.buf, 0x00, TPM_BUFSIZE);

	return rc;
}

static void __devexit tpm_tis_i2c_exit (void)
{
	if (tpm_dev.client != NULL) {
		i2c_unregister_device(tpm_dev.client);
	}
	i2c_del_driver(&tpm_tis_i2c_driver);
	printk (KERN_INFO "tpm_i2c_atmel: removed i2c driver.");
}

module_init(tpm_tis_i2c_init);
module_exit(tpm_tis_i2c_exit);

MODULE_AUTHOR("Teddy Reed <teddy@prosauce.org>");
MODULE_DESCRIPTION("Driver for ATMEL's AT97SC3204T I2C TPM on Beaglebone rev A5");
MODULE_LICENSE("GPL");
