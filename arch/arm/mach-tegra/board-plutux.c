/*
 * arch/arm/mach-tegra/board-plutux.c
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
#include <linux/gpio.h>
#include <linux/i2c.h>

#include <sound/wm8903.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>

#include <mach/tegra_asoc_pdata.h>

#include "board.h"
#include "board-plutux.h"
#include "gpio-names.h"

static struct tegra_asoc_platform_data plutux_audio_pdata = {
	.gpio_spkr_en = TEGRA_GPIO_SPKR_EN,
	.gpio_hp_det = COM_GPIO_HP_DET,
	.gpio_hp_mute = -1,
	.gpio_int_mic_en = COM_GPIO_INT_MIC_EN,
	.gpio_ext_mic_en = COM_GPIO_EXT_MIC_EN,
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

static struct platform_device plutux_audio_device = {
	.name = "tegra-snd-wm8903",
	.id = 0,
	.dev = {
		.platform_data = &plutux_audio_pdata,
	},
};

static struct wm8903_platform_data plutux_wm8903_pdata = {
	.irq_active_low = 0,
	.micdet_cfg = 0,
	.micdet_delay = 100,
	.gpio_base = PLUTUX_GPIO_WM8903(0),
	.gpio_cfg = {
		WM8903_GPIO_NO_CONFIG,
		WM8903_GPIO_NO_CONFIG,
		0,
		WM8903_GPIO_NO_CONFIG,
		WM8903_GPIO_NO_CONFIG,
	},
};

static struct i2c_board_info __initdata wm8903_board_info = {
	I2C_BOARD_INFO("wm8903", 0x1a),
	.platform_data = &plutux_wm8903_pdata,
	.irq = TEGRA_GPIO_TO_IRQ(COM_GPIO_CDC_IRQ),
};

static void __init plutux_i2c_init(void)
{
	i2c_register_board_info(0, &wm8903_board_info, 1);
}

static struct platform_device *plutux_devices[] __initdata = {
	&plutux_audio_device,
};

static void __init plutux_init(void)
{
	tamonten_init();

	platform_add_devices(plutux_devices,
			     ARRAY_SIZE(plutux_devices));
	plutux_i2c_init();
	plutux_hdmi_init();
}

MACHINE_START(PLUTUX, "plutux")
	.boot_params    = 0x00000100,
	.fixup          = tamonten_fixup,
	.map_io         = tegra_map_common_io,
	.reserve        = tamonten_reserve,
	.init_early     = tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine   = plutux_init,
MACHINE_END
