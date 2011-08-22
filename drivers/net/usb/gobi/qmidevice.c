/* qmidevice.c - gobi QMI device
 * Copyright (c) 2010, Code Aurora Forum. All rights reserved.

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "buffer.h"
#include "qmidevice.h"
#include "qcusbnet.h"

#include <linux/compat.h>

struct readreq {
	struct list_head node;
	void *data;
	u16 tid;
	u16 size;
};

struct notifyreq {
	struct list_head node;
	void (*func)(struct qcusbnet *, u16, void *);
	u16  cid;
	u16  tid;
	void *data;
};

struct client {
	struct list_head node;
	u16 cid;
	struct list_head reads;
	struct list_head notifies;
	struct list_head urbs;
};

struct urbsetup {
	u8 type;
	u8 code;
	u16 value;
	u16 index;
	u16 len;
};

struct qmihandle {
	u16 cid;
	struct qcusbnet *dev;
};

static int qcusbnet2k_fwdelay;

static bool device_valid(struct qcusbnet *dev);
static struct client *client_bycid(struct qcusbnet *dev, u16 cid);
static bool client_addread(struct qcusbnet *dev, u16 cid, u16 tid, void *data, u16 size);
static bool client_delread(struct qcusbnet *dev, u16 cid, u16 tid, void **data, u16 *size);
static bool client_addnotify(struct qcusbnet *dev, u16 cid, u16 tid,
			     void (*hook)(struct qcusbnet *, u16 cid, void *),
			     void *data);
static struct notifyreq *client_remove_notify(struct client *client, u16 tid);
static void client_notify_and_free(struct qcusbnet *dev,
				   struct notifyreq *notify);
static void client_notify_list(struct qcusbnet *dev,
			       struct list_head *notifies);
static bool client_addurb(struct qcusbnet *dev, u16 cid, struct urb *urb);
static struct urb *client_delurb(struct qcusbnet *dev, u16 cid,
				 struct urb *urb);

static int resubmit_int_urb(struct urb *urb);

static int devqmi_open(struct inode *inode, struct file *file);
static long devqmi_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
#ifdef CONFIG_COMPAT
static int devqmi_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
#endif
static int devqmi_release(struct inode *inode, struct file *file);
static ssize_t devqmi_read(struct file *file, char __user *buf, size_t size,
			   loff_t *pos);
static ssize_t devqmi_write(struct file *file, const char __user *buf,
			    size_t size, loff_t *pos);

static bool qmi_ready(struct qcusbnet *dev, u16 timeout);
static void wds_callback(struct qcusbnet *dev, u16 cid, void *data);
static int setup_wds_callback(struct qcusbnet *dev);
static int qmidms_getmeid(struct qcusbnet *dev);

#define IOCTL_QMI_GET_SERVICE_FILE      (0x8BE0 + 1)
#define IOCTL_QMI_GET_DEVICE_VIDPID     (0x8BE0 + 2)
#define IOCTL_QMI_GET_DEVICE_MEID       (0x8BE0 + 3)
#define IOCTL_QMI_CLOSE		 (0x8BE0 + 4)
#define CDC_GET_ENCAPSULATED_RESPONSE	0x01A1ll
#define CDC_CONNECTION_SPEED_CHANGE	0x08000000002AA1ll

static const struct file_operations devqmi_fops = {
	.owner   = THIS_MODULE,
	.read    = devqmi_read,
	.write   = devqmi_write,
	.unlocked_ioctl   = devqmi_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = devqmi_compat_ioctl,
#endif
	.open    = devqmi_open,
	.release = devqmi_release,
};

#ifdef CONFIG_SMP
static inline void assert_locked(struct qcusbnet *dev)
{
	BUG_ON(!spin_is_locked(&dev->qmi.clients_lock));
}
#else
static inline void assert_locked(struct qcusbnet *dev)
{

}
#endif

static bool device_valid(struct qcusbnet *dev)
{
	return dev && dev->valid;
}

void qc_setdown(struct qcusbnet *dev, u8 reason)
{
	set_bit(reason, &dev->down);
	netif_carrier_off(dev->usbnet->net);
}

void qc_cleardown(struct qcusbnet *dev, u8 reason)
{
	clear_bit(reason, &dev->down);
	if (!dev->down)
		netif_carrier_on(dev->usbnet->net);
}

bool qc_isdown(struct qcusbnet *dev, u8 reason)
{
	return test_bit(reason, &dev->down);
}

static int resubmit_int_urb(struct urb *urb)
{
	int status;
	int interval;

	BUG_ON(!urb);
	BUG_ON(!urb->dev);

	interval = urb->dev->speed == USB_SPEED_HIGH ? 7 : 3;
	usb_fill_int_urb(urb, urb->dev, urb->pipe, urb->transfer_buffer,
			 urb->transfer_buffer_length, urb->complete,
			 urb->context, interval);
	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status)
		GOBI_ERROR("failed to resubmit int urb: %d", status);
	return status;
}

static void read_callback(struct urb *urb)
{
	struct list_head *node;
	int result;
	u16 cid;
	struct client *client;
	void *data;
	void *copy;
	u16 size;
	struct qcusbnet *dev;
	unsigned long flags;
	u16 tid;
	LIST_HEAD(notifies);

	BUG_ON(!urb);

	dev = urb->context;
	if (!device_valid(dev)) {
		GOBI_ERROR("invalid device");
		return;
	}

	if (urb->status) {
		GOBI_ERROR("urb status = %d", urb->status);
		resubmit_int_urb(dev->qmi.inturb);
		return;
	}

	GOBI_DEBUG("read %d bytes", urb->actual_length);

	data = urb->transfer_buffer;
	size = urb->actual_length;

	if (gobi_debug >= 2)
		print_hex_dump(KERN_INFO, "gobi-read: ", DUMP_PREFIX_OFFSET,
			       16, 1, data, size, true);

	result = qmux_parse(&cid, data, size);
	if (result < 0) {
		GOBI_ERROR("failed to parse read: %d", result);
		resubmit_int_urb(dev->qmi.inturb);
		return;
	}

	if (size < result + 3) {
		GOBI_ERROR("data buffer too small (%d < %d)", size, result + 3);
		resubmit_int_urb(dev->qmi.inturb);
		return;
	}

	if (cid == QMICTL)
		tid = *(u8 *)(data + result + 1);
	else
		tid = *(u16 *)(data + result + 1);
	spin_lock_irqsave(&dev->qmi.clients_lock, flags);
	list_for_each(node, &dev->qmi.clients) {
		client = list_entry(node, struct client, node);
		if (client->cid == cid || (client->cid | 0xff00) == cid) {
			struct notifyreq *notify;

			copy = kmalloc(size, GFP_ATOMIC);
			memcpy(copy, data, size);
			if (!client_addread(dev, client->cid, tid, copy, size)) {
				GOBI_ERROR("failed to add read; discarding "
					   "read of cid=0x%04x tid=0x%04x",
					   cid, tid);
				kfree(copy);
				break;
			}

			notify = client_remove_notify(client, tid);
			if (notify)
				list_add(&notify->node, &notifies);
			if (cid >> 8 != 0xff)
				break;
		}
	}
	spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);

	client_notify_list(dev, &notifies);
	resubmit_int_urb(dev->qmi.inturb);
}

static void int_callback(struct urb *urb)
{
	int status;
	struct qcusbnet *dev = (struct qcusbnet *)urb->context;

	if (!device_valid(dev)) {
		GOBI_WARN("invalid device");
		return;
	}

	if (urb->status) {
		GOBI_ERROR("urb status = %d", urb->status);
		if (urb->status != -EOVERFLOW)
			return;
	} else {
		if ((urb->actual_length == 8) &&
		    (*(u64 *)urb->transfer_buffer == CDC_GET_ENCAPSULATED_RESPONSE)) {
			usb_fill_control_urb(dev->qmi.readurb, dev->usbnet->udev,
					     usb_rcvctrlpipe(dev->usbnet->udev, 0),
					     (unsigned char *)dev->qmi.readsetup,
					     dev->qmi.readbuf,
					     DEFAULT_READ_URB_LENGTH,
					     read_callback, dev);
			status = usb_submit_urb(dev->qmi.readurb, GFP_ATOMIC);
			if (status == 0) {
				/* Do not resubmit the int_urb because
				 * it will be resubmitted in read_callback */
				return;
			}
			GOBI_ERROR("failed to submit read urb: %d", status);
		} else if ((urb->actual_length == 16) &&
			   (*(u64 *)urb->transfer_buffer == CDC_CONNECTION_SPEED_CHANGE)) {
			/* if upstream or downstream is 0, stop traffic.
			 * Otherwise resume it */
			if ((*(u32 *)(urb->transfer_buffer + 8) == 0) ||
			    (*(u32 *)(urb->transfer_buffer + 12) == 0)) {
				qc_setdown(dev, DOWN_CDC_CONNECTION_SPEED);
				GOBI_DEBUG("traffic stopping due to "
					   "CONNECTION_SPEED_CHANGE");
			} else {
				qc_cleardown(dev, DOWN_CDC_CONNECTION_SPEED);
				GOBI_DEBUG("resuming traffic due to "
					   "CONNECTION_SPEED_CHANGE");
			}
		} else {
			GOBI_ERROR("ignoring invalid int urb");
			if (gobi_debug >= 1)
				print_hex_dump(KERN_INFO, "gobi-int: ",
					       DUMP_PREFIX_OFFSET, 16, 1,
					       urb->transfer_buffer,
					       urb->actual_length, true);
		}
	}

	resubmit_int_urb(dev->qmi.inturb);
	return;
}

