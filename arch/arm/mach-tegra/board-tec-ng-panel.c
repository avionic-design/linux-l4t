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

#include <linux/kernel.h>
#include <linux/init.h>

#include <mach/dc.h>

#include "devices.h"
#include "board-tec-ng.h"

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

int __init tec_ng_panel_init(void)
{
	tamonten_lvds_disp_pdata.fb = &tec_ng_lvds_fb_data;
	tamonten_lvds_disp_pdata.default_out->modes =
		tec_ng_lvds_modes;
	tamonten_lvds_disp_pdata.default_out->n_modes =
		ARRAY_SIZE(tec_ng_lvds_modes);

	tamonten_lvds_init(&tegra_disp1_device.dev);
	tamonten_hdmi_init();

	return tamonten_display_init(&tamonten_lvds_disp_pdata,
				&tamonten_hdmi_disp_pdata);
}
