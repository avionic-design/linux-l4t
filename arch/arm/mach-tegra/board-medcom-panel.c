/*
 * arch/arm/mach-tegra/board-medcom-panel.c
 *
 * Copyright (c) 2010-2012, NVIDIA Corporation.
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
#include <mach/nvmap.h>
#include <mach/tegra_fb.h>
#include <mach/fb.h>

#include "devices.h"
#include "gpio-names.h"
#include "board.h"

#define medcom_bl_enb		TEGRA_GPIO_PB5
#define medcom_lvds_shutdown	TEGRA_GPIO_PB2
#define medcom_en_vdd_pnl	TEGRA_GPIO_PC6
#define medcom_bl_vdd		TEGRA_GPIO_PW0
#define medcom_bl_pwm		TEGRA_GPIO_PB4

/* panel power on sequence timing */
#define medcom_pnl_to_lvds_ms	0
#define medcom_lvds_to_bl_ms	200

static int medcom_backlight_init(struct device *dev)
{
	int ret;

	ret = gpio_request(medcom_bl_enb, "backlight_enb");
	if (ret < 0)
		return ret;

	ret = gpio_direction_output(medcom_bl_enb, 1);
	if (ret < 0)
		gpio_free(medcom_bl_enb);
	else
		tegra_gpio_enable(medcom_bl_enb);

	return ret;
}

static void medcom_backlight_exit(struct device *dev)
{
	gpio_set_value(medcom_bl_enb, 0);
	gpio_free(medcom_bl_enb);
	tegra_gpio_disable(medcom_bl_enb);
}

static int medcom_backlight_notify(struct device *unused, int brightness)
{
	gpio_set_value(medcom_en_vdd_pnl, !!brightness);
	gpio_set_value(medcom_lvds_shutdown, !!brightness);
	gpio_set_value(medcom_bl_enb, !!brightness);
	return brightness;
}

static int medcom_disp1_check_fb(struct device *dev, struct fb_info *info);

static struct platform_pwm_backlight_data medcom_backlight_data = {
	.pwm_id		= 0,
	.max_brightness	= 255,
	.dft_brightness	= 224,
	.pwm_period_ns	= 5000000,
	.init		= medcom_backlight_init,
	.exit		= medcom_backlight_exit,
	.notify		= medcom_backlight_notify,
	/* Only toggle backlight on fb blank notifications for disp1 */
	.check_fb	= medcom_disp1_check_fb,
};

static struct platform_device medcom_backlight_device = {
	.name	= "pwm-backlight",
	.id	= -1,
	.dev	= {
		.platform_data = &medcom_backlight_data,
	},
};

static int medcom_panel_enable(void)
{
	gpio_set_value(medcom_en_vdd_pnl, 1);
	mdelay(medcom_pnl_to_lvds_ms);
	gpio_set_value(medcom_lvds_shutdown, 1);
	mdelay(medcom_lvds_to_bl_ms);
	return 0;
}

static int medcom_panel_disable(void)
{
	gpio_set_value(medcom_lvds_shutdown, 0);
	gpio_set_value(medcom_en_vdd_pnl, 0);
	return 0;
}

static struct resource medcom_disp1_resources[] = {
	{
		.name = "irq",
		.start  = INT_DISPLAY_GENERAL,
		.end    = INT_DISPLAY_GENERAL,
		.flags  = IORESOURCE_IRQ,
	},
	{
		.name = "regs",
		.start	= TEGRA_DISPLAY_BASE,
		.end	= TEGRA_DISPLAY_BASE + TEGRA_DISPLAY_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name = "fbmem",
		.flags	= IORESOURCE_MEM,
	},
};

static struct tegra_dc_mode medcom_panel_modes[] = {
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

static struct tegra_fb_data medcom_fb_data = {
	.win		= 0,
	.xres		= 1366,
	.yres		= 768,
	.bits_per_pixel	= 16,
	.flags		= TEGRA_FB_FLIP_ON_PROBE,
};

static struct tegra_dc_out medcom_disp1_out = {
	.type		= TEGRA_DC_OUT_RGB,

