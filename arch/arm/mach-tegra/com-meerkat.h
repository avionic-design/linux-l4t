/*
 * Copyright (c) 2014 Avionic Design GmbH
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

#ifndef _MACH_TEGRA_COM_MEERKAT_H
#define _MACH_TEGRA_COM_MEERKAT_H

#include "tegra-of-dev-auxdata.h"
#include "iomap.h"

struct of_dev_auxdata;

void tegra_meerkat_init_early(void);
void tegra_meerkat_init_late(void);
void tegra_meerkat_dt_init(struct of_dev_auxdata *auxdata);
void tegra_meerkat_init(void);
void tegra_meerkat_reserve(void);

#define COM_MEERKAT_AUXDATA \
	T124_SPI_OF_DEV_AUXDATA,					\
	OF_DEV_AUXDATA("nvidia,tegra124-apbdma", 0x60020000,		\
		"tegra-apbdma", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra124-se", 0x70012000,		\
		"tegra12-se", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra124-host1x", TEGRA_HOST1X_BASE,	\
		"host1x", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra124-gk20a", TEGRA_GK20A_BAR0_BASE,	\
		"gk20a.0", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra124-vic", TEGRA_VIC_BASE,		\
		"vic03.0", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra124-msenc", TEGRA_MSENC_BASE,	\
		"msenc", NULL),						\
	OF_DEV_AUXDATA("nvidia,tegra124-vi", TEGRA_VI_BASE,		\
		"vi.0", NULL),						\
	OF_DEV_AUXDATA("nvidia,tegra124-isp", TEGRA_ISP_BASE,		\
		"isp.0", NULL),						\
	OF_DEV_AUXDATA("nvidia,tegra124-isp", TEGRA_ISPB_BASE,		\
		"isp.1", NULL),						\
	OF_DEV_AUXDATA("nvidia,tegra124-tsec", TEGRA_TSEC_BASE,		\
		"tsec", NULL),						\
	OF_DEV_AUXDATA("nvidia,tegra114-hsuart", 0x70006000,		\
		"serial-tegra.0", NULL),				\
	OF_DEV_AUXDATA("nvidia,tegra114-hsuart", 0x70006040,		\
		"serial-tegra.1", NULL),				\
	OF_DEV_AUXDATA("nvidia,tegra114-hsuart", 0x70006200,		\
		"serial-tegra.2", NULL),				\
	OF_DEV_AUXDATA("nvidia,tegra114-hsuart", 0x70006300,		\
		"serial-tegra.3", NULL),				\
	OF_DEV_AUXDATA("nvidia,tegra20-uart", 0x70006000,		\
		"serial8250.0", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra20-uart", 0x70006040,		\
		"serial8250.1", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra20-uart", 0x70006200,		\
		"serial8250.2", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra20-uart", 0x70006300,		\
		"serial8250.3", NULL),					\
	T124_I2C_OF_DEV_AUXDATA,					\
	OF_DEV_AUXDATA("nvidia,tegra124-dc", TEGRA_DISPLAY_BASE,	\
		"tegradc.0", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra124-dc", TEGRA_DISPLAY2_BASE,	\
		"tegradc.1", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra124-hdmi", 0x54280000,		\
		"hdmi", NULL),						\
	OF_DEV_AUXDATA("nvidia,tegra124-nvavp", 0x60001000,		\
		"nvavp", NULL),						\
	OF_DEV_AUXDATA("nvidia,tegra124-pwm", 0x7000a000,		\
		"tegra-pwm", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra124-dfll", 0x70110000,		\
		"tegra_cl_dvfs", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra132-dfll", 0x70040084,		\
		"tegra_cl_dvfs", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra124-efuse", TEGRA_FUSE_BASE,	\
		"tegra-fuse", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra124-camera", 0,			\
		"pcl-generic", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra114-ahci-sata", 0x70027000,		\
		"tegra-sata.0", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra124-sdhci", 0x700b0000,		\
		"sdhci-tegra.0", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra124-sdhci", 0x700b0200,		\
		"sdhci-tegra.1", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra124-sdhci", 0x700b0400,		\
		"sdhci-tegra.2", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra124-sdhci", 0x700b0600,		\
		"sdhci-tegra.3", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra124-xhci", 0x70090000,		\
		"tegra-xhci", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra124-hda", 0x70030000,		\
		"tegra30-hda", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra124-ahub", 0x70300000,		\
		"tegra30-ahub", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra20-ehci", 0x7d000000,		\
		"tegra-ehci.0", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra20-ehci", 0x7d004000,		\
		"tegra-ehci.1", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra20-ehci", 0x7d008000,		\
		"tegra-ehci.2", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra20-udc", 0x7d000000,		\
		"tegra-udc.0", NULL),					\
	OF_DEV_AUXDATA("nvidia,tegra20-otg", 0x7d000000,		\
		"tegra-otg", NULL)


#endif
