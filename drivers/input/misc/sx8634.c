/*
 * Copyright (C) 2011 Avionic Design GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>

#include <linux/mfd/sx8634.h>
#include <linux/input/sx8634.h>

struct sx8634_touch {
	struct sx8634 *core;
	struct input_dev *input;
	struct notifier_block irq;
	unsigned short keycodes[SX8634_NUM_CAPS];
	u16 status;
};

static int sensitivity = -1;
module_param(sensitivity, int, S_IRUGO);
MODULE_PARM_DESC(sensitivity, "The pad sensitivity (0-7).");

static int threshold = -1;
module_param(threshold, int, S_IRUGO);
MODULE_PARM_DESC(threshold, "The value which needs to be exceed or fall below to trigger (0-100).");

static int debounce = -1;
module_param(debounce, int, S_IRUGO);
MODULE_PARM_DESC(debounce, "The number of samples above the threshold (1-4, 1 is default).");

/* The lock is held while this function is called */
static int sx8634_touch_irq(struct notifier_block *nb,
			unsigned long pending, void *data)
{
	struct sx8634_touch *sx = container_of(nb, struct sx8634_touch, irq);
	struct device *dev = sx->input->dev.parent;
	bool need_sync = false;
	int err;

	dev_dbg(dev, "> %s(pending=%02lx, data=%p)\n",
		__func__, pending, data);

	if (pending & I2C_IRQ_SRC_COMPENSATION)
		dev_dbg(dev, "compensation complete\n");

	if (pending & I2C_IRQ_SRC_BUTTONS) {
		unsigned long changed;
		unsigned int cap;
		u16 status;

		err = sx8634_read_reg(sx->core, I2C_CAP_STAT_MSB);
		if (err < 0) {
			dev_err(dev, "failed to read status register: %d\n",
				err);
			return notifier_from_errno(err);
		}

		status = err << 8;

		err = sx8634_read_reg(sx->core, I2C_CAP_STAT_LSB);
		if (err < 0) {
			dev_err(dev, "failed to read status register: %d\n",
				err);
			return notifier_from_errno(err);
		}

		status |= err;

		changed = status ^ sx->status;

		dev_dbg(dev, "status:%04x changed:%04lx\n", status, changed);

		for_each_set_bit(cap, &changed, SX8634_NUM_CAPS) {
			unsigned int level = (status & BIT(cap)) ? 1 : 0;
			input_report_key(sx->input, sx->keycodes[cap], level);
			need_sync = true;
		}

		sx->status = status;
	}

	if (pending & I2C_IRQ_SRC_SLIDER) {
		u16 status;

		dev_dbg(dev, "slider event\n");

		err = sx8634_read_reg(sx->core, I2C_CAP_STAT_MSB);
		if (err < 0) {
			dev_err(dev, "failed to read status register: %d\n",
				err);
			return notifier_from_errno(err);
		}

		status = err << 8;

		err = sx8634_read_reg(sx->core, I2C_CAP_STAT_LSB);
		if (err < 0) {
			dev_err(dev, "failed to read status register: %d\n",
				err);
			return notifier_from_errno(err);
		}

		status |= err;

		dev_dbg(dev, "status:%04x\n", status);
	}

	if (need_sync)
		input_sync(sx->input);

	dev_dbg(dev, "< %s()\n", __func__);

	return NOTIFY_OK;
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

	if (sensitivity < 0x00 || sensitivity > 0x07)
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

	if (threshold < 0x00 || threshold > 0xa0)
		return -EINVAL;

	err = sx8634_spm_write(sx, SPM_CAP_THRESHOLD(cap), threshold);
	if (err < 0)
		return err;

	return 0;
}

static int sx8634_set_debounce(struct sx8634 *sx, u8 samples)
{
	u8 value = 0;
	int err;

	if (samples < 1 || samples > 4)
		return -EINVAL;

	err = sx8634_spm_read(sx, SPM_BTN_CFG, &value);
	if (err < 0)
		return err;

	value &= ~SPM_BTN_CFG_TOUCH_DEBOUNCE_MASK;
	value |= (samples - 1) << SPM_BTN_CFG_TOUCH_DEBOUNCE_SHIFT;

	err = sx8634_spm_write(sx, SPM_BTN_CFG, value);
	if (err < 0)
		return err;

	return 0;
}

static int sx8634_touch_setup(struct platform_device *pdev,
			struct sx8634_touch_platform_data *pdata)
{
	struct sx8634_touch *sx = platform_get_drvdata(pdev);
	bool slider = false;
	unsigned int i;
	u8 value;
	int err;

	sx8634_lock(sx->core);

	err = sx8634_spm_load(sx->core);
	if (err < 0) {
		dev_dbg(&pdev->dev, "sx8634_spm_load(): %d\n", err);
		goto unlock;
	}

	/* disable all capacitive sensors */
	for (i = 0; i < SX8634_NUM_CAPS; i++) {
		err = sx8634_set_mode(sx->core, i, SX8634_CAP_MODE_DISABLED);
		if (err < 0)
			goto unlock;
	}

	err = sx8634_spm_sync(sx->core);
	if (err < 0) {
		dev_dbg(&pdev->dev, "sx8634_spm_sync(): %d\n", err);
		goto unlock;
	}

	err = sx8634_spm_load(sx->core);
	if (err < 0) {
		dev_dbg(&pdev->dev, "sx8634_spm_load(): %d\n", err);
		goto unlock;
	}

