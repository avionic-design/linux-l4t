/*
 * arch/arm/mach-tegra/board-plutux-hdmi.c
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

#define PLUTUX_GPIO_HDMI_HPD	TEGRA_GPIO_PN7

static int plutux_set_hdmi_power(bool enable)
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

static int plutux_hdmi_enable(void)
{
	return plutux_set_hdmi_power(true);
}

static int plutux_hdmi_disable(void)
{
	return plutux_set_hdmi_power(false);
}

static struct resource plutux_disp1_resources[] = {
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
	{
		.name	= "hdmi_regs",
		.start	= TEGRA_HDMI_BASE,
		.end	= TEGRA_HDMI_BASE + TEGRA_HDMI_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct tegra_fb_data plutux_fb_data = {
	.win		= 0,
	.xres		= 1280,
	.yres		= 720,
	.bits_per_pixel	= 16,
};

static struct tegra_dc_out plutux_disp1_out = {
	.type		= TEGRA_DC_OUT_HDMI,
	.flags		= TEGRA_DC_OUT_HOTPLUG_HIGH,

	.dcc_bus	= 1,
	.hotplug_gpio	= PLUTUX_GPIO_HDMI_HPD,

	.align		= TEGRA_DC_ALIGN_MSB,
	.order		= TEGRA_DC_ORDER_RED_BLUE,

	.enable		= plutux_hdmi_enable,
	.disable	= plutux_hdmi_disable,
};

static struct tegra_dc_platform_data plutux_disp1_pdata = {
	.flags = TEGRA_DC_FLAG_ENABLED,
	.default_out = &plutux_disp1_out,
	.fb = &plutux_fb_data,
};

static struct nvhost_device plutux_disp1_device = {
	.name = "tegradc",
	.id = 0,
	.resource = plutux_disp1_resources,
	.num_resources = ARRAY_SIZE(plutux_disp1_resources),
	.dev = {
		.platform_data = &plutux_disp1_pdata,
	},
};

#if defined(CONFIG_TEGRA_NVMAP)
static struct nvmap_platform_carveout plutux_carveouts[] = {
	[0] = NVMAP_HEAP_CARVEOUT_IRAM_INIT,
	[1] = {
		.name = "generic-0",
		.usage_mask = NVMAP_HEAP_CARVEOUT_GENERIC,
		.buddy_size = SZ_32K,
	},
};

static struct nvmap_platform_data plutux_nvmap_data = {
	.carveouts = plutux_carveouts,
	.nr_carveouts = ARRAY_SIZE(plutux_carveouts),
};

static struct platform_device plutux_nvmap_device = {
	.name = "tegra-nvmap",
	.id = -1,
	.dev = {
		.platform_data = &plutux_nvmap_data,
	},
};
#endif

#ifndef CONFIG_ARCH_TEGRA_2x_SOC
static struct resource tegra_cec_resources[] = {
	[0] = {
		.start = TEGRA_CEC_BASE,
		.end = TEGRA_CEC_BASE + TEGRA_CEC_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = INT_CEC,
		.end = INT_CEC,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device tegra_cec_device = {
	.name = "tegra_cec",
	.id   = -1,
	.resource = tegra_cec_resources,
	.num_resources = ARRAY_SIZE(tegra_cec_resources),
};
#endif

static struct platform_device *plutux_gfx_devices[] __initdata = {
#if defined(CONFIG_TEGRA_NVMAP)
	&plutux_nvmap_device,
#endif
#ifndef CONFIG_ARCH_TEGRA_2x_SOC
	&tegra_cec_device,
#endif
	&tegra_pwfm0_device,
};

int __init plutux_hdmi_init(void)
{
	struct resource *res;
	int err;

#if defined(CONFIG_TEGRA_NVMAP)
	plutux_carveouts[1].base = tegra_carveout_start;
	plutux_carveouts[1].size = tegra_carveout_size;
#endif

#ifdef CONFIG_TEGRA_GRHOST
	err = tegra2_register_host1x_devices();
	if (err)
		return err;
#endif

	err = platform_add_devices(plutux_gfx_devices,
				   ARRAY_SIZE(plutux_gfx_devices));
	if (err)
		return err;

	res = nvhost_get_resource_byname(&plutux_disp1_device,
					 IORESOURCE_MEM, "fbmem");
	if (res) {
		res->start = tegra_fb_start;
		res->end = tegra_fb_start + tegra_fb_size - 1;
	}

	err = nvhost_device_register(&plutux_disp1_device);
	if (err)
		return err;

	return 0;
}