int qc_startread(struct qcusbnet *dev)
{
	int interval;
	int status;

	if (!device_valid(dev)) {
		GOBI_ERROR("invalid device");
		return -ENXIO;
	}

	dev->qmi.readurb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->qmi.readurb) {
		GOBI_ERROR("failed to allocate read urb");
		return -ENOMEM;
	}

	dev->qmi.inturb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->qmi.inturb) {
		GOBI_ERROR("failed to allocate int urb");
		usb_free_urb(dev->qmi.readurb);
		return -ENOMEM;
	}

	dev->qmi.readbuf = kmalloc(DEFAULT_READ_URB_LENGTH, GFP_KERNEL);
	if (!dev->qmi.readbuf) {
		GOBI_ERROR("failed to allocate read buffer");
		usb_free_urb(dev->qmi.readurb);
		usb_free_urb(dev->qmi.inturb);
		return -ENOMEM;
	}

	dev->qmi.intbuf = kmalloc(DEFAULT_READ_URB_LENGTH, GFP_KERNEL);
	if (!dev->qmi.intbuf) {
		GOBI_ERROR("failed to allocate int buffer");
		usb_free_urb(dev->qmi.readurb);
		usb_free_urb(dev->qmi.inturb);
		kfree(dev->qmi.readbuf);
		return -ENOMEM;
	}

	dev->qmi.readsetup = kmalloc(sizeof(*dev->qmi.readsetup), GFP_KERNEL);
	if (!dev->qmi.readsetup) {
		GOBI_ERROR("failed to allocate setup packet buffer");
		usb_free_urb(dev->qmi.readurb);
		usb_free_urb(dev->qmi.inturb);
		kfree(dev->qmi.readbuf);
		kfree(dev->qmi.intbuf);
		return -ENOMEM;
	}

	dev->qmi.readsetup->type = 0xA1;
	dev->qmi.readsetup->code = 1;
	dev->qmi.readsetup->value = 0;
	dev->qmi.readsetup->index = 0;
	dev->qmi.readsetup->len = DEFAULT_READ_URB_LENGTH;

	interval = (dev->usbnet->udev->speed == USB_SPEED_HIGH) ? 7 : 3;

	usb_fill_int_urb(dev->qmi.inturb, dev->usbnet->udev,
			 usb_rcvintpipe(dev->usbnet->udev, 0x81),
			 dev->qmi.intbuf, DEFAULT_READ_URB_LENGTH,
			 int_callback, dev, interval);
	status = usb_submit_urb(dev->qmi.inturb, GFP_KERNEL);
	if (status != 0)
		GOBI_ERROR("failed to submit int urb: %d", status);
	return status;
}

