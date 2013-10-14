/*
 * arch/arm/mach-tegra/com-tamonten-display.c
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

#include <mach/iomap.h>
#include <linux/nvmap.h>

#include "devices.h"
#include "board.h"
#include "com-tamonten.h"
#if defined(CONFIG_ARCH_TEGRA_2x_SOC)
#include "tegra2_host1x_devices.h"
#elif defined(CONFIG_ARCH_TEGRA_3x_SOC)
#include "tegra3_host1x_devices.h"
#endif

#if defined(CONFIG_TEGRA_NVMAP)
static struct nvmap_platform_carveout tamonten_carveouts[] = {
	[0] = NVMAP_HEAP_CARVEOUT_IRAM_INIT,
	[1] = {
		.name = "generic-0",
		.usage_mask = NVMAP_HEAP_CARVEOUT_GENERIC,
		.buddy_size = SZ_32K,
	},
};

static struct nvmap_platform_data tamonten_nvmap_data = {
	.carveouts = tamonten_carveouts,
	.nr_carveouts = ARRAY_SIZE(tamonten_carveouts),
};
#endif

static struct platform_device *tamonten_gfx_devices[] __initdata = {
#if defined(CONFIG_TEGRA_NVMAP)
	&tegra_nvmap_device,
#endif
#ifndef CONFIG_ARCH_TEGRA_2x_SOC
	&tegra_cec_device,
#endif
	&tegra_pwfm0_device,
};

int __init tamonten_display_init(struct tegra_dc_platform_data *disp1_pdata,
				 struct tegra_dc_platform_data *disp2_pdata)
{
	struct resource *res;
	int err;

	tegra_disp1_device.dev.platform_data = disp1_pdata;
	tegra_disp2_device.dev.platform_data = disp2_pdata;

#if defined(CONFIG_TEGRA_NVMAP)
	tamonten_carveouts[1].base = tegra_carveout_start;
	tamonten_carveouts[1].size = tegra_carveout_size;
	tegra_nvmap_device.dev.platform_data = &tamonten_nvmap_data;
#endif

#ifdef CONFIG_TEGRA_GRHOST
#if defined(CONFIG_ARCH_TEGRA_2x_SOC)
	err = tegra2_register_host1x_devices();
	if (err)
		return err;
#elif defined(CONFIG_ARCH_TEGRA_3x_SOC)
	err = tegra3_register_host1x_devices();
	if (err)
		return err;
#endif
#endif

	err = platform_add_devices(tamonten_gfx_devices,
				   ARRAY_SIZE(tamonten_gfx_devices));
	if (err)
		return err;

	res = nvhost_get_resource_byname(&tegra_disp1_device,
					 IORESOURCE_MEM, "fbmem");
	if (res) {
		res->start = tegra_fb_start;
		res->end = tegra_fb_start + tegra_fb_size - 1;
	}

	res = nvhost_get_resource_byname(&tegra_disp2_device,
					 IORESOURCE_MEM, "fbmem");
	if (res) {
		res->start = tegra_fb2_start;
		res->end = tegra_fb2_start + tegra_fb2_size - 1;
	}

	/* Copy the bootloader fb to the fb. */
	if (tegra_bootloader_fb_start) {
		tegra_move_framebuffer(tegra_fb_start,
			tegra_bootloader_fb_start,
			min(tegra_fb_size, tegra_bootloader_fb_size));
	}

	err = nvhost_device_register(&tegra_disp1_device);
	if (err)
		return err;

	err = nvhost_device_register(&tegra_disp2_device);
	if (err)
		return err;

	return 0;
}
