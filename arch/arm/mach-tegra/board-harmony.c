/*
 * arch/arm/mach-tegra/board-harmony.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (C) 2011 NVIDIA, Inc.
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/clk.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/dma-mapping.h>
#include <linux/fsl_devices.h>
#include <linux/pda_power.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/i2c.h>
#include <linux/i2c-tegra.h>
#include <linux/platform_data/tegra_usb.h>

#include <sound/wm8903.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>
#include <asm/delay.h>

#include <mach/harmony_audio.h>
#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/sdhci.h>
#include <mach/nand.h>
#include <mach/pinmux.h>
#include <mach/pinmux-t2.h>
#include <mach/suspend.h>
#include <mach/usb_phy.h>

#include "board.h"
#include "board-harmony.h"
#include "clock.h"
#include "devices.h"
#include "gpio-names.h"

static struct tegra_utmip_config utmi_phy_config = {
	.hssync_start_delay = 0,
	.idle_wait_delay = 17,
	.elastic_limit = 16,
	.term_range_adj = 6,
	.xcvr_setup = 9,
	.xcvr_lsfslew = 2,
	.xcvr_lsrslew = 2,
};

static struct tegra_ehci_platform_data tegra_ehci_pdata = {
	.phy_config = &utmi_phy_config,
	.operating_mode = TEGRA_USB_HOST,
	.power_down_on_bus_suspend = 1,
};

static struct tegra_nand_chip_parms nand_chip_parms[] = {
	/* Samsung K5E2G1GACM */
	[0] = {
		.vendor_id   = 0xEC,
		.device_id   = 0xAA,
		.capacity    = 256,
		.timing      = {
			.trp		= 21,
			.trh		= 15,
			.twp		= 21,
			.twh		= 15,
			.tcs		= 31,
			.twhr		= 60,
			.tcr_tar_trr	= 20,
			.twb		= 100,
			.trp_resp	= 30,
			.tadl		= 100,
		},
	},
	/* Hynix H5PS1GB3EFR */
	[1] = {
		.vendor_id   = 0xAD,
		.device_id   = 0xDC,
		.capacity    = 512,
		.timing      = {
			.trp		= 12,
			.trh		= 10,
			.twp		= 12,
			.twh		= 10,
			.tcs		= 20,
			.twhr		= 80,
			.tcr_tar_trr	= 20,
			.twb		= 100,
			.trp_resp	= 20,
			.tadl		= 70,
		},
	},
};

/* Current layout is:
 *
 * BCT @ 0 (0x300000)        -- boot config table
 * PT  @ 0x300000 (0x1000)   -- partition table
 * EBT @ 0x301000 (0x100000) -- bootloader
 * BMP @ 0x401000 (0x148c)   -- rgb565 bitmap
 * WAV @ 0x40248c (0x2a000)  -- wav audio clip
 * ARG @ 0x42c48c (0x800)    -- ??
 * DRM @ 0x42cc8c (0x19000)  -- bleh?
 * UIP @ 0x445c8c (0x800)    -- update information partition
 * USP @ 0x44648c (0x600000) -- update staging partition
 * USR @ 0xa4648c (THE REST) -- <available>
 *
 * What we will do is we will actually just skip the first 16MB, and just
 * mark it as vendor, and then layout our partitions.
 *
 * so:
 *
 *
 */
static struct mtd_partition harmony_nand_partitions[] = {
	[0] = {
		.name		= "recovery",
		.offset		= 0x1b80*0x800,
		.size		= 0xa00*0x800,
		.mask_flags	= MTD_WRITEABLE, /* r/o */
	},
	[1] = {
		.name		= "boot",
		.offset		= 0x2680*0x800,
		.size		= 0x1000*0x800,
	},
	[2] = {
		.name		= "system",
		.offset		= 0x3780*0x800,
		.size		= 0xef40*0x800,
	},
	[3] = {
		.name		= "cache",
		.offset		= 0x127c0*0x800,
		.size		= 0x4000*0x800,
	},
	[4] = {
		.name		= "userdata",
		.offset		= 0x168c0*0x800,
		.size		= 0x29640*0x800,
	},
};

struct tegra_nand_platform harmony_nand_data = {
	.max_chips	= 8,
	.chip_parms	= nand_chip_parms,
	.nr_chip_parms  = ARRAY_SIZE(nand_chip_parms),
	.parts		= harmony_nand_partitions,
	.nr_parts	= ARRAY_SIZE(harmony_nand_partitions),
};

static struct resource resources_nand[] = {
	[0] = {
		.start  = INT_NANDFLASH,
		.end    = INT_NANDFLASH,
		.flags  = IORESOURCE_IRQ,
	},
};

