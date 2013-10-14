/*
 * arch/arm/mach-tegra/com-tamonten.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (C) 2011 NVIDIA, Inc.
 * Copyright (c) 2012, NVIDIA CORPORATION. All rights reserved.
 * Copyright (C) 2013, Avionic Design GmbH
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
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/clk.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/i2c-tegra.h>
#include <linux/memblock.h>
#include <linux/platform_data/tegra_usb.h>
#include <linux/tegra_uart.h>
#include <linux/nct1008.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>

#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/sdhci.h>
#include <mach/nand.h>
#include <mach/clk.h>
#include <mach/usb_phy.h>

#include "clock.h"
#include "board.h"
#include "com-tamonten.h"
#include "clock.h"
#include "devices.h"
#include "gpio-names.h"
#include "pm.h"

#define TAMONTEN_GPIO_TEMP_ALERT	TEGRA_GPIO_PN6

static struct tegra_usb_platform_data tegra_udc_pdata = {
	.port_otg = false,
	.has_hostpc = false,
	.phy_intf = TEGRA_USB_PHY_INTF_UTMI,
	.op_mode = TEGRA_USB_OPMODE_DEVICE,
	.u_data.dev = {
		.vbus_pmu_irq = 0,
		.vbus_gpio = -1,
		.charging_supported = false,
		.remote_wakeup_supported = false,
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
	},
};

static struct tegra_usb_platform_data tegra_ehci3_utmi_pdata = {
	.port_otg = false,
	.has_hostpc = false,
	.phy_intf = TEGRA_USB_PHY_INTF_UTMI,
	.op_mode = TEGRA_USB_OPMODE_HOST,
	.u_data.host = {
		.vbus_gpio = TEGRA_GPIO_PD3,
		.vbus_reg = NULL,
		.hot_plug = true,
		.remote_wakeup_supported = false,
		.power_off_on_suspend = true,
	},
	.u_cfg.utmi = {
		.hssync_start_delay = 9,
		.elastic_limit = 16,
		.idle_wait_delay = 17,
		.term_range_adj = 6,
		.xcvr_setup = 8,
		.xcvr_lsfslew = 2,
		.xcvr_lsrslew = 2,
	},
};

static struct tegra_nand_chip_parms nand_chip_parms[] = {
	/* Hynix HY27UF084G2B */
	[0] = {
		.vendor_id = 0xAD,
		.device_id = 0xDC,
		.read_id_fourth_byte = 0x95,
		.capacity = 512,
		.timing = {
			.trp = 12,
			.trh = 1,
			.twp = 12,
			.twh = 0,
			.tcs = 24,
			.twhr = 58,
			.tcr_tar_trr = 0,
			.twb = 116,
			.trp_resp = 24,
			.tadl = 24,
		},
	},
};

static struct tegra_nand_platform tamonten_nand_data = {
	.max_chips = 8,
	.chip_parms = nand_chip_parms,
	.nr_chip_parms = ARRAY_SIZE(nand_chip_parms),
	.wp_gpio = TEGRA_GPIO_PC7,
};

