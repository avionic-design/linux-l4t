/*
 * arch/arm/mach-tegra/board-tec-ng.h
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

#ifndef _MACH_TEGRA_BOARD_TEC_NG_H
#define _MACH_TEGRA_BOARD_TEC_NG_H

#include "com-tamonten.h"
#include "tamonten-wm8903.h"
#include "tamonten-adnp.h"
#include "tamonten-tsc2007.h"

#define TEC_NG_GPIO_CPLD_IRQ	COM_GPIO_0
#define TEC_NG_IRQ_CPLD		COM_GPIO_TO_IRQ(TEC_NG_GPIO_CPLD_IRQ)

#define TEC_NG_GPIO_TOUCH_IRQ	BOARD_GPIO(ADNP, 7)
#define TEC_NG_IRQ_TOUCH	BOARD_GPIO_TO_IRQ(ADNP, TEC_NG_GPIO_TOUCH_IRQ)

int tec_ng_panel_init(void);

#endif
