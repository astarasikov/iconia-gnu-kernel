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
#include <linux/rfkill-gpio.h>
#include <linux/leds.h>
#include <linux/leds_pwm.h>

#include <sound/max98095.h>
#include <sound/wm8903.h>

#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/sdhci.h>
#include <mach/pinmux.h>
#include <mach/pinmux-t2.h>
#include <mach/kbc.h>
#include <mach/suspend.h>
#include <mach/system.h>
#include <mach/seaboard_audio.h>
#include <mach/clk.h>

#include <asm/cacheflush.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "board.h"
#include "board-seaboard.h"
#include "clock.h"
#include "devices.h"
#include "gpio-names.h"

extern void tegra_throttling_enable(bool);

static void (*legacy_arm_pm_restart)(char mode, const char *cmd);

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
	{ "pll_p_out4", "pll_p",        24000000,       true},
	{ "pll_a",      "pll_p_out1",   56448000,       true},
	{ "pll_a_out0", "pll_a",        11289600,       true},
	{ "cdev1",      "pll_a_out0",   11289600,       true},
	{ "i2s1",       "pll_a_out0",   11289600,       false},
	{ "audio",      "pll_a_out0",   11289600,       false},
	{ "audio_2x",   "audio",        22579200,       false},
	{ "spdif_out",  "pll_a_out0",   11289600,       false},
	{ "uartb",      "pll_p",        216000000,      false},
	{ "uartd",      "pll_p",        216000000,      false},
	{ "pwm",        "clk_m",        12000000,       false},
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
		.vbus_gpio = TEGRA_GPIO_USB1,
	},
	[1] = {
		.hssync_start_delay = 0,
		.idle_wait_delay = 17,
		.elastic_limit = 16,
		.term_range_adj = 6,
		.xcvr_setup = 8,
		.xcvr_lsfslew = 2,
		.xcvr_lsrslew = 2,
		.vbus_gpio = TEGRA_GPIO_USB3,
		.shared_pin_vbus_en_oc = true,
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
		.keep_clock_in_bus_suspend = 1,
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
		.keep_clock_in_bus_suspend = 1,
	},
};

static struct cyapa_platform_data cyapa_i2c_platform_data = {
	.flag				= 0,
	.gen				= CYAPA_GEN2,
	.power_state			= CYAPA_PWR_ACTIVE,
	.polling_interval_time_active	= CYAPA_POLLING_INTERVAL_TIME_ACTIVE,
	.polling_interval_time_lowpower	= CYAPA_POLLING_INTERVAL_TIME_LOWPOWER,
	.active_touch_timeout		= CYAPA_ACTIVE_TOUCH_TIMEOUT,
	.name				= CYAPA_I2C_NAME,
	.irq_gpio			= TEGRA_GPIO_CYTP_INT,
	.report_rate			= CYAPA_REPORT_RATE,
};

static struct tegra_i2c_platform_data seaboard_i2c1_platform_data = {
        .adapter_nr     = 0,
        .bus_count      = 1,
        .bus_clk_rate   = { 400000, 0 },
	.slave_addr	= 0xfc,
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
        .bus_clk_rate   = { 100000, 100000 },
        .bus_mux        = { &i2c2_ddc, &i2c2_gen2 },
        .bus_mux_len    = { 1, 1 },
	.slave_addr	= 0xfc,
};

static struct tegra_i2c_platform_data seaboard_i2c3_platform_data = {
        .adapter_nr     = 3,
        .bus_count      = 1,
        .bus_clk_rate   = { 400000, 0 },
	.slave_addr	= 0xfc,
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
	KEY(2, 1, KEY_F4),
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
	KEY(5, 3, KEY_F5),
	KEY(5, 4, KEY_S),
	KEY(5, 5, KEY_X),
	KEY(5, 6, KEY_2),
	KEY(5, 7, KEY_W),

	KEY(6, 0, KEY_RO),
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

	KEY(9, 2, KEY_102ND),
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
	KEY(13, 2, KEY_YEN),
	KEY(13, 4, KEY_BACKSLASH),

	KEY(13, 6, KEY_LEFTALT),

	KEY(14, 1, KEY_BACKSPACE),
	KEY(14, 3, KEY_BACKSLASH),
	KEY(14, 4, KEY_ENTER),
	KEY(14, 5, KEY_SPACE),
	KEY(14, 6, KEY_DOWN),
	KEY(14, 7, KEY_UP),

