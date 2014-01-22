#ifndef _LINUX_I2C_ADNP_H
#define _LINUX_I2C_ADNP_H 1

struct adnp_platform_data {
	unsigned gpio_base;
	unsigned nr_gpios;
	int irq_base;
	const char *const *names;
	int (*machxo_check)(u32 devid, u32 traceid,
			u32 sram_usercode, u32 cfg_usercode);
};

#endif /* _LINUX_I2C_ADNP_H */
