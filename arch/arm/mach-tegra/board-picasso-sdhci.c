/*
 * arch/arm/mach-tegra/board-harmony-sdhci.c
 *
 * Copyright (C) 2010 Google, Inc.
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

#include <linux/resource.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/mmc/host.h>

#include <asm/mach-types.h>
#include <mach/irqs.h>
#include <mach/iomap.h>
#include <mach/sdhci.h>

#include "gpio-names.h"
#include "board.h"

#define PICASSO_WLAN_PWR	TEGRA_GPIO_PK5
#define PICASSO_WLAN_RST	TEGRA_GPIO_PK6
#define PBJ20_WIFI_IRQ_GPIO	TEGRA_GPIO_PS0


static void (*wifi_status_cb)(int card_present, void *dev_id);
static void *wifi_status_cb_devid;
static int picasso_wifi_status_register(void (*callback)(int , void *), void *);
static struct clk *wifi_32k_clk;

static int picasso_wifi_reset(int on);
static int picasso_wifi_power(int on);
static int picasso_wifi_set_carddetect(int val);

/*
#if defined(CONFIG_BCM4329_HW_OOB) || defined(CONFIG_BCM4329_OOB_INTR_ONLY)

static struct resource picasso_wifi_wakeup_resources[] = {
	{
		.name   = "bcm4329_wlan_irq",
		.start  = TEGRA_GPIO_TO_IRQ(PBJ20_WIFI_IRQ_GPIO),
		.end    = TEGRA_GPIO_TO_IRQ(PBJ20_WIFI_IRQ_GPIO),
		.flags  = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL | IORESOURCE_IRQ_SHAREABLE,
	},
};

static struct wifi_platform_data picasso_wifi_control = {
	.set_power      = picasso_wifi_power,
	.set_reset      = picasso_wifi_reset,
	.set_carddetect = picasso_wifi_set_carddetect,
};

static struct platform_device picasso_wifi_device = {
	.name           = "bcm4329_wlan",
	.id             = 1,
	.num_resources  = ARRAY_SIZE(picasso_wifi_wakeup_resources),
	.resource       = picasso_wifi_wakeup_resources,
	.dev            = {
		.platform_data = &picasso_wifi_control,
	},
};

#else

static struct wifi_platform_data picasso_wifi_control = {
	.set_power      = picasso_wifi_power,
	.set_reset      = picasso_wifi_reset,
	.set_carddetect = picasso_wifi_set_carddetect,
};

static struct platform_device picasso_wifi_device = {
	.name           = "bcm4329_wlan",
	.id             = 1,
	.dev            = {
		.platform_data = &picasso_wifi_control,
	},
};

#endif
*/


