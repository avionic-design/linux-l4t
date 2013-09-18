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

#include <linux/kernel.h>
#include <linux/init.h>

#include <mach/dc.h>

#include "board-plutux.h"

/*
 * We add LVDS only because the l4t drivers seems to hardcode lvds as
 * primary interface
 */
static struct tegra_fb_data plutux_lvds_fb_data = {
	.win = 0,
	.xres = 1366,
	.yres = 768,
	.bits_per_pixel = 16,
	.flags = TEGRA_FB_FLIP_ON_PROBE,
};

static struct tegra_dc_out plutux_lvds_out = {
	.type		= TEGRA_DC_OUT_RGB,
};

static struct tegra_dc_platform_data plutux_lvds_pdata = {
	.flags		= 0,
	.default_out	= &plutux_lvds_out,
	.fb		= &plutux_lvds_fb_data,
};

int __init plutux_hdmi_init(void)
{
	tamonten_hdmi_init();
	return tamonten_display_init(
		&plutux_lvds_pdata, &tamonten_hdmi_disp_pdata);
}
