/*
 * arch/arm/mach-tegra/com-tamonten.h
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

#ifndef _MACH_TEGRA_COM_TAMONTEN_H
#define _MACH_TEGRA_COM_TAMONTEN_H

struct machine_desc;
struct tag;
struct meminfo;

#define TAMONTEN_GPIO_TPS6586X(_x_)	(TEGRA_NR_GPIOS + (_x_))
#define TAMONTEN_GPIO_LAST		TAMONTEN_GPIO_TPS6586X(4)

#define COM_GPIO_SD_CD			TEGRA_GPIO_PH2
#define COM_GPIO_SD_WP			TEGRA_GPIO_PH3
#define COM_GPIO_SD_POWER		TEGRA_GPIO_PI6

#define COM_GPIO_CDC_IRQ		TEGRA_GPIO_PX3
#define COM_GPIO_HP_DET			TEGRA_GPIO_PW2
#define COM_GPIO_EXT_MIC_EN		TEGRA_GPIO_PX1

/* fixed voltage regulator enable/mode gpios */
#define TPS_GPIO_EN_1V5			TAMONTEN_GPIO_TPS6586X(0)
#define TPS_GPIO_EN_1V2			TAMONTEN_GPIO_TPS6586X(1)
#define TPS_GPIO_EN_1V05		TAMONTEN_GPIO_TPS6586X(2)
#define TPS_GPIO_MODE_1V05		TAMONTEN_GPIO_TPS6586X(3)

void tamonten_pinmux_init(void);
int tamonten_regulator_init(void);
int tamonten_suspend_init(void);
int tamonten_pcie_init(void);

void tamonten_fixup(struct machine_desc *desc,
		    struct tag *tags, char **cmdline,
		    struct meminfo *mi);
void tamonten_init(void);
void tamonten_reserve(void);

#endif
