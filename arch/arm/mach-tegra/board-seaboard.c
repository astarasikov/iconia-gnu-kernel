/*
 * Copyright (c) 2010, 2011 NVIDIA Corporation.
 * Copyright (C) 2010, 2011 Google, Inc.
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
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/i2c.h>
#include <linux/i2c-tegra.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>

#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/sdhci.h>
#include <mach/pinmux.h>
#include <mach/pinmux-t2.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "board.h"
#include "board-seaboard.h"
#include "clock.h"
#include "devices.h"
#include "gpio-names.h"

static struct plat_serial8250_port debug_uart_platform_data[] = {
	{
		/* Memory and IRQ filled in before registration */
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

static __initdata struct tegra_clk_init_table seaboard_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "clk_m",      NULL,           12000000,       true},
	{ "pll_m",      "clk_m",        666000000,      true},
	{ "3d",         "pll_m",        300000000,      false},
	{ "2d",         "pll_m",        300000000,      false},
	{ "vi",         "pll_m",        50000000,       true},
	{ "vi_sensor",  "pll_m",        100000000,      false},
	{ "epp",        "pll_m",        300000000,      false},
	{ "mpe",        "pll_m",        100000000,      false},
	{ "emc",        "pll_m",        666000000,      true},
	{ "pll_c",      "clk_m",        600000000,      true},
	{ "pll_c_out1", "pll_c",        108000000,      true},
	{ "vde",        "pll_c",        240000000,      false},
	{ "pll_p",      "clk_m",        216000000,      true},
	{ "pll_p_out1", "pll_p",        28800000,       true},
	{ "pll_a",      "pll_p_out1",   56448000,       true},
	{ "pll_a_out0", "pll_a",        11289600,       true},
	{ "cdev1",      "pll_a_out0",   11289600,       true},
	{ "i2s1",       "pll_a_out0",   11289600,       false},
	{ "audio",      "pll_a_out0",   11289600,       false},
	{ "audio_2x",   "audio",        22579200,       false},
	{ "pll_p_out2", "pll_p",        48000000,       true},
	{ "pll_p_out3", "pll_p",        72000000,       true},
	{ "i2c1_i2c",   "pll_p_out3",   72000000,       true},
	{ "i2c2_i2c",   "pll_p_out3",   72000000,       true},
	{ "i2c3_i2c",   "pll_p_out3",   72000000,       true},
	{ "dvc_i2c",    "pll_p_out3",   72000000,       true},
	{ "csi",        "pll_p_out3",   72000000,       false},
	{ "pll_p_out4", "pll_p",        24000000,       true},
	{ "hclk",       "sclk",         108000000,      true},
	{ "pclk",       "hclk",         54000000,       true},
	{ "spdif_in",   "pll_p",        36000000,       false},
	{ "csite",      "pll_p",        144000000,      true},
	{ "host1x",     "pll_p",        144000000,      false},
	{ "disp1",      "pll_p",        216000000,      false},
	{ "pll_d",      "clk_m",        1000000,        false},
	{ "pll_d_out0", "pll_d",        500000,         false},
	{ "dsi",        "pll_d",        1000000,        false},
	{ "pll_u",      "clk_m",        480000000,      true},
	{ "clk_d",      "clk_m",        24000000,       true},
	{ "timer",      "clk_m",        12000000,       true},
	{ "i2s2",       "clk_m",        12000000,       false},
	{ "spdif_out",  "pll_a_out0",   11289600,       false},
	{ "spi",        "clk_m",        12000000,       false},
	{ "xio",        "clk_m",        12000000,       false},
	{ "twc",        "clk_m",        12000000,       false},
	{ "sbc1",       "clk_m",        12000000,       false},
	{ "sbc2",       "clk_m",        12000000,       false},
	{ "sbc3",       "clk_m",        12000000,       false},
	{ "sbc4",       "clk_m",        12000000,       false},
	{ "ide",        "clk_m",        12000000,       false},
	{ "ndflash",    "clk_m",        12000000,       false},
	{ "vfir",       "clk_m",        12000000,       false},
	{ "la",         "clk_m",        12000000,       false},
	{ "owr",        "clk_m",        12000000,       false},
	{ "nor",        "clk_m",        12000000,       false},
	{ "mipi",       "clk_m",        12000000,       false},
	{ "i2c1",       "clk_m",        3000000,        false},
	{ "i2c2",       "clk_m",        3000000,        false},
	{ "i2c3",       "clk_m",        3000000,        false},
	{ "dvc",        "clk_m",        3000000,        false},
	{ "uarta",      "clk_m",        12000000,       false},
        { "uartb",      "pll_p",        216000000,      true},
	{ "uartc",      "clk_m",        12000000,       false},
        { "uartd",      "pll_p",        216000000,      true},
	{ "uarte",      "clk_m",        12000000,       false},
	{ "cve",        "clk_m",        12000000,       false},
	{ "tvo",        "clk_m",        12000000,       false},
	{ "hdmi",       "clk_m",        12000000,       false},
	{ "tvdac",      "clk_m",        12000000,       false},
	{ "disp2",      "clk_m",        12000000,       false},
	{ "usbd",       "clk_m",        12000000,       true},
	{ "usb2",       "clk_m",        12000000,       false},
	{ "usb3",       "clk_m",        12000000,       true},
	{ "isp",        "clk_m",        12000000,       false},
	{ "csus",       "clk_m",        12000000,       false},
	{ "pwm",        "clk_32k",      32768,          false},
	{ "clk_32k",    NULL,           32768,          true},
	{ "pll_s",      "clk_32k",      32768,          false},
	{ "rtc",        "clk_32k",      32768,          true},
	{ "kbc",        "clk_32k",      32768,          true},
	{ "blink",      "clk_32k",      32768,          true},
	{ NULL,		NULL,		0,		0},
};

