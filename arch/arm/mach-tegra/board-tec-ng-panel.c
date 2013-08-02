/*
 * arch/arm/mach-tegra/board-tec-ng-panel.c
 *
 * Copyright (c) 2013, Avionic Design GmbH
 * Copyright (c) 2013, Julian Scheel <julian@jusst.de>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <asm/mach-types.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/pwm_backlight.h>
#include <linux/resource.h>
#include <linux/nvhost.h>
#include <linux/nvmap.h>

#include <mach/irqs.h>
#include <mach/iomap.h>
#include <mach/dc.h>
#include <mach/fb.h>

#include "board.h"
#include "board-tec-ng.h"
#include "devices.h"
#include "gpio-names.h"
#include "tegra3_host1x_devices.h"

#define TEC_NG_HDMI_HPD TEGRA_GPIO_PN7

#define TEC_NG_LVDS_SHUTDOWN TEGRA_GPIO_PB2
#define TEC_NG_BL_EN TEGRA_GPIO_PH2
#define TEC_NG_BL_PWM TEGRA_GPIO_PH0

#ifdef CONFIG_TEGRA_DC
static struct regulator *tec_ng_hdmi_avdd = NULL;
static struct regulator *tec_ng_hdmi_pll = NULL;
#endif

static int tec_ng_gpio_request(unsigned int gpio, const char *label,
		unsigned int direction_out, int value)
{
	int ret = 0;

	ret = gpio_request(gpio, label);
	if (ret < 0) {
		pr_err("%s(): Request gpio %s failed: %d\n", __func__, label,
				ret);
		return ret;
	}

	if (direction_out)
		ret = gpio_direction_output(gpio, value);
	else
		ret = gpio_direction_input(gpio);

	if (ret < 0) {
		pr_err("%s(): Failed to set gpio %s direction: %d\n", __func__,
				label, ret);
		gpio_free(gpio);
		return ret;
	}

	return 0;
}

#ifdef CONFIG_TEGRA_DC
static int tec_ng_enable_regulator(struct regulator **regulator,
		const char *name)
{
	struct regulator *reg = *regulator;
	int ret;

	if (reg != NULL)
		goto enable;

	reg = regulator_get(NULL, name);
	if (IS_ERR_OR_NULL(reg)) {
		ret = PTR_ERR(reg);
		pr_err("%s(): could not get regulator %s.\n", __func__, name);
		reg = NULL;
		goto out;
	}

enable:
	ret = regulator_enable(reg);
	if (ret < 0) {
		pr_err("%s(): could not enable regulator %s.\n", __func__,
				name);
		regulator_put(reg);
		reg = NULL;
	}

out:
	*regulator = reg;
	return ret;
}

static int tec_ng_disable_regulator(struct regulator **regulator)
{
	if (*regulator == NULL)
		return -EINVAL;

	regulator_disable(*regulator);
	regulator_put(*regulator);
	*regulator = NULL;

	return 0;
}

static int tec_ng_hdmi_enable(void)
{
	int ret;

	ret = tec_ng_enable_regulator(&tec_ng_hdmi_avdd, "avdd_hdmi");
	if (ret < 0)
		return ret;

	ret = tec_ng_enable_regulator(&tec_ng_hdmi_pll, "avdd_hdmi_pll");
	if (ret < 0)
		return ret;

	return 0;
}

static int tec_ng_hdmi_disable(void)
{
	tec_ng_disable_regulator(&tec_ng_hdmi_avdd);
	tec_ng_disable_regulator(&tec_ng_hdmi_pll);

	return 0;
}

static struct tegra_fb_data tec_ng_hdmi_fb_data = {
	.win = 0,
	.xres = 1280,
	.yres = 720,
#ifdef CONFIG_TEGRA_DC_USE_HW_BPP
	.bits_per_pixel = -1,
#else
	.bits_per_pixel = 32,
#endif
	.flags = TEGRA_FB_FLIP_ON_PROBE,
};

static struct tegra_dc_out tec_ng_hdmi_out = {
	.type = TEGRA_DC_OUT_HDMI,
	.flags = TEGRA_DC_OUT_HOTPLUG_HIGH,
	.parent_clk = "pll_d2_out0",

	.dcc_bus = 3,
	.hotplug_gpio = TEC_NG_HDMI_HPD,

	.max_pixclock = KHZ2PICOS(148500),

	.align = TEGRA_DC_ALIGN_MSB,
	.order = TEGRA_DC_ORDER_RED_BLUE,

	.enable = tec_ng_hdmi_enable,
	.disable = tec_ng_hdmi_disable,
};

static struct tegra_dc_platform_data tec_ng_hdmi_pdata = {
	.flags = TEGRA_DC_FLAG_ENABLED,
	.default_out = &tec_ng_hdmi_out,
	.fb = &tec_ng_hdmi_fb_data,
	.emc_clk_rate = 300000000,
};

static int tec_ng_lvds_enable(void)
{
	gpio_set_value(TEC_NG_LVDS_SHUTDOWN, 1);

	return 0;
}

static int tec_ng_lvds_disable(void)
{
	gpio_set_value(TEC_NG_LVDS_SHUTDOWN, 0);

	return 0;
}

static int tec_ng_backlight_init(struct device *dev)
{
	int ret = 0;

	ret = tec_ng_gpio_request(TEC_NG_BL_EN, "backlight_enb", 1, 1);
	if (ret < 0)
		return ret;

	ret = tec_ng_gpio_request(TEC_NG_BL_PWM, "backlight_pwm", 1, 1);
	if (ret < 0) {
		gpio_free(TEC_NG_BL_EN);
		return ret;
	}

	ret = tec_ng_gpio_request(TEC_NG_LVDS_SHUTDOWN, "lvds_shutdown", 1, 1);
	if (ret < 0) {
		gpio_free(TEC_NG_BL_EN);
		gpio_free(TEC_NG_BL_PWM);
		return ret;
	}

	return 0;
}

static void tec_ng_backlight_exit(struct device *dev)
{
	gpio_set_value(TEC_NG_BL_EN, 0);
	gpio_free(TEC_NG_BL_EN);
	gpio_set_value(TEC_NG_LVDS_SHUTDOWN, 0);
	gpio_free(TEC_NG_LVDS_SHUTDOWN);
}

static int tec_ng_backlight_notify(struct device *dev, int brightness)
{
	gpio_set_value(TEC_NG_BL_EN, brightness > 0 ? 1 : 0);

	return brightness;
}

static int tec_ng_lvds_check_fb(struct device *dev, struct fb_info *info)
{
	return info->device == &tegra_disp1_device.dev;
}

static struct platform_pwm_backlight_data tec_ng_backlight_data = {
	.pwm_id = 0,
	.max_brightness = 255,
	.dft_brightness = 224,
	.pwm_period_ns = 1000000,
	.init = tec_ng_backlight_init,
	.exit = tec_ng_backlight_exit,
	.notify = tec_ng_backlight_notify,
	.check_fb = tec_ng_lvds_check_fb,
};

static struct platform_device tec_ng_backlight_device = {
	.name = "pwm-backlight",
	.id = -1,
	.dev = {
		.platform_data = &tec_ng_backlight_data,
	},
};

static struct tegra_dc_mode tec_ng_lvds_modes[] = {
	{
		.pclk = 33260000,
		.h_ref_to_sync = 0,
		.v_ref_to_sync = 0,
		.h_sync_width = 16,
		.v_sync_width = 15,
		.h_back_porch = 120,
		.v_back_porch = 15,
		.h_active = 800,
		.v_active = 480,
		.h_front_porch = 120,
		.v_front_porch = 15,
	},
};

static struct tegra_fb_data tec_ng_lvds_fb_data = {
	.win = 0,
	.xres = 800,
	.yres = 480,
	.bits_per_pixel = 32,
	.flags = TEGRA_FB_FLIP_ON_PROBE,
};

static struct tegra_dc_out tec_ng_lvds_out = {
	.align = TEGRA_DC_ALIGN_MSB,
	.order = TEGRA_DC_ORDER_RED_BLUE,
	.parent_clk = "pll_d_out0",
	.parent_clk_backup = "pll_d2_out0",
	.type = TEGRA_DC_OUT_RGB,
	.depth = 18,
	.dither = TEGRA_DC_ORDERED_DITHER,
	.modes = tec_ng_lvds_modes,
	.n_modes = ARRAY_SIZE(tec_ng_lvds_modes),
	.enable = tec_ng_lvds_enable,
	.disable = tec_ng_lvds_disable,
};

static struct tegra_dc_platform_data tec_ng_lvds_pdata = {
	.flags = TEGRA_DC_FLAG_ENABLED,
	.default_out = &tec_ng_lvds_out,
	.emc_clk_rate = 300000000,
	.fb = &tec_ng_lvds_fb_data,
};
#endif

#ifdef CONFIG_TEGRA_NVMAP
static struct nvmap_platform_carveout tec_ng_carveouts[] = {
	NVMAP_HEAP_CARVEOUT_IRAM_INIT,
	{
		.name = "generic-0",
		.usage_mask = NVMAP_HEAP_CARVEOUT_GENERIC,
		.base = 0,
		.size = 0,
		.buddy_size = SZ_32K,
	},
};

static struct nvmap_platform_data tec_ng_nvmap_data = {
	.carveouts = tec_ng_carveouts,
	.nr_carveouts = ARRAY_SIZE(tec_ng_carveouts),
};

static struct platform_device tec_ng_nvmap_device = {
	.name = "tegra-nvmap",
	.id = -1,
	.dev = {
		.platform_data = &tec_ng_nvmap_data,
	},
};
#endif

static struct platform_device *tec_ng_backlight_devices[] __initdata = {
	&tegra_pwfm0_device,
	&tec_ng_backlight_device,
};

int __init tec_ng_panel_init(void)
{
	struct resource *res;
	int ret;

#ifdef CONFIG_TEGRA_NVMAP
	tec_ng_carveouts[1].base = tegra_carveout_start;
	tec_ng_carveouts[1].size = tegra_carveout_size;
#endif

#ifdef CONFIG_TEGRA_GRHOST
	ret = tegra3_register_host1x_devices();
	if (ret) {
		pr_err("%s(): Failed to register host1x devices.\n", __func__);
		return ret;
	}
#endif

	tec_ng_gpio_request(TEC_NG_HDMI_HPD, "hdmi_hpd", 0, 0);

#ifdef CONFIG_TEGRA_NVMAP
	platform_device_register(&tec_ng_nvmap_device);
#endif
	platform_add_devices(tec_ng_backlight_devices,
			ARRAY_SIZE(tec_ng_backlight_devices));

#if defined(CONFIG_TEGRA_GRHOST) && defined(CONFIG_TEGRA_DC)
	tegra_disp1_device.dev.platform_data = &tec_ng_lvds_pdata;
	res = nvhost_get_resource_byname(&tegra_disp1_device, IORESOURCE_MEM,
			"fbmem");
	if (res == NULL) {
		pr_err("%s(): Failed to retrieve framebuffer memory\n",
				__func__);
		return ret;

	}
	res->start = tegra_fb_start;
	res->end = tegra_fb_start + tegra_fb_size - 1;

	if (tegra_bootloader_fb_size)
		tegra_move_framebuffer(tegra_fb_start,
				tegra_bootloader_fb_start,
				min(tegra_fb_size, tegra_bootloader_fb_size));
	else
		tegra_clear_framebuffer(tegra_fb_start, tegra_fb_size);

	ret = nvhost_device_register(&tegra_disp1_device);
	if (ret) {
		pr_err("%s(): Failed to register display device\n", __func__);
		return ret;
	}

	tegra_disp2_device.dev.platform_data = &tec_ng_hdmi_pdata;
	res = nvhost_get_resource_byname(&tegra_disp2_device, IORESOURCE_MEM,
			"fbmem");
	if (res == NULL) {
		pr_err("%s(): Failed to retrieve framebuffer memory\n",
				__func__);
		return ret;

	}
	res->start = tegra_fb2_start;
	res->end = tegra_fb2_start + tegra_fb2_size - 1;

	if (tegra_bootloader_fb2_size)
		tegra_move_framebuffer(tegra_fb2_start,
				tegra_bootloader_fb2_start,
				min(tegra_fb2_size, tegra_bootloader_fb2_size));
	else
		tegra_clear_framebuffer(tegra_fb2_start, tegra_fb2_size);

	ret = nvhost_device_register(&tegra_disp2_device);
	if (ret) {
		pr_err("%s(): Failed to register display device\n", __func__);
		return ret;
	}
#endif

	return 0;
}
