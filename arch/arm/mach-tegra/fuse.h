/*
 * arch/arm/mach-tegra/fuse.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@android.com>
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

enum tegra_revision {
	TEGRA_REVISION_UNKNOWN = 0,
	TEGRA_REVISION_A02,
	TEGRA_REVISION_A03,
	TEGRA_REVISION_A03p,
	TEGRA_REVISION_A04,
	TEGRA_REVISION_MAX,
};

/* Number of different speed gradings */
#define NUM_SPEED_LEVELS	3
/* Number of corners/VF curves per grade */
#define NUM_PROCESS_CORNERS	4


#define SKU_ID_T20	8
#define SKU_ID_T25SE	20
#define SKU_ID_AP25	23
#define SKU_ID_T25	24
#define SKU_ID_AP25E	27
#define SKU_ID_T25E	28

extern int tegra_sku_id;
extern int tegra_cpu_process_id;
extern int tegra_core_process_id;
extern u64 tegra_chip_uid;

void tegra_init_fuse(void);
u32 tegra_fuse_readl(unsigned long offset);
void tegra_fuse_writel(u32 value, unsigned long offset);
enum tegra_revision tegra_get_revision(void);
int tegra_speedo_id(void);
