/*
 * Copyright (C) 2010 NVIDIA, Inc.
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
#include <linux/i2c.h>
#include <linux/pda_power.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/tps6586x.h>
#include <linux/gpio.h>
#include <linux/io.h>

#include <mach/iomap.h>
#include <mach/irqs.h>

#include "gpio-names.h"
#include "power.h"
#include "wakeups-t2.h"
#include "board.h"
#include "board-picasso.h"

#define PMC_CTRL		0x0
#define PMC_CTRL_INTR_LOW	(1 << 17)

static struct regulator_consumer_supply tps658621_sm0_supply[] = {
	REGULATOR_SUPPLY("vdd_core", NULL),
};

static struct regulator_consumer_supply tps658621_sm1_supply[] = {
	REGULATOR_SUPPLY("vdd_cpu", NULL),
};

static struct regulator_consumer_supply tps658621_sm2_supply[] = {
	REGULATOR_SUPPLY("vdd_sm2", NULL),
};

static struct regulator_consumer_supply tps658621_ldo0_supply[] = {
	REGULATOR_SUPPLY("p_cam_avdd", NULL),
};

static struct regulator_consumer_supply tps658621_ldo1_supply[] = {
	REGULATOR_SUPPLY("avdd_pll", NULL),
};

static struct regulator_consumer_supply tps658621_ldo2_supply[] = {
	REGULATOR_SUPPLY("vdd_rtc", NULL),
	REGULATOR_SUPPLY("vdd_aon", NULL),
};

static struct regulator_consumer_supply tps658621_ldo3_supply[] = {
	REGULATOR_SUPPLY("avdd_usb", NULL),
	REGULATOR_SUPPLY("avdd_usb_pll", NULL),
};

static struct regulator_consumer_supply tps658621_ldo4_supply[] = {
	REGULATOR_SUPPLY("avdd_osc", NULL),
	REGULATOR_SUPPLY("vddio_sys", NULL),
};

static struct regulator_consumer_supply tps658621_ldo5_supply[] = {
	REGULATOR_SUPPLY("vmmc", "sdhci-tegra.0"),
	REGULATOR_SUPPLY("vmmc", "sdhci-tegra.1"),
	REGULATOR_SUPPLY("vmmc", "sdhci-tegra.2"),
	REGULATOR_SUPPLY("vmmc", "sdhci-tegra.3"),
};

static struct regulator_consumer_supply tps658621_ldo6_supply[] = {
	REGULATOR_SUPPLY("vddio_vi", "tegra_camera"),
};

static struct regulator_consumer_supply tps658621_ldo7_supply[] = {
	REGULATOR_SUPPLY("avdd_hdmi", NULL),
	REGULATOR_SUPPLY("vdd_fuse", NULL),
};

static struct regulator_consumer_supply tps658621_ldo8_supply[] = {
	REGULATOR_SUPPLY("avdd_hdmi_pll", NULL),
};

static struct regulator_consumer_supply tps658621_ldo9_supply[] = {
	REGULATOR_SUPPLY("avdd_2v85", NULL),
	REGULATOR_SUPPLY("vdd_ddr_rx", NULL),
	REGULATOR_SUPPLY("avdd_amp", NULL),
};

#define REGULATOR_INIT(_id, _minmv, _maxmv, _always_on)				\
	static struct regulator_init_data reg_##_id##_data = \
	{								\
		.constraints = {					\
			.min_uV = (_minmv)*1000,			\
			.max_uV = (_maxmv)*1000,			\
			.valid_modes_mask = (REGULATOR_MODE_FAST |	\
					     REGULATOR_MODE_NORMAL),	\
			.valid_ops_mask = (REGULATOR_CHANGE_MODE |	\
					   REGULATOR_CHANGE_STATUS |	\
					   REGULATOR_CHANGE_VOLTAGE),	\
			.always_on = _always_on, \
			.apply_uV = (_minmv == _maxmv), \
		},							\
		.num_consumer_supplies = ARRAY_SIZE(tps658621_##_id##_supply),\
		.consumer_supplies = tps658621_##_id##_supply,		\
	}

REGULATOR_INIT(sm0, 725, 1300, true);
REGULATOR_INIT(sm1, 725, 1125, true);
REGULATOR_INIT(sm2, 3000, 4550, true);
REGULATOR_INIT(ldo0, 1250, 3300, false);
REGULATOR_INIT(ldo1, 725, 1500, true);
REGULATOR_INIT(ldo2, 725, 1275, false);
REGULATOR_INIT(ldo3, 1250, 3300, true);
REGULATOR_INIT(ldo4, 1700, 2475, true);
REGULATOR_INIT(ldo5, 1250, 3300, true);
REGULATOR_INIT(ldo6, 1250, 1800, false);
REGULATOR_INIT(ldo7, 1250, 3300, false);
REGULATOR_INIT(ldo8, 1250, 3300, false);
REGULATOR_INIT(ldo9, 1250, 3300, true);

static struct tps6586x_rtc_platform_data rtc_data = {
	.irq = TEGRA_NR_IRQS + TPS6586X_INT_RTC_ALM1,
};

#define TPS_REG(_id, _reg)			\
	{					\
		.id = TPS6586X_ID_##_id,	\
		.name = "tps6586x-regulator",	\
		.platform_data = &reg_##_reg##_data,		\
	}

static struct tps6586x_subdev_info tps_devs[] = {
	TPS_REG(SM_0, sm0),
	TPS_REG(SM_1, sm1),
	TPS_REG(SM_2, sm2),
	TPS_REG(LDO_0, ldo0),
	TPS_REG(LDO_1, ldo1),
	TPS_REG(LDO_2, ldo2),
	TPS_REG(LDO_3, ldo3),
	TPS_REG(LDO_4, ldo4),
	TPS_REG(LDO_5, ldo5),
	TPS_REG(LDO_6, ldo6),
	TPS_REG(LDO_7, ldo7),
	TPS_REG(LDO_8, ldo8),
	TPS_REG(LDO_9, ldo9),
	{
	 .id = 0,
	 .name = "tps6586x-rtc",
	 .platform_data = &rtc_data,
	 },
};

static struct tps6586x_platform_data tps_platform = {
	.irq_base = TEGRA_NR_IRQS,
	.num_subdevs = ARRAY_SIZE(tps_devs),
	.subdevs = tps_devs,
	.gpio_base = PICASSO_TPS6586X_GPIO_BASE,
};

static struct i2c_board_info __initdata picasso_regulators[] = {
	{
	 I2C_BOARD_INFO("tps6586x", 0x34),
	 .irq = INT_EXTERNAL_PMU,
	 .platform_data = &tps_platform,
	 },
};

int __init picasso_regulator_init(void)
{
	void __iomem *pmc = IO_ADDRESS(TEGRA_PMC_BASE);
	u32 pmc_ctrl;

	/* configure the power management controller to trigger PMU
	 * interrupts when low */
	pmc_ctrl = readl(pmc + PMC_CTRL);
	writel(pmc_ctrl | PMC_CTRL_INTR_LOW, pmc + PMC_CTRL);

	regulator_has_full_constraints();
	/* set initial_mode to MODE_FAST for SM1 */
	reg_sm1_data.constraints.initial_mode = REGULATOR_MODE_FAST;

	i2c_register_board_info(4, picasso_regulators, 1);
	return 0;
}

static int __init picasso_pcie_init(void)
{
	int ret;

	ret = gpio_request(PICASSO_TPS6586X_GPIO_BASE, "pcie_vdd");
	if (ret < 0)
		goto fail;

	ret = gpio_direction_output(PICASSO_TPS6586X_GPIO_BASE, 1);
	if (ret < 0)
		goto fail;

	gpio_export(PICASSO_TPS6586X_GPIO_BASE, false);
	return 0;

 fail:
	pr_err("%s: gpio_request failed #%d\n", __func__, PICASSO_TPS6586X_GPIO_BASE);
	gpio_free(PICASSO_TPS6586X_GPIO_BASE);
	return ret;
}

late_initcall(picasso_pcie_init);
