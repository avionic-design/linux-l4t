/*
 * arch/arm/mach-tegra/board-medcom-wide.c
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
#include <linux/nct1008.h>
#include <linux/dma-mapping.h>
#include <linux/pda_power.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/i2c.h>
#include <linux/i2c-tegra.h>
#include <linux/i2c/adnp.h>
#include <linux/input/sx8634.h>
#include <linux/memblock.h>
#include <linux/delay.h>
#include <linux/platform_data/tegra_usb.h>
#include <linux/tegra_uart.h>

#include <sound/wm8903.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>

#include <mach/tegra_asoc_pdata.h>
#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/sdhci.h>
#include <mach/nand.h>
#include <mach/clk.h>
#include <mach/usb_phy.h>

#include "clock.h"
#include "board.h"
#include "board-medcom-wide.h"
#include "clock.h"
#include "devices.h"
#include "gpio-names.h"
#include "pm.h"

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
	/* Hynix H5PS1GB3EFR */
	[0] = {
		.vendor_id = 0xAD,
		.device_id = 0xDC,
		.read_id_fourth_byte = 0x95,
		.capacity  = 512,
		.timing = {
			.trp = 12,
			.trh = 10,
			.twp = 12,
			.twh = 10,
			.tcs = 20,
			.twhr = 80,
			.tcr_tar_trr = 20,
			.twb = 100,
			.trp_resp = 20,
			.tadl = 70,
		},
	},
};

static struct tegra_nand_platform medcom_wide_nand_data = {
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
		.platform_data = &medcom_wide_nand_data,
	},
};

static struct tegra_asoc_platform_data medcom_wide_audio_pdata = {
	.gpio_spkr_en = TEGRA_GPIO_SPKR_EN,
	.gpio_hp_det = TEGRA_GPIO_HP_DET,
	.gpio_hp_mute = -1,
	.gpio_int_mic_en = TEGRA_GPIO_INT_MIC_EN,
	.gpio_ext_mic_en = TEGRA_GPIO_EXT_MIC_EN,
	.i2s_param[HIFI_CODEC] = {
		.audio_port_id = 0,
		.is_i2s_master = 1,
		.i2s_mode = TEGRA_DAIFMT_I2S,
	},
	.i2s_param[BASEBAND] = {
		.audio_port_id = -1,
	},
	.i2s_param[BT_SCO] = {
		.audio_port_id = 3,
		.is_i2s_master = 1,
		.i2s_mode = TEGRA_DAIFMT_DSP_A,
	},
};

static struct platform_device medcom_wide_audio_device = {
	.name = "tegra-snd-wm8903",
	.id = 0,
	.dev = {
		.platform_data = &medcom_wide_audio_pdata,
	},
};

