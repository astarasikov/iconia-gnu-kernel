/*
 * arch/arm/mach-tegra/board-picasso.c
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/serial_8250.h>
#include <linux/i2c.h>
#include <linux/i2c/atmel_mxt_ts.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/i2c-tegra.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <mach/usb_phy.h>
#include <linux/platform_data/tegra_usb.h>
#include <linux/input.h>

#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/pinmux.h>
#include <mach/iomap.h>
#include <mach/io.h>
#include <mach/sdhci.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "board.h"
#include "board-picasso.h"
#include "clock.h"
#include "devices.h"
#include "gpio-names.h"
#include "wakeups-t2.h"

/******************************************************************************
 * Debug Serial
 *****************************************************************************/
static struct plat_serial8250_port debug_uart_platform_data[] = {
	{
		.membase	= IO_ADDRESS(TEGRA_UARTD_BASE),
		.mapbase	= TEGRA_UARTD_BASE,
		.irq		= INT_UARTD,
		.flags		= UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 216000000,
	}, {
		.flags		= 0,
	}
};

static struct platform_device debug_uart = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM,
	.dev = {
		.platform_data = debug_uart_platform_data,
	},
};

/******************************************************************************
 * USB
 *****************************************************************************/
static struct tegra_utmip_config utmi_phy_config[] = {
	[0] = {
			.hssync_start_delay = 0,
			.idle_wait_delay = 17,
			.elastic_limit = 16,
			.term_range_adj = 6,
			.xcvr_setup = 15,
			.xcvr_lsfslew = 2,
			.xcvr_lsrslew = 2,
	},
	[1] = {
			.hssync_start_delay = 0,
			.idle_wait_delay = 17,
			.elastic_limit = 16,
			.term_range_adj = 6,
			.xcvr_setup = 8,
			.xcvr_lsfslew = 2,
			.xcvr_lsrslew = 2,
	},
};

static struct tegra_ulpi_config ulpi_phy_config = {
	.reset_gpio = PICASSO_GPIO_ULPI_RESET,
	.clk = "cdev2",
};

static struct tegra_ehci_platform_data tegra_ehci_pdata[] = {
	[0] = {
			.phy_config = &utmi_phy_config[0],
			.operating_mode = TEGRA_USB_HOST,
			.power_down_on_bus_suspend = 0,
	},
	[1] = {
			.phy_config = &ulpi_phy_config,
			.operating_mode = TEGRA_USB_HOST,
			.power_down_on_bus_suspend = 0,
	},
	[2] = {
			.phy_config = &utmi_phy_config[1],
			.operating_mode = TEGRA_USB_HOST,
			.power_down_on_bus_suspend = 0,
	},
};

static void __init picasso_usb_init(void) {
	tegra_ehci1_device.dev.platform_data = &tegra_ehci_pdata[0];
	tegra_ehci2_device.dev.platform_data = &tegra_ehci_pdata[1];
	tegra_ehci3_device.dev.platform_data = &tegra_ehci_pdata[2];	
	tegra_otg_device.dev.platform_data = &tegra_ehci1_device;

	platform_device_register(&tegra_udc_device);
	platform_device_register(&tegra_ehci2_device);
	
	//platform_device_register(&tegra_otg_device);
	platform_device_register(&tegra_ehci3_device);
}
/******************************************************************************
 * Clocks
 *****************************************************************************/
static __initdata struct tegra_clk_init_table picasso_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "uartd",	"pll_p",	216000000,	true},
	{ "pll_m",	"clk_m",	600000000,	true},
	{ "emc",	"pll_m",	600000000,	true},
	{ "uartc",	"pll_c",	600000000,	false},
	{ "blink",	"clk_32k",	32768,		false},
	{ "pll_p_out4",	"pll_p",	24000000,	true },
	{ "pwm",	"clk_m",	12000000,	false},
	{ "pll_a",	NULL,		11289600,	true},
	{ "pll_a_out0",	NULL,		11289600,	true},
	{ "i2s1",	"pll_a_out0",	2822400,	true},
	{ "i2s2",	"pll_a_out0",	11289600,	true},
	{ "audio",	"pll_a_out0",	11289600,	true},
	{ "audio_2x",	"audio",	22579200,	true},
	{ "spdif_out",	"pll_a_out0",	5644800,	false},
	//{ "kbc",	"clk_32k",	32768,		true},
	{ "usbd",	"clk_m",	12000000,	false},
	{ "usb2",	"clk_m",	12000000,	false},
	{ "usb3",	"clk_m",	12000000,	false},
	{ NULL,		NULL,		0,		0},
};

/******************************************************************************
 * Touchscreen
 *****************************************************************************/
