/*
 * mouse driver for a ASUS TF101 touchpad
 *
 * Copyright (C)  Ilya Petrov <ilya.muromec@gmail.com>
 *
 * Based on NVEC driver.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#include <linux/slab.h>
#include <linux/serio.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include "asusec.h"
#include <linux/i2c.h>

#define START_STREAMING	{'\x06', '\x03', '\x01'}
#define STOP_STREAMING	{'\x06', '\x04'}
#define SEND_COMMAND	{'\x06', '\x01', '\xf4', '\x01'}

#define asusec_write_async(__x, __y, __sz) {}
extern struct asusec_chip *ec_chip;

static unsigned char MOUSE_RESET[] = {'\x06', '\x01', '\xff', '\x03'};

struct asusec_ps2 {
	struct serio *ser_dev;
	struct notifier_block notifier;
	struct asusec_chip *asusec;
};

static struct asusec_ps2 ps2_dev;

static int ps2_startstreaming(struct serio *ser_dev)
{
	unsigned char buf[] = START_STREAMING;
	asusec_write_async(ps2_dev.asusec, buf, sizeof(buf));
	return 0;
}

static void ps2_stopstreaming(struct serio *ser_dev)
{
	unsigned char buf[] = STOP_STREAMING;
	asusec_write_async(ps2_dev.asusec, buf, sizeof(buf));
}

static int ps2_sendcommand(struct serio *ser_dev, unsigned char cmd)
{
	u16 asus_ec_cmd;
	int ret;
	asus_ec_cmd = (((cmd & 0x00ff) << 8) | 0xD4);

	dev_dbg(&ser_dev->dev, "Sending ps2 cmd %02x\n", cmd);
	ret = i2c_smbus_write_word_data(ec_chip->client, 0x64, asus_ec_cmd);
	if (ret < 0) {
		dev_err(&ser_dev->dev, "Write to device fails status %x\n",ret);
		return ret;
	}
	
	return 0;
}

static int asusec_ps2_notifier(struct notifier_block *nb,
				unsigned long event_type, void *data)
{
	int i;
	unsigned char *msg = (unsigned char *)data;

	msg++;

	if(*msg == 0x21) {
		msg++;

		serio_interrupt(ps2_dev.ser_dev, msg[0], 0);
		serio_interrupt(ps2_dev.ser_dev, msg[1], 0);
		serio_interrupt(ps2_dev.ser_dev, msg[2], 0);

		return NOTIFY_STOP;
	}

	return NOTIFY_DONE;
}


static int __devinit asusec_mouse_probe(struct platform_device *pdev)
{
	struct asusec_chip *asusec = dev_get_drvdata(pdev->dev.parent);
	struct serio *ser_dev = kzalloc(sizeof(struct serio), GFP_KERNEL);

	ser_dev->id.type = SERIO_8042;
	ser_dev->write = ps2_sendcommand;
	ser_dev->open = ps2_startstreaming;
	ser_dev->close = ps2_stopstreaming;

	strlcpy(ser_dev->name, "asusec mouse", sizeof(ser_dev->name));
	strlcpy(ser_dev->phys, "asusec", sizeof(ser_dev->phys));

	ps2_dev.ser_dev = ser_dev;
	ps2_dev.notifier.notifier_call = asusec_ps2_notifier;
	ps2_dev.asusec = asusec;
	asusec_register_notifier(asusec, &ps2_dev.notifier, 0);

	serio_register_port(ser_dev);

	/* mouse reset */
	asusec_write_async(asusec, MOUSE_RESET, 4);
	printk("asusec mouse mfd\n");

	return 0;
}

static struct platform_driver asusec_mouse_driver = {
	.probe	= asusec_mouse_probe,
	.driver	= {
		.name	= "asusec-mouse",
		.owner	= THIS_MODULE,
	},
};

static int __init asusec_mouse_init(void)
{
	return platform_driver_register(&asusec_mouse_driver);
}

module_init(asusec_mouse_init);

MODULE_DESCRIPTION("asusec mouse driver");
MODULE_AUTHOR("Ilya Petrov <ilya.muromec@gmail.com>");
MODULE_LICENSE("GPL");
