/*
 * drivers/sys/sys-acer-picasso.c
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
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#include <linux/mfd/acer_picasso_ec.h>

static struct acer_picasso_ec_priv *priv = NULL;

static void picasso_shutdown(void) {
	priv->write(priv->client, EC_SYS_SHUTDOWN, 0);
	
	local_irq_disable();
	while (1);
}

static int picasso_sys_probe(struct platform_device *pdev)
{
	priv = dev_get_drvdata(pdev->dev.parent);
	if (!priv) {
		dev_err(&pdev->dev, "no private data supplied\n");
		return -EINVAL;
	}
	pm_power_off = picasso_shutdown;
	return 0;
}

#if CONFIG_PM
static int picasso_sys_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	return 0;
}

static int picasso_sys_resume(struct platform_device *pdev)
{
	return 0;
}
#else
#define picasso_sys_suspend NULL
#define picasso_sys_resume NULL
#endif

static struct platform_driver picasso_sys_driver = {
	.probe		= picasso_sys_probe,
	.suspend	= picasso_sys_suspend,
	.resume		= picasso_sys_resume,
	.driver		= {
		.name		= PICASSO_EC_SYS_ID,
		.owner		= THIS_MODULE,
	},
};

static int __init picasso_sys_init(void)
{
	return platform_driver_register(&picasso_sys_driver);
}

static void __exit picasso_sys_exit(void)
{
	platform_driver_unregister(&picasso_sys_driver);
}

module_init(picasso_sys_init);
module_exit(picasso_sys_exit)
