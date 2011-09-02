#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>

#include <asm/gpio.h>

#include <asm/uaccess.h>
#include <linux/delay.h>

//----
#include <linux/input.h>
#include <linux/debugfs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/gpio.h>
//----

#include "../../../arch/arm/mach-tegra/gpio-names.h"
#include <linux/workqueue.h>
#include <linux/delay.h>
#include "dsp.h"


#undef DUMP_REG

#define DSP_IOC_MAGIC	0xf3
#define DSP_IOC_MAXNR	2
#define DSP_CONTROL	_IOW(DSP_IOC_MAGIC, 1,int)
#define DSP_RECONFIG	_IOW(DSP_IOC_MAGIC, 2,int)

#define START_RECORDING 1
#define END_RECORDING 0
#define PLAYBACK 2

#define DEVICE_NAME		"dsp_fm34"

struct i2c_client *fm34_client;

static int fm34_probe(struct i2c_client *client,
			   const struct i2c_device_id *id);
static int fm34_remove(struct i2c_client *client);
static int fm34_suspend(struct i2c_client *client, pm_message_t mesg);
static int fm34_resume(struct i2c_client *client);
static void fm34_reconfig(void) ;
extern int hs_micbias_power(int on);
extern bool jack_alive;

static const struct i2c_device_id fm34_id[] = {
	{DEVICE_NAME, 0},
	{}
};

struct i2c_client *fm34_client;
struct fm34_chip *dsp_chip;
bool bConfigured=false;

MODULE_DEVICE_TABLE(i2c, fm34_id);

static struct i2c_driver fm34_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= DEVICE_NAME,
	},
	.probe		= fm34_probe,
	.remove		= fm34_remove,
	.resume         = fm34_resume,
	.suspend        = fm34_suspend,
	.id_table	= fm34_id,
};

void fm34_reset_DSP(void)
{
	gpio_set_value(TEGRA_GPIO_PH2, 0);
	msleep(10);
	FM34_INFO("GPIO = %d , state = %d\n", TEGRA_GPIO_PH2, gpio_get_value(TEGRA_GPIO_PH2));

	gpio_set_value(TEGRA_GPIO_PH2, 1);
	FM34_INFO("GPIO = %d , state = %d\n", TEGRA_GPIO_PH2, gpio_get_value(TEGRA_GPIO_PH2));

	return;
}

int fm34_config_DSP(void)
{
	int ret=0;
	struct i2c_msg msg[3];
	u8 buf1;
	int config_length;
	u8 *config_table;

	if(!bConfigured){
		gpio_set_value(TEGRA_GPIO_PH3, 1); // Enable DSP
		msleep(20);

		//access chip to check if acknowledgement.
		buf1=0xC0;
		/* Write register */
		msg[0].addr = dsp_chip->client->addr;
		msg[0].flags = 0;
		msg[0].len = 1;
		msg[0].buf = &buf1;

		ret = i2c_transfer(dsp_chip->client->adapter, msg, 1);
		if(ret < 0){
			FM34_INFO("DSP NOack, Failed to read 0x%x: %d\n", buf1, ret);
			return ret;
		}
		else
			FM34_INFO("DSP ACK,  read 0x%x: %d\n", buf1, ret);

		fm34_reset_DSP();
		msleep(100);

		FM34_INFO("Load DSP parameters\n");
		config_length= sizeof(input_parameter);
		config_table= (u8 *)input_parameter;

		ret = i2c_master_send(dsp_chip->client, config_table, config_length);
		FM34_INFO("config_length = %d\n", config_length);

		if(ret == config_length){
			FM34_INFO("DSP configuration is done\n");
			bConfigured=true;
		}	

		msleep(100);
	}

	gpio_set_value(TEGRA_GPIO_PH3, 0);

	return ret;
}
EXPORT_SYMBOL(fm34_config_DSP);

static ssize_t fm34_show(struct device *class, struct device_attribute *attr, char *buf)
{
	struct fm34_chip *data = i2c_get_clientdata(to_i2c_client(class));
	fm34_reconfig();

	return sprintf(buf, "%d\n", data->status);
}