static struct tegra_i2c_platform_data seaboard_i2c1_platform_data = {
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

static struct tegra_i2c_platform_data seaboard_i2c2_platform_data = {
        .adapter_nr     = 1,
        .bus_count      = 2,
        .bus_clk_rate   = { 400000, 100000 },
        .bus_mux        = { &i2c2_ddc, &i2c2_gen2 },
        .bus_mux_len    = { 1, 1 },
};

static struct tegra_i2c_platform_data seaboard_i2c3_platform_data = {
        .adapter_nr     = 3,
        .bus_count      = 1,
        .bus_clk_rate   = { 400000, 0 },
};

static struct tegra_i2c_platform_data seaboard_dvc_platform_data = {
        .adapter_nr     = 4,
        .bus_count      = 1,
        .bus_clk_rate   = { 400000, 0 },
        .is_dvc         = true,
};

static struct gpio_keys_button seaboard_gpio_keys_buttons[] = {
	{
		.code		= SW_LID,
		.gpio		= TEGRA_GPIO_LIDSWITCH,
		.active_low	= 0,
		.desc		= "Lid",
		.type		= EV_SW,
		.wakeup		= 1,
		.debounce_interval = 1,
	},
	{
		.code		= KEY_POWER,
		.gpio		= TEGRA_GPIO_POWERKEY,
		.active_low	= 1,
		.desc		= "Power",
		.type		= EV_KEY,
		.wakeup		= 1,
	},
};

static struct gpio_keys_platform_data seaboard_gpio_keys = {
	.buttons	= seaboard_gpio_keys_buttons,
	.nbuttons	= ARRAY_SIZE(seaboard_gpio_keys_buttons),
};

static struct platform_device seaboard_gpio_keys_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.dev		= {
		.platform_data = &seaboard_gpio_keys,
	}
};

static struct tegra_sdhci_platform_data sdhci_pdata1 = {
	.cd_gpio	= -1,
	.wp_gpio	= -1,
	.power_gpio	= -1,
};

static struct tegra_sdhci_platform_data sdhci_pdata3 = {
	.cd_gpio	= TEGRA_GPIO_SD2_CD,
	.wp_gpio	= TEGRA_GPIO_SD2_WP,
	.power_gpio	= TEGRA_GPIO_SD2_POWER,
};

