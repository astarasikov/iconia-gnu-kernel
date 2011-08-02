/* 
 * ASUS EC driver for EEEPad transformer TF101
 *
 * Copyright (C) 2011 Ilya Petrov <ilua.muromec.gmail.com>
 *
 * Oiriginally based on code from ASUS.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <asm/gpio.h>
#include <asm/ioctl.h>
#include <asm/uaccess.h>
#include <linux/power_supply.h>

#include "asusec.h"

/*
 * functions declaration
 */
static int asusec_i2c_write_data(struct i2c_client *client, u16 data);
static int asusec_i2c_read_data(struct i2c_client *client);
static void asusec_reset_dock(void);
static int asusec_is_init_running(void);
static int asusec_chip_init(struct i2c_client *client);
static void asusec_work_function(struct work_struct *dat);
static void asusec_dock_init_work_function(struct work_struct *dat);
static int __devinit asusec_probe(struct i2c_client *client,
		const struct i2c_device_id *id);
static int __devexit asusec_remove(struct i2c_client *client);


static int asusec_keypad_get_response(struct i2c_client *client, int res);
static int asusec_keypad_enable(struct i2c_client *client);
static int asusec_touchpad_get_response(struct i2c_client *client, int res);
static int asusec_touchpad_enable(struct i2c_client *client);
static int asusec_touchpad_disable(struct i2c_client *client);
static int asusec_suspend(struct i2c_client *client, pm_message_t mesg);
static int asusec_resume(struct i2c_client *client);
static int asusec_dock_battery_get_capacity(union power_supply_propval *val);
static int asusec_dock_battery_get_status(union power_supply_propval *val);
static int asusec_dock_battery_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val);

/*
 * global variable
 */

struct i2c_client dockram_client;
struct asusec_chip *ec_chip;

static struct workqueue_struct *asusec_wq;

static const struct i2c_device_id asusec_id[] = {
	{"asusec", 0},
	{}
};

#include <linux/platform_device.h>
#include <linux/mfd/core.h>

static struct mfd_cell asusec_devices[] = {
	{
		.name		= "asusec-kbd",
		.id		= 1,
	},
	{
		.name		= "asusec-mouse",
		.id		= 1,
	},
};

static enum power_supply_property asusec_dock_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CAPACITY,
};

static struct power_supply asusec_power_supply[] = {
	{
		.name		= "dock_battery",
		.type		= POWER_SUPPLY_TYPE_BATTERY,
		.properties	= asusec_dock_properties,
		.num_properties	= ARRAY_SIZE(asusec_dock_properties),
		.get_property	= asusec_dock_battery_get_property,
	},
};

MODULE_DEVICE_TABLE(i2c, asusec_id);

static struct i2c_driver asusec_driver = {
	.class	= I2C_CLASS_HWMON,
	.driver	 = {
		.name = "asusec",
		.owner = THIS_MODULE,
	},
	.probe	 = asusec_probe,
	.remove	 = __devexit_p(asusec_remove),
	.suspend = asusec_suspend,
	.resume = asusec_resume,
	.id_table = asusec_id,
};


/*
 * functions definition
 */
static void asusec_dockram_init(struct i2c_client *client){
	dockram_client.adapter = client->adapter;
	dockram_client.addr = 0x1b;
	dockram_client.detected = client->detected; 
	dockram_client.dev = client->dev;
	dockram_client.driver = client->driver;
	dockram_client.flags = client->flags;
	dockram_client.irq = client->irq;
	strcpy(dockram_client.name,client->name);
}

static int asusec_dockram_read_data(int cmd)
{
	int ret = 0;

	if (ec_chip->dock_in == 0){
		return -1;
	}
	
	ret = i2c_smbus_read_i2c_block_data(&dockram_client, cmd, 32, ec_chip->i2c_dm_data);
	if (ret < 0) {
		ASUSEC_ERR("Fail to read dockram data, status %d\n", ret);
	}
	return ret;
}

