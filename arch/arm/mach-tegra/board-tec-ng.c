/*
 * arch/arm/mach-tegra/board-tec-ng.c
 *
 * Copyright (c) 2013, Avionic Design GmbH
 * Copyright (c) 2013, Julian Scheel <julian@jusst.de>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>

#include "board-tec-ng.h"
#include "board.h"

static void __init tec_ng_init(void)
{
	tamonten_init();
	tamonten_wm8903_init();
	tamonten_adnp_init(COM_I2C_BUS_GEN1, TEC_NG_IRQ_CPLD);
	tamonten_tsc2007_init(COM_I2C_BUS_GEN2,
			TEC_NG_GPIO_TOUCH_IRQ, TEC_NG_IRQ_TOUCH);

	tec_ng_panel_init();
}


static const char *tec_ng_dt_board_compat[] = {
	"avionic-design,tec_ng",
	NULL
};

MACHINE_START(TEC_NG, "tec_ng")
	.boot_params	= TAMONTEN_BOOT_PARAMS,
	.fixup		= tamonten_fixup,
	.map_io		= tegra_map_common_io,
	.reserve	= tamonten_reserve,
	.init_early	= tegra_init_early,
	.init_irq	= tegra_init_irq,
	.timer		= &tegra_timer,
	.init_machine	= tec_ng_init,
	.dt_compat	= tec_ng_dt_board_compat,
MACHINE_END
