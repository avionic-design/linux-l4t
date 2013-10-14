/*
 * arch/arm/mach-tegra/com-tamonten-ng-power.c
 *
 * Copyright (C) 2011-2012, NVIDIA Corporation
 * Copyright (C) 2013, Avionic Design GmbH
 * Copyright (C) 2013, Julian Scheel <julian@jusst.de>
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

#include <linux/resource.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/mmc/host.h>

#include <asm/mach-types.h>
#include <mach/irqs.h>
#include <mach/iomap.h>
#include <mach/sdhci.h>

#include "com-tamonten.h"
#include "board.h"

static struct resource sdhci_resource0[] = {
	{
		.start = INT_SDMMC4,
		.end = INT_SDMMC4,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = TEGRA_SDMMC4_BASE,
		.end = TEGRA_SDMMC4_BASE + TEGRA_SDMMC4_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource sdhci_resource1[] = {
	{
		.start = INT_SDMMC3,
		.end = INT_SDMMC3,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = TEGRA_SDMMC3_BASE,
		.end = TEGRA_SDMMC3_BASE + TEGRA_SDMMC3_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct tegra_sdhci_platform_data tegra_sdhci_platform_data0 = {
	.cd_gpio = -1,
	.wp_gpio = -1,
	.power_gpio = -1,
	.is_8bit = 1,
	.tap_delay = 0x4f,
	.ddr_clk_limit = 41000000,
	.mmc_data = {
		.built_in = 1,
	},
};

static struct tegra_sdhci_platform_data tegra_sdhci_platform_data1 = {
	.cd_gpio = COM_GPIO_SD_CD,
	.wp_gpio = COM_GPIO_SD_WP,
	.power_gpio = -1,
	.tap_delay = 0x0f,
	.ddr_clk_limit = 41000000,
};

static struct platform_device tegra_sdhci_device0 = {
	.name = "sdhci-tegra",
	.id = 3,
	.resource = sdhci_resource0,
	.num_resources = ARRAY_SIZE(sdhci_resource0),
	.dev = {
		.platform_data = &tegra_sdhci_platform_data0,
	},
};

static struct platform_device tegra_sdhci_device1 = {
	.name = "sdhci-tegra",
	.id = 2,
	.resource = sdhci_resource1,
	.num_resources = ARRAY_SIZE(sdhci_resource1),
	.dev = {
		.platform_data = &tegra_sdhci_platform_data1,
	},
};

int __init tamonten_ng_sdhci_init(void)
{
	int ret = 0;

	ret = platform_device_register(&tegra_sdhci_device0);
	if (ret < 0)
		return ret;

	ret = platform_device_register(&tegra_sdhci_device1);

	return ret;
}
