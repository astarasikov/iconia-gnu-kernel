/*
 * Cypress APA trackpad with I2C interface
 *
 * Copyright (C) 2009 Compulab, Ltd.
 * Dudley Du <dudl@cypress.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>

#include <linux/cyapa.h>


/* DEBUG: debug switch macro */
#define DBG_CYAPA_READ_BLOCK_DATA 0


/*
 * Cypress I2C APA trackpad driver version is defined as below:
 * CYAPA_MAJOR_VER.CYAPA_MINOR_VER.CYAPA_REVISION_VER
 */
#define CYAPA_MAJOR_VER		0
#define CYAPA_MINOR_VER		9
#define CYAPA_REVISION_VER	8

#define CYAPA_MT_MAX_TOUCH  255
#define CYAPA_MT_MAX_WIDTH  255

#define MAX_FINGERS	5
#define CYAPA_TOOL_WIDTH 50
#define CYAPA_DEFAULT_TOUCH_PRESSURE 50
#define CYAPA_MT_TOUCH_MAJOR  50

/* When in IRQ mode read the device every THREAD_IRQ_SLEEP_SECS */
#define CYAPA_THREAD_IRQ_SLEEP_SECS	2
#define CYAPA_THREAD_IRQ_SLEEP_MSECS (CYAPA_THREAD_IRQ_SLEEP_SECS * MSEC_PER_SEC)
/*
 * When in Polling mode and no data received for CYAPA_NO_DATA_THRES msecs
 * reduce the polling rate to CYAPA_NO_DATA_SLEEP_MSECS
 */
#define CYAPA_NO_DATA_THRES	(MSEC_PER_SEC)
#define CYAPA_NO_DATA_SLEEP_MSECS	(MSEC_PER_SEC / 4)

/* report data start reg offset address. */
#define DATA_REG_START_OFFSET  0x0000

/* Device Sleep Modes */
#define DEV_POWER_REG  0x0009
#define INTERRUPT_MODE_MASK  0x01
#define PWR_LEVEL_MASK  0x06
#define PWR_BITS_SHIFT 1
#define GET_PWR_LEVEL(reg) (((reg)&PWR_LEVEL_MASK)>>PWR_BITS_SHIFT)

#define INT_SRC_BIT_MASK 0x80
#define VALID_DATA_BIT_MASK 0x08
#define DEV_STATUS_MASK 0x03

#define CYAPA_REG_MAP_SIZE  256

/*
 * APA trackpad device states.
 * Used in register 0x00, bit1-0, DeviceStatus field.
 */
enum cyapa_devicestate {
	CYAPA_DEV_NORMAL = 0x03,
	/*
	 * After trackpad booted, and can report data, it should set this value.
	 * Other values stand for trackpad device is in abnormal state.
	 * It may need to be reset.
	 * Other values are defined later if needed.
	 */
};

#define CYAPA_MAX_TOUCHES (MAX_FINGERS)
#define CYAPA_ONE_TIME_GESTURES  (1)
struct cyapa_touch_gen2 {
	u8 xy;
	u8 x;
	u8 y;
	u8 pressure;
};

struct cyapa_touch {
	int x;
	int y;
	int pressure;
};

struct cyapa_gesture {
	u8 id;
	u8 param1;
	u8 param2;
};

struct cyapa_reg_data_gen2 {
	u8 device_status;
	u8 relative_flags;
	s8 deltax;
	s8 deltay;
	u8 avg_pressure;
	u8 touch_fingers;
	u8 reserved1;
	u8 reserved2;
	struct cyapa_touch_gen2 touches[CYAPA_MAX_TOUCHES];
	u8 gesture_count;
	struct cyapa_gesture gesture[CYAPA_ONE_TIME_GESTURES];
};

union cyapa_reg_data {
	struct cyapa_reg_data_gen2 gen2_data;
};

struct cyapa_report_data {
	u8 button;
	u8 reserved1;
	u8 reserved2;
	u8 avg_pressure;
	int rel_deltaX;
	int rel_deltaY;

	int touch_fingers;
	struct cyapa_touch touches[CYAPA_MAX_TOUCHES];

	int gesture_count;
	struct cyapa_gesture gestures[CYAPA_ONE_TIME_GESTURES];
};

/* The main device structure */
struct cyapa_i2c {
	/* synchronize i2c bus operations. */
	struct semaphore reg_io_sem;
	/* synchronize accessing members of cyapa_i2c data structure. */
	spinlock_t miscdev_spinlock;
	/* synchronize accessing and updating file->f_pos. */
	struct mutex misc_mutex;
	int misc_open_count;
	enum cyapa_work_mode fw_work_mode;

	struct i2c_client	*client;
	struct input_dev	*input;
	struct delayed_work dwork;
	/* synchronize access to dwork. */
	spinlock_t lock;
	int no_data_count;
	int scan_ms;
	int open_count;

	int irq;
	int down_to_polling_mode;
	struct cyapa_platform_data *pdata;
	unsigned short data_base_offset;
	unsigned short control_base_offset;
	unsigned short command_base_offset;
	unsigned short query_base_offset;

	/* read from query data region. */
	char product_id[16];
	unsigned char capability[14];
	unsigned char fw_maj_ver;  /* firmware major version. */
	unsigned char fw_min_ver;  /* firmware minor version. */
	unsigned char hw_maj_ver;  /* hardware major version. */
	unsigned char hw_min_ver;  /* hardware minor version. */
	int max_abs_x;
	int max_abs_y;
	int physical_size_x;
	int physical_size_y;
};

