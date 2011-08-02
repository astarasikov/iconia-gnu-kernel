/*
 * keyboard driver for a NVIDIA compliant embedded controller
 *
 * Copyright (C) 2011 Marc Dietrich <marvin24@gmx.de>
 *
 * Authors:  Pierre-Hugues Husson <phhusson@free.fr>
 *           Marc Dietrich <marvin24@gmx.de>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#include <linux/slab.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include "asusec-keytable.h"
#include "asusec.h"

#define ACK_KBD_EVENT {'\x05', '\xed', '\x01'}

#define asusec_write_async(__x, __y, __sz) {}

static unsigned char keycodes[ARRAY_SIZE(code_tab_102us)
			+ ARRAY_SIZE(extcode_tf101)];

struct asusec_keys {
	struct input_dev *input;
	struct notifier_block notifier;
	struct asusec_chip *asusec;
};

static struct asusec_keys keys_dev;

static int asusec_keys_notifier(struct notifier_block *nb,
				unsigned long event_type, void *data)
{
	int state=1;
	unsigned char *msg = (unsigned char *)data;

	msg++;

	if(*msg == 5) {
		msg++;

		if(*msg == 0xE0) {
			msg++;
		}

		if(*msg == 0xF0) {
			state = 0;
			msg++;
		}

		input_report_key(keys_dev.input, code_tab_102us[*msg], state);


		input_sync(keys_dev.input);

		return NOTIFY_STOP;

	} else if (*msg == 0x41) {
		msg++;

		input_report_key(keys_dev.input, extcode_tf101[*msg], 1);
		input_report_key(keys_dev.input, extcode_tf101[*msg], 0);
		input_sync(keys_dev.input);


		return NOTIFY_STOP;

	}


	return NOTIFY_DONE;
}

static int asusec_kbd_event(struct input_dev *dev, unsigned int type,
				unsigned int code, int value)
{
	unsigned char buf[] = ACK_KBD_EVENT;
	struct asusec_chip *asusec = keys_dev.asusec;

	if (type == EV_REP)
		return 0;

	if (type != EV_LED)
		return -1;

	if (code != LED_CAPSL)
		return -1;

	buf[2] = !!value;
	asusec_write_async(asusec, buf, sizeof(buf));

	return 0;
}

static int __devinit asusec_kbd_probe(struct platform_device *pdev)
{
	struct asusec_chip *asusec = dev_get_drvdata(pdev->dev.parent);
	int i, j, err;
	struct input_dev *idev;

	j = 0;

	for (i = 0; i < ARRAY_SIZE(code_tab_102us); ++i)
		keycodes[j++] = code_tab_102us[i];

	for (i = 0; i < ARRAY_SIZE(extcode_tf101); ++i)
		keycodes[j++] = extcode_tf101[i];

	idev = input_allocate_device();
	idev->name = "asusec keyboard";
	idev->phys = "asusec";
	idev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_LED) |  BIT_MASK(EV_REP);
	idev->ledbit[0] = BIT_MASK(LED_CAPSL);
	idev->event = asusec_kbd_event;
	idev->keycode = keycodes;
	idev->keycodesize = sizeof(unsigned char);
	idev->keycodemax = ARRAY_SIZE(keycodes);

	for (i = 0; i < ARRAY_SIZE(keycodes); ++i)
		set_bit(keycodes[i], idev->keybit);

	clear_bit(0, idev->keybit);
	err = input_register_device(idev);
	if (err)
		goto fail;

	keys_dev.input = idev;
	keys_dev.notifier.notifier_call = asusec_keys_notifier;
	keys_dev.asusec = asusec;
	asusec_register_notifier(asusec, &keys_dev.notifier, 0);

	return 0;

fail:
	input_free_device(idev);
	return err;
}

static struct platform_driver asusec_kbd_driver = {
	.probe	= asusec_kbd_probe,
	.driver	= {
		.name	= "asusec-kbd",
		.owner	= THIS_MODULE,
	},
};

static int __init asusec_kbd_init(void)
{
	return platform_driver_register(&asusec_kbd_driver);
}

module_init(asusec_kbd_init);

MODULE_AUTHOR("Ilya Petrov <ilya.muromec@gmail.com>");
MODULE_DESCRIPTION("asusec keyboard driver");
MODULE_LICENSE("GPL");
