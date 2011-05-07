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
#include <linux/i2c/atmel_mxt_ts.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <mach/usb_phy.h>
#include <linux/platform_data/tegra_usb.h>
#include <linux/nct1008.h>
#include <linux/power/bq20z75.h>
#include <linux/cyapa.h>

#include <sound/wm8903.h>

#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/sdhci.h>
#include <mach/pinmux.h>
#include <mach/pinmux-t2.h>
#include <mach/kbc.h>
#include <mach/suspend.h>
#include <mach/seaboard_audio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "board.h"
#include "board-seaboard.h"
#include "clock.h"
#include "devices.h"
#include "gpio-names.h"

extern void tegra_throttling_enable(bool);

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
	{ "pll_c",      "clk_m",        600000000,      true},
	{ "pll_c_out1", "pll_c",        108000000,      true},
	{ "3d",         "pll_c",        300000000,      false},
	{ "2d",         "pll_c",        300000000,      false},
	{ "vi",         "pll_c",        150000000,       true},
	{ "vi_sensor",  "pll_c",        24000000,       false},
	{ "epp",        "pll_c",        300000000,      false},
	{ "mpe",        "pll_c",        100000000,      false},
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
	{ "uartc",      "pll_c",        600000000,      true},
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
	{ "pwm",        "clk_m",        12000000,       false},
	{ "clk_32k",    NULL,           32768,          true},
	{ "pll_s",      "clk_32k",      32768,          false},
	{ "rtc",        "clk_32k",      32768,          true},
	{ "kbc",        "clk_32k",      32768,          true},
	{ "blink",      "clk_32k",      32768,          true},
	{ NULL,		NULL,		0,		0},
};

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
	.reset_gpio = TEGRA_GPIO_PV1,
	.clk = "cdev2",
};

static struct tegra_ehci_platform_data tegra_ehci_pdata[] = {
	[0] = {
		.phy_config = &utmi_phy_config[0],
		.operating_mode = TEGRA_USB_HOST,
		.power_down_on_bus_suspend = 1,
	},
	[1] = {
		.phy_config = &ulpi_phy_config,
		.operating_mode = TEGRA_USB_HOST,
		.power_down_on_bus_suspend = 1,
	},
	[2] = {
		.phy_config = &utmi_phy_config[1],
		.operating_mode = TEGRA_USB_HOST,
		.power_down_on_bus_suspend = 1,
	},
};