static const u8 mxt_config_data[] = {
	/* MXT_GEN_COMMAND(6) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_GEN_POWER(7) */
	0x32, 0xa, 0x19,
	/* MXT_GEN_ACQUIRE(8) */
	0x0a, 0x00, 0xa, 0xa, 0x00, 0x00, 0x05, 0xa, 0x1e, 0x19,
	/* MXT_TOUCH_MULTI(9) */
	0x8f, 0x00, 0x00, 0x1c, 0x29, 0x00, 0x10, 0x37, 0x03, 0x01,
	0x00, 0x05, 0x05, 0x20, 0x0a, 0x05, 0x0a, 0x05, 0x1f, 0x03,
	0xff, 0x04, 0x00, 0x00, 0x00, 0x00, 0x98, 0x22, 0xd4, 0x16,
	0x0a, 0x0a, 0x00, 0x00,
	/* MXT_TOUCH_KEYARRAY(15) */
	0x01, 0x18, 0x29, 0x04, 0x01, 0x00, 0x00, 0xff, 0x01, 0x00,
	0x00,
	/* MXT_PROCG_NOISE(22) */
	0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x31, 0x00,
	0x00, 0x0b, 0x11, 0x16, 0x20, 0x24, 0x00,
	/* MXT_PROCI_ONETOUCH(24) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_SPT_SELFTEST(25) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	/* MXT_PROCI_TWOTOUCH(27) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_SPT_CTECONFIG(28) */
	0x00, 0x00, 0x00, 0x10, 0x10, 0x3c,
	/* MXT_PROCI_GRIP(40) */
	0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_PROCI_PALM(41) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static struct mxt_platform_data mxt_platform_data = {
	.x_line			= 0x1c,
	.y_line			= 0x29,
	.x_size			= 1280,
	.y_size			= 800,
	.blen			= 0x10,
	.threshold		= 0x37,
	.voltage		= 3300000,
	.orient			= MXT_DIAGONAL,
	.irqflags		= IRQF_TRIGGER_FALLING,
	.config			= mxt_config_data,
	.config_length	= sizeof(mxt_config_data),
};

static struct i2c_board_info mxt_device = {
	I2C_BOARD_INFO("atmel_mxt_ts", 0x4c),
	.platform_data = &mxt_platform_data,
	.irq = TEGRA_GPIO_TO_IRQ(PICASSO_GPIO_TS_IRQ),
};

static void __init picasso_touch_init(void) {
	tegra_gpio_enable(PICASSO_GPIO_TS_IRQ);
	gpio_request(PICASSO_GPIO_TS_IRQ, "atmel_touch_chg");

	tegra_gpio_enable(PICASSO_GPIO_TS_RESET);
	gpio_request(PICASSO_GPIO_TS_RESET, "atmel_touch_reset");

	gpio_set_value(PICASSO_GPIO_TS_RESET, 0);
	msleep(1);
	gpio_set_value(PICASSO_GPIO_TS_RESET, 1);
	msleep(100);

	i2c_register_board_info(0, &mxt_device, 1);
}

/******************************************************************************
 * I2C
 *****************************************************************************/
static struct tegra_i2c_platform_data picasso_i2c1_platform_data = {
	.adapter_nr     = 0,
	.bus_count      = 1,
	.bus_clk_rate   = { 400000, 0 },
};

static const struct tegra_pingroup_config i2c2_ddc = {
	.pingroup       = TEGRA_PINGROUP_DDC,
		.func           = TEGRA_MUX_I2C2,
};

static const struct tegra_pingroup_config i2c2_gen2 = {
	.pingroup       = TEGRA_PINGROUP_PTA,
	.func           = TEGRA_MUX_I2C2,
};

static struct tegra_i2c_platform_data picasso_i2c2_platform_data = {
	.adapter_nr     = 1,
	.bus_count      = 2,
	.bus_clk_rate   = { 50000, 100000 },
	.bus_mux        = { &i2c2_ddc, &i2c2_gen2 },
	.bus_mux_len    = { 1, 1 },
};

static struct tegra_i2c_platform_data picasso_i2c3_platform_data = {
	.adapter_nr     = 3,
	.bus_count      = 1,
	.bus_clk_rate   = { 400000, 0 },
};

static struct tegra_i2c_platform_data picasso_dvc_platform_data = {
	.adapter_nr     = 4,
	.bus_count      = 1,
	.bus_clk_rate   = { 400000, 0 },
	.is_dvc         = true,
};

static void picasso_i2c_init(void)
{
	tegra_i2c_device1.dev.platform_data = &picasso_i2c1_platform_data;
	tegra_i2c_device2.dev.platform_data = &picasso_i2c2_platform_data;
	tegra_i2c_device3.dev.platform_data = &picasso_i2c3_platform_data;
	tegra_i2c_device4.dev.platform_data = &picasso_dvc_platform_data;

	platform_device_register(&tegra_i2c_device4);
	platform_device_register(&tegra_i2c_device3);
	platform_device_register(&tegra_i2c_device2);
	platform_device_register(&tegra_i2c_device1);
}

/******************************************************************************
 * GPIO Keys
 *****************************************************************************/
static struct gpio_keys_button picasso_keys[] = {
	{
		.code = KEY_VOLUMEUP,
		.gpio = PICASSO_GPIO_KEY_nVOLUMEUP,
		.active_low = 1,
		.desc = "Volume Up Key",
		.type = EV_KEY,
		.debounce_interval = 10,
	},
	{
		.code = KEY_VOLUMEDOWN,
		.gpio = PICASSO_GPIO_KEY_nVOLUMEDOWN,
		.active_low = 1,
		.desc = "Volume Down Key",
		.type = EV_KEY,
		.debounce_interval = 10,
	},
	{
		.code = KEY_POWER,
		.gpio = PICASSO_GPIO_KEY_POWER,
		.active_low = 1,
		.desc = "Power Key",
		.type = EV_KEY,
		.wakeup = 1,
		.debounce_interval = 10,
	},
	{
		.code = KEY_POWER,
		.gpio = PICASSO_GPIO_KEY_POWER2,
		.active_low = 1,
		.desc = "Power Key 2",
		.type = EV_KEY,
		.debounce_interval = 10,
	},
};

static struct gpio_keys_platform_data picasso_keys_platform_data = {
	.buttons	= picasso_keys,
	.nbuttons	= ARRAY_SIZE(picasso_keys),
};

static struct platform_device picasso_keys_device = {
	.name	= "gpio-keys",
	.id	= 0,
	.dev	= {
		.platform_data	= &picasso_keys_platform_data,
	},
};

static void picasso_keys_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(picasso_keys); i++)
		tegra_gpio_enable(picasso_keys[i].gpio);
}