	/* configure capacitive sensor parameters */
	for (i = 0; i < SX8634_NUM_CAPS; i++) {
		struct sx8634_cap *cap = &pdata->caps[i];

		if (sensitivity < 0)
			value = cap->sensitivity;
		else
			value = sensitivity;

		err = sx8634_set_sensitivity(sx->core, i, value);
		if (err < 0) {
		}

		if (threshold < 0)
			value = cap->threshold;
		else
			value = threshold;

		err = sx8634_set_threshold(sx->core, i, value);
		if (err < 0) {
		}
	}

	if (debounce < 0)
		value = pdata->debounce;
	else
		value = debounce;

	err = sx8634_set_debounce(sx->core, value);
	if (err < 0) {
		dev_warn(&pdev->dev, "sx8634_set_debounce(samples=%d): %d\n",
			value, err);
		goto unlock;
	}

	err = sx8634_spm_sync(sx->core);
	if (err < 0) {
		dev_dbg(&pdev->dev, "sx8634_spm_sync(): %d\n", err);
		goto unlock;
	}

	err = sx8634_spm_load(sx->core);
	if (err < 0) {
		dev_dbg(&pdev->dev, "sx8634_spm_load(): %d\n", err);
		goto unlock;
	}

	/* enable individual cap sensitivity */
	err = sx8634_spm_write(sx->core, SPM_CAP_MODE_MISC, 0x04);
	if (err < 0)
		goto unlock;

	/* enable capacitive sensors */
	for (i = 0; i < SX8634_NUM_CAPS; i++) {
		struct sx8634_cap *cap = &pdata->caps[i];

		if (cap->mode == SX8634_CAP_MODE_BUTTON) {
			input_set_capability(sx->input, EV_KEY, cap->keycode);
			sx->keycodes[i] = cap->keycode;
		}

		if (cap->mode == SX8634_CAP_MODE_SLIDER)
			slider = true;

		err = sx8634_set_mode(sx->core, i, cap->mode);
		if (err < 0) {
		}
	}

	err = sx8634_spm_sync(sx->core);
	if (err < 0) {
		dev_dbg(&pdev->dev, "sx8634_spm_sync(): %d\n", err);
		goto unlock;
	}

	sx->input->id.bustype = BUS_I2C;
	sx->input->id.product = 0;
	sx->input->id.version = 0;
	sx->input->name = "sx8634";
	sx->input->dev.parent = &pdev->dev;

	/* setup sliders */
	if (slider) {
		/* TODO: implement this properly */
		input_set_abs_params(sx->input, ABS_MISC, 0, 100, 0, 0);
		input_set_capability(sx->input, EV_ABS, ABS_MISC);
	}

unlock:
	sx8634_unlock(sx->core);
	return err;
}

#ifdef CONFIG_OF
static int sx8634_parse_dt(struct device *dev,
			struct sx8634_touch_platform_data *pdata)
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

static struct of_device_id sx8634_touch_of_match[] = {
	{ .compatible = "semtech,sx8634-touch" },
	{ }
};
MODULE_DEVICE_TABLE(of, sx8634_touch_of_match);
#else
static int sx8634_parse_dt(struct device *dev,
			struct sx8634_touch_platform_data *pdata)
{
	return -ENODEV;
}

#define sx8634_touch_of_match NULL
#endif

static int __devinit sx8634_touch_probe(struct platform_device *pdev)
{
	struct sx8634_platform_data *core_pdata =
		pdev->dev.parent->platform_data;
	struct sx8634_touch_platform_data *pdata = core_pdata->touch;
	struct sx8634_touch_platform_data defpdata;
	struct sx8634_touch *sx;
	int err = 0;

	if (!pdata) {
		err = sx8634_parse_dt(&pdev->dev, &defpdata);
		if (err < 0)
			return err;

		pdata = &defpdata;
	}

	sx = devm_kzalloc(&pdev->dev, sizeof(*sx), GFP_KERNEL);
	if (!sx)
		return -ENOMEM;

	sx->core = cell_to_sx8634(pdev);
	platform_set_drvdata(pdev, sx);

	sx->input = input_allocate_device();
	if (!sx->input)
		return -ENOMEM;

	err = sx8634_touch_setup(pdev, pdata);
	if (err < 0)
		goto free;

	sx->irq.notifier_call = sx8634_touch_irq;
	err = sx8634_register_notifier(sx->core, &sx->irq);
	if (err < 0) {
		dev_err(&pdev->dev,
			"failed to register event notifier: %d\n", err);
		goto free;
	}

	err = input_register_device(sx->input);
	if (err < 0)
		goto free_irq;

	return 0;

free_irq:
	sx8634_unregister_notifier(sx->core, &sx->irq);
free:
	input_free_device(sx->input);
	return err;
}

static int __devexit sx8634_touch_remove(struct platform_device *pdev)
{
	struct sx8634_touch *sx = platform_get_drvdata(pdev);

	sx8634_unregister_notifier(sx->core, &sx->irq);
	input_unregister_device(sx->input);

	return 0;
}

static struct platform_driver sx8634_touch_driver = {
	.driver = {
		.name = "sx8634-touch",
		.owner = THIS_MODULE,
		.of_match_table = sx8634_touch_of_match,
	},
	.probe = sx8634_touch_probe,
	.remove = __devexit_p(sx8634_touch_remove),
};

module_platform_driver(sx8634_touch_driver);

MODULE_AUTHOR("Thierry Reding <thierry.reding@avionic-design.de>");
MODULE_AUTHOR("Alban Bedel <alban.bedel@avionic-design.de>");
MODULE_DESCRIPTION("Semtech SX8634 Controller Driver");
MODULE_LICENSE("GPL");
