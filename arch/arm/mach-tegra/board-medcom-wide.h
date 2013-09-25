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
#include "tamonten-wm8903.h"

#define ADNP_GPIO(_x_)			(BOARD_GPIO_WM8903_LAST + (_x_))
#define ADNP_GPIO_TO_IRQ(_x_)		(INT_BOARD_BASE + 32 + ((_x_) - ADNP_GPIO(0)))
#define ADNP_IRQ(_x_)			ADNP_GPIO_TO_IRQ(ADNP_GPIO(_x_))

#define MEDCOM_WIDE_GPIO_CPLD_IRQ	COM_GPIO_0
#define MEDCOM_WIDE_IRQ_CPLD		COM_GPIO_TO_IRQ(MEDCOM_WIDE_GPIO_CPLD_IRQ)

int medcom_wide_panel_init(void);

#endif