void qc_stopread(struct qcusbnet *dev)
{
	if (dev->qmi.readurb) {
		GOBI_DEBUG("killing read urb");
		usb_kill_urb(dev->qmi.readurb);
	}

	if (dev->qmi.inturb) {
		GOBI_DEBUG("killing int urb");
		usb_kill_urb(dev->qmi.inturb);
	}

	kfree(dev->qmi.readsetup);
	dev->qmi.readsetup = NULL;
	kfree(dev->qmi.readbuf);
	dev->qmi.readbuf = NULL;
	kfree(dev->qmi.intbuf);
	dev->qmi.intbuf = NULL;

	usb_free_urb(dev->qmi.readurb);
	dev->qmi.readurb = NULL;
	usb_free_urb(dev->qmi.inturb);
	dev->qmi.inturb = NULL;
}

static int read_async(struct qcusbnet *dev, u16 cid, u16 tid,
		      void (*hook)(struct qcusbnet *, u16, void *),
		      void *data)
{
	struct list_head *node;
	struct client *client;
	struct readreq *readreq;

	unsigned long flags;

	if (!device_valid(dev)) {
		GOBI_ERROR("invalid device (cid=0x%04x, tid=0x%04x)", cid, tid);
		return -ENXIO;
	}

	spin_lock_irqsave(&dev->qmi.clients_lock, flags);

	client = client_bycid(dev, cid);
	if (!client) {
		spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);
		GOBI_ERROR("could not find client (cid=0x%04x, tid=0x%04x)",
			   cid, tid);
		return -ENXIO;
	}

	list_for_each(node, &client->reads) {
		readreq = list_entry(node, struct readreq, node);
		if (!tid || tid == readreq->tid) {
			spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);
			hook(dev, cid, data);
			return 0;
		}
	}

	if (!client_addnotify(dev, cid, tid, hook, data)) {
		spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);
		GOBI_ERROR("failed to add notify (cid=0x%04x, tid=0x%04x)",
			   cid, tid);
		/* TODO(ttuttle): return error? */
		return 0;
	}

	spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);

	return 0;
}

static void upsem(struct qcusbnet *dev, u16 cid, void *data)
{
	GOBI_DEBUG("(cid=0x%04x)", cid);
	up((struct semaphore *)data);
}

static int read_sync(struct qcusbnet *dev, void **buf, u16 cid, u16 tid)
{
	struct list_head *node;
	int result;
	struct client *client;
	struct notifyreq *notify;
	struct semaphore sem;
	void *data;
	unsigned long flags;
	u16 size;

	if (!device_valid(dev)) {
		GOBI_ERROR("invalid device");
		return -ENXIO;
	}

	spin_lock_irqsave(&dev->qmi.clients_lock, flags);

	client = client_bycid(dev, cid);
	if (!client) {
		spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);
		GOBI_ERROR("could not find client (cid=0x%04x, tid=0x%04x)",
			   cid, tid);
		return -ENXIO;
	}

	while (!client_delread(dev, cid, tid, &data, &size)) {
		sema_init(&sem, 0);
		if (!client_addnotify(dev, cid, tid, upsem, &sem)) {
			spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);
			GOBI_WARN("failed to register for notification "
				  "(cid=0x%04x, tid=0x%04x)", cid, tid);
			return -EFAULT;
		}

		spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);

		result = down_interruptible(&sem);
		if (result) {
			GOBI_WARN("interrupted: %d (cid=0x%04x, tid=0x%04x)",
			    result, cid, tid);

			spin_lock_irqsave(&dev->qmi.clients_lock, flags);
			list_for_each(node, &client->notifies) {
				notify = list_entry(node, struct notifyreq, node);
				if (notify->data == &sem) {
					list_del(&notify->node);
					kfree(notify);
					break;
				}
			}
			spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);

			return -EINTR;
		}

		if (!device_valid(dev)) {
			GOBI_ERROR("invalid device (cid=0x%04x, tid=0x%04x)",
				   cid, tid);
			return -ENXIO;
		}

		spin_lock_irqsave(&dev->qmi.clients_lock, flags);
	}

	spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);
	*buf = data;
	return size;
}

struct writectx {
	struct semaphore sem;
	struct kref ref;
	struct urbsetup setup;
	struct buffer *data;
};

static void writectx_release(struct kref *ref)
{
	struct writectx *ctx = container_of(ref, struct writectx, ref);
	buffer_put(ctx->data);
	kfree(ctx);
}

static void write_callback(struct urb *urb)
{
	struct writectx *ctx = urb->context;

	GOBI_DEBUG("%p %d %d", ctx, urb->status, urb->actual_length);
	up(&ctx->sem);
	kref_put(&ctx->ref, writectx_release);
}

/** @brief Synchronously (probably) sends a data buffer to the card.
 *
 *  @param dev
 *  @param data_buf Borrowed reference to data buffer
 *  @param cid Client ID
 */
