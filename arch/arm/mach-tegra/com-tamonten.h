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

#include <mach/gpio.h>
#include "gpio-names.h"
#include "com-tamonten-display.h"

struct machine_desc;
struct tag;
struct meminfo;
struct tegra_dc_platform_data;
struct device;

/* The I2C buses */
/* Note: DDC and GEN2 must follow each other because they are handled
 * by the same controller on T20 */
#define COM_I2C_BUS_GEN1		0
#define COM_I2C_BUS_DDC			1
#define COM_I2C_BUS_GEN2		2
#define COM_I2C_BUS_CAM			3
#define COM_I2C_BUS_PWR			4

/* The GPIOs */
#define TAMONTEN_PMU_GPIO_BASE		TEGRA_NR_GPIOS
#define TAMONTEN_PMU_GPIO(_x_)		(TAMONTEN_PMU_GPIO_BASE + (_x_))

#define TAMONTEN_PMU_IRQ_BASE		TEGRA_NR_IRQS
#define TAMONTEN_PMU_IRQ(_x_)		(TAMONTEN_PMU_IRQ_BASE + (_x_))

#define TAMONTEN_GPIO_LAST		TAMONTEN_PMU_GPIO(TAMONTEN_PMU_GPIO_COUNT)
#define TAMONTEN_IRQ_LAST		TAMONTEN_PMU_IRQ(TAMONTEN_PMU_IRQ_COUNT)

#define COM_GPIO_TO_IRQ(_x_)		TEGRA_GPIO_TO_IRQ(_x_)

#define COM_PWM_BACKLIGHT		0

#ifdef CONFIG_COM_TAMONTEN

#define TAMONTEN_PMU_GPIO_COUNT		4
#define TAMONTEN_PMU_IRQ_COUNT		(27)

#define TAMONTEN_BOOT_PARAMS		0x00000100

#define COM_GPIO_0			TEGRA_GPIO_PU0
#define COM_GPIO_1			TEGRA_GPIO_PU1

#define COM_GPIO_ALIVE			TEGRA_GPIO_PV0

#define COM_GPIO_WAKEUP			TEGRA_GPIO_PV3
#define COM_GPIO_SDIO_CMD_SPARE		TEGRA_GPIO_PD5

#define COM_GPIO_VGA_DET		TEGRA_GPIO_PX2

#define COM_GPIO_SD_CD			TEGRA_GPIO_PH2
#define COM_GPIO_SD_WP			TEGRA_GPIO_PH3

#define COM_GPIO_CDC_IRQ		TEGRA_GPIO_PX3
#define COM_GPIO_HP_DET			TEGRA_GPIO_PW2
#define COM_GPIO_EXT_MIC_EN		TEGRA_GPIO_PX1

#define COM_GPIO_BACKLIGHT_ENABLE	TEGRA_GPIO_PB5
#define COM_GPIO_BACKLIGHT_PWM		TEGRA_GPIO_PB4

#define COM_GPIO_LVDS_SHUTDOWN		TEGRA_GPIO_PB2

#define COM_GPIO_HDMI_HPD		TEGRA_GPIO_PN7

#define COM_GPIO_nRST_PERIPHERALS	TEGRA_GPIO_PI4
#define COM_GPIO_DBG_IRQ		TEGRA_GPIO_PC1
#define COM_GPIO_TS_IRQ			TEGRA_GPIO_PD2

/* Some of the test points on the Tamonten COM module */
#define COM_GPIO_TP_IRQ			TEGRA_GPIO_PA0
#define COM_GPIO_TP16			TEGRA_GPIO_PI6
#define COM_GPIO_TP17			TEGRA_GPIO_PI5

/* fixed voltage regulator enable/mode gpios */
#define TPS_GPIO_EN_1V5			TAMONTEN_PMU_GPIO(0)
#define TPS_GPIO_EN_1V2			TAMONTEN_PMU_GPIO(1)
#define TPS_GPIO_EN_1V05		TAMONTEN_PMU_GPIO(2)
#define TPS_GPIO_MODE_1V05		TAMONTEN_PMU_GPIO(3)

void tamonten_pinmux_init(void);
int tamonten_regulator_init(void);
int tamonten_suspend_init(void);

#else
#include <linux/mfd/tps6591x.h>

#define TAMONTEN_PMU_GPIO_COUNT		TPS6591X_GPIO_NR
#define TAMONTEN_PMU_IRQ_COUNT		(18)

#define TAMONTEN_BOOT_PARAMS		0x80000100

#define COM_PWM_BACKLIGHT		0

#define COM_GPIO_0			TEGRA_GPIO_PU5
#define COM_GPIO_1			TEGRA_GPIO_PU6

#define COM_GPIO_ALIVE			TEGRA_GPIO_PV2

#define COM_GPIO_WAKEUP			TEGRA_GPIO_PV3
#define COM_GPIO_SATA_nDET		TEGRA_GPIO_PP0

#define COM_GPIO_SD_CD			TEGRA_GPIO_PI5
#define COM_GPIO_SD_WP			TEGRA_GPIO_PI3

#define COM_GPIO_CDC_IRQ		TEGRA_GPIO_PW3
#define COM_GPIO_HP_DET			TEGRA_GPIO_PW2
#define COM_GPIO_EXT_MIC_EN		TEGRA_GPIO_PX1

#define COM_GPIO_BACKLIGHT_ENABLE	TEGRA_GPIO_PH2
#define COM_GPIO_BACKLIGHT_PWM		TEGRA_GPIO_PH0

#define COM_GPIO_LVDS_SHUTDOWN		TEGRA_GPIO_PB2

#define COM_GPIO_HDMI_HPD		TEGRA_GPIO_PN7

#define COM_GPIO_nRST_PERIPHERALS	TEGRA_GPIO_PI4
#define COM_GPIO_DBG_IRQ		TEGRA_GPIO_PC1
#define COM_GPIO_TS_IRQ			TEGRA_GPIO_PH4

/* Thermal diode offset is taken from board-cardhu.h */
#define TDIODE_OFFSET	(10000)	/* in millicelsius */

int tamonten_ng_regulator_init(void);
int tamonten_ng_sdhci_init(void);
int tamonten_ng_pinmux_init(void);
int tamonten_ng_edp_init(void);
int tamonten_ng_suspend_init(void);
int tamonten_ng_sensors_init(void);

#endif

void tamonten_emc_init(void);
int tamonten_pcie_init(void);

void tamonten_fixup(struct machine_desc *desc,
		    struct tag *tags, char **cmdline,
		    struct meminfo *mi);
void tamonten_init(void);
void tamonten_reserve(void);

#endif
