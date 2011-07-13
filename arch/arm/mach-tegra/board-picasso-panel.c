/*
 * arch/arm/mach-tegra/board-picasso-panel.c
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

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/resource.h>
#include <asm/mach-types.h>
#include <linux/platform_device.h>
#include <linux/pwm_backlight.h>
#include <mach/nvhost.h>
#include <mach/nvmap.h>
#include <mach/irqs.h>
#include <mach/iomap.h>
#include <mach/dc.h>
#include <mach/fb.h>

#include "devices.h"
#include "gpio-names.h"
#include "board.h"
#include "board-picasso.h"

static struct regulator *picasso_hdmi_reg = NULL;
static struct regulator *picasso_hdmi_pll = NULL;

static int picasso_backlight_init(struct device *dev) {
	int ret;

	ret = gpio_request(PICASSO_GPIO_BL_ENABLE, "backlight_enb");
	if (ret < 0)
		return ret;

	ret = gpio_direction_output(PICASSO_GPIO_BL_ENABLE, 1);
	if (ret < 0)
		gpio_free(PICASSO_GPIO_BL_ENABLE);
	else
		tegra_gpio_enable(PICASSO_GPIO_BL_ENABLE);

	return ret;
};

static void picasso_backlight_exit(struct device *dev) {
	gpio_set_value(PICASSO_GPIO_BL_ENABLE, 0);
	gpio_free(PICASSO_GPIO_BL_ENABLE);
	tegra_gpio_disable(PICASSO_GPIO_BL_ENABLE);
}

static int picasso_backlight_notify(struct device *unused, int brightness)
{
	gpio_set_value(PICASSO_GPIO_BL_ENABLE, !!brightness);
	return brightness;
}

static struct platform_pwm_backlight_data picasso_backlight_data = {
	.pwm_id		= 2,
	.max_brightness	= 255,
	.dft_brightness	= 224,
	.pwm_period_ns	= 4166667,
	.init		= picasso_backlight_init,
	.exit		= picasso_backlight_exit,
	.notify		= picasso_backlight_notify,
};

static struct platform_device picasso_backlight_device = {
	.name	= "pwm-backlight",
	.id	= -1,
	.dev	= {
		.platform_data = &picasso_backlight_data,
	},
};

static int picasso_panel_enable(void)
{
	gpio_set_value(PICASSO_GPIO_PNL_ENABLE, 1);
	msleep(200);
	gpio_set_value(PICASSO_GPIO_LVDS_SHUTDOWN, 1);
	return 0;
}

static int picasso_panel_disable(void)
{
	gpio_set_value(PICASSO_GPIO_LVDS_SHUTDOWN, 0);
	gpio_set_value(PICASSO_GPIO_PNL_ENABLE, 0);
	return 0;
}

static int picasso_hdmi_enable(void)
{
	if (!picasso_hdmi_reg) {
		picasso_hdmi_reg = regulator_get(NULL, "avdd_hdmi"); /* LD07 */
		if (IS_ERR_OR_NULL(picasso_hdmi_reg)) {
			pr_err("hdmi: couldn't get regulator avdd_hdmi\n");
			picasso_hdmi_reg = NULL;
			return PTR_ERR(picasso_hdmi_reg);
		}
	}
	regulator_enable(picasso_hdmi_reg);

	if (!picasso_hdmi_pll) {
		picasso_hdmi_pll = regulator_get(NULL, "avdd_hdmi_pll"); /* LD08 */
		if (IS_ERR_OR_NULL(picasso_hdmi_pll)) {
			pr_err("hdmi: couldn't get regulator avdd_hdmi_pll\n");
			picasso_hdmi_pll = NULL;
			regulator_disable(picasso_hdmi_reg);
			picasso_hdmi_reg = NULL;
			return PTR_ERR(picasso_hdmi_pll);
		}
	}
	regulator_enable(picasso_hdmi_pll);
	return 0;
}

static int picasso_hdmi_disable(void)
{
	regulator_disable(picasso_hdmi_reg);
	regulator_disable(picasso_hdmi_pll);
	return 0;
}

