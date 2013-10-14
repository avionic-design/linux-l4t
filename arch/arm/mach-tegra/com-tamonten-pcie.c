/*
 * arch/arm/mach-tegra/com-tamonten-pcie.c
 *
 * Copyright (C) 2010 CompuLab, Ltd.
 * Mike Rapoport <mike@compulab.co.il>
 * Copyright (C) 2012-2013, Avionic Design GmbH
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

#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>

#include <mach/pinmux.h>
#include <mach/pci.h>
#include "devices.h"
#include "com-tamonten.h"

#ifdef CONFIG_TEGRA_PCI

static struct tegra_pci_platform_data tamonten_pci_platform_data = {
	.port_status[0] = 1,
	.port_status[1] = 1,
	.use_dock_detect = 0,
	.gpio = 0,
};

int __init tamonten_pcie_init(void)
{
	tegra_pci_device.dev.platform_data = &tamonten_pci_platform_data;
	platform_device_register(&tegra_pci_device);

	return 0;
}

#else

int __init tamonten_pcie_init(void)
{
	return 0;
}

#endif