static int write_sync(struct qcusbnet *dev, struct buffer *data_buf, u16 cid)
{
	int result;
	struct writectx *ctx;
	struct urb *urb;
	struct urb *removed_urb;
	unsigned long flags;

	if (!device_valid(dev)) {
		GOBI_ERROR("invalid device (cid=0x%04x)", cid);
		return -ENXIO;
	}

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		GOBI_ERROR("failed to allocate write context (cid=0x%04x)",
			   cid);
		return -ENOMEM;
	}

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		kfree(ctx);
		GOBI_ERROR("failed to allocate urb (cid=0x%04x)", cid);
		return -ENOMEM;
	}

	result = qmux_fill(cid, data_buf);
	if (result < 0) {
		usb_free_urb(urb);
		kfree(ctx);
		return result;
	}

	/* CDC Send Encapsulated Request packet */
	ctx->setup.type = 0x21;
	ctx->setup.code = 0;
	ctx->setup.value = 0;
	ctx->setup.index = 0;
	ctx->setup.len = buffer_size(data_buf);

	usb_fill_control_urb(urb, dev->usbnet->udev,
			     usb_sndctrlpipe(dev->usbnet->udev, 0),
			     (unsigned char *)&ctx->setup,
			     buffer_data(data_buf), buffer_size(data_buf),
			     NULL, dev);

	if (gobi_debug >= 2)
		print_hex_dump(KERN_INFO,  "gobi-write: ", DUMP_PREFIX_OFFSET,
			       16, 1, buffer_data(data_buf),
			       buffer_size(data_buf), true);

	sema_init(&ctx->sem, 0);
	kref_init(&ctx->ref);
	kref_get(&ctx->ref);	/* get a ref to the context for the urb */

	/* The context now has another ref to the data buffer */
	buffer_get(data_buf);
	ctx->data = data_buf;

	urb->complete = write_callback;
	urb->context = ctx;

	result = usb_autopm_get_interface(dev->iface);
	if (result < 0) {
		GOBI_ERROR("unable to resume interface: %d (cid=0x%04x)",
			   result, cid);
		if (result == -EPERM) {
			qc_suspend(dev->iface, PMSG_SUSPEND);
		}
		usb_free_urb(urb);
		kref_put(&ctx->ref, writectx_release);
		return result;
	}

	spin_lock_irqsave(&dev->qmi.clients_lock, flags);
	if (!client_addurb(dev, cid, urb)) {
		usb_free_urb(urb);
		spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);
		GOBI_ERROR("failed to add urb (cid=0x%04x)", cid);
		usb_autopm_put_interface(dev->iface);
		kref_put(&ctx->ref, writectx_release);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);

	result = usb_submit_urb(urb, GFP_KERNEL);
	if (result < 0)	{
		GOBI_ERROR("failed to submit urb: %d (cid=0x%04x)",
			   result, cid);

		spin_lock_irqsave(&dev->qmi.clients_lock, flags);
		removed_urb = client_delurb(dev, cid, urb);
		if (urb == removed_urb)
			usb_free_urb(urb);
		else
			GOBI_ERROR("didn't get write urb back (cid=0x%04x)",
				   cid);
		spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);

		usb_autopm_put_interface(dev->iface);
		kref_put(&ctx->ref, writectx_release);
		return result;
	}

	result = down_interruptible(&ctx->sem);
	kref_put(&ctx->ref, writectx_release);
	if (!device_valid(dev)) {
		GOBI_ERROR("invalid device (cid=0x%04x)", cid);
		return -ENXIO;
	}

	usb_autopm_put_interface(dev->iface);

	spin_lock_irqsave(&dev->qmi.clients_lock, flags);
	if (client_delurb(dev, cid, urb) != urb) {
		GOBI_ERROR("didn't get write urb back (cid=0x%04x)", cid);
		spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);

	if (!result) {
		if (!urb->status) {
			result = buffer_size(data_buf);
		} else {
			GOBI_ERROR("urb status = %d (cid=0x%04x)",
				   urb->status, cid);
			result = urb->status;
		}
	} else {
		GOBI_ERROR("interrupted: %d (cid=0x%04x)", result, cid);
		GOBI_ERROR("(modem may need to be reset)");
	}

	usb_free_urb(urb);
	return result;
}

static int cid_alloc(struct qcusbnet *dev, u8 type)
{
	struct buffer *wbuf;
	void *rbuf;
	u16 cid;
	u8 tid;
	u16 size;
	int result;

	do {
		tid = atomic_add_return(1, &dev->qmi.qmitid);
	} while (tid == 0);

	wbuf = qmictl_new_getcid(tid, type);
	if (!wbuf) {
		GOBI_ERROR("failed to create getcid request");
		return -ENOMEM;
	}

	result = write_sync(dev, wbuf, QMICTL);
	buffer_put(wbuf);
	if (result < 0) {
		GOBI_ERROR("failed to write getcid request: %d", result);
		return result;
	}

	result = read_sync(dev, &rbuf, QMICTL, tid);
	if (result < 0) {
		GOBI_ERROR("failed to read alloccid response: %d", result);
		return result;
	}

	size = result;

	result = qmictl_alloccid_resp(rbuf, size, &cid);
	kfree(rbuf);
	if (result < 0) {
		GOBI_WARN("failed to parse alloccid response: %d", result);
		return result;
	}

	return cid;
}

static int cid_free(struct qcusbnet *dev, u16 cid)
{
	struct buffer *wbuf;
	void *rbuf;
	u8 tid;
	u16 size;
	int result;

	do {
		tid = atomic_add_return(1, &dev->qmi.qmitid);
	} while (tid == 0);

	wbuf = qmictl_new_releasecid(tid, cid);
	if (!wbuf) {
		GOBI_ERROR("failed to create releasecid request");
		return -ENOMEM;
	}

	result = write_sync(dev, wbuf, QMICTL);
	buffer_put(wbuf);
	if (result < 0) {
		GOBI_ERROR("failed to write releasecid request: %d", result);
		return result;
	}

	result = read_sync(dev, &rbuf, QMICTL, tid);
	if (result < 0) {
		GOBI_ERROR("failed to read freecid response: %d", result);
		return result;
	}

	size = result;

	result = qmictl_freecid_resp(rbuf, size);
	kfree(rbuf);
	if (result < 0) {
		GOBI_ERROR("failed to parse freecid response: %d", result);
		return result;
	}

	return 0;
}

static int client_alloc(struct qcusbnet *dev, u8 type)
{
	u16 cid;
	struct client *client;
	int result;
	unsigned long flags;

	if (!device_valid(dev)) {
		GOBI_ERROR("invalid device (type=0x%02x)", type);
		return -ENXIO;
	}

	if (type) {
		result = cid_alloc(dev, type);
		if (result < 0) {
			GOBI_WARN("failed to allocate cid: %d (type=0x%02x)",
				  result, type);
			return result;
		}
		cid = result;
	} else {
		cid = 0;
	}

	spin_lock_irqsave(&dev->qmi.clients_lock, flags);

	if (client_bycid(dev, cid)) {
		spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);
		GOBI_ERROR("duplicate cid 0x%04x (type=0x%02x)", cid, type);
		return -ETOOMANYREFS;
	}

	client = kmalloc(sizeof(*client), GFP_ATOMIC);
	if (!client) {
		spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);
		GOBI_ERROR("failed to allocate client (type=0x%02x)", type);
		return -ENOMEM;
	}

	list_add_tail(&client->node, &dev->qmi.clients);
	client->cid = cid;
	INIT_LIST_HEAD(&client->reads);
	INIT_LIST_HEAD(&client->notifies);
	INIT_LIST_HEAD(&client->urbs);

	spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);

	return cid;
}

