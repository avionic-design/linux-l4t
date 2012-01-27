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
#include <linux/of_irq.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <linux/i2c/adnp.h>

#define GPIO_DDR(gpio) (0x00 << (gpio)->reg_shift)
#define GPIO_PLR(gpio) (0x01 << (gpio)->reg_shift)
#define GPIO_IER(gpio) (0x02 << (gpio)->reg_shift)
#define GPIO_ISR(gpio) (0x03 << (gpio)->reg_shift)
#define GPIO_PTR(gpio) (0x04 << (gpio)->reg_shift)

struct adnp {
	struct i2c_client *client;
	const char *const *names;
	struct gpio_chip gpio;
	unsigned int reg_shift;

	struct mutex i2c_lock;
	int gpio_base;

#ifdef CONFIG_GPIO_ADNP_IRQ
	struct mutex irq_lock;
	int irq_base;
	int irq;

	u8 *irq_mask;
	u8 *irq_mask_cur;
	u8 *irq_rising;
	u8 *irq_falling;
#endif
};

static int adnp_read(struct adnp *gpio, unsigned offset, uint8_t *value)
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

static int adnp_write(struct adnp *gpio, unsigned offset, uint8_t value)
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

static int adnp_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct adnp *gpio = container_of(chip, struct adnp, gpio);
	unsigned int reg = offset >> gpio->reg_shift;
	unsigned int pos = offset & 7;
	u8 value;
	int err;

	mutex_lock(&gpio->i2c_lock);

	err = adnp_read(gpio, GPIO_PLR(gpio) + reg, &value);
	if (err < 0)
		goto out;

	err = (value & BIT(pos)) ? 1 : 0;

out:
	mutex_unlock(&gpio->i2c_lock);
	return err;
}

static int __adnp_gpio_set(struct adnp *gpio, unsigned offset, int value)
{
	unsigned int reg = offset >> gpio->reg_shift;
	unsigned int pos = offset & 7;
	int err;
	u8 val;

	err = adnp_read(gpio, GPIO_PLR(gpio) + reg, &val);
	if (err < 0)
		return err;

	if (value)
		val |= BIT(pos);
	else
		val &= ~BIT(pos);

	return adnp_write(gpio, GPIO_PLR(gpio) + reg, val);
}

static void adnp_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct adnp *gpio = container_of(chip, struct adnp, gpio);
	int err;

	mutex_lock(&gpio->i2c_lock);

	err = __adnp_gpio_set(gpio, offset, value);
	if (err < 0)
		dev_err(chip->dev, "failed to set pin level: %d\n", err);

	mutex_unlock(&gpio->i2c_lock);
}

static int adnp_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct adnp *gpio = container_of(chip, struct adnp, gpio);
	unsigned int reg = offset >> gpio->reg_shift;
	unsigned int pos = offset & 7;
	u8 value;
	int err;

	mutex_lock(&gpio->i2c_lock);

	err = adnp_read(gpio, GPIO_DDR(gpio) + reg, &value);
	if (err < 0)
		goto out;

	value &= ~BIT(pos);

	err = adnp_write(gpio, GPIO_DDR(gpio) + reg, value);
	if (err < 0)
		goto out;

	err = adnp_read(gpio, GPIO_DDR(gpio) + reg, &value);
	if (err < 0)
		goto out;

	if (err & BIT(pos))
		err = -EACCES;

	err = 0;

out:
	mutex_unlock(&gpio->i2c_lock);
	return err;
}

static int adnp_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
		int value)
{
	struct adnp *gpio = container_of(chip, struct adnp, gpio);
	unsigned int reg = offset >> gpio->reg_shift;
	unsigned int pos = offset & 7;
	int err;
	u8 val;

	mutex_lock(&gpio->i2c_lock);

	err = adnp_read(gpio, GPIO_DDR(gpio) + reg, &val);
	if (err < 0)
		goto out;

	val |= BIT(pos);

	err = adnp_write(gpio, GPIO_DDR(gpio) + reg, val);
	if (err < 0)
		goto out;

	err = adnp_read(gpio, GPIO_DDR(gpio) + reg, &val);
	if (err < 0)
		goto out;

	if (!(val & BIT(pos))) {
		err = -EPERM;
		goto out;
	}

	__adnp_gpio_set(gpio, offset, value);
	err = 0;

out:
	mutex_unlock(&gpio->i2c_lock);
	return err;
}

