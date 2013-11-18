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
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/i2c/adnp.h>
#include <linux/input/sx8634.h>

#include <media/soc_camera.h>
#include <media/tegra_v4l2_camera.h>
#include <media/tvp5150.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>

#include "devices.h"
#include "board.h"
#include "board-medcom-wide.h"

static struct adnp_platform_data medcom_adnp_pdata = {
	.gpio_base = BOARD_GPIO(ADNP, 0),
	.nr_gpios = BOARD_ADNP_GPIO_COUNT,
	.irq_base = BOARD_IRQ(ADNP, 0),
	.names = NULL,
};

#define SX8634_DEFAULT_SENSITIVITY	0x07
#define SX8634_DEFAULT_THRESHOLD	0x45

static struct sx8634_platform_data medcom_wide_keypad1_pdata = {
	.reset_gpio = BOARD_GPIO(ADNP, 11),
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
	.reset_gpio = BOARD_GPIO(ADNP, 10),
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
		.irq = MEDCOM_WIDE_IRQ_CPLD,
	}, {
		I2C_BOARD_INFO("sx8634", 0x2b),
		.platform_data = &medcom_wide_keypad1_pdata,
		.irq = BOARD_IRQ(ADNP, 3),
	}, {
		I2C_BOARD_INFO("sx8634", 0x2c),
		.platform_data = &medcom_wide_keypad2_pdata,
		.irq = BOARD_IRQ(ADNP, 2),
	},
};

static void __init medcom_wide_i2c_init(void)
{
	i2c_register_board_info(COM_I2C_BUS_GEN1,
				medcom_wide_i2c0_board_info,
				ARRAY_SIZE(medcom_wide_i2c0_board_info));
}

#ifdef CONFIG_VIDEO_TEGRA
static struct i2c_board_info medcom_wide_camera_bus_board_info[] = {
	{
		I2C_BOARD_INFO("tvp5150", 0x5d),
	},
};

static struct soc_camera_input medcom_wide_camera_inputs[] = {
	{
		.input = {
			.name = "Internal",
			.type = V4L2_INPUT_TYPE_CAMERA,
		},
		.sensor_input = TVP5150_COMPOSITE1,
		.sensor_output = TVP5150_NORMAL,
	},
	{
		.input = {
			.name = "External",
			.type = V4L2_INPUT_TYPE_CAMERA,
		},
		.sensor_input = TVP5150_COMPOSITE0,
		.sensor_output = TVP5150_NORMAL,
	},
};

static struct soc_camera_link medcom_wide_camera_iclink = {
	.bus_id = -1,
	.i2c_adapter_id = 0,
	.board_info = medcom_wide_camera_bus_board_info,
	.inputs = medcom_wide_camera_inputs,
	.input_count = ARRAY_SIZE(medcom_wide_camera_inputs),
};

static struct platform_device medcom_wide_soc_camera = {
	.name = "soc-camera-pdrv",
	.id = 0,
	.dev = {
		.platform_data = &medcom_wide_camera_iclink,
	},
};

static struct tegra_camera_platform_data medcom_wide_camera_platform_data = {
	.flip_v = 0,
	.flip_h = 0,
	.port = TEGRA_CAMERA_PORT_VIP,
};

static void __init medcom_wide_camera_init(void)
{
	tegra_camera_device.dev.platform_data = &medcom_wide_camera_platform_data;
	nvhost_device_register(&tegra_camera_device);
	platform_device_register(&medcom_wide_soc_camera);
}
#else
static void __init medcom_wide_camera_init(void) {}
#endif /* CONFIG_VIDEO_TEGRA */

static void __init medcom_wide_init(void)
{
	tamonten_init();
	tamonten_wm8903_init();

	medcom_wide_i2c_init();
	medcom_wide_camera_init();
	medcom_wide_panel_init();
}

static const char *medcom_wide_dt_board_compat[] = {
	"avionic-design,medcom-wide",
	NULL
};

MACHINE_START(MEDCOM_WIDE, "medcom-wide")
	.boot_params    = TAMONTEN_BOOT_PARAMS,
	.fixup          = tamonten_fixup,
	.map_io         = tegra_map_common_io,
	.reserve        = tamonten_reserve,
	.init_early     = tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine   = medcom_wide_init,
	.dt_compat      = medcom_wide_dt_board_compat,
MACHINE_END
