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

static int __devinit tpm_tis_i2c_probe (struct i2c_client *client, const struct i2c_device_id *id);
static int __devinit tpm_tis_i2c_init (void);
static void __devexit tpm_tis_i2c_exit (void);

static int tpm_i2c_write(u8 *buffer, size_t len)
{
	int rc;

	struct i2c_msg msg1 = { tpm_dev.client->addr, 0, len, tpm_dev.buf };

	rc = -EIO;
	if (len > TPM_BUFSIZE) {
		return -EINVAL;
	}

	/** should lock the device
	 * **/
	memcpy(&(tpm_dev.buf[0]), buffer, len);
	rc = i2c_transfer(tpm_dev.client->adapter, &msg1, 1);

	printk(KERN_INFO "tpm_i2c_atmel, write, %i\n", rc);

	/** should unlock device **/
	if (rc <= 0)
		return -EIO;

	return 0;
}

static int tpm_i2c_read(u8 *buffer, size_t len)
{
	int rc;
	u32 trapdoor = 0;
	const u32 trapdoor_limit = 60000; /* 5min with base 5mil seconds */

	struct i2c_msg msg1 = { tpm_dev.client->addr, I2C_M_RD, len, buffer };

	/** should lock the device **/
	printk(KERN_INFO "tpm_i2c_atmel, read, len requested %i\n", len);

	do {
		rc = i2c_transfer(tpm_dev.client->adapter, &msg1, 1);
		if (rc > 0x00)
			break;
		trapdoor++;
		msleep(5);
	} while (trapdoor < trapdoor_limit);

	if (trapdoor >= trapdoor_limit)
		return -EFAULT;

	printk(KERN_INFO "tpm_i2c_atmel, done read, rc is %i, 0x%X\n", rc, buffer);

	/** should unlock device **/
	return rc;
}

/* begin direct from Infineon */
static u8 tpm_tis_i2c_status(struct tpm_chip *chip)
{
	printk(KERN_INFO "tpm_i2c_atmel, status called\n");
	/* NOTE: since I2C read may fail, return 0 in this case --> time-out */
	//u8 buf;
	/*if (iic_tpm_read(TPM_STS(chip->vendor.locality), &buf, 1) < 0)
		return 0;
	else
		return buf;
	*/
	return 1;
}

static void tpm_tis_i2c_ready(struct tpm_chip *chip)
{
	printk(KERN_INFO "tpm_i2c_atmel, ready called\n");
	/* this causes the current command to be aborted */
	//u8 buf = TPM_STS_COMMAND_READY;
	//iic_tpm_write_long(TPM_STS(chip->vendor.locality), &buf, 1);
}

static int tpm_tis_i2c_recv(struct tpm_chip *chip, u8 *buf, size_t count)
{
	int rc = 0;
	int expected;

	printk(KERN_INFO "tpm_i2c_atmel, recv, size %i\n", count);

	if (count < TPM_HEADER_SIZE) {
		rc = -EIO;
		return rc;
	}

	rc = tpm_i2c_read(buf, TPM_HEADER_SIZE); /* returns status of read */

	//expected = be32_to_cpu(*(__be32 *)(buf + 2));
	expected = buf[4];
	expected = expected << 8;
	expected += buf[5];

	printk(KERN_INFO "tpm_i2c_atmel, recv, expected %i\n", expected);
	if (expected <= TPM_HEADER_SIZE) {
		/* finished here */
		return rc;
	}

	printk(KERN_INFO "tpm_i2c_atmel, need to read %i more\n", expected - TPM_HEADER_SIZE);

	rc = tpm_i2c_read(&buf[TPM_HEADER_SIZE], expected - TPM_HEADER_SIZE);
	/** signal ready optional? **/

	return rc;
}

static int tpm_tis_i2c_send(struct tpm_chip *chip, u8 *buf, size_t len)
{
	int rc = 0;

	printk(KERN_INFO "tpm_i2c_atmel, send, 0x%X, size %i\n", buf, len);
	/** simple write **/
	rc = tpm_i2c_write(buf, len);
	printk(KERN_INFO "tpm_i2c_atmel, send, end\n");
	return rc;
}


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
	//.req_complete_mask = TPM_STS_DATA_AVAIL | TPM_STS_VALID,
	//.req_complete_val = TPM_STS_DATA_AVAIL | TPM_STS_VALID,
	//.req_canceled = TPM_STS_COMMAND_READY,
	.attr_group = &tis_attr_grp,
	.miscdev.fops = &tis_ops,
};
/* end direct from Infineon */


MODULE_DEVICE_TABLE(i2c, tpm_tis_i2c_table);
static SIMPLE_DEV_PM_OPS(tpm_tis_i2c_ops, tpm_pm_suspend, tpm_pm_resume);

/* Board info modification
 * i2c2 on Beaglebone for 3.2.0 kernel is bus: i2c-3
static struct i2c_board_info __initdata beagle_i2c_devices[] = {
		{ I2C_BOARD_INFO("tpm_i2c_atmel", 0x29), }
};
*/

static int __devinit tpm_tis_i2c_probe (struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int rc;
	struct tpm_chip *chip;

	printk(KERN_INFO "probed tpm_i2c_atmel\n");

	rc = 0;
	if (tpm_dev.client != NULL) {
		return -EBUSY; /* Only support one client */
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "failed algorithm check on i2c bus.");
		return -ENODEV;
	}

	tpm_dev.client = client;

	chip = tpm_register_hardware(&client->dev, &tpm_tis_i2c);
	if (!chip) {
		rc = -ENODEV;
		client->driver = NULL;
		tpm_dev.client = NULL;
		return rc;
	}

	tpm_dev.chip = chip;

	printk(KERN_INFO "tpm_i2c_atmel probe finished\n");

	return rc;
}

static int __devexit tpm_tis_i2c_remove(struct i2c_client *client)
{
	struct tpm_chip *chip = tpm_dev.chip;
	/** no locality found in AVR code **/
	//release_locality(chip, chip->vendor.locality, 1);

	/* close file handles */
	//tpm_dev_vendor_release(chip);

	/* remove hardware */
	tpm_remove_hardware(chip->dev);

	/* reset these pointers, otherwise we oops */
	chip->dev->release = NULL;
	chip->release = NULL;
	tpm_dev.client = NULL;
	dev_set_drvdata(chip->dev, chip);

	return 0;
}

static int __devinit tpm_tis_i2c_init (void)
{
	return i2c_add_driver(&tpm_tis_i2c_driver);
}

static void __devexit tpm_tis_i2c_exit (void) {
	i2c_del_driver(&tpm_tis_i2c_driver);
}

static struct i2c_device_id tpm_tis_i2c_table[] = {
		{ "tpm_i2c_atmel", 0 },
		{ }
};

static struct i2c_driver tpm_tis_i2c_driver = {
	.driver = {
		.name = "tpm_i2c_atmel",
		.owner = THIS_MODULE,
		.pm = &tpm_tis_i2c_ops,
	},
	.probe = tpm_tis_i2c_probe,
	.remove = tpm_tis_i2c_remove, /* __devexit_p() */
	.id_table = tpm_tis_i2c_table,
};

module_init(tpm_tis_i2c_init);
module_exit(tpm_tis_i2c_exit);

MODULE_AUTHOR("Teddy Reed <teddy.reed@gmail.com");
MODULE_DESCRIPTION("Driver for ATMEL's AT97SC3204T TPM");
MODULE_LICENSE("GPL");
