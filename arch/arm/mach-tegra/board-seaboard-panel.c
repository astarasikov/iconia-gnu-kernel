/*
 * arch/arm/mach-tegra/board-seaboard-panel.c
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
#include "board-seaboard.h"

static int seaboard_backlight_init(struct device *dev) {
	int ret;

	ret = gpio_request(TEGRA_GPIO_BACKLIGHT, "backlight_enb");
	if (ret < 0)
		return ret;

	ret = gpio_direction_output(TEGRA_GPIO_BACKLIGHT, 1);
	if (ret < 0)
		gpio_free(TEGRA_GPIO_BACKLIGHT);

	return ret;
};

static void seaboard_backlight_exit(struct device *dev) {
	gpio_set_value(TEGRA_GPIO_BACKLIGHT, 0);
	gpio_free(TEGRA_GPIO_BACKLIGHT);
}

static int seaboard_backlight_notify(struct device *unused, int brightness)
{
	gpio_set_value(TEGRA_GPIO_EN_VDD_PNL, !!brightness);
	gpio_set_value(TEGRA_GPIO_LVDS_SHUTDOWN, !!brightness);
	gpio_set_value(TEGRA_GPIO_BACKLIGHT, !!brightness);
	return brightness;
}

static int seaboard_disp1_check_fb(struct device *dev, struct fb_info *info);

static struct platform_pwm_backlight_data seaboard_backlight_data = {
	.pwm_id		= 2,
	.max_brightness	= 255,
	.dft_brightness	= 224,
	.pwm_period_ns	= 1000000,
	.init		= seaboard_backlight_init,
	.exit		= seaboard_backlight_exit,
	.notify		= seaboard_backlight_notify,
	/* Only toggle backlight on fb blank notifications for disp1 */
	.check_fb	= seaboard_disp1_check_fb,
};

static struct platform_device seaboard_backlight_device = {
	.name	= "pwm-backlight",
	.id	= -1,
	.dev	= {
		.platform_data = &seaboard_backlight_data,
	},
};

static int seaboard_panel_enable(void)
{
	gpio_set_value(TEGRA_GPIO_LVDS_SHUTDOWN, 1);
	return 0;
}

static int seaboard_panel_disable(void)
{
	gpio_set_value(TEGRA_GPIO_LVDS_SHUTDOWN, 0);
	return 0;
}

static int seaboard_set_hdmi_power(bool enable)
{
	static struct {
		struct regulator *regulator;
		const char *name;
	} regs[] = {
		{ .name = "avdd_hdmi" },
		{ .name = "avdd_hdmi_pll" },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		if (!regs[i].regulator) {
			regs[i].regulator = regulator_get(NULL, regs[i].name);

			if (IS_ERR(regs[i].regulator)) {
				int ret = PTR_ERR(regs[i].regulator);
				regs[i].regulator = NULL;
				return ret;
			}
		}

		if (enable)
			regulator_enable(regs[i].regulator);
		else
			regulator_disable(regs[i].regulator);
	}

	return 0;
}

static int seaboard_hdmi_enable(void)
{
	return seaboard_set_hdmi_power(true);
}

static int seaboard_hdmi_disable(void)
{
	return seaboard_set_hdmi_power(false);
}

static void seaboard_hdmi_enable_ddc(void)
{
	gpio_set_value(TEGRA_GPIO_HDMI_ENB, 1);
}

static void seaboard_hdmi_disable_ddc(void)
{
	gpio_set_value(TEGRA_GPIO_HDMI_ENB, 0);
}

