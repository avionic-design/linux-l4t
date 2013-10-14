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

#include <mach/gpio.h>
#include <mach/irqs.h>
#include <linux/mfd/tps6591x.h>

#define TEC_NG_GPIO_BASE		TEGRA_NR_GPIOS

/* SD Card Slot */
#define TEC_NG_SD_CD			TEGRA_GPIO_PI5
#define TEC_NG_SD_WP			TEGRA_GPIO_PI3

/* TPS6591x GPIOs */
#define TPS6591X_GPIO_BASE		TEGRA_NR_GPIOS
#define TPS6591X_GPIO_0			(TPS6591X_GPIO_BASE + TPS6591X_GPIO_GP0)
#define TPS6591X_GPIO_1			(TPS6591X_GPIO_BASE + TPS6591X_GPIO_GP1)
#define TPS6591X_GPIO_2			(TPS6591X_GPIO_BASE + TPS6591X_GPIO_GP2)
#define TPS6591X_GPIO_3			(TPS6591X_GPIO_BASE + TPS6591X_GPIO_GP3)
#define TPS6591X_GPIO_4			(TPS6591X_GPIO_BASE + TPS6591X_GPIO_GP4)
#define TPS6591X_GPIO_5			(TPS6591X_GPIO_BASE + TPS6591X_GPIO_GP5)
#define TPS6591X_GPIO_6			(TPS6591X_GPIO_BASE + TPS6591X_GPIO_GP6)
#define TPS6591X_GPIO_7			(TPS6591X_GPIO_BASE + TPS6591X_GPIO_GP7)
#define TPS6591X_GPIO_8			(TPS6591X_GPIO_BASE + TPS6591X_GPIO_GP8)
#define TPS6591X_GPIO_END		(TPS6591X_GPIO_BASE + TPS6591X_GPIO_NR)

/* WM8903 GPIOs */
#define TEC_NG_GPIO_WM8903(_x_)		(TPS6591X_GPIO_END + (_x_))
#define TEC_NG_GPIO_WM8903_END		TEC_NG_GPIO_BASE(4)

#define TEGRA_GPIO_CDC_IRQ		TEGRA_GPIO_PW3
#define TEGRA_GPIO_SPKR_EN		TEC_NG_GPIO_WM8903(2)
#define TEGRA_GPIO_HP_DET		TEGRA_GPIO_PW2

/* TPS6591x IRQs */
#define TPS6591X_IRQ_BASE		TEGRA_NR_IRQS
#define TPS6591X_IRQ_END		(TPS6591X_IRQ_BASE + 18)

/* Thermal diode offset is taken from board-cardhu.h */
#define TDIODE_OFFSET	(10000)	/* in millicelsius */

int tec_ng_regulator_init(void);
int tec_ng_sdhci_init(void);
int tec_ng_pinmux_init(void);
int tec_ng_panel_init(void);
int tec_ng_edp_init(void);
int tec_ng_suspend_init(void);
int tec_ng_sensors_init(void);

#endif
