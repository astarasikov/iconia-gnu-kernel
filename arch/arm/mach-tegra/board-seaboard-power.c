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
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/mfd/tps6586x.h>
#include <linux/gpio.h>
#include <linux/power/gpio-charger.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <mach/iomap.h>
#include <mach/powergate.h>
#include <linux/err.h>

#include <asm/mach-types.h>
#include "board-seaboard.h"
#include "gpio-names.h"

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
	REGULATOR_SUPPLY("vddio_sys", "panjit_touch"),
};
static struct regulator_consumer_supply tps658621_ldo5_supply[] = {
	REGULATOR_SUPPLY("vcore_mmc", "sdhci-tegra.1"),
	REGULATOR_SUPPLY("vcore_mmc", "sdhci-tegra.3"),
};
static struct regulator_consumer_supply tps658621_ldo6_supply[] = {
	REGULATOR_SUPPLY("vddio_vi", NULL),
	REGULATOR_SUPPLY("vdd_dmic", NULL),
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

static struct regulator_consumer_supply wwan_pwr_consumer_supply[] = {
	REGULATOR_SUPPLY("vcc_modem3v", NULL),
};

struct regulator_init_data wwan_pwr_initdata = {
	.consumer_supplies = wwan_pwr_consumer_supply,
	.num_consumer_supplies = 1,
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.always_on = 1,
	},
};

static struct fixed_voltage_config wwan_pwr = {
	.supply_name		= "si4825",
	.microvolts		= 3300000, /* 3.3V */
	.gpio			= TPS_GPIO_WWAN_PWR,
	.startup_delay		= 0,
	.enable_high		= 1,
	.enabled_at_boot	= 1,
	.init_data		= &wwan_pwr_initdata,
};

static struct regulator_consumer_supply vdd_1v5_consumer_supply[] = {
	REGULATOR_SUPPLY("vdd_1v5", NULL),
};

struct regulator_init_data vdd_1v5_initdata = {
	.consumer_supplies = vdd_1v5_consumer_supply,
	.num_consumer_supplies = 1,
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.always_on = 0,
	},
};

static struct fixed_voltage_config vdd_1v5 = {
	.supply_name		= "vdd_1v5",
	.microvolts		= 1500000, /* 1.5V */
	.gpio			= TPS_GPIO_EN_1V5,
	.startup_delay		= 0,
	.enable_high		= 0,
	.enabled_at_boot	= 0,
	.init_data		= &vdd_1v5_initdata,
};

#define REGULATOR_INIT(_id, _minmv, _maxmv, _always_on)			\
	{								\
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

static struct regulator_init_data sm0_data = REGULATOR_INIT(sm0, 950, 1300, true);
static struct regulator_init_data sm1_data = REGULATOR_INIT(sm1, 750, 1275, true);
static struct regulator_init_data sm2_data = REGULATOR_INIT(sm2, 3000, 4550, true);
static struct regulator_init_data ldo0_data = REGULATOR_INIT(ldo0, 1250, 3300, false);
static struct regulator_init_data ldo1_data = REGULATOR_INIT(ldo1, 1100, 1100, true);
static struct regulator_init_data ldo2_data = REGULATOR_INIT(ldo2, 900, 1300, false);
static struct regulator_init_data ldo3_data = REGULATOR_INIT(ldo3, 3300, 3300, true);
static struct regulator_init_data ldo4_data = REGULATOR_INIT(ldo4, 1800, 1800, true);
static struct regulator_init_data ldo5_data = REGULATOR_INIT(ldo5, 2850, 3300, true);
static struct regulator_init_data ldo6_data = REGULATOR_INIT(ldo6, 1800, 1800, false);
static struct regulator_init_data ldo7_data = REGULATOR_INIT(ldo7, 3300, 3300, false);
static struct regulator_init_data ldo8_data = REGULATOR_INIT(ldo8, 1800, 1800, false);
static struct regulator_init_data ldo9_data = REGULATOR_INIT(ldo9, 2850, 2850, true);

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
	TPS_GPIO_FIXED_REG(0, &wwan_pwr),
	TPS_GPIO_FIXED_REG(1, &vdd_1v5),
	{
		.id	= 0,
		.name	= "tps6586x-rtc",
		.platform_data	= &rtc_data,
	},
};

