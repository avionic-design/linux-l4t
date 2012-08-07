/*
 * arch/arm/mach-tegra/board-medcom.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (C) 2011 NVIDIA, Inc.
 * Copyright (C) 2012 Avionic Design GmbH
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

#include <sound/wm8903.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>

#include <mach/tegra_wm8903_pdata.h>
#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/sdhci.h>
#include <mach/nand.h>
#include <mach/clk.h>
#include <mach/usb_phy.h>

#include "clock.h"
#include "board.h"
#include "board-medcom.h"
#include "clock.h"
#include "devices.h"
#include "gpio-names.h"
#include "pm.h"

/* NVidia bootloader tags */
#define ATAG_NVIDIA		0x41000801

#define ATAG_NVIDIA_RM			0x1
#define ATAG_NVIDIA_DISPLAY		0x2
#define ATAG_NVIDIA_FRAMEBUFFER		0x3
#define ATAG_NVIDIA_CHIPSHMOO		0x4
#define ATAG_NVIDIA_CHIPSHMOOPHYS	0x5
#define ATAG_NVIDIA_PRESERVED_MEM_0	0x10000
#define ATAG_NVIDIA_PRESERVED_MEM_N	2
#define ATAG_NVIDIA_FORCE_32		0x7fffffff

struct tag_tegra {
	__u32 bootarg_key;
	__u32 bootarg_len;
	char bootarg[1];
};

static int __init parse_tag_nvidia(const struct tag *tag)
{

	return 0;
}
__tagtable(ATAG_NVIDIA, parse_tag_nvidia);

static struct tegra_utmip_config utmi_phy_config = {
	.hssync_start_delay = 0,
	.idle_wait_delay = 17,
	.elastic_limit = 16,
	.term_range_adj = 6,
	.xcvr_setup = 9,
	.xcvr_lsfslew = 2,
	.xcvr_lsrslew = 2,
};

static struct tegra_ehci_platform_data tegra_ehci_pdata = {
	.phy_config = &utmi_phy_config,
	.operating_mode = TEGRA_USB_HOST,
	.power_down_on_bus_suspend = 1,
};

static struct tegra_nand_chip_parms nand_chip_parms[] = {
	/* Samsung K5E2G1GACM */
	[0] = {
	       .vendor_id = 0xEC,
	       .device_id = 0xAA,
	       .read_id_fourth_byte = 0x15,
	       .capacity  = 256,
	       .timing = {
			  .trp = 21,
			  .trh = 15,
			  .twp = 21,
			  .twh = 15,
			  .tcs = 31,
			  .twhr = 60,
			  .tcr_tar_trr = 20,
			  .twb = 100,
			  .trp_resp = 30,
			  .tadl = 100,
			  },
	       },
	/* Hynix H5PS1GB3EFR */
	[1] = {
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

struct tegra_nand_platform medcom_nand_data = {
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

struct platform_device tegra_nand_device = {
	.name = "tegra_nand",
	.id = -1,
	.num_resources = ARRAY_SIZE(resources_nand),
	.resource = resources_nand,
	.dev = {
		.platform_data = &medcom_nand_data,
		},
};

static struct gpio_keys_button medcom_gpio_keys_buttons[] = {
	{
		.code		= KEY_POWER,
		.gpio		= TEGRA_GPIO_POWERKEY,
		.active_low	= 1,
		.desc		= "Power",
		.type		= EV_KEY,
		.wakeup		= 1,
	},
};

static struct gpio_keys_platform_data medcom_gpio_keys = {
	.buttons	= medcom_gpio_keys_buttons,
	.nbuttons	= ARRAY_SIZE(medcom_gpio_keys_buttons),
};

static struct platform_device medcom_gpio_keys_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.dev		= {
		.platform_data = &medcom_gpio_keys,
	}
};

static void medcom_keys_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(medcom_gpio_keys_buttons); i++)
		tegra_gpio_enable(medcom_gpio_keys_buttons[i].gpio);
}

static struct tegra_wm8903_platform_data medcom_audio_pdata = {
	.gpio_spkr_en		= TEGRA_GPIO_SPKR_EN,
	.gpio_hp_det		= TEGRA_GPIO_HP_DET,
	.gpio_hp_mute		= -1,
	.gpio_int_mic_en	= TEGRA_GPIO_INT_MIC_EN,
	.gpio_ext_mic_en	= TEGRA_GPIO_EXT_MIC_EN,
};

static struct platform_device medcom_audio_device = {
	.name	= "tegra-snd-wm8903",
	.id	= 0,
	.dev	= {
		.platform_data  = &medcom_audio_pdata,
	},
};

static struct tegra_i2c_platform_data medcom_i2c1_platform_data = {
	.adapter_nr     = 0,
	.bus_count      = 1,
	.bus_clk_rate   = { 100000, 0 },
};