/* global pointer to trackpad touch data structure. */
static struct cyapa_i2c *global_touch;

static void cyapa_get_query_data(struct cyapa_i2c *touch);


#if DBG_CYAPA_READ_BLOCK_DATA
#define DUMP_BUF_SIZE (40 * 3 + 20)  /* max will dump 40 bytes data. */
void cyapa_dump_data_block(const char *func, u8 reg, u8 length, void *data)
{
	char buf[DUMP_BUF_SIZE];
	unsigned buf_len = sizeof(buf);
	char *p = buf;
	int i;
	int l;

	l = snprintf(p, buf_len, "reg 0x%04x: ", reg);
	buf_len -= l;
	p += l;
	for (i = 0; i < length && buf_len; i++, p += l, buf_len -= l)
		l = snprintf(p, buf_len, "%02x ", *((char *)data + i));
	pr_info("%s: data block length = %d\n", func, length);
	pr_info("%s: %s\n", func, buf);
}

void cyapa_dump_report_data(const char *func,
				struct cyapa_report_data *report_data)
{
	int i;

	pr_info("%s: ------------------------------------\n", func);
	pr_info("%s: report_data.button = 0x%02x\n",
		func, report_data->button);
	pr_info("%s: report_data.avg_pressure = %d\n",
		func, report_data->avg_pressure);
	pr_info("%s: report_data.touch_fingers = %d\n",
		func, report_data->touch_fingers);
	for (i = 0; i < report_data->touch_fingers; i++) {
		pr_info("%s: report_data.touches[%d].x = %d\n",
			func, i, report_data->touches[i].x);
		pr_info("%s: report_data.touches[%d].y = %d\n",
			func, i, report_data->touches[i].y);
		pr_info("%s: report_data.touches[%d].pressure = %d\n",
			func, i, report_data->touches[i].pressure);
	}
	pr_info("%s: report_data.gesture_count = %d\n",
			func, report_data->gesture_count);
	for (i = 0; i < report_data->gesture_count; i++) {
		pr_info("%s: report_data.gestures[%d].id = 0x%02x\n",
			func, i, report_data->gestures[i].id);
		pr_info("%s: report_data.gestures[%d].param1 = 0x%02x\n",
			func, i, report_data->gestures[i].param1);
		pr_info("%s: report_data.gestures[%d].param2 = 0x%02x\n",
			func, i, report_data->gestures[i].param2);
	}
	pr_info("%s: -------------------------------------\n", func);
}
#else
void cyapa_dump_data_block(const char *func, u8 reg, u8 length, void *data) {}
void cyapa_dump_report_data(const char *func,
		struct cyapa_report_data *report_data) {}
#endif


/*
 * When requested IRQ number is not available, the trackpad driver
 * falls back to using polling mode.
 * In this case, do not actually enable/disable irq.
 */
static void cyapa_enable_irq(struct cyapa_i2c *touch)
{
	if (touch->down_to_polling_mode == false)
		enable_irq(touch->irq);
}

static void cyapa_disable_irq(struct cyapa_i2c *touch)
{
	if (touch->down_to_polling_mode == false)
		disable_irq(touch->irq);
}

static int cyapa_wait_for_i2c_bus_ready(struct cyapa_i2c *touch)
{
	cyapa_disable_irq(touch);
	if (down_interruptible(&touch->reg_io_sem)) {
		cyapa_enable_irq(touch);
		return -ERESTARTSYS;
	}

	return 0;
}

/*
 * cyapa_i2c_reg_write_byte - write one byte to i2c register map.
 * @touch - private data structure of the trackpad driver.
 * @reg - the offset value of the i2c register map from offset 0.
 * @val - the value should be written to the register map.
 *
 * This function returns negative errno, else zero on success.
 */
static s32 cyapa_i2c_reg_write_byte(struct cyapa_i2c *touch, u16 reg, u8 val)
{
	int ret = 0;

	ret = cyapa_wait_for_i2c_bus_ready(touch);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(touch->client, (u8)reg, val);

	up(&touch->reg_io_sem);
	cyapa_enable_irq(touch);

	return ret;
}

/*
 * cyapa_i2c_reg_read_block - read a block data from trackpad
 *      i2c register map.
 * @touch - private data structure of the trackpad driver.
 * @reg - the offset value of the i2c register map from offset 0.
 * @length - length of the block to be read in bytes.
 * @values - pointer to the buffer that used to store register block
 *           valuse read.
 *
 * Returns negative errno, else the number of bytes written.
 *
 * Note:
 * In trackpad device, the memory block allocated for I2C register map
 * is 256 bytes, so the max read block for I2C bus is 256 bytes.
 */
static s32 cyapa_i2c_reg_read_block(struct cyapa_i2c *touch, u16 reg,
		int length, char *values)
{
	int retval = 0;
	u8 buf[1];

	retval = cyapa_wait_for_i2c_bus_ready(touch);
	if (retval < 0)
		return retval;

	/*
	 * step1: set read pointer of easy I2C.
	 */
	buf[0] = (u8)reg;
	retval = i2c_master_send(touch->client, buf, 1);
	if (retval < 0)
		goto error;

	/* step2: read data. */
	retval = i2c_master_recv(touch->client, values, length);
	if (retval < 0) {
		pr_debug("i2c_master_recv error, %d\n", retval);
		goto error;
	}

	if (retval != length)
		pr_warning("warning I2C block read bytes" \
			"[%d] not equal to requested bytes [%d].\n",
			retval, length);

	/* DEBUG: dump read block data */
	cyapa_dump_data_block(__func__, (u8)reg, retval, values);

error:
	up(&touch->reg_io_sem);
	cyapa_enable_irq(touch);

	return retval;
}

