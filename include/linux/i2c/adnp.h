#ifndef _LINUX_I2C_ADNP_H
#define _LINUX_I2C_ADNP_H 1

struct adnp_platform_data {
	unsigned gpio_base;
	unsigned nr_gpios;
	int irq_base;
	const char *const *names;
};

#endif /* _LINUX_I2C_ADNP_H */