static int asusec_i2c_write_data(struct i2c_client *client, u16 data)
{
	int ret = 0;

	if (ec_chip->dock_in == 0){
		return -1;
	}

	ret = i2c_smbus_write_word_data(client, 0x64, data);
	if (ret < 0) {
		ASUSEC_ERR("Fail to write data, status %d\n", ret);
	}
	return ret;
}

static int asusec_i2c_read_data(struct i2c_client *client)
{
	int ret = 0;

	if (ec_chip->dock_in == 0){
		return -1;
	}

	ret = i2c_smbus_read_i2c_block_data(client, 0x6A, 8, ec_chip->i2c_data);
	if (ret < 0) {
		ASUSEC_ERR("Fail to read data, status %d\n", ret);
	}
	return ret;
}

static int asusec_keypad_get_response(struct i2c_client *client, int res)
{
	int retry = ASUSEC_RETRY_COUNT;

	while(retry-- > 0){
		asusec_i2c_read_data(client);		
		ASUSEC_I2C_DATA(ec_chip->i2c_data, ec_chip->index);
		if ((ec_chip->i2c_data[1] & ASUSEC_OBF_MASK) && 
			(!(ec_chip->i2c_data[1] & ASUSEC_AUX_MASK))){ 
			if (ec_chip->i2c_data[2]  == res){
				goto get_asusec_keypad_i2c;
			}
		}		
		msleep(CONVERSION_TIME_MS/5);
	}
	return -1;

get_asusec_keypad_i2c:
	return 0;	

}

static int asusec_keypad_enable(struct i2c_client *client)
{
	int retry = ASUSEC_RETRY_COUNT;

	while(retry-- > 0){
		asusec_i2c_write_data(client, 0xF400);		
		if(!asusec_keypad_get_response(client, ASUSEC_PS2_ACK)){
			goto keypad_enable_ok;
		}
	}
	ASUSEC_ERR("fail to enable keypad");
	return -1;

keypad_enable_ok:
	return 0;
}

static int asusec_keypad_disable(struct i2c_client *client)
{	
	int retry = ASUSEC_RETRY_COUNT;	

	while(retry-- > 0){
		asusec_i2c_write_data(client, 0xF500);
		if(!asusec_keypad_get_response(client, ASUSEC_PS2_ACK)){
			goto keypad_disable_ok;
		}
	}

	ASUSEC_ERR("fail to disable keypad");
	return -1;

keypad_disable_ok:
	return 0;
}


static int asusec_touchpad_get_response(struct i2c_client *client, int res)
{
	int retry = ASUSEC_RETRY_COUNT;

	msleep(CONVERSION_TIME_MS);
	while(retry-- > 0){
		asusec_i2c_read_data(client);
		ASUSEC_I2C_DATA(ec_chip->i2c_data, ec_chip->index);
		if ((ec_chip->i2c_data[1] & ASUSEC_OBF_MASK) && 
			(ec_chip->i2c_data[1] & ASUSEC_AUX_MASK)){ 
			if (ec_chip->i2c_data[2] == res){
				goto get_asusec_touchpad_i2c;
			}
		}		
		msleep(CONVERSION_TIME_MS/5);
	}

	ASUSEC_ERR("fail to get touchpad response");
	return -1;

get_asusec_touchpad_i2c:
	return 0;	

}

static int asusec_touchpad_enable(struct i2c_client *client)
{
	ec_chip->tp_wait_ack = 1;		
	asusec_i2c_write_data(client, 0xF4D4);
	return 0;
}

static int asusec_touchpad_disable(struct i2c_client *client)
{	
	int retry = 5;	

	while(retry-- > 0){
		asusec_i2c_write_data(client, 0xF5D4);
		if(!asusec_touchpad_get_response(client, ASUSEC_PS2_ACK)){
			goto touchpad_disable_ok;
		}
	}

	ASUSEC_ERR("fail to disable touchpad");
	return -1;

touchpad_disable_ok:
	return 0;
}

static int asusec_i2c_test(struct i2c_client *client){
	return asusec_i2c_write_data(client, 0x0000);
}

