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

#include <linux/kernel.h>
#include <linux/init.h>

#include <mach/dc.h>

#include "devices.h"
#include "board-medcom-wide.h"

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

int __init medcom_wide_panel_init(void)
{
	tamonten_lvds_disp_pdata.fb = &medcom_wide_fb_data;
	tamonten_lvds_disp_pdata.default_out->modes =
		medcom_wide_panel_modes;
	tamonten_lvds_disp_pdata.default_out->n_modes =
		ARRAY_SIZE(medcom_wide_panel_modes);

	tamonten_lvds_init(&tegra_disp1_device.dev);
	return tamonten_display_init(&tamonten_lvds_disp_pdata, NULL);
}
