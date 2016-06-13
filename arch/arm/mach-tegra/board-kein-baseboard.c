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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/irqchip.h>
#include <linux/clocksource.h>
#include <linux/platform_data/tegra_usb.h>

#include <asm/mach/arch.h>

#include "board.h"
#include "common.h"
#include "com-meerkat.h"

static const char * const kein_baseboard_dt_board_compat[] = {
	"ad,meerkat",
	NULL
};

static struct of_dev_auxdata kein_baseboard_auxdata_lookup[] __initdata = {
	COM_MEERKAT_AUXDATA,
	{}
};

void __init kein_baseboard_init(void)
{
	tegra_meerkat_dt_init(kein_baseboard_auxdata_lookup);
}

DT_MACHINE_START(MEERKAT_DT, "Avionic Design Meerkat (Device Tree)")
	.atag_offset	= 0x100,
	.smp		= smp_ops(tegra_smp_ops),
	.map_io		= tegra_map_common_io,
	.reserve	= tegra_meerkat_reserve,
	.init_early	= tegra_meerkat_init_early,
	.init_irq	= irqchip_init,
	.init_time	= clocksource_of_init,
	.init_machine	= kein_baseboard_init,
	.restart	= tegra_assert_system_reset,
	.dt_compat	= kein_baseboard_dt_board_compat,
	.init_late	= tegra_meerkat_init_late
MACHINE_END