#ifdef CONFIG_DEBUG_FS
static void adnp_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	struct adnp *gpio = container_of(chip, struct adnp, gpio);
	unsigned int regs;
	unsigned int i, j;
	u8 *base;
	u8 *ddr;
	u8 *plr;
	u8 *ier;
	u8 *isr;
	u8 *ptr;
	int err;

	regs = 1 << gpio->reg_shift;

	base = kzalloc(regs * 5, GFP_KERNEL);
	if (!base)
		return;

	ddr = base + (regs * 0);
	plr = base + (regs * 1);
	ier = base + (regs * 2);
	isr = base + (regs * 3);
	ptr = base + (regs * 4);

	for (i = 0; i < regs; i++) {
		err = adnp_read(gpio, GPIO_DDR(gpio) + i, &ddr[i]);
		if (err < 0)
			goto out;

		err = adnp_read(gpio, GPIO_PLR(gpio) + i, &plr[i]);
		if (err < 0)
			goto out;

		err = adnp_read(gpio, GPIO_IER(gpio) + i, &ier[i]);
		if (err < 0)
			goto out;

		err = adnp_read(gpio, GPIO_ISR(gpio) + i, &isr[i]);
		if (err < 0)
			goto out;

		err = adnp_read(gpio, GPIO_PTR(gpio) + i, &ptr[i]);
		if (err < 0)
			goto out;
	}

	for (i = 0; i < regs; i++) {
		for (j = 0; j < 8; j++) {
			unsigned int bit = (i << gpio->reg_shift) + j;
			const char *direction = "input ";
			const char *level = "low     ";
			const char *interrupt = "disabled";
			const char *pending = "";

			if (ddr[i] & BIT(j))
				direction = "output";

			if (ptr[i] & BIT(j))
				level = "tristate";
			else if (plr[i] & BIT(j))
				level = "high    ";

			if (ier[i] & BIT(j))
				interrupt = "enabled ";

			if (isr[i] & BIT(j))
				pending = "pending";

			seq_printf(s, "%2u: %s %s IRQ %s %s\n", bit,
					direction, level, interrupt, pending);
		}
	}

out:
	kfree(base);
}
#else
#define adnp_gpio_dbg_show NULL
#endif

static int adnp_gpio_setup(struct adnp *gpio, unsigned int nr_gpios)
{
	struct gpio_chip *chip = &gpio->gpio;

	gpio->reg_shift = get_count_order(nr_gpios) - 3;

	chip->direction_input = adnp_gpio_direction_input;
	chip->direction_output = adnp_gpio_direction_output;
	chip->get = adnp_gpio_get;
	chip->set = adnp_gpio_set;
	chip->dbg_show = adnp_gpio_dbg_show;
	chip->can_sleep = 1;

	chip->base = gpio->gpio_base;
	chip->ngpio = nr_gpios;
	chip->label = gpio->client->name;
	chip->dev = &gpio->client->dev;
	chip->owner = THIS_MODULE;
	chip->names = gpio->names;

#ifdef CONFIG_OF_GPIO
	chip->of_node = chip->dev->of_node;
#endif

	return 0;
}

#ifdef CONFIG_GPIO_ADNP_IRQ
static unsigned long __adnp_irq_pending(struct adnp *gpio, unsigned int reg)
{
	u8 status = 0;
	int err;

	err = adnp_read(gpio, GPIO_ISR(gpio) + reg, &status);
	if (err < 0)
		status = 0;

	return status;
}

static irqreturn_t adnp_irq(int irq, void *data)
{
	struct adnp *gpio = data;
	unsigned int regs;
	unsigned int reg;

	regs = 1 << gpio->reg_shift;

	/*
	 * TODO: Reading pending and acknowledge should be atomic. Perhaps the
	 *       CPLD implementation needs to change to allow disabling all
	 *       interrupts before reading the ISR.
	 */

	for (reg = 0; reg < regs; reg++) {
		unsigned int base = reg << gpio->reg_shift;
		unsigned long pending;
		u8 level = 0;
		int bit;
		int err;

		mutex_lock(&gpio->i2c_lock);

		pending = __adnp_irq_pending(gpio, reg);
		if (!pending) {
			mutex_unlock(&gpio->i2c_lock);
			continue;
		}

		err = adnp_read(gpio, GPIO_PLR(gpio) + reg, &level);
		if (err < 0) {
			mutex_unlock(&gpio->i2c_lock);
			continue;
		}

		mutex_unlock(&gpio->i2c_lock);

		pending &= (gpio->irq_falling[reg] & ~level) |
			   (gpio->irq_rising[reg] & level);

		for_each_set_bit(bit, &pending, 8)
			handle_nested_irq(gpio->irq_base + base + bit);
	}

	return IRQ_HANDLED;
}

