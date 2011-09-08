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
#include <linux/regulator/consumer.h>
#include <linux/pda_power.h>
#include <linux/mfd/acer_picasso_ec.h>
#include <linux/nct1008.h>
#include <linux/rfkill-gpio.h>

#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/pinmux.h>
#include <mach/iomap.h>
#include <mach/io.h>
#include <mach/sdhci.h>
#include <mach/suspend.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <sound/wm8903.h>
#include <mach/tegra_wm8903_pdata.h>

#include "board.h"
#include "board-picasso.h"
#include "clock.h"
#include "devices.h"
#include "fuse.h"
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
	{ "clk_m",	NULL,	12000000,	true},
	{ "pll_c",	"clk_m",	600000000,	true},
	{ "pll_p",	"clk_m",	216000000,	true},
	{ "uartb",	"pll_p",	216000000,	true},
	{ "uartc",	"pll_c",	600000000,	true},
	{ "uartd",	"pll_p",	216000000,	true},
	{ "blink",	"clk_32k",	32768,		true},
	{ "pll_a",	NULL,		11289600,	true},
	{ "pll_a_out0",	NULL,		11289600,	true},
	{ "i2s1",	"pll_a_out0",	2822400,	true},
	{ "i2s2",	"pll_a_out0",	11289600,	true},
	{ "audio",	"pll_a_out0",	11289600,	true},
	{ "audio_2x",	"audio",	22579200,	true},
	{ "spdif_out",	"pll_a_out0",	5644800,	false},
	{ NULL,		NULL,		0,		0},
};

/******************************************************************************
 * Touchscreen
 *****************************************************************************/
static const u8 mxt_config_data[] = {
	/* MXT_GEN_COMMAND(6) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_GEN_POWER(7) */
	0x41, 0xff, 0x32,
	/* MXT_GEN_ACQUIRE(8) */
	0x09, 0x00, 0x0a, 0x0a, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
	/* MXT_TOUCH_MULTI(9) */
	0x8f, 0x00, 0x00, 0x1c, 0x29, 0x00, 0x10, 0x37, 0x03, 0x01,	
	0x00, 0x05, 0x05, 0x20, 0x0a, 0x05, 0x0a, 0x05, 0x1f, 0x03,
	0xff, 0x04, 0x00, 0x00, 0x00, 0x00, 0x98, 0x22, 0xd4, 0x16,
	0x0a, 0x0a, 0x00, 0x00,
	/* MXT_TOUCH_KEYARRAY(15-1) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00,
	/* MXT_TOUCH_KEYARRAY(15-2) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00,
	/* MXT_SPT_COMMSCONFIG(18) */
	0x00, 0x00,
	/* MXT_PROCG_NOISE(22) */
	0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00,
	0x00, 0x00, 0x0a, 0x13, 0x19, 0x1e, 0x00,
	/* MXT_PROCI_ONETOUCH(24) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_SPT_SELFTEST(25) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	/* MXT_PROCI_TWOTOUCH(27) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_SPT_CTECONFIG(28) */
	0x00, 0x00, 0x00, 0x08, 0x1c, 0x3c,
	/* MXT_PROCI_GRIP(40) */
	0x00, 0x00, 0x00, 0x00, 0x00,
	/* MXT_PROCI_PALM(41) */
	0x01, 0x00, 0x00, 0x23, 0x00, 0x00,
	/* MXT_TOUCH_PROXIMITY(43) */
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

static struct i2c_board_info mxt_device_picasso = {
	I2C_BOARD_INFO("atmel_mxt_ts", 0x4c),
	.platform_data = &mxt_platform_data,
	.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_MXT_IRQ),
};

static struct i2c_board_info mxt_device_tf101 = {
	I2C_BOARD_INFO("atmel_mxt_ts", 0x5b),
	.platform_data = &mxt_platform_data,
	.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_MXT_IRQ),
};