/******************************************************************************
 * SDHC
 *****************************************************************************/
static struct tegra_sdhci_platform_data tegra_sdhci_platform_data1 = {
	.cd_gpio = -1,
	.wp_gpio = -1,
	.power_gpio = -1,
};

static struct tegra_sdhci_platform_data tegra_sdhci_platform_data3 = {
	.cd_gpio = PICASSO_GPIO_SDHCI2_CD,
	.wp_gpio = -1,
	.power_gpio = PICASSO_GPIO_SDHCI2_PWR,
};

static struct tegra_sdhci_platform_data tegra_sdhci_platform_data4 = {
	.cd_gpio = -1,
	.wp_gpio = -1,
	.power_gpio = -1,
};

static void __init picasso_sdhci_init(void) {
	tegra_sdhci_device1.dev.platform_data = &tegra_sdhci_platform_data1;
	tegra_sdhci_device3.dev.platform_data = &tegra_sdhci_platform_data3;
	tegra_sdhci_device4.dev.platform_data = &tegra_sdhci_platform_data4;

	platform_device_register(&tegra_sdhci_device4);
	platform_device_register(&tegra_sdhci_device3);
	//platform_device_register(&tegra_sdhci_device1);
}
/******************************************************************************
 * Platform devices
 *****************************************************************************/
static struct platform_device *picasso_devices[] __initdata = {
	&debug_uart,
	&tegra_pmu_device,
	&tegra_gart_device,
	&tegra_aes_device,
	&picasso_keys_device,
};

static int __init tegra_picasso_protected_aperture_init(void)
{
	tegra_protected_aperture_init(tegra_grhost_aperture);
	return 0;
}
late_initcall(tegra_picasso_protected_aperture_init);

static void __init tegra_picasso_reserve(void)
{
	tegra_reserve(SZ_256M, SZ_8M, SZ_16M);
}

static void __init tegra_picasso_init(void)
{
	picasso_pinmux_init();
	tegra_clk_init_from_table(picasso_clk_init_table);
	
	platform_add_devices(picasso_devices, ARRAY_SIZE(picasso_devices));

	picasso_emc_init();
	picasso_i2c_init();
	picasso_regulator_init();
	picasso_usb_init();
	picasso_keys_init();
	picasso_panel_init();
	picasso_sdhci_init();
	picasso_touch_init();
}

MACHINE_START(VENTANA, "picasso")
	.boot_params    = 0x00000100,
	.map_io		= tegra_map_common_io,
	.init_early	= tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.reserve		= tegra_picasso_reserve,
	.init_machine	= tegra_picasso_init,
MACHINE_END