static struct cyapa_platform_data cyapa_i2c_platform_data = {
	.flag				= 0,
	.gen				= CYAPA_GEN2,
	.power_state			= CYAPA_PWR_ACTIVE,
	.use_absolute_mode		= false,
	.use_polling_mode		= false,
	.polling_interval_time_active	= CYAPA_ACTIVE_POLLING_INTVAL_TIME,
	.polling_interval_time_lowpower	= CYAPA_LOWPOWER_POLLING_INTVAL_TIME,
	.active_touch_timeout		= CYAPA_ACTIVE_TOUCH_TIMEOUT,
	.name				= CYAPA_I2C_NAME,
	.irq_gpio			= TEGRA_GPIO_CYTP_INT,
	.report_rate			= CYAPA_REPORT_RATE,
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

static const u32 cros_kbd_keymap[] = {
	KEY(0, 2, KEY_LEFTCTRL),
	KEY(0, 4, KEY_RIGHTCTRL),

	KEY(1, 0, KEY_LEFTMETA),
	KEY(1, 1, KEY_ESC),
	KEY(1, 2, KEY_TAB),
	KEY(1, 3, KEY_GRAVE),
	KEY(1, 4, KEY_A),
	KEY(1, 5, KEY_Z),
	KEY(1, 6, KEY_1),
	KEY(1, 7, KEY_Q),

	KEY(2, 0, KEY_F1),
	KEY(2, 2, KEY_F3),
	KEY(2, 3, KEY_F2),
	KEY(2, 4, KEY_D),
	KEY(2, 5, KEY_C),
	KEY(2, 6, KEY_3),
	KEY(2, 7, KEY_E),

	KEY(4, 0, KEY_B),
	KEY(4, 1, KEY_G),
	KEY(4, 2, KEY_T),
	KEY(4, 3, KEY_5),
	KEY(4, 4, KEY_F),
	KEY(4, 5, KEY_V),
	KEY(4, 6, KEY_4),
	KEY(4, 7, KEY_R),

	KEY(5, 0, KEY_F10),
	KEY(5, 1, KEY_F7),
	KEY(5, 2, KEY_F6),
	KEY(5, 4, KEY_S),
	KEY(5, 5, KEY_X),
	KEY(5, 6, KEY_2),
	KEY(5, 7, KEY_W),

	KEY(6, 2, KEY_RIGHTBRACE),
	KEY(6, 4, KEY_K),
	KEY(6, 5, KEY_COMMA),
	KEY(6, 6, KEY_8),
	KEY(6, 7, KEY_I),

	KEY(8, 0, KEY_N),
	KEY(8, 1, KEY_H),
	KEY(8, 2, KEY_Y),
	KEY(8, 3, KEY_6),
	KEY(8, 4, KEY_J),
	KEY(8, 5, KEY_M),
	KEY(8, 6, KEY_7),
	KEY(8, 7, KEY_U),

	KEY(9, 5, KEY_LEFTSHIFT),
	KEY(9, 7, KEY_RIGHTSHIFT),

	KEY(10, 0, KEY_EQUAL),
	KEY(10, 1, KEY_APOSTROPHE),
	KEY(10, 2, KEY_LEFTBRACE),
	KEY(10, 3, KEY_MINUS),
	KEY(10, 4, KEY_SEMICOLON),
	KEY(10, 5, KEY_SLASH),
	KEY(10, 6, KEY_0),
	KEY(10, 7, KEY_P),

	KEY(11, 1, KEY_F9),
	KEY(11, 2, KEY_F8),
	KEY(11, 4, KEY_L),
	KEY(11, 5, KEY_DOT),
	KEY(11, 6, KEY_9),
	KEY(11, 7, KEY_O),

	KEY(13, 0, KEY_RIGHTALT),
	KEY(13, 6, KEY_LEFTALT),

	KEY(14, 1, KEY_BACKSPACE),
	KEY(14, 3, KEY_BACKSLASH),
	KEY(14, 4, KEY_ENTER),
	KEY(14, 5, KEY_SPACE),
	KEY(14, 6, KEY_DOWN),
	KEY(14, 7, KEY_UP),

	KEY(15, 6, KEY_RIGHT),
	KEY(15, 7, KEY_LEFT),
};

static const struct matrix_keymap_data cros_keymap_data = {
        .keymap         = cros_kbd_keymap,
        .keymap_size    = ARRAY_SIZE(cros_kbd_keymap),
};

static struct tegra_kbc_wake_key seaboard_wake_cfg[] = {
       [0] = {
               .row = 1,
               .col = 7,
       },
       [1] = {
               .row = 15,
               .col = 0,
       },
};

static struct tegra_kbc_platform_data seaboard_kbc_platform_data = {
       .debounce_cnt = 2,
       .repeat_cnt = 5 * 32,
       .use_ghost_filter = true,
       .wake_cnt = ARRAY_SIZE(seaboard_wake_cfg),
       .wake_cfg = seaboard_wake_cfg,
};

static void seaboard_kbc_init(void)
{
       struct tegra_kbc_platform_data *data = &seaboard_kbc_platform_data;
       int i, j;

       BUG_ON((KBC_MAX_ROW + KBC_MAX_COL) > KBC_MAX_GPIO);
       /*
        * Setup the pin configuration information.
        */
       for (i = 0; i < KBC_MAX_ROW; i++) {
               data->pin_cfg[i].num = i;
               data->pin_cfg[i].is_row = true;
       }

       for (j = 0; j < KBC_MAX_COL; j++) {
               data->pin_cfg[i + j].num = j;
               data->pin_cfg[i + j].is_row = false;
       }

       tegra_kbc_device.dev.platform_data = data;

       platform_device_register(&tegra_kbc_device);
}

static struct tegra_sdhci_platform_data sdhci_pdata1 = {
	.cd_gpio	= -1,
	.wp_gpio	= -1,
	.power_gpio	= -1,
	.pm_flags	= MMC_PM_KEEP_POWER,
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

static struct seaboard_audio_platform_data audio_pdata = {
	.gpio_spkr_en = TEGRA_GPIO_SPKR_EN,
	.gpio_hp_det = TEGRA_GPIO_HP_DET,
	.gpio_hp_mute = -1,
};

static struct platform_device audio_device = {
	.name = "tegra-snd-seaboard",
	.id   = 0,
	.dev  = {
		.platform_data = &audio_pdata,
	},
};

static struct platform_device spdif_dit_device = {
	.name   = "spdif-dit",
	.id     = -1,
};

static struct platform_device *seaboard_devices[] __initdata = {
	&debug_uart,
	&tegra_uartc_device,
	&tegra_pmu_device,
	&tegra_rtc_device,
	&tegra_gart_device,
	&tegra_sdhci_device4,
	&tegra_sdhci_device3,
	&tegra_sdhci_device1,
	&seaboard_gpio_keys_device,
	&audio_device,
	&tegra_avp_device,
	&tegra_i2s_device1,
	&tegra_das_device,
	&tegra_pcm_device,
	&tegra_spdif_device,
	&spdif_dit_device,
};

static struct nct1008_platform_data nct1008_pdata = {
	.supported_hwrev	= true,
	.ext_range		= false,
	.conv_rate		= 0x08,
	.offset			= 0,
	.hysteresis		= 0,
	.shutdown_ext_limit	= 115,
	.shutdown_local_limit	= 120,
	.throttling_ext_limit	= 90,
	.alarm_fn		= tegra_throttling_enable,
};

static struct wm8903_platform_data wm8903_pdata = {
	.irq_active_low = 0,
	.micdet_cfg = 0,
	.micdet_delay = 100,
	.gpio_base = SEABOARD_GPIO_WM8903(0),
	.gpio_cfg = {
		(WM8903_GPn_FN_DMIC_LR_CLK_OUTPUT << WM8903_GP1_FN_SHIFT),
		(WM8903_GPn_FN_DMIC_LR_CLK_OUTPUT << WM8903_GP2_FN_SHIFT)
			| WM8903_GP1_DIR_MASK,
		0,
		WM8903_GPIO_NO_CONFIG,
		WM8903_GPIO_NO_CONFIG,
	},
};


static struct i2c_board_info __initdata wm8903_device = {
	I2C_BOARD_INFO("wm8903", 0x1a),
	.platform_data = &wm8903_pdata,
	.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_WM8903_IRQ),
};

static struct i2c_board_info __initdata isl29018_device = {
	I2C_BOARD_INFO("isl29018", 0x44),
	.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_ISL29018_IRQ),
};

