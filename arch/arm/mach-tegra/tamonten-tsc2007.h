/*
 * arch/arm/mach-tegra/tamonten-tsc2007.h
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

#ifndef _MACH_TEGRA_TAMONTEN_TSC2007_H
#define _MACH_TEGRA_TAMONTEN_TSC2007_H

#ifdef CONFIG_TAMONTEN_TSC2007
void tamonten_tsc2007_init(int i2c_bus, int gpio, int irq);
#else
static inline void tamonten_tsc2007_init(int i2c_bus, int gpio, int irq)
{}
#endif

#endif
