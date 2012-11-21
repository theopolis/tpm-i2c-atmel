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

/* Copied values from Infineon, not checked against AVR example code. */
/* max. buffer size supported by our (Infineon) TPM */
#define TPM_BUFSIZE 1260

/* max. number of iterations after I2C NAK */
#define MAX_COUNT 3

#define SLEEP_DURATION_LOW 55
#define SLEEP_DURATION_HI 65

/* max. number of iterations after I2C NAK for 'long' commands
 * we need this especially for sending TPM_READY, since the cleanup after the
 * transtion to the ready state may take some time, but it is unpredictable
 * how long it will take.
 */
#define MAX_COUNT_LONG 50

#define SLEEP_DURATION_LONG_LOW 200
#define SLEEP_DURATION_LONG_HI 220

/* After sending TPM_READY to 'reset' the TPM we have to sleep even longer */
#define SLEEP_DURATION_RESET_LOW 2400
#define SLEEP_DURATION_RESET_HI 2600

/* we want to use usleep_range instead of msleep for the 5ms TPM_TIMEOUT */
#define TPM_TIMEOUT_US_LOW 		(TPM_TIMEOUT * 1000)
#define TPM_TIMEOUT_US_HI  		(TPM_TIMEOUT_US_LOW + 2000)
/* End copied */

/* Copied, defined in TIS */
enum tis_access {
	TPM_ACCESS_VALID 			= 0x80,
	TPM_ACCESS_ACTIVE_LOCALITY 	= 0x20,
	TPM_ACCESS_REQUEST_PENDING 	= 0x04,
	TPM_ACCESS_REQUEST_USE 		= 0x02,
};

enum tis_status {
	TPM_STS_VALID 				= 0x80,
	TPM_STS_COMMAND_READY 		= 0x40,
	TPM_STS_GO 					= 0x20,
	TPM_STS_DATA_AVAIL 			= 0x10,
	TPM_STS_DATA_EXPECT 		= 0x08,
};

enum tis_defaults {
	TIS_SHORT_TIMEOUT 			= 750,	/* ms */
	TIS_LONG_TIMEOUT 			= 2000,	/* 2 sec */
};

#define	TPM_ACCESS(l)			(0x0000 | ((l) << 4))
#define	TPM_STS(l)				(0x0001 | ((l) << 4))
#define	TPM_DATA_FIFO(l)		(0x0005 | ((l) << 4))
#define	TPM_DID_VID(l)			(0x0006 | ((l) << 4))
/* End copied */


struct tpm_i2c_atmel_dev {
	struct i2c_client *client;
	u8 buf[TPM_BUFSIZE + sizeof(u8)]; /* max buf size + addr */
	struct tpm_chip *chip;
};

struct tpm_i2c_atmel_dev tpm_dev;
static struct i2c_driver tpm_tis_i2c_driver;

static int iic_tpm_read(u8 addr, u8 *buffer, size_t len);
static int iic_tpm_write_generic(u8 addr, u8 *buffer, size_t len, unsigned int sleep_low, unsigned int sleep_hi, u8 max_count);
static int iic_tpm_write(u8 addr, u8 *buffer, size_t len);
static int iic_tpm_write_long(u8 addr, u8 *buffer, size_t len);

static int check_locality(struct tpm_chip *chip, int loc);
static void release_locality(struct tpm_chip *chip, int loc, int force);
static int request_locality(struct tpm_chip *chip, int loc);

int atpm_read_value (struct i2c_client *client, unsigned int /*u8*/ reg);
int atpm_write_value (struct i2c_client *client, unsigned int reg, unsigned long /*u16*/ value);

static int __devinit tpm_tis_i2c_probe (struct i2c_client *client, const struct i2c_device_id *id);
static int __devinit tpm_tis_i2c_create (struct device *dev);
static int __devexit atpm_remove (struct i2c_client *client);

static int __devinit tpm_tis_i2c_init (void);
static void __devexit tpm_tis_i2c_exit (void);



