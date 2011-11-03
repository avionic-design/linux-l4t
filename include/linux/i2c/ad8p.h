#ifndef _LINUX_I2C_AD8P_H
#define _LINUX_I2C_AD8P_H 1

struct ad8p_platform_data {
	unsigned gpio_base;
	int irq_base;
	const char *const *names;
};

#endif /* _LINUX_I2C_AD8P_H */