static struct i2c_board_info __initdata nct1008_device = {
	I2C_BOARD_INFO("nct1008", 0x4c),
	.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_NCT1008_THERM2_IRQ),
	.platform_data = &nct1008_pdata,
};

static struct bq20z75_platform_data bq20z75_pdata = {
	.i2c_retry_count	= 2,
	.battery_detect		= -1,
};

static struct i2c_board_info __initdata bq20z75_device = {
	I2C_BOARD_INFO("bq20z75", 0x0b),
	.platform_data	= &bq20z75_pdata,
};

static struct i2c_board_info __initdata ak8975_device = {
	I2C_BOARD_INFO("ak8975", 0x0c),
	.irq		= TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_MAGNETOMETER),
};

static struct i2c_board_info __initdata cyapa_device = {
	I2C_BOARD_INFO("cypress_i2c_apa", 0x67),
	.irq		= TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_CYTP_INT),
	.platform_data	= &cyapa_i2c_platform_data,
};

static struct i2c_board_info __initdata mpu3050_device = {
	I2C_BOARD_INFO("mpu3050", 0x68),
	.irq            = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_MPU3050_IRQ),
};

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

static struct mxt_platform_data mxt_platform_data = {
	.x_line			= 27,
	.y_line			= 42,
	.x_size			= 768,
	.y_size			= 1386,
	.blen			= 0x16,
	.threshold		= 0x28,
	.voltage		= 3300000,	/* 3.3V */
	.orient			= MXT_DIAGONAL,
	.irqflags		= IRQF_TRIGGER_FALLING,
	.config			= mxt_config_data,
	.config_length		= sizeof(mxt_config_data),
};

