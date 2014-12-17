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
	"ad,kein-baseboard",
	NULL
};

static struct tegra_usb_platform_data tegra_ehci2_pdata = {
	.port_otg = false,
	.has_hostpc = true,
	.unaligned_dma_buf_supported = false,
	.phy_intf = TEGRA_USB_PHY_INTF_UTMI,
	.op_mode = TEGRA_USB_OPMODE_HOST,
	.u_data.host = {
		.vbus_gpio = -1,
		.hot_plug = false,
		.remote_wakeup_supported = true,
		.power_off_on_suspend = true,
	},
	.u_cfg.utmi = {
		.hssync_start_delay = 0,
		.elastic_limit = 16,
		.idle_wait_delay = 17,
		.term_range_adj = 6,
		.xcvr_setup = 8,
		.xcvr_lsfslew = 2,
		.xcvr_lsrslew = 2,
		.xcvr_setup_offset = 0,
		.xcvr_use_fuses = 1,
		.vbus_oc_map = 0x5,
	},
};

static struct of_dev_auxdata kein_baseboard_auxdata_lookup[] __initdata = {
	COM_MEERKAT_AUXDATA,
	OF_DEV_AUXDATA("nvidia,tegra20-ehci", 0x7d008000,
		"tegra-ehci.2", &tegra_ehci2_pdata),
	{}
};

void __init kein_baseboard_init(void)
{
	tegra_meerkat_dt_init(kein_baseboard_auxdata_lookup);
}

DT_MACHINE_START(JETSON_TK1, "kein-baseboard")
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
	.init_late	= tegra_init_late
MACHINE_END
