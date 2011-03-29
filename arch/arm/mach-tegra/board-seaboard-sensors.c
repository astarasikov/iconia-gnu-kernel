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

/* I2C addresses. */
#define TEGRA_CAMERA_I2C_ADDR_PORT_EXPANDER	0x20
#define TEGRA_CAMERA_I2C_ADDR_PORT_SWITCH	0x70

/* GPIOs relevant to camera module. */
#define TEGRA_CAMERA_GPIO_CAM_PWR_EN	TEGRA_GPIO_PV4
#define TEGRA_CAMERA_GPIO_VI_GP3	TEGRA_GPIO_PBB4
#define TEGRA_CAMERA_GPIO_PMU		(TEGRA_NR_GPIOS + 1)

/* Port expander registers. */
#define TCA6416_REG_INP                 0x00
#define TCA6416_REG_OUTP                0x02
#define TCA6416_REG_PINV                0x04
#define TCA6416_REG_CNF                 0x06

/* Port expander ports. */
#define TCA6416_PORT_CAM1_PWDN          (1 << 0)
#define TCA6416_PORT_CAM1_RST           (1 << 1)
#define TCA6416_PORT_TP_CAM1_AF_PWDN    (1 << 2)
#define TCA6416_PORT_CAM1_LDO_SHDN      (1 << 3)
#define TCA6416_PORT_CAM2_PWDN          (1 << 4)
#define TCA6416_PORT_CAM2_RST           (1 << 5)
#define TCA6416_PORT_TP_CAM2_AF_PWDN    (1 << 6)
#define TCA6416_PORT_CAM2_LDO_SHDN      (1 << 7)
#define TCA6416_PORT_CAM3_PWDN          (1 << 8)
#define TCA6416_PORT_CAM3_RST           (1 << 9)
#define TCA6416_PORT_TP_CAM3_AF_PWDN    (1 << 10)
#define TCA6416_PORT_CAM3_LDO_SHDN	(1 << 11)
#define TCA6416_PORT_CAM_LED1           (1 << 12)
#define TCA6416_PORT_CAM_LED2		(1 << 13)
#define TCA6416_PORT_GPIO_PI6		(1 << 14)
#define TCA6416_PORT_CAM_I2C_MUX_RST    (1 << 15)

static struct regulator *regulator;
static struct i2c_client *port_expander;
static struct i2c_client *port_switch;
static struct clk *clk_vi;
static struct clk *clk_vi_sensor;
static struct clk *clk_csi;
static struct clk *clk_isp;
static struct clk *clk_csus;

static void tegra_camera_dump_port_expander_regs(struct nvhost_device *ndev)
{
#ifdef DEBUG
	u16 val;

	dev_info(&ndev->dev, "Port expander regs:\n");
	val = i2c_smbus_read_word_data(port_expander, TCA6416_REG_INP);
	dev_info(&ndev->dev, "INP = 0x%04x\n", val);
	val = i2c_smbus_read_word_data(port_expander, TCA6416_REG_OUTP);
	dev_info(&ndev->dev, "OUTP = 0x%04x\n", val);
	val = i2c_smbus_read_word_data(port_expander, TCA6416_REG_PINV);
	dev_info(&ndev->dev, "PINV = 0x%04x\n", val);
	val = i2c_smbus_read_word_data(port_expander, TCA6416_REG_CNF);
	dev_info(&ndev->dev, "CNF = 0x%04x\n", val);
#endif
}

static void tegra_camera_dump_port_switch_regs(struct nvhost_device *ndev)
{
#ifdef DEBUG
	u8 val;

	val = i2c_smbus_read_byte(port_switch);
	dev_info(&ndev->dev, "I2C switch reg = 0x%02x\n", val);
#endif
}

