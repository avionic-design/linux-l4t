/*
 * arch/arm/mach-tegra/board-medcom-wide.h
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

#ifndef _MACH_TEGRA_BOARD_MEDCOM_WIDE_H
#define _MACH_TEGRA_BOARD_MEDCOM_WIDE_H

#include "com-tamonten.h"

#define MEDCOM_WIDE_GPIO_WM8903(_x_)	(TAMONTEN_GPIO_LAST + (_x_))

#define ADNP_GPIO(_x_)			(MEDCOM_WIDE_GPIO_WM8903(5) + (_x_))
#define ADNP_GPIO_TO_IRQ(_x_)		(INT_BOARD_BASE + 32 + ((_x_) - ADNP_GPIO(0)))
#define ADNP_IRQ(_x_)			ADNP_GPIO_TO_IRQ(ADNP_GPIO(_x_))

#define TEGRA_GPIO_SPKR_EN		MEDCOM_WIDE_GPIO_WM8903(2)
#define TEGRA_GPIO_CPLD_IRQ		TEGRA_GPIO_PU0

int medcom_wide_panel_init(void);

#endif