static void client_free(struct qcusbnet *dev, u16 cid)
{
	struct list_head *node, *tmp;
	struct client *client;
	struct urb *urb;
	void *data;
	u16 size;
	unsigned long flags;

	if (cid != QMICTL)
		cid_free(dev, cid);

	spin_lock_irqsave(&dev->qmi.clients_lock, flags);
	list_for_each_safe(node, tmp, &dev->qmi.clients) {
		struct notifyreq *notify;

		client = list_entry(node, struct client, node);
		if (client->cid != cid)
			continue;

		while ((notify = client_remove_notify(client, 0)) != NULL) {
			/* release lock during notification */
			spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);
			client_notify_and_free(dev, notify);
			spin_lock_irqsave(&dev->qmi.clients_lock, flags);
		}
		urb = client_delurb(dev, cid, NULL);
		while (urb != NULL) {
			usb_kill_urb(urb);
			usb_free_urb(urb);
			urb = client_delurb(dev, cid, NULL);
		}
		while (client_delread(dev, cid, 0, &data, &size))
			kfree(data);

		list_del(&client->node);
		kfree(client);
		break;
	}
	spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);
}

struct client *client_bycid(struct qcusbnet *dev, u16 cid)
{
	struct list_head *node;
	struct client *client;

	if (!device_valid(dev)) {
		GOBI_ERROR("invalid device");
		return NULL;
	}

	assert_locked(dev);

	list_for_each(node, &dev->qmi.clients) {
		client = list_entry(node, struct client, node);
		if (client->cid == cid)
			return client;
	}

	return NULL;
}

static bool client_addread(struct qcusbnet *dev, u16 cid, u16 tid, void *data,
			   u16 size)
{
	struct client *client;
	struct readreq *req;

	assert_locked(dev);

	client = client_bycid(dev, cid);
	if (!client) {
		GOBI_ERROR("failed to find client");
		return false;
	}

	req = kmalloc(sizeof(*req), GFP_ATOMIC);
	if (!req) {
		GOBI_ERROR("failed to allocate req");
		return false;
	}

	req->data = data;
	req->size = size;
	req->tid = tid;

	list_add_tail(&req->node, &client->reads);

	return true;
}

static bool client_delread(struct qcusbnet *dev, u16 cid, u16 tid, void **data,
			   u16 *size)
{
	struct client *client;
	struct readreq *req;
	struct list_head *node;

	assert_locked(dev);

	client = client_bycid(dev, cid);
	if (!client) {
		GOBI_WARN("failed to find client (cid=0x%04x, tid=0x%04x)",
			  cid, tid);
		return false;
	}

	list_for_each(node, &client->reads) {
		req = list_entry(node, struct readreq, node);
		if (!tid || tid == req->tid) {
			*data = req->data;
			*size = req->size;
			list_del(&req->node);
			kfree(req);
			return true;
		}

		GOBI_DEBUG("skipping 0x%04x data TID = %x", cid, req->tid);
	}

	GOBI_DEBUG("no read to delete (cid=0x%04x, tid=0x%04x)", cid, tid);
	return false;
}

static bool client_addnotify(struct qcusbnet *dev, u16 cid, u16 tid,
			     void (*hook)(struct qcusbnet *, u16, void *),
			     void *data)
{
	struct client *client;
	struct notifyreq *req;

	assert_locked(dev);

	client = client_bycid(dev, cid);
	if (!client) {
		GOBI_WARN("failed to find client (cid=0x%04x, tid=0x%04x)",
			  cid, tid);
		return false;
	}

	req = kmalloc(sizeof(*req), GFP_ATOMIC);
	if (!req) {
		GOBI_ERROR("failed to allocate req (cid=0x%04x, tid=0x%04x)",
			   cid, tid);
		return false;
	}

	list_add_tail(&req->node, &client->notifies);
	req->func = hook;
	req->data = data;
	req->cid = cid;
	req->tid = tid;

	return true;
}

static struct notifyreq *client_remove_notify(struct client *client, u16 tid)
{
	struct notifyreq *notify;
	struct list_head *node;

	list_for_each(node, &client->notifies) {
		notify = list_entry(node, struct notifyreq, node);
		if (!tid || !notify->tid || tid == notify->tid) {
			list_del(&notify->node);
			return notify;
		}
		GOBI_DEBUG("skipping data TID = %x", notify->tid);
	}
	GOBI_WARN("no notify to call (cid=0x%04x, tid=0x%04x)",
		  client->cid, tid);
	return NULL;
}

static void client_notify_and_free(struct qcusbnet *dev,
				   struct notifyreq *notify)
{
	if (notify->func)
		notify->func(dev, notify->cid, notify->data);
	kfree(notify);
}

static void client_notify_list(struct qcusbnet *dev,
			       struct list_head *notifies)
{
	struct list_head *node, *tmp;
	struct notifyreq *notify;

	/* The calling thread must not hold any qmidevice spinlocks */
	list_for_each_safe(node, tmp, notifies) {
		notify = list_entry(node, struct notifyreq, node);
		list_del(&notify->node);
		client_notify_and_free(dev, notify);
	}
}

static bool client_addurb(struct qcusbnet *dev, u16 cid, struct urb *urb)
{
	struct client *client;
	struct urbreq *req;

	assert_locked(dev);

	client = client_bycid(dev, cid);
	if (!client) {
		GOBI_ERROR("failed to find client (cid=0x%04x)", cid);
		return false;
	}

	req = kmalloc(sizeof(*req), GFP_ATOMIC);
	if (!req) {
		GOBI_ERROR("failed to allocate req (cid=0x%04x)", cid);
		return false;
	}

	req->urb = urb;
	list_add(&req->node, &client->urbs);

	return true;
}

static struct urb *client_delurb(struct qcusbnet *dev, u16 cid, struct urb *urb)
{
	struct client *client;
	struct urbreq *req;
	struct list_head *node;

	assert_locked(dev);

	client = client_bycid(dev, cid);
	if (!client) {
		GOBI_ERROR("failed to find client (cid=0x%04x)", cid);
		return NULL;
	}

