/*
 * arch/arm/mach-tegra/board-plutux.h
 *
 * Copyright (C) 2010 Google, Inc.
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

#ifndef _MACH_TEGRA_BOARD_PLUTUX_H
#define _MACH_TEGRA_BOARD_PLUTUX_H

#include "com-tamonten.h"

#define PLUTUX_GPIO_WM8903(_x_)	(TAMONTEN_GPIO_LAST + (_x_))

#define TEGRA_GPIO_SPKR_EN	PLUTUX_GPIO_WM8903(2)

int plutux_hdmi_init(void);

#endif