static void adnp_irq_update_mask(struct adnp *gpio)
{
	unsigned int regs = 1 << gpio->reg_shift;
	bool equal = true;
	unsigned int i;

	for (i = 0; i < regs; i++) {
		if (gpio->irq_mask[i] != gpio->irq_mask_cur[i]) {
			equal = false;
			break;
		}
	}

	if (equal)
		return;

	memcpy(gpio->irq_mask, gpio->irq_mask_cur, regs);

	mutex_lock(&gpio->i2c_lock);

	for (i = 0; i < regs; i++)
		adnp_write(gpio, GPIO_IER(gpio) + i, gpio->irq_mask[i]);

	mutex_unlock(&gpio->i2c_lock);
}

static int adnp_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct adnp *gpio = container_of(chip, struct adnp, gpio);
	return gpio->irq_base + offset;
}

static void adnp_irq_mask(struct irq_data *data)
{
	struct adnp *gpio = irq_data_get_irq_chip_data(data);
	unsigned int irq = data->irq - gpio->irq_base;
	unsigned int reg = irq >> gpio->reg_shift;
	unsigned int pos = irq & 7;

	gpio->irq_mask_cur[reg] &= ~BIT(pos);
}

static void adnp_irq_unmask(struct irq_data *data)
{
	struct adnp *gpio = irq_data_get_irq_chip_data(data);
	unsigned int irq = data->irq - gpio->irq_base;
	unsigned int reg = irq >> gpio->reg_shift;
	unsigned int pos = irq & 7;

	gpio->irq_mask_cur[reg] |= BIT(pos);
}

static int adnp_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct adnp *gpio = irq_data_get_irq_chip_data(data);
	unsigned int irq = data->irq - gpio->irq_base;
	unsigned int reg = irq >> gpio->reg_shift;
	unsigned int pos = irq & 7;

	if ((type & IRQ_TYPE_EDGE_BOTH) == 0)
		return -EINVAL;

	if (type & IRQ_TYPE_EDGE_RISING)
		gpio->irq_rising[reg] |= BIT(pos);
	else
		gpio->irq_rising[reg] &= ~BIT(pos);

	if (type & IRQ_TYPE_EDGE_FALLING)
		gpio->irq_falling[reg] |= BIT(pos);
	else
		gpio->irq_falling[reg] &= ~BIT(pos);

	return 0;
}

static void adnp_irq_bus_lock(struct irq_data *data)
{
	struct adnp *gpio = irq_data_get_irq_chip_data(data);
	unsigned int regs = 1 << gpio->reg_shift;

	mutex_lock(&gpio->irq_lock);
	memcpy(gpio->irq_mask_cur, gpio->irq_mask, regs);
}

static void adnp_irq_bus_unlock(struct irq_data *data)
{
	struct adnp *gpio = irq_data_get_irq_chip_data(data);

	adnp_irq_update_mask(gpio);
	mutex_unlock(&gpio->irq_lock);
}

static struct irq_chip adnp_irq_chip = {
	.name = "gpio-adnp",
	.irq_mask = adnp_irq_mask,
	.irq_unmask = adnp_irq_unmask,
	.irq_set_type = adnp_irq_set_type,
	.irq_bus_lock = adnp_irq_bus_lock,
	.irq_bus_sync_unlock = adnp_irq_bus_unlock,
};

static int adnp_irq_setup(struct adnp *gpio)
{
	unsigned int regs = 1 << gpio->reg_shift;
	int pin;
	int err;

	mutex_init(&gpio->irq_lock);

	gpio->irq_mask = devm_kzalloc(gpio->gpio.dev, regs * 4, GFP_KERNEL);
	if (!gpio->irq_mask)
		return -ENOMEM;

	gpio->irq_mask_cur = gpio->irq_mask + (regs * 1);
	gpio->irq_rising = gpio->irq_mask + (regs * 2);
	gpio->irq_falling = gpio->irq_mask + (regs * 3);

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
		irq_set_chip(irq, &adnp_irq_chip);
		irq_set_nested_thread(irq, true);
#ifdef CONFIG_ARM
		set_irq_flags(irq, IRQF_VALID);
#else
		irq_set_noprobe(irq);
#endif
	}

	err = request_threaded_irq(gpio->irq, NULL, adnp_irq,
			IRQF_TRIGGER_RISING | IRQF_ONESHOT,
			dev_name(&gpio->client->dev), gpio);
	if (err != 0) {
		dev_err(gpio->gpio.dev, "can't request IRQ#%d: %d\n",
				gpio->irq, err);
		return err;
	}

	gpio->gpio.to_irq = adnp_gpio_to_irq;
	return 0;
}

