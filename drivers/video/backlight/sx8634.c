/*
 * Copyright (C) 2013 Avionic Design GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/backlight.h>
#include <linux/mfd/sx8634.h>

struct sx8634_backlight {
	struct backlight_device *dev;
	int pin;
};

struct sx8634_backlights {
	struct sx8634 *core;
	struct sx8634_backlight bl[GPP_PIN_COUNT];
	int bl_count;
};

static int sx8634_backlight_update_status(struct backlight_device *bl)
{
	struct sx8634_backlights *sx = dev_get_drvdata(bl->dev.parent);
	struct sx8634_backlight *sx_bl = bl_get_data(bl);
	int err;

	sx8634_lock(sx->core);

	err = sx8634_write_reg(sx->core, I2C_GPP_PIN_ID, sx_bl->pin);
	if (!err)
		err = sx8634_write_reg(sx->core, I2C_GPP_INTENSITY,
				bl->props.brightness);

	sx8634_unlock(sx->core);
	return err;
}

static int sx8634_backlight_get_brightness(struct backlight_device *bl)
{
	struct sx8634_backlights *sx = dev_get_drvdata(bl->dev.parent);
	struct sx8634_backlight *sx_bl = bl_get_data(bl);
	int err;

	sx8634_lock(sx->core);

	err = sx8634_write_reg(sx->core, I2C_GPP_PIN_ID, sx_bl->pin);
	if (!err)
		err = sx8634_read_reg(sx->core, I2C_GPP_INTENSITY);

	sx8634_unlock(sx->core);
	return err;
}


static const struct backlight_ops sx8634_backlight_ops = {
	.update_status = sx8634_backlight_update_status,
	.get_brightness = sx8634_backlight_get_brightness,
};

static int __devinit sx8634_backlight_probe(struct platform_device *pdev)
{
	struct sx8634_platform_data *core_pdata =
		pdev->dev.parent->platform_data;
	struct sx8634_backlight_platform_data *pdata =
		core_pdata->backlight;
	struct backlight_properties props = {};
	struct sx8634_backlights *sx;
	u8 old_value, pin_mask = 0;
	u8 polarity = 0;
	u8 function = 0;
	int i, err;

	if (!pdata)
		return -EINVAL;

	sx = devm_kzalloc(&pdev->dev, sizeof(*sx), GFP_KERNEL);
	if (!sx)
		return -ENOMEM;

	sx->core = cell_to_sx8634(pdev);
	platform_set_drvdata(pdev, sx);

	props.type = BACKLIGHT_PLATFORM;

	/* Create the backlight devices */
	for (i = 0 ; i < ARRAY_SIZE(pdata->pin) ; i++) {
		struct sx8634_backlight_pin *pin = &pdata->pin[i];
		struct backlight_device *bl;
		char devname[64];

		if (pin->max_brightness <= 0)
			continue;

		snprintf(devname, sizeof(devname), "%s.%d.%d",
			pdev->name, pdev->id, sx->bl_count);
		props.max_brightness = pin->max_brightness;
		bl = backlight_device_register(devname, &pdev->dev,
					&sx->bl[sx->bl_count],
					&sx8634_backlight_ops, &props);

		if (IS_ERR(bl)) {
			err = PTR_ERR(bl);
			goto unregister_bl;
		}

		pin_mask |= BIT(i);
		if (pin->high_active)
			polarity |= BIT(i);
		if (pin->linear)
			function |= BIT(i);

		sx->bl[sx->bl_count].pin = i;
		sx->bl[sx->bl_count].dev = bl;
		sx->bl_count += 1;
	}

	/* Setup the backlight pins */
	sx8634_lock(sx->core);

	err = sx8634_spm_load(sx->core);
	if (err < 0)
		goto unlock;

	/* Set the mode */
	for (i = 0 ; i < sx->bl_count ; i++) {
		int pin = sx->bl[i].pin;
		int shift = (pin & 3) << 1;
		u8 reg = (pin > 3) ?
			SPM_GPIO_MODE_7_4 : SPM_GPIO_MODE_3_0;

		err = sx8634_spm_write(sx->core, reg,
				SPM_GPIO_MODE_GPP << shift);
		if (err < 0)
			goto unlock;
	}

	/* Set the polarity */
	err = sx8634_spm_read(sx->core, SPM_GPIO_POLARITY, &old_value);
	if (err < 0)
		goto unlock;

	polarity |= (old_value & ~pin_mask);

	err = sx8634_spm_write(sx->core, SPM_GPIO_POLARITY, polarity);
	if (err < 0)
		goto unlock;

	/* Set the function */
	err = sx8634_spm_read(sx->core, SPM_GPIO_FUNCTION, &old_value);
	if (err < 0)
		goto unlock;

	function |= (old_value & ~pin_mask);

	err = sx8634_spm_write(sx->core, SPM_GPIO_FUNCTION, function);
	if (err < 0)
		goto unlock;

	err = sx8634_spm_sync(sx->core);
	if (err < 0)
		goto unlock;

	sx8634_unlock(sx->core);

	return 0;

unlock:
	sx8634_unlock(sx->core);
unregister_bl:
	for (i = 0 ; i < sx->bl_count ; i++)
		backlight_device_unregister(sx->bl[i].dev);
	return err;
}

static int __devexit sx8634_backlight_remove(struct platform_device *pdev)
{
	struct sx8634_backlights *sx = platform_get_drvdata(pdev);
	int i;

	for (i = 0 ; i < sx->bl_count ; i++)
		backlight_device_unregister(sx->bl[i].dev);

	return 0;
}

static struct platform_driver sx8634_backlight_driver = {
	.driver = {
		.name = "sx8634-backlight",
		.owner = THIS_MODULE,
	},
	.probe = sx8634_backlight_probe,
	.remove = __devexit_p(sx8634_backlight_remove),
};
module_platform_driver(sx8634_backlight_driver);

MODULE_AUTHOR("Alban Bedel <alban.bedel@avionic-design.de>");
MODULE_DESCRIPTION("Semtech SX8634 Controller Backlight Driver");
MODULE_LICENSE("GPL");
