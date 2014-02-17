/*
 * Copyright (C) 2013 Avionic Design GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mfd/core.h>
#include <linux/mfd/sx8634.h>

#define MAX_RETRIES 64

struct sx8634 {
	struct i2c_client *client;
	unsigned long spm_dirty;
	u8 *spm_cache;
	int reset_gpio;
	struct blocking_notifier_head irq_notifier;
	struct mutex lock;
};

void sx8634_lock(struct sx8634 *sx)
{
	mutex_lock(&sx->lock);
}
EXPORT_SYMBOL(sx8634_lock);

void sx8634_unlock(struct sx8634 *sx)
{
	mutex_unlock(&sx->lock);
}
EXPORT_SYMBOL(sx8634_unlock);

int sx8634_read_reg(struct sx8634 *sx, u8 reg)
{
	return i2c_smbus_read_byte_data(sx->client, reg);
}
EXPORT_SYMBOL(sx8634_read_reg);

int sx8634_write_reg(struct sx8634 *sx, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(sx->client, reg, val);
}
EXPORT_SYMBOL(sx8634_write_reg);

static int spm_wait(struct i2c_client *client)
{
	unsigned int retries = MAX_RETRIES;
	int err;

	do {
		err = i2c_smbus_read_byte_data(client, I2C_IRQ_SRC);
		if (err < 0)
			return err;

		if (err & I2C_IRQ_SRC_SPM)
			break;

		msleep(20);
	} while (--retries);

	return retries ? 0 : -ETIMEDOUT;
}

static ssize_t spm_read_block(struct i2c_client *client, loff_t offset,
		void *buffer, size_t size)
{
	u8 enable = I2C_SPM_CFG_ON | I2C_SPM_CFG_READ;
	int err;

	BUG_ON(size < SPM_BLOCK_SIZE);
	BUG_ON((offset & 7) != 0);

	err = i2c_smbus_write_byte_data(client, I2C_SPM_CFG, enable);
	if (err < 0)
		return err;

	err = i2c_smbus_write_byte_data(client, I2C_SPM_BASE, offset);
	if (err < 0)
		return err;

	err = i2c_smbus_read_i2c_block_data(client, 0, SPM_BLOCK_SIZE, buffer);
	if (err < 0)
		return err;

	err = i2c_smbus_write_byte_data(client, I2C_SPM_CFG, I2C_SPM_CFG_OFF);
	if (err < 0)
		return err;

	return 0;
}

static ssize_t spm_write_block(struct i2c_client *client, loff_t offset,
		const void *buffer, size_t size)
{
	u8 enable = I2C_SPM_CFG_ON | I2C_SPM_CFG_WRITE;
	int err;

	BUG_ON(size < SPM_BLOCK_SIZE);
	BUG_ON((offset & 7) != 0);

	err = i2c_smbus_write_byte_data(client, I2C_SPM_CFG, enable);
	if (err < 0) {
		dev_warn(&client->dev, "%s failed: %d\n",
			 "i2c_smbus_write_byte_data(I2C_SPM_CFG, ON)",
			 err);
		return err;
	}

	err = i2c_smbus_write_byte_data(client, I2C_SPM_BASE, offset);
	if (err < 0) {
		dev_warn(&client->dev, "%s failed: %d\n",
			 "i2c_smbus_write_byte_data(I2C_SPM_BASE)",
			 err);
		return err;
	}

	err = i2c_smbus_write_i2c_block_data(client, 0, SPM_BLOCK_SIZE, buffer);
	if (err < 0) {
		dev_warn(&client->dev, "%s failed: %d\n",
			 "i2c_smbus_write_i2c_block_data()",
			 err);
		return err;
	}

	err = i2c_smbus_write_byte_data(client, I2C_SPM_CFG, I2C_SPM_CFG_OFF);
	if (err < 0) {
		dev_warn(&client->dev, "%s failed: %d\n",
			 "i2c_smbus_write_byte_data(I2C_SPM_CFG, OFF)",
			 err);
		return err;
	}

	err = spm_wait(client);
	if (err < 0) {
		if (err == -ETIMEDOUT) {
			dev_warn(&client->dev, "spm_wait() timed out\n");
			err = 0;
		}

		return err;
	}

	return 0;
}

ssize_t sx8634_spm_load(struct sx8634 *sx)
{
	loff_t offset;
	ssize_t err;

	if (sx->spm_dirty != 0)
		dev_warn(&sx->client->dev, "discarding modified SPM cache\n");

	memset(sx->spm_cache, 0, SPM_SIZE);

	for (offset = 0; offset < SPM_SIZE; offset += SPM_BLOCK_SIZE) {
		err = spm_read_block(sx->client, offset,
				sx->spm_cache + offset, SPM_BLOCK_SIZE);
		if (err < 0) {
			dev_err(&sx->client->dev, "spm_read_block(): %d\n",
					err);
			return err;
		}
	}

	sx->spm_dirty = 0;

	return 0;
}
EXPORT_SYMBOL(sx8634_spm_load);

ssize_t sx8634_spm_sync(struct sx8634 *sx)
{
	int bit;

	for_each_set_bit(bit, &sx->spm_dirty, SPM_NUM_BLOCKS) {
		loff_t offset = bit * SPM_BLOCK_SIZE;
		ssize_t err;

		err = spm_write_block(sx->client, offset,
				sx->spm_cache + offset, SPM_BLOCK_SIZE);
		if (err < 0) {
			dev_err(&sx->client->dev, "spm_write_block(): %d\n",
					err);
			return err;
		}
	}

	sx->spm_dirty = 0;

	return 0;
}
EXPORT_SYMBOL(sx8634_spm_sync);

int sx8634_spm_read(struct sx8634 *sx, unsigned int offset, u8 *value)
{
	if (offset >= SPM_SIZE)
		return -ENXIO;

	*value = sx->spm_cache[offset];

	return 0;
}
EXPORT_SYMBOL(sx8634_spm_read);

int sx8634_spm_write(struct sx8634 *sx, unsigned int offset, u8 value)
{
	if (offset >= SPM_SIZE)
		return -ENXIO;

	sx->spm_dirty |= BIT(offset / SPM_BLOCK_SIZE);
	sx->spm_cache[offset] = value;

	return 0;
}
EXPORT_SYMBOL(sx8634_spm_write);

static int sx8634_reset(struct sx8634 *sx)
{
	unsigned int retries = MAX_RETRIES;
	int err;

	err = i2c_smbus_write_byte_data(sx->client, I2C_SOFT_RESET, 0xde);
	if (err < 0)
		return err;

	err = i2c_smbus_write_byte_data(sx->client, I2C_SOFT_RESET, 0x00);
	if (err < 0)
		return err;

	do {
		err = i2c_smbus_read_byte_data(sx->client, I2C_IRQ_SRC);
		if (err < 0)
			return err;

		if (err & I2C_IRQ_SRC_READY)
			break;

		msleep(20);
	} while (--retries);

	return retries ? 0 : -ETIMEDOUT;
}

int sx8634_register_notifier(struct sx8634 *sx, struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&sx->irq_notifier, nb);
}
EXPORT_SYMBOL(sx8634_register_notifier);

int sx8634_unregister_notifier(struct sx8634 *sx, struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&sx->irq_notifier, nb);
}
EXPORT_SYMBOL(sx8634_unregister_notifier);

static irqreturn_t sx8634_irq(int irq, void *data)
{
	struct sx8634 *sx = data;
	int pending;
	int err;

	dev_dbg(&sx->client->dev, "> %s(irq=%d, data=%p)\n",
		__func__, irq, data);

	sx8634_lock(sx);

	err = i2c_smbus_read_byte_data(sx->client, I2C_IRQ_SRC);
	if (err <= 0) {
		if (err < 0)
			dev_err(&sx->client->dev,
				"failed to read IRQ source register: %d\n",
				err);
		sx8634_unlock(sx);
		return IRQ_NONE;
	}

	pending = err;

	if (pending & I2C_IRQ_SRC_GPI)
		dev_dbg(&sx->client->dev, "GPI event\n");

	if (pending & I2C_IRQ_SRC_SPM)
		dev_dbg(&sx->client->dev, "SPM event\n");

	if (pending & I2C_IRQ_SRC_NVM)
		dev_dbg(&sx->client->dev, "NVM event\n");

	if (pending & I2C_IRQ_SRC_READY)
		dev_dbg(&sx->client->dev, "ready event\n");

	blocking_notifier_call_chain(&sx->irq_notifier, pending, sx);

	sx8634_unlock(sx);

	dev_dbg(&sx->client->dev, "< %s()\n", __func__);

	return IRQ_HANDLED;
}

static ssize_t sx8634_spm_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sx8634 *sx = i2c_get_clientdata(client);
	ssize_t len = 0;
	size_t i, j;
	int err;

	sx8634_lock(sx);

	err = sx8634_spm_load(sx);
	if (err < 0) {
		sx8634_unlock(sx);
		return err;
	}

	for (i = 0; i < SPM_SIZE; i += SPM_BLOCK_SIZE) {
		const char *prefix = "";

		for (j = 0; j < SPM_BLOCK_SIZE; j++) {
			len += sprintf(buf + len, "%s%02x", prefix,
					sx->spm_cache[i + j]);
			prefix = " ";
		}

		len += sprintf(buf + len, "\n");
	}

	sx8634_unlock(sx);
	return len;
}

static DEVICE_ATTR(spm, 0664, sx8634_spm_show, NULL);

static struct attribute *sx8634_attributes[] = {
	&dev_attr_spm.attr,
	NULL
};

static const struct attribute_group sx8634_attr_group = {
	.attrs = sx8634_attributes,
};

static struct mfd_cell sx8634_cells[] = {
	{
		.name = "sx8634-touch",
	},
	{
		.name = "sx8634-backlight",
	},
};

#ifdef CONFIG_OF
static int sx8634_parse_dt(struct device *dev,
			struct sx8634_platform_data *pdata)
{
	struct device_node *node = dev->of_node;

	if (!node)
		return -ENODEV;

	memset(pdata, 0, sizeof(*pdata));

	pdata->reset_gpio = of_get_gpio(node, 0);

	return 0;
}

static struct of_device_id sx8634_of_match[] = {
	{ .compatible = "semtech,sx8634" },
	{ }
};
MODULE_DEVICE_TABLE(of, sx8634_of_match);
#else
static int sx8634_parse_dt(struct device *dev,
			struct sx8634_platform_data *pdata)
{
	return -ENODEV;
}

#define sx8634_of_match NULL
#endif

static int __devinit sx8634_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct sx8634_platform_data *pdata = client->dev.platform_data;
	struct sx8634_platform_data defpdata;
	struct sx8634 *sx;
	int err = 0;

	if (!pdata) {
		err = sx8634_parse_dt(&client->dev, &defpdata);
		if (err < 0)
			return err;

		pdata = &defpdata;
	}

	sx = devm_kzalloc(&client->dev, sizeof(*sx), GFP_KERNEL);
	if (!sx)
		return -ENOMEM;

	sx->spm_cache = devm_kzalloc(&client->dev, SPM_SIZE, GFP_KERNEL);
	if (!sx->spm_cache)
		return -ENOMEM;

	BLOCKING_INIT_NOTIFIER_HEAD(&sx->irq_notifier);
	mutex_init(&sx->lock);
	sx->reset_gpio = pdata->reset_gpio;
	sx->client = client;
	i2c_set_clientdata(client, sx);

	/* Reset using RESETB if possible, otherwise reset over i2c */
	if (gpio_is_valid(sx->reset_gpio)) {
		err = gpio_request_one(sx->reset_gpio, GPIOF_OUT_INIT_LOW,
				"sx8634 reset");
		if (err < 0) {
			dev_err(&client->dev,
				"failed to setup reset GPIO: %d\n", err);
			return err;
		}
		/* Only 50ns is really required */
		usleep_range(1, 1000);
		gpio_direction_output(sx->reset_gpio, 1);
		/* Wait for the power up to complete */
		msleep(150);
	} else {
		err = sx8634_reset(sx);
		if (err == -ETIMEDOUT) {
			dev_warn(&sx->client->dev, "sx8634_reset() timed out\n");
		} else {
			dev_err(&sx->client->dev, "sx8634_reset(): %d\n", err);
			return err;
		}
	}

	err = sysfs_create_group(&client->dev.kobj, &sx8634_attr_group);
	if (err < 0)
		goto free_gpio;

	/* clear interrupts */
	err = i2c_smbus_read_byte_data(client, I2C_IRQ_SRC);
	if (err < 0) {
		dev_err(&client->dev, "can't clear interrupts: %d\n", err);
		goto remove_sysfs;
	}

	err = request_threaded_irq(client->irq, NULL, sx8634_irq,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "sx8634", sx);
	if (err < 0) {
		dev_err(&client->dev, "can't allocate IRQ#%d\n", client->irq);
		goto remove_sysfs;
	}

	err = mfd_add_devices(&client->dev, pdata->id,
			sx8634_cells, ARRAY_SIZE(sx8634_cells),
			NULL, 0);
	if (err) {
		dev_err(&client->dev, "failed to add devices: %d\n", err);
		goto remove_sysfs;
	}

	return 0;