static void adnp_irq_teardown(struct adnp *gpio)
{
	irq_free_descs(gpio->irq_base, gpio->gpio.ngpio);
	free_irq(gpio->irq, gpio);
}
#else
static int adnp_irq_setup(struct adnp *gpio)
{
	return 0;
}

static void adnp_irq_teardown(struct adnp *gpio)
{
}
#endif

#ifdef CONFIG_OF
static int adnp_parse_dt(struct device *dev, struct adnp_platform_data *pdata)
{
	struct device_node *node = dev->of_node;
	u32 value;
	int err;

	if (!node)
		return -ENODEV;

	memset(pdata, 0, sizeof(*pdata));

	pdata->irq_base = INT_BOARD_BASE;
	pdata->gpio_base = -1;
	pdata->names = NULL;

	err = of_property_read_u32(node, "nr-gpios", &value);
	if (err < 0)
		return err;

	pdata->nr_gpios = value;

	return 0;
}
#else
static int adnp_parse_dt(struct device *dev, struct adnp_platform_data *pdata)
{
	return -ENODEV;
}
#endif

static __devinit int adnp_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct adnp_platform_data *pdata = client->dev.platform_data;
	struct adnp_platform_data defpdata;
	struct adnp *gpio;
	int err;

	gpio = kzalloc(sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

#ifdef CONFIG_OF
	if ((client->irq <= 0) && client->dev.of_node)
		client->irq = irq_of_parse_and_map(client->dev.of_node, 0);
#endif

	if (!pdata) {
		err = adnp_parse_dt(&client->dev, &defpdata);
		if (err < 0)
			return err;

		pdata = &defpdata;
	}

	gpio->gpio_base = pdata->gpio_base;
	gpio->irq_base = pdata->irq_base;
	gpio->names = pdata->names;

	mutex_init(&gpio->i2c_lock);
	gpio->irq = client->irq;
	gpio->client = client;

	err = adnp_gpio_setup(gpio, pdata->nr_gpios);
	if (err < 0)
		goto gpio_failed;

	err = adnp_irq_setup(gpio);
	if (err < 0)
		goto irq_failed;

	err = gpiochip_add(&gpio->gpio);
	if (err < 0)
		goto irq_failed;

	i2c_set_clientdata(client, gpio);
	return 0;

irq_failed:
	adnp_irq_teardown(gpio);
gpio_failed:
	kfree(gpio);
	return err;
}

static __devexit int adnp_i2c_remove(struct i2c_client *client)
{
	struct adnp *gpio = i2c_get_clientdata(client);
	int err;

	err = gpiochip_remove(&gpio->gpio);
	if (err < 0) {
		dev_err(&client->dev, "%s failed: %d\n", "gpiochip_remove()",
				err);
		return err;
	}

	adnp_irq_teardown(gpio);
	kfree(gpio);
	return 0;
}

static const struct i2c_device_id adnp_i2c_id[] = {
	{ "gpio-adnp" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adnp_i2c_id);

static struct i2c_driver adnp_i2c_driver = {
	.driver = {
		.name = "gpio-adnp",
		.owner = THIS_MODULE,
	},
	.probe = adnp_i2c_probe,
	.remove = __devexit_p(adnp_i2c_remove),
	.id_table = adnp_i2c_id,
};

static int __init adnp_init(void)
{
	int ret = 0;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&adnp_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register %s I2C driver: %d\n",
				adnp_i2c_driver.driver.name, ret);
	}
#endif
	return ret;
}
module_init(adnp_init);

static void __exit adnp_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&adnp_i2c_driver);
#endif
}
module_exit(adnp_exit);

MODULE_DESCRIPTION("Avionic Design N-bit GPIO expander");
MODULE_DESCRIPTION("Thierry Reding <thierry.reding@avionic-design.de>");
MODULE_LICENSE("GPL");