static struct i2c_board_info __initdata mxt_device = {
	I2C_BOARD_INFO("atmel_mxt_ts", 0x5a),
	.platform_data = &mxt_platform_data,
	.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_MXT_IRQ),
};

static __initdata struct tegra_pingroup_config mxt_pinmux_config[] = {
	{TEGRA_PINGROUP_LVP0,  TEGRA_MUX_RSVD4,         TEGRA_PUPD_NORMAL,    TEGRA_TRI_NORMAL},
};

static void register_ehci_device(struct platform_device *pdev)
{
	if (pdev->dev.platform_data)
		platform_device_register(pdev);
}

static int seaboard_ehci_init(void)
{
	int gpio_status;

	/* If we ever have a derivative that doesn't use USB1, make the code
	 * below conditional
	 */
	BUG_ON(!tegra_ehci1_device.dev.platform_data);
	gpio_status = gpio_request(TEGRA_GPIO_USB1, "VBUS_USB1");
	if (gpio_status < 0) {
		pr_err("VBUS_USB1 request GPIO FAILED\n");
		WARN_ON(1);
	}

	gpio_status = gpio_direction_output(TEGRA_GPIO_USB1, 1);
	if (gpio_status < 0) {
		pr_err("VBUS_USB1 request GPIO DIRECTION FAILED\n");
		WARN_ON(1);
	}
	gpio_set_value(TEGRA_GPIO_USB1, 1);

	register_ehci_device(&tegra_ehci1_device);
	register_ehci_device(&tegra_ehci2_device);
	register_ehci_device(&tegra_ehci3_device);

	return 0;
}

static void __init seaboard_i2c_init(void)
{
	tegra_i2c_device1.dev.platform_data = &seaboard_i2c1_platform_data;
	tegra_i2c_device2.dev.platform_data = &seaboard_i2c2_platform_data;
	tegra_i2c_device3.dev.platform_data = &seaboard_i2c3_platform_data;
	tegra_i2c_device4.dev.platform_data = &seaboard_dvc_platform_data;

	platform_device_register(&tegra_i2c_device1);
	platform_device_register(&tegra_i2c_device2);
	platform_device_register(&tegra_i2c_device3);
	platform_device_register(&tegra_i2c_device4);
}

static void __init seaboard_i2c_register_devices(void)
{
	tegra_pinmux_config_table(mxt_pinmux_config, ARRAY_SIZE(mxt_pinmux_config));

	gpio_request(TEGRA_GPIO_MXT_RST, "TSP_LDO_ON");
	tegra_gpio_enable(TEGRA_GPIO_MXT_RST);
	gpio_direction_output(TEGRA_GPIO_MXT_RST, 1);
	gpio_export(TEGRA_GPIO_MXT_RST, 0);

	gpio_request(TEGRA_GPIO_MXT_IRQ, "TSP_INT");
	tegra_gpio_enable(TEGRA_GPIO_MXT_IRQ);
	gpio_direction_input(TEGRA_GPIO_MXT_IRQ);

	gpio_request(TEGRA_GPIO_MPU3050_IRQ, "mpu_int");
	gpio_direction_input(TEGRA_GPIO_MPU3050_IRQ);

	gpio_request(TEGRA_GPIO_ISL29018_IRQ, "isl29018");
	gpio_direction_input(TEGRA_GPIO_ISL29018_IRQ);

	gpio_request(TEGRA_GPIO_NCT1008_THERM2_IRQ, "temp_alert");
	gpio_direction_input(TEGRA_GPIO_NCT1008_THERM2_IRQ);

	i2c_register_board_info(0, &wm8903_device, 1);
	i2c_register_board_info(0, &isl29018_device, 1);
	i2c_register_board_info(0, &mxt_device, 1);
	i2c_register_board_info(0, &mpu3050_device, 1);

	i2c_register_board_info(2, &bq20z75_device, 1);

	i2c_register_board_info(4, &nct1008_device, 1);
	i2c_register_board_info(4, &ak8975_device, 1);
}