static int tegra_camera_enable(struct nvhost_device *ndev)
{
	struct i2c_adapter *adapter;
	struct i2c_board_info port_expander_info = {
		I2C_BOARD_INFO("tca6416",
			       TEGRA_CAMERA_I2C_ADDR_PORT_EXPANDER) };
	struct i2c_board_info port_switch_info = {
		I2C_BOARD_INFO("pca9546",
			       TEGRA_CAMERA_I2C_ADDR_PORT_SWITCH) };
	int err;
	u16 val;
	u8 val2;

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

	/* Get the I2C adapter and clients for the stuff on the camera board. */
	adapter = i2c_get_adapter(TEGRA_CAMERA_I2C_ADAPTER_ID);
	if (!adapter) {
		err = -EINVAL;
		goto exit_regulator_disable;
	}

	port_expander = i2c_new_device(adapter, &port_expander_info);
	if (!port_expander) {
		err = -EINVAL;
		goto exit_adapter_put;
	}

	port_switch = i2c_new_device(adapter, &port_switch_info);
	if (!port_switch) {
		err = -EINVAL;
		goto exit_port_expander_unregister;
	}

	/* Set up GPIOs. */
	err = gpio_request(TEGRA_CAMERA_GPIO_CAM_PWR_EN, "cam_pwr_en");
	if (err != 0)
		goto exit_port_switch_unregister;
	gpio_direction_output(TEGRA_CAMERA_GPIO_CAM_PWR_EN, 1);

	err = gpio_request(TEGRA_CAMERA_GPIO_VI_GP3, "vi_gp3");
	if (err != 0)
		goto exit_gpio_free_cam_pwr_en;
	gpio_direction_output(TEGRA_CAMERA_GPIO_VI_GP3, 1);

	err = gpio_request(TEGRA_CAMERA_GPIO_PMU, "tegra_camera");
	if (err != 0)
		goto exit_gpio_free_vi_gp3;
	gpio_direction_output(TEGRA_CAMERA_GPIO_PMU, 1);

	/* All port pins on the port expander are inputs by default.
	 * Set all to output.
	 */
	i2c_smbus_write_word_data(port_expander, TCA6416_REG_CNF, 0x0000);

	/* Take port switch out of reset and turn on camera 3. */
	val = TCA6416_PORT_CAM3_RST |
	      TCA6416_PORT_TP_CAM3_AF_PWDN |
	      TCA6416_PORT_CAM3_LDO_SHDN |
	      TCA6416_PORT_CAM_I2C_MUX_RST |
	      TCA6416_PORT_CAM_LED1;
	i2c_smbus_write_word_data(port_expander, TCA6416_REG_OUTP, val);

	tegra_camera_dump_port_expander_regs(ndev);

	/* Twiddle port switch to select our camera. */
	val2 = i2c_smbus_read_byte(port_switch);
	val2 |= (1 << 2); /* Enable port 2 (out of 0..3). */
	i2c_smbus_write_byte(port_switch, val2);

	tegra_camera_dump_port_switch_regs(ndev);

	/* Give the sensor time to come out of reset.  The OV9740 needs
	 * 8192 clock cycles (from vi_sensor clock) before the first I2C
	 * transaction.
	 */
	udelay(1000);

	return 0;

exit_gpio_free_vi_gp3:
	gpio_free(TEGRA_CAMERA_GPIO_VI_GP3);
exit_gpio_free_cam_pwr_en:
	gpio_free(TEGRA_CAMERA_GPIO_CAM_PWR_EN);
exit_port_switch_unregister:
	i2c_unregister_device(port_switch);
exit_port_expander_unregister:
	i2c_unregister_device(port_expander);
exit_adapter_put:
	i2c_put_adapter(adapter);
exit_regulator_disable:
	regulator_disable(regulator);
exit_regulator_put:
	regulator_put(regulator);
exit:
	return err;
}

static void tegra_camera_disable(struct nvhost_device *ndev)
{
	struct i2c_adapter *adapter;

	gpio_free(TEGRA_CAMERA_GPIO_PMU);
	gpio_free(TEGRA_CAMERA_GPIO_VI_GP3);
	gpio_free(TEGRA_CAMERA_GPIO_CAM_PWR_EN);

	adapter = i2c_get_adapter(TEGRA_CAMERA_I2C_ADAPTER_ID);
	BUG_ON(!adapter);
	i2c_unregister_device(port_switch);
	i2c_unregister_device(port_expander);
	i2c_put_adapter(adapter);

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

static struct i2c_board_info seaboard_i2c_bus3_sensor_info = {
	I2C_BOARD_INFO("ov9740", 0x10),
};

static struct soc_camera_link ov9740_iclink = {
	.bus_id		= 0,
	.i2c_adapter_id	= TEGRA_CAMERA_I2C_ADAPTER_ID,
	.board_info	= &seaboard_i2c_bus3_sensor_info,
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
	.flip_v			= 1,
	.flip_h			= 0,
};

int __init seaboard_sensors_init(void)
{
	tegra_camera_device.dev.platform_data = &tegra_camera_platform_data;

	tegra_gpio_enable(TEGRA_CAMERA_GPIO_CAM_PWR_EN);
	tegra_gpio_enable(TEGRA_CAMERA_GPIO_VI_GP3);

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
}