static const struct tegra_pingroup_config i2c2_ddc = {
	.pingroup       = TEGRA_PINGROUP_DDC,
	.func           = TEGRA_MUX_I2C2,
};

static const struct tegra_pingroup_config i2c2_gen2 = {
	.pingroup       = TEGRA_PINGROUP_PTA,
	.func           = TEGRA_MUX_I2C2,
};

static struct tegra_i2c_platform_data medcom_i2c2_platform_data = {
	.adapter_nr     = 1,
	.bus_count      = 2,
	.bus_clk_rate   = { 100000, 100000 },
	.bus_mux        = { &i2c2_ddc, &i2c2_gen2 },
	.bus_mux_len    = { 1, 1 },
};

static struct tegra_i2c_platform_data medcom_i2c3_platform_data = {
	.adapter_nr     = 3,
	.bus_count      = 1,
	.bus_clk_rate   = { 400000, 0 },
};

static struct tegra_i2c_platform_data medcom_dvc_platform_data = {
	.adapter_nr     = 4,
	.bus_count      = 1,
	.bus_clk_rate   = { 400000, 0 },
	.is_dvc         = true,
};

static struct wm8903_platform_data medcom_wm8903_pdata = {
	.irq_active_low = 0,
	.micdet_cfg = 0,
	.micdet_delay = 100,
	.gpio_base = MEDCOM_GPIO_WM8903(0),
	.gpio_cfg = {
		WM8903_GPIO_NO_CONFIG,
		WM8903_GPIO_NO_CONFIG,
		0,
		WM8903_GPIO_NO_CONFIG,
		WM8903_GPIO_NO_CONFIG,
	},
};

static struct adnp_platform_data medcom_adnp_pdata = {
	.gpio_base = -1,
	.nr_gpios = 64,
	.irq_base = INT_BOARD_BASE,
	.names = NULL,
};


#define MEDCOM_GPIO_KEYPAD1 MEDCOM_GPIO_ADNP(3)
#define MEDCOM_GPIO_KEYPAD2 MEDCOM_GPIO_ADNP(2)

#define SX8634_DEFAULT_SENSITIVITY	0x07
#define SX8634_DEFAULT_THRESHOLD	0x45

static struct sx8634_platform_data medcom_keypad1_pdata = {
	.caps = {
		[1] = {
			.mode = SX8634_CAP_MODE_BUTTON,
			.keycode = KEY_MENU,
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


static struct sx8634_platform_data medcom_keypad2_pdata = {
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

static struct i2c_board_info __initdata medcom_i2c0_board_info[] = {
	{
		I2C_BOARD_INFO("wm8903", 0x1a),
		.platform_data = &medcom_wm8903_pdata,
		.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_CDC_IRQ),
	}, {
		I2C_BOARD_INFO("sx8634", 0x2b),
		.platform_data = &medcom_keypad1_pdata,
		.irq = TEGRA_GPIO_TO_IRQ(MEDCOM_GPIO_KEYPAD1),
	} , {
		I2C_BOARD_INFO("sx8634", 0x2c),
		.platform_data = &medcom_keypad2_pdata,
		.irq = TEGRA_GPIO_TO_IRQ(MEDCOM_GPIO_KEYPAD2),
	} , {
		I2C_BOARD_INFO("gpio-adnp", 0x41),
		.platform_data = &medcom_adnp_pdata,
		.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_CPLD_IRQ),
	},
};

static void __init medcom_i2c_init(void)
{
	tegra_i2c_device1.dev.platform_data = &medcom_i2c1_platform_data;
	tegra_i2c_device2.dev.platform_data = &medcom_i2c2_platform_data;
	tegra_i2c_device3.dev.platform_data = &medcom_i2c3_platform_data;
	tegra_i2c_device4.dev.platform_data = &medcom_dvc_platform_data;

	platform_device_register(&tegra_i2c_device1);
	platform_device_register(&tegra_i2c_device2);
	platform_device_register(&tegra_i2c_device3);
	platform_device_register(&tegra_i2c_device4);

	i2c_register_board_info(0, medcom_i2c0_board_info,
			ARRAY_SIZE(medcom_i2c0_board_info));
}

/* PDA power */
static struct pda_power_pdata pda_power_pdata = {
};

static struct platform_device pda_power_device = {
	.name   = "pda_power",
	.id     = -1,
	.dev    = {
		.platform_data  = &pda_power_pdata,
	},
};

static void medcom_debug_uart_init(void)
{
	struct clk *c;

	debug_uart_clk = clk_get_sys("serial8250.0", "uartd");
	debug_uart_port_base = ((struct plat_serial8250_port *)(
		debug_uartd_device.dev.platform_data))->mapbase;

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
		pr_err("Not getting the clock %s for debug console\n",
					debug_uart_clk->name);
	}
	return;
}

static struct platform_device *medcom_devices[] __initdata = {
	&tegra_uartc_device, /* modem */
	&debug_uartd_device,
	&tegra_sdhci_device1,
	&tegra_sdhci_device2,
	&tegra_sdhci_device4,
	&tegra_i2s_device1,
	&tegra_i2s_device2,
	&tegra_spdif_device,
	&tegra_das_device,
	&spdif_dit_device,
	&bluetooth_dit_device,
	&tegra_pcm_device,
	&medcom_audio_device,
	&tegra_pmu_device,
	&tegra_nand_device,
	&tegra_udc_device,
	&medcom_gpio_keys_device,
	&pda_power_device,
	&tegra_ehci3_device,
	&tegra_spi_device1,
	&tegra_spi_device2,
	&tegra_spi_device3,
	&tegra_spi_device4,
	&tegra_gart_device,
	&tegra_avp_device,
};

static void __init tegra_medcom_fixup(struct machine_desc *desc,
	struct tag *tags, char **cmdline, struct meminfo *mi)
{
	mi->nr_banks = 2;
	mi->bank[0].start = PHYS_OFFSET;
	mi->bank[0].size = 448 * SZ_1M;
	mi->bank[1].start = SZ_512M;
	mi->bank[1].size = SZ_512M;
}

static __initdata struct tegra_clk_init_table medcom_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "uartc",	"pll_p",	216000000,	true },
	{ "uartd",	"pll_p",	216000000,	true },
	{ "i2s1",	"pll_a_out0",	0,		false},
	{ "i2c1-fast",	"pll_p_out3",	72000000,	true},
	{ "i2c2-fast",	"pll_p_out3",	72000000,	true},
	{ "i2c3-fast",	"pll_p_out3",	72000000,	true},
	{ "dvc-fast",	"pll_p_out3",	72000000,	true},
	{ "spdif_out",	"pll_a_out0",	0,		false},
	{ "sdmmc1",	"clk_m",	48000000,	true },
	{ "sdmmc2",	"clk_m",	48000000,	true },
	{ "sdmmc4",	"clk_m",	48000000,	true },
	{ "i2c1",	"clk_m",	3000000,	false},
	{ "i2c2",	"clk_m",	3000000,	false},
	{ "i2c3",	"clk_m",	3000000,	false},
	{ "dvc",	"clk_m",	3000000,	false},
	{ "ndflash",	"pll_p",	108000000,	true},
	{ "pwm",	"clk_32k",	32768,		false},
	{ NULL,		NULL,		0,		0},
};


