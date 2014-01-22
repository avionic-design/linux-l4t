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

#define CPLD_WIP_MASK            0x80000000
#define CPLD_WIP_SHIFT           31

#define CPLD_PLATFORM_ID_MASK    0x0FFF0000
#define CPLD_PLATFORM_ID_SHIFT   16

#define CPLD_MAJOR_VERSION_MASK  0x0000FF00
#define CPLD_MAJOR_VERSION_SHIFT 8

#define CPLD_MINOR_VERSION_MASK  0x000000FF
#define CPLD_MINOR_VERSION_SHIFT 0

static u32 *platform_id;
static int platform_id_count;

static int machxo_check(u32 devid, u32 traceid,
			u32 sram_usercode, u32 cfg_usercode)
{
	int major, minor;

	/* Check if the CPLD has been programmed */
	if (sram_usercode == 0 || cfg_usercode == 0) {
		pr_err("CPLD isn't programmed!\n");
		return -ENODEV;
	}

	/* Warn if the CFG usercode doesn't match the SRAM one */
	if ((sram_usercode & ~CPLD_WIP_MASK) !=
		(cfg_usercode & ~CPLD_WIP_MASK))
		pr_warn("CPLD CFG usercode doesn't match SRAM usercode!\n");

	/* If a platform ID has been set enforce it */
	if (platform_id && platform_id_count > 0) {
		u32 pid = (cfg_usercode & CPLD_PLATFORM_ID_MASK)
			>> CPLD_PLATFORM_ID_SHIFT;
		int i;
		for (i = 0; i < platform_id_count; i += 1)
			if (platform_id[i] == pid)
				break;
		if (i >= platform_id_count) {
			pr_err("CPLD has an unknown plaform ID\n");
			return -ENODEV;
		}
	}

	/* Show the CPLD version */
	major = (cfg_usercode & CPLD_MAJOR_VERSION_MASK)
		>> CPLD_MAJOR_VERSION_SHIFT;
	minor = (cfg_usercode & CPLD_MINOR_VERSION_MASK)
		>> CPLD_MINOR_VERSION_SHIFT;
	pr_info("Found CPLD version %d.%d (TraceID: %08x)\n",
		major, minor, traceid);

	/* Warn if the CPLD isn't a release version */
	if (cfg_usercode & CPLD_WIP_MASK) {
		pr_warn("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
		pr_warn("!!!!!!!!!!! Unreleased CPLD version !!!!!!!!!!!\n");
		pr_warn("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
	}

	return 0;
}

static struct adnp_platform_data adnp_pdata = {
	.gpio_base = BOARD_ADNP_GPIO_BASE,
	.nr_gpios = BOARD_ADNP_GPIO_COUNT,
	.irq_base = BOARD_ADNP_IRQ_BASE,
	.names = NULL,
	.machxo_check = machxo_check,
};

static struct i2c_board_info __initdata adnp_board_info = {
	I2C_BOARD_INFO("gpio-adnp", 0x41),
	.platform_data = &adnp_pdata,
};

void __init tamonten_adnp_init(int i2c_bus, int irq,
			u32 *pid, int pid_count)
{
	platform_id = pid;
	platform_id_count = pid_count;
	adnp_board_info.irq = irq;
	i2c_register_board_info(i2c_bus, &adnp_board_info, 1);
}