static struct resource resources_nand[] = {
	[0] = {
		.start = INT_NANDFLASH,
		.end = INT_NANDFLASH,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device tegra_nand_device = {
	.name = "tegra_nand",
	.id = -1,
	.num_resources = ARRAY_SIZE(resources_nand),
	.resource = resources_nand,
	.dev = {
		.platform_data = &tamonten_nand_data,
	},
};

static struct tegra_i2c_platform_data tamonten_i2c1_platform_data = {
	.adapter_nr = COM_I2C_BUS_GEN1,
	.bus_count = 1,
	/* On plutux .bus_clk_rate = { 400000, 0 } but why this is
	   different doesn't show in the history. */
	.bus_clk_rate = { 100000, 0 },
};

static const struct tegra_pingroup_config i2c2_ddc = {
	.pingroup = TEGRA_PINGROUP_DDC,
	.func = TEGRA_MUX_I2C2,
};

static const struct tegra_pingroup_config i2c2_gen2 = {
	.pingroup = TEGRA_PINGROUP_PTA,
	.func = TEGRA_MUX_I2C2,
};

static struct tegra_i2c_platform_data tamonten_i2c2_platform_data = {
	.adapter_nr = COM_I2C_BUS_DDC,
	.bus_count = 2,
	.bus_clk_rate = { 100000, 100000 },
	.bus_mux = { &i2c2_ddc, &i2c2_gen2 },
	.bus_mux_len = { 1, 1 },
};

static struct tegra_i2c_platform_data tamonten_i2c3_platform_data = {
	.adapter_nr = COM_I2C_BUS_CAM,
	.bus_count = 1,
	.bus_clk_rate = { 400000, 0 },
};

static struct tegra_i2c_platform_data tamonten_dvc_platform_data = {
	.adapter_nr = COM_I2C_BUS_PWR,
	.bus_count = 1,
	.bus_clk_rate = { 400000, 0 },
	.is_dvc = true,
};

static struct nct1008_platform_data tamonten_nct1008_pdata = {
	.supported_hwrev = true,
	.ext_range = false,
	.conv_rate = 0x08,
	.offset = 0,
};

static struct i2c_board_info __initdata tamonten_dvc_board_info[] = {
	{
		I2C_BOARD_INFO("nct1008", 0x4c),
		.platform_data = &tamonten_nct1008_pdata,
		.irq = TEGRA_GPIO_TO_IRQ(TAMONTEN_GPIO_TEMP_ALERT)
	},
};

static void __init tamonten_i2c_init(void)
{
	tegra_i2c_device1.dev.platform_data = &tamonten_i2c1_platform_data;
	tegra_i2c_device2.dev.platform_data = &tamonten_i2c2_platform_data;
	tegra_i2c_device3.dev.platform_data = &tamonten_i2c3_platform_data;
	tegra_i2c_device4.dev.platform_data = &tamonten_dvc_platform_data;

	platform_device_register(&tegra_i2c_device1);
	platform_device_register(&tegra_i2c_device2);
	platform_device_register(&tegra_i2c_device3);
	platform_device_register(&tegra_i2c_device4);

	i2c_register_board_info(4, tamonten_dvc_board_info,
				ARRAY_SIZE(tamonten_dvc_board_info));
}

static struct plat_serial8250_port uart3_platform_data[] = {
	{
		.membase	= IO_ADDRESS(TEGRA_UARTC_BASE),
		.mapbase	= TEGRA_UARTC_BASE,
		.irq		= INT_UARTC,
		.flags		= UPF_BOOT_AUTOCONF | UPF_FIXED_TYPE,
		.type		= PORT_TEGRA,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 216000000,
	}, {
		.flags		= 0
	}
};

static struct platform_device uart3_device = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM1,
	.dev = {
		.platform_data = uart3_platform_data,
	},
};

static struct plat_serial8250_port uart2_platform_data[] = {
	{
		.membase  = IO_ADDRESS(TEGRA_UARTB_BASE),
		.mapbase  = TEGRA_UARTB_BASE,
		.irq      = INT_UARTB,
		.flags    = UPF_BOOT_AUTOCONF | UPF_FIXED_TYPE,
		.type     = PORT_TEGRA,
		.iotype   = UPIO_MEM,
		.regshift = 2,
		.uartclk  = 216000000,
	}, {
		.flags    = 0,
	},
};

static struct platform_device uart2_device = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM2,
	.dev = {
		.platform_data = uart2_platform_data,
	},
};

static struct platform_device *tamonten_uart_devices[] __initdata = {
	&tegra_uartd_device,
	&uart3_device,
	&uart2_device,
};

static struct uart_clk_parent uart_parent_clk[] __initdata = {
	[0] = { .name = "pll_p" },
	[1] = { .name = "pll_m" },
	[2] = { .name = "clk_m" },
};

static struct tegra_uart_platform_data tamonten_uart_pdata;

static void __init uart_debug_init(void)
{
	struct plat_serial8250_port *port = debug_uartd_device.dev.platform_data;
	unsigned long rate = port->uartclk;
	struct clk *c;

	/* UARTD is the debug port. */
	pr_info("Selecting UARTD as the debug console\n");
	tamonten_uart_devices[0] = &debug_uartd_device;
	debug_uart_port_base = port->mapbase;
	debug_uart_clk = clk_get_sys("serial8250.0", "uartd");

	/* Clock enable for the debug channel */
	if (!IS_ERR_OR_NULL(debug_uart_clk)) {
		pr_info("The debug console clock name is %s\n",
			debug_uart_clk->name);
		c = tegra_get_clock_by_name("pll_p");
		if (IS_ERR_OR_NULL(c))
			pr_err("Not getting the parent clock pll_p\n");
		else
			clk_set_parent(debug_uart_clk, c);

		clk_enable(debug_uart_clk);
		clk_set_rate(debug_uart_clk, rate);
	} else {
		pr_err("Not getting the clock %s for debug console\n",
		       debug_uart_clk->name);
	}
}