static struct resource picasso_disp1_resources[] = {
	{
		.name	= "irq",
		.start	= INT_DISPLAY_GENERAL,
		.end	= INT_DISPLAY_GENERAL,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "regs",
		.start	= TEGRA_DISPLAY_BASE,
		.end	= TEGRA_DISPLAY_BASE + TEGRA_DISPLAY_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "fbmem",
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource picasso_disp2_resources[] = {
	{
		.name	= "irq",
		.start	= INT_DISPLAY_B_GENERAL,
		.end	= INT_DISPLAY_B_GENERAL,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "regs",
		.start	= TEGRA_DISPLAY2_BASE,
		.end	= TEGRA_DISPLAY2_BASE + TEGRA_DISPLAY2_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "fbmem",
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "hdmi_regs",
		.start	= TEGRA_HDMI_BASE,
		.end	= TEGRA_HDMI_BASE + TEGRA_HDMI_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct tegra_dc_mode picasso_panel_modes[] = {
	{
		.pclk = 62200000,
		.h_ref_to_sync = 11,
		.v_ref_to_sync = 1,
		.h_sync_width = 58,
		.v_sync_width = 4,
		.h_back_porch = 58,
		.v_back_porch = 4,
		.h_active = 1280, 
		.v_active = 800,  
		.h_front_porch = 58,
		.v_front_porch = 4,
	},
};

static struct tegra_fb_data picasso_fb_data = {
	.win		= 0,
	.xres		= 1280,
	.yres		= 800, 
	.bits_per_pixel	= 32,
};

static struct tegra_fb_data picasso_hdmi_fb_data = {
	.win		= 0,
	.xres		= 1280,
	.yres		= 800,
	.bits_per_pixel	= 32,
};

static struct tegra_dc_out picasso_disp1_out = {
	.type		= TEGRA_DC_OUT_RGB,

	.align		= TEGRA_DC_ALIGN_MSB,
	.order		= TEGRA_DC_ORDER_RED_BLUE,

	.modes	 	= picasso_panel_modes,
	.n_modes 	= ARRAY_SIZE(picasso_panel_modes),

	.enable		= picasso_panel_enable,
	.disable	= picasso_panel_disable,

	.depth		= 18,
	.dither		= TEGRA_DC_ORDERED_DITHER,
};

static struct tegra_dc_out picasso_disp2_out = {
	.type		= TEGRA_DC_OUT_HDMI,
	.flags		= TEGRA_DC_OUT_HOTPLUG_HIGH,

	.dcc_bus	= 1,
	.hotplug_gpio	= PICASSO_GPIO_HDMI_HPD,

	.align		= TEGRA_DC_ALIGN_MSB,
	.order		= TEGRA_DC_ORDER_RED_BLUE,

	.enable		= picasso_hdmi_enable,
	.disable	= picasso_hdmi_disable,
};

static struct tegra_dc_platform_data picasso_disp1_pdata = {
	.flags		= TEGRA_DC_FLAG_ENABLED,
	.default_out	= &picasso_disp1_out,
	.fb		= &picasso_fb_data,
};

static struct tegra_dc_platform_data picasso_disp2_pdata = {
	.flags		= 0,
	.default_out	= &picasso_disp2_out,
	.fb		= &picasso_hdmi_fb_data,
};

static struct nvhost_device picasso_disp1_device = {
	.name		= "tegradc",
	.id		= 0,
	.resource	= picasso_disp1_resources,
	.num_resources	= ARRAY_SIZE(picasso_disp1_resources),
	.dev = {
		.platform_data = &picasso_disp1_pdata,
	},
};

static struct nvhost_device picasso_disp2_device = {
	.name		= "tegradc",
	.id		= 1,
	.resource	= picasso_disp2_resources,
	.num_resources	= ARRAY_SIZE(picasso_disp2_resources),
	.dev = {
		.platform_data = &picasso_disp2_pdata,
	},
};

static struct nvmap_platform_carveout picasso_carveouts[] = {
	[0] = {
		.name		= "iram",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_IRAM,
		.base		= TEGRA_IRAM_BASE,
		.size		= TEGRA_IRAM_SIZE,
		.buddy_size	= 0, /* no buddy allocation for IRAM */
	},
	[1] = {
		.name		= "generic-0",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_GENERIC,
		.buddy_size	= SZ_32K,
	},
};

static struct nvmap_platform_data picasso_nvmap_data = {
	.carveouts	= picasso_carveouts,
	.nr_carveouts	= ARRAY_SIZE(picasso_carveouts),
};

static struct platform_device picasso_nvmap_device = {
	.name	= "tegra-nvmap",
	.id	= -1,
	.dev	= {
		.platform_data = &picasso_nvmap_data,
	},
};

static struct platform_device *picasso_gfx_devices[] __initdata = {
	&picasso_nvmap_device,
	&tegra_grhost_device,
	&tegra_pwfm2_device,
	&picasso_backlight_device,
};

int __init picasso_panel_init(void)
{
	int err;
	struct resource *res;

	gpio_request(PICASSO_GPIO_PNL_ENABLE, "pnl_pwr_enb");
	gpio_direction_output(PICASSO_GPIO_PNL_ENABLE, 1);
	tegra_gpio_enable(PICASSO_GPIO_PNL_ENABLE);

	gpio_request(PICASSO_GPIO_LVDS_SHUTDOWN, "lvds_shdn");
	gpio_direction_output(PICASSO_GPIO_LVDS_SHUTDOWN, 1);
	tegra_gpio_enable(PICASSO_GPIO_LVDS_SHUTDOWN);

	tegra_gpio_enable(PICASSO_GPIO_HDMI_HPD);
	gpio_request(PICASSO_GPIO_HDMI_HPD, "hdmi_hpd");
	gpio_direction_input(PICASSO_GPIO_HDMI_HPD);

	picasso_carveouts[1].base = tegra_carveout_start;
	picasso_carveouts[1].size = tegra_carveout_size;

	err = platform_add_devices(picasso_gfx_devices,
				   ARRAY_SIZE(picasso_gfx_devices));


	res = nvhost_get_resource_byname(&picasso_disp1_device,
		IORESOURCE_MEM, "fbmem");
	res->start = tegra_fb_start;
	res->end = tegra_fb_start + tegra_fb_size - 1;

	res = nvhost_get_resource_byname(&picasso_disp2_device,
		IORESOURCE_MEM, "fbmem");
	res->start = tegra_fb2_start;
	res->end = tegra_fb2_start + tegra_fb2_size - 1;

	if (!err)
		err = nvhost_device_register(&picasso_disp1_device);

	if (!err)
		err = nvhost_device_register(&picasso_disp2_device);

	return err;
}

