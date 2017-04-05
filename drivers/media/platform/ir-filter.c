/*
 * Copyright 2017 Alban Bedel <alban.bedel@avionic-design.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 *
 * Driver to expose an IR filter controlled with 2 GPIOS as V4L2 control.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/workqueue.h>
#include <linux/of.h>

#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>

struct ir_filter {
	struct v4l2_subdev subdev;
	struct v4l2_ctrl_handler ctrl_handler;

	struct delayed_work disable_gpios_work;
	u32 disable_gpios_delay;

	struct gpio_desc *enable_gpio;
	struct gpio_desc *disable_gpio;
};

#define DEFAULT_DISABLE_GPIOS_DELAY 200

#define V4L2_CID_IR_FILTER_STATUS (V4L2_CID_USER_IR_FILTER_BASE + 0)

static void ir_filter_disable_gpios(struct work_struct *work)
{
	struct ir_filter *ir_filter = container_of(
		work, struct ir_filter, disable_gpios_work.work);

	gpiod_set_value_cansleep(ir_filter->disable_gpio, 0);
	gpiod_set_value_cansleep(ir_filter->enable_gpio, 0);
}

static int ir_filter_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ir_filter *ir_filter = container_of(
		ctrl->handler, struct ir_filter, ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_IR_FILTER_STATUS:
		/* We have to make sure that both GPIO are not enabled
		 * at the same time */
		if (ctrl->val) {
			gpiod_set_value_cansleep(ir_filter->disable_gpio, 0);
			gpiod_set_value_cansleep(ir_filter->enable_gpio, 1);
		} else {
			gpiod_set_value_cansleep(ir_filter->enable_gpio, 0);
			gpiod_set_value_cansleep(ir_filter->disable_gpio, 1);
		}
		if (ir_filter->disable_gpios_delay > 0) {
			unsigned long jiffies =
				msecs_to_jiffies(ir_filter->disable_gpios_delay);
			schedule_delayed_work(
				&ir_filter->disable_gpios_work, jiffies);
		}
		return 0;

	default:
		return -EINVAL;
	}
}

static const struct v4l2_ctrl_ops ir_filter_ctrl_ops = {
	.s_ctrl = ir_filter_s_ctrl,
};

static const struct v4l2_ctrl_config ir_filter_status_ctrl = {
	.ops = &ir_filter_ctrl_ops,
	.id = V4L2_CID_IR_FILTER_STATUS,
	.name = "IR Filter",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 0,
};

static struct v4l2_subdev_core_ops ir_filter_core_ops = {
	.queryctrl = v4l2_subdev_queryctrl,
	.querymenu = v4l2_subdev_querymenu,
	.g_ctrl = v4l2_subdev_g_ctrl,
	.s_ctrl = v4l2_subdev_s_ctrl,
	.g_ext_ctrls = v4l2_subdev_g_ext_ctrls,
	.try_ext_ctrls = v4l2_subdev_try_ext_ctrls,
	.s_ext_ctrls = v4l2_subdev_s_ext_ctrls,
};

static struct v4l2_subdev_ops ir_filter_subdev_ops = {
	.core = &ir_filter_core_ops,
};

static int ir_filter_probe(struct platform_device *pdev)
{
	struct ir_filter *ir_filter;

	ir_filter = devm_kzalloc(&pdev->dev, sizeof(*ir_filter), GFP_KERNEL);
	if (!ir_filter)
		return -ENOMEM;

	INIT_DELAYED_WORK(&ir_filter->disable_gpios_work,
			ir_filter_disable_gpios);

	ir_filter->disable_gpios_delay = DEFAULT_DISABLE_GPIOS_DELAY;
	of_property_read_u32(pdev->dev.of_node, "disable-gpios-delay",
			&ir_filter->disable_gpios_delay);

	ir_filter->enable_gpio = devm_gpiod_get(
		&pdev->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(ir_filter->enable_gpio)) {
		if (PTR_ERR(ir_filter->enable_gpio) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to get enable GPIO\n");
		return PTR_ERR(ir_filter->enable_gpio);
	}

	ir_filter->disable_gpio = devm_gpiod_get(
		&pdev->dev, "disable", GPIOD_OUT_HIGH);
	if (IS_ERR(ir_filter->disable_gpio)) {
		if (PTR_ERR(ir_filter->disable_gpio) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to get disable GPIO\n");
		return PTR_ERR(ir_filter->disable_gpio);
	}

	if (ir_filter->disable_gpios_delay > 0) {
		unsigned long jiffies =
			msecs_to_jiffies(ir_filter->disable_gpios_delay);
		schedule_delayed_work(
			&ir_filter->disable_gpios_work, jiffies);
	}

	v4l2_subdev_init(&ir_filter->subdev, &ir_filter_subdev_ops);
	ir_filter->subdev.dev = &pdev->dev;
	ir_filter->subdev.owner = pdev->dev.driver->owner;
	strlcpy(ir_filter->subdev.name, pdev->dev.driver->name,
		sizeof(ir_filter->subdev.name));

	v4l2_ctrl_handler_init(&ir_filter->ctrl_handler, 1);
	ir_filter->subdev.ctrl_handler = &ir_filter->ctrl_handler;

	v4l2_ctrl_new_custom(&ir_filter->ctrl_handler,
			&ir_filter_status_ctrl, NULL);

	if (ir_filter->ctrl_handler.error) {
		dev_err(&pdev->dev, "Control initialization error %d\n",
			ir_filter->ctrl_handler.error);
		return ir_filter->ctrl_handler.error;
	}

	return v4l2_async_register_subdev(&ir_filter->subdev);
}

static const struct of_device_id ir_filter_of_match[] = {
	{ .compatible = "ir-filter" },
	{ },
};
MODULE_DEVICE_TABLE(of, ir_filter_of_match);

static struct platform_driver ir_filter_driver = {
	.probe = ir_filter_probe,
	.driver	 = {
		.of_match_table = of_match_ptr(ir_filter_of_match),
		.name = "ir-filter",
		.owner = THIS_MODULE,
	},
};
module_platform_driver(ir_filter_driver);

MODULE_AUTHOR("Alban Bedel <alban.bedel@avionic-design.de>");
MODULE_DESCRIPTION("IR filter driver");
MODULE_LICENSE("GPL");
