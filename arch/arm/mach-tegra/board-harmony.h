/*
 * arch/arm/mach-tegra/board-harmony.h
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

#ifndef _MACH_TEGRA_BOARD_HARMONY_H
#define _MACH_TEGRA_BOARD_HARMONY_H

#define HARMONY_GPIO_TPS6586X(_x_)	(TEGRA_NR_GPIOS + (_x_))
#define HARMONY_GPIO_WM8903(_x_)	(HARMONY_GPIO_TPS6586X(4) + (_x_))

/* fixed voltage regulator enable/mode gpios */
#define TPS_GPIO_EN_1V5			(HARMONY_GPIO_TPS6586X(0))
#define TPS_GPIO_EN_1V2			(HARMONY_GPIO_TPS6586X(1))
#define TPS_GPIO_EN_1V05		(HARMONY_GPIO_TPS6586X(2))
#define TPS_GPIO_MODE_1V05		(HARMONY_GPIO_TPS6586X(3))

#define TEGRA_GPIO_SD2_CD		TEGRA_GPIO_PI5
#define TEGRA_GPIO_SD2_WP		TEGRA_GPIO_PH1
#define TEGRA_GPIO_SD2_POWER		TEGRA_GPIO_PT3
#define TEGRA_GPIO_SD4_CD		TEGRA_GPIO_PH2
#define TEGRA_GPIO_SD4_WP		TEGRA_GPIO_PH3
#define TEGRA_GPIO_SD4_POWER		TEGRA_GPIO_PI6
#define TEGRA_GPIO_CDC_IRQ		TEGRA_GPIO_PX3
#define TEGRA_GPIO_SPKR_EN		HARMONY_GPIO_WM8903(2)
#define TEGRA_GPIO_HP_DET		TEGRA_GPIO_PW2
#define TEGRA_GPIO_INT_MIC_EN		TEGRA_GPIO_PX0
#define TEGRA_GPIO_EXT_MIC_EN		TEGRA_GPIO_PX1

#define TEGRA_GPIO_BACKLIGHT		TEGRA_GPIO_PB5
#define TEGRA_GPIO_LVDS_SHUTDOWN	TEGRA_GPIO_PB2
#define TEGRA_GPIO_BACKLIGHT_PWM	TEGRA_GPIO_PB4
#define TEGRA_GPIO_BACKLIGHT_VDD	TEGRA_GPIO_PW0
#define TEGRA_GPIO_EN_VDD_PNL		TEGRA_GPIO_PC6
#define TEGRA_GPIO_HDMI_HPD		TEGRA_GPIO_PN7

#define TEGRA_GPIO_POWERKEY		TEGRA_GPIO_PV2

/* WLAN pwr and reset gpio */
#define TEGRA_GPIO_WLAN_PWR_LOW		TEGRA_GPIO_PK5
#define TEGRA_GPIO_WLAN_RST_LOW		TEGRA_GPIO_PK6

void harmony_pinmux_init(void);
int harmony_panel_init(void);
int harmony_power_init(void);

#endif