static struct resource seaboard_disp1_resources[] = {
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
		.start	= 0x18012000,
		.end	= 0x18012000 + 0x402000 - 1, /* enough for 1368*768 16bpp */
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource seaboard_disp2_resources[] = {
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
		.name	= "hdmi_regs",
		.start	= TEGRA_HDMI_BASE,
		.end	= TEGRA_HDMI_BASE + TEGRA_HDMI_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct tegra_dc_mode seaboard_panel_modes[] = {
	{
		.pclk = 62200000,
		.h_ref_to_sync = 11,
		.v_ref_to_sync = 1,
		.h_sync_width = 58,
		.v_sync_width = 4,
		.h_back_porch = 58,
		.v_back_porch = 4,
		.h_active = 1366,
		.v_active = 768,
		.h_front_porch = 58,
		.v_front_porch = 4,
	},
};

static struct tegra_dc_mode wario_panel_modes[] = {
	{
		.pclk = 62200000,
		.h_ref_to_sync = 16,
		.v_ref_to_sync = 1,
		.h_sync_width = 58,
		.v_sync_width = 40,
		.h_back_porch = 58,
		.v_back_porch = 20,
		.h_active = 1280,
		.v_active = 800,
		.h_front_porch = 58,
		.v_front_porch = 1,
	},
};

static struct tegra_dc_mode arthur_panel_modes[] = {
	{
		.pclk = 82400000,
		.h_ref_to_sync = 11,
		.v_ref_to_sync = 1,
		.h_sync_width = 32,
		.v_sync_width = 5,
		.h_back_porch = 112,
		.v_back_porch = 20,
		.h_active = 1366,
		.v_active = 910,
		.h_front_porch = 48,
		.v_front_porch = 2,
	},
};

static struct tegra_fb_data seaboard_fb_data = {
	.win		= 0,
	.xres		= 1366,
	.yres		= 768,
	.bits_per_pixel	= 16,
};

static struct tegra_fb_data wario_fb_data = {
	.win		= 0,
	.xres		= 1280,
	.yres		= 800,
	.bits_per_pixel	= 16,
};

static struct tegra_fb_data arthur_fb_data = {
	.win		= 0,
	.xres		= 1366,
	.yres		= 910,
	.bits_per_pixel	= 24,
};

static struct tegra_fb_data seaboard_hdmi_fb_data = {
	.win		= 0,
	.xres		= 1280,
	.yres		= 720,
	.bits_per_pixel	= 16,
};

static struct tegra_dc_out seaboard_disp1_out = {
	.type		= TEGRA_DC_OUT_RGB,

	.align		= TEGRA_DC_ALIGN_MSB,
	.order		= TEGRA_DC_ORDER_RED_BLUE,
	.depth		= 18,
	.dither		= TEGRA_DC_ORDERED_DITHER,

	.modes		= seaboard_panel_modes,
	.n_modes	= ARRAY_SIZE(seaboard_panel_modes),

	.enable		= seaboard_panel_enable,
	.disable	= seaboard_panel_disable,
};

static struct tegra_dc_out seaboard_disp2_out = {
	.type		= TEGRA_DC_OUT_HDMI,
	.flags		= TEGRA_DC_OUT_HOTPLUG_HIGH,

	.dcc_bus	= 1,
	.hotplug_gpio	= TEGRA_GPIO_HDMI_HPD,

	.align		= TEGRA_DC_ALIGN_MSB,
	.order		= TEGRA_DC_ORDER_RED_BLUE,

	.enable		= seaboard_hdmi_enable,
	.disable	= seaboard_hdmi_disable,
	.enable_ddc	= seaboard_hdmi_enable_ddc,
	.disable_ddc	= seaboard_hdmi_disable_ddc,
};

static struct tegra_dc_platform_data seaboard_disp1_pdata = {
	.flags		= TEGRA_DC_FLAG_ENABLED,
	.default_out	= &seaboard_disp1_out,
	.fb		= &seaboard_fb_data,
	.emc_clk_rate	= 300000000,
};

static struct tegra_dc_platform_data seaboard_disp2_pdata = {
	.flags		= 0,
	.default_out	= &seaboard_disp2_out,
	.fb		= &seaboard_hdmi_fb_data,
};

static struct nvhost_device seaboard_disp1_device = {
	.name		= "tegradc",
	.id		= 0,
	.resource	= seaboard_disp1_resources,
	.num_resources	= ARRAY_SIZE(seaboard_disp1_resources),
	.dev = {
		.platform_data = &seaboard_disp1_pdata,
	},
};

static int seaboard_disp1_check_fb(struct device *dev, struct fb_info *info)
{
	return info->device == &seaboard_disp1_device.dev;
}

static struct nvhost_device seaboard_disp2_device = {
	.name		= "tegradc",
	.id		= 1,
	.resource	= seaboard_disp2_resources,
	.num_resources	= ARRAY_SIZE(seaboard_disp2_resources),
	.dev = {
		.platform_data = &seaboard_disp2_pdata,
	},
};

static struct nvmap_platform_carveout seaboard_carveouts[] = {
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
		.base		= 0x18C00000,
		.size		= SZ_128M - 0xC00000,
		.buddy_size	= SZ_32K,
	},
};

static struct nvmap_platform_data seaboard_nvmap_data = {
	.carveouts	= seaboard_carveouts,
	.nr_carveouts	= ARRAY_SIZE(seaboard_carveouts),
};

static struct platform_device seaboard_nvmap_device = {
	.name	= "tegra-nvmap",
	.id	= -1,
	.dev	= {
		.platform_data = &seaboard_nvmap_data,
	},
};

static struct platform_device *seaboard_gfx_devices[] __initdata = {
	&seaboard_nvmap_device,
	&tegra_grhost_device,
	&tegra_pwfm2_device,
	&seaboard_backlight_device,
};

int __init seaboard_panel_init(void)
{
	int err;

	gpio_request(TEGRA_GPIO_EN_VDD_PNL, "en_vdd_pnl");
	gpio_direction_output(TEGRA_GPIO_EN_VDD_PNL, 1);

	gpio_request(TEGRA_GPIO_BACKLIGHT_VDD, "bl_vdd");
	gpio_direction_output(TEGRA_GPIO_BACKLIGHT_VDD, 1);

	gpio_request(TEGRA_GPIO_HDMI_ENB, "hdmi_5v_en");
	gpio_direction_output(TEGRA_GPIO_HDMI_ENB, 0);

	gpio_request(TEGRA_GPIO_LVDS_SHUTDOWN, "lvds_shdn");
	gpio_direction_output(TEGRA_GPIO_LVDS_SHUTDOWN, 1);

	gpio_request(TEGRA_GPIO_HDMI_HPD, "hdmi_hpd");
	gpio_direction_input(TEGRA_GPIO_HDMI_HPD);

	if (machine_is_wario()) {
		seaboard_disp1_out.modes = wario_panel_modes;
		seaboard_disp1_pdata.fb = &wario_fb_data;
	} else if (machine_is_arthur()) {
		seaboard_disp1_out.modes = arthur_panel_modes;
		seaboard_disp1_pdata.fb = &arthur_fb_data;
	}

	err = platform_add_devices(seaboard_gfx_devices,
				   ARRAY_SIZE(seaboard_gfx_devices));

	if (!err)
		err = nvhost_device_register(&seaboard_disp1_device);

	if (!err)
		err = nvhost_device_register(&seaboard_disp2_device);

	return err;
}