struct platform_device tegra_nand_device = {
	.name           = "tegra_nand",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(resources_nand),
	.resource       = resources_nand,
	.dev            = {
		.platform_data = &harmony_nand_data,
	},
};

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
		.flags		= 0
	}
};

static struct platform_device debug_uart = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM,
	.dev = {
		.platform_data = debug_uart_platform_data,
	},
};

static struct harmony_audio_platform_data harmony_audio_pdata = {
	.gpio_spkr_en		= TEGRA_GPIO_SPKR_EN,
	.gpio_hp_det		= TEGRA_GPIO_HP_DET,
	.gpio_int_mic_en	= TEGRA_GPIO_INT_MIC_EN,
	.gpio_ext_mic_en	= TEGRA_GPIO_EXT_MIC_EN,
};

static struct platform_device harmony_audio_device = {
	.name	= "tegra-snd-harmony",
	.id	= 0,
	.dev	= {
		.platform_data  = &harmony_audio_pdata,
	},
};

static struct gpio_keys_button harmony_gpio_keys_buttons[] = {
	{
		.code		= KEY_POWER,
		.gpio		= TEGRA_GPIO_POWERKEY,
		.active_low	= 1,
		.desc		= "Power",
		.type		= EV_KEY,
		.wakeup		= 1,
	},
};

static struct gpio_keys_platform_data harmony_gpio_keys = {
	.buttons	= harmony_gpio_keys_buttons,
	.nbuttons	= ARRAY_SIZE(harmony_gpio_keys_buttons),
};

static struct platform_device harmony_gpio_keys_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.dev		= {
		.platform_data = &harmony_gpio_keys,
	}
};

/* PDA power */
static struct pda_power_pdata pda_power_pdata = {
};

static struct platform_device pda_power_device = {
	.name   = "pda_power",
	.id     = -1,
	.dev    = {
		.platform_data  = &pda_power_pdata,
	},
};

static struct tegra_i2c_platform_data harmony_i2c1_platform_data = {
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

static struct tegra_i2c_platform_data harmony_i2c2_platform_data = {
        .adapter_nr     = 1,
        .bus_count      = 2,
        .bus_clk_rate   = { 400000, 100000 },
        .bus_mux        = { &i2c2_ddc, &i2c2_gen2 },
        .bus_mux_len    = { 1, 1 },
};

static struct tegra_i2c_platform_data harmony_i2c3_platform_data = {
        .adapter_nr     = 3,
        .bus_count      = 1,
        .bus_clk_rate   = { 400000, 0 },
};

static struct tegra_i2c_platform_data harmony_dvc_platform_data = {
        .adapter_nr     = 4,
        .bus_count      = 1,
        .bus_clk_rate   = { 400000, 0 },
        .is_dvc         = true,
};

static struct wm8903_platform_data harmony_wm8903_pdata = {
	.irq_active_low = 0,
	.micdet_cfg = 0,
	.micdet_delay = 100,
	.gpio_base = HARMONY_GPIO_WM8903(0),
	.gpio_cfg = {
		WM8903_GPIO_NO_CONFIG,
		WM8903_GPIO_NO_CONFIG,
		0,
		WM8903_GPIO_NO_CONFIG,
		WM8903_GPIO_NO_CONFIG,
	},
};

static struct i2c_board_info __initdata wm8903_board_info = {
	I2C_BOARD_INFO("wm8903", 0x1a),
	.platform_data = &harmony_wm8903_pdata,
	.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_CDC_IRQ),
};

static void __init harmony_i2c_init(void)
{
	tegra_i2c_device1.dev.platform_data = &harmony_i2c1_platform_data;
	tegra_i2c_device2.dev.platform_data = &harmony_i2c2_platform_data;
	tegra_i2c_device3.dev.platform_data = &harmony_i2c3_platform_data;
	tegra_i2c_device4.dev.platform_data = &harmony_dvc_platform_data;

	platform_device_register(&tegra_i2c_device1);
	platform_device_register(&tegra_i2c_device2);
	platform_device_register(&tegra_i2c_device3);
	platform_device_register(&tegra_i2c_device4);

	gpio_request(TEGRA_GPIO_CDC_IRQ, "wm8903");
	gpio_direction_input(TEGRA_GPIO_CDC_IRQ);

	i2c_register_board_info(0, &wm8903_board_info, 1);
}

static struct platform_device *harmony_devices[] __initdata = {
	&debug_uart,
	&tegra_pmu_device,
	&tegra_nand_device,
	&tegra_gart_device,
	&pda_power_device,
	&tegra_sdhci_device1,
	&tegra_sdhci_device2,
	&tegra_sdhci_device4,
	&harmony_gpio_keys_device,
	&tegra_ehci3_device,
	&tegra_i2s_device1,
	&tegra_das_device,
	&tegra_pcm_device,
	&harmony_audio_device,
	&tegra_avp_device,
};

