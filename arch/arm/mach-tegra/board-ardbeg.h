/*
 * arch/arm/mach-tegra/board-ardbeg.h
 *
 * Copyright (c) 2013, NVIDIA Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef _MACH_TEGRA_BOARD_ARDBEG_H
#define _MACH_TEGRA_BOARD_ARDBEG_H

#include <mach/gpio-tegra.h>
#include <mach/irqs.h>
#include "gpio-names.h"

int ardbeg_pinmux_init(void);
int ardbeg_panel_init(void);
int ardbeg_kbc_init(void);
int ardbeg_sdhci_init(void);
int ardbeg_sensors_init(void);
int ardbeg_regulator_init(void);
int ardbeg_suspend_init(void);

/* Touchscreen definitions */
#ifdef CONFIG_ARCH_TEGRA_11x_SOC
#define TOUCH_GPIO_IRQ_RAYDIUM_SPI	TEGRA_GPIO_PK2
#define TOUCH_GPIO_RST_RAYDIUM_SPI	TEGRA_GPIO_PK4
#define TOUCH_SPI_ID			0	/*SPI 1 on ardbeg_interposer*/
#define TOUCH_SPI_CS			0	/*CS  0 on ardbeg_interposer*/
#else
#define TOUCH_GPIO_IRQ_RAYDIUM_SPI	TEGRA_GPIO_PK2
#define TOUCH_GPIO_RST_RAYDIUM_SPI	TEGRA_GPIO_PK4
#define TOUCH_SPI_ID			0	/*SPI 1 on ardbeg_interposer*/
#define TOUCH_SPI_CS			0	/*CS  0 on ardbeg_interposer*/
#endif

#define PALMAS_TEGRA_GPIO_BASE	TEGRA_NR_GPIOS
#define PALMAS_TEGRA_IRQ_BASE	TEGRA_NR_IRQS

/* Baseband IDs */
enum tegra_bb_type {
	TEGRA_BB_NEMO = 1,
	TEGRA_BB_HSIC_HUB = 6,
};

#define UTMI1_PORT_OWNER_XUSB   0x1
#define UTMI2_PORT_OWNER_XUSB   0x2
#define HSIC1_PORT_OWNER_XUSB   0x4

#endif