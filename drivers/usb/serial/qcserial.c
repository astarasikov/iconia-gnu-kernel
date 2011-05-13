/*
 * Qualcomm Serial USB driver
 *
 *	Copyright (c) 2008 QUALCOMM Incorporated.
 *	Copyright (c) 2009 Greg Kroah-Hartman <gregkh@suse.de>
 *	Copyright (c) 2009 Novell Inc.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 *
 */

#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/slab.h>
#include "usb-wwan.h"

#define DRIVER_AUTHOR "Qualcomm Inc"
#define DRIVER_DESC "Qualcomm USB Serial driver"

static int debug;
static bool export_non_qmi = true;

#define USB_DEVICE_DRVINFO(vid,pid,data) \
	USB_DEVICE(vid,pid), .driver_info = data

enum {
	GOBI2K = 0,
	GOBI3K = 1,
};

static const struct usb_device_id id_table[] = {
	{USB_DEVICE_DRVINFO(0x05c6, 0x9211, GOBI2K)},	/* Acer Gobi QDL device */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9212, GOBI2K)},	/* Acer Gobi Modem Device */
	{USB_DEVICE_DRVINFO(0x03f0, 0x1f1d, GOBI2K)},	/* HP un2400 Gobi Modem Device */
	{USB_DEVICE_DRVINFO(0x03f0, 0x201d, GOBI2K)},	/* HP un2400 Gobi QDL Device */
	{USB_DEVICE_DRVINFO(0x04da, 0x250d, GOBI2K)},	/* Panasonic Gobi Modem device */
	{USB_DEVICE_DRVINFO(0x04da, 0x250c, GOBI2K)},	/* Panasonic Gobi QDL device */
	{USB_DEVICE_DRVINFO(0x413c, 0x8172, GOBI2K)},	/* Dell Gobi Modem device */
	{USB_DEVICE_DRVINFO(0x413c, 0x8171, GOBI2K)},	/* Dell Gobi QDL device */
	{USB_DEVICE_DRVINFO(0x1410, 0xa001, GOBI2K)},	/* Novatel Gobi Modem device */
	{USB_DEVICE_DRVINFO(0x1410, 0xa008, GOBI2K)},	/* Novatel Gobi QDL device */
	{USB_DEVICE_DRVINFO(0x1410, 0xa010, GOBI2K)},	/* Novatel Gobi QDL device */
	{USB_DEVICE_DRVINFO(0x1410, 0xa011, GOBI2K)},	/* Novatel Gobi QDL device */
	{USB_DEVICE_DRVINFO(0x1410, 0xa012, GOBI2K)},	/* Novatel Gobi QDL device */
	{USB_DEVICE_DRVINFO(0x1410, 0xa013, GOBI2K)},	/* Novatel Gobi QDL device */
	{USB_DEVICE_DRVINFO(0x1410, 0xa014, GOBI2K)},	/* Novatel Gobi QDL device */
	{USB_DEVICE_DRVINFO(0x0b05, 0x1776, GOBI2K)},	/* Asus Gobi Modem device */
	{USB_DEVICE_DRVINFO(0x0b05, 0x1774, GOBI2K)},	/* Asus Gobi QDL device */
	{USB_DEVICE_DRVINFO(0x19d2, 0xfff3, GOBI2K)},	/* ONDA Gobi Modem device */
	{USB_DEVICE_DRVINFO(0x19d2, 0xfff2, GOBI2K)},	/* ONDA Gobi QDL device */
	{USB_DEVICE_DRVINFO(0x1557, 0x0a80, GOBI2K)},	/* OQO Gobi QDL device */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9001, GOBI2K)},   /* Generic Gobi Modem device */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9002, GOBI2K)},	/* Generic Gobi Modem device */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9202, GOBI2K)},	/* Generic Gobi Modem device */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9203, GOBI2K)},	/* Generic Gobi Modem device */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9222, GOBI2K)},	/* Generic Gobi Modem device */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9008, GOBI2K)},	/* Generic Gobi QDL device */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9201, GOBI2K)},	/* Generic Gobi QDL device */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9221, GOBI2K)},	/* Generic Gobi QDL device */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9231, GOBI2K)},	/* Generic Gobi QDL device */
	{USB_DEVICE_DRVINFO(0x1f45, 0x0001, GOBI2K)},	/* Unknown Gobi QDL device */
	{USB_DEVICE_DRVINFO(0x413c, 0x8185, GOBI2K)},	/* Dell Gobi 2000 QDL device (N0218, VU936) */
	{USB_DEVICE_DRVINFO(0x413c, 0x8186, GOBI2K)},	/* Dell Gobi 2000 Modem device (N0218, VU936) */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9224, GOBI2K)},	/* Sony Gobi 2000 QDL device (N0279, VU730) */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9225, GOBI2K)},	/* Sony Gobi 2000 Modem device (N0279, VU730) */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9244, GOBI2K)},	/* Samsung Gobi 2000 QDL device (VL176) */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9245, GOBI2K)},	/* Samsung Gobi 2000 Modem device (VL176) */
	{USB_DEVICE_DRVINFO(0x03f0, 0x241d, GOBI2K)},	/* HP Gobi 2000 QDL device (VP412) */
	{USB_DEVICE_DRVINFO(0x03f0, 0x251d, GOBI2K)},	/* HP Gobi 2000 Modem device (VP412) */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9214, GOBI2K)},	/* Acer Gobi 2000 QDL device (VP413) */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9215, GOBI2K)},	/* Acer Gobi 2000 Modem device (VP413) */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9264, GOBI2K)},	/* Asus Gobi 2000 QDL device (VR305) */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9265, GOBI2K)},	/* Asus Gobi 2000 Modem device (VR305) */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9234, GOBI2K)},	/* Top Global Gobi 2000 QDL device (VR306) */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9235, GOBI2K)},	/* Top Global Gobi 2000 Modem device (VR306) */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9274, GOBI2K)},	/* iRex Technologies Gobi 2000 QDL device (VR307) */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9275, GOBI2K)},	/* iRex Technologies Gobi 2000 Modem device (VR307) */
	{USB_DEVICE_DRVINFO(0x1199, 0x9000, GOBI2K)},	/* Sierra Wireless Gobi 2000 QDL device (VT773) */
	{USB_DEVICE_DRVINFO(0x1199, 0x9001, GOBI2K)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{USB_DEVICE_DRVINFO(0x1199, 0x9002, GOBI2K)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{USB_DEVICE_DRVINFO(0x1199, 0x9003, GOBI2K)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{USB_DEVICE_DRVINFO(0x1199, 0x9004, GOBI2K)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{USB_DEVICE_DRVINFO(0x1199, 0x9005, GOBI2K)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{USB_DEVICE_DRVINFO(0x1199, 0x9006, GOBI2K)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{USB_DEVICE_DRVINFO(0x1199, 0x9007, GOBI2K)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{USB_DEVICE_DRVINFO(0x1199, 0x9008, GOBI2K)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{USB_DEVICE_DRVINFO(0x1199, 0x9009, GOBI2K)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{USB_DEVICE_DRVINFO(0x1199, 0x900a, GOBI2K)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{USB_DEVICE_DRVINFO(0x16d8, 0x8001, GOBI2K)},	/* CMDTech Gobi 2000 QDL device (VU922) */
	{USB_DEVICE_DRVINFO(0x16d8, 0x8002, GOBI2K)},	/* CMDTech Gobi 2000 Modem device (VU922) */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9204, GOBI2K)},	/* Gobi 2000 QDL device */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9205, GOBI2K)},	/* Gobi 2000 Modem device */
	{USB_DEVICE_DRVINFO(0x05c6, 0x9208, GOBI2K)},	/* Gobi 2000 QDL device */
	{USB_DEVICE_DRVINFO(0x05c6, 0x920B, GOBI2K)},	/* Gobi 2000 Modem device */

	{USB_DEVICE_DRVINFO(0x05c6, 0x920c, GOBI3K)},	/* Gobi 3000 QDL */
	{USB_DEVICE_DRVINFO(0x05c6, 0x920d, GOBI3K)},	/* Gobi 3000 Composite */
	{USB_DEVICE_DRVINFO(0x1410, 0xa020, GOBI3K)},   /* Novatel Gobi 3000 QDL */
	{USB_DEVICE_DRVINFO(0x1410, 0xa021, GOBI3K)},	/* Novatel Gobi 3000 Composite */
	{USB_DEVICE_DRVINFO(0x413c, 0x8193, GOBI3K)},	/* Dell Gobi 3000 QDL */
	{USB_DEVICE_DRVINFO(0x413c, 0x8194, GOBI3K)},	/* Dell Gobi 3000 Composite */
	{USB_DEVICE_DRVINFO(0x12D1, 0x14F0, GOBI3K)},	/* Sony Gobi 3000 QDL */
	{USB_DEVICE_DRVINFO(0x12D1, 0x14F1, GOBI3K)},	/* Sony Gobi 3000 Composite */
	{ }				/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver qcdriver = {
	.name			= "qcserial",
	.probe			= usb_serial_probe,
	.disconnect		= usb_serial_disconnect,
	.id_table		= id_table,
	.suspend		= usb_serial_suspend,
	.resume			= usb_serial_resume,
	.supports_autosuspend	= true,
};

static int qcprobe(struct usb_serial *serial, const struct usb_device_id *id)
{
	struct usb_wwan_intf_private *data;
	struct usb_host_interface *intf = serial->interface->cur_altsetting;
	int retval = -ENODEV;
	__u8 nintf;
	__u8 ifnum;

	dbg("%s", __func__);

	nintf = serial->dev->actconfig->desc.bNumInterfaces;
	dbg("Num Interfaces = %d", nintf);
	ifnum = intf->desc.bInterfaceNumber;
	dbg("This Interface = %d", ifnum);

	data = serial->private = kzalloc(sizeof(struct usb_wwan_intf_private),
					 GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	spin_lock_init(&data->susp_lock);

	if (nintf == 1) {
		/* QDL mode */
		/* Gobi 2000 has a single altsetting, older ones have two */
		if (serial->interface->num_altsetting == 2)
			intf = &serial->interface->altsetting[1];
		else if (serial->interface->num_altsetting > 2) {
			printk(KERN_INFO "too many altsettings: %u", serial->interface->num_altsetting);
			return -ENODEV;
		}

		if (intf->desc.bNumEndpoints == 2 &&
		    usb_endpoint_is_bulk_in(&intf->endpoint[0].desc) &&
		    usb_endpoint_is_bulk_out(&intf->endpoint[1].desc)) {
			dbg("QDL port found");

			if (serial->interface->num_altsetting == 1)
				return 0;

			retval = usb_set_interface(serial->dev, ifnum, 1);
			if (retval < 0) {
				dev_err(&serial->dev->dev,
					"Could not set interface, error %d\n",
					retval);
				retval = -ENODEV;
				kfree(data);
			}
			return retval;
		}
	} else {
		/* Composite mode */
		if (   ifnum == 2
		    || (id->driver_info == GOBI3K && ifnum == 3)
		    || (export_non_qmi && ifnum != 0)) {
			dbg("Exporting interface %d", ifnum);
			retval = usb_set_interface(serial->dev, ifnum, 0);
			if (retval < 0) {
				dev_err(&serial->dev->dev,
					"Could not set interface, error %d\n",
					retval);
				retval = -ENODEV;
				kfree(data);
			}
			return retval;
		} else if (ifnum==3) {
			/*
			 * NMEA (serial line 9600 8N1)
			 * # echo "\$GPS_START" > /dev/ttyUSBx
			 * # echo "\$GPS_STOP"  > /dev/ttyUSBx
			 */
			dbg("NMEA GPS interface found");
			retval = usb_set_interface(serial->dev, ifnum, 0);
			if (retval < 0) {
				dev_err(&serial->dev->dev,
					"Could not set interface, error %d\n",
					retval);
				retval = -ENODEV;
			}
		}
		dev_err(&serial->dev->dev,
			"unknown number of interfaces: %d\n", nintf);
		kfree(data);
		return -ENODEV;
	}

	return retval;
}

static struct usb_serial_driver qcdevice = {
	.driver = {
		.owner     = THIS_MODULE,
		.name      = "qcserial",
	},
	.description         = "Qualcomm USB modem",
	.id_table            = id_table,
	.usb_driver          = &qcdriver,
	.num_ports           = 1,
	.probe               = qcprobe,
	.open		     = usb_wwan_open,
	.close		     = usb_wwan_close,
	.write		     = usb_wwan_write,
	.write_room	     = usb_wwan_write_room,
	.chars_in_buffer     = usb_wwan_chars_in_buffer,
	.attach		     = usb_wwan_startup,
	.disconnect	     = usb_wwan_disconnect,
	.release	     = usb_wwan_release,
#ifdef CONFIG_PM
	.suspend	     = usb_wwan_suspend,
	.resume		     = usb_wwan_resume,
#endif
};

static int __init qcinit(void)
{
	int retval;

	printk(KERN_INFO "qcserial: loaded");

	retval = usb_serial_register(&qcdevice);
	if (retval)
		return retval;

	retval = usb_register(&qcdriver);
	if (retval) {
		usb_serial_deregister(&qcdevice);
		return retval;
	}

	return 0;
}

static void __exit qcexit(void)
{
	usb_deregister(&qcdriver);
	usb_serial_deregister(&qcdevice);
}

module_init(qcinit);
module_exit(qcexit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL v2");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
module_param(export_non_qmi, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(export_non_qmi, "Export all non-QMI interfaces");
