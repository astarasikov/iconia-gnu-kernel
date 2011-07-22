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
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/mfd/tps6586x.h>

#include <mach/irqs.h>
#include <mach/iomap.h>
#include <linux/err.h>

#include "board-harmony.h"

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
	REGULATOR_SUPPLY("pex_clk", NULL),
};
static struct regulator_consumer_supply tps658621_ldo1_supply[] = {
	REGULATOR_SUPPLY("vdd_plla_p_c", NULL),
	REGULATOR_SUPPLY("vdd_pllm", NULL),
	REGULATOR_SUPPLY("vdd_pllu", NULL),
	REGULATOR_SUPPLY("vdd_pllx", NULL),
};
static struct regulator_consumer_supply tps658621_ldo2_supply[] = {
	REGULATOR_SUPPLY("vdd_rtc", NULL),
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
	REGULATOR_SUPPLY("vcore_mmc", "sdhci-tegra.0"),
	REGULATOR_SUPPLY("vcore_mmc", "sdhci-tegra.1"),
	REGULATOR_SUPPLY("vcore_mmc", "sdhci-tegra.2"),
};
static struct regulator_consumer_supply tps658621_ldo6_supply[] = {
	REGULATOR_SUPPLY("avdd_vdac", NULL),
};
static struct regulator_consumer_supply tps658621_ldo7_supply[] = {
	REGULATOR_SUPPLY("avdd_hdmi", NULL),
};
static struct regulator_consumer_supply tps658621_ldo8_supply[] = {
	REGULATOR_SUPPLY("avdd_hdmi_pll", NULL),
};
static struct regulator_consumer_supply tps658621_ldo9_supply[] = {
	REGULATOR_SUPPLY("vdd_ddr_rx", NULL),
	REGULATOR_SUPPLY("avdd_cam", NULL),
	REGULATOR_SUPPLY("avdd_amp", NULL),
};

/* regulator supplies power to WWAN - by default disable */
static struct regulator_consumer_supply vdd_1v5_consumer_supply[] = {
	REGULATOR_SUPPLY("vdd_1v5", NULL),
};

static struct regulator_init_data vdd_1v5_initdata = {
	.consumer_supplies = vdd_1v5_consumer_supply,
	.num_consumer_supplies = 1,
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.always_on = 0,
	},
};

static struct fixed_voltage_config vdd_1v5 = {
	.supply_name		= "vdd_1v5",
	.microvolts		= 1500000, /* Enable 1.5V */
	.gpio			= TPS_GPIO_EN_1V5, /* GPIO BASE+0 */
	.startup_delay		= 0,
	.enable_high		= 0,
	.enabled_at_boot	= 0,
	.init_data		= &vdd_1v5_initdata,
};

/* regulator supplies power to WLAN - enable here, to satisfy SDIO probing */
static struct regulator_consumer_supply vdd_1v2_consumer_supply[] = {
	REGULATOR_SUPPLY("vdd_1v2", NULL),
};

static struct regulator_init_data vdd_1v2_initdata = {
	.consumer_supplies = vdd_1v2_consumer_supply,
	.num_consumer_supplies = 1,
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.always_on = 1,
	},
};

static struct fixed_voltage_config vdd_1v2 = {
	.supply_name		= "vdd_1v2",
	.microvolts		= 1200000, /* Enable 1.2V */
	.gpio			= TPS_GPIO_EN_1V2, /* GPIO BASE+1 */
	.startup_delay		= 0,
	.enable_high		= 1,
	.enabled_at_boot	= 1,
	.init_data		= &vdd_1v2_initdata,
};

/* regulator supplies power to PLL - enable here */
static struct regulator_consumer_supply vdd_1v05_consumer_supply[] = {
	REGULATOR_SUPPLY("vdd_1v05", NULL),
};

static struct regulator_init_data vdd_1v05_initdata = {
	.consumer_supplies = vdd_1v05_consumer_supply,
	.num_consumer_supplies = 1,
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.always_on = 1,
	},
};

static struct fixed_voltage_config vdd_1v05 = {
	.supply_name		= "vdd_1v05",
	.microvolts		= 1050000, /* Enable 1.05V */
	.gpio			= TPS_GPIO_EN_1V05, /* BASE+2 */
	.startup_delay		= 0,
	.enable_high		= 1,
	.enabled_at_boot	= 0,
	.init_data		= &vdd_1v05_initdata,
};

/* mode pin for 1.05V regulator - enable here */
static struct regulator_consumer_supply vdd_1v05_mode_consumer_supply[] = {
	REGULATOR_SUPPLY("vdd_1v05_mode", NULL),
};

static struct regulator_init_data vdd_1v05_mode_initdata = {
	.consumer_supplies = vdd_1v05_mode_consumer_supply,
	.num_consumer_supplies = 1,
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.always_on = 1,
	},
};

static struct fixed_voltage_config vdd_1v05_mode = {
	.supply_name		= "vdd_1v05_mode",
	.microvolts		= 1050000, /* Enable 1.05V */
	.gpio			= TPS_GPIO_MODE_1V05, /* BASE+3 */
	.startup_delay		= 0,
	.enable_high		= 1,
	.enabled_at_boot	= 0,
	.init_data		= &vdd_1v05_mode_initdata,
};