static struct resource sdhci_resource0[] = {
	[0] = {
		.start  = INT_SDMMC1,
		.end    = INT_SDMMC1,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_SDMMC1_BASE,
		.end	= TEGRA_SDMMC1_BASE + TEGRA_SDMMC1_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource sdhci_resource2[] = {
	[0] = {
		.start  = INT_SDMMC3,
		.end    = INT_SDMMC3,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_SDMMC3_BASE,
		.end	= TEGRA_SDMMC3_BASE + TEGRA_SDMMC3_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource sdhci_resource3[] = {
	[0] = {
		.start  = INT_SDMMC4,
		.end    = INT_SDMMC4,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_SDMMC4_BASE,
		.end	= TEGRA_SDMMC4_BASE + TEGRA_SDMMC4_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};


static struct tegra_sdhci_platform_data tegra_sdhci_platform_data0 = {
	//.register_status_notify	= picasso_wifi_status_register,
	/*.cccr   = {
		.sdio_vsn       = 2,
		.multi_block    = 1,
		.low_speed      = 0,
		.wide_bus       = 0,
		.high_power     = 1,
		.high_speed     = 1,
	},
	.cis  = {
		.vendor         = 0x02d0,
		.device         = 0x4329,
	},*/
	.cd_gpio = -1,
	.wp_gpio = -1,
	.power_gpio = -1,
};

static struct tegra_sdhci_platform_data tegra_sdhci_platform_data2 = {
	.cd_gpio = TEGRA_GPIO_PI5,
	.wp_gpio = -1,
	.power_gpio = TEGRA_GPIO_PI6,
};

static struct tegra_sdhci_platform_data tegra_sdhci_platform_data3 = {
	.cd_gpio = -1,
	.wp_gpio = -1,
	.power_gpio = -1,
};

static struct platform_device tegra_sdhci_device0 = {
	.name		= "sdhci-tegra",
	.id		= 0,
	.resource	= sdhci_resource0,
	.num_resources	= ARRAY_SIZE(sdhci_resource0),
	.dev = {
		.platform_data = &tegra_sdhci_platform_data0,
	},
};

static struct platform_device tegra_sdhci_device2 = {
	.name		= "sdhci-tegra",
	.id		= 2,
	.resource	= sdhci_resource2,
	.num_resources	= ARRAY_SIZE(sdhci_resource2),
	.dev = {
		.platform_data = &tegra_sdhci_platform_data2,
	},
};

static struct platform_device tegra_sdhci_device3 = {
	.name		= "sdhci-tegra",
	.id		= 3,
	.resource	= sdhci_resource3,
	.num_resources	= ARRAY_SIZE(sdhci_resource3),
	.dev = {
		.platform_data = &tegra_sdhci_platform_data3,
	},
};

static int picasso_wifi_status_register(
		void (*callback)(int card_present, void *dev_id),
		void *dev_id)
{
	if (wifi_status_cb)
		return -EAGAIN;
	wifi_status_cb = callback;
	wifi_status_cb_devid = dev_id;
	return 0;
}

static int picasso_wifi_set_carddetect(int val)
{
	pr_debug("%s: %d\n", __func__, val);
	if (wifi_status_cb)
		wifi_status_cb(val, wifi_status_cb_devid);
	else
		pr_warning("%s: Nobody to notify\n", __func__);
	return 0;
}

static int picasso_wifi_power(int on)
{
	pr_debug("%s: %d\n", __func__, on);

	if (on)
		gpio_direction_input(PBJ20_WIFI_IRQ_GPIO);
	else
		gpio_direction_output(PBJ20_WIFI_IRQ_GPIO, 0);
	mdelay(50);
	gpio_set_value(PICASSO_WLAN_RST, on);
	mdelay(80);

	if (on)
		clk_enable(wifi_32k_clk);
	else
		clk_disable(wifi_32k_clk);

	return 0;
}

static int picasso_wifi_reset(int on)
{
	pr_debug("%s: do nothing\n", __func__);
	return 0;
}

static int __init picasso_wifi_init(void)
{
	wifi_32k_clk = clk_get_sys(NULL, "blink");
	if (IS_ERR(wifi_32k_clk)) {
		pr_err("%s: unable to get blink clock\n", __func__);
		return PTR_ERR(wifi_32k_clk);
	}

#if defined(CONFIG_BCM4329_HW_OOB) || defined(CONFIG_BCM4329_OOB_INTR_ONLY)
	gpio_request(PBJ20_WIFI_IRQ_GPIO,"oob irq");
	tegra_gpio_enable(PBJ20_WIFI_IRQ_GPIO);
	gpio_direction_input(PBJ20_WIFI_IRQ_GPIO);
#endif

	gpio_request(PICASSO_WLAN_PWR, "wlan_power");
	gpio_request(PICASSO_WLAN_RST, "wlan_rst");

	tegra_gpio_enable(PICASSO_WLAN_PWR);
	tegra_gpio_enable(PICASSO_WLAN_RST);

	gpio_direction_output(PICASSO_WLAN_PWR, 0);
	gpio_direction_output(PICASSO_WLAN_RST, 0);

	//platform_device_register(&picasso_wifi_device);

	//device_init_wakeup(&picasso_wifi_device.dev, 1);
	//device_set_wakeup_enable(&picasso_wifi_device.dev, 0);

	return 0;
}
int __init picasso_sdhci_init(void)
{
	platform_device_register(&tegra_sdhci_device3);
	platform_device_register(&tegra_sdhci_device2);
	//platform_device_register(&tegra_sdhci_device0);

	//picasso_wifi_init();
	return 0;
}
