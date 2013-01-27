#!/bin/bash

####
# This is an attempt to make a generic patch routine to insert support
# for Atmel's I2C TPM, tested on kernel version 3.2.
####

if [ ! -d "$1" ]; then
	echo "Usage: $0 /path/to/kernel/source"
	exit
fi 

OBJ_LINE="obj-\$(CONFIG_TCG_TIS_I2C_ATMEL) += tpm_i2c_atmel.o"

####
# Move source file to drivers/char/tpm
####

cp ./tpm_i2c_atmel.c $1/drivers/char/tpm/

####
# Add obj line
####

CHECK=`awk '/tpm_i2c_atmel/ { print NR;}' $1/drivers/char/tpm/Makefile`

if [ ! "${CHECK}" = "" ]; then
	echo "Problem: Makefile already contains tpm_i2c_atmel obj line."
else
	echo "${OBJ_LINE}" >> $1/drivers/char/tpm/Makefile
fi

####
# Insert tristate
####

CHECK=`awk '/TCG_TIS_I2C_ATMEL/ { print NR;}' $1/drivers/char/tpm/Kconfig`
if [ ! "${CHECK}" = "" ]; then
	echo "Problem: Kconfig already contains TCG_TIS_I2C_ATMEL tristate."
else
	LAST_LINE=`awk '/endif # TCG_TPM/ { print NR;}' $1/drivers/char/tpm/Kconfig`
	sed "$((LAST_LINE-1))r patches/Kconfig.add" < $1/drivers/char/tpm/Kconfig > Kconfig.mod
	mv Kconfig.mod $1/drivers/char/tpm/Kconfig
fi

echo "Success"