	list_for_each(node, &client->urbs) {
		req = list_entry(node, struct urbreq, node);
		if (urb == NULL || req->urb == urb) {
			list_del(&req->node);
			urb = req->urb;
			kfree(req);
			return urb;
		}
	}
	if (urb) {
		/* Only warn if we didn't find a specific URB we were looking
		   for. */
		GOBI_ERROR("no matching urb to delete (cid=0x%04x)", cid);
	}
	return NULL;
}

static int devqmi_open(struct inode *inode, struct file *file)
{
	struct qmihandle *handle;
	struct qmidev *qmidev = container_of(inode->i_cdev, struct qmidev, cdev);
	struct qcusbnet *dev = container_of(qmidev, struct qcusbnet, qmi);

	/* We need an extra ref on the device per fd, since we stash a ref
	 * inside the handle. If qcusbnet_get() returns NULL, that means the
	 * device has been removed from the list - no new refs for us. */
	struct qcusbnet *ref = qcusbnet_get(dev);

	if (!ref)
		return -ENXIO;

	file->private_data = kmalloc(sizeof(struct qmihandle), GFP_KERNEL);
	if (!file->private_data) {
		GOBI_ERROR("failed to allocate struct qmihandle");
		return -ENOMEM;
	}

	handle = (struct qmihandle *)file->private_data;
	handle->cid = (u16)-1;
	handle->dev = ref;

	GOBI_DEBUG("%p %04x", handle, handle->cid);

	return 0;
}

static long devqmi_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int result;
	u32 vidpid;

	struct qmihandle *handle = (struct qmihandle *)file->private_data;

	GOBI_DEBUG("%p %04x %08x", handle, handle->cid, cmd);

	if (!device_valid(handle->dev)) {
		GOBI_WARN("invalid device");
		return -ENXIO;
	}

	if (handle->dev->dying) {
		GOBI_WARN("dying device");
		return -ENXIO;
	}

	switch (cmd) {

	case IOCTL_QMI_GET_SERVICE_FILE:
		GOBI_DEBUG("Setting up QMI for service %lu", arg);
		if (((u8)arg) == 0) {
			GOBI_WARN("cannot use QMICTL from userspace");
			return -EINVAL;
		}

		if (handle->cid != (u16)-1) {
			GOBI_WARN("cid already set");
			return -EBADR;
		}

		result = client_alloc(handle->dev, (u8)arg);
		if (result < 0) {
			GOBI_WARN("failed to allocate client: %d", result);
			return result;
		}

		handle->cid = result;

		return 0;

	/* Okay, all aboard the nasty hack express. If we don't have this
	 * ioctl() (and we just rely on userspace to close() the file
	 * descriptors), if userspace has any refs left to this fd (like, say, a
	 * pending read()), then the read might hang around forever. Userspace
	 * needs a way to cause us to kick people off those waitqueues before
	 * closing the fd for good.
	 *
	 * If this driver used workqueues, the correct approach here would
	 * instead be to make the file descriptor select()able, and then just
	 * use select() instead of aio in userspace (thus allowing us to get
	 * away with one thread total and avoiding the recounting mess
	 * altogether).
	 */
	case IOCTL_QMI_CLOSE:
		if (handle->cid == (u16)-1) {
			GOBI_WARN("no cid");
			return -EBADR;
		}

		file->private_data = NULL;
		client_free(handle->dev, handle->cid);
		kfree(handle);

		return 0;

	case IOCTL_QMI_GET_DEVICE_VIDPID:
		if (!arg) {
			GOBI_WARN("GET_DEVICE_VIDPID: bad VIDPID buffer");
			return -EINVAL;
		}

		if (!handle->dev->usbnet) {
			GOBI_ERROR("GET_DEVICE_VIDPID: dev->usbnet is NULL");
			return -ENOMEM;
		}

		if (!handle->dev->usbnet->udev) {
			GOBI_ERROR("GET_DEVICE_VIDPID: dev->usbnet->udev "
				   "is NULL");
			return -ENOMEM;
		}

		vidpid = ((le16_to_cpu(handle->dev->usbnet->udev->descriptor.idVendor) << 16)
			  + le16_to_cpu(handle->dev->usbnet->udev->descriptor.idProduct));

		result = copy_to_user((unsigned int *)arg, &vidpid, 4);
		if (result) {
			GOBI_WARN("GET_DEVICE_VIDPID: copy_to_user failed: %d",
				  result);
			return result;
		}

		return 0;

	case IOCTL_QMI_GET_DEVICE_MEID:
		if (!arg) {
			GOBI_WARN("GET_DEVICE_MEID: bad MEID buffer");
			return -EINVAL;
		}

		result = copy_to_user((unsigned int *)arg, &handle->dev->meid[0], 14);
		if (result) {
			GOBI_WARN("GET_DEVICE_MEID: copy_to_user failed: %d",
				  result);
			return result;
		}

		return 0;

	default:
		GOBI_WARN("I don't even know *how* to %08x!", cmd);
		return -EBADRQC;

	}
}

#ifdef CONFIG_COMPAT
static int devqmi_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return devqmi_ioctl(file, cmd, compat_ptr(arg));
}
#endif

static int devqmi_release(struct inode *inode, struct file *file)
{
	struct qmihandle *handle = (struct qmihandle *)file->private_data;

	if (handle) {
		file->private_data = NULL;
		if (handle->cid != (u16)-1)
			client_free(handle->dev, handle->cid);
		qcusbnet_put(handle->dev);
		kfree(handle);
	}

	return 0;
}

static ssize_t devqmi_read(struct file *file, char __user *buf, size_t size,
			   loff_t *pos)
{
	int result, status;
	void *data = NULL;
	void *smalldata;
	struct qmihandle *handle = (struct qmihandle *)file->private_data;

	if (!handle) {
		GOBI_WARN("handle is NULL");
		return -EBADF;
	}

	if (handle->dev->dying) {
		GOBI_WARN("dying device");
		return -ENXIO;
	}

	if (!device_valid(handle->dev)) {
		GOBI_WARN("invalid device");
		return -ENXIO;
	}

	if (handle->cid == (u16)-1) {
		GOBI_WARN("cid is not set");
		return -EBADR;
	}

	result = read_sync(handle->dev, &data, handle->cid, 0);
	if (result <= 0) {
		GOBI_WARN("read_sync failed: %d", result);
		return result;
	}

	result -= qmux_size;
	smalldata = data + qmux_size;

	if (result > size) {
		GOBI_WARN("read data is too large (%d > %d)", result, size);
		kfree(data);
		return -EOVERFLOW;
	}

	status = copy_to_user(buf, smalldata, result);
	if (status) {
		GOBI_ERROR("copy_to_user failed: %d", status);
		result = status;
	}

	kfree(data);
	return result;
}

