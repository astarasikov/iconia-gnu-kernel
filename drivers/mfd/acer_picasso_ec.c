/*
    acer_picasso_ec.c - i2c chip driver for Acer EC
    Copyright (C) 2011 Alexander Tarasikov <alexander.tarasikov@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/mfd/core.h>
#include <linux/mfd/acer_picasso_ec.h>

#define I2C_READ_RETRY_TIMES 3
#define I2C_WRITE_RETRY_TIMES 3

static s32 ec_read_word(struct i2c_client *client, u8 command)
{
        int i, ret;
		struct acer_picasso_ec_priv *priv = NULL;

		if (!client) {
			printk(KERN_ERR "%s: client is NULL\n", __func__);
			ret = -EINVAL;
			goto exit;
		}

		priv = i2c_get_clientdata(client);
		mutex_lock(&priv->mutex);

        for (i = 0; i < I2C_READ_RETRY_TIMES; i++) {
                ret = i2c_smbus_read_word_data(client, command);

                if (ret >= 0)
                        break;

                dev_err(&client->dev,
                        "%s: failed, trying again\n",
                        __func__);
        }

        if (i == I2C_READ_RETRY_TIMES) {
                dev_err(&client->dev,
                        "%s: failed\n", __func__);
                ret = -EINVAL;
        }

exit:
		mutex_unlock(&priv->mutex);
        return ret;

}

static s32 ec_write_word(struct i2c_client *client, u8 command, u16 value)
{
        int i, ret;
		struct acer_picasso_ec_priv *priv = NULL;
		
		if (!client) {
			printk(KERN_ERR "%s: client is NULL\n", __func__);
			ret = -EINVAL;
			goto exit;
		}
		
		priv = i2c_get_clientdata(client);
		mutex_lock(&priv->mutex);

        for (i = 0; i < I2C_WRITE_RETRY_TIMES; i++) {
                ret = i2c_smbus_write_word_data(client, command, value);

                if (ret == 0)
                        break;

                dev_err(&client->dev,
                        "%s: failed, trying again\n",
                        __func__);
        }

        if (i == I2C_WRITE_RETRY_TIMES) {
                dev_err(&client->dev,
                        "%s: failed\n", __func__);
                ret = -EINVAL;
        }

exit:
		mutex_unlock(&priv->mutex);
        return ret;
}

static struct mfd_cell picasso_ec_funcs[] = {
	{
		.id = -1,
		.name = PICASSO_EC_BAT_ID,
	},
	{
		.id = -1,
		.name = PICASSO_EC_LED_ID,
	},
	{
		.id = -1,
		.name = PICASSO_EC_SYS_ID,
	}
};

static int picasso_ec_check_version(struct i2c_client *client) {
	int rc = 0;
	s32 ver_minor, ver_major;

	ver_major = ec_read_word(client, EC_VER_MAJOR);
	if (ver_major < 0) {
		dev_err(&client->dev, "failed to read EC major version\n");	
		return ver_major;
	}

	ver_minor = ec_read_word(client, EC_VER_MINOR);
	if (ver_minor < 0) {
		dev_err(&client->dev, "failed to read EC minor version\n");
		return ver_minor;
	}

	ver_major = (ver_major << 16) | (ver_minor & 0xffff);
	dev_info(&client->dev, "EC version is %x\n", ver_major);

	return rc;
}

static int picasso_ec_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	int rc;
	struct acer_picasso_ec_priv *priv;

	priv = kzalloc(sizeof(struct acer_picasso_ec_priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&client->dev, "failed to private data\n");
		return -ENOMEM;
	}

	priv->read = ec_read_word;
	priv->write = ec_write_word;
	priv->client = client;
	mutex_init(&priv->mutex);

	i2c_set_clientdata(client, priv);

	rc = picasso_ec_check_version(client);
	if (rc < 0) {
		dev_err(&client->dev, "Failed to read picasso EC version\n");
		goto fail;
	}

	rc = mfd_add_devices(&client->dev, -1,
		picasso_ec_funcs, ARRAY_SIZE(picasso_ec_funcs),
		NULL, -1);

	if (rc) {
		dev_err(&client->dev, "error adding subdevices");
	}
	return 0;

fail:
	i2c_set_clientdata(client, NULL);
	mutex_destroy(&priv->mutex);
	return -ENODEV;
}

static int picasso_ec_remove(struct i2c_client *client)
{
	struct acer_picasso_ec_priv *priv = i2c_get_clientdata(client);
	mfd_remove_devices(&client->dev);

	mutex_destroy(&priv->mutex);
	kfree(priv);
	return 0;
}

static const struct i2c_device_id picasso_ec_ids[] = {
	{PICASSO_EC_ID, 0},
	{}
};

#if CONFIG_PM
static int picasso_ec_suspend(struct i2c_client *client, pm_message_t mesg)
{
	return 0;
}

static int picasso_ec_resume(struct i2c_client *client)
{
	return 0;
}
#else
#define picasso_ec_suspend NULL
#define picasso_ec_resume NULL
#endif

static struct i2c_driver picasso_ec_driver = {
	.driver	=	{
		.name	=	PICASSO_EC_NAME,
		.owner	=	THIS_MODULE,
	},
	.id_table	=	picasso_ec_ids,
	.probe	=	picasso_ec_probe,
	.remove	=	picasso_ec_remove,
	.suspend	=	picasso_ec_suspend,
	.resume		=	picasso_ec_resume,
};

static int __init picasso_ec_init(void)
{
	printk(KERN_INFO "%s: registering driver\n", __func__);
	return i2c_add_driver(&picasso_ec_driver);
}

static void __exit picasso_ec_exit(void)
{
	printk(KERN_INFO "%s: unregistering driver\n", __func__);
	i2c_del_driver(&picasso_ec_driver);
}

MODULE_AUTHOR("Alexander Tarasikov <alexander.tarasikov@gmail.com>");
MODULE_DESCRIPTION("Acer Picasso EC driver");
MODULE_LICENSE("GPL");
module_init(picasso_ec_init);
module_exit(picasso_ec_exit);
