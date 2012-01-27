/*
 * Copyright (C) 2011 Avionic Design GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <linux/input/sx8634.h>

#define I2C_IRQ_SRC 0x00
#define I2C_IRQ_SRC_MODE (1 << 0)
#define I2C_IRQ_SRC_COMPENSATION (1 << 1)
#define I2C_IRQ_SRC_BUTTONS (1 << 2)
#define I2C_IRQ_SRC_SLIDER (1 << 3)
#define I2C_IRQ_SRC_GPI (1 << 4)
#define I2C_IRQ_SRC_SPM (1 << 5)
#define I2C_IRQ_SRC_NVM (1 << 6)
#define I2C_IRQ_SRC_READY (1 << 7)

#define I2C_CAP_STAT_MSB 0x01
#define I2C_CAP_STAT_LSB 0x02
#define I2C_SLD_POS_MSB 0x03
#define I2C_SLD_POS_LSB 0x04
#define I2C_GPI_STAT 0x07
#define I2C_SPM_STAT 0x08
#define I2C_COMP_OP_MODE 0x09
#define I2C_GPO_CTRL 0x0a
#define I2C_GPP_PIN_ID 0x0b
#define I2C_GPP_INTENSITY 0x0c
#define I2C_SPM_CFG 0x0d
#define I2C_SPM_CFG_WRITE (0 << 3)
#define I2C_SPM_CFG_READ (1 << 3)
#define I2C_SPM_CFG_OFF (0 << 4)
#define I2C_SPM_CFG_ON (1 << 4)
#define I2C_SPM_BASE 0x0e
#define I2C_SPM_KEY_MSB 0xac
#define I2C_SPM_KEY_LSB 0xad
#define I2C_SOFT_RESET 0xb1

#define SPM_CFG 0x00
#define SPM_CAP_MODE_MISC 0x09

#define SPM_CAP_MODE(x) (((x) <= 3) ? 0x0c : (((x) <= 7) ? 0x0b : 0x0a))
#define SPM_CAP_MODE_SHIFT(x) (((x) & 3) * 2)
#define SPM_CAP_MODE_MASK 0x3
#define SPM_CAP_MODE_MASK_SHIFTED(x) (SPM_CAP_MODE_MASK << SPM_CAP_MODE_SHIFT(x))

#define SPM_CAP_SENS(x) (0x0d + ((x) / 2))
#define SPM_CAP_SENS_MAX 0x7
#define SPM_CAP_SENS_SHIFT(x) (((x) & 1) ? 0 : 4)
#define SPM_CAP_SENS_MASK 0x7
#define SPM_CAP_SENS_MASK_SHIFTED(x) (SPM_CAP_SENS_MASK << SPM_CAP_SENS_SHIFT(x))

#define SPM_CAP_THRESHOLD(x) (0x13 + (x))
#define SPM_CAP_THRESHOLD_MAX 0xff

#define SPM_BLOCK_SIZE 8
#define SPM_NUM_BLOCKS 16
#define SPM_SIZE (SPM_BLOCK_SIZE * SPM_NUM_BLOCKS)

#define MAX_RETRIES 64

struct sx8634 {
	struct i2c_client *client;
	struct input_dev *input;
	unsigned short keycodes[SX8634_NUM_CAPS];
	unsigned long spm_dirty;
	u8 *spm_cache;
	u16 status;
};

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

		msleep(10);
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

static ssize_t sx8634_spm_load(struct sx8634 *sx)
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

static ssize_t sx8634_spm_sync(struct sx8634 *sx)
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

static int sx8634_spm_read(struct sx8634 *sx, unsigned int offset, u8 *value)
{
	if (offset >= SPM_SIZE)
		return -ENXIO;

	*value = sx->spm_cache[offset];

	return 0;
}

static int sx8634_spm_write(struct sx8634 *sx, unsigned int offset, u8 value)
{
	if (offset >= SPM_SIZE)
		return -ENXIO;

	sx->spm_dirty |= BIT(offset / SPM_BLOCK_SIZE);
	sx->spm_cache[offset] = value;

	return 0;
}

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

		msleep(10);
	} while (--retries);

	return retries ? 0 : -ETIMEDOUT;
}

static irqreturn_t sx8634_irq(int irq, void *data)
{
	struct sx8634 *sx = data;
	bool need_sync = false;
	u8 pending;
	int err;

	dev_dbg(&sx->client->dev, "> %s(irq=%d, data=%p)\n", __func__, irq, data);

	err = i2c_smbus_read_byte_data(sx->client, I2C_IRQ_SRC);
	if (err < 0) {
		dev_err(&sx->client->dev, "failed to read IRQ source register: %d\n", err);
		return IRQ_NONE;
	}

	pending = err;

	dev_dbg(&sx->client->dev, "%s(): pending: %02x\n", __func__, pending);

	if (pending & I2C_IRQ_SRC_COMPENSATION)
		dev_dbg(&sx->client->dev, "compensation complete\n");

	if (pending & I2C_IRQ_SRC_BUTTONS) {
		unsigned long changed;
		unsigned int cap;
		u16 status;

		err = i2c_smbus_read_byte_data(sx->client, I2C_CAP_STAT_MSB);
		if (err < 0) {
			dev_err(&sx->client->dev, "failed to read status register: %d\n", err);
			return IRQ_NONE;
		}

		status = err << 8;

		err = i2c_smbus_read_byte_data(sx->client, I2C_CAP_STAT_LSB);
		if (err < 0) {
			dev_err(&sx->client->dev, "failed to read status register: %d\n", err);
			return IRQ_NONE;
		}

		status |= err;

		changed = status ^ sx->status;

		dev_dbg(&sx->client->dev, "status:%04x changed:%04lx\n", status, changed);

		for_each_set_bit(cap, &changed, SX8634_NUM_CAPS) {
			unsigned int level = (status & BIT(cap)) ? 1 : 0;
			input_report_key(sx->input, sx->keycodes[cap], level);
			need_sync = true;
		}

		sx->status = status;
	}

	if (pending & I2C_IRQ_SRC_SLIDER) {
		u16 status;

		dev_dbg(&sx->client->dev, "slider event\n");

		err = i2c_smbus_read_byte_data(sx->client, I2C_CAP_STAT_MSB);
		if (err < 0) {
			dev_err(&sx->client->dev, "failed to read status register: %d\n", err);
			return IRQ_NONE;
		}

		status = err << 8;

		err = i2c_smbus_read_byte_data(sx->client, I2C_CAP_STAT_LSB);
		if (err < 0) {
			dev_err(&sx->client->dev, "failed to read status register: %d\n", err);
			return IRQ_NONE;
		}

		status |= err;

		dev_dbg(&sx->client->dev, "status:%04x\n", status);
	}

	if (need_sync)
		input_sync(sx->input);

	if (pending & I2C_IRQ_SRC_GPI)
		dev_dbg(&sx->client->dev, "GPI event\n");

	if (pending & I2C_IRQ_SRC_SPM)
		dev_dbg(&sx->client->dev, "SPM event\n");

	if (pending & I2C_IRQ_SRC_NVM)
		dev_dbg(&sx->client->dev, "NVM event\n");

	if (pending & I2C_IRQ_SRC_READY)
		dev_dbg(&sx->client->dev, "ready event\n");

	dev_dbg(&sx->client->dev, "< %s()\n", __func__);
	return IRQ_HANDLED;
}

static int sx8634_set_mode(struct sx8634 *sx, unsigned int cap, enum sx8634_cap_mode mode)
{
	u8 value = 0;
	int err;

	if ((cap >= SX8634_NUM_CAPS) || (mode == SX8634_CAP_MODE_RESERVED))
		return -EINVAL;

	err = sx8634_spm_read(sx, SPM_CAP_MODE(cap), &value);
	if (err < 0)
		return err;

	value &= ~SPM_CAP_MODE_MASK_SHIFTED(cap);
	value |= (mode & SPM_CAP_MODE_MASK) << SPM_CAP_MODE_SHIFT(cap);

	err = sx8634_spm_write(sx, SPM_CAP_MODE(cap), value);
	if (err < 0)
		return err;

	return 0;
}

static int sx8634_set_sensitivity(struct sx8634 *sx, unsigned int cap,
		u8 sensitivity)
{
	u8 value = 0;
	int err = 0;

	if (cap >= SX8634_NUM_CAPS)
		return -EINVAL;

	err = sx8634_spm_read(sx, SPM_CAP_SENS(cap), &value);
	if (err < 0)
		return err;

	value &= ~SPM_CAP_SENS_MASK_SHIFTED(cap);
	value |= (sensitivity & SPM_CAP_SENS_MASK) << SPM_CAP_SENS_SHIFT(cap);

	err = sx8634_spm_write(sx, SPM_CAP_SENS(cap), value);
	if (err < 0)
		return err;

	return 0;
}

static int sx8634_set_threshold(struct sx8634 *sx, unsigned int cap,
		u8 threshold)
{
	int err;

	if (cap >= SX8634_NUM_CAPS)
		return -EINVAL;

	err = sx8634_spm_write(sx, SPM_CAP_THRESHOLD(cap), threshold);
	if (err < 0)
		return err;

	return 0;
}

static int sx8634_setup(struct sx8634 *sx, struct sx8634_platform_data *pdata)
{
	bool slider = false;
	unsigned int i;
	int err;

	err = sx8634_reset(sx);
	if (err < 0) {
		if (err == -ETIMEDOUT) {
			dev_warn(&sx->client->dev, "spm_wait() timed out\n");
		} else {
			dev_err(&sx->client->dev, "sx8634_reset(): %d\n", err);
			return err;
		}
	}

	err = sx8634_spm_load(sx);
	if (err < 0) {
		dev_dbg(&sx->client->dev, "sx8634_spm_load(): %d\n", err);
		return err;
	}

	/* disable all capacitive sensors */
	for (i = 0; i < SX8634_NUM_CAPS; i++) {
		err = sx8634_set_mode(sx, i, SX8634_CAP_MODE_DISABLED);
		if (err < 0)
			return err;
	}

	err = sx8634_spm_sync(sx);
	if (err < 0) {
		dev_dbg(&sx->client->dev, "sx8634_spm_sync(): %d\n", err);
		return err;
	}

	err = sx8634_spm_load(sx);
	if (err < 0) {
		dev_dbg(&sx->client->dev, "sx8634_spm_load(): %d\n", err);
		return err;
	}

	/* configure capacitive sensor parameters */
	for (i = 0; i < SX8634_NUM_CAPS; i++) {
		struct sx8634_cap *cap = &pdata->caps[i];

		err = sx8634_set_sensitivity(sx, i, cap->sensitivity);
		if (err < 0) {
		}

		err = sx8634_set_threshold(sx, i, cap->threshold);
		if (err < 0) {
		}
	}

	err = sx8634_spm_sync(sx);
	if (err < 0) {
		dev_dbg(&sx->client->dev, "sx8634_spm_sync(): %d\n", err);
		return err;
	}

	err = sx8634_spm_load(sx);
	if (err < 0) {
		dev_dbg(&sx->client->dev, "sx8634_spm_load(): %d\n", err);
		return err;
	}

	/* enable individual cap sensitivity */
	err = sx8634_spm_write(sx, SPM_CAP_MODE_MISC, 0x04);
	if (err < 0)
		return err;

	/* enable capacitive sensors */
	for (i = 0; i < SX8634_NUM_CAPS; i++) {
		struct sx8634_cap *cap = &pdata->caps[i];

		if (cap->mode == SX8634_CAP_MODE_BUTTON) {
			input_set_capability(sx->input, EV_KEY, cap->keycode);
			sx->keycodes[i] = cap->keycode;
		}

		if (cap->mode == SX8634_CAP_MODE_SLIDER)
			slider = true;

		err = sx8634_set_mode(sx, i, cap->mode);
		if (err < 0) {
		}
	}

	err = sx8634_spm_sync(sx);
	if (err < 0) {
		dev_dbg(&sx->client->dev, "sx8634_spm_sync(): %d\n", err);
		return err;
	}

	sx->input->id.bustype = BUS_I2C;
	sx->input->id.product = 0;
	sx->input->id.version = 0;
	sx->input->name = "sx8634";
	sx->input->dev.parent = &sx->client->dev;

	/* setup sliders */
	if (slider) {
		/* TODO: implement this properly */
		input_set_abs_params(sx->input, ABS_MISC, 0, 100, 0, 0);
		input_set_capability(sx->input, EV_ABS, ABS_MISC);
	}

	return 0;
}