static void asusec_reset_dock(void){
	ec_chip->dock_init = 0;
	ASUSEC_NOTICE("send EC_Request\n");	
	gpio_set_value(TEGRA_GPIO_PS3, 0);
	msleep(CONVERSION_TIME_MS);
	gpio_set_value(TEGRA_GPIO_PS3, 1);		
}
static int asusec_is_init_running(void){
	int ret_val;
	
	mutex_lock(&ec_chip->dock_init_lock);
	ret_val = ec_chip->dock_init;
	ec_chip->dock_init = 1;
	mutex_unlock(&ec_chip->dock_init_lock);
	return ret_val;
}

static void asusec_clear_i2c_buffer(struct i2c_client *client){
	int i;
	for ( i=0; i<8; i++){
		asusec_i2c_read_data(client);
	}
}
static int asusec_chip_init(struct i2c_client *client)
{
	int ret_val = 0;
	int i;

	if(asusec_is_init_running()){
		return 0;
	}	

	//wake_lock(&ec_chip->wake_lock);
	disable_irq_nosync(client->irq);

	ec_chip->op_mode = 0;

	for ( i = 0; i < 10; i++){
		ret_val = asusec_i2c_test(client);
		if (ret_val < 0)
			msleep(300);
		else
			break;
	}
	if(ret_val < 0){
		goto fail_to_access_ec;
	}	

	for ( i=0; i<8; i++){
		asusec_i2c_read_data(client);
	}
	
	
	msleep(750);
	asusec_clear_i2c_buffer(client);
	asusec_touchpad_disable(client);

	asusec_keypad_disable(client);
	

	ASUSEC_NOTICE("touchpad and keyboard init\n");
	ec_chip->status = 1;
	ec_chip->d_index = 0;

	asusec_keypad_enable(client);
	asusec_clear_i2c_buffer(client);
	
	enable_irq(client->irq);
	ec_chip->init_success = 1;

	if (ec_chip->tp_enable){
		asusec_touchpad_enable(client);
	}

	//wake_unlock(&ec_chip->wake_lock);
	return 0;

fail_to_access_ec:
	if (asusec_dockram_read_data(0x00) < 0){
		ASUSEC_NOTICE("No EC detected\n");
		ec_chip->dock_in = 0;
	} else {
		ASUSEC_NOTICE("Need EC FW update\n");
	}
	enable_irq(client->irq);
	//wake_unlock(&ec_chip->wake_lock);
	return -1;

}


static irqreturn_t asusec_interrupt_handler(int irq, void *dev_id){

	int gpio = irq_to_gpio(irq);

	if (gpio == TEGRA_GPIO_PS2){
		disable_irq_nosync(irq);
			if (ec_chip->suspend_state){
				ec_chip->wakeup_lcd = 1;
				ec_chip->ap_wake_wakeup = 1;
			}
			queue_delayed_work(asusec_wq, &ec_chip->asusec_work, 0);
	}
	else if (gpio == TEGRA_GPIO_PX5){
		ec_chip->dock_in = 0;
		ec_chip->dock_det++;
		queue_delayed_work(asusec_wq, &ec_chip->asusec_dock_init_work, 0);
	}
	return IRQ_HANDLED;	
}