/*
 * cyapa_i2c_reg_write_block - write a block data to trackpad
 *      i2c register map.
 * @touch - private data structure of the trackpad driver.
 * @reg - the offset value of the i2c register map from offset 0.
 * @length - length of the block to be written in bytes.
 * @values - pointer to the block data buffur that will be written.
 *
 * Returns negative errno, else the number of bytes written.
 *
 * Note:
 * In trackpad device, the memory block allocated for I2C register map
 * is 256 bytes, so the max write block for I2C bus is 256 bytes.
 */
static s32 cyapa_i2c_reg_write_block(struct cyapa_i2c *touch, u16 reg,
		int length, const char *values)

{
	int retval = 0;
	u8 buf[CYAPA_REG_MAP_SIZE + 1];

	retval = cyapa_wait_for_i2c_bus_ready(touch);
	if (retval < 0)
		return retval;

	/*
	 * step1: write data to easy I2C in one command.
	 */
	buf[0] = (u8)reg;
	/* copy data shoud be write to I2C slave device. */
	memcpy((void *)&buf[1], (const void *)values, length);

	retval = i2c_master_send(touch->client, buf, length+1);
	if (retval < 0)
		goto error;

	/* one additional written byte is register offset. */
	if (retval != (length + 1))
		pr_warning("warning I2C block write bytes" \
			"[%d] not equal to requested bytes [%d].\n",
			retval, length);

error:
	up(&touch->reg_io_sem);
	cyapa_enable_irq(touch);

	return (retval < 0) ? retval : (retval - 1);
}


/*
 **************************************************************
 * misc cyapa device for trackpad firmware update,
 * and for raw read/write operations.
 * The following programs may open and use cyapa device.
 * 1. X Input Driver.
 * 2. trackpad firmware update program.
 **************************************************************
 */
static int cyapa_misc_open(struct inode *inode, struct file *file)
{
	int count;
	struct cyapa_i2c *touch = global_touch;

	if (touch == NULL)
		return -ENODEV;
	file->private_data = (void *)touch;

	spin_lock(&touch->miscdev_spinlock);
	if (touch->misc_open_count) {
		spin_unlock(&touch->miscdev_spinlock);
		return -EBUSY;
	}
	count = ++touch->misc_open_count;
	spin_unlock(&touch->miscdev_spinlock);

	return 0;
}

static int cyapa_misc_close(struct inode *inode, struct file *file)
{
	int count;
	struct cyapa_i2c *touch = (struct cyapa_i2c *)file->private_data;

	spin_lock(&touch->miscdev_spinlock);
	count = --touch->misc_open_count;
	spin_unlock(&touch->miscdev_spinlock);

	return 0;
}

static int cyapa_pos_validate(unsigned int pos)
{
	return (pos >= 0) && (pos < CYAPA_REG_MAP_SIZE);
}

static loff_t cyapa_misc_llseek(struct file *file, loff_t offset, int origin)
{
	loff_t ret = -EINVAL;
	struct cyapa_i2c *touch = (struct cyapa_i2c *)file->private_data;

	if (touch == NULL) {
		pr_err("cypress trackpad device does not exit.\n");
		return -ENODEV;
	}

	mutex_lock(&touch->misc_mutex);
	switch (origin) {
	case SEEK_SET:
		if (cyapa_pos_validate(offset)) {
			file->f_pos = offset;
			ret = file->f_pos;
		}
		break;

	case SEEK_CUR:
		if (cyapa_pos_validate(file->f_pos + offset)) {
			file->f_pos += offset;
			ret = file->f_pos;
		}
		break;

	case SEEK_END:
		if (cyapa_pos_validate(CYAPA_REG_MAP_SIZE + offset)) {
			file->f_pos = (CYAPA_REG_MAP_SIZE + offset);
			ret = file->f_pos;
		}
		break;

	default:
		break;
	}
	mutex_unlock(&touch->misc_mutex);

	return ret;
}

static int cyapa_miscdev_rw_params_check(struct cyapa_i2c *touch,
	unsigned long offset, unsigned int length)
{
	unsigned int max_offset;

	if (touch == NULL)
		return -ENODEV;

	/*
	 * application may read/write 0 length byte
	 * to reset read/write pointer to offset.
	 */
	max_offset = (length == 0) ? offset : (length - 1 + offset);

	/* max registers contained in one register map in bytes is 256. */
	if (cyapa_pos_validate(offset) && cyapa_pos_validate(max_offset))
		return 0;

	pr_debug("invalid parameters, length=%d, offset=0x%x\n",
			length, (unsigned int)offset);

	return -EINVAL;
}

static ssize_t cyapa_misc_read(struct file *file, char __user *usr_buf,
		size_t count, loff_t *offset)
{
	int ret = 0;
	int reg_len = (int)count;
	unsigned long reg_offset = *offset;
	char reg_buf[CYAPA_REG_MAP_SIZE];
	struct cyapa_i2c *touch = (struct cyapa_i2c *)file->private_data;

	ret = cyapa_miscdev_rw_params_check(touch, reg_offset, count);
	if (ret < 0)
		return ret;

	ret = cyapa_i2c_reg_read_block(touch, (u16)reg_offset,
				reg_len, reg_buf);
	if (ret < 0) {
		pr_err("cyapa trackpad I2C read FAILED.\n");
		return ret;
	}

	if (ret < reg_len)
		pr_warning("Expected %d bytes, read %d bytes.\n",
			reg_len, ret);
	reg_len = ret;

	if (copy_to_user(usr_buf, reg_buf, reg_len)) {
		ret = -EFAULT;
	} else {
		*offset += reg_len;
		ret = reg_len;
	}

	return ret;
}

