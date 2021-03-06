ATMEL's AT97SC3204T I2C TPM
==================================================

*NOTE:* Support exists in mainline Linux now. 

## Instructions (Kernel):

The following few steps will add the I2C TPM as a tristate in `drivers/char/tpm`.
You will need to configure your board to include the TPM on the correct I2C bus. 
Atmel's I2C TPM has an address of 0x29. You can add: 
```cpp
{ I2C_BOARD_INFO("tpm_i2c_atmel", 0x29), }
```
to the `i2c_board_info` struct used by the I2C bus.

0. Patch your system board to include `tpm_i2c_atmel` with the I2C bus and address of the TPM. 
1. Insert support for the TPM: `[tpm-i2c-atmel] $ bash ./patch_kernel.sh /kernel/source` 
   This moves the source into `drivers/char/tpm`, and adds support to Kconfig & Makefile.
2. Add your preference for `CONFIG_TCG_TIS_I2C_ATMEL`.
3. Compile your kernel.

## Beaglebone (A5+) (Kernel):

For a BeagleBone, in addition to the above steps, there are two included patches
to (1) enable the I2C-2 bus, and (2) add the I2C TPM at address 0x29 on I2C-2.

0. Follow the above
1. Apply patches: (for Linux 3.7, only patch `bb-ra5-add-tpm-i2c-atmel-3.7.patch`)
```
[kernel-dir] $ patch -p0 < /tpm-i2c-atmel/patches/bb-ra5-add-i2c1.patch
[kernel-dir] $ patch -p0 < /tpm-i2c-atmel/patches/bb-ra5-add-tpm-i2c-atmel.patch
```

## Beaglebone (A5+) (Module):

If you would like the Atmel I2C TPM driver as a module built outside of the kernel
you can checkout the beaglebone branch and modify `tpm_i2c_atmel.`c to specify the 
I2C bus and address (though should not change).

0. (Optional) Patch your `board-am335xevm.c` using `patches/bb-ra5-add-i2c1.patch`.
   `[kernel-dir] $ patch -p0 < /tpm-i2c-atmel/bb-ra5-add-i2c1.patch`
   This will enable the I2C-2 bus. The BeagleBone SRM identifies I2C-2 for use with
   peripherals. 
3. `git checkout -b beaglebone beaglebone`
4. `KERNEL_DIR=/location CROSS_COMPILER=/location/bin/arm-linux- make`
5. Copy the .ko to the BeagleBone: `insmod ./tpm_i2c_atmel.ko`

## Other:

The TPM will need initialization; TrouSerS's tpm_tools can do this from userspace.

0. `git clone git://trousers.git.sourceforge.net/gitroot/trousers/trousers`
1. `git clone git://trousers.git.sourceforge.net/gitroot/trousers/tpm-tools`
2. Follow the directions for both, tpm-tools depends on TrouSerS.
3. `./tpm-tools/src/tpm_mgmt/tpm_startup`

*Note:* newer versions of the Linux Kernel support TPM initialization and self test.
  We expect U-boot to have issued a startup and self test, similar to platforms 
  implementing TPM in BIOS.