static void __init picasso_touch_init(void) {
	gpio_request(TEGRA_GPIO_MXT_IRQ, "atmel_touch_chg");
	gpio_request(TEGRA_GPIO_VENTANA_TS_RST, "atmel_touch_reset");

	gpio_set_value(TEGRA_GPIO_VENTANA_TS_RST, 0);
	msleep(1);
	gpio_set_value(TEGRA_GPIO_VENTANA_TS_RST, 1);
	msleep(100);

	if(machine_is_picasso())
		i2c_register_board_info(0, &mxt_device_picasso, 1);

	if(machine_is_tf101())
		i2c_register_board_info(0, &mxt_device_tf101, 1);
}

/******************************************************************************
 * Power Supply
 *****************************************************************************/
static char *picasso_batteries[] = {
	"battery",
};

static struct resource picasso_power_resources[] = {
	[0] = {
		.name = "ac",
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE |
		IORESOURCE_IRQ_LOWEDGE,
		.start = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_AC_ONLINE),
		.end = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_AC_ONLINE),
	},
};

static int picasso_is_ac_online(void)
{
	return !gpio_get_value(TEGRA_GPIO_AC_ONLINE);
}

static void picasso_set_charge(int flags)
{
	gpio_direction_output(TEGRA_GPIO_VENTANA_DISABLE_CHARGER, !flags);
}

static int picasso_power_init(struct device *dev)
{
	int rc = 0;

	rc = gpio_request(TEGRA_GPIO_VENTANA_DISABLE_CHARGER, "Charger Disable");
	if (rc)
		goto err_chg;

	rc = gpio_request(TEGRA_GPIO_AC_ONLINE, "Charger Detection");
	if (rc)
		goto err_ac;

	return 0;

err_ac:
	gpio_free(TEGRA_GPIO_VENTANA_DISABLE_CHARGER);
err_chg:
	return rc;
}

static void picasso_power_exit(struct device *dev)
{
	gpio_free(TEGRA_GPIO_VENTANA_DISABLE_CHARGER);
	gpio_free(TEGRA_GPIO_AC_ONLINE);
}

static struct pda_power_pdata picasso_power_data = {
	.init = picasso_power_init,
	.is_ac_online = picasso_is_ac_online,
	.set_charge = picasso_set_charge,
	.exit = picasso_power_exit,
	.supplied_to = picasso_batteries,
	.num_supplicants = ARRAY_SIZE(picasso_batteries),
};

static struct platform_device picasso_powerdev = {
	.name = "pda-power",
	.id = -1,
	.resource = picasso_power_resources,
	.num_resources = ARRAY_SIZE(picasso_power_resources),
	.dev = {
		.platform_data = &picasso_power_data,
	},
};

/******************************************************************************
 * Audio
 *****************************************************************************/
static struct tegra_wm8903_platform_data picasso_audio_pdata = {
	.gpio_spkr_en		= PICASSO_GPIO_SPK_AMP,
	.gpio_hp_mute		= -1,
	.gpio_hp_det		= PICASSO_GPIO_HP_DETECT,
	.gpio_int_mic_en	= PICASSO_GPIO_MIC_EN_INT,
	.gpio_ext_mic_en	= TEGRA_GPIO_VENTANA_EN_MIC_EXT,
};

static struct platform_device picasso_audio_device = {
	.name	= "tegra-snd-wm8903",
	.id	= 0,
	.dev	= {
		.platform_data  = &picasso_audio_pdata,
	},
};

static struct wm8903_platform_data picasso_wm8903_pdata = {
	.micdet_delay = 100,
	.gpio_base = PICASSO_WM8903_GPIO_BASE,
	.gpio_cfg = {
		WM8903_GPIO_NO_CONFIG,
		WM8903_GPIO_NO_CONFIG,
		WM8903_GPn_FN_GPIO_OUTPUT << WM8903_GP3_FN_SHIFT,
		WM8903_GPIO_NO_CONFIG,
		WM8903_GPIO_NO_CONFIG,
	},
};

static struct i2c_board_info __initdata wm8903_device = {
	I2C_BOARD_INFO("wm8903", 0x1a),
	.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_WM8903_IRQ),
	.platform_data = &picasso_wm8903_pdata,
};

