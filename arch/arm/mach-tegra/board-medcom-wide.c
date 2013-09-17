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
#include <linux/nct1008.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/i2c/adnp.h>
#include <linux/input/sx8634.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>

#include "board.h"
#include "board-medcom-wide.h"
#include "gpio-names.h"

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
	i2c_register_board_info(0, medcom_wide_i2c0_board_info,
				ARRAY_SIZE(medcom_wide_i2c0_board_info));

	i2c_register_board_info(4, medcom_wide_dvc_board_info,
				ARRAY_SIZE(medcom_wide_dvc_board_info));
}

static void __init medcom_wide_init(void)
{
	tamonten_init();
	tamonten_wm8903_init();

	medcom_wide_i2c_init();
	medcom_wide_panel_init();
}

MACHINE_START(MEDCOM_WIDE, "medcom-wide")
	.boot_params    = 0x00000100,
	.fixup          = tamonten_fixup,
	.map_io         = tegra_map_common_io,
	.reserve        = tamonten_reserve,
	.init_early     = tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine   = medcom_wide_init,
MACHINE_END