static struct tegra_i2c_platform_data medcom_wide_i2c1_platform_data = {
	.adapter_nr = 0,
	.bus_count = 1,
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

static struct tegra_i2c_platform_data medcom_wide_i2c2_platform_data = {
	.adapter_nr = 1,
	.bus_count = 2,
	.bus_clk_rate = { 100000, 100000 },
	.bus_mux = { &i2c2_ddc, &i2c2_gen2 },
	.bus_mux_len = { 1, 1 },
};

static struct tegra_i2c_platform_data medcom_wide_i2c3_platform_data = {
	.adapter_nr = 3,
	.bus_count = 1,
	.bus_clk_rate = { 400000, 0 },
};

static struct tegra_i2c_platform_data medcom_wide_dvc_platform_data = {
	.adapter_nr = 4,
	.bus_count = 1,
	.bus_clk_rate = { 400000, 0 },
	.is_dvc = true,
};

static struct wm8903_platform_data medcom_wide_wm8903_pdata = {
	.irq_active_low = 0,
	.micdet_cfg = 0,
	.micdet_delay = 100,
	.gpio_base = MEDCOM_WIDE_GPIO_WM8903(0),
	.gpio_cfg = {
		WM8903_GPIO_NO_CONFIG,
		WM8903_GPIO_NO_CONFIG,
		0,
		WM8903_GPIO_NO_CONFIG,
		WM8903_GPIO_NO_CONFIG,
	},
};

static struct adnp_platform_data medcom_adnp_pdata = {
	.gpio_base = ADNP_GPIO(0),
	.nr_gpios = 64,
	.irq_base = ADNP_GPIO_TO_IRQ(ADNP_GPIO(0)),
	.names = NULL,
};

#define SX8634_DEFAULT_SENSITIVITY	0x07
#define SX8634_DEFAULT_THRESHOLD	0x45

static struct sx8634_platform_data medcom_wide_keypad1_pdata = {
	.reset_gpio = ADNP_GPIO(11),
	.debounce = 3,
	.caps = {
		[1] = {
			.mode = SX8634_CAP_MODE_BUTTON,
			.keycode = KEY_INFO,
			.sensitivity = SX8634_DEFAULT_SENSITIVITY,
			.threshold = SX8634_DEFAULT_THRESHOLD,
		},
		[2] = {
			.mode = SX8634_CAP_MODE_BUTTON,
			.keycode = KEY_HELP,
			.sensitivity = SX8634_DEFAULT_SENSITIVITY,
			.threshold = SX8634_DEFAULT_THRESHOLD,
		},
		[3] = {
			.mode = SX8634_CAP_MODE_BUTTON,
			.keycode = KEY_COFFEE,
			.sensitivity = SX8634_DEFAULT_SENSITIVITY,
			.threshold = SX8634_DEFAULT_THRESHOLD,
		},
		[4] = {
			.mode = SX8634_CAP_MODE_BUTTON,
			.keycode = KEY_UNKNOWN,
			.sensitivity = SX8634_DEFAULT_SENSITIVITY,
			.threshold = SX8634_DEFAULT_THRESHOLD,
		},
		[5] = {
			.mode = SX8634_CAP_MODE_BUTTON,
			.keycode = KEY_BRIGHTNESSDOWN,
			.sensitivity = SX8634_DEFAULT_SENSITIVITY,
			.threshold = SX8634_DEFAULT_THRESHOLD,
		},
		[6] = {
			.mode = SX8634_CAP_MODE_BUTTON,
			.keycode = KEY_BRIGHTNESSUP,
			.sensitivity = SX8634_DEFAULT_SENSITIVITY,
			.threshold = SX8634_DEFAULT_THRESHOLD,
		},
	},
};

static struct sx8634_platform_data medcom_wide_keypad2_pdata = {
	.reset_gpio = ADNP_GPIO(10),
	.debounce = 3,
	.caps = {
		[1] = {
			.mode = SX8634_CAP_MODE_BUTTON,
			.keycode = KEY_DISPLAY_OFF,
			.sensitivity = SX8634_DEFAULT_SENSITIVITY,
			.threshold = SX8634_DEFAULT_THRESHOLD,
		},
		[2] = {
			.mode = SX8634_CAP_MODE_BUTTON,
			.keycode = KEY_DOWN,
			.sensitivity = SX8634_DEFAULT_SENSITIVITY,
			.threshold = SX8634_DEFAULT_THRESHOLD,
		},
		[3] = {
			.mode = SX8634_CAP_MODE_BUTTON,
			.keycode = KEY_UP,
			.sensitivity = SX8634_DEFAULT_SENSITIVITY,
			.threshold = SX8634_DEFAULT_THRESHOLD,
		},
		[4] = {
			.mode = SX8634_CAP_MODE_BUTTON,
			.keycode = KEY_MUTE,
			.sensitivity = SX8634_DEFAULT_SENSITIVITY,
			.threshold = SX8634_DEFAULT_THRESHOLD,
		},
		[5] = {
			.mode = SX8634_CAP_MODE_BUTTON,
			.keycode = KEY_VOLUMEUP,
			.sensitivity = SX8634_DEFAULT_SENSITIVITY,
			.threshold = SX8634_DEFAULT_THRESHOLD,
		},
		[6] = {
			.mode = SX8634_CAP_MODE_BUTTON,
			.keycode = KEY_VOLUMEDOWN,
			.sensitivity = SX8634_DEFAULT_SENSITIVITY,
			.threshold = SX8634_DEFAULT_THRESHOLD,
		},
	},
};

static struct i2c_board_info __initdata medcom_wide_i2c0_board_info[] = {
	{
		I2C_BOARD_INFO("wm8903", 0x1a),
		.platform_data = &medcom_wide_wm8903_pdata,
		.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_CDC_IRQ),
	}, {
		I2C_BOARD_INFO("gpio-adnp", 0x41),
		.platform_data = &medcom_adnp_pdata,
		.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_CPLD_IRQ),
	}, {
		I2C_BOARD_INFO("sx8634", 0x2b),
		.platform_data = &medcom_wide_keypad1_pdata,
		.irq = ADNP_IRQ(3),
	}, {
		I2C_BOARD_INFO("sx8634", 0x2c),
		.platform_data = &medcom_wide_keypad2_pdata,
		.irq = ADNP_IRQ(2),
	},
};

static struct nct1008_platform_data medcom_wide_nct1008_pdata = {
	.supported_hwrev = true,
	.ext_range = false,
	.conv_rate = 0x08,
	.offset = 0,
};

static struct i2c_board_info __initdata medcom_wide_dvc_board_info[] = {
	{
		I2C_BOARD_INFO("nct1008", 0x4c),
		.platform_data = &medcom_wide_nct1008_pdata,
	}
};

static void __init medcom_wide_i2c_init(void)
{
	tegra_i2c_device1.dev.platform_data = &medcom_wide_i2c1_platform_data;
	tegra_i2c_device2.dev.platform_data = &medcom_wide_i2c2_platform_data;
	tegra_i2c_device3.dev.platform_data = &medcom_wide_i2c3_platform_data;
	tegra_i2c_device4.dev.platform_data = &medcom_wide_dvc_platform_data;

	platform_device_register(&tegra_i2c_device1);
	platform_device_register(&tegra_i2c_device2);
	platform_device_register(&tegra_i2c_device3);
	platform_device_register(&tegra_i2c_device4);

	i2c_register_board_info(0, medcom_wide_i2c0_board_info,
				ARRAY_SIZE(medcom_wide_i2c0_board_info));

	i2c_register_board_info(4, medcom_wide_dvc_board_info,
				ARRAY_SIZE(medcom_wide_dvc_board_info));
}