static struct tegra_sdhci_platform_data sdhci_pdata1 = {
	.cd_gpio	= -1,
	.wp_gpio	= -1,
	.power_gpio	= -1,
};

static struct tegra_sdhci_platform_data sdhci_pdata2 = {
	.cd_gpio	= TEGRA_GPIO_SD2_CD,
	.wp_gpio	= TEGRA_GPIO_SD2_WP,
	.power_gpio	= TEGRA_GPIO_SD2_POWER,
};

static struct tegra_sdhci_platform_data sdhci_pdata4 = {
	.cd_gpio	= TEGRA_GPIO_SD4_CD,
	.wp_gpio	= TEGRA_GPIO_SD4_WP,
	.power_gpio	= TEGRA_GPIO_SD4_POWER,
	.is_8bit	= 1,
};

static void __init tegra_medcom_init(void)
{
	tegra_clk_init_from_table(medcom_clk_init_table);

	medcom_pinmux_init();

	medcom_keys_init();

	medcom_debug_uart_init();

	printk("Initializing tegra medcom platform\n");

	tegra_sdhci_device1.dev.platform_data = &sdhci_pdata1;
	tegra_sdhci_device2.dev.platform_data = &sdhci_pdata2;
	tegra_sdhci_device4.dev.platform_data = &sdhci_pdata4;

	tegra_ehci3_device.dev.platform_data = &tegra_ehci_pdata;

	platform_add_devices(medcom_devices, ARRAY_SIZE(medcom_devices));
	medcom_i2c_init();
	medcom_regulator_init();
	medcom_suspend_init();
	medcom_panel_init();
	medcom_pcie_init();
}

void __init tegra_medcom_reserve(void)
{
	if (memblock_reserve(0x0, 4096) < 0)
		pr_warn("Cannot reserve first 4K of memory for safety\n");

	tegra_reserve(SZ_128M, SZ_8M, SZ_16M);
}

MACHINE_START(MEDCOM, "medcom")
	.boot_params  = 0x00000100,
	.fixup		= tegra_medcom_fixup,
	.map_io         = tegra_map_common_io,
	.reserve        = tegra_medcom_reserve,
	.init_early	= tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine   = tegra_medcom_init,
MACHINE_END
