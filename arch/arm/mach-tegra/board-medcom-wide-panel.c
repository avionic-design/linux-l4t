/*
 * arch/arm/mach-tegra/board-medcom-wide-panel.c
 *
 * Copyright (c) 2010-2012, NVIDIA Corporation.
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

#include <linux/delay.h>
#include <linux/resource.h>
#include <linux/platform_device.h>
#include <asm/mach-types.h>
#include <linux/nvhost.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/pwm_backlight.h>

#include <mach/dc.h>
#include <mach/irqs.h>
#include <mach/iomap.h>
#include <linux/nvmap.h>
#include <mach/tegra_fb.h>
#include <mach/fb.h>

#include "devices.h"
#include "gpio-names.h"
#include "board.h"
#include "tegra2_host1x_devices.h"

#define MEDCOM_WIDE_GPIO_LVDS_SHUTDOWN		TEGRA_GPIO_PB2
#define MEDCOM_WIDE_GPIO_BACKLIGHT_PWM		TEGRA_GPIO_PB4
#define MEDCOM_WIDE_GPIO_BACKLIGHT_ENABLE	TEGRA_GPIO_PB5
#define MEDCOM_WIDE_GPIO_PANEL_ENABLE		TEGRA_GPIO_PC6
#define MEDCOM_WIDE_GPIO_BACKLIGHT_VDD		TEGRA_GPIO_PW0

/* panel power on sequence timing */
#define MEDCOM_WIDE_PANEL_TO_LVDS_MS		0
#define MEDCOM_WIDE_LVDS_TO_BACKLIGHT_MS	200

static int medcom_wide_panel_enable(void)
{
	gpio_set_value(MEDCOM_WIDE_GPIO_PANEL_ENABLE, 1);
	mdelay(MEDCOM_WIDE_PANEL_TO_LVDS_MS);
	gpio_set_value(MEDCOM_WIDE_GPIO_LVDS_SHUTDOWN, 1);
	mdelay(MEDCOM_WIDE_LVDS_TO_BACKLIGHT_MS);

	return 0;
}

static int medcom_wide_panel_disable(void)
{
	gpio_set_value(MEDCOM_WIDE_GPIO_LVDS_SHUTDOWN, 0);
	gpio_set_value(MEDCOM_WIDE_GPIO_PANEL_ENABLE, 0);

	return 0;
}

static struct resource medcom_wide_disp1_resources[] = {
	{
		.name = "irq",
		.start = INT_DISPLAY_GENERAL,
		.end = INT_DISPLAY_GENERAL,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "regs",
		.start = TEGRA_DISPLAY_BASE,
		.end = TEGRA_DISPLAY_BASE + TEGRA_DISPLAY_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "fbmem",
		.flags = IORESOURCE_MEM,
	},
};

static struct tegra_dc_mode medcom_wide_panel_modes[] = {
	{
		.pclk = 61715000,
		.h_ref_to_sync = 4,
		.v_ref_to_sync = 2,
		.h_sync_width = 136,
		.v_sync_width = 4,
		.h_back_porch = 2,
		.v_back_porch = 21,
		.h_active = 1366,
		.v_active = 768,
		.h_front_porch = 36,
		.v_front_porch = 10,
	},
};

static struct tegra_fb_data medcom_wide_fb_data = {
	.win = 0,
	.xres = 1366,
	.yres = 768,
	.bits_per_pixel = 16,
	.flags = TEGRA_FB_FLIP_ON_PROBE,
};

static struct tegra_dc_out medcom_wide_disp1_out = {
	.type = TEGRA_DC_OUT_RGB,

	.align = TEGRA_DC_ALIGN_MSB,
	.order = TEGRA_DC_ORDER_RED_BLUE,
	.depth = 18,
	.dither = TEGRA_DC_ORDERED_DITHER,

	.modes = medcom_wide_panel_modes,
	.n_modes = ARRAY_SIZE(medcom_wide_panel_modes),

	.enable = medcom_wide_panel_enable,
	.disable = medcom_wide_panel_disable,
};

static struct tegra_dc_platform_data medcom_wide_disp1_pdata = {
	.flags = TEGRA_DC_FLAG_ENABLED,
	.default_out = &medcom_wide_disp1_out,
	.fb = &medcom_wide_fb_data,
};

static struct nvhost_device medcom_wide_disp1_device = {
	.name = "tegradc",
	.id = 0,
	.resource = medcom_wide_disp1_resources,
	.num_resources = ARRAY_SIZE(medcom_wide_disp1_resources),
	.dev = {
		.platform_data = &medcom_wide_disp1_pdata,
	},
};

