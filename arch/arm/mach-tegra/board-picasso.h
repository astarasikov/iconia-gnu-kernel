/*
 * arch/arm/mach-tegra/board-picasso.h
 *
 * Copyright (C) 2011 Google, Inc.
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

#ifndef _MACH_TEGRA_BOARD_PICASSO_H
#define _MACH_TEGRA_BOARD_PICASSO_H

int picasso_charge_init(void);
int picasso_regulator_init(void);
int picasso_pinmux_init(void);
int picasso_panel_init(void);
int picasso_wired_jack_init(void);
int picasso_sensors_init(void);
int picasso_kbc_init(void);
int picasso_emc_init(void);

#define PICASSO_GPIO_KEY_nVOLUMEUP	TEGRA_GPIO_PQ4
#define PICASSO_GPIO_KEY_nVOLUMEDOWN	TEGRA_GPIO_PQ5
#define PICASSO_GPIO_KEY_POWER	TEGRA_GPIO_PC7
#define PICASSO_GPIO_KEY_POWER2	TEGRA_GPIO_PI3
#define PICASSO_GPIO_SWITCH_LOCK	TEGRA_GPIO_PQ2
#define PICASSO_GPIO_SWITCH_DOCK	TEGRA_GPIO_PR0

#define PICASSO_GPIO_ULPI_RESET	TEGRA_GPIO_PG2
#define PICASSO_GPIO_TS_IRQ	TEGRA_GPIO_PV6
#define PICASSO_GPIO_TS_RESET	TEGRA_GPIO_PQ7

#define PICASSO_GPIO_PNL_ENABLE	TEGRA_GPIO_PC6
#define PICASSO_GPIO_BL_ENABLE	TEGRA_GPIO_PD4
#define PICASSO_GPIO_LVDS_SHUTDOWN	TEGRA_GPIO_PB2
#define PICASSO_GPIO_HDMI_HPD	TEGRA_GPIO_PN7

#define PICASSO_GPIO_SDHCI2_CD	TEGRA_GPIO_PI5
#define PICASSO_GPIO_SDHCI2_PWR	TEGRA_GPIO_PI6

#define PICASSO_GPIO_CHARGE_DISABLE	TEGRA_GPIO_PR6

#define PICASSO_GPIO_HP_DETECT	TEGRA_GPIO_PW2
#define PICASSO_GPIO_HP_DET_DOCK	TEGRA_GPIO_PX6
#define PICASSO_GPIO_MIC_EN_EXT	TEGRA_GPIO_PX1
#define PICASSO_GPIO_MIC_EN_INT	TEGRA_GPIO_PX0

#define PICASSO_GPIO_SIM_DETECT	TEGRA_GPIO_PI7
#define PICASSO_GPIO_GPS	TEGRA_GPIO_PZ3
#define PICASSO_GPIO_VIBRATOR	TEGRA_GPIO_PV5

/* TPS6586X gpios */
#define TPS6586X_GPIO_BASE	TEGRA_NR_GPIOS
#define AVDD_DSI_CSI_ENB_GPIO	(TPS6586X_GPIO_BASE + 1) /* gpio2 */

/* TCA6416 gpios */
#define TCA6416_GPIO_BASE	(TEGRA_NR_GPIOS + 4)
#define CAM1_PWR_DN_GPIO	(TCA6416_GPIO_BASE + 0) /* gpio0 */
#define CAM1_RST_L_GPIO		(TCA6416_GPIO_BASE + 1) /* gpio1 */
#define CAM1_AF_PWR_DN_L_GPIO	(TCA6416_GPIO_BASE + 2) /* gpio2 */
#define CAM1_LDO_SHUTDN_L_GPIO	(TCA6416_GPIO_BASE + 3) /* gpio3 */
#define CAM2_PWR_DN_GPIO	(TCA6416_GPIO_BASE + 4) /* gpio4 */
#define CAM2_RST_L_GPIO		(TCA6416_GPIO_BASE + 5) /* gpio5 */
#define CAM2_AF_PWR_DN_L_GPIO	(TCA6416_GPIO_BASE + 6) /* gpio6 */
#define CAM2_LDO_SHUTDN_L_GPIO	(TCA6416_GPIO_BASE + 7) /* gpio7 */
#define CAM3_PWR_DN_GPIO	(TCA6416_GPIO_BASE + 8) /* gpio8 */
#define CAM3_RST_L_GPIO		(TCA6416_GPIO_BASE + 9) /* gpio9 */
#define CAM3_AF_PWR_DN_L_GPIO	(TCA6416_GPIO_BASE + 10) /* gpio10 */
#define CAM3_LDO_SHUTDN_L_GPIO	(TCA6416_GPIO_BASE + 11) /* gpio11 */
#define CAM_I2C_MUX_RST_GPIO	(TCA6416_GPIO_BASE + 15) /* gpio15 */

#endif
