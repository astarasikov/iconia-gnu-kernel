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
		.rate = 380000,   /* SDRAM frequency */
		.regs = {
			0x00000016,   /* RC */
			0x00000031,   /* RFC */
			0x00000012,   /* RAS */
			0x00000006,   /* RP */
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
			0x00000006,   /* PCHG2PDEN */
			0x00000005,   /* ACT2PDEN */
			0x00000001,   /* AR2PDEN */
			0x00000010,   /* RW2PDEN */
			0x000000c8,   /* TXSR */
			0x00000003,   /* TCKE */
			0x0000000e,   /* TFAW */
			0x00000007,   /* TRPAB */
			0x00000008,   /* TCLKSTABLE */
			0x00000002,   /* TCLKSTOP */
			0x00000000,   /* TREFBW */
			0x00000004,   /* QUSE_EXTRA */
			0x00000002,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000083,   /* FBIO_CFG5 */
			0xe044048b,   /* CFG_DIG_DLL */
			0x007e4010,   /* DLL_XFORM_DQS */
			0x00016617,   /* DLL_XFORM_QUSE */
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
			0x00000002,   /* FBIO_CFG6 */
			0x00000000,   /* ODT_WRITE */
			0x00000000,   /* ODT_READ */
			0x00000083,   /* FBIO_CFG5 */
			0xe044048b,   /* CFG_DIG_DLL */
			0x007e0010,   /* DLL_XFORM_DQS */
			0x00023215,   /* DLL_XFORM_QUSE */
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
		.table		= kaen_emc_tables_Samsung_333Mhz,
		.table_size	= ARRAY_SIZE(kaen_emc_tables_Samsung_333Mhz),
		.name		= "Samsung 333MHz",
	},
	{
		.table		= kaen_emc_tables_Nanya_333Mhz,
		.table_size	= ARRAY_SIZE(kaen_emc_tables_Nanya_333Mhz),
		.name		= "Nanya 333MHz",
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

#define STRAP_OPT 0x008
#define GMI_AD0 (1 << 4)
#define GMI_AD1 (1 << 5)
#define RAM_ID_MASK (GMI_AD0 | GMI_AD1)
#define RAM_CODE_SHIFT 4


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

void __init seaboard_emc_init(void)
{
	/* Adding allowances for wario as it shares the same memory
	 * configuration as seaboard.
	 */
	BUG_ON(!machine_is_seaboard() && !machine_is_wario());

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

void __init aebl_emc_init(void)
{
	BUG_ON(!machine_is_aebl());
	tegra_init_emc(aebl_emc_tables, ARRAY_SIZE(aebl_emc_tables));
}
