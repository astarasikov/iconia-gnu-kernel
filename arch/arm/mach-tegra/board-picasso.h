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

//include common ventana gpios
#include "board-seaboard.h"

int picasso_regulator_init(void);
void picasso_pinmux_init(void);
int picasso_panel_init(void);
int tf101_panel_init(void);
int picasso_kbc_init(void);
int picasso_emc_init(void);

#define PICASSO_GPIO_ULPI_RESET	TEGRA_GPIO_PG2

#define PICASSO_GPIO_KEY_POWER	TEGRA_GPIO_PC7
#define PICASSO_GPIO_KEY_POWER2	TEGRA_GPIO_PI3
#define PICASSO_GPIO_SIM_DETECT	TEGRA_GPIO_PI7
#define PICASSO_GPIO_SWITCH_LOCK	TEGRA_GPIO_PQ2
#define PICASSO_GPIO_KEY_nVOLUMEUP	TEGRA_GPIO_PQ4
#define PICASSO_GPIO_KEY_nVOLUMEDOWN	TEGRA_GPIO_PQ5
#define PICASSO_GPIO_SWITCH_DOCK	TEGRA_GPIO_PR0
#define PICASSO_GPIO_KXTF9_IRQ	TEGRA_GPIO_PS7
#define PICASSO_GPIO_BT_EXT_WAKE	TEGRA_GPIO_PU1
#define PICASSO_GPIO_BT_HOST_WAKE	TEGRA_GPIO_PU6
#define PICASSO_GPIO_VIBRATOR	TEGRA_GPIO_PV5
#define PICASSO_GPIO_HP_DETECT	TEGRA_GPIO_PW2
#define PICASSO_GPIO_MIC_EN_INT	TEGRA_GPIO_PX0
#define PICASSO_GPIO_HP_DET_DOCK	TEGRA_GPIO_PX6
#define PICASSO_GPIO_AL3000A_IRQ	TEGRA_GPIO_PZ2
#define PICASSO_GPIO_GPS	TEGRA_GPIO_PZ3
#define PICASSO_GPIO_MPU3050_IRQ	TEGRA_GPIO_PZ4

/* TPS6586X gpios */
#define	PICASSO_TPS6586X_GPIO_BASE	TEGRA_NR_GPIOS
#define AVDD_DSI_CSI_ENB_GPIO	(PICASSO_TPS6586X_GPIO_BASE + 1) /* gpio2 */

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

#define PICASSO_WM8903_GPIO_BASE (TCA6416_GPIO_BASE + 16)
#define PICASSO_GPIO_SPK_AMP (PICASSO_WM8903_GPIO_BASE + 2)

#endif