static int asusec_irq_dock_in(struct i2c_client *client)
{
	int rc = 0 ;
	unsigned gpio = TEGRA_GPIO_PX5;
	unsigned irq = gpio_to_irq(TEGRA_GPIO_PX5);
	const char* label = "asusec_dock_in" ; 

	ASUSEC_INFO("gpio = %d, irq = %d\n", gpio, irq);
	ASUSEC_INFO("GPIO = %d , state = %d\n", gpio, gpio_get_value(gpio));

	tegra_gpio_enable(gpio);
	rc = gpio_request(gpio, label);
	if (rc) {
		ASUSEC_ERR("gpio_request failed for input %d\n", gpio);		
	}

	rc = gpio_direction_input(gpio) ;
	if (rc) {
		ASUSEC_ERR("gpio_direction_input failed for input %d\n", gpio);			
		goto err_gpio_direction_input_failed;
	}
	ASUSEC_INFO("GPIO = %d , state = %d\n", gpio, gpio_get_value(gpio));

	rc = request_irq(irq, asusec_interrupt_handler,IRQF_SHARED|IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING/*|IRQF_TRIGGER_HIGH|IRQF_TRIGGER_LOW*/, label, client);
	if (rc < 0) {
		ASUSEC_ERR("Could not register for %s interrupt, irq = %d, rc = %d\n", label, irq, rc);	
		rc = -EIO;
		goto err_gpio_request_irq_fail ;
	}	
	ASUSEC_INFO("request irq = %d, rc = %d\n", irq, rc);

	if (gpio_get_value(gpio)){
		ASUSEC_NOTICE("No dock detected\n");
		ec_chip->dock_in = 0;
	} else{
		ASUSEC_NOTICE("Dock detected\n");
		ec_chip->dock_in = 1;
	}

	return 0 ;

err_gpio_request_irq_fail :	
	gpio_free(gpio);
err_gpio_direction_input_failed:
	return rc;
}


static int asusec_irq(struct i2c_client *client)
{
	int rc = 0 ;
	unsigned gpio = irq_to_gpio(client->irq);
	const char* label = "asusec_input" ; 

	ASUSEC_INFO("gpio = %d, irq = %d\n", gpio, client->irq);
	ASUSEC_INFO("GPIO = %d , state = %d\n", gpio, gpio_get_value(gpio));

	tegra_gpio_enable(gpio);
	rc = gpio_request(gpio, label);
	if (rc) {
		ASUSEC_ERR("gpio_request failed for input %d\n", gpio);		
		goto err_request_input_gpio_failed;
	}

	rc = gpio_direction_input(gpio) ;
	if (rc) {
		ASUSEC_ERR("gpio_direction_input failed for input %d\n", gpio);			
		goto err_gpio_direction_input_failed;
	}
	ASUSEC_INFO("GPIO = %d , state = %d\n", gpio, gpio_get_value(gpio));

	rc = request_irq(client->irq, asusec_interrupt_handler,/*IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING|IRQF_TRIGGER_HIGH|*/IRQF_TRIGGER_LOW, label, client);
	if (rc < 0) {
		ASUSEC_ERR("Could not register for %s interrupt, irq = %d, rc = %d\n", label, client->irq, rc);	
		rc = -EIO;
		goto err_gpio_request_irq_fail ;
	}	
	ASUSEC_INFO("request irq = %d, rc = %d\n", client->irq, rc);	

	return 0 ;

err_gpio_request_irq_fail :	
	gpio_free(gpio);
err_gpio_direction_input_failed:
err_request_input_gpio_failed :
	return rc;
}

static int asusec_irq_ec_request(struct i2c_client *client)
{
	int rc = 0 ;
	unsigned gpio = TEGRA_GPIO_PS3;
	const char* label = "asusec_request" ; 

	ASUSEC_INFO("gpio = %d, irq = %d\n", gpio, client->irq);
	ASUSEC_INFO("GPIO = %d , state = %d\n", gpio, gpio_get_value(gpio));

	tegra_gpio_enable(gpio);
	rc = gpio_request(gpio, label);
	if (rc) {
		ASUSEC_ERR("gpio_request failed for input %d\n", gpio);		
		goto err_exit;
	}

	rc = gpio_direction_output(gpio, 1) ;
	if (rc) {
		ASUSEC_ERR("gpio_direction_output failed for input %d\n", gpio);			
		goto err_exit;
	}
	ASUSEC_INFO("GPIO = %d , state = %d\n", gpio, gpio_get_value(gpio));
	
	return 0 ;

err_exit:
	return rc;
}



