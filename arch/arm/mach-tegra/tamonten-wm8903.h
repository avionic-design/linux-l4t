/*
 * arch/arm/mach-tegra/tamonten-wm8903.h
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

#ifndef _MACH_TEGRA_TAMONTEN_WM8903_H
#define _MACH_TEGRA_TAMONTEN_WM8903_H

#define BOARD_GPIO_WM8903(_x_)	(TAMONTEN_GPIO_LAST + (_x_))
#define BOARD_GPIO_WM8903_LAST	BOARD_GPIO_WM8903(5)

#define BOARD_GPIO_SPKR_EN	BOARD_GPIO_WM8903(2)

#ifdef CONFIG_TAMONTEN_WM8903
void tamonten_wm8903_init(void);
#else
static inline void tamonten_wm8903_init(void) {}
#endif

#endif