static struct tps6586x_platform_data tps_platform = {
	.irq_base = TEGRA_NR_IRQS,
	.num_subdevs = ARRAY_SIZE(tps_devs),
	.subdevs = tps_devs,
	.gpio_base = TPS_GPIO_BASE,
};

static struct i2c_board_info __initdata seaboard_regulators[] = {
	{
		I2C_BOARD_INFO("tps6586x", 0x34),
		.irq		= INT_EXTERNAL_PMU,
		.platform_data	= &tps_platform,
	},
};

int __init seaboard_regulator_init(void)
{
	void __iomem *pmc = IO_ADDRESS(TEGRA_PMC_BASE);
	u32 pmc_ctrl;

	/* configure the power management controller to trigger PMU
	 * interrupts when low
	 */
	pmc_ctrl = readl(pmc + PMC_CTRL);
	writel(pmc_ctrl | PMC_CTRL_INTR_LOW, pmc + PMC_CTRL);

	regulator_has_full_constraints();

	/* set initial_mode to MODE_FAST for SM1 */
	sm1_data.constraints.initial_mode = REGULATOR_MODE_FAST;

	i2c_register_board_info(4, seaboard_regulators, 1);
	return 0;
}

/* AC power */
static char *tegra_batteries[] = {
	"battery",
};

static struct resource seaboard_ac_resources[] = {
	[0] = {
		.name = "ac",
		.start = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_AC_ONLINE),
		.end = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_AC_ONLINE),
		.flags = (IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE |
				IORESOURCE_IRQ_LOWEDGE),
	},
};

static struct gpio_charger_platform_data seaboard_ac_platform_data = {
	.name			= "ac",
	.gpio			= TEGRA_GPIO_AC_ONLINE,
	.gpio_active_low	= 1,
	.supplied_to		= tegra_batteries,
	.type			= POWER_SUPPLY_TYPE_MAINS,
	.num_supplicants	= ARRAY_SIZE(tegra_batteries),
};

static struct platform_device seaboard_ac_power_device = {
	.name		= "gpio-charger",
	.id		= 0,
	.resource	= seaboard_ac_resources,
	.num_resources	= ARRAY_SIZE(seaboard_ac_resources),
	.dev = {
		.platform_data	= &seaboard_ac_platform_data,
	},
};

static unsigned disable_charger_gpio = TEGRA_GPIO_DISABLE_CHARGER;

int __init seaboard_ac_power_init(void)
{
	int err;

	err = gpio_request(TEGRA_GPIO_AC_ONLINE, "ac online");
	if (err < 0) {
		pr_err("could not acquire ac online GPIO\n");
	} else {
		gpio_direction_input(TEGRA_GPIO_AC_ONLINE);
		gpio_free(TEGRA_GPIO_AC_ONLINE);
	}

	err = gpio_request(disable_charger_gpio, "disable charger");
	if (err < 0) {
		pr_err("could not acquire charger disable\n");
	} else {
		gpio_direction_output(disable_charger_gpio, 0);
		gpio_free(disable_charger_gpio);
	}

	err = platform_device_register(&seaboard_ac_power_device);
	return err;
}

static void seaboard_power_off(void)
{
	int ret;

	ret = tps6586x_power_off();
	if (ret)
		pr_err("Failed to power off\n");

	while(1);
}

int __init seaboard_power_init(void)
{
	int err;

	err = seaboard_regulator_init();
	if (err < 0)
		pr_warning("Unable to initialize regulator\n");

	if (machine_is_ventana())
		disable_charger_gpio = TEGRA_GPIO_VENTANA_DISABLE_CHARGER;

	err = seaboard_ac_power_init();
	if (err < 0)
		pr_warning("Unable to initialize ac power\n");

	pm_power_off = seaboard_power_off;

	tegra_powergate_power_off(TEGRA_POWERGATE_PCIE);

	return 0;
}