remove_sysfs:
	sysfs_remove_group(&client->dev.kobj, &sx8634_attr_group);
free_gpio:
	if (gpio_is_valid(sx->reset_gpio)) {
		gpio_direction_output(sx->reset_gpio, 0);
		gpio_free(sx->reset_gpio);
	}
	return err;
}

static int __devexit sx8634_i2c_remove(struct i2c_client *client)
{
	struct sx8634 *sx = i2c_get_clientdata(client);

	sysfs_remove_group(&client->dev.kobj, &sx8634_attr_group);

	if (gpio_is_valid(sx->reset_gpio)) {
		gpio_direction_output(sx->reset_gpio, 0);
		gpio_free(sx->reset_gpio);
	}

	return 0;
}

#ifdef CONFIG_PM
static int sx8634_i2c_suspend(struct device *dev)
{
	return 0;
}

static int sx8634_i2c_resume(struct device *dev)
{
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(sx8634_i2c_pm,
		sx8634_i2c_suspend,
		sx8634_i2c_resume);

static const struct i2c_device_id sx8634_i2c_ids[] = {
	{ "sx8634", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sx8634_i2c_ids);

static struct i2c_driver sx8634_driver = {
	.driver = {
		.name = "sx8634",
		.owner = THIS_MODULE,
		.pm = &sx8634_i2c_pm,
		.of_match_table = sx8634_of_match,
	},
	.probe = sx8634_i2c_probe,
	.remove = __devexit_p(sx8634_i2c_remove),
	.id_table = sx8634_i2c_ids,
};

module_i2c_driver(sx8634_driver);

MODULE_AUTHOR("Thierry Reding <thierry.reding@avionic-design.de>");
MODULE_AUTHOR("Alban Bedel <alban.bedel@avionic-design.de>");
MODULE_DESCRIPTION("Semtech SX8634 Controller Driver");
MODULE_LICENSE("GPL");
