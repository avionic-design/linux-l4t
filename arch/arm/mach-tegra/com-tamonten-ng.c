/*
 * arch/arm/mach-tegra/com-tamonten-ng.c
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
#include <linux/clk.h>
#include <linux/serial_8250.h>
#include <linux/i2c.h>
#include <linux/i2c-tegra.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <linux/platform_data/tegra_usb.h>
#include <linux/tegra_uart.h>
#include <linux/memblock.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/io_dpd.h>
#include <mach/usb_phy.h>
#include <mach/thermal.h>
#include <mach/pci.h>

#include "com-tamonten.h"
#include "board.h"
#include "clock.h"
#include "devices.h"
#include "pm.h"

static struct balanced_throttle throttle_list[] = {
#ifdef CONFIG_TEGRA_THERMAL_THROTTLE
	{
		.id = BALANCED_THROTTLE_ID_TJ,
		.throt_tab_size = 10,
		.throt_tab = {
			{      0, 1000 },
			{ 640000, 1000 },
			{ 640000, 1000 },
			{ 640000, 1000 },
			{ 640000, 1000 },
			{ 640000, 1000 },
			{ 760000, 1000 },
			{ 760000, 1050 },
			{1000000, 1050 },
			{1000000, 1100 },
		},
	},
#endif
#ifdef CONFIG_TEGRA_SKIN_THROTTLE
	{
		.id = BALANCED_THROTTLE_ID_SKIN,
		.throt_tab_size = 6,
		.throt_tab = {
			{ 640000, 1200 },
			{ 640000, 1200 },
			{ 760000, 1200 },
			{ 760000, 1200 },
			{1000000, 1200 },
			{1000000, 1200 },
		},
	},
#endif
};

static struct tegra_thermal_data thermal_data = {
	.shutdown_device_id = THERMAL_DEVICE_ID_NCT_EXT,
	.temp_shutdown = 90000,

#if defined(CONFIG_TEGRA_EDP_LIMITS) || defined(CONFIG_TEGRA_THERMAL_THROTTLE)
	.throttle_edp_device_id = THERMAL_DEVICE_ID_NCT_EXT,
#endif
#ifdef CONFIG_TEGRA_EDP_LIMITS
	.edp_offset = TDIODE_OFFSET,  /* edp based on tdiode */
	.hysteresis_edp = 3000,
#endif
#ifdef CONFIG_TEGRA_THERMAL_THROTTLE
	.temp_throttle = 85000,
	.tc1 = 0,
	.tc2 = 1,
	.passive_delay = 2000,
#endif
#ifdef CONFIG_TEGRA_SKIN_THROTTLE
	.skin_device_id = THERMAL_DEVICE_ID_SKIN,
	.temp_throttle_skin = 43000,
	.tc1_skin = 0,
	.tc2_skin = 1,
	.passive_delay_skin = 5000,

	.skin_temp_offset = 9793,
	.skin_period = 1100,
	.skin_devs_size = 2,
	.skin_devs = {
		{
			THERMAL_DEVICE_ID_NCT_EXT,
			{
				2, 1, 1, 1,
				1, 1, 1, 1,
				1, 1, 1, 0,
				1, 1, 0, 0,
				0, 0, -1, -7
			}
		},
		{
			THERMAL_DEVICE_ID_NCT_INT,
			{
				-11, -7, -5, -3,
				-3, -2, -1, 0,
				0, 0, 1, 1,
				1, 2, 2, 3,
				4, 6, 11, 18
			}
		},
	},
#endif
};

static __initdata struct tegra_clk_init_table tamonten_ng_clk_init_table[] = {
	{ "pll_a",	NULL,		552960000,	true},
	{ "pll_m",	NULL,		0,		false},
	{ "pll_c",	NULL,		400000000,	true},
	{ "pwm",	"pll_p",	3187500,	false},

	/* All the AHUB clients clocks must be running otherwise the AHUB
	 * just freezes the whole chip when it's accessed. As the current clock
	 * API doesn't allow us to represent that properly we just enable
	 * (and re-parent) them here. The L4T u-boot contains some code
	 * that does this but mainline u-boot doesn't.
	 */
	{ "i2s0",	"pll_a_out0",	0,		true},
	{ "i2s1",	"pll_a_out0",	0,		true},
	{ "i2s2",	"pll_a_out0",	0,		true},
	{ "i2s3",	"pll_a_out0",	0,		true},
	{ "i2s4",	"pll_a_out0",	0,		true},
	{ "spdif_out",	"pll_a_out0",	0,		true},
	{ NULL,		NULL,		0,		0},
};