static ssize_t cyapa_misc_write(struct file *file, const char __user *usr_buf,
		size_t count, loff_t *offset)
{
	int ret = 0;
	unsigned long reg_offset = *offset;
	char reg_buf[CYAPA_REG_MAP_SIZE];
	struct cyapa_i2c *touch = (struct cyapa_i2c *)file->private_data;

	ret = cyapa_miscdev_rw_params_check(touch, reg_offset, count);
	if (ret < 0)
		return ret;

	if (copy_from_user(reg_buf, usr_buf, (int)count)) {
		pr_err("copy data from user space failed.\n");
		return -EINVAL;
	}

	ret = cyapa_i2c_reg_write_block(touch,
					(u16)reg_offset,
					(int)count,
					reg_buf);
	if (ret < 0)
		pr_err("cyapa trackpad I2C write FAILED.\n");

	*offset = (ret < 0) ? reg_offset : (reg_offset + ret);

	return ret;
}

static long cyapa_misc_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	int ret = 0;
	struct cyapa_i2c *touch = (struct cyapa_i2c *)file->private_data;
	struct cyapa_misc_ioctl_data ioctl_data;
	unsigned char buf[8];

	if (touch == NULL) {
		pr_err("cypress trackpad device does not exist.\n");
		return -ENODEV;
	}

	/* copy to kernel space. */
	if (copy_from_user(&ioctl_data, (u8 *)arg, sizeof(struct cyapa_misc_ioctl_data)))
		return -EINVAL;

	switch (cmd) {
	case CYAPA_GET_PRODUCT_ID:
		if (!ioctl_data.buf || ioctl_data.len < 16)
			return -EINVAL;

		cyapa_get_query_data(touch);
		ioctl_data.len = 16;
		if (copy_to_user(ioctl_data.buf, touch->product_id, 16))
				return -EIO;
		if (copy_to_user((void *)arg, &ioctl_data,
			sizeof(struct cyapa_misc_ioctl_data)))
			return -EIO;
		return ioctl_data.len;

	case CYAPA_GET_DRIVER_VER:
		if (!ioctl_data.buf || ioctl_data.len < 3)
			return -EINVAL;

		cyapa_get_query_data(touch);
		ioctl_data.len = 3;
		memset(buf, 0, sizeof(buf));
		buf[0] = (unsigned char)CYAPA_MAJOR_VER;
		buf[1] = (unsigned char)CYAPA_MINOR_VER;
		buf[2] = (unsigned char)CYAPA_REVISION_VER;
		if (copy_to_user(ioctl_data.buf, buf, ioctl_data.len))
			return -EIO;
		if (copy_to_user((void *)arg, &ioctl_data,
			sizeof(struct cyapa_misc_ioctl_data)))
			return -EIO;
		return ioctl_data.len;

	case CYAPA_GET_FIRMWARE_VER:
		if (!ioctl_data.buf || ioctl_data.len < 2)
			return -EINVAL;

		cyapa_get_query_data(touch);
		ioctl_data.len = 2;
		memset(buf, 0, sizeof(buf));
		buf[0] = touch->fw_maj_ver;
		buf[1] = touch->fw_min_ver;
		if (copy_to_user(ioctl_data.buf, buf, ioctl_data.len))
			return -EIO;
		if (copy_to_user((void *)arg, &ioctl_data, sizeof(struct cyapa_misc_ioctl_data)))
			return -EIO;
		return ioctl_data.len;

	case CYAPA_GET_HARDWARE_VER:
		if (!ioctl_data.buf || ioctl_data.len < 2)
			return -EINVAL;

		cyapa_get_query_data(touch);
		ioctl_data.len = 2;
		memset(buf, 0, sizeof(buf));
		buf[0] = touch->hw_maj_ver;
		buf[1] = touch->hw_min_ver;
		if (copy_to_user(ioctl_data.buf, buf, ioctl_data.len))
			return -EIO;
		if (copy_to_user((void *)arg, &ioctl_data, sizeof(struct cyapa_misc_ioctl_data)))
			return -EIO;
		return ioctl_data.len;

	case CYAPA_SET_BOOTLOADER_MODE:
		return ret;

	case CYAPA_SET_STREAM_MODE:
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct file_operations cyapa_misc_fops = {
	.owner = THIS_MODULE,
	.open = cyapa_misc_open,
	.release = cyapa_misc_close,
	.unlocked_ioctl = cyapa_misc_ioctl,
	.llseek = cyapa_misc_llseek,
	.read = cyapa_misc_read,
	.write = cyapa_misc_write,
};

static struct miscdevice cyapa_misc_dev = {
	.name = CYAPA_MISC_NAME,
	.fops = &cyapa_misc_fops,
	.minor = MISC_DYNAMIC_MINOR,
};

static int __init cyapa_misc_init(void)
{
	return misc_register(&cyapa_misc_dev);
}

static void __exit cyapa_misc_exit(void)
{
	misc_deregister(&cyapa_misc_dev);
}

static void cyapa_update_firmware_dispatch(struct cyapa_i2c *touch)
{
	/* do something here to update trackpad firmware. */
}

/*
 ***************************************************************
 * Cypress i2c trackpad input device driver.
 ***************************************************************
 */

#define REG_OFFSET_DATA_BASE     0x0000
#define REG_OFFSET_CONTROL_BASE  0x0029
#define REG_OFFSET_COMMAND_BASE  0x0049
#define REG_OFFSET_QUERY_BASE   0x004B
static void cyapa_get_reg_offset(struct cyapa_i2c *touch)
{
	touch->data_base_offset = REG_OFFSET_DATA_BASE;
	touch->control_base_offset = REG_OFFSET_CONTROL_BASE;
	touch->command_base_offset = REG_OFFSET_COMMAND_BASE;
	touch->query_base_offset = REG_OFFSET_QUERY_BASE;

	/* this function will be updated later depending firmware support. */
}

static void cyapa_get_query_data(struct cyapa_i2c *touch)
{
	char query_data[40];
	int ret_read_size = 0;
	int i;

	/* only the firmware with GEN2 protocol support MT protocol.*/
	if (touch->pdata->gen != CYAPA_GEN2) {
		return;
	}

	memset(query_data, 0, 40);
	ret_read_size = cyapa_i2c_reg_read_block(touch,
				touch->query_base_offset,
				38,
				query_data);
	if (ret_read_size < 0)
		return;

	touch->product_id[0] = query_data[0];
	touch->product_id[1] = query_data[1];
	touch->product_id[2] = query_data[2];
	touch->product_id[3] = query_data[3];
	touch->product_id[4] = query_data[4];
	touch->product_id[5] = '-';
	touch->product_id[6] = query_data[5];
	touch->product_id[7] = query_data[6];
	touch->product_id[8] = query_data[7];
	touch->product_id[9] = query_data[8];
	touch->product_id[10] = query_data[9];
	touch->product_id[11] = query_data[10];
	touch->product_id[12] = '-';
	touch->product_id[13] = query_data[11];
	touch->product_id[14] = query_data[12];
	touch->product_id[15] = '\0';

	touch->fw_maj_ver = query_data[15];
	touch->fw_min_ver = query_data[16];
	touch->hw_maj_ver = query_data[17];
	touch->hw_min_ver = query_data[18];

	for (i = 0; i < 13; i++)
		touch->capability[i] = query_data[19+i];

	touch->max_abs_x =
		(((query_data[32] & 0xF0) << 4) | query_data[33]);
	touch->max_abs_y =
		(((query_data[32] & 0x0F) << 8) | query_data[34]);

	touch->physical_size_x =
		(((query_data[35] & 0xF0) << 4) | query_data[36]);
	touch->physical_size_y =
		(((query_data[35] & 0x0F) << 8) | query_data[37]);
	if (!touch->physical_size_x || !touch->physical_size_y) {
		touch->physical_size_x = 105;
		touch->physical_size_y = 60;
	}
}

static int cyapa_i2c_reconfig(struct cyapa_i2c *touch)
{
	/* trackpad gen2 firmware. default is interrupt mode. */
	cyapa_get_reg_offset(touch);
	cyapa_get_query_data(touch);

	pr_info("Cypress Trackpad Information:\n");
	pr_info("\t\t\tProduct ID:  %s\n",
		touch->product_id);
	pr_info("\t\t\tFirmware Version:  %d.%d\n",
		touch->fw_maj_ver, touch->fw_min_ver);
	pr_info("\t\t\tHardware Version:  %d.%d\n",
		touch->hw_maj_ver, touch->hw_min_ver);
	pr_info("\t\t\tDriver Version:  %d.%d.%d\n",
		CYAPA_MAJOR_VER, CYAPA_MINOR_VER, CYAPA_REVISION_VER);
	pr_info("\t\t\tMax ABS X,Y:   %d,%d\n",
		touch->max_abs_x, touch->max_abs_y);
	pr_info("\t\t\tPhysical Size X,Y:   %d,%d\n",
		touch->physical_size_x, touch->physical_size_y);

	return 0;
}

static int cyapa_i2c_reset_config(struct cyapa_i2c *touch)
{
	return 0;
}

static int cyapa_verify_data_device(struct cyapa_i2c *touch,
				union cyapa_reg_data *reg_data)
{
	struct cyapa_reg_data_gen2 *data_gen2 = NULL;

	if (touch->pdata->gen != CYAPA_GEN2)
		return -EINVAL;

	data_gen2 = &reg_data->gen2_data;
	if ((data_gen2->device_status & INT_SRC_BIT_MASK) != INT_SRC_BIT_MASK)
		return -EINVAL;

	if ((data_gen2->device_status & DEV_STATUS_MASK) != CYAPA_DEV_NORMAL)
		return -EBUSY;

	return 0;
}

static inline void cyapa_report_fingers(struct input_dev *input, int fingers)
{
	input_report_key(input, BTN_TOOL_FINGER, (fingers == 1));
	input_report_key(input, BTN_TOOL_DOUBLETAP, (fingers == 2));
	input_report_key(input, BTN_TOOL_TRIPLETAP, (fingers == 3));
	input_report_key(input, BTN_TOOL_QUADTAP, (fingers > 3));
}

static void cyapa_parse_gen2_data(struct cyapa_i2c *touch,
		struct cyapa_reg_data_gen2 *reg_data,
		struct cyapa_report_data *report_data)
{
	int i;

	/* bit2-middle button; bit1-right button; bit0-left button. */
	report_data->button = reg_data->relative_flags & 0x07;

	/* get relative delta X and delta Y. */
	report_data->rel_deltaX = reg_data->deltax;
	/* The Y direction of trackpad is opposite of screen. */
	report_data->rel_deltaY = -reg_data->deltay;

	/* copy fingers touch data */
	report_data->avg_pressure = reg_data->avg_pressure;
	report_data->touch_fingers
		= ((reg_data->touch_fingers > CYAPA_MAX_TOUCHES) ?
			(CYAPA_MAX_TOUCHES) : (reg_data->touch_fingers));
	for (i = 0; i < report_data->touch_fingers; i++) {
		report_data->touches[i].x =
			((reg_data->touches[i].xy & 0xF0) << 4)
				| reg_data->touches[i].x;
		report_data->touches[i].y =
			((reg_data->touches[i].xy & 0x0F) << 8)
				| reg_data->touches[i].y;
		report_data->touches[i].pressure = reg_data->touches[i].pressure;
	}

	/* parse gestures */
	report_data->gesture_count =
		(((reg_data->gesture_count) > CYAPA_ONE_TIME_GESTURES) ?
			CYAPA_ONE_TIME_GESTURES : reg_data->gesture_count);
	for (i = 0; i < report_data->gesture_count; i++) {
		report_data->gestures[i].id = reg_data->gesture[i].id;
		report_data->gestures[i].param1 = reg_data->gesture[i].param1;
		report_data->gestures[i].param2 = reg_data->gesture[i].param2;
	}

	/* DEBUG: dump parsed report data */
	cyapa_dump_report_data(__func__, report_data);
}

static int cyapa_handle_input_report_data(struct cyapa_i2c *touch,
		struct cyapa_report_data *report_data)
{
	int i;
	struct input_dev *input = touch->input;

	/* report raw trackpad data. */
	for (i = 0; i < report_data->touch_fingers; i++) {
		input_report_abs(input, ABS_MT_POSITION_X,
			report_data->touches[i].x);
		input_report_abs(input, ABS_MT_POSITION_Y,
			report_data->touches[i].y);
		input_report_abs(input, ABS_MT_TOUCH_MAJOR,
			report_data->touches[i].pressure > 0 ?
				CYAPA_MT_TOUCH_MAJOR : 0);
		input_report_abs(input, ABS_MT_PRESSURE,
			report_data->touches[i].pressure);
		input_mt_sync(input);
	}

	/*
	 * report mouse device data.
	 * always track the first finger,
	 * when detached multi-finger touched.
	 */
	input_report_key(input, BTN_TOUCH, (report_data->touch_fingers > 0));
	cyapa_report_fingers(input, report_data->touch_fingers);

	input_report_abs(input, ABS_TOOL_WIDTH, 15);
	input_report_abs(input, ABS_X, report_data->touches[0].x);
	input_report_abs(input, ABS_Y, report_data->touches[0].y);
	input_report_abs(input, ABS_PRESSURE, report_data->touches[0].pressure);

	/*
	 * Workaround for firmware button reporting issue.
	 * Report any reported button as BTN_LEFT.
	 */
	input_report_key(input, BTN_LEFT, report_data->button);

	input_sync(input);

	return report_data->touch_fingers | report_data->button;
}

static bool cyapa_i2c_get_input(struct cyapa_i2c *touch)
{
	int ret_read_size = -1;
	int read_length = 0;
	union cyapa_reg_data reg_data;
	struct cyapa_reg_data_gen2 *gen2_data;
	struct cyapa_report_data report_data;

	/* read register data from trackpad. */
	gen2_data = &reg_data.gen2_data;
	read_length = sizeof(struct cyapa_reg_data_gen2);

	ret_read_size = cyapa_i2c_reg_read_block(touch,
					DATA_REG_START_OFFSET,
					read_length,
					(char *)&reg_data);
	if (ret_read_size < 0)
		return 0;

	if (cyapa_verify_data_device(touch, &reg_data) < 0)
		return 0;

	/* process and parse raw data read from Trackpad. */
	cyapa_parse_gen2_data(touch, gen2_data, &report_data);

	/* report data to input subsystem. */
	return cyapa_handle_input_report_data(touch, &report_data);
}

/* Control driver polling read rate and work handler sleep time */
static unsigned long cyapa_i2c_adjust_delay(struct cyapa_i2c *touch,
			bool have_data)
{
	unsigned long delay, nodata_count_thres;

	if (touch->down_to_polling_mode == false) {
		delay = msecs_to_jiffies(CYAPA_THREAD_IRQ_SLEEP_MSECS);
		return round_jiffies_relative(delay);
	}

	delay = touch->pdata->polling_interval_time_active;
	if (have_data) {
		touch->no_data_count = 0;
	} else {
		nodata_count_thres =
			CYAPA_NO_DATA_THRES / touch->scan_ms;
		if (touch->no_data_count < nodata_count_thres)
			touch->no_data_count++;
		else
			delay = CYAPA_NO_DATA_SLEEP_MSECS;
	}
	return msecs_to_jiffies(delay);
}

/* Work Handler */
static void cyapa_i2c_work_handler(struct work_struct *work)
{
	bool have_data;
	struct cyapa_i2c *touch =
		container_of(work, struct cyapa_i2c, dwork.work);
	unsigned long delay;

	/*
	 * use spinlock to avoid confict accessing
	 * when firmware switching into bootloader mode.
	 */
	spin_lock(&touch->miscdev_spinlock);
	if (touch->fw_work_mode == CYAPA_BOOTLOAD_MODE) {
		spin_unlock(&touch->miscdev_spinlock);
		cyapa_update_firmware_dispatch(touch);
	} else {
		spin_unlock(&touch->miscdev_spinlock);

		have_data = cyapa_i2c_get_input(touch);
		/*
		 * While interrupt driven, there is no real need to poll the
		 * device. But trackpads are very sensitive, so there could be
		 * errors related to physical environment and the attention
		 * line isn't necessarily asserted. In such case we can lose
		 * the trackpad. We poll the device once in
		 * CYAPA_THREAD_IRQ_SLEEP_SECS and if error is detected,
		 * we try to reset and reconfigure the trackpad.
		 */
		delay = cyapa_i2c_adjust_delay(touch, have_data);
	}

	return;
}

static void cyapa_i2c_reschedule_work(struct cyapa_i2c *touch,
		unsigned long delay)
{
	unsigned long flags;

	spin_lock_irqsave(&touch->lock, flags);

	/*
	 * If work is already scheduled then subsequent schedules will not
	 * change the scheduled time that's why we have to cancel it first.
	 */
	__cancel_delayed_work(&touch->dwork);
	schedule_delayed_work(&touch->dwork, delay);

	spin_unlock_irqrestore(&touch->lock, flags);
}

static irqreturn_t cyapa_i2c_irq(int irq, void *dev_id)
{
	struct cyapa_i2c *touch = dev_id;

	cyapa_i2c_reschedule_work(touch, 0);

	return IRQ_HANDLED;
}

static int cyapa_i2c_open(struct input_dev *input)
{
	struct cyapa_i2c *touch = input_get_drvdata(input);
	int retval;

	if (0 == touch->open_count) {
		retval = cyapa_i2c_reset_config(touch);
		if (retval < 0) {
			pr_err("reset i2c trackpad error code, %d.\n", retval);
			return retval;
		}
	}
	touch->open_count++;

	if (touch->down_to_polling_mode == true) {
		/*
		 * In polling mode, by default, initialize the polling interval
		 * to CYAPA_NO_DATA_SLEEP_MSECS,
		 * Once data is read, the polling rate will be automatically
		 * increased.
		 */
		cyapa_i2c_reschedule_work(touch,
			msecs_to_jiffies(CYAPA_NO_DATA_SLEEP_MSECS));
	}

	return 0;
}

static void cyapa_i2c_close(struct input_dev *input)
{
	struct cyapa_i2c *touch = input_get_drvdata(input);

	touch->open_count--;

	if (0 == touch->open_count)
		cancel_delayed_work_sync(&touch->dwork);
}

static struct cyapa_i2c *cyapa_i2c_touch_create(struct i2c_client *client)
{
	struct cyapa_i2c *touch;

	touch = kzalloc(sizeof(struct cyapa_i2c), GFP_KERNEL);
	if (!touch)
		return NULL;

	touch->pdata = (struct cyapa_platform_data *)client->dev.platform_data;

	touch->scan_ms = touch->pdata->report_rate
		? (1000 / touch->pdata->report_rate) : 0;
	touch->open_count = 0;
	touch->client = client;
	touch->down_to_polling_mode = false;
	global_touch = touch;
	touch->fw_work_mode = CYAPA_STREAM_MODE;
	touch->misc_open_count = 0;
	sema_init(&touch->reg_io_sem, 1);
	spin_lock_init(&touch->miscdev_spinlock);
	mutex_init(&touch->misc_mutex);

	INIT_DELAYED_WORK(&touch->dwork, cyapa_i2c_work_handler);
	spin_lock_init(&touch->lock);

	return touch;
}

static int cyapa_create_input_dev(struct cyapa_i2c *touch)
{
	int retval = 0;
	struct input_dev *input = NULL;

	input = touch->input = input_allocate_device();
	if (!touch->input) {
		pr_err("Allocate memory for Input device failed, %d\n", retval);
		return -ENOMEM;
	}

	input->name = "cyapa_i2c_trackpad";
	input->phys = touch->client->adapter->name;
	input->id.bustype = BUS_I2C;
	input->id.version = 1;
	input->id.product = 0;  /* means any product in eventcomm. */
	input->dev.parent = &touch->client->dev;

	input->open = cyapa_i2c_open;
	input->close = cyapa_i2c_close;
	input_set_drvdata(input, touch);

	__set_bit(EV_ABS, input->evbit);

	/*
	 * set and report not-MT axes to support synaptics X Driver.
	 * When multi-fingers on trackpad, only the first finger touch
	 * will be reported as X/Y axes values.
	 */
	input_set_abs_params(input, ABS_X, 0, touch->max_abs_x, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, touch->max_abs_y, 0, 0);
	input_set_abs_params(input, ABS_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(input, ABS_TOOL_WIDTH, 0, 255, 0, 0);

	/* finger touch area */
	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, CYAPA_MT_MAX_TOUCH, 0, 0);
	/* finger approach area. not suport yet, resreved for future devices. */
	input_set_abs_params(input, ABS_MT_WIDTH_MAJOR, 0, CYAPA_MT_MAX_WIDTH, 0, 0);
	input_set_abs_params(input, ABS_MT_WIDTH_MINOR, 0, CYAPA_MT_MAX_WIDTH, 0, 0);
	/* finger orientation. not support yet, reserved for future devices. */
	input_set_abs_params(input, ABS_MT_ORIENTATION, 0, 1, 0, 0);
	/* finger position */
	input_set_abs_params(input, ABS_MT_POSITION_X,
		0, touch->max_abs_x, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y,
		0, touch->max_abs_y, 0, 0);

	__set_bit(EV_KEY, input->evbit);
	__set_bit(BTN_TOUCH, input->keybit);
	__set_bit(BTN_TOOL_FINGER, input->keybit);
	__set_bit(BTN_TOOL_DOUBLETAP, input->keybit);
	__set_bit(BTN_TOOL_TRIPLETAP, input->keybit);
	__set_bit(BTN_TOOL_QUADTAP, input->keybit);

	__set_bit(BTN_LEFT, input->keybit);

	input_set_events_per_packet(input, 60);

	/* Register the device in input subsystem */
	retval = input_register_device(touch->input);
	if (retval) {
		pr_err("Input device register failed, %d\n", retval);
		input_free_device(input);
	}

	return retval;
}

static int __devinit cyapa_i2c_probe(struct i2c_client *client,
			       const struct i2c_device_id *dev_id)
{
	int retval = 0;
	struct cyapa_i2c *touch;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EIO;

	touch = cyapa_i2c_touch_create(client);
	if (!touch) {
		pr_err("allocate memory for touch failed.\n");
		return -ENOMEM;
	}

	/* First, initialize pdata */
	if (touch->pdata->init) {
		retval = touch->pdata->init();
		if (retval) {
			pr_err("board initialize failed: %d\n", retval);
			goto err_mem_free;
		}
	}

	/*
	 * set irq number for interrupt mode.
	 * normally, polling mode only will be used
	 * when special platform that do not support slave interrupt.
	 * or allocate irq number to it failed.
	 */
	if (touch->pdata->irq_gpio <= 0) {
		if (client->irq) {
			touch->irq = client->irq;
		} else {
			/* irq mode is not supported by platform. */
			touch->irq = -1;
		}
	} else {
		touch->irq = gpio_to_irq(touch->pdata->irq_gpio);
	}

	if (touch->irq <= 0) {
		pr_err("failed to allocate irq\n");
		goto err_mem_free;
	}

	set_irq_type(touch->irq, IRQF_TRIGGER_FALLING);
	retval = request_irq(touch->irq,
			cyapa_i2c_irq,
			0,
			CYAPA_I2C_NAME,
			touch);
	if (retval) {
		pr_warning("IRQ request failed: %d," \
			"falling back to polling mode.\n", retval);

		touch->down_to_polling_mode = true;
	}

	/* reconfig trackpad depending on platform setting. */
	cyapa_i2c_reconfig(touch);

	/* create an input_dev instance for trackpad device. */
	retval = cyapa_create_input_dev(touch);
	if (retval) {
		free_irq(touch->irq, touch);
		pr_err("create input_dev instance failed.\n");
		goto err_mem_free;
	}

	i2c_set_clientdata(client, touch);

	return 0;

err_mem_free:
	/* release previous allocated input_dev instances. */
	if (touch->input) {
		input_free_device(touch->input);
		touch->input = NULL;
	}

	kfree(touch);
	global_touch = NULL;

	return retval;
}

static int __devexit cyapa_i2c_remove(struct i2c_client *client)
{
	struct cyapa_i2c *touch = i2c_get_clientdata(client);

	if (touch->down_to_polling_mode == false)
		free_irq(client->irq, touch);

	if (touch->input)
		input_unregister_device(touch->input);
	kfree(touch);
	global_touch = NULL;

	return 0;
}

static int cyapa_i2c_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct cyapa_i2c *touch = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&touch->dwork);

	return 0;
}