static void asusec_dock_init_work_function(struct work_struct *dat)
{
	int gpio = TEGRA_GPIO_PX5;
	int i = 0;
	int d_counter = 0;
	int gpio_state = 0;
	ASUSEC_INFO("Dock-init function\n");

	//wake_lock(&ec_chip->wake_lock_init);
	if (1) {//ASUSGetProjectID()==101){
		ASUSEC_NOTICE("EP101 dock-init\n");
		if (ec_chip->dock_det){
			gpio_state = gpio_get_value(gpio);
			for(i = 0; i < 40; i++){
				msleep(50);
				if (gpio_state == gpio_get_value(gpio)){
					d_counter++;
				} else {
					gpio_state = gpio_get_value(gpio);
					d_counter = 0;
				}
				if (d_counter > 4){
					break;
				}
			}
			ec_chip->dock_det--;
			ec_chip->re_init = 0;
		}
		
		mutex_lock(&ec_chip->input_lock);
		if (gpio_get_value(gpio)){
			ASUSEC_NOTICE("No dock detected\n");
			ec_chip->dock_in = 0;
			ec_chip->init_success = 0;
			ec_chip->tp_enable = 1;

		} else{

			ASUSEC_NOTICE("Dock detected %d / %d\n",
					gpio_get_value(TEGRA_GPIO_PS4),
					ec_chip->status);

			ec_chip->dock_in = 1;
			if (ec_chip->init_success == 0){
				msleep(400);
				asusec_reset_dock();
				msleep(200);
				asusec_chip_init(ec_chip->client);
			}
		}
		mutex_unlock(&ec_chip->input_lock);
	}

}


int asusec_register_notifier(struct asusec_chip *ec_chip, struct notifier_block *nb,
				unsigned int events)
{

	return atomic_notifier_chain_register(&ec_chip->notifier_list, nb);
}
EXPORT_SYMBOL_GPL(asusec_register_notifier);

static void asusec_work_function(struct work_struct *dat)
{
	int gpio = TEGRA_GPIO_PS2;
	int irq = gpio_to_irq(gpio);
	int ret_val = 0;

	if (ec_chip->wakeup_lcd){
		if (gpio_get_value(TEGRA_GPIO_PS4)){
			ec_chip->wakeup_lcd = 0;
			ec_chip->dock_in = gpio_get_value(TEGRA_GPIO_PX5) ? 0 : 1;
			//wake_lock_timeout(&ec_chip->wake_lock, 3*HZ);
			msleep(500);
		}
	}

	ret_val = asusec_i2c_read_data(ec_chip->client);
	enable_irq(irq);

	if (ret_val < 0){
		return ;
	}
	atomic_notifier_call_chain(&ec_chip->notifier_list, ec_chip->i2c_data[1],
			ec_chip->i2c_data);

}

static void asusec_reset_counter(unsigned long data){
	ec_chip->d_index = 0;
}

static int __devinit asusec_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int err = 0;

	ASUSEC_INFO("asusec probe\n");

	ec_chip = kzalloc(sizeof (struct asusec_chip), GFP_KERNEL);
	if (!ec_chip) {
		ASUSEC_ERR("Memory allocation fails\n");
		err = -ENOMEM;
		goto exit;
	}
		
	i2c_set_clientdata(client, ec_chip);
	ec_chip->client = client;
	ec_chip->client->driver = &asusec_driver;				
	ec_chip->client->flags = 1;

	mutex_init(&ec_chip->lock);
	mutex_init(&ec_chip->kbc_lock);
	mutex_init(&ec_chip->input_lock);
	mutex_init(&ec_chip->dock_init_lock);

	init_timer(&ec_chip->asusec_timer);
	ec_chip->asusec_timer.function = asusec_reset_counter;

	//wake_lock_init(&ec_chip->wake_lock, WAKE_LOCK_SUSPEND, "asusec_wake");
	//wake_lock_init(&ec_chip->wake_lock_init, WAKE_LOCK_SUSPEND, "asusec_wake_init");

	ec_chip->status = 0;
	ec_chip->dock_det = 0;
	ec_chip->dock_in = 0;
	ec_chip->dock_init = 0;
	ec_chip->d_index = 0;
	ec_chip->suspend_state = 0;
	ec_chip->init_success = 0;
	ec_chip->wakeup_lcd = 0;
	ec_chip->tp_wait_ack = 0;
	ec_chip->tp_enable = 1;
	ec_chip->re_init = 0;
	ec_chip->ec_wakeup = 0;
	asusec_dockram_init(client);
	

	err = power_supply_register(&client->dev, &asusec_power_supply[0]);
	if (err){
		ASUSEC_ERR("fail to register power supply for dock\n");
		goto exit;
	}
	
	asusec_wq = create_singlethread_workqueue("asusec_wq");
	INIT_DELAYED_WORK_DEFERRABLE(&ec_chip->asusec_work, asusec_work_function);
	INIT_DELAYED_WORK_DEFERRABLE(&ec_chip->asusec_dock_init_work, asusec_dock_init_work_function);
	
	ATOMIC_INIT_NOTIFIER_HEAD(&ec_chip->notifier_list);

	asusec_irq_dock_in(client);
	asusec_irq_ec_request(client);
	asusec_irq(client);

	queue_delayed_work(asusec_wq, &ec_chip->asusec_dock_init_work, 0);

	err = mfd_add_devices(&client->dev, -1, asusec_devices, ARRAY_SIZE(asusec_devices),
			NULL, 0);
	if(err)
		dev_err(&client->dev, "error adding subdevices\n");


	return 0;