static ssize_t sx8634_spm_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sx8634 *sx = i2c_get_clientdata(client);
	ssize_t len = 0;
	size_t i, j;
	int err;

	err = sx8634_spm_load(sx);
	if (err < 0)
		return err;

	for (i = 0; i < SPM_SIZE; i += SPM_BLOCK_SIZE) {
		const char *prefix = "";

		for (j = 0; j < SPM_BLOCK_SIZE; j++) {
			len += sprintf(buf + len, "%s%02x", prefix,
					sx->spm_cache[i + j]);
			prefix = " ";
		}

		len += sprintf(buf + len, "\n");
	}

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

#ifdef CONFIG_OF
static int sx8634_parse_dt(struct device *dev, struct sx8634_platform_data *pdata)
{
	struct device_node *node = dev->of_node;
	struct device_node *child = NULL;
	u32 sensitivity_def = 0x00;
	u32 threshold_def = 0xa0;
	int err;

	if (!node)
		return -ENODEV;

	memset(pdata, 0, sizeof(*pdata));

	err = of_property_read_u32(node, "threshold", &threshold_def);
	if (err < 0) {
	}

	if (threshold_def > SPM_CAP_THRESHOLD_MAX) {
		dev_info(dev, "invalid threshold: %u, using %u\n",
				threshold_def, SPM_CAP_THRESHOLD_MAX);
		threshold_def = SPM_CAP_THRESHOLD_MAX;
	}

	err = of_property_read_u32(node, "sensitivity", &sensitivity_def);
	if (err < 0) {
	}

	if (sensitivity_def > SPM_CAP_SENS_MAX) {
		dev_info(dev, "invalid sensitivity: %u, using %u\n",
				sensitivity_def, SPM_CAP_SENS_MAX);
		sensitivity_def = SPM_CAP_SENS_MAX;
	}

	while ((child = of_get_next_child(node, child))) {
		u32 sensitivity = sensitivity_def;
		u32 threshold = threshold_def;
		struct sx8634_cap *cap;
		u32 keycode;
		u32 index;

		err = of_property_read_u32(child, "reg", &index);
		if (err < 0) {
		}

		if (index >= SX8634_NUM_CAPS) {
			dev_err(dev, "invalid cap index: %u\n", index);
			continue;
		}

		cap = &pdata->caps[index];

		err = of_property_read_u32(child, "threshold", &threshold);
		if (err < 0) {
		}

		cap->threshold = threshold;

		err = of_property_read_u32(child, "sensitivity", &sensitivity);
		if (err < 0) {
		}

		cap->sensitivity = sensitivity;

		err = of_property_read_u32(child, "linux,code", &keycode);
		if (err == 0) {
			cap->mode = SX8634_CAP_MODE_BUTTON;
			cap->keycode = keycode;
		} else {
			cap->mode = SX8634_CAP_MODE_SLIDER;
		}
	}

	return 0;
}