/*
 * iic_tpm_read() - read from TPM register
 * @addr: register address to read from
 * @buffer: provided by caller
 * @len: number of bytes to read
 *
 * Read len bytes from TPM register and put them into
 * buffer (little-endian format, i.e. first byte is put into buffer[0]).
 *
 * NOTE: TPM is big-endian for multi-byte values. Multi-byte
 * values have to be swapped.
 *
 * NOTE: We can't unfortunately use the combined read/write functions
 * provided by the i2c core as the TPM currently does not support the
 * repeated start condition and due to it's special requirements.
 * The i2c_smbus* functions do not work for this chip.
 *
 * Return -EIO on error, 0 on success.
 */
static int iic_tpm_read(u8 addr, u8 *buffer, size_t len)
{

	struct i2c_msg msg1 = { tpm_dev.client->addr, 0, 1, &addr };
	struct i2c_msg msg2 = { tpm_dev.client->addr, I2C_M_RD, len, buffer };

	int rc;
	int count;

	/* Lock the adapter for the duration of the whole sequence. */
	if (!tpm_dev.client->adapter->algo->master_xfer)
		return -EOPNOTSUPP;
	i2c_lock_adapter(tpm_dev.client->adapter);

	for (count = 0; count < MAX_COUNT; count++) {
		rc = i2c_transfer(tpm_dev.client->adapter, &msg1, 1);
		if (rc > 0)
			break;	/* break here to skip sleep */

		usleep_range(SLEEP_DURATION_LOW, SLEEP_DURATION_HI);
	}

	if (rc <= 0)
		goto out;

	/* After the TPM has successfully received the register address it needs
	 * some time, thus we're sleeping here again, before retrieving the data
	 */
	for (count = 0; count < MAX_COUNT; count++) {
		usleep_range(SLEEP_DURATION_LOW, SLEEP_DURATION_HI);
		rc = i2c_transfer(tpm_dev.client->adapter, &msg2, 1);
		if (rc > 0)
			break;

	}

out:
	i2c_unlock_adapter(tpm_dev.client->adapter);
	if (rc <= 0)
		return -EIO;

	return 0;
}

static int iic_tpm_write_generic(u8 addr, u8 *buffer, size_t len,
				 unsigned int sleep_low,
				 unsigned int sleep_hi, u8 max_count)
{
	int rc = -EIO;
	int count;

	struct i2c_msg msg1 = { tpm_dev.client->addr, 0, len + 1, tpm_dev.buf };

	if (len > TPM_BUFSIZE)
		return -EINVAL;

	if (!tpm_dev.client->adapter->algo->master_xfer)
		return -EOPNOTSUPP;
	i2c_lock_adapter(tpm_dev.client->adapter);

	/* prepend the 'register address' to the buffer */
	tpm_dev.buf[0] = addr;
	memcpy(&(tpm_dev.buf[1]), buffer, len);

	/*
	 * NOTE: We have to use these special mechanisms here and unfortunately
	 * cannot rely on the standard behavior of i2c_transfer.
	 */
	for (count = 0; count < max_count; count++) {
		rc = i2c_transfer(tpm_dev.client->adapter, &msg1, 1);
		if (rc > 0)
			break;

		usleep_range(sleep_low, sleep_hi);
	}

	i2c_unlock_adapter(tpm_dev.client->adapter);
	if (rc <= 0)
		return -EIO;

	return 0;
}

/*
 * iic_tpm_write() - write to TPM register
 * @addr: register address to write to
 * @buffer: containing data to be written
 * @len: number of bytes to write
 *
 * Write len bytes from provided buffer to TPM register (little
 * endian format, i.e. buffer[0] is written as first byte).
 *
 * NOTE: TPM is big-endian for multi-byte values. Multi-byte
 * values have to be swapped.
 *
 * NOTE: use this function instead of the iic_tpm_write_generic function.
 *
 * Return -EIO on error, 0 on success
 */
static int iic_tpm_write(u8 addr, u8 *buffer, size_t len)
{
	return iic_tpm_write_generic(addr, buffer, len, SLEEP_DURATION_LOW,
				     SLEEP_DURATION_HI, MAX_COUNT);
}

/*
 * This function is needed especially for the cleanup situation after
 * sending TPM_READY
 * */
static int iic_tpm_write_long(u8 addr, u8 *buffer, size_t len)
{
	return iic_tpm_write_generic(addr, buffer, len, SLEEP_DURATION_LONG_LOW,
				     SLEEP_DURATION_LONG_HI, MAX_COUNT_LONG);
}

