/*
 * AMS AS3722 input key driver
 *
 * Copyright (C) 2015 Avionic Design GmbH
 * Copyright (C) 2015 Julian Scheel <julian@jusst.de>
 *
 * Original code written by Ari Saastamoinen, Juha Yrjölä and Felipe Balbi.
 * Rewritten by Aaro Koskinen.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/irq.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mfd/as3722.h>
#include <linux/mfd/as3722-plat.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>

struct as3722_input {
	struct input_dev *idev;
	struct as3722 *as3722;

	int onkey_irq;
};

static irqreturn_t as3722_input_irq(int irq, void *_pwr)
{
	struct as3722_input *input = _pwr;

	if (irq == input->onkey_irq) {
		int state = 0;
		if (regmap_read(input->as3722->regmap, AS3722_RESET_CONTROL_REG, &state) < 0)
			return IRQ_HANDLED;

		input_report_key(input->idev, KEY_POWER, state & BIT(2));
	}
	input_sync(input->idev);

	return IRQ_HANDLED;
}

static int as3722_input_probe(struct platform_device *pdev)
{
	struct as3722_input *input;
	struct input_dev *idev;
	int error;

	input = devm_kzalloc(&pdev->dev, sizeof(*input),
			GFP_KERNEL);
	if (!input)
		return -ENOMEM;

	input->as3722 = dev_get_drvdata(pdev->dev.parent);

	input->onkey_irq = as3722_irq_get_virq(input->as3722,
			AS3722_IRQ_ONKEY);
	if (input->onkey_irq < 0)
		return input->onkey_irq;

	idev = devm_input_allocate_device(&pdev->dev);
	if (!idev)
		return -ENOMEM;
	input->idev = idev;

	idev->name = "as3722-input";
	idev->dev.parent = &pdev->dev;

	input_set_capability(idev, EV_KEY, KEY_POWER);
	input_set_drvdata(idev, input);

	/* Set drvdata on platform device for PM handlers */
	dev_set_drvdata(&pdev->dev, input);

	error = devm_request_threaded_irq(&pdev->dev, input->onkey_irq,
					  NULL, as3722_input_irq,
					  IRQF_ONESHOT | IRQF_EARLY_RESUME,
					  "as3722-input", input);
	if (error)
		return error;

	error = input_register_device(idev);
	if (error)
		return error;

	return 0;
}

static int as3722_input_remove(struct platform_device *pdev)
{
	struct as3722_input *input = platform_get_drvdata(pdev);

	input_unregister_device(input->idev);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int as3722_input_suspend(struct device *dev)
{
	struct as3722_input *input = dev_get_drvdata(dev);
	enable_irq_wake(input->onkey_irq);

	return 0;
}

static int as3722_input_resume(struct device *dev)
{
	struct as3722_input *input = dev_get_drvdata(dev);
	disable_irq_wake(input->onkey_irq);

	return 0;
}
#endif

#ifdef CONFIG_OF
static const struct of_device_id as3722_input_match[] = {
	{ .compatible = "ams,as3722-input", },
	{ },
};
#endif

static const struct dev_pm_ops as3722_input_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(as3722_input_suspend, as3722_input_resume)
};

static struct platform_driver as3722_input_driver = {
	.probe		= as3722_input_probe,
	.remove		= as3722_input_remove,
	.driver		= {
		.name	= "as3722-input",
		.owner	= THIS_MODULE,
		.pm	= &as3722_input_pm_ops,
		.of_match_table = of_match_ptr(as3722_input_match),
	},
};
module_platform_driver(as3722_input_driver);

MODULE_ALIAS("platform:as3722-input");
MODULE_DESCRIPTION("AS3722 Input Keys");
MODULE_AUTHOR("Julian Scheel <julian@jusst.de>");
MODULE_LICENSE("GPL");
