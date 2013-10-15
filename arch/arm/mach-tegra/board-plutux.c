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

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>

#include "board.h"
#include "board-plutux.h"

static void __init plutux_init(void)
{
	tamonten_init();
	tamonten_wm8903_init();

	plutux_hdmi_init();
}

MACHINE_START(PLUTUX, "plutux")
	.boot_params    = TAMONTEN_BOOT_PARAMS,
	.fixup          = tamonten_fixup,
	.map_io         = tegra_map_common_io,
	.reserve        = tamonten_reserve,
	.init_early     = tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine   = plutux_init,
MACHINE_END