static int check_locality(struct tpm_chip *chip, int loc)
{
	u8 buf;
	int rc;

	rc = iic_tpm_read(TPM_ACCESS(loc), &buf, 1);
	if (rc < 0)
		return rc;

	if ((buf & (TPM_ACCESS_ACTIVE_LOCALITY | TPM_ACCESS_VALID)) ==
	    (TPM_ACCESS_ACTIVE_LOCALITY | TPM_ACCESS_VALID)) {
		chip->vendor.locality = loc;
		return loc;
	}

	return -EIO;
}

/* implementation similar to tpm_tis */
static void release_locality(struct tpm_chip *chip, int loc, int force)
{
	u8 buf;
	if (iic_tpm_read(TPM_ACCESS(loc), &buf, 1) < 0)
		return;

	if (force || (buf & (TPM_ACCESS_REQUEST_PENDING | TPM_ACCESS_VALID)) ==
	    (TPM_ACCESS_REQUEST_PENDING | TPM_ACCESS_VALID)) {
		buf = TPM_ACCESS_ACTIVE_LOCALITY;
		iic_tpm_write(TPM_ACCESS(loc), &buf, 1);
	}
}

static int request_locality(struct tpm_chip *chip, int loc)
{
	unsigned long stop;
	u8 buf = TPM_ACCESS_REQUEST_USE;

	if (check_locality(chip, loc) >= 0)
		return loc;

	iic_tpm_write(TPM_ACCESS(loc), &buf, 1);

	/* wait for burstcount */
	stop = jiffies + chip->vendor.timeout_a;
	do {
		if (check_locality(chip, loc) >= 0)
			return loc;
		usleep_range(TPM_TIMEOUT_US_LOW, TPM_TIMEOUT_US_HI);
	} while (time_before(jiffies, stop));

	return -ETIME;
}


/* examples */
int atpm_read_value (struct i2c_client *client, unsigned int /*u8*/ reg) {
	return 0;
}

int atpm_write_value (struct i2c_client *client, unsigned int reg, unsigned long /*u16*/ value) {
	return 0;
}

static int __devexit atpm_remove (struct i2c_client *client) {
	return 0;
}

/* begin direct from Infineon */
static u8 tpm_tis_i2c_status(struct tpm_chip *chip)
{
	/* NOTE: since I2C read may fail, return 0 in this case --> time-out */
	u8 buf;
	/*if (iic_tpm_read(TPM_STS(chip->vendor.locality), &buf, 1) < 0)
		return 0;
	else
		return buf;
	*/
	return 0;
}

static void tpm_tis_i2c_ready(struct tpm_chip *chip)
{
	/* this causes the current command to be aborted */
	u8 buf = TPM_STS_COMMAND_READY;
	//iic_tpm_write_long(TPM_STS(chip->vendor.locality), &buf, 1);
}

static int tpm_tis_i2c_recv(struct tpm_chip *chip, u8 *buf, size_t count) {
	return 0;
}

