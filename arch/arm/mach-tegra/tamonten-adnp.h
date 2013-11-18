/*
 * arch/arm/mach-tegra/tamonten-adnp.h
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

#ifndef _MACH_TEGRA_TAMONTEN_ADNP_H
#define _MACH_TEGRA_TAMONTEN_ADNP_H

#include "tamonten-board.h"

#ifdef CONFIG_TAMONTEN_ADNP
void tamonten_adnp_init(int i2c_bus, int irq);
#else
static inline void tamonten_adnp_init(int i2c_bus, int irq) {}
#endif

#endif
