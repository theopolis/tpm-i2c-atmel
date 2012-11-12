/*
 * atmel-i2c-tpm.c
 *
 *  Created on: Sep 3, 2012
 *      Author: teddy
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/i2c.h>
#include "tpm.h"
#include "tpm_i2c.h"

/*
static struct i2c_driver atmel_tpm_driver = {
		.owner 		= THIS_MODULE,
		.name		= "atmel_tpm",
		//.flags 		= I2C_DF_NOTIFY,
		//.attach_adapter = atmel_tpm_attach_adapter,
		//.detatch_client	= atmel_tpm_detach_client,
};
*/

/* max. buffer size supported by our (infineon) TPM */
#define TPM_BUFSIZE 1260

/* copied from infineon, but defined in TIS */
enum tis_access {
	TPM_ACCESS_VALID = 0x80,
	TPM_ACCESS_ACTIVE_LOCALITY = 0x20,
	TPM_ACCESS_REQUEST_PENDING = 0x04,
	TPM_ACCESS_REQUEST_USE = 0x02,
};

enum tis_status {
	TPM_STS_VALID = 0x80,
	TPM_STS_COMMAND_READY = 0x40,
	TPM_STS_GO = 0x20,
	TPM_STS_DATA_AVAIL = 0x10,
	TPM_STS_DATA_EXPECT = 0x08,
};

enum tis_defaults {
	TIS_SHORT_TIMEOUT = 750,	/* ms */
	TIS_LONG_TIMEOUT = 2000,	/* 2 sec */
};
/* end copied */


struct tpm_i2c_atmel_dev {
	struct i2c_client *client;
	u8 buf[TPM_BUFSIZE + sizeof(u8)]; /* max buf size + addr */
	struct tpm_chip *chip;
};

struct tpm_i2c_atmel_dev tpm_dev;
static struct i2c_driver tpm_tis_i2c_driver;

int atpm_read_value (struct i2c_client *client, unsigned int /*u8*/ reg);
int atpm_write_value (struct i2c_client *client, unsigned int reg, unsigned long /*u16*/ value);
static int __init tpm_tis_i2c_probe (struct i2c_client *client, const struct i2c_device_id *id);
static int __devexit atpm_remove (struct i2c_client *client);

/* working from: mjmwired.net/kernel/Docuymentation/i2c/writing-clients */
static struct i2c_device_id tpm_tis_i2c_table[] = {
		{ "tpm_i2c_atmel", 0 }, // shows up 24 on BB and AVR
		{ }
};

/* examples */
int atpm_read_value (struct i2c_client *client, unsigned int /*u8*/ reg) {
	return 0;
}

int atpm_write_value (struct i2c_client *client, unsigned int reg, unsigned long /*u16*/ value) {
	return 0;
}

static int __init tpm_tis_i2c_probe (struct i2c_client *client,
		const struct i2c_device_id *id) {
	int rc;

	printk(KERN_INFO "probed atpm\n");
	if (tpm_dev.client != NULL) {
		return -EBUSY; /* Only support one client */
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "failed algorithm check on i2c bus.");
		return -ENODEV;
	}

	//printk(KERN_INFO "probed atpm\n");
	return 0;
}

static int __devexit atpm_remove (struct i2c_client *client) {
	return 0;
}

/* begin direct from infineon */
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

#define TPM_STS_COMMAND_READY 0x40

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
	.status = tpm_tis_i2c_status,
	.recv = tpm_tis_i2c_recv,
	.send = tpm_tis_i2c_send,
	.cancel = tpm_tis_i2c_ready,
	.req_complete_mask = TPM_STS_DATA_AVAIL | TPM_STS_VALID,
	.req_complete_val = TPM_STS_DATA_AVAIL | TPM_STS_VALID,
	.req_canceled = TPM_STS_COMMAND_READY,
	.attr_group = &tis_attr_grp,
	.miscdev.fops = &tis_ops,
};
/* end direct from infineon */

static int __init tpm_tis_i2c_init (struct device *dev)
{
	u32 vendor;
	int rc;
	struct tpm_chip *chip;

	rc = 0;
	chip = tpm_register_hardware (dev, &tpm_tis_i2c);
	if (!chip) {
		rc = -ENODEV;
		goto out_err;
	}

	//i2c_add_driver(&tpm_tis_i2c_driver);
out_err:
	printk(KERN_INFO "init_module() called\n");
	return rc;
}

static void __devexit atpm_cleanup (void)
{
	i2c_del_driver(&tpm_tis_i2c_driver);
	printk(KERN_INFO "cleanup_module() called\n");
}

/* todo: lookup MODULE_DEVICE_TABLE */
MODULE_DEVICE_TABLE(i2c, tpm_tis_i2c_table);
/* not sure what this does */
static SIMPLE_DEV_PM_OPS(tpm_tis_i2c_ops, tpm_pm_suspend, tpm_pm_resume);

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
module_exit(atpm_cleanup);
//module_i2c_driver(tpm_tis_i2c_driver);
MODULE_AUTHOR("Teddy Reed <teddy.reed@gmail.com");
MODULE_DESCRIPTION("Driver for ATMEL's AT97SC3204T TPM.");
MODULE_LICENSE("GPL");
