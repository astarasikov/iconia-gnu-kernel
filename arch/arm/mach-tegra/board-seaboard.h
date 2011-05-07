/*
 * arch/arm/mach-tegra/board-seaboard.h
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

#ifndef _MACH_TEGRA_BOARD_SEABOARD_H
#define _MACH_TEGRA_BOARD_SEABOARD_H

#define TPS_GPIO_BASE			TEGRA_NR_GPIOS

#define TEGRA_CAMERA_GPIO_PMU		(TPS_GPIO_BASE + 1)
#define TPS_GPIO_WWAN_PWR		(TPS_GPIO_BASE + 2)

#define SEABOARD_GPIO_WM8903(_x_)	(TPS_GPIO_BASE + 4 + (_x_))

#define TEGRA_GPIO_SD2_CD		TEGRA_GPIO_PI5
#define TEGRA_GPIO_SD2_WP		TEGRA_GPIO_PH1
#define TEGRA_GPIO_SD2_POWER		TEGRA_GPIO_PI6

#define TEGRA_GPIO_LIDSWITCH		TEGRA_GPIO_PC7
#define TEGRA_GPIO_USB1			TEGRA_GPIO_PD0
#define TEGRA_GPIO_POWERKEY		TEGRA_GPIO_PV2
#define TEGRA_GPIO_BACKLIGHT		TEGRA_GPIO_PD4
#define TEGRA_GPIO_LVDS_SHUTDOWN	TEGRA_GPIO_PB2
#define TEGRA_GPIO_BACKLIGHT_PWM	TEGRA_GPIO_PU5
#define TEGRA_GPIO_BACKLIGHT_VDD	TEGRA_GPIO_PW0
#define TEGRA_GPIO_EN_VDD_PNL		TEGRA_GPIO_PC6
#define TEGRA_GPIO_MAGNETOMETER		TEGRA_GPIO_PN5
#define TEGRA_GPIO_NCT1008_THERM2_IRQ	TEGRA_GPIO_PN6
#define TEGRA_GPIO_HDMI_HPD		TEGRA_GPIO_PN7
#define TEGRA_GPIO_ISL29018_IRQ		TEGRA_GPIO_PZ2
#define TEGRA_GPIO_MPU3050_IRQ		TEGRA_GPIO_PZ4
#define TEGRA_GPIO_AC_ONLINE		TEGRA_GPIO_PV3
#define TEGRA_GPIO_BATT_DETECT          TEGRA_GPIO_PP2
#define TEGRA_GPIO_WLAN_POWER		TEGRA_GPIO_PK6
#define TEGRA_GPIO_HP_DET		TEGRA_GPIO_PX1
#define TEGRA_GPIO_DISABLE_CHARGER	TEGRA_GPIO_PX2
#define TEGRA_GPIO_WM8903_IRQ		TEGRA_GPIO_PX3
#define TEGRA_GPIO_CYTP_INT		TEGRA_GPIO_PW2
#define TEGRA_GPIO_MXT_RST		TEGRA_GPIO_PV7
#define TEGRA_GPIO_MXT_IRQ		TEGRA_GPIO_PV6
#define TEGRA_GPIO_HDMI_ENB		TEGRA_GPIO_PV5
#define TEGRA_GPIO_KAEN_HP_MUTE		TEGRA_GPIO_PA5
#define TEGRA_GPIO_BT_RESET		TEGRA_GPIO_PU0
#define TEGRA_GPIO_RECOVERY_SWITCH	TEGRA_GPIO_PH0
#define TEGRA_GPIO_DEV_SWITCH		TEGRA_GPIO_PV0
#define TEGRA_GPIO_WP_STATUS		TEGRA_GPIO_PH3

#define TPS_GPIO_BASE			TEGRA_NR_GPIOS

#define TPS_GPIO_WWAN_PWR		(TPS_GPIO_BASE + 2)

#define TEGRA_GPIO_SPKR_EN		SEABOARD_GPIO_WM8903(2)

#define GPIO_WM8903(_x_)                (TPS_GPIO_BASE + 4 + (_x_))

void seaboard_pinmux_init(void);
int seaboard_panel_init(void);
int seaboard_power_init(void);
int seaboard_sensors_init(void);
void seaboard_emc_init(void);

#ifdef CONFIG_MACH_KAEN
int kaen_sensors_init(void);
void kaen_emc_init(void);
#else
static inline int kaen_sensors_init(void) { return 0; }
static inline void kaen_emc_init(void) { return; }
#endif

#ifdef CONFIG_MACH_AEBL
int aebl_sensors_init(void);
void aebl_emc_init(void);
#else
static inline int aebl_sensors_init(void) { return 0; }
static inline void aebl_emc_init(void) { return; }
#endif

#ifdef CONFIG_MACH_ARTHUR
void arthur_emc_init(void);
#else
static inline void arthur_emc_init(void) { return; }
#endif

#endif