static void __init kaen_i2c_register_devices(void)
{
	gpio_request(TEGRA_GPIO_MPU3050_IRQ, "mpu_int");
	gpio_direction_input(TEGRA_GPIO_MPU3050_IRQ);

	gpio_request(TEGRA_GPIO_ISL29018_IRQ, "isl29018");
	gpio_direction_input(TEGRA_GPIO_ISL29018_IRQ);

	gpio_request(TEGRA_GPIO_NCT1008_THERM2_IRQ, "temp_alert");
	gpio_direction_input(TEGRA_GPIO_NCT1008_THERM2_IRQ);

	gpio_request(TEGRA_GPIO_CYTP_INT, "gpio_cytp_int");
	gpio_direction_input(TEGRA_GPIO_CYTP_INT);

	i2c_register_board_info(0, &wm8903_device, 1);
	i2c_register_board_info(0, &isl29018_device, 1);
	i2c_register_board_info(0, &cyapa_device, 1);
	i2c_register_board_info(0, &mpu3050_device, 1);

	i2c_register_board_info(2, &bq20z75_device, 1);

	i2c_register_board_info(4, &nct1008_device, 1);
	i2c_register_board_info(4, &ak8975_device, 1);
}

static void __init wario_i2c_register_devices(void)
{
	gpio_request(TEGRA_GPIO_MPU3050_IRQ, "mpu_int");
	gpio_direction_input(TEGRA_GPIO_MPU3050_IRQ);

	gpio_request(TEGRA_GPIO_ISL29018_IRQ, "isl29018");
	gpio_direction_input(TEGRA_GPIO_ISL29018_IRQ);

	gpio_request(TEGRA_GPIO_NCT1008_THERM2_IRQ, "temp_alert");
	gpio_direction_input(TEGRA_GPIO_NCT1008_THERM2_IRQ);

	gpio_request(TEGRA_GPIO_CYTP_INT, "gpio_cytp_int");
	gpio_direction_input(TEGRA_GPIO_CYTP_INT);

	i2c_register_board_info(0, &wm8903_device, 1);
	i2c_register_board_info(0, &isl29018_device, 1);
	i2c_register_board_info(0, &cyapa_device, 1);
	i2c_register_board_info(0, &mpu3050_device, 1);

	i2c_register_board_info(2, &bq20z75_device, 1);

	i2c_register_board_info(4, &nct1008_device, 1);
	i2c_register_board_info(4, &ak8975_device, 1);
}

static void __init aebl_i2c_register_devices(void)
{
	gpio_request(TEGRA_GPIO_MPU3050_IRQ, "mpu_int");
	gpio_direction_input(TEGRA_GPIO_MPU3050_IRQ);

	gpio_request(TEGRA_GPIO_ISL29018_IRQ, "isl29018");
	gpio_direction_input(TEGRA_GPIO_ISL29018_IRQ);

	gpio_request(TEGRA_GPIO_NCT1008_THERM2_IRQ, "temp_alert");
	gpio_direction_input(TEGRA_GPIO_NCT1008_THERM2_IRQ);

	gpio_request(TEGRA_GPIO_CYTP_INT, "gpio_cytp_int");
	gpio_direction_input(TEGRA_GPIO_CYTP_INT);

	i2c_register_board_info(0, &wm8903_device, 1);
	i2c_register_board_info(0, &isl29018_device, 1);
	i2c_register_board_info(0, &cyapa_device, 1);
	i2c_register_board_info(0, &mpu3050_device, 1);

	i2c_register_board_info(2, &bq20z75_device, 1);

	i2c_register_board_info(4, &nct1008_device, 1);
	i2c_register_board_info(4, &ak8975_device, 1);
}

