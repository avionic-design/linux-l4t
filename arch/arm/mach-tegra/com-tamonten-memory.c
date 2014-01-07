/*
 * Copyright (C) 2014, Avionic Design GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/kernel.h>
#include <linux/init.h>

#include "tegra2_emc.h"

static const struct tegra_emc_table tamonten_emc_table[] = {
	{
		.rate = 333000,
		.regs = {
			0x00000014,   /* RC */
			0x00000041,   /* RFC */
			0x0000000f,   /* RAS */
			0x00000005,   /* RP */
			0x00000004,   /* R2W */
			0x00000005,   /* W2R */
			0x00000003,   /* R2P */
			0x0000000b,   /* W2P */
			0x00000005,   /* RD_RCD */
			0x00000005,   /* WR_RCD */
			0x00000004,   /* RRD */
			0x00000001,   /* REXT */
			0x00000003,   /* WDV */
			0x00000004,   /* QUSE */
			0x00000003,   /* QRST */
			0x00000009,   /* QSAFE */
			0x0000000c,   /* RDV */
			0x000004df,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000005,   /* PCHG2PDEN */
			0x00000005,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000e,   /* RW2PDEN */
			0x000000c8,   /* TXSR */
			0x00000003,   /* TCKE */
			0x0000000f,   /* TFAW */
			0x00000006,   /* TRPAB */
			0x0000000f,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000000,   /* TREFBW */
			0x00000000,   /* QUSE_EXTRA */
			0x00000002,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000083,   /* FBIO_CFG5 */
			0x00380006,   /* CFG_DIG_DLL */
			0x00000010,   /* DLL_XFORM_DQS */
			0x00000008,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000000,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		},
	},
};

static const struct tegra_emc_chip tamonten_emc_chip = {
	.description = "Hynix 333MHz",
	.mem_manufacturer_id = -1,
	.mem_revision_id1 = -1,
	.mem_revision_id2 = -1,
	.mem_pid = -1,

	.table = tamonten_emc_table,
	.table_size = ARRAY_SIZE(tamonten_emc_table),
};

void __init tamonten_emc_init(void)
{
	tegra_init_emc(&tamonten_emc_chip, 1);
}