static ssize_t devqmi_write(struct file *file, const char __user * buf,
			    size_t size, loff_t *pos)
{
	int status;
	struct buffer *wbuf;
	struct qmihandle *handle = (struct qmihandle *)file->private_data;

	if (!handle) {
		GOBI_WARN("handle is NULL");
		return -EBADF;
	}

	if (!device_valid(handle->dev)) {
		GOBI_WARN("invalid device");
		/* TODO(ttuttle): what the heck is this for: */
		file->f_op = file->f_dentry->d_inode->i_fop;
		return -ENXIO;
	}

	if (handle->cid == (u16)-1) {
		GOBI_WARN("cid is not set");
		return -EBADR;
	}

	if (size + qmux_size <= size) {
		GOBI_WARN("size too big");
		return -EINVAL;
	}

	wbuf = buffer_new(size + qmux_size);
	if (!wbuf) {
		GOBI_ERROR("failed to allocate buffer");
		return -ENOMEM;
	}
	status = copy_from_user(buffer_data(wbuf) + qmux_size, buf, size);
	if (status) {
		GOBI_ERROR("copy_from_user failed: %d", status);
		buffer_put(wbuf);
		return status;
	}

	status = write_sync(handle->dev, wbuf, handle->cid);
	buffer_put(wbuf);

	if (status == size + qmux_size)
		return size;
	return status;
}

int qc_register(struct qcusbnet *dev)
{
	int result;
	int qmiidx = 0;
	dev_t devno;
	char *name;

	dev->valid = true;
	dev->dying = false;
	result = client_alloc(dev, QMICTL);
	if (result) {
		dev->valid = false;
		return result;
	}
	atomic_set(&dev->qmi.qmitid, 1);

	result = qc_startread(dev);
	if (result) {
		dev->valid = false;
		return result;
	}

	if (!qmi_ready(dev, 30000)) {
		GOBI_ERROR("device unresponsive to QMI");
		return -ETIMEDOUT;
	}

	result = setup_wds_callback(dev);
	if (result) {
		dev->valid = false;
		return result;
	}

	result = qmidms_getmeid(dev);
	if (result) {
		dev->valid = false;
		return result;
	}

	result = alloc_chrdev_region(&devno, 0, 1, "qcqmi");
	if (result < 0)
		return result;

	cdev_init(&dev->qmi.cdev, &devqmi_fops);
	dev->qmi.cdev.owner = THIS_MODULE;
	dev->qmi.cdev.ops = &devqmi_fops;

	result = cdev_add(&dev->qmi.cdev, devno, 1);
	if (result) {
		GOBI_ERROR("failed to add cdev: %d", result);
		return result;
	}

	name = strstr(dev->usbnet->net->name, "usb");
	if (!name) {
		GOBI_ERROR("bad net name: %s", dev->usbnet->net->name);
		return -ENXIO;
	}
	name += strlen("usb");
	qmiidx = simple_strtoul(name, NULL, 10);
	if (qmiidx < 0) {
		GOBI_ERROR("bad minor number: %s", name);
		return -ENXIO;
	}

	printk(KERN_INFO "creating qcqmi%d", qmiidx);
	device_create(dev->qmi.devclass, &dev->iface->dev, devno, NULL, "qcqmi%d", qmiidx);

	dev->qmi.devnum = devno;
	return 0;
}

void qc_deregister(struct qcusbnet *dev)
{
	struct list_head *node, *tmp;
	struct client *client;

	dev->dying = true;
	list_for_each_safe(node, tmp, &dev->qmi.clients) {
		client = list_entry(node, struct client, node);
		client_free(dev, client->cid);
	}

	qc_stopread(dev);
	dev->valid = false;
	if (!IS_ERR(dev->qmi.devclass))
		device_destroy(dev->qmi.devclass, dev->qmi.devnum);
	cdev_del(&dev->qmi.cdev);
	unregister_chrdev_region(dev->qmi.devnum, 1);
}

static bool qmi_ready(struct qcusbnet *dev, u16 timeout)
{
	int result;
	struct buffer *wbuf;
	void *rbuf;
	u16 rbufsize;
	struct semaphore sem;
	u16 now;
	unsigned long flags;
	u8 tid;

	if (!device_valid(dev)) {
		GOBI_ERROR("invalid device");
		return -EFAULT;
	}


	for (now = 0; now < timeout; now += 100) {
		sema_init(&sem, 0);

		tid = atomic_add_return(1, &dev->qmi.qmitid);
		if (!tid)
			tid = atomic_add_return(1, &dev->qmi.qmitid);
		wbuf = qmictl_new_ready(tid);
		if (!wbuf)
			return -ENOMEM;

		result = read_async(dev, QMICTL, tid, upsem, &sem);
		if (result) {
			buffer_put(wbuf);
			return false;
		}

		write_sync(dev, wbuf, QMICTL);
		buffer_put(wbuf);

		msleep(100);
		if (!down_trylock(&sem)) {
			spin_lock_irqsave(&dev->qmi.clients_lock, flags);
			if (client_delread(dev,	QMICTL,	tid, &rbuf, &rbufsize)) {
				spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);
				kfree(rbuf);
				break;
			} else {
				spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);
			}
		} else {
			struct notifyreq *notify = NULL;
			struct client *client;

			spin_lock_irqsave(&dev->qmi.clients_lock, flags);
			client = client_bycid(dev, QMICTL);
			if (client)
				notify = client_remove_notify(client, tid);
			spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);
			if (notify)
				client_notify_and_free(dev, notify);
		}
	}

	if (now >= timeout)
		return false;

	GOBI_DEBUG("ready after %u milliseconds", now);

	/* 3580 and newer doesn't need a delay; older needs 5000ms */
	if (qcusbnet2k_fwdelay)
		msleep(qcusbnet2k_fwdelay * 1000);

	return true;
}

