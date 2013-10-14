/*
 * arch/arm/mach-tegra/com-tamonten-hdmi.h
 *
 * Copyright (C) 2013 Avionic Design GmbH
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>

#include <mach/dc.h>
#include <mach/tegra_fb.h>
#include <mach/fb.h>

#include "devices.h"
#include "board.h"
#include "com-tamonten.h"

static int tamonten_set_hdmi_power(bool enable)
{
	static struct {
		struct regulator *regulator;
		const char *name;
	} regs[] = {
		{ .name = "avdd_hdmi" },
		{ .name = "avdd_hdmi_pll" },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		if (!regs[i].regulator) {
			regs[i].regulator = regulator_get(NULL, regs[i].name);

			if (IS_ERR(regs[i].regulator)) {
				int ret = PTR_ERR(regs[i].regulator);
				regs[i].regulator = NULL;
				return ret;
			}
		}

		if (enable)
			regulator_enable(regs[i].regulator);
		else
			regulator_disable(regs[i].regulator);
	}

	return 0;
}

static int tamonten_hdmi_enable(void)
{
	return tamonten_set_hdmi_power(true);
}

static int tamonten_hdmi_disable(void)
{
	return tamonten_set_hdmi_power(false);
}

static struct tegra_fb_data tamonten_hdmi_fb_data = {
	.win		= 0,
	.xres		= 1280,
	.yres		= 720,
	.bits_per_pixel	= 16,
};

static struct tegra_dc_out tamonten_hdmi_disp_out = {
	.type		= TEGRA_DC_OUT_HDMI,
	.flags		= TEGRA_DC_OUT_HOTPLUG_HIGH,
#ifdef CONFIG_COM_TAMONTEN_NG
	.parent_clk	= "pll_d2_out0",
#endif

	.dcc_bus	= COM_I2C_BUS_DDC,
	.hotplug_gpio	= COM_GPIO_HDMI_HPD,

	.align		= TEGRA_DC_ALIGN_MSB,
	.order		= TEGRA_DC_ORDER_RED_BLUE,

	.enable		= tamonten_hdmi_enable,
	.disable	= tamonten_hdmi_disable,
};

struct tegra_dc_platform_data tamonten_hdmi_disp_pdata = {
	.flags		= TEGRA_DC_FLAG_ENABLED,
	.default_out	= &tamonten_hdmi_disp_out,
	.fb		= &tamonten_hdmi_fb_data,
};

void __init tamonten_hdmi_init(void)
{
	gpio_request(COM_GPIO_HDMI_HPD, "hdmi_hpd");
	gpio_direction_input(COM_GPIO_HDMI_HPD);
	tegra_gpio_enable(COM_GPIO_HDMI_HPD);
}