#define REGULATOR_INIT(_id, _minmv, _maxmv, _always_on)			\
	static struct regulator_init_data _id##_data = {		\
		.constraints = {					\
			.min_uV = (_minmv)*1000,			\
			.max_uV = (_maxmv)*1000,			\
			.valid_modes_mask = (REGULATOR_MODE_NORMAL |	\
					     REGULATOR_MODE_FAST),	\
			.valid_ops_mask = (REGULATOR_CHANGE_MODE |	\
					   REGULATOR_CHANGE_STATUS |	\
					   REGULATOR_CHANGE_VOLTAGE),	\
			.always_on = _always_on,			\
			.apply_uV = (_minmv == _maxmv),			\
		},							\
		.num_consumer_supplies = ARRAY_SIZE(tps658621_##_id##_supply),\
		.consumer_supplies = tps658621_##_id##_supply,		\
	}

REGULATOR_INIT(sm0, 950, 1300, true);
REGULATOR_INIT(sm1, 750, 1125, true);
REGULATOR_INIT(sm2, 3000, 4550, true);
REGULATOR_INIT(ldo0, 1250, 3300, false);
REGULATOR_INIT(ldo1, 725, 1500, false);
REGULATOR_INIT(ldo2, 725, 1500, false);
REGULATOR_INIT(ldo3, 3300, 3300, true);
REGULATOR_INIT(ldo4, 1700, 2475, false);
REGULATOR_INIT(ldo5, 1250, 3300, false);
REGULATOR_INIT(ldo6, 1250, 3300, false);
REGULATOR_INIT(ldo7, 1250, 3300, false);
REGULATOR_INIT(ldo8, 1250, 3300, false);
REGULATOR_INIT(ldo9, 1250, 3300, false);

static struct tps6586x_rtc_platform_data rtc_data = {
	.irq = TEGRA_NR_IRQS + TPS6586X_INT_RTC_ALM1,
};

#define TPS_REG(_id, _data)			\
	{					\
		.id = TPS6586X_ID_##_id,	\
		.name = "tps6586x-regulator",	\
		.platform_data = _data,		\
	}

#define TPS_GPIO_FIXED_REG(_id, _data)		\
	{					\
		.id = _id,			\
		.name = "reg-fixed-voltage",	\
		.platform_data = _data,		\
	}

static struct tps6586x_subdev_info tps_devs[] = {
	TPS_REG(SM_0, &sm0_data),
	TPS_REG(SM_1, &sm1_data),
	TPS_REG(SM_2, &sm2_data),
	TPS_REG(LDO_0, &ldo0_data),
	TPS_REG(LDO_1, &ldo1_data),
	TPS_REG(LDO_2, &ldo2_data),
	TPS_REG(LDO_3, &ldo3_data),
	TPS_REG(LDO_4, &ldo4_data),
	TPS_REG(LDO_5, &ldo5_data),
	TPS_REG(LDO_6, &ldo6_data),
	TPS_REG(LDO_7, &ldo7_data),
	TPS_REG(LDO_8, &ldo8_data),
	TPS_REG(LDO_9, &ldo9_data),
	TPS_GPIO_FIXED_REG(0, &vdd_1v5),
	TPS_GPIO_FIXED_REG(1, &vdd_1v2),
	TPS_GPIO_FIXED_REG(2, &vdd_1v05),
	TPS_GPIO_FIXED_REG(3, &vdd_1v05_mode),
	{
		.id	= 0,
		.name	= "tps6586x-rtc",
		.platform_data	= &rtc_data,
	},
};

static struct tps6586x_platform_data tps_platform = {
	.irq_base	= TEGRA_NR_IRQS,
	.num_subdevs	= ARRAY_SIZE(tps_devs),
	.subdevs	= tps_devs,
	.gpio_base	= HARMONY_GPIO_TPS6586X(0),
};

static struct i2c_board_info __initdata harmony_regulators[] = {
	{
		I2C_BOARD_INFO("tps6586x", 0x34),
		.irq		= INT_EXTERNAL_PMU,
		.platform_data	= &tps_platform,
	},
};

int __init harmony_regulator_init(void)
{
	void __iomem *pmc = IO_ADDRESS(TEGRA_PMC_BASE);
	u32 pmc_ctrl;

	/* configure the power management controller to trigger PMU
	 * interrupts when low
	 */
	pmc_ctrl = readl(pmc + PMC_CTRL);
	writel(pmc_ctrl | PMC_CTRL_INTR_LOW, pmc + PMC_CTRL);

	i2c_register_board_info(4, harmony_regulators, 1);

	return 0;
}

static void harmony_power_off(void)
{
	int ret;

	ret = tps6586x_power_off();
	if (ret)
		pr_err("Failed to power off\n");

	while(1);
}

int __init harmony_power_init(void)
{
	int err;

	err = harmony_regulator_init();
	if (err < 0)
		pr_warning("Unable to initialize regulator\n");

	pm_power_off = harmony_power_off;

	return 0;
}