exit:
	return err;
}

static int __devexit asusec_remove(struct i2c_client *client)
{
	struct asusec_chip *chip = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "%s()\n", __func__);
	kfree(chip);
	return 0;
}


static int asusec_suspend(struct i2c_client *client, pm_message_t mesg){
	printk("asusec_suspend+\n");
	printk("asusec_suspend-\n");
	return 0;
}

static int asusec_resume(struct i2c_client *client){

	printk("asusec_resume+\n");

	ec_chip->suspend_state = 0;

	ec_chip->init_success = 0;
	queue_delayed_work(asusec_wq, &ec_chip->asusec_dock_init_work, 0);

	printk("asusec_resume-\n");
	return 0;	
}



static int asusec_dock_battery_get_capacity(union power_supply_propval *val){
	int bat_percentage = 0;
	int ret_val = 0;

	val->intval = -1;
	if ((ec_chip->op_mode == 0) && (ec_chip->dock_in)){
		ret_val = asusec_dockram_read_data(0x14);

		if (ret_val < 0){
			return -1;
		}
		else {
			bat_percentage = (ec_chip->i2c_dm_data[14] << 8 )| ec_chip->i2c_dm_data[13];
			val->intval = bat_percentage;
			return 0;
		}
	}
	return -1;
}

static int asusec_dock_battery_get_status(union power_supply_propval *val){
	int ret_val = 0;

	val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
	if ((ec_chip->op_mode == 0) && (ec_chip->dock_in)){
		ret_val = asusec_dockram_read_data(0x0A);

		if (ret_val < 0){
			return -1;
		}
		else {
			if (ec_chip->i2c_dm_data[1] & 0x4)
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			return 0;
		}
	}
	return -1;
}

static int asusec_dock_battery_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	switch (psp) {
		case POWER_SUPPLY_PROP_CAPACITY:
			if(asusec_dock_battery_get_capacity(val) < 0)
				goto error;
			break;
		case POWER_SUPPLY_PROP_STATUS:
			if(asusec_dock_battery_get_status(val) < 0)
				goto error;
			break;
		default:
			return -EINVAL;
	}
	return 0;

error:
	return -EINVAL;
}

static int __init asusec_init(void)
{
	int err_code = 0;	


	err_code=i2c_add_driver(&asusec_driver);
	if(err_code){
		ASUSEC_ERR("i2c_add_driver fail\n") ;
		goto i2c_add_driver_fail ;
	}

	ASUSEC_INFO("return value %d\n", err_code) ;

	return 0;

i2c_add_driver_fail :

	return err_code;

}

static void __exit asusec_exit(void)
{
	i2c_del_driver(&asusec_driver);
}

module_init(asusec_init);
module_exit(asusec_exit);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ilya Petrov <ilya.muromec@gmail.com>");