static int cyapa_i2c_resume(struct i2c_client *client)
{
	int ret;
	struct cyapa_i2c *touch = i2c_get_clientdata(client);

	if (touch->pdata->wakeup) {
		ret = touch->pdata->wakeup();
		if (ret) {
			pr_err("wakeup failed, %d\n", ret);
			return ret;
		}
	}

	ret = cyapa_i2c_reset_config(touch);
	if (ret) {
		pr_err("reset and config trackpad device failed: %d\n", ret);
		return ret;
	}

	cyapa_i2c_reschedule_work(touch,
			msecs_to_jiffies(CYAPA_NO_DATA_SLEEP_MSECS));

	return 0;
}

static const struct i2c_device_id cypress_i2c_id_table[] = {
	{ CYAPA_I2C_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, cypress_i2c_id_table);

static struct i2c_driver cypress_i2c_driver = {
	.driver = {
		.name = CYAPA_I2C_NAME,
		.owner = THIS_MODULE,
	},

	.probe = cyapa_i2c_probe,
	.remove = __devexit_p(cyapa_i2c_remove),
#ifdef CONFIG_PM
	.suspend = cyapa_i2c_suspend,
	.resume = cyapa_i2c_resume,
#endif
	.id_table = cypress_i2c_id_table,
};

static int __init cyapa_i2c_init(void)
{
	int ret;

	ret = i2c_add_driver(&cypress_i2c_driver);
	if (ret) {
		pr_err("cypress i2c driver register FAILED.\n");
		return ret;
	}

	ret = cyapa_misc_init();
	if (ret) {
		i2c_del_driver(&cypress_i2c_driver);
		pr_err("cyapa misc device register FAILED.\n");
		return ret;
	}

	return ret;
}

static void __exit cyapa_i2c_exit(void)
{
	cyapa_misc_exit();

	i2c_del_driver(&cypress_i2c_driver);
}

module_init(cyapa_i2c_init);
module_exit(cyapa_i2c_exit);

MODULE_DESCRIPTION("Cypress I2C Trackpad Driver");
MODULE_AUTHOR("Dudley Du <dudl@cypress.com>");
MODULE_LICENSE("GPL");