static struct plat_serial8250_port modem_uart_platform_data[] = {
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

static struct platform_device modem_uartc_device = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM1,
	.dev = {
		.platform_data = modem_uart_platform_data,
	},
};

static struct platform_device *medcom_wide_uart_devices[] __initdata = {
	&tegra_uartd_device,
	&modem_uartc_device,
};

static struct uart_clk_parent uart_parent_clk[] __initdata = {
	[0] = { .name = "pll_p" },
	[1] = { .name = "pll_m" },
	[2] = { .name = "clk_m" },
};

static struct tegra_uart_platform_data medcom_wide_uart_pdata;

static void __init uart_debug_init(void)
{
	struct plat_serial8250_port *port = debug_uartd_device.dev.platform_data;
	unsigned long rate = port->uartclk;
	struct clk *c;

	/* UARTD is the debug port. */
	pr_info("Selecting UARTD as the debug console\n");
	medcom_wide_uart_devices[0] = &debug_uartd_device;
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

static void __init medcom_wide_uart_init(void)
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

	medcom_wide_uart_pdata.parent_clk_list = uart_parent_clk;
	medcom_wide_uart_pdata.parent_clk_count = ARRAY_SIZE(uart_parent_clk);
	tegra_uartd_device.dev.platform_data = &medcom_wide_uart_pdata;

	/* Register low speed only if it is selected */
	if (!is_tegra_debug_uartport_hs())
		uart_debug_init();

	platform_add_devices(medcom_wide_uart_devices,
			     ARRAY_SIZE(medcom_wide_uart_devices));
}

static struct platform_device *medcom_wide_devices[] __initdata = {
	&tegra_sdhci_device4,
	&tegra_i2s_device1,
	&tegra_i2s_device2,
	&tegra_spdif_device,
	&tegra_das_device,
	&spdif_dit_device,
	&bluetooth_dit_device,
	&tegra_pcm_device,
	&medcom_wide_audio_device,
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

static void __init medcom_wide_fixup(struct machine_desc *desc,
				     struct tag *tags, char **cmdline,
				     struct meminfo *mi)
{
	mi->nr_banks = 1;
	mi->bank[0].start = PHYS_OFFSET;
	mi->bank[0].size = 448 * SZ_1M;
}

static __initdata struct tegra_clk_init_table medcom_wide_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "uarta",	"pll_p",	216000000,	false },
	{ "uartb",	"pll_p",	216000000,	false },
	{ "uartc",	"pll_p",	216000000,	true  }, /* modem */
	{ "uartd",	"pll_p",	216000000,	true  }, /* debug */
	{ "uarte",	"pll_p",	216000000,	false },
	{ "i2s1",	"pll_a_out0",	0,		false },
	{ "spdif_out",	"pll_a_out0",	0,		false },
	{ "sdmmc4",	"clk_m",	48000000,	true  },
	{ "ndflash",	"pll_p",	108000000,	true  },
	{ "pwm",	"clk_32k",	32768,		false },
	{ NULL,		NULL,		0,		false },
};

static struct tegra_sdhci_platform_data sdhci_pdata4 = {
	.cd_gpio = TEGRA_GPIO_SD4_CD,
	.wp_gpio = TEGRA_GPIO_SD4_WP,
	.power_gpio = TEGRA_GPIO_SD4_POWER,
	.is_8bit = 1,
};

static void __init medcom_wide_init(void)
{
	tegra_clk_init_from_table(medcom_wide_clk_init_table);

	medcom_wide_pinmux_init();
	medcom_wide_uart_init();

	tegra_sdhci_device4.dev.platform_data = &sdhci_pdata4;
	tegra_udc_device.dev.platform_data = &tegra_udc_pdata;
	tegra_ehci3_device.dev.platform_data = &tegra_ehci3_utmi_pdata;

	platform_add_devices(medcom_wide_devices,
			     ARRAY_SIZE(medcom_wide_devices));
	medcom_wide_i2c_init();
	medcom_wide_regulator_init();
	medcom_wide_suspend_init();
	medcom_wide_panel_init();
}

void __init medcom_wide_reserve(void)
{
	if (memblock_reserve(0x0, 4096) < 0)
		pr_warn("Cannot reserve first 4K of memory for safety\n");

	tegra_reserve(SZ_128M, SZ_8M, SZ_16M);
}

MACHINE_START(MEDCOM_WIDE, "medcom-wide")
	.boot_params    = 0x00000100,
	.fixup          = medcom_wide_fixup,
	.map_io         = tegra_map_common_io,
	.reserve        = medcom_wide_reserve,
	.init_early     = tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine   = medcom_wide_init,
MACHINE_END