static void __init arthur_i2c_register_devices(void)
{
	gpio_request(TEGRA_GPIO_ISL29018_IRQ, "isl29018");
	gpio_direction_input(TEGRA_GPIO_ISL29018_IRQ);

	gpio_request(TEGRA_GPIO_NCT1008_THERM2_IRQ, "temp_alert");
	gpio_direction_input(TEGRA_GPIO_NCT1008_THERM2_IRQ);

	gpio_request(TEGRA_GPIO_CYTP_INT, "gpio_cytp_int");
	gpio_direction_input(TEGRA_GPIO_CYTP_INT);

	i2c_register_board_info(0, &isl29018_device, 1);
	i2c_register_board_info(0, &cyapa_device, 1);

	i2c_register_board_info(4, &nct1008_device, 1);
}

static void __init seaboard_common_init(void)
{
	seaboard_pinmux_init();

	tegra_clk_init_from_table(seaboard_clk_init_table);

	/* Power up WLAN */
	gpio_request(TEGRA_GPIO_PK6, "wlan_pwr_rst");
	gpio_direction_output(TEGRA_GPIO_PK6, 1);

	tegra_sdhci_device1.dev.platform_data = &sdhci_pdata1;
	tegra_sdhci_device3.dev.platform_data = &sdhci_pdata3;
	tegra_sdhci_device4.dev.platform_data = &sdhci_pdata4;

	platform_add_devices(seaboard_devices, ARRAY_SIZE(seaboard_devices));

	seaboard_power_init();
	seaboard_ehci_init();
	seaboard_panel_init();
	seaboard_kbc_init();

	gpio_request(TEGRA_GPIO_RECOVERY_SWITCH, "recovery_switch");
	gpio_direction_input(TEGRA_GPIO_RECOVERY_SWITCH);
	gpio_export(TEGRA_GPIO_RECOVERY_SWITCH, false);

	gpio_request(TEGRA_GPIO_DEV_SWITCH, "dev_switch");
	gpio_direction_input(TEGRA_GPIO_DEV_SWITCH);
	gpio_export(TEGRA_GPIO_DEV_SWITCH, false);

	gpio_request(TEGRA_GPIO_WP_STATUS, "wp_status");
	gpio_direction_input(TEGRA_GPIO_WP_STATUS);
	gpio_export(TEGRA_GPIO_WP_STATUS, false);
}

static struct tegra_suspend_platform_data seaboard_suspend = {
	.cpu_timer = 5000,
	.cpu_off_timer = 5000,
	.core_timer = 0x7e7e,
	.core_off_timer = 0x7f,
	.separate_req = true,
	.corereq_high = false,
	.sysclkreq_high = true,
	.suspend_mode = TEGRA_SUSPEND_LP1,
};

static void __init __init_debug_uart_D(void)
{
	debug_uart_platform_data[0].membase = IO_ADDRESS(TEGRA_UARTD_BASE);
	debug_uart_platform_data[0].mapbase = TEGRA_UARTD_BASE;
	debug_uart_platform_data[0].irq = INT_UARTD;
}

static void __init __init_debug_uart_B(void)
{
	debug_uart_platform_data[0].membase = IO_ADDRESS(TEGRA_UARTB_BASE);
	debug_uart_platform_data[0].mapbase = TEGRA_UARTB_BASE;
	debug_uart_platform_data[0].irq = INT_UARTB;
}

static void __init tegra_seaboard_init(void)
{
	tegra_init_suspend(&seaboard_suspend);

	__init_debug_uart_D();

	tegra_ehci1_device.dev.platform_data = &tegra_ehci_pdata[0];
	tegra_ehci2_device.dev.platform_data = &tegra_ehci_pdata[1];
	tegra_ehci3_device.dev.platform_data = &tegra_ehci_pdata[2];

	seaboard_common_init();

	seaboard_emc_init();

	seaboard_i2c_register_devices();
	seaboard_i2c_init();

	seaboard_sensors_init();
}