	.align		= TEGRA_DC_ALIGN_MSB,
	.order		= TEGRA_DC_ORDER_RED_BLUE,
	.depth		= 18,
	.dither		= TEGRA_DC_ORDERED_DITHER,

	.modes		= medcom_panel_modes,
	.n_modes	= ARRAY_SIZE(medcom_panel_modes),

	.enable		= medcom_panel_enable,
	.disable	= medcom_panel_disable,
};

static struct tegra_dc_platform_data medcom_disp1_pdata = {
	.flags		= TEGRA_DC_FLAG_ENABLED,
	.default_out	= &medcom_disp1_out,
	.fb		= &medcom_fb_data,
};

static struct nvhost_device medcom_disp1_device = {
	.name		= "tegradc",
	.id		= 0,
	.resource	= medcom_disp1_resources,
	.num_resources	= ARRAY_SIZE(medcom_disp1_resources),
	.dev = {
		.platform_data = &medcom_disp1_pdata,
	},
};

static int medcom_disp1_check_fb(struct device *dev, struct fb_info *info)
{
	return info->device == &medcom_disp1_device.dev;
}

#if defined(CONFIG_TEGRA_NVMAP)
static struct nvmap_platform_carveout medcom_carveouts[] = {
	[0] = NVMAP_HEAP_CARVEOUT_IRAM_INIT,
	[1] = {
		.name		= "generic-0",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_GENERIC,
		.buddy_size	= SZ_32K,
	},
};

static struct nvmap_platform_data medcom_nvmap_data = {
	.carveouts	= medcom_carveouts,
	.nr_carveouts	= ARRAY_SIZE(medcom_carveouts),
};

static struct platform_device medcom_nvmap_device = {
	.name	= "tegra-nvmap",
	.id	= -1,
	.dev	= {
		.platform_data = &medcom_nvmap_data,
	},
};
#endif

static struct platform_device *medcom_gfx_devices[] __initdata = {
#if defined(CONFIG_TEGRA_NVMAP)
	&medcom_nvmap_device,
#endif
	&tegra_pwfm0_device,
	&medcom_backlight_device,
};

int __init medcom_panel_init(void)
{
	int err;
	struct resource *res;

	gpio_request(medcom_en_vdd_pnl, "en_vdd_pnl");
	gpio_direction_output(medcom_en_vdd_pnl, 1);
	tegra_gpio_enable(medcom_en_vdd_pnl);

	gpio_request(medcom_bl_vdd, "bl_vdd");
	gpio_direction_output(medcom_bl_vdd, 1);
	tegra_gpio_enable(medcom_bl_vdd);

	gpio_request(medcom_lvds_shutdown, "lvds_shdn");
	gpio_direction_output(medcom_lvds_shutdown, 1);
	tegra_gpio_enable(medcom_lvds_shutdown);

#if defined(CONFIG_TEGRA_NVMAP)
	medcom_carveouts[1].base = tegra_carveout_start;
	medcom_carveouts[1].size = tegra_carveout_size;
#endif

#ifdef CONFIG_TEGRA_GRHOST
	err = nvhost_device_register(&tegra_grhost_device);
	if (err)
		return err;
#endif

	err = platform_add_devices(medcom_gfx_devices,
				   ARRAY_SIZE(medcom_gfx_devices));
	if (err)
		return err;

	res = nvhost_get_resource_byname(&medcom_disp1_device,
		IORESOURCE_MEM, "fbmem");
	if (res) {
		res->start = tegra_fb_start;
		res->end = tegra_fb_start + tegra_fb_size - 1;
	}

	/* Copy the bootloader fb to the fb. */
	if (tegra_bootloader_fb_start)
		tegra_move_framebuffer(tegra_fb_start,
			tegra_bootloader_fb_start,
			min(tegra_fb_size, tegra_bootloader_fb_size));
	err = nvhost_device_register(&medcom_disp1_device);
	if (err)
		return err;

	return 0;
}