static void __init picasso_sound_init(void) {
	i2c_register_board_info(0, &wm8903_device, 1);
	platform_device_register(&picasso_audio_device);
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
	.bus_clk_rate   = { 400000, 100000 },
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

static void __init picasso_i2c_init(void)
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
 * Sensors
 *****************************************************************************/
/*FIXME
 * al3000a_ls 0:0x1c PZ2
 * kxtf9 0:0xF PS7
 * mpu3050 4:0x68 PZ4
 */

static struct nct1008_platform_data ventana_nct1008_pdata = {
	.supported_hwrev = true,
	.ext_range = false,
	.conv_rate = 0x08,
	.offset = 0,
	.hysteresis = 0,
	.shutdown_ext_limit = 85,
	.shutdown_local_limit = 90,
	.throttling_ext_limit = 65,
	.alarm_fn = tegra_throttling_enable,
};

static struct i2c_board_info __initdata picasso_i2c4_board_info[] = {
	{
		I2C_BOARD_INFO("nct1008", 0x4C),
		.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_NCT1008_THERM2_IRQ),
		.platform_data = &ventana_nct1008_pdata,
	},
	{
		I2C_BOARD_INFO("ak8975", 0x0c),
		.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_MAGNETOMETER),
	},
};

static struct i2c_board_info __initdata picasso_ec = {
	I2C_BOARD_INFO(PICASSO_EC_ID, 0x58),
};

static struct i2c_board_info __initdata tf101_asusec[] = {
	{
		I2C_BOARD_INFO("asusec", 0x19),
		.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PS2),
	},
	{
		I2C_BOARD_INFO("bq20z45", 0x0B),
	},
};


