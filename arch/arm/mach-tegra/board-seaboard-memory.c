/*
 * arch/arm/mach-tegra/board-seaboard-memory.c
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>

#include <asm/mach-types.h>

#include <mach/iomap.h>

#include "board-seaboard.h"
#include "fuse.h"
#include "tegra2_emc.h"



struct tegra_board_emc_table {
	int				id; /* Boot strap ID */
	const struct tegra_emc_table	*table;
	const int			table_size;
	const char			*name;
};


static const struct tegra_emc_table seaboard_emc_tables_hynix_333Mhz[] = {
	{
		.rate = 166500,   /* SDRAM frequency */
		.regs = {
			0x0000000a,   /* RC */
			0x00000021,   /* RFC */
			0x00000008,   /* RAS */
			0x00000003,   /* RP */
			0x00000004,   /* R2W */
			0x00000004,   /* W2R */
			0x00000002,   /* R2P */
			0x0000000c,   /* W2P */
			0x00000003,   /* RD_RCD */
			0x00000003,   /* WR_RCD */
			0x00000002,   /* RRD */
			0x00000001,   /* REXT */
			0x00000004,   /* WDV */
			0x00000005,   /* QUSE */
			0x00000004,   /* QRST */
			0x00000009,   /* QSAFE */
			0x0000000d,   /* RDV */
			0x000004df,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000003,   /* PCHG2PDEN */
			0x00000003,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000a,   /* RW2PDEN */
			0x000000c8,   /* TXSR */
			0x00000003,   /* TCKE */
			0x00000006,   /* TFAW */
			0x00000004,   /* TRPAB */
			0x0000000f,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000000,   /* TREFBW */
			0x00000000,   /* QUSE_EXTRA */
			0x00000002,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000083,   /* FBIO_CFG5 */
			0xa04004ae,   /* CFG_DIG_DLL */
			0x007fd010,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000000,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	}, {
		.rate = 333000,   /* SDRAM frequency */
		.regs = {
			0x00000014,   /* RC */
			0x00000041,   /* RFC */
			0x0000000f,   /* RAS */
			0x00000005,   /* RP */
			0x00000004,   /* R2W */
			0x00000005,   /* W2R */
			0x00000003,   /* R2P */
			0x0000000c,   /* W2P */
			0x00000005,   /* RD_RCD */
			0x00000005,   /* WR_RCD */
			0x00000003,   /* RRD */
			0x00000001,   /* REXT */
			0x00000004,   /* WDV */
			0x00000005,   /* QUSE */
			0x00000004,   /* QRST */
			0x00000009,   /* QSAFE */
			0x0000000d,   /* RDV */
			0x000009ff,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000005,   /* PCHG2PDEN */
			0x00000005,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000f,   /* RW2PDEN */
			0x000000c8,   /* TXSR */
			0x00000003,   /* TCKE */
			0x0000000c,   /* TFAW */
			0x00000006,   /* TRPAB */
			0x0000000f,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000000,   /* TREFBW */
			0x00000000,   /* QUSE_EXTRA */
			0x00000002,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000083,   /* FBIO_CFG5 */
			0xe034048b,   /* CFG_DIG_DLL */
			0x007e8010,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000000,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	}
};

static const struct tegra_emc_table seaboard_emc_tables_hynix_380Mhz[] = {
	{
		.rate = 190000,   /* SDRAM frequency */
		.regs = {
			0x0000000c,   /* RC */
			0x00000026,   /* RFC */
			0x00000009,   /* RAS */
			0x00000003,   /* RP */
			0x00000004,   /* R2W */
			0x00000004,   /* W2R */
			0x00000002,   /* R2P */
			0x0000000c,   /* W2P */
			0x00000003,   /* RD_RCD */
			0x00000003,   /* WR_RCD */
			0x00000002,   /* RRD */
			0x00000001,   /* REXT */
			0x00000004,   /* WDV */
			0x00000005,   /* QUSE */
			0x00000004,   /* QRST */
			0x00000009,   /* QSAFE */
			0x0000000d,   /* RDV */
			0x0000059f,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000003,   /* PCHG2PDEN */
			0x00000003,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000b,   /* RW2PDEN */
			0x000000c8,   /* TXSR */
			0x00000003,   /* TCKE */
			0x00000007,   /* TFAW */
			0x00000004,   /* TRPAB */
			0x0000000f,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000000,   /* TREFBW */
			0x00000000,   /* QUSE_EXTRA */
			0x00000002,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000083,   /* FBIO_CFG5 */
			0xa06204ae,   /* CFG_DIG_DLL */
			0x007dc010,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000000,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	}, {
		.rate = 380000,   /* SDRAM frequency */
		.regs = {
			0x00000017,   /* RC */
			0x0000004b,   /* RFC */
			0x00000012,   /* RAS */
			0x00000006,   /* RP */
			0x00000004,   /* R2W */
			0x00000005,   /* W2R */
			0x00000003,   /* R2P */
			0x0000000c,   /* W2P */
			0x00000006,   /* RD_RCD */
			0x00000006,   /* WR_RCD */
			0x00000003,   /* RRD */
			0x00000001,   /* REXT */
			0x00000004,   /* WDV */
			0x00000005,   /* QUSE */
			0x00000004,   /* QRST */
			0x00000009,   /* QSAFE */
			0x0000000d,   /* RDV */
			0x00000b5f,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000006,   /* PCHG2PDEN */
			0x00000006,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x00000011,   /* RW2PDEN */
			0x000000c8,   /* TXSR */
			0x00000003,   /* TCKE */
			0x0000000e,   /* TFAW */
			0x00000007,   /* TRPAB */
			0x0000000f,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000000,   /* TREFBW */
			0x00000000,   /* QUSE_EXTRA */
			0x00000002,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000083,   /* FBIO_CFG5 */
			0xe044048b,   /* CFG_DIG_DLL */
			0x007d8010,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000000,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	}
};

void __init seaboard_emc_init(void)
{
	/* Adding allowances for wario as it shares the same memory
	 * configuration as seaboard.
	 */
	BUG_ON(!machine_is_seaboard() && !machine_is_wario()
		&& !machine_is_asymptote());

	if (tegra_sku_id == SKU_ID_T20) {
		tegra_init_emc(seaboard_emc_tables_hynix_333Mhz,
			ARRAY_SIZE(seaboard_emc_tables_hynix_333Mhz));
		pr_info("Tegra EMC table in use: Hynix 333MHz\n");
	} else if (tegra_sku_id == SKU_ID_T25) {
		tegra_init_emc(seaboard_emc_tables_hynix_380Mhz,
			ARRAY_SIZE(seaboard_emc_tables_hynix_380Mhz));
		pr_info("Tegra EMC table in use: Hynix 380MHz\n");
	} else {
		pr_err("EMC table not found for Tegra SKU %d. " \
		       "System stability might be compromised\n", tegra_sku_id);
	}
}

#ifdef CONFIG_MACH_KAEN

#define STRAP_OPT 0x008
#define GMI_AD0 (1 << 4)
#define GMI_AD1 (1 << 5)
#define RAM_ID_MASK (GMI_AD0 | GMI_AD1)
#define RAM_CODE_SHIFT 4

static const struct tegra_emc_table kaen_emc_tables_Nanya_333Mhz[] = {
	{
		.rate = 166500,   /* SDRAM frequency */
		.regs = {
			0x0000000a,   /* RC */
			0x00000016,   /* RFC */
			0x00000008,   /* RAS */
			0x00000003,   /* RP */
			0x00000004,   /* R2W */
			0x00000004,   /* W2R */
			0x00000002,   /* R2P */
			0x0000000a,   /* W2P */
			0x00000003,   /* RD_RCD */
			0x00000003,   /* WR_RCD */
			0x00000002,   /* RRD */
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
			0x00000003,   /* PCHG2PDEN */
			0x00000003,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x00000009,   /* RW2PDEN */
			0x000000c8,   /* TXSR */
			0x00000003,   /* TCKE */
			0x00000007,   /* TFAW */
			0x00000004,   /* TRPAB */
			0x00000006,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000000,   /* TREFBW */
			0x00000000,   /* QUSE_EXTRA */
			0x00000003,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000083,   /* FBIO_CFG5 */
			0xa06e04ae,   /* CFG_DIG_DLL */
			0x007e2010,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000000,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	}, {
		.rate = 333000,   /* SDRAM frequency */
		.regs = {
			0x00000014,   /* RC */
			0x0000002b,   /* RFC */
			0x0000000f,   /* RAS */
			0x00000005,   /* RP */
			0x00000004,   /* R2W */
			0x00000005,   /* W2R */
			0x00000003,   /* R2P */
			0x0000000a,   /* W2P */
			0x00000005,   /* RD_RCD */
			0x00000005,   /* WR_RCD */
			0x00000003,   /* RRD */
			0x00000001,   /* REXT */
			0x00000003,   /* WDV */
			0x00000004,   /* QUSE */
			0x00000003,   /* QRST */
			0x00000009,   /* QSAFE */
			0x0000000c,   /* RDV */
			0x000009ff,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000005,   /* PCHG2PDEN */
			0x00000005,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000e,   /* RW2PDEN */
			0x000000c8,   /* TXSR */
			0x00000003,   /* TCKE */
			0x0000000d,   /* TFAW */
			0x00000006,   /* TRPAB */
			0x00000006,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000000,   /* TREFBW */
			0x00000000,   /* QUSE_EXTRA */
			0x00000003,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000083,   /* FBIO_CFG5 */
			0xe04e048b,   /* CFG_DIG_DLL */
			0x007e2010,   /* DLL_XFORM_DQS */
			0x007f8417,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000000,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	}
};

static const struct tegra_emc_table kaen_emc_tables_Nanya_380Mhz[] = {
	{
		.rate = 190000,   /* SDRAM frequency */
		.regs = {
			0x0000000b,   /* RC */
			0x00000019,   /* RFC */
			0x00000009,   /* RAS */
			0x00000003,   /* RP */
			0x00000004,   /* R2W */
			0x00000004,   /* W2R */
			0x00000002,   /* R2P */
			0x0000000b,   /* W2P */
			0x00000003,   /* RD_RCD */
			0x00000003,   /* WR_RCD */
			0x00000002,   /* RRD */
			0x00000001,   /* REXT */
			0x00000003,   /* WDV */
			0x00000004,   /* QUSE */
			0x00000003,   /* QRST */
			0x00000009,   /* QSAFE */
			0x0000000c,   /* RDV */
			0x0000059f,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000003,   /* PCHG2PDEN */
			0x00000003,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000a,   /* RW2PDEN */
			0x000000c8,   /* TXSR */
			0x00000003,   /* TCKE */
			0x00000007,   /* TFAW */
			0x00000004,   /* TRPAB */
			0x00000008,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000000,   /* TREFBW */
			0x00000000,   /* QUSE_EXTRA */
			0x00000003,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000083,   /* FBIO_CFG5 */
			0xa06204ae,   /* CFG_DIG_DLL */
			0x007da010,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000000,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	}, {
		.rate = 380000,   /* SDRAM frequency */
		.regs = {
			0x00000016,   /* RC */
			0x00000031,   /* RFC */
			0x00000012,   /* RAS */
			0x00000005,   /* RP */
			0x00000004,   /* R2W */
			0x00000005,   /* W2R */
			0x00000003,   /* R2P */
			0x0000000b,   /* W2P */
			0x00000005,   /* RD_RCD */
			0x00000005,   /* WR_RCD */
			0x00000003,   /* RRD */
			0x00000001,   /* REXT */
			0x00000003,   /* WDV */
			0x00000004,   /* QUSE */
			0x00000003,   /* QRST */
			0x00000009,   /* QSAFE */
			0x0000000c,   /* RDV */
			0x00000b5f,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000005,   /* PCHG2PDEN */
			0x00000005,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000f,   /* RW2PDEN */
			0x000000c8,   /* TXSR */
			0x00000003,   /* TCKE */
			0x0000000e,   /* TFAW */
			0x00000006,   /* TRPAB */
			0x00000008,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000000,   /* TREFBW */
			0x00000000,   /* QUSE_EXTRA */
			0x00000003,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000083,   /* FBIO_CFG5 */
			0xe044048b,   /* CFG_DIG_DLL */
			0x007da010,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000000,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	}
};

static const struct tegra_emc_table kaen_emc_tables_Samsung_333Mhz[] = {
	{
		.rate = 166500,   /* SDRAM frequency */
		.regs = {
			0x0000000a,   /* RC */
			0x00000016,   /* RFC */
			0x00000008,   /* RAS */
			0x00000003,   /* RP */
			0x00000004,   /* R2W */
			0x00000004,   /* W2R */
			0x00000002,   /* R2P */
			0x0000000c,   /* W2P */
			0x00000003,   /* RD_RCD */
			0x00000003,   /* WR_RCD */
			0x00000002,   /* RRD */
			0x00000001,   /* REXT */
			0x00000004,   /* WDV */
			0x00000005,   /* QUSE */
			0x00000004,   /* QRST */
			0x00000009,   /* QSAFE */
			0x0000000d,   /* RDV */
			0x000004df,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000003,   /* PCHG2PDEN */
			0x00000003,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000a,   /* RW2PDEN */
			0x000000c8,   /* TXSR */
			0x00000003,   /* TCKE */
			0x00000006,   /* TFAW */
			0x00000004,   /* TRPAB */
			0x00000008,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000000,   /* TREFBW */
			0x00000000,   /* QUSE_EXTRA */
			0x00000003,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000083,   /* FBIO_CFG5 */
			0xa06e04ae,   /* CFG_DIG_DLL */
			0x007e2010,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000000,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	}, {
		.rate = 333000,   /* SDRAM frequency */
		.regs = {
			0x00000014,   /* RC */
			0x0000002b,   /* RFC */
			0x0000000f,   /* RAS */
			0x00000005,   /* RP */
			0x00000004,   /* R2W */
			0x00000005,   /* W2R */
			0x00000003,   /* R2P */
			0x0000000c,   /* W2P */
			0x00000005,   /* RD_RCD */
			0x00000005,   /* WR_RCD */
			0x00000003,   /* RRD */
			0x00000001,   /* REXT */
			0x00000004,   /* WDV */
			0x00000005,   /* QUSE */
			0x00000004,   /* QRST */
			0x00000009,   /* QSAFE */
			0x0000000d,   /* RDV */
			0x000009ff,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000005,   /* PCHG2PDEN */
			0x00000005,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000f,   /* RW2PDEN */
			0x000000c8,   /* TXSR */
			0x00000003,   /* TCKE */
			0x0000000c,   /* TFAW */
			0x00000006,   /* TRPAB */
			0x00000008,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000000,   /* TREFBW */
			0x00000000,   /* QUSE_EXTRA */
			0x00000002,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000083,   /* FBIO_CFG5 */
			0xe04e048b,   /* CFG_DIG_DLL */
			0x007de010,   /* DLL_XFORM_DQS */
			0x00022015,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000000,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	}
};

static const struct tegra_emc_table kaen_emc_tables_Samsung_380Mhz[] = {
	{
		.rate = 190000,   /* SDRAM frequency */
		.regs = {
			0x0000000c,   /* RC */
			0x00000019,   /* RFC */
			0x00000009,   /* RAS */
			0x00000003,   /* RP */
			0x00000004,   /* R2W */
			0x00000004,   /* W2R */
			0x00000002,   /* R2P */
			0x0000000c,   /* W2P */
			0x00000003,   /* RD_RCD */
			0x00000003,   /* WR_RCD */
			0x00000002,   /* RRD */
			0x00000001,   /* REXT */
			0x00000004,   /* WDV */
			0x00000005,   /* QUSE */
			0x00000004,   /* QRST */
			0x00000009,   /* QSAFE */
			0x0000000d,   /* RDV */
			0x0000059f,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000003,   /* PCHG2PDEN */
			0x00000003,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000b,   /* RW2PDEN */
			0x000000c8,   /* TXSR */
			0x00000003,   /* TCKE */
			0x00000007,   /* TFAW */
			0x00000004,   /* TRPAB */
			0x00000008,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000000,   /* TREFBW */
			0x00000000,   /* QUSE_EXTRA */
			0x00000003,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000083,   /* FBIO_CFG5 */
			0xa06204ae,   /* CFG_DIG_DLL */
			0x007e2010,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000000,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	}, {
		.rate = 380000,   /* SDRAM frequency */
		.regs = {
			0x00000017,   /* RC */
			0x00000031,   /* RFC */
			0x00000012,   /* RAS */
			0x00000006,   /* RP */
			0x00000004,   /* R2W */
			0x00000005,   /* W2R */
			0x00000003,   /* R2P */
			0x0000000c,   /* W2P */
			0x00000006,   /* RD_RCD */
			0x00000006,   /* WR_RCD */
			0x00000003,   /* RRD */
			0x00000001,   /* REXT */
			0x00000004,   /* WDV */
			0x00000005,   /* QUSE */
			0x00000004,   /* QRST */
			0x00000009,   /* QSAFE */
			0x0000000d,   /* RDV */
			0x00000b5f,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000006,   /* PCHG2PDEN */
			0x00000006,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x00000011,   /* RW2PDEN */
			0x000000c8,   /* TXSR */
			0x00000003,   /* TCKE */
			0x0000000e,   /* TFAW */
			0x00000007,   /* TRPAB */
			0x00000008,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000000,   /* TREFBW */
			0x00000000,   /* QUSE_EXTRA */
			0x00000003,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000083,   /* FBIO_CFG5 */
			0xe044048b,   /* CFG_DIG_DLL */
			0x007de010,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000000,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	}
};

struct tegra_board_emc_table kaen_emc[] = {
	{
		.table		= kaen_emc_tables_Samsung_380Mhz,
		.table_size	= ARRAY_SIZE(kaen_emc_tables_Samsung_380Mhz),
		.name		= "Samsung 380MHz",
	},
	{
		.table		= kaen_emc_tables_Nanya_380Mhz,
		.table_size	= ARRAY_SIZE(kaen_emc_tables_Nanya_380Mhz),
		.name		= "Nanya 380MHz",
	},
	{
		.table		= kaen_emc_tables_Samsung_380Mhz,
		.table_size	= ARRAY_SIZE(kaen_emc_tables_Samsung_380Mhz),
		.name		= "Samsung 380MHz",
	},
	{
		.table		= kaen_emc_tables_Nanya_380Mhz,
		.table_size	= ARRAY_SIZE(kaen_emc_tables_Nanya_380Mhz),
		.name		= "Nanya 380MHz",
	},
};

void __init kaen_emc_init(void)
{
	u32 reg;
	int ram_id;
	void __iomem *apb_misc = IO_ADDRESS(TEGRA_APB_MISC_BASE);

	BUG_ON(!machine_is_kaen());

	/* TODO: Move this to use the apbio library in 2.6.38 */
	reg = readl(apb_misc + STRAP_OPT);
	ram_id = (reg & RAM_ID_MASK) >> RAM_CODE_SHIFT;

	if (ram_id >= ARRAY_SIZE(kaen_emc) || !kaen_emc[ram_id].table) {
		pr_err("EMC table for ram id %d not found. " \
		       "System stability might be compromised\n", ram_id);
	} else {
		pr_info("Tegra EMC table in use: %s\n", kaen_emc[ram_id].name);
		tegra_init_emc(kaen_emc[ram_id].table,
			       kaen_emc[ram_id].table_size);
	}
}
#endif

#ifdef CONFIG_MACH_AEBL
static const struct tegra_emc_table aebl_emc_tables[] = {
	{
		.rate = 190000,   /* SDRAM frequency */
		.regs = {
			0x0000000b,   /* RC */
			0x00000026,   /* RFC */
			0x00000008,   /* RAS */
			0x00000003,   /* RP */
			0x00000004,   /* R2W */
			0x00000004,   /* W2R */
			0x00000002,   /* R2P */
			0x0000000b,   /* W2P */
			0x00000003,   /* RD_RCD */
			0x00000003,   /* WR_RCD */
			0x00000002,   /* RRD */
			0x00000001,   /* REXT */
			0x00000003,   /* WDV */
			0x00000004,   /* QUSE */
			0x00000005,   /* QRST */
			0x00000009,   /* QSAFE */
			0x0000000c,   /* RDV */
			0x0000059f,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000003,   /* PCHG2PDEN */
			0x00000003,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000a,   /* RW2PDEN */
			0x000000c8,   /* TXSR */
			0x00000003,   /* TCKE */
			0x00000007,   /* TFAW */
			0x00000004,   /* TRPAB */
			0x0000000f,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000000,   /* TREFBW */
			0x00000000,   /* QUSE_EXTRA */
			0x00000003,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000083,   /* FBIO_CFG5 */
			0xa06204ae,   /* CFG_DIG_DLL */
			0x007e8010,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000000,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	}, {
		.rate = 380000,   /* SDRAM frequency */
		.regs = {
			0x00000015,   /* RC */
			0x0000004c,   /* RFC */
			0x00000010,   /* RAS */
			0x00000005,   /* RP */
			0x00000004,   /* R2W */
			0x00000005,   /* W2R */
			0x00000003,   /* R2P */
			0x0000000b,   /* W2P */
			0x00000005,   /* RD_RCD */
			0x00000005,   /* WR_RCD */
			0x00000003,   /* RRD */
			0x00000001,   /* REXT */
			0x00000003,   /* WDV */
			0x00000004,   /* QUSE */
			0x00000003,   /* QRST */
			0x00000009,   /* QSAFE */
			0x0000000c,   /* RDV */
			0x00000b5f,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000005,   /* PCHG2PDEN */
			0x00000005,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000f,   /* RW2PDEN */
			0x000000c8,   /* TXSR */
			0x00000003,   /* TCKE */
			0x0000000e,   /* TFAW */
			0x00000006,   /* TRPAB */
			0x0000000f,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000000,   /* TREFBW */
			0x00000000,   /* QUSE_EXTRA */
			0x00000003,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000083,   /* FBIO_CFG5 */
			0xe044048b,   /* CFG_DIG_DLL */
			0x007e0010,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000000,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	}
};

void __init aebl_emc_init(void)
{
	BUG_ON(!machine_is_aebl());
	tegra_init_emc(aebl_emc_tables, ARRAY_SIZE(aebl_emc_tables));
}
#endif

#ifdef CONFIG_MACH_ARTHUR
static const struct tegra_emc_table arthur_emc_tables[] = {
	{
		.rate = 190000,   /* SDRAM frequency */
		.regs = {
			0x0000000c,   /* RC */
			0x00000026,   /* RFC */
			0x00000009,   /* RAS */
			0x00000003,   /* RP */
			0x00000004,   /* R2W */
			0x00000004,   /* W2R */
			0x00000002,   /* R2P */
			0x0000000c,   /* W2P */
			0x00000003,   /* RD_RCD */
			0x00000003,   /* WR_RCD */
			0x00000002,   /* RRD */
			0x00000001,   /* REXT */
			0x00000004,   /* WDV */
			0x00000005,   /* QUSE */
			0x00000004,   /* QRST */
			0x00000009,   /* QSAFE */
			0x0000000d,   /* RDV */
			0x0000059f,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000003,   /* PCHG2PDEN */
			0x00000003,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000b,   /* RW2PDEN */
			0x000000c8,   /* TXSR */
			0x00000003,   /* TCKE */
			0x00000007,   /* TFAW */
			0x00000004,   /* TRPAB */
			0x0000000f,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000000,   /* TREFBW */
			0x00000000,   /* QUSE_EXTRA */
			0x00000003,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000083,   /* FBIO_CFG5 */
			0xa06204ae,   /* CFG_DIG_DLL */
			0x007e0010,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000000,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	}, {
		.rate = 380000,   /* SDRAM frequency */
		.regs = {
			0x00000017,   /* RC */
			0x0000004b,   /* RFC */
			0x00000012,   /* RAS */
			0x00000006,   /* RP */
			0x00000004,   /* R2W */
			0x00000005,   /* W2R */
			0x00000003,   /* R2P */
			0x0000000c,   /* W2P */
			0x00000006,   /* RD_RCD */
			0x00000006,   /* WR_RCD */
			0x00000003,   /* RRD */
			0x00000001,   /* REXT */
			0x00000004,   /* WDV */
			0x00000005,   /* QUSE */
			0x00000004,   /* QRST */
			0x00000009,   /* QSAFE */
			0x0000000d,   /* RDV */
			0x00000b5f,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000006,   /* PCHG2PDEN */
			0x00000006,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x00000011,   /* RW2PDEN */
			0x000000c8,   /* TXSR */
			0x00000003,   /* TCKE */
			0x0000000e,   /* TFAW */
			0x00000007,   /* TRPAB */
			0x0000000f,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000000,   /* TREFBW */
			0x00000000,   /* QUSE_EXTRA */
			0x00000003,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000083,   /* FBIO_CFG5 */
			0xe044048b,   /* CFG_DIG_DLL */
			0x007dc010,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000000,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	}
};

void __init arthur_emc_init(void)
{
	BUG_ON(!machine_is_arthur());
	tegra_init_emc(arthur_emc_tables, ARRAY_SIZE(arthur_emc_tables));
}
#endif

#ifdef CONFIG_MACH_VENTANA
static const struct tegra_emc_table ventana_emc_tables_elpida_300Mhz[] = {
	{
		.rate = 25000,   /* SDRAM frquency */
		.regs = {
			0x00000002,   /* RC */
			0x00000006,   /* RFC */
			0x00000003,   /* RAS */
			0x00000003,   /* RP */
			0x00000006,   /* R2W */
			0x00000004,   /* W2R */
			0x00000002,   /* R2P */
			0x00000009,   /* W2P */
			0x00000003,   /* RD_RCD */
			0x00000003,   /* WR_RCD */
			0x00000002,   /* RRD */
			0x00000002,   /* REXT */
			0x00000002,   /* WDV */
			0x00000004,   /* QUSE */
			0x00000003,   /* QRST */
			0x00000008,   /* QSAFE */
			0x0000000b,   /* RDV */
			0x0000004d,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000003,   /* PCHG2PDEN */
			0x00000008,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000a,   /* RW2PDEN */
			0x00000004,   /* TXSR */
			0x00000003,   /* TCKE */
			0x00000008,   /* TFAW */
			0x00000004,   /* TRPAB */
			0x00000006,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000068,   /* TREFBW */
			0x00000003,   /* QUSE_EXTRA */
			0x00000003,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000082,   /* FBIO_CFG5 */
			0xa06a04ae,   /* CFG_DIG_DLL */
			0x0001f000,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000003,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	},
	{
		.rate = 50000,   /* SDRAM frequency */
		.regs = {
			0x00000003,   /* RC */
			0x00000007,   /* RFC */
			0x00000003,   /* RAS */
			0x00000003,   /* RP */
			0x00000006,   /* R2W */
			0x00000004,   /* W2R */
			0x00000002,   /* R2P */
			0x00000009,   /* W2P */
			0x00000003,   /* RD_RCD */
			0x00000003,   /* WR_RCD */
			0x00000002,   /* RRD */
			0x00000002,   /* REXT */
			0x00000002,   /* WDV */
			0x00000005,   /* QUSE */
			0x00000003,   /* QRST */
			0x00000008,   /* QSAFE */
			0x0000000b,   /* RDV */
			0x0000009f,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000003,   /* PCHG2PDEN */
			0x00000008,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000a,   /* RW2PDEN */
			0x00000007,   /* TXSR */
			0x00000003,   /* TCKE */
			0x00000008,   /* TFAW */
			0x00000004,   /* TRPAB */
			0x00000006,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x000000d0,   /* TREFBW */
			0x00000004,   /* QUSE_EXTRA */
			0x00000000,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000082,   /* FBIO_CFG5 */
			0xa06a04ae,   /* CFG_DIG_DLL */
			0x0001f000,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000005,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	},
	{
		.rate = 75000,   /* SDRAM frequency */
		.regs = {
			0x00000005,   /* RC */
			0x0000000a,   /* RFC */
			0x00000004,   /* RAS */
			0x00000003,   /* RP */
			0x00000006,   /* R2W */
			0x00000004,   /* W2R */
			0x00000002,   /* R2P */
			0x00000009,   /* W2P */
			0x00000003,   /* RD_RCD */
			0x00000003,   /* WR_RCD */
			0x00000002,   /* RRD */
			0x00000002,   /* REXT */
			0x00000002,   /* WDV */
			0x00000005,   /* QUSE */
			0x00000003,   /* QRST */
			0x00000008,   /* QSAFE */
			0x0000000b,   /* RDV */
			0x000000ff,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000003,   /* PCHG2PDEN */
			0x00000008,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000a,   /* RW2PDEN */
			0x0000000b,   /* TXSR */
			0x00000003,   /* TCKE */
			0x00000008,   /* TFAW */
			0x00000004,   /* TRPAB */
			0x00000006,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000138,   /* TREFBW */
			0x00000004,   /* QUSE_EXTRA */
			0x00000000,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000082,   /* FBIO_CFG5 */
			0xa06a04ae,   /* CFG_DIG_DLL */
			0x0001f000,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000007,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	},
	{
		.rate = 150000,   /* SDRAM frequency */
		.regs = {
			0x00000009,   /* RC */
			0x00000014,   /* RFC */
			0x00000007,   /* RAS */
			0x00000004,   /* RP */
			0x00000006,   /* R2W */
			0x00000004,   /* W2R */
			0x00000002,   /* R2P */
			0x00000009,   /* W2P */
			0x00000003,   /* RD_RCD */
			0x00000003,   /* WR_RCD */
			0x00000002,   /* RRD */
			0x00000002,   /* REXT */
			0x00000002,   /* WDV */
			0x00000005,   /* QUSE */
			0x00000003,   /* QRST */
			0x00000008,   /* QSAFE */
			0x0000000b,   /* RDV */
			0x0000021f,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000004,   /* PCHG2PDEN */
			0x00000008,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000a,   /* RW2PDEN */
			0x00000015,   /* TXSR */
			0x00000003,   /* TCKE */
			0x00000008,   /* TFAW */
			0x00000004,   /* TRPAB */
			0x00000006,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000270,   /* TREFBW */
			0x00000000,   /* QUSE_EXTRA */
			0x00000001,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000082,   /* FBIO_CFG5 */
			0xA04C04AE,   /* CFG_DIG_DLL */
			0x007FC010,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x0000000e,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	},
	{
		.rate = 300000,   /* SDRAM frequency */
		.regs = {
			0x00000012,   /* RC */
			0x00000027,   /* RFC */
			0x0000000D,   /* RAS */
			0x00000007,   /* RP */
			0x00000007,   /* R2W */
			0x00000005,   /* W2R */
			0x00000003,   /* R2P */
			0x00000009,   /* W2P */
			0x00000006,   /* RD_RCD */
			0x00000006,   /* WR_RCD */
			0x00000003,   /* RRD */
			0x00000003,   /* REXT */
			0x00000002,   /* WDV */
			0x00000006,   /* QUSE */
			0x00000003,   /* QRST */
			0x00000009,   /* QSAFE */
			0x0000000c,   /* RDV */
			0x0000045f,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000004,   /* PDEX2WR */
			0x00000004,   /* PDEX2RD */
			0x00000007,   /* PCHG2PDEN */
			0x00000008,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000e,   /* RW2PDEN */
			0x0000002A,   /* TXSR */
			0x00000003,   /* TCKE */
			0x0000000F,   /* TFAW */
			0x00000008,   /* TRPAB */
			0x00000005,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x000004E1,   /* TREFBW */
			0x00000005,   /* QUSE_EXTRA */
			0x00000002,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000282,   /* FBIO_CFG5 */
			0xE03C048B,   /* CFG_DIG_DLL */
			0x007FC010,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x0000001B,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	}
};

static const struct tegra_emc_table ventana_emc_tables_elpida_400Mhz[] = {
	{
		.rate = 23750,   /* SDRAM frquency */
		.regs = {
			0x00000002,   /* RC */
			0x00000006,   /* RFC */
			0x00000003,   /* RAS */
			0x00000003,   /* RP */
			0x00000006,   /* R2W */
			0x00000004,   /* W2R */
			0x00000002,   /* R2P */
			0x0000000b,   /* W2P */
			0x00000003,   /* RD_RCD */
			0x00000003,   /* WR_RCD */
			0x00000002,   /* RRD */
			0x00000002,   /* REXT */
			0x00000003,   /* WDV */
			0x00000005,   /* QUSE */
			0x00000004,   /* QRST */
			0x00000008,   /* QSAFE */
			0x0000000c,   /* RDV */
			0x00000047,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000003,   /* PCHG2PDEN */
			0x00000008,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000b,   /* RW2PDEN */
			0x00000004,   /* TXSR */
			0x00000003,   /* TCKE */
			0x00000008,   /* TFAW */
			0x00000004,   /* TRPAB */
			0x00000008,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000060,   /* TREFBW */
			0x00000004,   /* QUSE_EXTRA */
			0x00000003,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000082,   /* FBIO_CFG5 */
			0xa0ae04ae,   /* CFG_DIG_DLL */
			0x0001f800,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000003,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	},
	{
		.rate = 63333,   /* SDRAM frquency */
		.regs = {
			0x00000004,   /* RC */
			0x00000009,   /* RFC */
			0x00000003,   /* RAS */
			0x00000003,   /* RP */
			0x00000006,   /* R2W */
			0x00000004,   /* W2R */
			0x00000002,   /* R2P */
			0x0000000b,   /* W2P */
			0x00000003,   /* RD_RCD */
			0x00000003,   /* WR_RCD */
			0x00000002,   /* RRD */
			0x00000002,   /* REXT */
			0x00000003,   /* WDV */
			0x00000006,   /* QUSE */
			0x00000004,   /* QRST */
			0x00000008,   /* QSAFE */
			0x0000000c,   /* RDV */
			0x000000c4,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000003,   /* PCHG2PDEN */
			0x00000008,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000b,   /* RW2PDEN */
			0x00000009,   /* TXSR */
			0x00000003,   /* TCKE */
			0x00000008,   /* TFAW */
			0x00000004,   /* TRPAB */
			0x00000008,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000107,   /* TREFBW */
			0x00000005,   /* QUSE_EXTRA */
			0x00000000,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000082,   /* FBIO_CFG5 */
			0xa0ae04ae,   /* CFG_DIG_DLL */
			0x0001f800,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000006,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	},
	{
		.rate = 95000,   /* SDRAM frquency */
		.regs = {
			0x00000006,   /* RC */
			0x0000000d,   /* RFC */
			0x00000004,   /* RAS */
			0x00000003,   /* RP */
			0x00000006,   /* R2W */
			0x00000004,   /* W2R */
			0x00000002,   /* R2P */
			0x0000000b,   /* W2P */
			0x00000003,   /* RD_RCD */
			0x00000003,   /* WR_RCD */
			0x00000002,   /* RRD */
			0x00000002,   /* REXT */
			0x00000003,   /* WDV */
			0x00000006,   /* QUSE */
			0x00000004,   /* QRST */
			0x00000008,   /* QSAFE */
			0x0000000c,   /* RDV */
			0x0000013f,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000003,   /* PCHG2PDEN */
			0x00000008,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000b,   /* RW2PDEN */
			0x0000000e,   /* TXSR */
			0x00000003,   /* TCKE */
			0x00000008,   /* TFAW */
			0x00000004,   /* TRPAB */
			0x00000008,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x0000018c,   /* TREFBW */
			0x00000005,   /* QUSE_EXTRA */
			0x00000001,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000082,   /* FBIO_CFG5 */
			0xa0ae04ae,   /* CFG_DIG_DLL */
			0x0001f000,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000009,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	},
	{
		.rate = 190000,   /* SDRAM frquency */
		.regs = {
			0x0000000c,   /* RC */
			0x00000019,   /* RFC */
			0x00000008,   /* RAS */
			0x00000004,   /* RP */
			0x00000007,   /* R2W */
			0x00000004,   /* W2R */
			0x00000002,   /* R2P */
			0x0000000b,   /* W2P */
			0x00000004,   /* RD_RCD */
			0x00000004,   /* WR_RCD */
			0x00000002,   /* RRD */
			0x00000003,   /* REXT */
			0x00000003,   /* WDV */
			0x00000006,   /* QUSE */
			0x00000004,   /* QRST */
			0x00000009,   /* QSAFE */
			0x0000000d,   /* RDV */
			0x000002bf,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000003,   /* PDEX2WR */
			0x00000003,   /* PDEX2RD */
			0x00000004,   /* PCHG2PDEN */
			0x00000008,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x0000000c,   /* RW2PDEN */
			0x0000001b,   /* TXSR */
			0x00000003,   /* TCKE */
			0x0000000a,   /* TFAW */
			0x00000004,   /* TRPAB */
			0x00000008,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000317,   /* TREFBW */
			0x00000005,   /* QUSE_EXTRA */
			0x00000002,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000082,   /* FBIO_CFG5 */
			0xa06204ae,   /* CFG_DIG_DLL */
			0x007f7010,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000012,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	},
	{
		.rate = 380000,   /* SDRAM frquency */
		.regs = {
			0x00000017,   /* RC */
			0x00000032,   /* RFC */
			0x00000010,   /* RAS */
			0x00000007,   /* RP */
			0x00000008,   /* R2W */
			0x00000005,   /* W2R */
			0x00000003,   /* R2P */
			0x0000000b,   /* W2P */
			0x00000007,   /* RD_RCD */
			0x00000007,   /* WR_RCD */
			0x00000004,   /* RRD */
			0x00000003,   /* REXT */
			0x00000003,   /* WDV */
			0x00000007,   /* QUSE */
			0x00000004,   /* QRST */
			0x0000000a,   /* QSAFE */
			0x0000000e,   /* RDV */
			0x0000059f,   /* REFRESH */
			0x00000000,   /* BURST_REFRESH_NUM */
			0x00000004,   /* PDEX2WR */
			0x00000004,   /* PDEX2RD */
			0x00000007,   /* PCHG2PDEN */
			0x00000008,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x00000011,   /* RW2PDEN */
			0x00000036,   /* TXSR */
			0x00000003,   /* TCKE */
			0x00000013,   /* TFAW */
			0x00000008,   /* TRPAB */
			0x00000007,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x0000062d,   /* TREFBW */
			0x00000006,   /* QUSE_EXTRA */
			0x00000003,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000282,   /* FBIO_CFG5 */
			0xe044048b,   /* CFG_DIG_DLL */
			0x007fb010,   /* DLL_XFORM_DQS */
			0x00000000,   /* DLL_XFORM_QUSE */
			0x00000000,   /* ZCAL_REF_CNT */
			0x00000023,   /* ZCAL_WAIT_CNT */
			0x00000000,   /* AUTO_CAL_INTERVAL */
			0x00000000,   /* CFG_CLKTRIM_0 */
			0x00000000,   /* CFG_CLKTRIM_1 */
			0x00000000,   /* CFG_CLKTRIM_2 */
		}
	}
};

void __init ventana_emc_init(void)
{
	BUG_ON(!machine_is_ventana());
	switch (tegra_sku_id) {
	case SKU_ID_T20:
		tegra_init_emc(ventana_emc_tables_elpida_300Mhz,
			       ARRAY_SIZE(ventana_emc_tables_elpida_300Mhz));
		pr_info("Tegra EMC table in use: Elpida_300MHz\n");
		break;

	case SKU_ID_T25:
		tegra_init_emc(ventana_emc_tables_elpida_400Mhz,
			       ARRAY_SIZE(ventana_emc_tables_elpida_400Mhz));
		pr_info("Tegra EMC table in use: Elpida_400MHz\n");
		break;

	default:
		pr_err("EMC table not found for Tegra SKU %d. " \
		       "System stability might be compromised\n",
			tegra_sku_id);
		break;
	}
}
#endif