static void __init tamonten_uart_init(void)
{
	int i;
	struct clk *c;

	for (i = 0; i < ARRAY_SIZE(uart_parent_clk); ++i) {
		c = tegra_get_clock_by_name(uart_parent_clk[i].name);
		if (IS_ERR_OR_NULL(c)) {
			pr_err("Not able to get the clock for %s\n",
			       uart_parent_clk[i].name);
			continue;
		}
		uart_parent_clk[i].parent_clk = c;
		uart_parent_clk[i].fixed_clk_rate = clk_get_rate(c);
	}

	tamonten_uart_pdata.parent_clk_list = uart_parent_clk;
	tamonten_uart_pdata.parent_clk_count = ARRAY_SIZE(uart_parent_clk);
	tegra_uartd_device.dev.platform_data = &tamonten_uart_pdata;

	/* Register low speed only if it is selected */
	if (!is_tegra_debug_uartport_hs())
		uart_debug_init();

	platform_add_devices(tamonten_uart_devices,
			     ARRAY_SIZE(tamonten_uart_devices));
}

static struct platform_device *tamonten_devices[] __initdata = {
	&tegra_sdhci_device4,
	&tegra_i2s_device1,
	&tegra_i2s_device2,
	&tegra_spdif_device,
	&tegra_das_device,
	&spdif_dit_device,
	&bluetooth_dit_device,
	&tegra_pcm_device,
	&tegra_pmu_device,
	&tegra_nand_device,
	&tegra_udc_device,
	&tegra_ehci3_device,
	&tegra_spi_device1,
	&tegra_spi_device2,
	&tegra_spi_device3,
	&tegra_spi_device4,
	&tegra_gart_device,
	&tegra_avp_device,
};

void __init tamonten_fixup(struct machine_desc *desc,
			   struct tag *tags, char **cmdline,
			   struct meminfo *mi)
{
	mi->nr_banks = 1;
	mi->bank[0].start = PHYS_OFFSET;
	mi->bank[0].size = 448 * SZ_1M;
}

static __initdata struct tegra_clk_init_table tamonten_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "uarta",	"pll_p",	216000000,	false },
	{ "uartb",	"pll_p",	216000000,	false },
	{ "uartc",	"pll_p",	216000000,	true  },
	{ "uartd",	"pll_p",	216000000,	true  },
	{ "uarte",	"pll_p",	216000000,	false },
	{ "i2s1",	"pll_a_out0",	0,		false },
	{ "spdif_out",	"pll_a_out0",	0,		false },
	{ "sdmmc4",	"clk_m",	48000000,	true  },
	{ "ndflash",	"pll_p",	108000000,	true  },
	{ "pwm",	"clk_32k",	32768,		false },
	{ NULL,		NULL,		0,		false },
};

static struct tegra_sdhci_platform_data sdhci_pdata4 = {
	.cd_gpio = COM_GPIO_SD_CD,
	.wp_gpio = COM_GPIO_SD_WP,
	.power_gpio = -1,
	.is_8bit = 1,
};

void __init tamonten_init(void)
{
	tegra_clk_init_from_table(tamonten_clk_init_table);

	tamonten_pinmux_init();
	tamonten_uart_init();

	tegra_sdhci_device4.dev.platform_data = &sdhci_pdata4;
	tegra_udc_device.dev.platform_data = &tegra_udc_pdata;
	tegra_ehci3_device.dev.platform_data = &tegra_ehci3_utmi_pdata;

	platform_add_devices(tamonten_devices,
			     ARRAY_SIZE(tamonten_devices));
	tamonten_i2c_init();
	tamonten_regulator_init();
	tamonten_suspend_init();
	tamonten_pcie_init();
}

void __init tamonten_reserve(void)
{
	if (memblock_reserve(0x0, 4096) < 0)
		pr_warn("Cannot reserve first 4K of memory for safety\n");

	tegra_reserve(SZ_128M, SZ_8M, SZ_16M);
}
