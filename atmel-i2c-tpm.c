/*
 * atmel-i2c-tpm.c
 *
 *  Created on: Sep 3, 2012
 *      Author: teddy
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include "i2c-tpm.h"

/*
static struct i2c_driver atmel_tpm_driver = {
		.owner 		= THIS_MODULE,
		.name		= "atmel_tpm",
		//.flags 		= I2C_DF_NOTIFY,
		//.attach_adapter = atmel_tpm_attach_adapter,
		//.detatch_client	= atmel_tpm_detach_client,
};
*/

int atpm_read_value (struct i2c_client *client, unsigned int /*u8*/ reg);
int atpm_write_value (struct i2c_client *client, unsigned int reg, unsigned long /*u16*/ value);
static int atpm_probe (struct i2c_client *client, const struct i2c_device_id *id);
static int atpm_remove (struct i2c_client *client);

/* working from: mjmwired.net/kernel/Docuymentation/i2c/writing-clients */
static struct i2c_device_id atpm_id[] = {
		{ "atpm", 24 }, // shows up 24 on BB and AVR
		{ }
};

MODULE_DEVICE_TABLE(i2c, atpm_id);

static struct i2c_driver atpm_driver = {
		.driver = {
				.name = "atpm",
				.owner = THIS_MODULE,
		},
		.probe = atpm_probe,
		.remove = atpm_remove, /* __devexit_p() */
		.id_table = atpm_id,
};

/* examples */
int atpm_read_value (struct i2c_client *client, unsigned int /*u8*/ reg) {
	return 0;
}

int atpm_write_value (struct i2c_client *client, unsigned int reg, unsigned long /*u16*/ value) {
	return 0;
}

static int atpm_probe (struct i2c_client *client, const struct i2c_device_id *id) {
	printk(KERN_INFO "probed atpm\n");
	return 0;
}

static int atpm_remove (struct i2c_client *client) {
	return 0;
}

static int __init atpm_init (void)
{
	i2c_add_driver(&atpm_driver);
	printk(KERN_INFO "init_module() called\n");
	return 0;
}

static void __exit atpm_cleanup (void)
{
	i2c_del_driver(&atpm_driver);
	printk(KERN_INFO "cleanup_module() called\n");
}

MODULE_AUTHOR("Teddy Reed <teddy.reed@gmail.com");
MODULE_DESCRIPTION("Driver for ATMEL's AT97SC3204T TPM.");
MODULE_LICENSE("GPL");

module_init(atpm_init);
module_exit(atpm_cleanup);