static struct of_device_id sx8634_of_match[] = {
	{ .compatible = "semtech,sx8634" },
	{ }
};
MODULE_DEVICE_TABLE(of, sx8634_of_match);
#else
static int sx8634_parse_dt(struct device *dev, struct sx8634_platform_data *pdata)
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
	if (!sx) {
		err = -ENOMEM;
		goto out;
	}

	sx->spm_cache = devm_kzalloc(&client->dev, SPM_SIZE, GFP_KERNEL);
	if (!sx->spm_cache) {
		err = -ENOMEM;
		goto out;
	}

	sx->input = input_allocate_device();
	if (!sx->input) {
		err = -ENOMEM;
		goto out;
	}

	sx->client = client;

	err = sx8634_setup(sx, pdata);
	if (err < 0)
		goto free;

	err = sysfs_create_group(&client->dev.kobj, &sx8634_attr_group);
	if (err < 0)
		goto free;

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

	err = input_register_device(sx->input);
	if (err < 0)
		goto free_irq;

	i2c_set_clientdata(client, sx);

	err = 0;
	goto out;

free_irq:
	free_irq(client->irq, sx);
remove_sysfs:
	sysfs_remove_group(&client->dev.kobj, &sx8634_attr_group);
free:
	input_free_device(sx->input);
out:
	return err;
}

static int __devexit sx8634_i2c_remove(struct i2c_client *client)
{
	struct sx8634 *sx = i2c_get_clientdata(client);

	input_unregister_device(sx->input);
	sysfs_remove_group(&client->dev.kobj, &sx8634_attr_group);
	free_irq(client->irq, sx);

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

static int __init sx8634_i2c_init(void)
{
	return i2c_add_driver(&sx8634_driver);
}
module_init(sx8634_i2c_init);

static void __exit sx8634_i2c_exit(void)
{
	i2c_del_driver(&sx8634_driver);
}
module_exit(sx8634_i2c_exit);

MODULE_AUTHOR("Thierry Reding <thierry.reding@avionic-design.de>");
MODULE_DESCRIPTION("Semtech SX8634 Controller Driver");
MODULE_LICENSE("GPL");
