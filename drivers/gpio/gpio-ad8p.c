/*
 * Copyright (C) 2011 Avionic Design GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <linux/i2c/ad8p.h>

#define GPIO_DDR 0x00
#define GPIO_PLR 0x01
#define GPIO_IER 0x02
#define GPIO_ISR 0x03
#define GPIO_PTR 0x04

struct ad8p {
	struct i2c_client *client;
	const char *const *names;
	struct gpio_chip gpio;

	struct mutex i2c_lock;
	int gpio_base;

#ifdef CONFIG_GPIO_AD8P_IRQ
	struct mutex irq_lock;
	int irq_base;
	int irq;

	u8 irq_mask;
	u8 irq_mask_cur;
#endif
};

static int ad8p_read(struct ad8p *gpio, unsigned offset, uint8_t *value)
{
	int err;

	err = i2c_smbus_read_byte_data(gpio->client, offset);
	if (err < 0) {
		dev_err(gpio->gpio.dev, "%s failed: %d\n",
				"i2c_smbus_read_byte_data()", err);
		return err;
	}

	*value = err;
	return 0;
}

static int ad8p_write(struct ad8p *gpio, unsigned offset, uint8_t value)
{
	int err;

	err = i2c_smbus_write_byte_data(gpio->client, offset, value);
	if (err < 0) {
		dev_err(gpio->gpio.dev, "%s failed: %d\n",
				"i2c_smbus_write_byte_data()", err);
		return err;
	}

	return 0;
}

static int ad8p_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct ad8p *gpio = container_of(chip, struct ad8p, gpio);
	u8 value;
	int err;

	mutex_lock(&gpio->i2c_lock);

	err = ad8p_read(gpio, GPIO_PLR, &value);
	if (err < 0)
		goto out;

	err = (value & BIT(offset)) ? 1 : 0;

out:
	mutex_unlock(&gpio->i2c_lock);
	return err;
}

static void ad8p_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct ad8p *gpio = container_of(chip, struct ad8p, gpio);
	int err;
	u8 val;

	mutex_lock(&gpio->i2c_lock);

	err = ad8p_read(gpio, GPIO_PLR, &val);
	if (err < 0)
		goto out;

	if (value)
		val |= BIT(offset);
	else
		val &= ~BIT(offset);

	ad8p_write(gpio, GPIO_PLR, val);

out:
	mutex_unlock(&gpio->i2c_lock);
}

static int ad8p_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct ad8p *gpio = container_of(chip, struct ad8p, gpio);
	u8 value;
	int err;

	mutex_lock(&gpio->i2c_lock);

	err = ad8p_read(gpio, GPIO_DDR, &value);
	if (err < 0)
		goto out;

	value &= ~BIT(offset);

	err = ad8p_write(gpio, GPIO_DDR, value);
	if (err < 0)
		goto out;

	err = ad8p_read(gpio, GPIO_DDR, &value);
	if (err < 0)
		goto out;

	if (err & BIT(offset))
		err = -EACCES;

	err = 0;

out:
	mutex_unlock(&gpio->i2c_lock);
	return err;
}

static int ad8p_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
		int value)
{
	struct ad8p *gpio = container_of(chip, struct ad8p, gpio);
	int err;
	u8 val;

	mutex_lock(&gpio->i2c_lock);

	err = ad8p_read(gpio, GPIO_DDR, &val);
	if (err < 0)
		goto out;

	val |= BIT(offset);

	err = ad8p_write(gpio, GPIO_DDR, val);
	if (err < 0)
		goto out;

	err = ad8p_read(gpio, GPIO_DDR, &val);
	if (err < 0)
		goto out;

	if (!(val & BIT(offset))) {
		err = -EPERM;
		goto out;
	}

	ad8p_gpio_set(chip, offset, value);
	err = 0;

out:
	mutex_unlock(&gpio->i2c_lock);
	return err;
}

static int ad8p_gpio_setup(struct ad8p *gpio)
{
	struct gpio_chip *chip = &gpio->gpio;

	chip->direction_input = ad8p_gpio_direction_input;
	chip->direction_output = ad8p_gpio_direction_output;
	chip->get = ad8p_gpio_get;
	chip->set = ad8p_gpio_set;
	chip->can_sleep = 1;

	chip->base = gpio->gpio_base;
	chip->ngpio = 8;
	chip->label = gpio->client->name;
	chip->dev = &gpio->client->dev;
	chip->owner = THIS_MODULE;
	chip->names = gpio->names;

	return 0;
}

#ifdef CONFIG_GPIO_AD8P_IRQ
static u8 ad8p_irq_pending(struct ad8p *gpio)
{
	u8 status = 0;
	int err;

	mutex_lock(&gpio->i2c_lock);

	err = ad8p_read(gpio, GPIO_ISR, &status);
	if (err < 0)
		status = 0;

	mutex_unlock(&gpio->i2c_lock);
	return status;
}

static void ad8p_irq_enable(struct ad8p *gpio, unsigned int offset)
{
	u8 value = 0;
	int err;

	mutex_lock(&gpio->i2c_lock);

	err = ad8p_read(gpio, GPIO_IER, &value);
	if (err < 0)
		goto out;

	value |= BIT(offset);

	err = ad8p_write(gpio, GPIO_IER, value);
	if (err < 0)
		goto out;

out:
	mutex_unlock(&gpio->i2c_lock);
}

static void ad8p_irq_disable(struct ad8p *gpio, unsigned int offset)
{
	u8 value = 0;
	int err;

	mutex_lock(&gpio->i2c_lock);

	err = ad8p_read(gpio, GPIO_IER, &value);
	if (err < 0)
		goto out;

	value &= ~BIT(offset);

	err = ad8p_write(gpio, GPIO_IER, value);
	if (err < 0)
		goto out;

out:
	mutex_unlock(&gpio->i2c_lock);
}

static void ad8p_irq_ack(struct ad8p *gpio, unsigned int offset)
{
	mutex_lock(&gpio->i2c_lock);
	ad8p_write(gpio, GPIO_ISR, BIT(offset));
	mutex_unlock(&gpio->i2c_lock);
}

static irqreturn_t ad8p_irq(int irq, void *data)
{
	struct ad8p *gpio = data;
	u8 pending;

	pending = ad8p_irq_pending(gpio);

	while (pending) {
		int bit = __ffs(pending);

		ad8p_irq_disable(gpio, bit);
		ad8p_irq_ack(gpio, bit);

		handle_nested_irq(gpio->irq_base + bit);

		ad8p_irq_enable(gpio, bit);
		pending &= ~BIT(bit);
	}

	return IRQ_HANDLED;
}

static void ad8p_irq_update_mask(struct ad8p *gpio)
{
	if (gpio->irq_mask == gpio->irq_mask_cur)
		return;

	gpio->irq_mask = gpio->irq_mask_cur;

	mutex_lock(&gpio->i2c_lock);
	ad8p_write(gpio, GPIO_IER, gpio->irq_mask);
	mutex_unlock(&gpio->i2c_lock);
}

static int ad8p_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct ad8p *gpio = container_of(chip, struct ad8p, gpio);
	return gpio->irq_base + offset;
}

static void ad8p_irq_mask(struct irq_data *data)
{
	struct ad8p *gpio = irq_data_get_irq_chip_data(data);
	gpio->irq_mask_cur &= ~BIT(data->irq - gpio->irq_base);
}

static void ad8p_irq_unmask(struct irq_data *data)
{
	struct ad8p *gpio = irq_data_get_irq_chip_data(data);
	gpio->irq_mask_cur |= BIT(data->irq - gpio->irq_base);
}

static int ad8p_irq_set_type(struct irq_data *data, unsigned int type)
{
	if (type != IRQ_TYPE_EDGE_BOTH)
		return -EINVAL;

	return 0;
}

static void ad8p_irq_bus_lock(struct irq_data *data)
{
	struct ad8p *gpio = irq_data_get_irq_chip_data(data);

	mutex_lock(&gpio->irq_lock);
	gpio->irq_mask_cur = gpio->irq_mask;
}

static void ad8p_irq_bus_unlock(struct irq_data *data)
{
	struct ad8p *gpio = irq_data_get_irq_chip_data(data);

	ad8p_irq_update_mask(gpio);
	mutex_unlock(&gpio->irq_lock);
}

static struct irq_chip ad8p_irq_chip = {
	.name = "gpio-ad8p",
	.irq_mask = ad8p_irq_mask,
	.irq_unmask = ad8p_irq_unmask,
	.irq_set_type = ad8p_irq_set_type,
	.irq_bus_lock = ad8p_irq_bus_lock,
	.irq_bus_sync_unlock = ad8p_irq_bus_unlock,
};

static int ad8p_irq_setup(struct ad8p *gpio)
{
	int pin;
	int err;

	mutex_init(&gpio->irq_lock);

	err = irq_alloc_descs(-1, gpio->irq_base, gpio->gpio.ngpio, -1);
	if (err < 0) {
		dev_err(gpio->gpio.dev, "%s failed: %d\n",
				"irq_alloc_descs()", err);
		return err;
	}

	gpio->irq_base = err;

	for (pin = 0; pin < gpio->gpio.ngpio; pin++) {
		int irq = gpio->irq_base + pin;

		irq_clear_status_flags(irq, IRQ_NOREQUEST);
		irq_set_chip_data(irq, gpio);
		irq_set_chip(irq, &ad8p_irq_chip);
		irq_set_nested_thread(irq, true);
#ifdef CONFIG_ARM
		set_irq_flags(irq, IRQF_VALID);
#else
		irq_set_noprobe(irq);
#endif
	}

	err = request_threaded_irq(gpio->irq, NULL, ad8p_irq,
			IRQF_TRIGGER_RISING | IRQF_ONESHOT,
			dev_name(&gpio->client->dev), gpio);
	if (err != 0) {
		dev_err(gpio->gpio.dev, "%s failed: %d\n",
				"request_threaded_irq()", err);
		return err;
	}

	gpio->gpio.to_irq = ad8p_gpio_to_irq;
	return 0;
}

static void ad8p_irq_teardown(struct ad8p *gpio)
{
	irq_free_descs(gpio->irq_base, gpio->gpio.ngpio);
	free_irq(gpio->irq, gpio);
}
#else
static int ad8p_irq_setup(struct ad8p *gpio)
{
	return 0;
}

static void ad8p_irq_teardown(struct ad8p *gpio)
{
}
#endif

static __devinit int ad8p_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ad8p_platform_data *pdata;
	struct ad8p *gpio;
	int err;

	gpio = kzalloc(sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	pdata = client->dev.platform_data;
	if (pdata) {
		gpio->gpio_base = pdata->gpio_base;
		gpio->irq_base = pdata->irq_base;
		gpio->names = pdata->names;
	}

	mutex_init(&gpio->i2c_lock);
	gpio->irq = client->irq;
	gpio->client = client;

	err = ad8p_gpio_setup(gpio);
	if (err < 0)
		goto gpio_failed;

	err = ad8p_irq_setup(gpio);
	if (err < 0)
		goto irq_failed;

	err = gpiochip_add(&gpio->gpio);
	if (err < 0)
		goto irq_failed;

	i2c_set_clientdata(client, gpio);
	return 0;

irq_failed:
	ad8p_irq_teardown(gpio);
gpio_failed:
	kfree(gpio);
	return err;
}

static __devexit int ad8p_i2c_remove(struct i2c_client *client)
{
	struct ad8p *gpio = i2c_get_clientdata(client);
	int err;

	err = gpiochip_remove(&gpio->gpio);
	if (err < 0) {
		dev_err(&client->dev, "%s failed: %d\n", "gpiochip_remove()",
				err);
		return err;
	}

	ad8p_irq_teardown(gpio);
	kfree(gpio);
	return 0;
}

static const struct i2c_device_id ad8p_i2c_id[] = {
	{ "gpio-ad8p", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ad8p_i2c_id);

static struct i2c_driver ad8p_i2c_driver = {
	.driver = {
		.name = "gpio-ad8p",
		.owner = THIS_MODULE,
	},
	.probe = ad8p_i2c_probe,
	.remove = __devexit_p(ad8p_i2c_remove),
	.id_table = ad8p_i2c_id,
};

static int __init ad8p_init(void)
{
	int ret = 0;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&ad8p_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register %s I2C driver: %d\n",
				ad8p_i2c_driver.driver.name, ret);
	}
#endif
	return ret;
}
module_init(ad8p_init);

static void __exit ad8p_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&ad8p_i2c_driver);
#endif
}
module_exit(ad8p_exit);

MODULE_DESCRIPTION("Avionic Design 8-bit GPIO expander");
MODULE_DESCRIPTION("Thierry Reding <thierry.reding@avionic-design.de>");
MODULE_LICENSE("GPL");
