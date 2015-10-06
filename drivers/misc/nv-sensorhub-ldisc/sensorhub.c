/*
 * Copyright (C) 2015 Alban Bedel <alban.bedel@avionic-design.de>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>

static int sensorhub_probe(struct platform_device *pdev)
{
	struct gpio_desc *reset, *boot0;

	/* Request the GPIO with reset enabled */
	reset = devm_gpiod_get(&pdev->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(reset))
		return PTR_ERR(reset);

	boot0 = devm_gpiod_get(&pdev->dev, "boot0", GPIOD_OUT_LOW);
	if (IS_ERR(boot0))
		return PTR_ERR(boot0);

	/* Wait for MCU to finish reset */
	msleep(1000);

	/* And take the reset out */
	gpiod_set_value(reset, 0);

	if (gpiod_export(reset, false))
		dev_warn(&pdev->dev, "Failed to export reset GPIO\n");

	if (gpiod_export(boot0, false))
		dev_warn(&pdev->dev, "Failed to export boot0 GPIO\n");

	return 0;
}

static const struct of_device_id sensorhub_of_match[] = {
	{ .compatible = "parrot,sensorhub", },
	{},
};
MODULE_DEVICE_TABLE(of, sensorhub_of_match);

static struct platform_driver sensorhub_driver = {
	.driver = {
		.name = "sensorhub",
		.owner = THIS_MODULE,
		.of_match_table = sensorhub_of_match,
	},
	.probe = sensorhub_probe,
};

module_platform_driver(sensorhub_driver);

MODULE_AUTHOR("Alban Bedel <alban.bedel@avionic-design.de>");
MODULE_DESCRIPTION("Sensorhub driver");
MODULE_LICENSE("GPL v2");
