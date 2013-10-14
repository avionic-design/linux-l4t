/*
 * arch/arm/mach-tegra/com-tamonten-display.h
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

#ifndef _MACH_TEGRA_COM_TAMONTEN_DISPLAY_H
#define _MACH_TEGRA_COM_TAMONTEN_DISPLAY_H

void tamonten_hdmi_init(void);
extern struct tegra_dc_platform_data tamonten_hdmi_disp_pdata;

void tamonten_lvds_init(struct device *fb_device);
extern struct tegra_dc_platform_data tamonten_lvds_disp_pdata;

int tamonten_display_init(struct tegra_dc_platform_data *disp1_pdata,
			  struct tegra_dc_platform_data *disp2_pdata);

#endif
