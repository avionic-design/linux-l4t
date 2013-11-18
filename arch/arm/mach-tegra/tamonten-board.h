/*
 * arch/arm/mach-tegra/tamonten-board.h
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

#ifndef _MACH_TEGRA_TAMONTEN_BOARD_H
#define _MACH_TEGRA_TAMONTEN_BOARD_H

#include "com-tamonten.h"

/* Macros to get a GPIO/IRQ from a board chip */
#define BOARD_GPIO(chip, n)		(BOARD_## chip ##_GPIO_BASE + (n))
#define BOARD_IRQ(chip, n)		(BOARD_## chip ##_IRQ_BASE + (n))
#define BOARD_GPIO_TO_IRQ(chip, gp)	BOARD_IRQ(chip, (gp) - BOARD_GPIO(chip, 0))

/* WM8903 audio codec */
#define BOARD_WM8903_GPIO_BASE		TAMONTEN_GPIO_LAST
#define BOARD_WM8903_GPIO_LAST		(BOARD_WM8903_GPIO_BASE + \
					BOARD_WM8903_GPIO_COUNT)

#define BOARD_WM8903_IRQ_BASE		TAMONTEN_IRQ_LAST
#define BOARD_WM8903_IRQ_LAST		(BOARD_WM8903_IRQ_BASE + \
					BOARD_WM8903_IRQ_COUNT)

#ifdef CONFIG_TAMONTEN_WM8903
#define BOARD_WM8903_GPIO_COUNT		5
#define BOARD_WM8903_IRQ_COUNT		0
#else
#define BOARD_WM8903_GPIO_COUNT		0
#define BOARD_WM8903_IRQ_COUNT		0
#endif

/* Avionic Design GPIO expander */
#define BOARD_ADNP_GPIO_BASE		BOARD_WM8903_GPIO_LAST
#define BOARD_ADNP_GPIO_LAST		(BOARD_ADNP_GPIO_BASE + \
					BOARD_ADNP_GPIO_COUNT)

#define BOARD_ADNP_IRQ_BASE		BOARD_WM8903_IRQ_LAST
#define BOARD_ADNP_IRQ_LAST		(BOARD_ADNP_IRQ_BASE + \
					BOARD_ADNP_IRQ_COUNT)

#ifdef CONFIG_TAMONTEN_ADNP
#define BOARD_ADNP_GPIO_COUNT		64
#define BOARD_ADNP_IRQ_COUNT		64
#else
#define BOARD_ADNP_GPIO_COUNT		0
#define BOARD_ADNP_IRQ_COUNT		0
#endif

/* Base GPIO/IRQ for extra GPIO controllers on the base board */
#define BOARD_EXTRA_GPIO_BASE		BOARD_ADNP_GPIO_LAST
#define BOARD_EXTRA_IRQ_BASE		BOARD_ADNP_IRQ_LAST

#endif
