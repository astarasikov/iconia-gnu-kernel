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
	.reset_gpio = TEGRA_GPIO_PG2,
	.clk = "clk_dev2",
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

/******************************************************************************
 * Clocks
 *****************************************************************************/
static __initdata struct tegra_clk_init_table picasso_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "uartd",	"pll_p",	216000000,	true},
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
	{ "kbc",	"clk_32k",	32768,		true},
	{ NULL,		NULL,		0,		0},
};

/******************************************************************************
 * Touchscreen
 *****************************************************************************/
#if 0
static const u8 mxt_config_data[] = {
	/* MXT_GEN_POWER(7) */
	0x32, 0xa, 0x25,
	/* MXT_GEN_ACQUIRE(8) */
	0x0a, 0x00, 0xa, 0xa, 0x00, 0x00, 0x05, 0xa, 0x1e, 0x19,
	/* MXT_TOUCH_MULTI(9) */
	143, 0, 0, 28, 41, 0, 16, 55, 3, 1,
	0, 5, 5, 32, 10, 5, 10, 5, 31, 3, 255, 4,
	0, 0, 0, 0, 152, 34, 212, 22, 10, 10, 0, 0,
	/* MXT_TOUCH_KEYARRAY(15) */
	1, 24, 41, 4, 1, 0, 0, 255, 1, 0, 0,
	/* MXT_PROCG_NOISE(22) */
	5, 0, 0, 0, 0, 0, 0, 0, 45, 0, 0, 11, 17, 22, 32, 36, 0,
	/* MXT_PROCI_ONETOUCH(24) */
	0, 0 ,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* MXT_PROCI_TWOTOUCH(27) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_SPT_SELFTEST(25) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	/* MXT_SPT_CTECONFIG(28) */
	0x00, 0x00, 0x00, 0x10, 0x10, 0x3c,
	/* MXT_PROCI_GRIP(40) */
	0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_PROCI_PALM(41) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_SPT_DIGITIZER(43) */
	0x0, 0x00, 0x00, 0x00, 0x00, 0x00,
};
#else
static const u8 mxt_config_data[] = {
	/* MXT_GEN_COMMAND(6) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_GEN_POWER(7) */
	0xFF, 0xff, 0x32,
	/* MXT_GEN_ACQUIRE(8) */
	0x0a, 0x00, 0x14, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_TOUCH_MULTI(9) */
	0x0F, 0x00, 0x00, 0x1b, 0x2a, 0x00, 0x10, 0x32, 0x02, 0x05,
	0x00, 0x02, 0x01, 0x00, 0x0a, 0x0a, 0x0a, 0x0a, 0x00, 0x03,
	0x56, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x0a, 0x00, 0x00, 0x00,
	/* MXT_TOUCH_KEYARRAY(15) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00,
	/* MXT_PROCG_NOISE(22) */
	0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x19, 0x00,
	0x00, 0x00, 0x05, 0x0a, 0x14, 0x1e, 0x00,
	/* MXT_PROCI_ONETOUCH(24) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_PROCI_TWOTOUCH(27) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_SPT_SELFTEST(25) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	/* MXT_SPT_CTECONFIG(28) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_PROCI_GRIP(40) */
	0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_PROCI_PALM(41) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_SPT_DIGITIZER(43) */
	0x00, 0x00, 0x00, 0x00,
};
#endif

static struct mxt_platform_data mxt_platform_data = {
//	.x_line			= 27,
//	.y_line			= 42,
	.x_size			= 800,
	.y_size			= 1280,
//	.blen			= 0x16,
//	.threshold		= 0x28,
	.irqflags		= IRQF_TRIGGER_FALLING,
	.config			= mxt_config_data,
	.config_length		= sizeof(mxt_config_data),
};

static struct i2c_board_info mxt_device = {
	I2C_BOARD_INFO("atmel_mxt_ts", 0x4c),
	.platform_data = &mxt_platform_data,
	.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV6),
};

static void __init picasso_touch_init(void) {
	tegra_gpio_enable(TEGRA_GPIO_PV6);
	gpio_request(TEGRA_GPIO_PV6, "atmel_touch_chg");

	tegra_gpio_enable(TEGRA_GPIO_PQ7);
	gpio_request(TEGRA_GPIO_PQ7, "atmel_touch_reset");

	gpio_set_value(TEGRA_GPIO_PQ7, 0);
	msleep(1);
	gpio_set_value(TEGRA_GPIO_PQ7, 1);
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
#define GPIO_KEY(_id, _gpio,_isactivelow, _iswake)		\
	{					\
		.code = _id,			\
		.gpio = TEGRA_GPIO_##_gpio,	\
		.active_low = _isactivelow,		\
		.desc = #_id,			\
		.type = EV_KEY,			\
		.wakeup = _iswake,		\
		.debounce_interval = 10,	\
	}

static struct gpio_keys_button picasso_keys[] = {
	[0] = GPIO_KEY(KEY_VOLUMEUP, PQ4, 1,  0),
	[1] = GPIO_KEY(KEY_VOLUMEDOWN, PQ5, 1, 0),
	[2] = GPIO_KEY(KEY_POWER, PC7, 0, 1),
	[3] = GPIO_KEY(KEY_POWER, PI3, 0, 0),
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
 * Platform devices
 *****************************************************************************/
static struct platform_device *picasso_devices[] __initdata = {
	&debug_uart,
	&tegra_pmu_device,
	&tegra_gart_device,
	&tegra_aes_device,
	&picasso_keys_device,
	&tegra_ehci1_device,
	&tegra_ehci2_device,
	&tegra_ehci3_device,
};

int __init tegra_picasso_protected_aperture_init(void)
{
	tegra_protected_aperture_init(tegra_grhost_aperture);
	return 0;
}
late_initcall(tegra_picasso_protected_aperture_init);

void __init tegra_picasso_reserve(void)
{
	tegra_reserve(SZ_256M, SZ_8M, SZ_16M);
}

static void __init tegra_picasso_init(void)
{
	picasso_pinmux_init();
	tegra_clk_init_from_table(picasso_clk_init_table);
	tegra_ehci1_device.dev.platform_data = &tegra_ehci_pdata[0];
	tegra_ehci2_device.dev.platform_data = &tegra_ehci_pdata[1];
	tegra_ehci3_device.dev.platform_data = &tegra_ehci_pdata[2];
	
	platform_add_devices(picasso_devices, ARRAY_SIZE(picasso_devices));

	picasso_emc_init();
	picasso_i2c_init();
	picasso_regulator_init();
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
