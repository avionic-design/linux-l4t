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

#define MEDCOM_WIDE_GPIO_TPS6586X(_x_)	(TEGRA_NR_GPIOS + (_x_))
#define MEDCOM_WIDE_GPIO_WM8903(_x_)	(MEDCOM_WIDE_GPIO_TPS6586X(4) + (_x_))

#define ADNP_GPIO(_x_)			(MEDCOM_WIDE_GPIO_WM8903(5) + (_x_))
#define ADNP_GPIO_TO_IRQ(_x_)		(INT_BOARD_BASE + 32 + ((_x_) - ADNP_GPIO(0)))
#define ADNP_IRQ(_x_)			ADNP_GPIO_TO_IRQ(ADNP_GPIO(_x_))

#define TEGRA_GPIO_SD2_CD		TEGRA_GPIO_PI5
#define TEGRA_GPIO_SD2_WP		TEGRA_GPIO_PH1
#define TEGRA_GPIO_SD2_POWER		TEGRA_GPIO_PT3
#define TEGRA_GPIO_SD4_CD		TEGRA_GPIO_PH2
#define TEGRA_GPIO_SD4_WP		TEGRA_GPIO_PH3
#define TEGRA_GPIO_SD4_POWER		TEGRA_GPIO_PI6
#define TEGRA_GPIO_CDC_IRQ		TEGRA_GPIO_PX3
#define TEGRA_GPIO_SPKR_EN		MEDCOM_WIDE_GPIO_WM8903(2)
#define TEGRA_GPIO_HP_DET		TEGRA_GPIO_PW2
#define TEGRA_GPIO_INT_MIC_EN		TEGRA_GPIO_PX0
#define TEGRA_GPIO_EXT_MIC_EN		TEGRA_GPIO_PX1
#define TEGRA_GPIO_CPLD_IRQ		TEGRA_GPIO_PU0
/* fixed voltage regulator enable/mode gpios */
#define TPS_GPIO_EN_1V5                 (MEDCOM_WIDE_GPIO_TPS6586X(0))
#define TPS_GPIO_EN_1V2                 (MEDCOM_WIDE_GPIO_TPS6586X(1))
#define TPS_GPIO_EN_1V05                (MEDCOM_WIDE_GPIO_TPS6586X(2))
#define TPS_GPIO_MODE_1V05              (MEDCOM_WIDE_GPIO_TPS6586X(3))

void medcom_wide_pinmux_init(void);
int medcom_wide_regulator_init(void);
int medcom_wide_suspend_init(void);
int medcom_wide_panel_init(void);

#endif
