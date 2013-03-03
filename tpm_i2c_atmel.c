/*
 * ATMEL I2C TPM AT97SC3204T
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
 * It is based on the Linux Infineon TIS 12C TPM driver from Peter Huewe,
 * the original tpm_tis device driver from Leendert van Dorn and Kyleen Hall,
 * and code provided from Atmel's Application Group, Crypto Products Division
 * and Max R. May.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/i2c.h>
#include <asm-generic/errno.h>

#include "tpm.h"

/* Specific to Atmel I2C TPM */
#define TPM_BUFSIZE 1024

/* Not included in older tpm.h */
#ifndef TPM_HEADER_SIZE
#define TPM_HEADER_SIZE 10
#endif

struct tpm_i2c_atmel_dev {
	struct i2c_client *client;
	u8 buf[TPM_BUFSIZE];
	struct tpm_chip *chip;
};

struct tpm_i2c_atmel_dev tpm_dev;
static struct i2c_driver tpm_tis_i2c_driver;

static u8 	tpm_i2c_read(u8 *buffer, u16 len);

static void tpm_tis_i2c_ready (struct tpm_chip *chip);
static u8 	tpm_tis_i2c_status (struct tpm_chip *chip);
static int 	tpm_tis_i2c_recv (struct tpm_chip *chip, u8 *buf, size_t count);
static int 	tpm_tis_i2c_send (struct tpm_chip *chip, u8 *buf, size_t count);

static int tpm_tis_i2c_probe (struct i2c_client *client, const struct i2c_device_id *id);
static int tpm_tis_i2c_remove (struct i2c_client *client);
static int tpm_tis_i2c_init (struct device *dev);

enum tis_defaults {
	TIS_SHORT_TIMEOUT = 750,	/* ms */
	TIS_LONG_TIMEOUT = 2000,	/* 2 sec */
};

static u8 tpm_i2c_read(u8 *buffer, u16 len)
{
	int rc;
	u32 trapdoor = 0;
	const u32 trapdoor_limit = 60000; /* 5min with base 5mil seconds */

	/** Read into buffer, of size len **/
	struct i2c_msg msg1 = {
		.addr = tpm_dev.client->addr,
		.flags = I2C_M_RD,
		.len = len,
		.buf = buffer
	};

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
	u16 expected;

	memset(tpm_dev.buf, 0x00, TPM_BUFSIZE);
	rc = tpm_i2c_read(tpm_dev.buf, TPM_HEADER_SIZE); /* returns status of read */

	expected = tpm_dev.buf[4];
	expected = expected << 8;
	expected += tpm_dev.buf[5]; /* should never be > TPM_BUFSIZE */

	if (expected <= TPM_HEADER_SIZE) {
		/* finished here */
		goto to_user;
	}

	/* Read again, the expected number of bytes */
	rc = tpm_i2c_read(tpm_dev.buf, expected);

to_user:
	memcpy(buf, tpm_dev.buf, expected);

	return expected;
}

static int tpm_tis_i2c_send (struct tpm_chip *chip, u8 *buf, size_t count)
{
	int rc;

	/** Write to tpm_dev.buf, size count **/
	struct i2c_msg msg1 = {
		.addr = tpm_dev.client->addr,
		.flags = 0,
		.len = count,
		.buf = tpm_dev.buf
	};

	rc = -EIO;
	if (count > TPM_BUFSIZE) {
		return -EINVAL;
	}

	/** should lock the device **/
	/** locking is performed by the i2c_transfer function **/

	memset(tpm_dev.buf, 0x00, TPM_BUFSIZE);
	/* should add sanitization */
	memcpy(tpm_dev.buf, buf, count);

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

MODULE_DEVICE_TABLE(i2c, tpm_tis_i2c_table);

static int tpm_tis_i2c_probe (struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int rc;

	if (tpm_dev.client != NULL)
		return -EBUSY; /* only 1 TPM per-system */

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "no algorithms associated to i2c bus\n");
		return -ENODEV;
	}

	/* not a good implementation, will match any i2c device that responds. */
	rc = i2c_smbus_read_byte(client);
	if (rc < 0x00) {
		return -ENODEV;
	}

	client->driver = &tpm_tis_i2c_driver;
	tpm_dev.client = client;
	rc = tpm_tis_i2c_init(&client->dev);
	if (rc != 0) {
		client->driver = NULL;
		tpm_dev.client = NULL;
		rc = -ENODEV;
	}

	return rc;
}

static int tpm_tis_i2c_remove(struct i2c_client *client)
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
	dev_set_drvdata(chip->dev, chip);

	return 0;
}

static int tpm_tis_i2c_init (struct device *dev)
{
	int rc = 0;
	struct tpm_chip *chip;

	chip = tpm_register_hardware(dev, &tpm_tis_i2c);
	if (!chip) {
		return -ENODEV;
	}

	/* Disable interrupts */
	chip->vendor.irq = 0;

	/* Set default timeouts */
	chip->vendor.timeout_a = msecs_to_jiffies(TIS_SHORT_TIMEOUT);
	chip->vendor.timeout_b = msecs_to_jiffies(TIS_LONG_TIMEOUT);
	chip->vendor.timeout_c = msecs_to_jiffies(TIS_SHORT_TIMEOUT);
	chip->vendor.timeout_d = msecs_to_jiffies(TIS_SHORT_TIMEOUT);

	/* The device responds to an unsolicited read with 0x1 0x2 0x3 ... */

	dev_info(dev, "1.2 TPM");

	tpm_dev.chip = chip;
	memset(tpm_dev.buf, 0x00, TPM_BUFSIZE);

	tpm_get_timeouts(chip);
	/* tpm_do_selftest(chip); */

	return rc;
}

static struct i2c_driver tpm_tis_i2c_driver = {
	.driver = {
		.name = "tpm_i2c_atmel",
		.owner = THIS_MODULE,
	},
	.probe = tpm_tis_i2c_probe,
	.remove = tpm_tis_i2c_remove, /* __devexit_p() */
	.id_table = tpm_tis_i2c_table,
};

static int __init tpm_tis_i2c_modinit(void)
{
	int rc = 0;

	rc = i2c_add_driver(&tpm_tis_i2c_driver);
	return rc;
}

static void __exit tpm_tis_i2c_exit(void)
{
	i2c_del_driver(&tpm_tis_i2c_driver);
}

module_init(tpm_tis_i2c_modinit);
module_exit(tpm_tis_i2c_exit);

MODULE_AUTHOR("Teddy Reed <teddy@prosauce.org>");
MODULE_DESCRIPTION("Atmel AT97SC3204T I2C TPM");
MODULE_VERSION("1.1");
MODULE_LICENSE("GPL");