static struct tegra_sdhci_platform_data sdhci_pdata4 = {
	.cd_gpio	= -1,
	.wp_gpio	= -1,
	.power_gpio	= -1,
	.is_8bit	= 1,
};

static struct platform_device *seaboard_devices[] __initdata = {
	&debug_uart,
	&tegra_pmu_device,
	&tegra_sdhci_device1,
	&tegra_sdhci_device3,
	&tegra_sdhci_device4,
	&seaboard_gpio_keys_device,
};

static struct i2c_board_info __initdata isl29018_device = {
	I2C_BOARD_INFO("isl29018", 0x44),
	.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_ISL29018_IRQ),
};

static struct i2c_board_info __initdata adt7461_device = {
	I2C_BOARD_INFO("adt7461", 0x4c),
};

static void __init seaboard_i2c_init(void)
{
	gpio_request(TEGRA_GPIO_ISL29018_IRQ, "isl29018");
	gpio_direction_input(TEGRA_GPIO_ISL29018_IRQ);

	i2c_register_board_info(0, &isl29018_device, 1);

	i2c_register_board_info(4, &adt7461_device, 1);

	tegra_i2c_device1.dev.platform_data = &seaboard_i2c1_platform_data;
	tegra_i2c_device2.dev.platform_data = &seaboard_i2c2_platform_data;
	tegra_i2c_device3.dev.platform_data = &seaboard_i2c3_platform_data;
	tegra_i2c_device4.dev.platform_data = &seaboard_dvc_platform_data;

	platform_device_register(&tegra_i2c_device1);
	platform_device_register(&tegra_i2c_device2);
	platform_device_register(&tegra_i2c_device3);
	platform_device_register(&tegra_i2c_device4);
}

static void __init seaboard_common_init(void)
{
	seaboard_pinmux_init();

	tegra_clk_init_from_table(seaboard_clk_init_table);

	tegra_sdhci_device1.dev.platform_data = &sdhci_pdata1;
	tegra_sdhci_device3.dev.platform_data = &sdhci_pdata3;
	tegra_sdhci_device4.dev.platform_data = &sdhci_pdata4;

	platform_add_devices(seaboard_devices, ARRAY_SIZE(seaboard_devices));
}

static void __init tegra_seaboard_init(void)
{
	/* Seaboard uses UARTD for the debug port. */
	debug_uart_platform_data[0].membase = IO_ADDRESS(TEGRA_UARTD_BASE);
	debug_uart_platform_data[0].mapbase = TEGRA_UARTD_BASE;
	debug_uart_platform_data[0].irq = INT_UARTD;

	seaboard_common_init();

	seaboard_i2c_init();
}

static void __init tegra_kaen_init(void)
{
	/* Kaen uses UARTB for the debug port. */
	debug_uart_platform_data[0].membase = IO_ADDRESS(TEGRA_UARTB_BASE);
	debug_uart_platform_data[0].mapbase = TEGRA_UARTB_BASE;
	debug_uart_platform_data[0].irq = INT_UARTB;

	seaboard_common_init();

	seaboard_i2c_init();
}

static void __init tegra_wario_init(void)
{
	/* Wario uses UARTB for the debug port. */
	debug_uart_platform_data[0].membase = IO_ADDRESS(TEGRA_UARTB_BASE);
	debug_uart_platform_data[0].mapbase = TEGRA_UARTB_BASE;
	debug_uart_platform_data[0].irq = INT_UARTB;

	seaboard_common_init();

	seaboard_i2c_init();
}


MACHINE_START(SEABOARD, "seaboard")
	.boot_params    = 0x00000100,
	.map_io         = tegra_map_common_io,
	.init_early     = tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine   = tegra_seaboard_init,
MACHINE_END

MACHINE_START(KAEN, "kaen")
	.boot_params    = 0x00000100,
	.map_io         = tegra_map_common_io,
	.init_early     = tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine   = tegra_kaen_init,
MACHINE_END

MACHINE_START(WARIO, "wario")
	.boot_params    = 0x00000100,
	.map_io         = tegra_map_common_io,
	.init_early     = tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine   = tegra_wario_init,
MACHINE_END