static struct tegra_usb_platform_data tegra_ehci1_utmi_pdata = {
	.port_otg = true,
	.has_hostpc = true,
	.phy_intf = TEGRA_USB_PHY_INTF_UTMI,
	.op_mode = TEGRA_USB_OPMODE_HOST,
	.u_data.host = {
		.vbus_gpio = -1,
		.hot_plug = true,
		.remote_wakeup_supported = true,
		.power_off_on_suspend = true,
	},
	.u_cfg.utmi = {
		.hssync_start_delay = 0,
		.elastic_limit = 16,
		.idle_wait_delay = 17,
		.term_range_adj = 6,
		.xcvr_setup = 15,
		.xcvr_lsfslew = 2,
		.xcvr_lsrslew = 2,
		.xcvr_setup_offset = 0,
		.xcvr_use_fuses = 1,
	},
};

static struct tegra_usb_otg_data tegra_otg_pdata = {
	.ehci_device = &tegra_ehci1_device,
	.ehci_pdata = &tegra_ehci1_utmi_pdata,
};

static struct tegra_usb_platform_data tegra_ehci3_utmi_pdata = {
	.port_otg = false,
	.has_hostpc = true,
	.phy_intf = TEGRA_USB_PHY_INTF_UTMI,
	.op_mode = TEGRA_USB_OPMODE_HOST,
	.u_data.host = {
		.vbus_gpio = -1,
		.vbus_reg = "vdd_vbus_typea_usb",
		.hot_plug = true,
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
	},
};

static void tamonten_ng_usb_init(void)
{
	tegra_otg_device.dev.platform_data = &tegra_otg_pdata;
	platform_device_register(&tegra_otg_device);

	tegra_ehci3_device.dev.platform_data = &tegra_ehci3_utmi_pdata;
	platform_device_register(&tegra_ehci3_device);
}

static struct tegra_i2c_platform_data tamonten_ng_i2c0_gen1_platform_data = {
	.adapter_nr = COM_I2C_BUS_GEN1,
	.bus_count = 1,
	.is_clkon_always = true,
	.bus_clk_rate = { 100000, 0 },
	.scl_gpio = { TEGRA_GPIO_PC4, 0 },
	.sda_gpio = { TEGRA_GPIO_PC5, 0 },
	.arb_recovery = arb_lost_recovery,
};

static struct tegra_i2c_platform_data tamonten_ng_i2c1_gen2_platform_data = {
	.adapter_nr = COM_I2C_BUS_GEN2,
	.bus_count = 1,
	.bus_clk_rate = { 100000, 0 },
	.scl_gpio = { TEGRA_GPIO_PT5, 0 },
	.sda_gpio = { TEGRA_GPIO_PT6, 0 },
	.arb_recovery = arb_lost_recovery,
};

static struct tegra_i2c_platform_data tamonten_ng_i2c2_cam_platform_data = {
	.adapter_nr = COM_I2C_BUS_CAM,
	.bus_count = 1,
	.bus_clk_rate = { 100000, 0 },
	.scl_gpio = { TEGRA_GPIO_PBB1, 0 },
	.sda_gpio = { TEGRA_GPIO_PBB2, 0 },
	.arb_recovery = arb_lost_recovery,
};

static struct tegra_i2c_platform_data tamonten_ng_i2c3_ddc_platform_data = {
	.adapter_nr = COM_I2C_BUS_DDC,
	.bus_count = 1,
	.bus_clk_rate = { 100000, 0 },
	.scl_gpio = { TEGRA_GPIO_PV4, 0 },
	.sda_gpio = { TEGRA_GPIO_PV5, 0 },
	.arb_recovery = arb_lost_recovery,
};

static struct tegra_i2c_platform_data tamonten_ng_i2c4_pwr_platform_data = {
	.adapter_nr = COM_I2C_BUS_PWR,
	.bus_count = 1,
	.bus_clk_rate = { 100000, 0 },
	.scl_gpio = { TEGRA_GPIO_PZ6, 0 },
	.sda_gpio = { TEGRA_GPIO_PZ7, 0 },
	.arb_recovery = arb_lost_recovery,
};

static void tamonten_ng_i2c_init(void)
{
	tegra_i2c_device1.dev.platform_data = &tamonten_ng_i2c0_gen1_platform_data;
	tegra_i2c_device2.dev.platform_data = &tamonten_ng_i2c1_gen2_platform_data;
	tegra_i2c_device3.dev.platform_data = &tamonten_ng_i2c2_cam_platform_data;
	tegra_i2c_device4.dev.platform_data = &tamonten_ng_i2c3_ddc_platform_data;
	tegra_i2c_device5.dev.platform_data = &tamonten_ng_i2c4_pwr_platform_data;

	platform_device_register(&tegra_i2c_device1);
	platform_device_register(&tegra_i2c_device2);
	platform_device_register(&tegra_i2c_device3);
	platform_device_register(&tegra_i2c_device4);
	platform_device_register(&tegra_i2c_device5);
}

static struct platform_device *tamonten_ng_uart_devices[] __initdata = {
	&tegra_uarta_device,
	&tegra_uartb_device,
	&tegra_uartc_device,
	&debug_uartd_device,
};