static int fm34_chip_init(struct i2c_client *client)
{
	int rc = 0;

	//config RST# pin, default HIGH.
	tegra_gpio_enable(TEGRA_GPIO_PH2);
	rc = gpio_request(TEGRA_GPIO_PH2, "fm34_reset");
	if (rc) {
		FM34_ERR("gpio_request failed for input %d\n", TEGRA_GPIO_PH2);
	}

	rc = gpio_direction_output(TEGRA_GPIO_PH2, 1) ;
	if (rc) {
		FM34_ERR("gpio_direction_output failed for input %d\n", TEGRA_GPIO_PH2);
	}
	FM34_INFO("GPIO = %d , state = %d\n", TEGRA_GPIO_PH2, gpio_get_value(TEGRA_GPIO_PH2));

	gpio_set_value(TEGRA_GPIO_PH2, 1);

	//config PWDN# pin, default HIGH.
	tegra_gpio_enable(TEGRA_GPIO_PH3);
	rc = gpio_request(TEGRA_GPIO_PH3, "fm34_pwdn");
	if (rc) {
		FM34_ERR("gpio_request failed for input %d\n", TEGRA_GPIO_PH3);
	}

	rc = gpio_direction_output(TEGRA_GPIO_PH3, 1) ;
	if (rc) {
		FM34_ERR("gpio_direction_output failed for input %d\n", TEGRA_GPIO_PH3);
	}
	FM34_INFO("GPIO = %d , state = %d\n", TEGRA_GPIO_PH3, gpio_get_value(TEGRA_GPIO_PH3));

	gpio_set_value(TEGRA_GPIO_PH3, 1);

	return 0;
}


static SENSOR_DEVICE_ATTR(dsp_status, S_IRUGO, fm34_show, NULL, 1);

static struct attribute *fm34_attr[] = {
	&sensor_dev_attr_dsp_status.dev_attr.attr,
	NULL
};


static int fm34_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct fm34_chip *data;
	int err;

	dev_dbg(&client->dev, "%s()\n", __func__);

	data = kzalloc(sizeof (struct fm34_chip), GFP_KERNEL);
	if (!data) {
		dev_err(&client->dev, "Memory allocation fails\n");
		err = -ENOMEM;
		goto exit;
	}

	dsp_chip=data;
	data->status = 0;

	i2c_set_clientdata(client, data);
	data->client = client;
	fm34_client= data->client;

	/* Register sysfs hooks */
	data->attrs.attrs = fm34_attr;
	err = sysfs_create_group(&client->dev.kobj, &data->attrs);
	if (err) {
		dev_err(&client->dev, "Not able to create the sysfs\n");
		goto exit_free;
	}

	fm34_chip_init(dsp_chip->client);
	data->status = 1;

	bConfigured=false;

	msleep(20);
	fm34_config_DSP();

	pr_info("%s()\n", __func__);

	return 0;


exit_free:
	kfree(data);
exit:
	return err;
}

static int fm34_remove(struct i2c_client *client)
{
	struct fm34_chip *data = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "%s()\n", __func__);
	pr_info("%s()\n", __func__);
	sysfs_remove_group(&client->dev.kobj, &data->attrs);

	kfree(data);
	return 0;
}

void fm34_reconfig(void)
{
	FM34_INFO("ReConfigure DSP\n");
	bConfigured=false;
	fm34_config_DSP();
}

static int fm34_suspend(struct i2c_client *client, pm_message_t mesg)
{
	printk("fm34_suspend+\n");
	gpio_set_value(TEGRA_GPIO_PH3, 0); // Bypass DSP
	printk("fm34_suspend-\n");
	return 0;
}
static int fm34_resume(struct i2c_client *client)
{
	printk("fm34_resume+\n");
	printk("fm34_resume-\n");
	return 0;
}

static int __init fm34_init(void)
{
	pr_info("%s()\n", __func__);
	return i2c_add_driver(&fm34_driver);
}

static void __exit fm34_exit(void)
{
	pr_info("%s()\n", __func__);
	i2c_del_driver(&fm34_driver);
}

module_init(fm34_init);
module_exit(fm34_exit);
