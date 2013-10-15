/*
 * arch/arm/mach-tegra/tamonten-wm8903.c
 *
 * Copyright (C) 2013 Avionic Design GmbH
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
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/i2c.h>

#include <sound/wm8903.h>

#include <mach/tegra_asoc_pdata.h>

#include "com-tamonten.h"
#include "tamonten-wm8903.h"
#include "gpio-names.h"

static struct tegra_asoc_platform_data tamonten_audio_pdata = {
	.gpio_spkr_en = BOARD_GPIO_SPKR_EN,
	.gpio_hp_det = COM_GPIO_HP_DET,
	.gpio_hp_mute = -1,
	.gpio_int_mic_en = -1,
	.gpio_ext_mic_en = COM_GPIO_EXT_MIC_EN,
	.i2s_param[HIFI_CODEC] = {
		.audio_port_id = 0,
		.is_i2s_master = 1,
		.i2s_mode = TEGRA_DAIFMT_I2S,
	},
	.i2s_param[BASEBAND] = {
		.audio_port_id = -1,
	},
};

static struct platform_device tamonten_audio_device = {
	.name = "tegra-snd-wm8903",
	.id = 0,
	.dev = {
		.platform_data = &tamonten_audio_pdata,
	},
};

static struct wm8903_platform_data tamonten_wm8903_pdata = {
	.irq_active_low = 0,
	.micdet_cfg = 0,
	.micdet_delay = 100,
	.gpio_base = BOARD_GPIO_WM8903(0),
	.gpio_cfg = {
		WM8903_GPn_FN_DMIC_LR_CLK_OUTPUT << WM8903_GP1_FN_SHIFT,
		WM8903_GPn_FN_DMIC_LR_CLK_OUTPUT << WM8903_GP2_FN_SHIFT |
			WM8903_GP2_DIR_MASK,
		0,
		WM8903_GPIO_NO_CONFIG,
		WM8903_GPIO_NO_CONFIG,
	},
};

static struct i2c_board_info __initdata wm8903_board_info = {
	I2C_BOARD_INFO("wm8903", 0x1a),
	.platform_data = &tamonten_wm8903_pdata,
	.irq = TEGRA_GPIO_TO_IRQ(COM_GPIO_CDC_IRQ),
};

void __init tamonten_wm8903_init(void)
{
	i2c_register_board_info(COM_I2C_BUS_GEN1,
				&wm8903_board_info, 1);
	platform_device_register(&tamonten_audio_device);
}