static void __init picasso_sensors_init(void) {
	gpio_request(TEGRA_GPIO_NCT1008_THERM2_IRQ, "nct1008");
	gpio_direction_input(TEGRA_GPIO_NCT1008_THERM2_IRQ);
	
	if(machine_is_picasso())
		i2c_register_board_info(2, &picasso_ec, 1);

	if(machine_is_tf101())
		i2c_register_board_info(2, tf101_asusec, ARRAY_SIZE(tf101_asusec));

	i2c_register_board_info(4, picasso_i2c4_board_info,
		ARRAY_SIZE(picasso_i2c4_board_info));
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
		.desc = "Power Key",
		.type = EV_KEY,
		.wakeup = 1,
		.debounce_interval = 10,
	},
	{
		.code = KEY_POWER,
		.gpio = PICASSO_GPIO_KEY_POWER2,
		.desc = "Power Key 2",
		.type = EV_KEY,
		.debounce_interval = 10,
	},
	{
		.code = SW_RFKILL_ALL,
		.gpio = PICASSO_GPIO_SWITCH_LOCK,
		.desc = "Lock Switch",
		.type = EV_SW,
		.debounce_interval = 10,
	},
	{
		.code = SW_DOCK,
		.gpio = PICASSO_GPIO_SWITCH_DOCK,
		.desc = "Dock Switch",
		.type = EV_SW,
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

/******************************************************************************
 * Bluetooth rfkill
 *****************************************************************************/
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

/******************************************************************************
 * SDHC
 *****************************************************************************/
static struct tegra_sdhci_platform_data tegra_sdhci_platform_data1 = {
	.cd_gpio = -1,
	.wp_gpio = -1,
	.power_gpio = TEGRA_GPIO_WLAN_POWER,
	.pm_flags = MMC_PM_KEEP_POWER,
};

static struct tegra_sdhci_platform_data tegra_sdhci_platform_data3 = {
	.cd_gpio = TEGRA_GPIO_SD2_CD,
	.wp_gpio = -1,
	.power_gpio = TEGRA_GPIO_SD2_POWER,
};

static struct tegra_sdhci_platform_data tegra_sdhci_platform_data4 = {
	.cd_gpio = -1,
	.wp_gpio = -1,
	.power_gpio = -1,
	.is_8bit = 1,
};

/******************************************************************************
 * Suspend
 *****************************************************************************/
static struct tegra_suspend_platform_data picasso_suspend_data = {
	/*
	 * Check power on time and crystal oscillator start time
	 * for appropriate settings.
	 */
	.cpu_timer = 2000,
	.cpu_off_timer = 100,
	.suspend_mode = TEGRA_SUSPEND_LP0,
	.core_timer = 0x7e7e,
	.core_off_timer = 0xf,
	.separate_req = true,
	.corereq_high = false,
	.sysclkreq_high = true,
	.wake_enb =
		TEGRA_WAKE_GPIO_PV3 | TEGRA_WAKE_GPIO_PC7 | TEGRA_WAKE_USB1_VBUS |
		TEGRA_WAKE_GPIO_PV2 | TEGRA_WAKE_GPIO_PS0,
	.wake_high = TEGRA_WAKE_GPIO_PC7,
	.wake_low = TEGRA_WAKE_GPIO_PV2,
	.wake_any =
		TEGRA_WAKE_GPIO_PV3 | TEGRA_WAKE_USB1_VBUS | TEGRA_WAKE_GPIO_PS0,
};

static void __init picasso_suspend_init(void) {
	/* A03 (but not A03p) chips do not support LP0 */
	if (tegra_get_revision() == TEGRA_REVISION_A03)
		picasso_suspend_data.suspend_mode = TEGRA_SUSPEND_LP1;
	tegra_init_suspend(&picasso_suspend_data);
}

/******************************************************************************
 * Platform devices
 *****************************************************************************/
static struct platform_device *picasso_devices[] __initdata = {
	&debug_uart,
	&tegra_uartb_device,
	&tegra_uartc_device,
	&tegra_pmu_device,
	&tegra_gart_device,
	&tegra_aes_device,
	&tegra_avp_device,
	&picasso_keys_device,
	&tegra_i2s_device1,
	&tegra_das_device,
	&tegra_pcm_device,
	&tegra_sdhci_device4,
	&tegra_sdhci_device3,
	&tegra_sdhci_device1,
	&picasso_powerdev,
	&bt_rfkill_device
};

static void __init tegra_picasso_reserve(void)
{
	tegra_reserve(SZ_128M, SZ_8M, SZ_16M);
}

static void __init tegra_limit_wifi_clock(void) {
	/* Temporary hack to keep SDIO for wifi capped at 43.2MHz due to
	 * stability issues with brcmfmac at 48MHz.
	 */
	struct clk *c, *p;
	c = tegra_get_clock_by_name("sdmmc1");
	p = tegra_get_clock_by_name("pll_p");
	if (c && p) {
		clk_set_parent(c, p);
		clk_set_rate(c, 43200000);
		clk_enable(c);
	}
}

static void __init tegra_picasso_init(void)
{
	picasso_pinmux_init();
	tegra_clk_init_from_table(picasso_clk_init_table);
	
	tegra_sdhci_device1.dev.platform_data = &tegra_sdhci_platform_data1;
	tegra_sdhci_device3.dev.platform_data = &tegra_sdhci_platform_data3;
	tegra_sdhci_device4.dev.platform_data = &tegra_sdhci_platform_data4;
	picasso_suspend_init();

	platform_add_devices(picasso_devices, ARRAY_SIZE(picasso_devices));

	tegra_limit_wifi_clock();
	picasso_emc_init();
	picasso_i2c_init();
	picasso_sensors_init();
	picasso_regulator_init();
	picasso_usb_init();

	if(machine_is_picasso())
		picasso_panel_init();

	if(machine_is_tf101())
		tf101_panel_init();

	picasso_touch_init();
	picasso_sound_init();
}

MACHINE_START(PICASSO, "picasso")
	.boot_params    = 0x00000100,
	.map_io		= tegra_map_common_io,
	.init_early	= tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.reserve		= tegra_picasso_reserve,
	.init_machine	= tegra_picasso_init,
MACHINE_END

MACHINE_START(TF101, "tf101")
	.boot_params    = 0x00000100,
	.map_io		= tegra_map_common_io,
	.init_early	= tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.reserve		= tegra_picasso_reserve,
	.init_machine	= tegra_picasso_init,
MACHINE_END
