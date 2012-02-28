/*
 * drivers/power/acer_picasso_battery.c
 *
 * Driver for EC in Acer Iconia A500
 *
 * Copyright (C) 2011 Alexander Tarasikov <alexander.tarasikov@gmail.com>
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
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mfd/acer_picasso_ec.h>

#define EC_POLL_PERIOD 30000	/* 30 Seconds */

static struct timer_list poll_timer;
static struct acer_picasso_ec_priv *priv = NULL;

static struct power_supply picasso_battery_supply;

static void tegra_battery_poll_timer_func(unsigned long unused)
{
	power_supply_changed(&picasso_battery_supply);
	mod_timer(&poll_timer, jiffies + msecs_to_jiffies(EC_POLL_PERIOD));
}

static s32 picasso_battery_read_register(enum picasso_ec_reg reg) {
	s32 ret;
	ret = priv->read(priv->client, reg);
	if (!ret) {
		msleep(500);
		ret = priv->read(priv->client, reg);
	}
	if (ret < 0) {
		dev_err(&priv->client->dev, "failed reading EC register %02x\n", reg);
	}
	return ret;
}

static int picasso_battery_get_condition(enum power_supply_property psp,
	union power_supply_propval *val)
{
	s32 ret;
	ret = picasso_battery_read_register(EC_BATT_DESIGN_CAPACITY);
	if (ret < 0) {
		return ret;
	}

	if (psp == POWER_SUPPLY_PROP_PRESENT) {
		val->intval = !!ret;
	}
	else {
		val->intval = ret ?
			POWER_SUPPLY_HEALTH_GOOD : POWER_SUPPLY_HEALTH_UNKNOWN;
	}
	return 0;
}

static int picasso_battery_get_status(struct power_supply *psy,
union power_supply_propval *val) {
	s32 ret, capacity;
	int bat_present, ac_status;
	
	ret = picasso_battery_read_register(EC_BATT_DESIGN_CAPACITY);
	if (ret < 0) {
		return ret;
	}
	bat_present = !!ret;

	ret = picasso_battery_read_register(EC_BATT_CAPACITY);
	if (ret < 0) {
		return ret;
	}
	capacity = ret;
	ac_status = power_supply_am_i_supplied(psy);

	if (capacity < 100) {
		if (ac_status) {
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		} else {
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		}
	} else {
		if (ac_status) {
			val->intval = POWER_SUPPLY_STATUS_FULL;
		} else {
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		}
	}

	if (!bat_present) {
		val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
	}

	return 0;
}

static int picasso_battery_get_temperature(union power_supply_propval *val) {
	s32 ret;
	ret = picasso_battery_read_register(EC_BATT_TEMPERATURE);
	if (ret < 0) {
		return ret;
	}
	val->intval = ret - 2731;
	return 0;
}

static int picasso_battery_get_voltage(union power_supply_propval *val) {
	s32 ret;
	ret = picasso_battery_read_register(EC_BATT_VOLTAGE);
	if (ret < 0) {
		return ret;
	}
	val->intval = ret * 1000;
	return 0;
}

static int picasso_battery_get_cycle_count(union power_supply_propval *val) {
	s32 ret;
	ret = picasso_battery_read_register(EC_BATT_CYCLE_COUNT);
	if (ret < 0) {
		return ret;
	}
	val->intval = ret;
	return 0;
}

static int picasso_battery_get_current_now(union power_supply_propval *val) {
	s32 ret;
	s16 curr;
	ret = picasso_battery_read_register(EC_BATT_CURRENT_NOW);
	if (ret < 0) {
		return ret;
	}
	curr = ret & 0xffff;
	val->intval = curr * 1000;
	return 0;
}

static int picasso_battery_get_battery_capacity(union power_supply_propval *val)
{
	s32 ret;
	
	ret = picasso_battery_read_register(EC_BATT_CAPACITY);

	if (ret < 0) {
		dev_err(&priv->client->dev, "i2c read for charge failed\n");
		return ret;
	}

	val->intval = ((ret >= 100) ? 100 : ret);
	return 0;
}

static int picasso_battery_get_property(struct power_supply *psy,
			       enum power_supply_property psp,
			       union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		return picasso_battery_get_condition(psp, val);
	case POWER_SUPPLY_PROP_HEALTH:
		return picasso_battery_get_condition(psp, val);
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		return picasso_battery_get_battery_capacity(val);
	case POWER_SUPPLY_PROP_STATUS:
		return picasso_battery_get_status(psy, val);
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return picasso_battery_get_voltage(val);
	case POWER_SUPPLY_PROP_TEMP:
		return picasso_battery_get_temperature(val);
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		return picasso_battery_get_cycle_count(val);
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		return picasso_battery_get_current_now(val);
	default:
		dev_err(&priv->client->dev,
			"%s: INVALID property\n", __func__);
		return -EINVAL;
	}
	return 0;
}

static enum power_supply_property picasso_battery_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_TEMP,
};

static struct power_supply picasso_battery_supply = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = picasso_battery_properties,
	.num_properties = ARRAY_SIZE(picasso_battery_properties),
	.get_property = picasso_battery_get_property,
};

static int picasso_battery_probe(struct platform_device *pdev)
{
	int ret = 0;
	priv = dev_get_drvdata(pdev->dev.parent);
	if (!priv) {
		dev_err(&pdev->dev, "no private data supplied\n");
		return -EINVAL;
	}

	ret = power_supply_register(&pdev->dev, &picasso_battery_supply);
	if (ret) {
		dev_err(&pdev->dev, "failed to register power supply\n");
		priv = NULL;
		return ret;
	}
	setup_timer(&poll_timer, tegra_battery_poll_timer_func, 0);
	mod_timer(&poll_timer, jiffies + msecs_to_jiffies(EC_POLL_PERIOD));

	return 0;
}

static int picasso_battery_remove(struct platform_device *pdev)
{
	power_supply_unregister(&picasso_battery_supply);
	priv = NULL;
	return 0;
}

#if CONFIG_PM
static int picasso_battery_suspend(struct platform_device *pdev,
				   pm_message_t mesg)
{
	del_timer_sync(&poll_timer);
	return 0;
}

static int picasso_battery_resume(struct platform_device *pdev)
{
	setup_timer(&poll_timer, tegra_battery_poll_timer_func, 0);
	mod_timer(&poll_timer, jiffies + msecs_to_jiffies(EC_POLL_PERIOD));
	return 0;
}
#else
#define picasso_battery_suspend NULL
#define picasso_battery_resume NULL
#endif

static struct platform_driver picasso_battery_driver = {
	.probe = picasso_battery_probe,
	.remove = picasso_battery_remove,
#ifdef CONFIG_PM
	.suspend = picasso_battery_suspend,
	.resume = picasso_battery_resume,
#endif
	.driver = {
		.name = PICASSO_EC_BAT_ID,
		.owner = THIS_MODULE,
	},
};

static int __init picasso_battery_init(void)
{
	return platform_driver_register(&picasso_battery_driver);
}

static void __exit picasso_battery_exit(void)
{
	platform_driver_unregister(&picasso_battery_driver);
}

module_init(picasso_battery_init);
module_exit(picasso_battery_exit);

MODULE_AUTHOR("Alexander Tarasikov <alexander.tarasikov@gmail.com>");
MODULE_DESCRIPTION("Acer Iconia A500 battery driver");
MODULE_LICENSE("GPL");