static void __init tegra_harmony_fixup(struct machine_desc *desc,
	struct tag *tags, char **cmdline, struct meminfo *mi)
{
	mi->nr_banks = 2;
	mi->bank[0].start = PHYS_OFFSET;
	mi->bank[0].size = 448 * SZ_1M;
	mi->bank[1].start = SZ_512M;
	mi->bank[1].size = SZ_512M;
}


static __initdata struct tegra_clk_init_table harmony_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "clk_dev1",	NULL,		26000000,	true},
	{ "clk_m",	NULL,		12000000,	true},
	{ "3d",		"pll_m",	266400000,	true},
	{ "2d",		"pll_m",	266400000,	true},
	{ "vi",		"pll_m",	50000000,	true},
	{ "vi_sensor",	"pll_m",	111000000,	false},
	{ "epp",	"pll_m",	266400000,	true},
	{ "mpe",	"pll_m",	111000000,	false},
	{ "emc",	"pll_m",	666000000,	true},
	{ "pll_c",	"clk_m",	600000000,	true},
	{ "pll_c_out1",	"pll_c",	240000000,	true},
	{ "vde",	"pll_c",	240000000,	false},
	{ "pll_p",	"clk_m",	216000000,	true},
	{ "pll_p_out1",	"pll_p",	28800000,	true},
	{ "pll_a",	"pll_p_out1",	56448000,	true},
	{ "pll_a_out0",	"pll_a",	11289600,	true},
	{ "cdev1",	"pll_a_out0",	11289600,	true},
	{ "i2s1",	"pll_a_out0",	11289600,	false},
	{ "audio",	"pll_a_out0",	11289600,	false},
	{ "audio_2x",	"audio",	22579200,	false},
	{ "pll_p_out2",	"pll_p",	48000000,	true},
	{ "pll_p_out3",	"pll_p",	72000000,	true},
	{ "i2c1_i2c",	"pll_p_out3",	72000000,	true},
	{ "i2c2_i2c",	"pll_p_out3",	72000000,	true},
	{ "i2c3_i2c",	"pll_p_out3",	72000000,	true},
	{ "dvc_i2c",	"pll_p_out3",	72000000,	true},
	{ "csi",	"pll_p_out3",	72000000,	false},
	{ "pll_p_out4",	"pll_p",	108000000,	true},
	{ "sclk",	"pll_p_out4",	108000000,	true},
	{ "hclk",	"sclk",		108000000,	true},
	{ "pclk",	"hclk",		54000000,	true},
	{ "apbdma",	"hclk",		54000000,	true},
	{ "spdif_in",	"pll_p",	36000000,	false},
	{ "csite",	"pll_p",	144000000,	true},
	{ "uartd",	"pll_p",	216000000,	true},
	{ "host1x",	"pll_p",	144000000,	true},
	{ "disp1",	"pll_p",	216000000,	true},
	{ "pll_d",	"clk_m",	1000000,	false},
	{ "pll_d_out0",	"pll_d",	500000,		false},
	{ "dsi",	"pll_d",	1000000,	false},
	{ "pll_u",	"clk_m",	480000000,	true},
	{ "clk_d",	"clk_m",	24000000,	true},
	{ "timer",	"clk_m",	12000000,	true},
	{ "i2s2",	"clk_m",	11289600,	false},
	{ "spdif_out",	"clk_m",	12000000,	false},
	{ "spi",	"clk_m",	12000000,	false},
	{ "xio",	"clk_m",	12000000,	false},
	{ "twc",	"clk_m",	12000000,	false},
	{ "sbc1",	"clk_m",	12000000,	false},
	{ "sbc2",	"clk_m",	12000000,	false},
	{ "sbc3",	"clk_m",	12000000,	false},
	{ "sbc4",	"clk_m",	12000000,	false},
	{ "ide",	"clk_m",	12000000,	false},
	{ "ndflash",	"clk_m",	108000000,	true},
	{ "vfir",	"clk_m",	12000000,	false},
	{ "la",		"clk_m",	12000000,	false},
	{ "owr",	"clk_m",	12000000,	false},
	{ "nor",	"clk_m",	12000000,	false},
	{ "mipi",	"clk_m",	12000000,	false},
	{ "i2c1",	"clk_m",	3000000,	false},
	{ "i2c2",	"clk_m",	3000000,	false},
	{ "i2c3",	"clk_m",	3000000,	false},
	{ "dvc",	"clk_m",	3000000,	false},
	{ "uarta",	"clk_m",	12000000,	false},
	{ "uartb",	"clk_m",	12000000,	false},
	{ "uartc",	"clk_m",	12000000,	false},
	{ "uarte",	"clk_m",	12000000,	false},
	{ "cve",	"clk_m",	12000000,	false},
	{ "tvo",	"clk_m",	12000000,	false},
	{ "hdmi",	"clk_m",	12000000,	false},
	{ "tvdac",	"clk_m",	12000000,	false},
	{ "disp2",	"clk_m",	12000000,	false},
	{ "usbd",	"clk_m",	12000000,	false},
	{ "usb2",	"clk_m",	12000000,	false},
	{ "usb3",	"clk_m",	12000000,	true},
	{ "isp",	"clk_m",	12000000,	false},
	{ "csus",	"clk_m",	12000000,	false},
	{ "pwm",	"clk_32k",	32768,		false},
	{ "clk_32k",	NULL,		32768,		true},
	{ "pll_s",	"clk_32k",	32768,		false},
	{ "rtc",	"clk_32k",	32768,		true},
	{ "kbc",	"clk_32k",	32768,		true},
	{ NULL,		NULL,		0,		0},
};