static void wds_callback(struct qcusbnet *dev, u16 cid, void *data)
{
	bool ret;
	int result;
	void *rbuf;
	u16 rbufsize;

	struct net_device_stats *stats = &(dev->usbnet->net->stats);

	struct qmiwds_stats dstats = {
		.txok      = (u32)-1,      .rxok = (u32)-1,
		.txerr     = (u32)-1,     .rxerr = (u32)-1,
		.txofl     = (u32)-1,     .rxofl = (u32)-1,
		.txbytesok = (u64)-1, .rxbytesok = (u64)-1,
	};
	unsigned long flags;

	if (!device_valid(dev)) {
		GOBI_ERROR("invalid device");
		return;
	}

	spin_lock_irqsave(&dev->qmi.clients_lock, flags);
	ret = client_delread(dev, cid, 0, &rbuf, &rbufsize);
	spin_unlock_irqrestore(&dev->qmi.clients_lock, flags);

	if (!ret) {
		GOBI_ERROR("no read found");
		return;
	}

	dstats.linkstate = !qc_isdown(dev, DOWN_NO_NDIS_CONNECTION);
	dstats.reconfigure = false;

	result = qmiwds_event_resp(rbuf, rbufsize, &dstats);
	if (result < 0) {
		GOBI_ERROR("failed to parse event response");
	} else {
		if (dstats.txofl != (u32)-1)
			stats->tx_fifo_errors = dstats.txofl;

		if (dstats.rxofl != (u32)-1)
			stats->rx_fifo_errors = dstats.rxofl;

		if (dstats.txerr != (u32)-1)
			stats->tx_errors = dstats.txerr;

		if (dstats.rxerr != (u32)-1)
			stats->rx_errors = dstats.rxerr;

		if (dstats.txok != (u32)-1)
			stats->tx_packets = dstats.txok + stats->tx_errors;

		if (dstats.rxok != (u32)-1)
			stats->rx_packets = dstats.rxok + stats->rx_errors;

		if (dstats.txbytesok != (u64)-1)
			stats->tx_bytes = dstats.txbytesok;

		if (dstats.rxbytesok != (u64)-1)
			stats->rx_bytes = dstats.rxbytesok;

		if (dstats.reconfigure) {
			GOBI_DEBUG("Net device link reset");
			qc_setdown(dev, DOWN_NO_NDIS_CONNECTION);
			qc_cleardown(dev, DOWN_NO_NDIS_CONNECTION);
		} else {
			if (dstats.linkstate) {
				GOBI_DEBUG("Net device link is connected");
				qc_cleardown(dev, DOWN_NO_NDIS_CONNECTION);
			} else {
				GOBI_DEBUG("Net device link is disconnected");
				qc_setdown(dev, DOWN_NO_NDIS_CONNECTION);
			}
		}
	}

	kfree(rbuf);

	result = read_async(dev, cid, 0, wds_callback, data);
	if (result != 0) {
		GOBI_ERROR("failed to set up async read: %d", result);
	}
}

static int setup_wds_callback(struct qcusbnet *dev)
{
	struct buffer *wbuf;
	u16 cid;
	int result;

	if (!device_valid(dev)) {
		GOBI_ERROR("invalid device");
		return -EFAULT;
	}

	result = client_alloc(dev, QMIWDS);
	if (result < 0) {
		GOBI_ERROR("failed to allocate client");
		return result;
	}
	cid = result;

	wbuf = qmiwds_new_seteventreport(1);
	if (!wbuf) {
		GOBI_ERROR("failed to create seteventreport request");
		return -ENOMEM;
	}

	result = write_sync(dev, wbuf, cid);
	buffer_put(wbuf);
	if (result < 0) {
		GOBI_ERROR("failed to write seteventreport request: %d",
			   result);
		return result;
	}

	wbuf = qmiwds_new_getpkgsrvcstatus(2);
	if (!wbuf) {
		GOBI_ERROR("failed to create getpkgsrvcstatus request");
		return -ENOMEM;
	}

	result = write_sync(dev, wbuf, cid);
	buffer_put(wbuf);
	if (result < 0) {
		GOBI_ERROR("failed to write getpkgsrvcstatus request: %d",
			   result);
		return result;
	}

	result = read_async(dev, cid, 0, wds_callback, NULL);
	if (result) {
		GOBI_ERROR("failed to set up async read: %d", result);
		return result;
	}

	/* TODO(ttuttle): magic numbers? */
	result = usb_control_msg(dev->usbnet->udev,
				 usb_sndctrlpipe(dev->usbnet->udev, 0),
				 0x22, 0x21, 1, 0, NULL, 0, 100);
	if (result < 0) {
		/* TODO(ttuttle): what does this message mean? */
		GOBI_ERROR("Bad SetControlLineState status %d", result);
		return result;
	}

	return 0;
}

static int qmidms_getmeid(struct qcusbnet *dev)
{
	int result;
	u16 cid;
	struct buffer *wbuf;
	void *rbuf;
	u16 size;

	if (!device_valid(dev))	{
		GOBI_ERROR("invalid device");
		return -EFAULT;
	}

	result = client_alloc(dev, QMIDMS);
	/* TODO(ttuttle): free client if we error out */
	if (result < 0) {
		GOBI_ERROR("failed to allocate client");
		return result;
	}
	cid = result;

	wbuf = qmidms_new_getmeid(1);
	if (!wbuf) {
		GOBI_ERROR("failed to create getmeid request");
		return -ENOMEM;
	}

	result = write_sync(dev, wbuf, cid);
	buffer_put(wbuf);
	if (result < 0) {
		GOBI_ERROR("failed to write getmeid request");
		return result;
	}

	result = read_sync(dev, &rbuf, cid, 1);
	if (result < 0) {
		GOBI_ERROR("failed to read meid response");
		return result;
	}
	size = result;

	result = qmidms_meid_resp(rbuf, size, &dev->meid[0], 14);
	kfree(rbuf);

	if (result < 0) {
		GOBI_ERROR("failed to parse meid response");
		memset(&dev->meid[0], '0', 14);
		/* TODO(ttuttle): Do this in other failure cases? */
	}

	client_free(dev, cid);
	return 0;
}

module_param(qcusbnet2k_fwdelay, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(qcusbnet2k_fwdelay, "Delay for old firmware");