static void __init tegra_kaen_init(void)
{
	struct clk *c, *p;
	tegra_init_suspend(&seaboard_suspend);

	__init_debug_uart_B();

	audio_pdata.gpio_hp_mute = TEGRA_GPIO_KAEN_HP_MUTE;
	tegra_gpio_enable(TEGRA_GPIO_KAEN_HP_MUTE);

	tegra_gpio_enable(TEGRA_GPIO_BATT_DETECT);
	bq20z75_pdata.battery_detect = TEGRA_GPIO_BATT_DETECT;
	/* battery present is low */
	bq20z75_pdata.battery_detect_present = 0;

	seaboard_kbc_platform_data.keymap_data = &cros_keymap_data;

	/* Temporary hack to keep SDIO for wifi capped at 43.2MHz due to
	 * stability issues with brcmfmac at 48MHz.
	 */
	c = tegra_get_clock_by_name("sdmmc1");
	p = tegra_get_clock_by_name("pll_p");
	if (c && p) {
		clk_set_parent(c, p);
		clk_set_rate(c, 43200000);
		clk_enable(c);
	}

	tegra_ehci1_device.dev.platform_data = &tegra_ehci_pdata[0];
	tegra_ehci2_device.dev.platform_data = &tegra_ehci_pdata[1];
	tegra_ehci3_device.dev.platform_data = &tegra_ehci_pdata[2];

	seaboard_common_init();
	kaen_emc_init();

	kaen_i2c_register_devices();
	seaboard_i2c_init();

	kaen_sensors_init();
}

static void __init tegra_aebl_init(void)
{
	tegra_init_suspend(&seaboard_suspend);

	__init_debug_uart_B();

	tegra_gpio_enable(TEGRA_GPIO_BATT_DETECT);
	bq20z75_pdata.battery_detect = TEGRA_GPIO_BATT_DETECT;
	/* battery present is low */
	bq20z75_pdata.battery_detect_present = 0;

	seaboard_kbc_platform_data.keymap_data = &cros_keymap_data;

	tegra_ehci1_device.dev.platform_data = &tegra_ehci_pdata[0];
	tegra_ehci2_device.dev.platform_data = &tegra_ehci_pdata[1];
	tegra_ehci3_device.dev.platform_data = &tegra_ehci_pdata[2];

	seaboard_common_init();
	aebl_emc_init();

	aebl_i2c_register_devices();
	seaboard_i2c_init();

	aebl_sensors_init();
}

static void __init tegra_wario_init(void)
{
	struct clk *c, *p;

	tegra_init_suspend(&seaboard_suspend);

	__init_debug_uart_B();

	seaboard_kbc_platform_data.keymap_data = &cros_keymap_data;

	tegra_ehci1_device.dev.platform_data = &tegra_ehci_pdata[0];
	tegra_ehci2_device.dev.platform_data = &tegra_ehci_pdata[1];
	tegra_ehci3_device.dev.platform_data = &tegra_ehci_pdata[2];

	seaboard_common_init();

	/* wario has same memory config as seaboard */
	seaboard_emc_init();

	/* Temporary hack to keep eMMC controller at 24MHz */
	c = tegra_get_clock_by_name("sdmmc4");
	p = tegra_get_clock_by_name("pll_p");
	if (c && p) {
		clk_set_parent(c, p);
		clk_set_rate(c, 24000000);
		clk_enable(c);
	}

	wario_i2c_register_devices();
	seaboard_i2c_init();
}

static void __init tegra_arthur_init(void)
{
	tegra_init_suspend(&seaboard_suspend);

	__init_debug_uart_B();

	seaboard_kbc_platform_data.keymap_data = &cros_keymap_data;

	tegra_ehci1_device.dev.platform_data = &tegra_ehci_pdata[0];
	tegra_ehci3_device.dev.platform_data = &tegra_ehci_pdata[2];

	seaboard_common_init();
	arthur_emc_init();

	arthur_i2c_register_devices();
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

MACHINE_START(AEBL, "aebl")
	.boot_params    = 0x00000100,
	.map_io         = tegra_map_common_io,
	.init_early     = tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine   = tegra_aebl_init,
MACHINE_END

MACHINE_START(WARIO, "wario")
	.boot_params    = 0x00000100,
	.map_io         = tegra_map_common_io,
	.init_early     = tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine   = tegra_wario_init,
MACHINE_END

MACHINE_START(ARTHUR, "arthur")
	.boot_params    = 0x00000100,
	.map_io         = tegra_map_common_io,
	.init_early     = tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine   = tegra_arthur_init,
MACHINE_END
