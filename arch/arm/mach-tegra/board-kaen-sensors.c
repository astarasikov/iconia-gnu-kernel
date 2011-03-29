/*
 * Copyright (C) 2011 NVIDIA Corporation.
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

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include <media/soc_camera.h>
#include <media/tegra_v4l2_camera.h>

#include <mach/gpio.h>
#include <mach/iomap.h>
#include <mach/nvhost.h>

#include "gpio-names.h"
#include "devices.h"

/* I2C adapter ID for the camera board. */
#define TEGRA_CAMERA_I2C_ADAPTER_ID	3

/* GPIOs relevant to camera module. */
#define TEGRA_CAMERA_GPIO_CAM_PWR_EN	TEGRA_GPIO_PV4
#define TEGRA_CAMERA_GPIO_CAM_RST	TEGRA_GPIO_PU2
#define TEGRA_CAMERA_GPIO_CAM_PWDN	TEGRA_GPIO_PU3

static struct regulator *regulator;
static struct clk *clk_vi;
static struct clk *clk_vi_sensor;
static struct clk *clk_csi;
static struct clk *clk_isp;
static struct clk *clk_csus;

static int tegra_camera_enable(struct nvhost_device *ndev)
{
	int err;

	/* Turn on relevant clocks. */
	clk_enable(clk_vi);
	clk_enable(clk_vi_sensor);
	clk_enable(clk_csi);
	clk_enable(clk_isp);
	clk_enable(clk_csus);

	/* Turn on power to the camera board. */
	regulator = regulator_get(&ndev->dev, "vddio_vi");
	if (IS_ERR(regulator)) {
		dev_info(&ndev->dev, "regulator_get() returned error %ld\n",
			 PTR_ERR(regulator));
		err = PTR_ERR(regulator);
		goto exit;
	}

	err = regulator_enable(regulator);
	if (err != 0)
		goto exit_regulator_put;

	/* Set up GPIOs. */
	gpio_set_value(TEGRA_CAMERA_GPIO_CAM_PWR_EN, 1);
	gpio_set_value(TEGRA_CAMERA_GPIO_CAM_RST, 1);
	gpio_set_value(TEGRA_CAMERA_GPIO_CAM_PWDN, 0);

	/* Give the sensor time to come out of reset.  The OV9740 needs
	 * 8192 clock cycles (from vi_sensor clock) before the first I2C
	 * transaction.
	 */
	udelay(1000);

	return 0;

exit_regulator_put:
	regulator_put(regulator);
exit:
	return err;
}

static void tegra_camera_disable(struct nvhost_device *ndev)
{
	gpio_set_value(TEGRA_CAMERA_GPIO_CAM_PWDN, 1);
	gpio_set_value(TEGRA_CAMERA_GPIO_CAM_RST, 0);
	gpio_set_value(TEGRA_CAMERA_GPIO_CAM_PWR_EN, 0);

	BUG_ON(!regulator);
	regulator_disable(regulator);
	regulator_put(regulator);
	regulator = NULL;

	/* Turn off relevant clocks. */
	clk_disable(clk_vi);
	clk_disable(clk_vi_sensor);
	clk_disable(clk_csi);
	clk_disable(clk_isp);
	clk_disable(clk_csus);
}

static struct i2c_board_info kaen_i2c_bus3_sensor_info = {
	I2C_BOARD_INFO("ov9740", 0x10),
};

static struct soc_camera_link ov9740_iclink = {
	.bus_id		= 0,
	.i2c_adapter_id	= TEGRA_CAMERA_I2C_ADAPTER_ID,
	.board_info	= &kaen_i2c_bus3_sensor_info,
	.module_name	= "ov9740",
};

static struct platform_device soc_camera = {
	.name	= "soc-camera-pdrv",
	.id	= 0,
	.dev	= {
		.platform_data = &ov9740_iclink,
	},
};

static struct tegra_camera_platform_data tegra_camera_platform_data = {
	.enable_camera		= tegra_camera_enable,
	.disable_camera		= tegra_camera_disable,
	.flip_v			= 0,
	.flip_h			= 0,
};

int __init kaen_sensors_init(void)
{
	int err;

	tegra_camera_device.dev.platform_data = &tegra_camera_platform_data;

	tegra_gpio_enable(TEGRA_CAMERA_GPIO_CAM_PWR_EN);
	err = gpio_request(TEGRA_CAMERA_GPIO_CAM_PWR_EN, "cam_pwr_en");
	if (err != 0)
		goto exit;
	err = gpio_direction_output(TEGRA_CAMERA_GPIO_CAM_PWR_EN, 0);
	if (err != 0)
		goto exit_free_gpio_cam_pwr_en;

	tegra_gpio_enable(TEGRA_CAMERA_GPIO_CAM_RST);
	err = gpio_request(TEGRA_CAMERA_GPIO_CAM_RST, "cam_rst");
	if (err != 0)
		goto exit_free_gpio_cam_pwr_en;
	err = gpio_direction_output(TEGRA_CAMERA_GPIO_CAM_RST, 0);
	if (err != 0)
		goto exit_free_gpio_cam_rst;

	tegra_gpio_enable(TEGRA_CAMERA_GPIO_CAM_PWDN);
	err = gpio_request(TEGRA_CAMERA_GPIO_CAM_PWDN, "cam_pwdn");
	if (err != 0)
		goto exit_free_gpio_cam_rst;
	err = gpio_direction_output(TEGRA_CAMERA_GPIO_CAM_PWDN, 0);
	if (err != 0)
		goto exit_free_gpio_cam_pwdn;

	clk_vi = clk_get_sys("tegra_camera", "vi");
	if (!clk_vi)
		pr_warn("Failed to get vi clock\n");

	clk_vi_sensor = clk_get_sys("tegra_camera", "vi_sensor");
	if (!clk_vi_sensor)
		pr_warn("Failed to get vi_sensor clock\n");

	clk_csi = clk_get_sys("tegra_camera", "csi");
	if (!clk_csi)
		pr_warn("Failed to get csi clock\n");

	clk_isp = clk_get_sys("tegra_camera", "isp");
	if (!clk_isp)
		pr_warn("Failed to get isp clock\n");

	clk_csus = clk_get_sys("tegra_camera", "csus");
	if (!clk_csus)
		pr_warn("Failed to get csus clock\n");

	nvhost_device_register(&tegra_camera_device);

	platform_device_register(&soc_camera);

	return 0;

exit_free_gpio_cam_pwdn:
	gpio_free(TEGRA_CAMERA_GPIO_CAM_PWDN);
exit_free_gpio_cam_rst:
	gpio_free(TEGRA_CAMERA_GPIO_CAM_RST);
exit_free_gpio_cam_pwr_en:
	gpio_free(TEGRA_CAMERA_GPIO_CAM_PWR_EN);
exit:
	BUG_ON(1);
	return err;
}

