/*
 * arch/arm/mach-tegra/tamonten-tsc2007.c
 *
 * Copyright (C) 2013, Avionic Design GmbH
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/hardirq.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/i2c/tsc2007.h>

static int touch_gpio = -1;

static int tsc2007_get_pendown(void)
{
	/*
	 * When called from interrupt context, we can't call gpio_get_value()
	 * as it will sleep (the GPIO controller is an I2C slave). This only
	 * happens in the tsc2007 hard-IRQ handler which uses get_pendown()
	 * to determine if an interrupt is actually pending. On the SKIDATA
	 * Tamonten Carrier the interrupt line is not shared so it is safe to
	 * return 1 for this case.
	 */
	if (gpio_cansleep(touch_gpio) && in_interrupt())
		return 1;

	return !gpio_get_value_cansleep(touch_gpio);
}

static int tsc2007_init(void)
{
	int err;

	err = gpio_request(touch_gpio, "touchscreen");
	if (err < 0)
		return err;

	err = gpio_direction_input(touch_gpio);
	if (err < 0)
		return err;

	return 0;
}

static void tsc2007_exit(void)
{
	gpio_free(touch_gpio);
}

static struct tsc2007_platform_data stc_tsc2007_pdata = {
	.model = 2007,
	.x_plate_ohms = 50,
	.max_rt = 110,
	.poll_delay = 10,
	.poll_period = 20,
	.fuzzx = 16,
	.fuzzy = 16,
	.fuzzz = 16,
	.get_pendown_state = tsc2007_get_pendown,
	.init_platform_hw = tsc2007_init,
	.exit_platform_hw = tsc2007_exit,
};

static struct i2c_board_info tsc2007_board_info __initdata = {
	I2C_BOARD_INFO("tsc2007", 0x48),
	.platform_data = &stc_tsc2007_pdata,
	.irq = -1,
};

void __init tamonten_tsc2007_init(int i2c_bus, int gpio, int irq)
{
	touch_gpio = gpio;
	tsc2007_board_info.irq = irq;
	i2c_register_board_info(i2c_bus, &tsc2007_board_info, 1);
}