static int medcom_wide_backlight_init(struct device *dev)
{
	int gpio = MEDCOM_WIDE_GPIO_BACKLIGHT_ENABLE;
	int ret;

	ret = gpio_request(gpio, "backlight enable");
	if (ret < 0)
		return ret;

	ret = gpio_direction_output(gpio, 1);
	if (ret < 0)
		gpio_free(gpio);

	return ret;
}

static void medcom_wide_backlight_exit(struct device *dev)
{
	int gpio = MEDCOM_WIDE_GPIO_BACKLIGHT_ENABLE;

	gpio_set_value(gpio, 0);
	gpio_free(gpio);
}

static int medcom_wide_backlight_notify(struct device *dev, int brightness)
{
	gpio_set_value(MEDCOM_WIDE_GPIO_PANEL_ENABLE, !!brightness);
	gpio_set_value(MEDCOM_WIDE_GPIO_LVDS_SHUTDOWN, !!brightness);
	gpio_set_value(MEDCOM_WIDE_GPIO_BACKLIGHT_ENABLE, !!brightness);

	return brightness;
}

static int medcom_wide_disp1_check_fb(struct device *dev, struct fb_info *info)
{
	return info->device == &medcom_wide_disp1_device.dev;
}

static struct platform_pwm_backlight_data medcom_wide_backlight_data = {
	.pwm_id = 0,
	.max_brightness = 255,
	.dft_brightness = 224,
	.pwm_period_ns = 5000000,
	.init = medcom_wide_backlight_init,
	.exit = medcom_wide_backlight_exit,
	.notify = medcom_wide_backlight_notify,
	/* Only toggle backlight on fb blank notifications for disp1 */
	.check_fb = medcom_wide_disp1_check_fb,
};

static struct platform_device medcom_wide_backlight_device = {
	.name = "pwm-backlight",
	.id = -1,
	.dev = {
		.platform_data = &medcom_wide_backlight_data,
	},
};

#if defined(CONFIG_TEGRA_NVMAP)
static struct nvmap_platform_carveout medcom_wide_carveouts[] = {
	[0] = NVMAP_HEAP_CARVEOUT_IRAM_INIT,
	[1] = {
		.name = "generic-0",
		.usage_mask = NVMAP_HEAP_CARVEOUT_GENERIC,
		.buddy_size = SZ_32K,
	},
};

static struct nvmap_platform_data medcom_wide_nvmap_data = {
	.carveouts = medcom_wide_carveouts,
	.nr_carveouts = ARRAY_SIZE(medcom_wide_carveouts),
};

static struct platform_device medcom_wide_nvmap_device = {
	.name = "tegra-nvmap",
	.id = -1,
	.dev = {
		.platform_data = &medcom_wide_nvmap_data,
	},
};
#endif

static struct platform_device *medcom_wide_gfx_devices[] __initdata = {
#if defined(CONFIG_TEGRA_NVMAP)
	&medcom_wide_nvmap_device,
#endif
	&tegra_pwfm0_device,
	&medcom_wide_backlight_device,
};

int __init medcom_wide_panel_init(void)
{
	struct resource *res;
	int err;

	gpio_request(MEDCOM_WIDE_GPIO_PANEL_ENABLE, "panel enable");
	gpio_direction_output(MEDCOM_WIDE_GPIO_PANEL_ENABLE, 1);

	gpio_request(MEDCOM_WIDE_GPIO_BACKLIGHT_VDD, "backlight VDD");
	gpio_direction_output(MEDCOM_WIDE_GPIO_BACKLIGHT_VDD, 1);

	gpio_request(MEDCOM_WIDE_GPIO_LVDS_SHUTDOWN, "LVDS shutdown");
	gpio_direction_output(MEDCOM_WIDE_GPIO_LVDS_SHUTDOWN, 1);

#if defined(CONFIG_TEGRA_NVMAP)
	medcom_wide_carveouts[1].base = tegra_carveout_start;
	medcom_wide_carveouts[1].size = tegra_carveout_size;
#endif

#ifdef CONFIG_TEGRA_GRHOST
	err = tegra2_register_host1x_devices();
	if (err)
		return err;
#endif

	err = platform_add_devices(medcom_wide_gfx_devices,
				   ARRAY_SIZE(medcom_wide_gfx_devices));
	if (err)
		return err;

	res = nvhost_get_resource_byname(&medcom_wide_disp1_device,
					 IORESOURCE_MEM, "fbmem");
	if (res) {
		res->start = tegra_fb_start;
		res->end = tegra_fb_start + tegra_fb_size - 1;
	}

	err = nvhost_device_register(&medcom_wide_disp1_device);
	if (err)
		return err;

	return 0;
}
