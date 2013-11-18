/*
 * arch/arm/mach-tegra/tamonten-adnp.c
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
#include <linux/i2c/adnp.h>

#include "devices.h"
#include "board.h"
#include "tamonten-adnp.h"

static struct adnp_platform_data adnp_pdata = {
	.gpio_base = BOARD_ADNP_GPIO_BASE,
	.nr_gpios = BOARD_ADNP_GPIO_COUNT,
	.irq_base = BOARD_ADNP_IRQ_BASE,
	.names = NULL,
};

static struct i2c_board_info __initdata adnp_board_info = {
	I2C_BOARD_INFO("gpio-adnp", 0x41),
	.platform_data = &adnp_pdata,
};

void __init tamonten_adnp_init(int i2c_bus, int irq)
{
	adnp_board_info.irq = irq;
	i2c_register_board_info(i2c_bus, &adnp_board_info, 1);
}
