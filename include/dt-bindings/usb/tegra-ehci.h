/*
 * Copyright (c) 2015 Avionic Design GmbH
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

#ifndef _DT_BINDINGS_USB_TEGRA_EHCI_H
#define _DT_BINDINGS_USB_TEGRA_EHCI_H

#define TEGRA_USB_OPMODE_DEVICE 0
#define TEGRA_USB_OPMODE_HOST 1

#define TEGRA_USB_PHY_INTF_UTMI 0
#define TEGRA_USB_PHY_INTF_ULPI_LINK 1
#define TEGRA_USB_PHY_INTF_ULPI_NULL 2
#define TEGRA_USB_PHY_INTF_HSIC 3
#define TEGRA_USB_PHY_INTF_ICUSB 4

#define TEGRA_USB_ID 0
#define TEGRA_USB_PMU_ID 1
#define TEGRA_USB_GPIO_ID 2
#define TEGRA_USB_VIRTUAL_ID 3

#define TEGRA_USB_QC2_5V 0
#define TEGRA_USB_QC2_9V 1
#define TEGRA_USB_QC2_12V 2
#define TEGRA_USB_QC2_20V 3

#endif