static int tpm_tis_i2c_send(struct tpm_chip *chip, u8 *buf, size_t len) {
	return 0;
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
	.status = tpm_tis_i2c_status,		/* Defined above */
	.recv = tpm_tis_i2c_recv,			/* Defined above */
	.send = tpm_tis_i2c_send,			/* Defined above */
	.cancel = tpm_tis_i2c_ready,		/* Defined above */
	.req_complete_mask = TPM_STS_DATA_AVAIL | TPM_STS_VALID,
	.req_complete_val = TPM_STS_DATA_AVAIL | TPM_STS_VALID,
	.req_canceled = TPM_STS_COMMAND_READY,
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

static int __devinit tpm_tis_i2c_create (struct device *dev)
{
	u32 vendor;
	int rc;
	struct tpm_chip *chip;

	/* for testing */
	u32 buffer;
	u8 addr = TPM_ACCESS(0);
	u8 change[sizeof(u8)*2];
	struct i2c_msg msg1 = { tpm_dev.client->addr, 0, 1, &addr };
	struct i2c_msg msg2 = { tpm_dev.client->addr, I2C_M_RD, 4, (u8 *)&buffer };
	struct i2c_msg msg3 = { tpm_dev.client->addr, 0, 2, (u8 *)&change };


	/* end for testing */

	rc = 0;
	chip = tpm_register_hardware(dev, &tpm_tis_i2c);
	if (!chip) {
		rc = -ENODEV;
		/* No data structures to free */
		goto out_clean;
	}

	/* Disable interrupts */
	chip->vendor.irq = 0;

	/* Default (Infineon) timeouts */
	chip->vendor.timeout_a = msecs_to_jiffies(TIS_SHORT_TIMEOUT);
	chip->vendor.timeout_b = msecs_to_jiffies(TIS_LONG_TIMEOUT);
	chip->vendor.timeout_c = msecs_to_jiffies(TIS_SHORT_TIMEOUT);
	chip->vendor.timeout_d = msecs_to_jiffies(TIS_SHORT_TIMEOUT);

	change[0] = TPM_ACCESS(1);
	change[1] = TPM_ACCESS_REQUEST_USE;

	i2c_transfer(tpm_dev.client->adapter, &msg3, 1);
	usleep_range(SLEEP_DURATION_LOW, SLEEP_DURATION_HI);

	printk(KERN_INFO "about to tpm_read\n");
	rc = i2c_transfer(tpm_dev.client->adapter, &msg1, 1);
	usleep_range(SLEEP_DURATION_LOW, SLEEP_DURATION_HI);

	if (rc < 0) {
		printk (KERN_INFO "wrong: %i\n", rc);
		return rc;
	}

	printk(KERN_INFO "about to tpm_read\n");
	rc = i2c_transfer(tpm_dev.client->adapter, &msg2, 1);

	if (rc < 0) {
		printk (KERN_INFO "wrong: %i\n", rc);
		return rc;
	}

	printk(KERN_INFO "tpm_read (REG) returned: %i\n", buffer);
	buffer = be32_to_cpu((__be32) buffer);
	printk(KERN_INFO "id: 0x%X, dev-id: 0x%X\n", buffer, buffer >> 16);
	printk(KERN_INFO "0x%X - 0x%X\n", (buffer & (TPM_ACCESS_ACTIVE_LOCALITY | TPM_ACCESS_VALID)),
			(TPM_ACCESS_ACTIVE_LOCALITY | TPM_ACCESS_VALID));



	return rc;

	if (request_locality(chip, 0) == 0) {
		rc = -ENODEV;
		goto out_vendor;
	}

	if (iic_tpm_read(TPM_DID_VID(0), (u8 *)&vendor, 4) < 0) {
		rc = -EIO;
		goto out_release;
	}

	vendor = be32_to_cpu((__be32) vendor);

	/* Delete me */
	printk(KERN_INFO "tpm_i2c_atmel: dev 0x%X\n", vendor >> 16);
	return 0;

	if (1) { //(vendor != TPM_TIS_I2C_DID_VID) {
		rc = -ENODEV;
		goto out_release;
	}

	dev_info(dev, "1.2 TPM (device-id 0x%X)\n", vendor >> 16);

	INIT_LIST_HEAD(&chip->vendor.list);
	tpm_dev.chip = chip;

	tpm_get_timeouts(chip);
	tpm_do_selftest(chip);

	goto out_clean;

out_release:
	release_locality(chip, chip->vendor.locality, 1);

out_vendor:
	/* close file handle */
	tpm_dev_vendor_release(chip);
	/* remove hardware and device nodes */
	tpm_remove_hardware(chip->dev);

	/* remove proc pointers to prevent double cleanup */
	chip->dev->release = NULL;
	chip->release = NULL;
	tpm_dev.client = NULL;
	dev_set_drvdata(chip->dev, chip);

out_clean:
	return rc;
}

static int __devinit tpm_tis_i2c_probe (struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int rc;

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

	/* interesting */
	rc = tpm_tis_i2c_create(&client->dev);
	if (rc != 0) {
		client->driver = NULL;
		tpm_dev.client = NULL;
		rc = -ENODEV;
	}
	printk(KERN_INFO "tpm_i2c_atmel probe finished\n");

	return rc;
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
	.remove = atpm_remove, /* __devexit_p() */
	.id_table = tpm_tis_i2c_table,
};

module_init(tpm_tis_i2c_init);
module_exit(tpm_tis_i2c_exit);

MODULE_AUTHOR("Teddy Reed <teddy.reed@gmail.com");
MODULE_DESCRIPTION("Driver for ATMEL's AT97SC3204T TPM");
MODULE_LICENSE("GPL");