static struct uart_clk_parent uart_parent_clk[] = {
	[0] = { .name = "clk_m" },
	[1] = { .name = "pll_p" },
	[2] = { .name = "pll_m" },
};

static struct tegra_uart_platform_data tamonten_ng_uart_pdata;

static void __init uart_debug_init(void)
{
	debug_uart_clk = clk_get_sys("serial8250.0", "uartd");
	debug_uart_port_base = ((struct plat_serial8250_port *)(
				debug_uartd_device.dev.platform_data))->mapbase;
}

static void __init tamonten_ng_uart_init(void)
{
	struct clk *c;
	int i;

	for (i = 0; i < ARRAY_SIZE(uart_parent_clk); i++) {
		c = tegra_get_clock_by_name(uart_parent_clk[i].name);
		if (IS_ERR_OR_NULL(c)) {
			pr_err("Not able to get the clock for %s\n",
					uart_parent_clk[i].name);
			continue;
		}
		uart_parent_clk[i].parent_clk = c;
		uart_parent_clk[i].fixed_clk_rate = clk_get_rate(c);
	}
	tamonten_ng_uart_pdata.parent_clk_list = uart_parent_clk;
	tamonten_ng_uart_pdata.parent_clk_count = ARRAY_SIZE(uart_parent_clk);

	tegra_uarta_device.dev.platform_data = &tamonten_ng_uart_pdata;
	tegra_uartb_device.dev.platform_data = &tamonten_ng_uart_pdata;
	tegra_uartc_device.dev.platform_data = &tamonten_ng_uart_pdata;
	tegra_uartd_device.dev.platform_data = &tamonten_ng_uart_pdata;

	uart_debug_init();
	if (!IS_ERR_OR_NULL(debug_uart_clk)) {
		pr_info("The debug console clock name is %s\n",
				debug_uart_clk->name);
		c = tegra_get_clock_by_name("pll_p");
		if (IS_ERR_OR_NULL(c))
			pr_err("Not getting the parent clock pll_p\n");
		else
			clk_set_parent(debug_uart_clk, c);

		clk_enable(debug_uart_clk);
		clk_set_rate(debug_uart_clk, clk_get_rate(c));
	} else {
		pr_err("Could not get the clock %s for debug console\n",
				debug_uart_clk->name);
	}

	platform_add_devices(tamonten_ng_uart_devices,
			ARRAY_SIZE(tamonten_ng_uart_devices));
}

static struct resource tegra_rtc_resources[] = {
	[0] = {
		.start = TEGRA_RTC_BASE,
		.end = TEGRA_RTC_BASE + TEGRA_RTC_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = INT_RTC,
		.end = INT_RTC,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device tegra_rtc_device = {
	.name = "tegra_rtc",
	.id   = -1,
	.resource = tegra_rtc_resources,
	.num_resources = ARRAY_SIZE(tegra_rtc_resources),
};

static struct platform_device *tamonten_ng_devices[] __initdata = {
	&tegra_pmu_device,
	&tegra_rtc_device,
	&tegra_udc_device,
#if defined(CONFIG_TEGRA_AVP)
	&tegra_avp_device,
#endif
#ifdef CONFIG_SATA_AHCI_TEGRA
	&tegra_sata_device,
#endif
	&tegra_ahub_device,
	&tegra_dam_device0,
	&tegra_dam_device1,
	&tegra_i2s_device1,
	&tegra_spdif_device,
	&spdif_dit_device,
	&tegra_pcm_device,
};

void __init tamonten_init(void)
{
	tegra_thermal_init(&thermal_data, throttle_list,
			ARRAY_SIZE(throttle_list));
	tegra_io_dpd_init();
	tegra_clk_init_from_table(tamonten_ng_clk_init_table);
	tamonten_ng_pinmux_init();
	tamonten_ng_i2c_init();
	tamonten_ng_usb_init();
	tamonten_ng_edp_init();
	tamonten_ng_uart_init();

	platform_add_devices(tamonten_ng_devices, ARRAY_SIZE(tamonten_ng_devices));
	tegra_ram_console_debug_init();
	tamonten_ng_regulator_init();
	tamonten_ng_suspend_init();
	tamonten_ng_sensors_init();
	tamonten_ng_sdhci_init();
	tamonten_pcie_init();

	tegra_release_bootloader_fb();
}

void __init tamonten_reserve(void)
{
#if defined(CONFIG_NVMAP_CONVERT_CARVEOUT_TO_IOVMM)
	tegra_reserve(0, SZ_8M + SZ_1M, SZ_16M);
#else
	tegra_reserve(SZ_128M, SZ_8M, SZ_8M);
#endif
	tegra_ram_console_debug_reserve(SZ_1M);
}

void __init tamonten_fixup(struct machine_desc *desc,
			   struct tag *tags, char **cmdline,
			   struct meminfo *mi)
{
}