	KEY(15, 1, KEY_MUHENKAN),
	KEY(15, 3, KEY_HENKAN),
	KEY(15, 6, KEY_RIGHT),
	KEY(15, 7, KEY_LEFT),
};

static const struct matrix_keymap_data cros_keymap_data = {
        .keymap         = cros_kbd_keymap,
        .keymap_size    = ARRAY_SIZE(cros_kbd_keymap),
};

static struct tegra_kbc_platform_data seaboard_kbc_platform_data = {
	.debounce_cnt = 2,
	.repeat_cnt = 5 * 32,
	.use_ghost_filter = true,
	.wakeup = true,
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

static struct rfkill_gpio_platform_data bt_rfkill_platform_data = {
	.name		= "bt_rfkill",
	.reset_gpio	= TEGRA_GPIO_BT_RESET,
	.power_clk_name	= "blink",
	.type		= RFKILL_TYPE_BLUETOOTH,
};

static struct platform_device bt_rfkill_device = {
	.name	= "rfkill_gpio",
	.id	= -1,
	.dev	= {
		.platform_data = &bt_rfkill_platform_data,
	},
};

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

static struct seaboard_audio_platform_data seaboard_audio_pdata = {
	.gpio_spkr_en = TEGRA_GPIO_SPKR_EN,
	.gpio_hp_det = TEGRA_GPIO_HP_DET,
	.gpio_hp_mute = -1,
};

static struct platform_device seaboard_audio_device = {
	.name = "tegra-snd-seaboard", /* must match DRV_NAME in seaboard.c */
	.id   = 0,
	.dev  = {
		.platform_data = &seaboard_audio_pdata,
	},
};

static struct platform_device spdif_dit_device = {
	.name   = "spdif-dit",
	.id     = -1,
};

static struct led_pwm arthur_pwm_leds[] = {
	{
		.name		= "tegra::kbd_backlight",
		.pwm_id		= 1,
		.max_brightness	= 255,
		.pwm_period_ns	= 1000000,
	},
};

static struct led_pwm_platform_data arthur_pwm_data = {
	.leds		= arthur_pwm_leds,
	.num_leds	= ARRAY_SIZE(arthur_pwm_leds),
};

static struct platform_device arthur_leds_pwm = {
	.name	= "leds_pwm",
	.id	= -1,
	.dev	= {
		.platform_data = &arthur_pwm_data,
	},
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
	&tegra_avp_device,
	&tegra_i2s_device1,
	&tegra_das_device,
	&tegra_pcm_device,
	&tegra_spdif_device,
	&spdif_dit_device,
	&bt_rfkill_device,
	&seaboard_audio_device,
};

static struct platform_device *arthur_specific_devices[] __initdata = {
	&tegra_pwfm1_device,
	&arthur_leds_pwm,
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

static struct max98095_eq_cfg max98095_eq_cfg[] = {
	{ /* Flat response */
		.name = "FLAT",
		.rate = 44100,
		.band1 = { 0x2000, 0xC002, 0x4000, 0x00E9, 0x0000 },
		.band2 = { 0x2000, 0xC00F, 0x4000, 0x02BC, 0x0000 },
		.band3 = { 0x2000, 0xC0A7, 0x4000, 0x0916, 0x0000 },
		.band4 = { 0x2000, 0xC5C2, 0x4000, 0x1A87, 0x0000 },
		.band5 = { 0x2000, 0xF6B0, 0x4000, 0x3F51, 0x0000 },
	},
	{ /* Low pass Fc=1KHz */
		.name = "LOWPASS1K",
		.rate = 44100,
		.band1 = { 0x205D, 0xC001, 0x3FEF, 0x002E, 0x02E0 },
		.band2 = { 0x5B9A, 0xC093, 0x3AB2, 0x088B, 0x1981 },
		.band3 = { 0x0D22, 0xC170, 0x26EA, 0x0D79, 0x32CF },
		.band4 = { 0x0894, 0xC612, 0x01B3, 0x1B34, 0x3FFA },
		.band5 = { 0x0815, 0x3FFF, 0xCF78, 0x0000, 0x29B7 },
	},
	{ /* BASS=-12dB, TREBLE=+9dB, Fc=5KHz */
		.name = "HIBOOST",
		.rate = 44100,
		.band1 = { 0x0815, 0xC001, 0x3AA4, 0x0003, 0x19A2 },
		.band2 = { 0x0815, 0xC103, 0x092F, 0x0B55, 0x3F56 },
		.band3 = { 0x0E0A, 0xC306, 0x1E5C, 0x136E, 0x3856 },
		.band4 = { 0x2459, 0xF665, 0x0CAA, 0x3F46, 0x3EBB },
		.band5 = { 0x5BBB, 0x3FFF, 0xCEB0, 0x0000, 0x28CA },
	},
	{ /* BASS=12dB, TREBLE=+12dB */
		.name = "LOUD12DB",
		.rate = 44100,
		.band1 = { 0x7FC1, 0xC001, 0x3EE8, 0x0020, 0x0BC7 },
		.band2 = { 0x51E9, 0xC016, 0x3C7C, 0x033F, 0x14E9 },
		.band3 = { 0x1745, 0xC12C, 0x1680, 0x0C2F, 0x3BE9 },
		.band4 = { 0x4536, 0xD7E2, 0x0ED4, 0x31DD, 0x3E42 },
		.band5 = { 0x7FEF, 0x3FFF, 0x0BAB, 0x0000, 0x3EED },
	},
	{
		.name = "FLAT",
		.rate = 16000,
		.band1 = { 0x2000, 0xC004, 0x4000, 0x0141, 0x0000 },
		.band2 = { 0x2000, 0xC033, 0x4000, 0x0505, 0x0000 },
		.band3 = { 0x2000, 0xC268, 0x4000, 0x115F, 0x0000 },
		.band4 = { 0x2000, 0xDA62, 0x4000, 0x33C6, 0x0000 },
		.band5 = { 0x2000, 0x4000, 0x4000, 0x0000, 0x0000 },
	},
	{
		.name = "LOWPASS1K",
		.rate = 16000,
		.band1 = { 0x2000, 0xC004, 0x4000, 0x0141, 0x0000 },
		.band2 = { 0x5BE8, 0xC3E0, 0x3307, 0x15ED, 0x26A0 },
		.band3 = { 0x0F71, 0xD15A, 0x08B3, 0x2BD0, 0x3F67 },
		.band4 = { 0x0815, 0x3FFF, 0xCF78, 0x0000, 0x29B7 },
		.band5 = { 0x0815, 0x3FFF, 0xCF78, 0x0000, 0x29B7 },
	},
	{ /* BASS=-12dB, TREBLE=+9dB, Fc=2KHz */
		.name = "HIBOOST",
		.rate = 16000,
		.band1 = { 0x0815, 0xC001, 0x3BD2, 0x0009, 0x16BF },
		.band2 = { 0x080E, 0xC17E, 0xF653, 0x0DBD, 0x3F43 },
		.band3 = { 0x0F80, 0xDF45, 0xEE33, 0x36FE, 0x3D79 },
		.band4 = { 0x590B, 0x3FF0, 0xE882, 0x02BD, 0x3B87 },
		.band5 = { 0x4C87, 0xF3D0, 0x063F, 0x3ED4, 0x3FB1 },
	},
	{ /* BASS=12dB, TREBLE=+12dB */
		.name = "LOUD12DB",
		.rate = 16000,
		.band1 = { 0x7FC1, 0xC001, 0x3D07, 0x0058, 0x1344 },
		.band2 = { 0x2DA6, 0xC013, 0x3CF1, 0x02FF, 0x138B },
		.band3 = { 0x18F1, 0xC08E, 0x244D, 0x0863, 0x34B5 },
		.band4 = { 0x2BE0, 0xF385, 0x04FD, 0x3EC5, 0x3FCE },
		.band5 = { 0x7FEF, 0x4000, 0x0BAB, 0x0000, 0x3EED },
	},
};

static struct max98095_biquad_cfg max98095_bq_cfg[] = {
	{
		.name = "LP4K",
		.rate = 44100,
		.band1 = { 0x5019, 0xe0de, 0x03c2, 0x0784, 0x03c2 },
		.band2 = { 0x5013, 0xe0e5, 0x03c1, 0x0783, 0x03c1 },
	},
	{
		.name = "HP4K",
		.rate = 44100,
		.band1 = { 0x5019, 0xe0de, 0x2e4b, 0xa36a, 0x2e4b },
		.band2 = { 0x5013, 0xe0e5, 0x2e47, 0xa371, 0x2e47 },
	},
};

static struct max98095_pdata max98095_pdata = {
	.eq_cfg		   = max98095_eq_cfg,
	.eq_cfgcnt	   = ARRAY_SIZE(max98095_eq_cfg),
	.bq_cfg		   = max98095_bq_cfg,
	.bq_cfgcnt	   = ARRAY_SIZE(max98095_bq_cfg),
	.digmic_left_mode  = 0, /* 0 -> analog, 1 -> digital mic */
	.digmic_right_mode = 0, /* 0 -> analog, 1 -> digital mic */
};

static struct i2c_board_info __initdata max98095_device = {
	I2C_BOARD_INFO("max98095", 0x10),
	.platform_data = &max98095_pdata,
	.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_MAX98095_IRQ),
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
	.poll_retry_count	= 10,
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

static const u8 seaboard_mxt_config_data[] = {
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
	/* MXT_TOUCH_KEYARRAY(15-1) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00,
	/* MXT_TOUCH_KEYARRAY(15-2) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00,
	/* MXT_SPT_COMMSCONFIG(18) */
	0x00, 0x00,
	/* MXT_PROCG_NOISE(22) */
	0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x19, 0x00,
	0x00, 0x00, 0x05, 0x0a, 0x14, 0x1e, 0x00,
	/* MXT_PROCI_ONETOUCH(24) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_SPT_SELFTEST(25) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	/* MXT_PROCI_TWOTOUCH(27) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_SPT_CTECONFIG(28) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_PROCI_GRIP(40) */
	0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_PROCI_PALM(41) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_SPT_DIGITIZER(43) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static struct mxt_platform_data seaboard_mxt_platform_data = {
	.x_line			= 27,
	.y_line			= 42,
	.x_size			= 768,
	.y_size			= 1386,
	.blen			= 0x16,
	.threshold		= 0x28,
	.voltage		= 3300000,	/* 3.3V */
	.orient			= MXT_DIAGONAL,
	.irqflags		= IRQF_TRIGGER_FALLING,
	.config			= seaboard_mxt_config_data,
	.config_length		= sizeof(seaboard_mxt_config_data),
};

static struct i2c_board_info __initdata seaboard_mxt_device = {
	I2C_BOARD_INFO("atmel_mxt_ts", 0x5a),
	.platform_data = &seaboard_mxt_platform_data,
	.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_MXT_IRQ),
};

static const u8 asymptote_mxt_config_data[] = {
	/* MXT_GEN_COMMAND(6) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_GEN_POWER(7) */
	0xFF, 0xff, 0x32,
	/* MXT_GEN_ACQUIRE(8) */
	0x0a, 0x00, 0x14, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_TOUCH_MULTI(9) */
	0x0F, 0x00, 0x00, 0x20, 0x2a, 0x00, 0x10, 0x50, 0x03, 0x05,
	0x00, 0x02, 0x01, 0x00, 0x0a, 0x0a, 0x0a, 0x0a, 0x00, 0x03,
	0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x0a, 0x00, 0x00, 0x00,
	/* MXT_TOUCH_KEYARRAY(15-1) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00,
	/* MXT_TOUCH_KEYARRAY(15-2) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00,
	/* MXT_SPT_COMMSCONFIG(18) */
	0x00, 0x00,
	/* MXT_PROCG_NOISE(22) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x23, 0x00,
	0x00, 0x00, 0x05, 0x0a, 0x14, 0x1e, 0x00,
	/* MXT_PROCI_ONETOUCH(24) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_SPT_SELFTEST(25) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	/* MXT_PROCI_TWOTOUCH(27) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_SPT_CTECONFIG(28) */
	0x00, 0x00, 0x00, 0x28, 0x28, 0x00,
	/* MXT_PROCI_GRIP(40) */
	0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_PROCI_PALM(41) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_SPT_DIGITIZER(43) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static struct mxt_platform_data asymptote_mxt_platform_data = {
	.x_line			= 32,
	.y_line			= 42,
	.x_size			= 768,
	.y_size			= 1024,
	.blen			= 0x16,
	.threshold		= 0x3c,
	.voltage		= 3300000,	/* 3.3V */
	.orient			= MXT_DIAGONAL,
	.irqflags		= IRQF_TRIGGER_FALLING,
	.config			= asymptote_mxt_config_data,
	.config_length		= sizeof(asymptote_mxt_config_data),
};

static struct i2c_board_info __initdata asymptote_mxt_device = {
	I2C_BOARD_INFO("atmel_mxt_ts", 0x4c),
	.platform_data = &asymptote_mxt_platform_data,
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
	/* If we ever have a derivative that doesn't use USB1, make the code
	 * below conditional
	 */
	BUG_ON(!tegra_ehci1_device.dev.platform_data);

	if (utmi_phy_config[0].vbus_gpio)
		tegra_usb_phy_utmi_vbus_init(&utmi_phy_config[0], "VBUS_USB1");

	if (utmi_phy_config[1].vbus_gpio)
		tegra_usb_phy_utmi_vbus_init(&utmi_phy_config[1], "VBUS_USB3");

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
	tegra_pinmux_config_table(mxt_pinmux_config,
				  ARRAY_SIZE(mxt_pinmux_config));

	gpio_request(SEABOARD_GPIO_MXT_RST, "TSP_LDO_ON");
	tegra_gpio_enable(SEABOARD_GPIO_MXT_RST);
	gpio_direction_output(SEABOARD_GPIO_MXT_RST, 1);
	gpio_export(SEABOARD_GPIO_MXT_RST, 0);

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
	i2c_register_board_info(0, &seaboard_mxt_device, 1);
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

	i2c_register_board_info(0, &cyapa_device, 1);

	i2c_register_board_info(2, &bq20z75_device, 1);

	i2c_register_board_info(3, &isl29018_device, 1);
	i2c_register_board_info(0, &max98095_device, 1);
	i2c_register_board_info(4, &nct1008_device, 1);
}

static void __init asymptote_i2c_register_devices(void)
{
	tegra_pinmux_config_table(mxt_pinmux_config,
				  ARRAY_SIZE(mxt_pinmux_config));

	gpio_request(ASYMPTOTE_GPIO_MXT_RST, "TSP_LDO_ON");
	tegra_gpio_enable(ASYMPTOTE_GPIO_MXT_RST);
	gpio_direction_output(ASYMPTOTE_GPIO_MXT_RST, 1);
	gpio_export(ASYMPTOTE_GPIO_MXT_RST, 0);

	gpio_request(ASYMPTOTE_GPIO_MXT_SLEEP, "TSP_SLEEP");
	tegra_gpio_enable(ASYMPTOTE_GPIO_MXT_SLEEP);
	gpio_direction_output(ASYMPTOTE_GPIO_MXT_SLEEP, 0);
	gpio_export(ASYMPTOTE_GPIO_MXT_SLEEP, 0);

	gpio_request(TEGRA_GPIO_MXT_IRQ, "TSP_INT");
	tegra_gpio_enable(TEGRA_GPIO_MXT_IRQ);
	gpio_direction_input(TEGRA_GPIO_MXT_IRQ);

	gpio_request(TEGRA_GPIO_MPU3050_IRQ, "mpu_int");
	gpio_direction_input(TEGRA_GPIO_MPU3050_IRQ);

	gpio_request(TEGRA_GPIO_NCT1008_THERM2_IRQ, "temp_alert");
	gpio_direction_input(TEGRA_GPIO_NCT1008_THERM2_IRQ);

	i2c_register_board_info(0, &wm8903_device, 1);
	i2c_register_board_info(0, &mpu3050_device, 1);
	i2c_register_board_info(2, &bq20z75_device, 1);
	i2c_register_board_info(3, &asymptote_mxt_device, 1);
	i2c_register_board_info(4, &nct1008_device, 1);
}

static void __init __seaboard_common_init(struct platform_device **devices,
					  int num_devices)
{
	tegra_clk_init_from_table(seaboard_clk_init_table);

	/* Power up WLAN */
	gpio_request(TEGRA_GPIO_PK6, "wlan_pwr_rst");
	gpio_direction_output(TEGRA_GPIO_PK6, 1);

	tegra_sdhci_device1.dev.platform_data = &sdhci_pdata1;
	tegra_sdhci_device3.dev.platform_data = &sdhci_pdata3;
	tegra_sdhci_device4.dev.platform_data = &sdhci_pdata4;

	platform_add_devices(seaboard_devices, ARRAY_SIZE(seaboard_devices));
	if (devices)		/* Add board-specific devices. */
		platform_add_devices(devices, num_devices);

	seaboard_power_init();
	seaboard_ehci_init();
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

static void __init tegra_set_clock_readskew(const char *clk_name, int skew)
{
	struct clk *c = tegra_get_clock_by_name(clk_name);
	if (!c)
		return;

	tegra_sdmmc_tap_delay(c, skew);
	clk_put(c);
}

static void __init seaboard_common_init(void)
{
	seaboard_pinmux_init();
	__seaboard_common_init(NULL, 0);
}

static void __init kaen_common_init(void)
{
	kaen_pinmux_init();
	__seaboard_common_init(NULL, 0);
}

static void __init aebl_common_init(void)
{
	aebl_pinmux_init();
	__seaboard_common_init(NULL, 0);
}

static void __init arthur_common_init(void)
{
	arthur_pinmux_init();
	__seaboard_common_init(arthur_specific_devices,
			       ARRAY_SIZE(arthur_specific_devices));
}

static void __init ventana_common_init(void)
{
	ventana_pinmux_init();
	__seaboard_common_init(NULL, 0);
}

static void __init asymptote_common_init(void)
{
	asymptote_pinmux_init();
	__seaboard_common_init(NULL, 0);
}

static struct tegra_suspend_platform_data seaboard_suspend = {
	.cpu_timer = 5000,
	.cpu_off_timer = 5000,
	.core_timer = 0x7e7e,
	.core_off_timer = 0x7f,
	.separate_req = true,
	.corereq_high = false,
	.sysclkreq_high = true,
	.suspend_mode = TEGRA_SUSPEND_LP0,
};

static void __init __init_debug_uart_D(void)
{
	struct clk *c;

	c = tegra_get_clock_by_name("uartd");
	clk_enable(c);

	debug_uart_platform_data[0].membase = IO_ADDRESS(TEGRA_UARTD_BASE);
	debug_uart_platform_data[0].mapbase = TEGRA_UARTD_BASE;
	debug_uart_platform_data[0].irq = INT_UARTD;
}

static void __init __init_debug_uart_B(void)
{
	struct clk *c;

	c = tegra_get_clock_by_name("uartb");
	clk_enable(c);

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
	seaboard_panel_init();
	seaboard_emc_init();

	seaboard_i2c_register_devices();
	seaboard_i2c_init();

	seaboard_sensors_init();
}

/* Architecture-specific restart for Kaen and other boards, where a GPIO line
 * is used to reset CPU and TPM together.
 *
 * Most of this function mimicks arm_machine_restart in process.c, except that
 * that function turns off caching and then flushes the cache one more time,
 * and we do not.  This is certainly less clean but unlikely to matter as the
 * additional dirty cache lines do not contain critical data.
 *
 * On boards that don't implement the reset hardware we fall back to the old
 * method.
 */
static void kaen_machine_restart(char mode, const char *cmd)
{
	/* Disable interrupts first */
	local_irq_disable();
	local_fiq_disable();

	/* We must flush the L2 cache for preserved / kcrashmem */
	outer_flush_all();

	/* Clean and invalidate caches */
	flush_cache_all();

	/* Reboot by resetting CPU and TPM via GPIO */
	printk(KERN_INFO "restart: issuing GPIO reset\n");
	gpio_set_value(TEGRA_GPIO_RESET, 0);

	printk(KERN_INFO "restart: trying legacy reboot\n");
	legacy_arm_pm_restart(mode, cmd);
}

static void __init tegra_kaen_init(void)
{
	tegra_init_suspend(&seaboard_suspend);

	__init_debug_uart_B();

	/* Enable RF for 3G modem */
	tegra_gpio_enable(TEGRA_GPIO_W_DISABLE);
	gpio_request(TEGRA_GPIO_W_DISABLE, "w_disable");
	gpio_direction_output(TEGRA_GPIO_W_DISABLE, 1);

	seaboard_audio_pdata.gpio_hp_mute = TEGRA_GPIO_KAEN_HP_MUTE;
	tegra_gpio_enable(TEGRA_GPIO_KAEN_HP_MUTE);

	tegra_gpio_enable(TEGRA_GPIO_BATT_DETECT);
	bq20z75_pdata.battery_detect = TEGRA_GPIO_BATT_DETECT;
	/* battery present is low */
	bq20z75_pdata.battery_detect_present = 0;

	seaboard_kbc_platform_data.keymap_data = &cros_keymap_data;

	/* setting skew makes WIFI stable when sdmmc1 runs 48MHz. */
	tegra_set_clock_readskew("sdmmc1", 8);

	tegra_ehci1_device.dev.platform_data = &tegra_ehci_pdata[0];
	tegra_ehci2_device.dev.platform_data = &tegra_ehci_pdata[1];
	tegra_ehci3_device.dev.platform_data = &tegra_ehci_pdata[2];

	kaen_common_init();
	kaen_panel_init();
	kaen_emc_init();

	kaen_i2c_register_devices();
	seaboard_i2c_init();

	kaen_sensors_init();
	legacy_arm_pm_restart = arm_pm_restart;
	arm_pm_restart = kaen_machine_restart;
}

static void __init tegra_aebl_init(void)
{
	tegra_init_suspend(&seaboard_suspend);

	__init_debug_uart_B();

	/* Enable RF for 3G modem */
	tegra_gpio_enable(TEGRA_GPIO_W_DISABLE);
	gpio_request(TEGRA_GPIO_W_DISABLE, "w_disable");
	gpio_direction_output(TEGRA_GPIO_W_DISABLE, 1);

	tegra_gpio_enable(TEGRA_GPIO_BATT_DETECT);
	bq20z75_pdata.battery_detect = TEGRA_GPIO_BATT_DETECT;
	/* battery present is low */
	bq20z75_pdata.battery_detect_present = 0;

	seaboard_kbc_platform_data.keymap_data = &cros_keymap_data;

	/* setting skew makes WIFI stable when sdmmc1 runs 48MHz. */
	tegra_set_clock_readskew("sdmmc1", 8);

	tegra_ehci1_device.dev.platform_data = &tegra_ehci_pdata[0];
	tegra_ehci2_device.dev.platform_data = &tegra_ehci_pdata[1];
	tegra_ehci3_device.dev.platform_data = &tegra_ehci_pdata[2];

	aebl_common_init();
	seaboard_panel_init();
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
	wario_panel_init();
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
	int err;
	struct clk *c, *p;

	tegra_init_suspend(&seaboard_suspend);

	__init_debug_uart_B();

	seaboard_audio_pdata.gpio_spkr_en = -1; /* No spkr_en on max98095. */
	/* Set up the GPIO and pingroup controlling the camera's power. */
	tegra_gpio_enable(TEGRA_GPIO_PV4);
	err = gpio_request(TEGRA_GPIO_PV4, "cam_3v3_pwr_en");
	WARN_ON(err);
	err = gpio_direction_output(TEGRA_GPIO_PV4, 1);
	WARN_ON(err);
	tegra_pinmux_set_tristate(TEGRA_PINGROUP_GPV, TEGRA_TRI_NORMAL);
	/* Export this GPIO so power can be controlled from userspace. */
	err = gpio_export(TEGRA_GPIO_PV4, false);
	WARN_ON(err);

	seaboard_kbc_platform_data.keymap_data = &cros_keymap_data;

	tegra_ehci1_device.dev.platform_data = &tegra_ehci_pdata[0];
	tegra_ehci3_device.dev.platform_data = &tegra_ehci_pdata[2];
	arthur_common_init();
	arthur_panel_init();
	arthur_emc_init();

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

	arthur_i2c_register_devices();
	seaboard_i2c_init();
}

static void __init tegra_asymptote_init(void)
{
	struct clk *c, *p;

	tegra_init_suspend(&seaboard_suspend);

	__init_debug_uart_B();

	seaboard_kbc_platform_data.keymap_data = &cros_keymap_data;

	tegra_ehci1_device.dev.platform_data = &tegra_ehci_pdata[0];
	tegra_ehci2_device.dev.platform_data = &tegra_ehci_pdata[1];
	tegra_ehci3_device.dev.platform_data = &tegra_ehci_pdata[2];

	asymptote_common_init();
	asymptote_panel_init();
	/* asymptote has same memory config as seaboard (for now) */
	seaboard_emc_init();

	/* Temporary hack to keep eMMC controller at 24MHz */
	c = tegra_get_clock_by_name("sdmmc4");
	p = tegra_get_clock_by_name("pll_p");
	if (c && p) {
		clk_set_parent(c, p);
		clk_set_rate(c, 24000000);
		clk_enable(c);
	}

	asymptote_i2c_register_devices();
	seaboard_i2c_init();
}

#define GPIO_KEY(_id, _gpio, _iswake)		\
	{					\
		.code = _id,			\
		.gpio = TEGRA_GPIO_##_gpio,	\
		.active_low = 1,		\
		.desc = #_id,			\
		.type = EV_KEY,			\
		.wakeup = _iswake,		\
		.debounce_interval = 10,	\
	}

static struct gpio_keys_button ventana_keys[] = {
	GPIO_KEY(KEY_MENU,		PQ3, 0),
	GPIO_KEY(KEY_HOME,		PQ1, 0),
	GPIO_KEY(KEY_BACK,		PQ2, 0),
	GPIO_KEY(KEY_VOLUMEUP,		PQ5, 0),
	GPIO_KEY(KEY_VOLUMEDOWN,	PQ4, 0),
	GPIO_KEY(KEY_POWER,		PV2, 1),
};

static struct gpio_keys_platform_data ventana_keys_data = {
	.buttons	= ventana_keys,
	.nbuttons	= ARRAY_SIZE(ventana_keys),
};

void __init tegra_ventana_init(void)
{
	tegra_init_suspend(&seaboard_suspend);

	__init_debug_uart_D();

	seaboard_gpio_keys_device.dev.platform_data = &ventana_keys_data;

	tegra_ehci1_device.dev.platform_data = &tegra_ehci_pdata[0];
	tegra_ehci2_device.dev.platform_data = &tegra_ehci_pdata[1];
	tegra_ehci3_device.dev.platform_data = &tegra_ehci_pdata[2];

	ventana_common_init();
	seaboard_panel_init();
	ventana_emc_init();

	seaboard_i2c_register_devices();
	seaboard_i2c_init();
}

static const char *seaboard_dt_board_compat[] = {
	"nvidia,seaboard",
	NULL
};

MACHINE_START(SEABOARD, "seaboard")
	.boot_params    = 0x00000100,
	.map_io         = tegra_map_common_io,
	.init_early     = tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine   = tegra_seaboard_init,
	.dt_compat	= seaboard_dt_board_compat,
MACHINE_END

static const char *kaen_dt_board_compat[] = {
	"google,kaen",
	NULL
};

MACHINE_START(KAEN, "kaen")
	.boot_params    = 0x00000100,
	.map_io         = tegra_map_common_io,
	.init_early     = tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine   = tegra_kaen_init,
	.dt_compat	= kaen_dt_board_compat,
MACHINE_END

static const char *aebl_dt_board_compat[] = {
	"google,aebl",
	NULL
};

MACHINE_START(AEBL, "aebl")
	.boot_params    = 0x00000100,
	.map_io         = tegra_map_common_io,
	.init_early     = tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine   = tegra_aebl_init,
	.dt_compat	= aebl_dt_board_compat,
MACHINE_END

static const char const *asymptote_dt_board_compat[] = {
	"google,asymptote",
	NULL
};

MACHINE_START(ASYMPTOTE, "asymptote")
	.boot_params    = 0x00000100,
	.map_io         = tegra_map_common_io,
	.init_early     = tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine   = tegra_asymptote_init,
	.dt_compat	= asymptote_dt_board_compat,
MACHINE_END

static const char *wario_dt_board_compat[] = {
	"nvidia,wario",
	NULL
};

MACHINE_START(WARIO, "wario")
	.boot_params    = 0x00000100,
	.map_io         = tegra_map_common_io,
	.init_early     = tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine   = tegra_wario_init,
	.dt_compat	= wario_dt_board_compat,
MACHINE_END

static const char *arthur_dt_board_compat[] = {
	"nvidia,arthur",
	NULL
};

MACHINE_START(ARTHUR, "arthur")
	.boot_params    = 0x00000100,
	.map_io         = tegra_map_common_io,
	.init_early     = tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine   = tegra_arthur_init,
	.dt_compat	= arthur_dt_board_compat,
MACHINE_END

static const char *ventana_dt_board_compat[] = {
	"nvidia,ventana",
	NULL
};

MACHINE_START(VENTANA, "ventana")
	.boot_params    = 0x00000100,
	.map_io		= tegra_map_common_io,
	.init_early	= tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine	= tegra_ventana_init,
	.dt_compat	= ventana_dt_board_compat,
MACHINE_END
