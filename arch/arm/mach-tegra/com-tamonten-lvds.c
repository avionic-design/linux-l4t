/*
 * arch/arm/mach-tegra/tamonten-backlight.c
 *
 * Copyright (C) 2013, Avionic Design GmbH
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
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/pwm_backlight.h>
#include <linux/fb.h>
#include <linux/delay.h>

#include <mach/dc.h>

#include "devices.h"
#include "com-tamonten.h"

/* panel power on sequence timing */
#define TAMONTEN_PANEL_TO_LVDS_MS		0
#define TAMONTEN_LVDS_TO_BACKLIGHT_MS		200

static struct device *tamonten_backlight_fb;

static int tamonten_backlight_initialize(struct device *dev)
{
	int ret;

	ret = gpio_request(COM_GPIO_BACKLIGHT_ENABLE,
			   "backlight enable");
	if (ret < 0)
		return ret;

	ret = gpio_direction_output(COM_GPIO_BACKLIGHT_ENABLE, 1);
	if (ret < 0)
		gpio_free(COM_GPIO_BACKLIGHT_ENABLE);

	return ret;
}

static void tamonten_backlight_exit(struct device *dev)
{
	gpio_set_value(COM_GPIO_BACKLIGHT_ENABLE, 0);
	gpio_free(COM_GPIO_BACKLIGHT_ENABLE);
}

static int tamonten_backlight_notify(struct device *dev, int brightness)
{
	gpio_set_value(COM_GPIO_LVDS_SHUTDOWN, !!brightness);
	gpio_set_value(COM_GPIO_BACKLIGHT_ENABLE, !!brightness);

	return brightness;
}

static int tamonten_backlight_check_fb(struct device *dev, struct fb_info *info)
{
	return info->device == tamonten_backlight_fb;
}

static struct platform_pwm_backlight_data tamonten_backlight_data = {
	.pwm_id = COM_PWM_BACKLIGHT,
	.max_brightness = 255,
	.dft_brightness = 224,
	.pwm_period_ns = 5000000,
	.init = tamonten_backlight_initialize,
	.exit = tamonten_backlight_exit,
	.notify = tamonten_backlight_notify,
	.check_fb = tamonten_backlight_check_fb,
};

static struct platform_device tamonten_backlight_device = {
	.name = "pwm-backlight",
	.id = -1,
	.dev = {
		.platform_data = &tamonten_backlight_data,
	},
};


static int tamonten_panel_enable(void)
{
	gpio_set_value(COM_GPIO_LVDS_SHUTDOWN, 1);
	mdelay(TAMONTEN_LVDS_TO_BACKLIGHT_MS);

	return 0;
}

static int tamonten_panel_disable(void)
{
	gpio_set_value(COM_GPIO_LVDS_SHUTDOWN, 0);

	return 0;
}

static struct tegra_dc_out tamonten_panel_disp_out = {
	.type = TEGRA_DC_OUT_RGB,

	.enable = tamonten_panel_enable,
	.disable = tamonten_panel_disable,

	.align = TEGRA_DC_ALIGN_MSB,
	.order = TEGRA_DC_ORDER_RED_BLUE,
	.depth = 18,
	.dither = TEGRA_DC_ORDERED_DITHER,

	.modes = NULL,
	.n_modes = 0,

#ifdef CONFIG_COM_TAMONTEN_NG
	.parent_clk = "pll_d_out0",
	.parent_clk_backup = "pll_d2_out0",
#endif
};

struct tegra_dc_platform_data tamonten_lvds_disp_pdata = {
	.flags = TEGRA_DC_FLAG_ENABLED,
	.default_out = &tamonten_panel_disp_out,
	.fb = NULL,
};

void __init tamonten_lvds_init(struct device *fb_device)
{
	if (gpio_request(COM_GPIO_LVDS_SHUTDOWN, "LVDS shutdown")) {
		pr_err("Failed to request LVDS shutdown gpio\n");
		return;
	}
	gpio_direction_output(COM_GPIO_LVDS_SHUTDOWN, 1);

	tamonten_backlight_fb = fb_device;
	platform_device_register(&tamonten_backlight_device);
}
