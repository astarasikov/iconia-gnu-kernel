/*
 * drivers/leds/leds-acer-picasso.c
 * The driver for LEDs in Acer Iconia Tab A500 tablet computer
 *
 * Copyright (C) 2011 Alexander Tarasikov <alexander.tarasikov@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#include <linux/mfd/acer_picasso_ec.h>

static void picasso_update_color_leds(struct work_struct* work);
static void picasso_set_brightness_color(struct led_classdev *led_cdev,
					 enum led_brightness brightness);

static DECLARE_WORK(colorled_wq, picasso_update_color_leds);
static struct acer_picasso_ec_priv *priv = NULL;

enum picasso_led {ORANGE, WHITE, PICASSO_LED_MAX};

static struct led_classdev picasso_leds[] = {
	[ORANGE] = {
	 .name = "orange",
	 .brightness_set = picasso_set_brightness_color,
	 },
	[WHITE] = {
	 .name = "white",
	 .brightness_set = picasso_set_brightness_color,
	 },
};

static void picasso_update_color_leds(struct work_struct* work) {
	if (!picasso_leds[ORANGE].brightness && !picasso_leds[WHITE].brightness) {
		priv->write(priv->client, 0x40, 0);
		return;
	}
	if (picasso_leds[WHITE].brightness) {
		priv->write(priv->client, 0x42, 0);
	}
	if (picasso_leds[ORANGE].brightness) {
		priv->write(priv->client, 0x43, 0);
	}
}

static void picasso_set_brightness_color(struct led_classdev *led_cdev,
					 enum led_brightness brightness)
{
	schedule_work(&colorled_wq);
}

static int picasso_leds_probe(struct platform_device *pdev)
{
	int ret = 0;
	int i;

	priv = dev_get_drvdata(pdev->dev.parent);
	if (!priv) {
		dev_err(&pdev->dev, "no private data supplied\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(picasso_leds); i++) {
		ret = led_classdev_register(&pdev->dev, &picasso_leds[i]);
		if (ret < 0) {
			goto led_fail;
		}
	}

	return 0;

led_fail:
	for (i--; i >= 0; i--) {
		led_classdev_unregister(&picasso_leds[i]);
	}
	return ret;
}

static int picasso_leds_remove(struct platform_device *pdev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(picasso_leds); i++) {
		led_classdev_unregister(&picasso_leds[i]);
	}
	priv = NULL;
	return 0;
}

#if CONFIG_PM
static int picasso_leds_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	cancel_work_sync(&colorled_wq);
	return 0;
}

static int picasso_leds_resume(struct platform_device *pdev)
{
	return 0;
}
#else
#define picasso_leds_suspend NULL
#define picasso_leds_resume NULL
#endif

static struct platform_driver picasso_leds_driver = {
	.probe		= picasso_leds_probe,
	.remove		= picasso_leds_remove,
	.suspend	= picasso_leds_suspend,
	.resume		= picasso_leds_resume,
	.driver		= {
		.name		= PICASSO_EC_LED_ID,
		.owner		= THIS_MODULE,
	},
};

static int __init picasso_leds_init(void)
{
	return platform_driver_register(&picasso_leds_driver);
}

static void __exit picasso_leds_exit(void)
{
	platform_driver_unregister(&picasso_leds_driver);
}

module_init(picasso_leds_init);
module_exit(picasso_leds_exit)