static struct tegra_sdhci_platform_data sdhci_pdata1 = {
	.cd_gpio	= -1,
	.wp_gpio	= -1,
	.power_gpio	= -1,
};

static struct tegra_sdhci_platform_data sdhci_pdata2 = {
	.cd_gpio	= TEGRA_GPIO_SD2_CD,
	.wp_gpio	= TEGRA_GPIO_SD2_WP,
	.power_gpio	= TEGRA_GPIO_SD2_POWER,
};

static struct tegra_sdhci_platform_data sdhci_pdata4 = {
	.cd_gpio	= TEGRA_GPIO_SD4_CD,
	.wp_gpio	= TEGRA_GPIO_SD4_WP,
	.power_gpio	= TEGRA_GPIO_SD4_POWER,
	.is_8bit	= 1,
};

static struct tegra_suspend_platform_data harmony_suspend = {
	.cpu_timer = 5000,
	.cpu_off_timer = 5000,
	.core_timer = 0x7e7e,
	.core_off_timer = 0x7f,
	.separate_req = true,
	.corereq_high = false,
	.sysclkreq_high = true,
	.suspend_mode = TEGRA_SUSPEND_LP0,
};

static int __init harmony_wifi_init(void)
{
	int gpio_pwr, gpio_rst;

	/* WLAN - Power up (low) and Reset (low) */
	gpio_pwr = gpio_request(TEGRA_GPIO_WLAN_PWR_LOW, "wlan_pwr");
	gpio_rst = gpio_request(TEGRA_GPIO_WLAN_RST_LOW, "wlan_rst");
	if (gpio_pwr < 0 || gpio_rst < 0)
		pr_warning("Unable to get gpio for WLAN Power and Reset\n");
	else {
		/* toggle in this order as per spec */
		gpio_direction_output(TEGRA_GPIO_WLAN_PWR_LOW, 0);
		gpio_direction_output(TEGRA_GPIO_WLAN_RST_LOW, 0);
		udelay(5);
		gpio_direction_output(TEGRA_GPIO_WLAN_PWR_LOW, 1);
		gpio_direction_output(TEGRA_GPIO_WLAN_RST_LOW, 1);
	}

	return 0;
}
/* make harmony_wifi_init to be invoked at subsys_initcall_sync
 * to ensure the required regulators (LDO3 supply of external
 * PMU and 1.2V regulator) are properly enabled, and mmc driver
 * has not yet probed for a device on SDIO bus
 */
subsys_initcall_sync(harmony_wifi_init);

static void __init tegra_harmony_init(void)
{
	tegra_init_suspend(&harmony_suspend);

	harmony_pinmux_init();

	tegra_clk_init_from_table(harmony_clk_init_table);

	tegra_sdhci_device1.dev.platform_data = &sdhci_pdata1;
	tegra_sdhci_device2.dev.platform_data = &sdhci_pdata2;
	tegra_sdhci_device4.dev.platform_data = &sdhci_pdata4;

	tegra_ehci3_device.dev.platform_data = &tegra_ehci_pdata;

	platform_add_devices(harmony_devices, ARRAY_SIZE(harmony_devices));
	harmony_power_init();
	harmony_panel_init();
	harmony_i2c_init();
}

MACHINE_START(HARMONY, "harmony")
	.boot_params  = 0x00000100,
	.fixup		= tegra_harmony_fixup,
	.map_io         = tegra_map_common_io,
	.init_early	= tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine   = tegra_harmony_init,
MACHINE_END
