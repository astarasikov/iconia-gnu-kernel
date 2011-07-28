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
#include <linux/input/mt.h>

#include <linux/cyapa.h>


/* DEBUG: debug switch macro */
#define DBG_CYAPA_READ_BLOCK_DATA 0


/*
 * Cypress I2C APA trackpad driver version is defined as below:
 * CYAPA_MAJOR_VER.CYAPA_MINOR_VER.CYAPA_REVISION_VER
 */
#define CYAPA_MAJOR_VER	1
#define CYAPA_MINOR_VER	0
#define CYAPA_REVISION_VER	0

#define CYAPA_MT_MAX_TOUCH  255
#define CYAPA_MT_MAX_WIDTH  255

#define MAX_FINGERS	5
#define CYAPA_TOOL_WIDTH 50
#define CYAPA_DEFAULT_TOUCH_PRESSURE 50
#define CYAPA_MT_TOUCH_MAJOR  50
/*
 * In the special case, where a finger is removed and makes contact
 * between two packets, there will be two touches for that finger,
 * with different tracking_ids.
 * Thus, the maximum number of slots must be twice the maximum number
 * of fingers.
 */
#define MAX_MT_SLOTS  (2 * MAX_FINGERS)

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

/*
 * bit 7: Valid interrupt source
 * bit 6 - 4: Reserved
 * bit 3 - 2: Power status
 * bit 1 - 0: Device status
 */
#define REG_OP_STATUS     0x00
#define OP_STATUS_SRC     0x80
#define OP_STATUS_POWER   0x0C
#define OP_STATUS_DEV     0x03
#define OP_STATUS_MASK (OP_STATUS_SRC | OP_STATUS_POWER | OP_STATUS_DEV)

/*
 * bit 7 - 4: Number of touched finger
 * bit 3: Valid data
 * bit 2: Middle Physical Button
 * bit 1: Right Physical Button
 * bit 0: Left physical Button
 */
#define REG_OP_DATA1       0x01
#define OP_DATA_VALID      0x08
#define OP_DATA_MIDDLE_BTN 0x04
#define OP_DATA_RIGHT_BTN  0x02
#define OP_DATA_LEFT_BTN   0x01
#define OP_DATA_BTN_MASK (OP_DATA_MIDDLE_BTN | OP_DATA_RIGHT_BTN | OP_DATA_LEFT_BTN)

/*
 * bit 7: Busy
 * bit 6 - 5: Reserved
 * bit 4: Booloader running
 * bit 3 - 1: Reserved
 * bit 0: Checksum valid
 */
#define REG_BL_STATUS        0x01
#define BL_STATUS_BUSY       0x80
#define BL_STATUS_RUNNING    0x10
#define BL_STATUS_DATA_VALID 0x08
#define BL_STATUS_CSUM_VALID 0x01
/*
 * bit 7: Invalid
 * bit 6: Invalid security key
 * bit 5: Bootloading
 * bit 4: Command checksum
 * bit 3: Flash protection error
 * bit 2: Flash checksum error
 * bit 1 - 0: Reserved
 */
#define REG_BL_ERROR         0x02
#define BL_ERROR_INVALID     0x80
#define BL_ERROR_INVALID_KEY 0x40
#define BL_ERROR_BOOTLOADING 0x20
#define BL_ERROR_CMD_CSUM    0x10
#define BL_ERROR_FLASH_PROT  0x08
#define BL_ERROR_FLASH_CSUM  0x04

#define REG_BL_KEY1 0x0D
#define REG_BL_KEY2 0x0E
#define REG_BL_KEY3 0x0F
#define BL_KEY1 0xC0
#define BL_KEY2 0xC1
#define BL_KEY3 0xC2

#define BL_HEAD_BYTES  16  /* bytes of bootloader head registers. */

/* Macro for register map group offset. */
#define CYAPA_REG_MAP_SIZE  256

#define PRODUCT_ID_SIZE  16
#define GEN2_QUERY_DATA_SIZE  38
#define GEN3_QUERY_DATA_SIZE  27
#define REG_PROTOCOL_GEN_QUERY_OFFSET  20

#define GEN2_REG_OFFSET_DATA_BASE     0x0000
#define GEN2_REG_OFFSET_CONTROL_BASE  0x0029
#define GEN2_REG_OFFSET_COMMAND_BASE  0x0049
#define GEN2_REG_OFFSET_QUERY_BASE    0x004B
#define GEN3_REG_OFFSET_DATA_BASE     0x0000
#define GEN3_REG_OFFSET_CONTROL_BASE  0x0000
#define GEN3_REG_OFFSET_COMMAND_BASE  0x0028
#define GEN3_REG_OFFSET_QUERY_BASE    0x002A

#define CYAPA_GEN2_OFFSET_SOFT_RESET  GEN2_REG_OFFSET_COMMAND_BASE
#define CYAPA_GEN3_OFFSET_SOFT_RESET  GEN3_REG_OFFSET_COMMAND_BASE

#define REG_OFFSET_POWER_MODE (GEN3_REG_OFFSET_COMMAND_BASE + 1)
#define OP_POWER_MODE_MASK   0xC0
#define OP_POWER_MODE_SHIFT  6
#define PWR_MODE_FULL_ACTIVE 3
#define PWR_MODE_LIGHT_SLEEP 2
#define PWR_MODE_DEEP_SLEEP  0
#define SET_POWER_MODE_DELAY 10000  /* unit: us */

/*
 * Status of the cyapa device detection worker.
 * The worker is started at driver initialization and
 * resume from system sleep.
 */
enum cyapa_detect_status {
	CYAPA_DETECT_DONE_SUCCESS,
	CYAPA_DETECT_DONE_FAILED,
};

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
	int tracking_id;
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

struct cyapa_touch_gen3 {
	/*
	 * high bits or x/y position value
	 * bit 7 - 4: high 4 bits of x position value
	 * bit 3 - 0: high 4 bits of y position value
	 */
	u8 xy;
	u8 x;  /* low 8 bits of x position value. */
	u8 y;  /* low 8 bits of y position value. */
	u8 pressure;
	/*
	 * The range of tracking_id is 0 - 15,
	 * it is incremented every time a finger makes contact
	 * with the trackpad.
	 */
	u8 tracking_id;
};

struct cyapa_reg_data_gen3 {
	/*
	 * bit 0 - 1: device status
	 * bit 3 - 2: power mode
	 * bit 6 - 4: reserved
	 * bit 7: interrupt valid bit
	 */
	u8 device_status;
	/*
	 * bit 7 - 4: number of fingers currently touching pad
	 * bit 3: valid data check bit
	 * bit 2: middle mechanism button state if exists
	 * bit 1: right mechanism button state if exists
	 * bit 0: left mechanism button state if exists
	 */
	u8 finger_btn;
	struct cyapa_touch_gen3 touches[CYAPA_MAX_TOUCHES];
};

union cyapa_reg_data {
	struct cyapa_reg_data_gen2 gen2_data;
	struct cyapa_reg_data_gen3 gen3_data;
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


struct cyapa_mt_slot {
	struct cyapa_touch contact;
	bool touch_state;  /* true: is touched, false: not touched. */
	bool slot_updated;
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
	/* indicate interrupt enabled by cyapa driver. */
	bool irq_enabled;
	/* indicate interrupt enabled by trackpad device. */
	bool bl_irq_enable;
	enum cyapa_work_mode fw_work_mode;

	struct i2c_client	*client;
	struct input_dev	*input;
	struct delayed_work dwork;
	struct work_struct detect_work;
	struct workqueue_struct *detect_wq;
	enum cyapa_detect_status detect_status;
	/* synchronize access to dwork. */
	spinlock_t lock;
	int no_data_count;
	int scan_ms;
	int open_count;

	int irq;
	/* driver using polling mode if failed to request irq. */
	bool polling_mode_enabled;
	struct cyapa_platform_data *pdata;
	unsigned short data_base_offset;
	unsigned short control_base_offset;
	unsigned short command_base_offset;
	unsigned short query_base_offset;

	struct cyapa_mt_slot mt_slots[MAX_MT_SLOTS];

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

static unsigned char bl_switch_active[] = {0x00, 0xFF, 0x38,
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
static unsigned char bl_switch_idle[] = {0x00, 0xFF, 0x3B,
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
static unsigned char bl_app_launch[] = {0x00, 0xFF, 0xA5,
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

/* global pointer to trackpad touch data structure. */
static struct cyapa_i2c *global_touch;

static int cyapa_get_query_data(struct cyapa_i2c *touch);
static int cyapa_i2c_reconfig(struct cyapa_i2c *touch, int boot);
static void cyapa_get_reg_offset(struct cyapa_i2c *touch);
static int cyapa_determine_firmware_gen(struct cyapa_i2c *touch);
static int cyapa_create_input_dev(struct cyapa_i2c *touch);
static void cyapa_i2c_reschedule_work(struct cyapa_i2c *touch,
		unsigned long delay);


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
		if (report_data->touches[i].tracking_id != -1)
			pr_info("%s: report_data.touches[%d].tracking_id = %d\n",
				func, i, report_data->touches[i].tracking_id);
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
	unsigned long flags;

	spin_lock_irqsave(&touch->miscdev_spinlock, flags);
	if (!touch->polling_mode_enabled &&
		touch->bl_irq_enable &&
		!touch->irq_enabled) {
		touch->irq_enabled = true;
		enable_irq(touch->irq);
	}
	spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);
}

static void cyapa_disable_irq(struct cyapa_i2c *touch)
{
	unsigned long flags;

	spin_lock_irqsave(&touch->miscdev_spinlock, flags);
	if (!touch->polling_mode_enabled &&
		touch->bl_irq_enable &&
		touch->irq_enabled) {
		touch->irq_enabled = false;
		disable_irq(touch->irq);
	}
	spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);
}

static void cyapa_bl_enable_irq(struct cyapa_i2c *touch)
{
	unsigned long flags;

	spin_lock_irqsave(&touch->miscdev_spinlock, flags);
	if (touch->polling_mode_enabled)
		goto out;

	touch->bl_irq_enable = true;
	if (!touch->irq_enabled) {
		touch->irq_enabled = true;
		enable_irq(touch->irq);
	}

out:
	spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);
}

static void cyapa_bl_disable_irq(struct cyapa_i2c *touch)
{
	unsigned long flags;

	spin_lock_irqsave(&touch->miscdev_spinlock, flags);
	if (touch->polling_mode_enabled)
		goto out;

	touch->bl_irq_enable = false;
	if (touch->irq_enabled) {
		touch->irq_enabled = false;
		disable_irq(touch->irq);
	}

out:
	spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);
}

static int cyapa_acquire_i2c_bus(struct cyapa_i2c *touch)
{
	cyapa_disable_irq(touch);
	if (down_interruptible(&touch->reg_io_sem)) {
		cyapa_enable_irq(touch);
		return -ERESTARTSYS;
	}

	return 0;
}

static void cyapa_release_i2c_bus(struct cyapa_i2c *touch)
{
	up(&touch->reg_io_sem);
	cyapa_enable_irq(touch);
}

static s32 cyapa_i2c_reg_read_byte(struct cyapa_i2c *touch, u16 reg)
{
	int ret;

	ret = cyapa_acquire_i2c_bus(touch);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(touch->client, (u8)reg);

	cyapa_release_i2c_bus(touch);

	return ret;
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
	int ret;

	ret = cyapa_acquire_i2c_bus(touch);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(touch->client, (u8)reg, val);

	cyapa_release_i2c_bus(touch);

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
	int ret;
	u8 buf[1];

	ret = cyapa_acquire_i2c_bus(touch);
	if (ret < 0)
		return ret;

	/*
	 * step1: set read pointer of easy I2C.
	 */
	buf[0] = (u8)reg;
	ret = i2c_master_send(touch->client, buf, 1);
	if (ret < 0)
		goto error;

	/* step2: read data. */
	ret = i2c_master_recv(touch->client, values, length);
	if (ret < 0) {
		pr_debug("i2c_master_recv error, %d\n", ret);
		goto error;
	}

	if (ret != length)
		pr_warning("warning I2C block read bytes" \
			"[%d] not equal to requested bytes [%d].\n",
			ret, length);

	/* DEBUG: dump read block data */
	cyapa_dump_data_block(__func__, (u8)reg, ret, values);

error:
	cyapa_release_i2c_bus(touch);

	return ret;
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
	int ret;
	u8 buf[CYAPA_REG_MAP_SIZE + 1];

	cyapa_dump_data_block(__func__, reg, length, (void *)values);

	ret = cyapa_acquire_i2c_bus(touch);
	if (ret < 0)
		return ret;

	/*
	 * step1: write data to easy I2C in one command.
	 */
	buf[0] = (u8)reg;
	/* copy data shoud be write to I2C slave device. */
	memcpy((void *)&buf[1], (const void *)values, length);

	ret = i2c_master_send(touch->client, buf, length+1);
	if (ret < 0)
		goto error;

	/* one additional written byte is register offset. */
	if (ret != (length + 1))
		pr_warning("warning I2C block write bytes" \
			"[%d] not equal to requested bytes [%d].\n",
			ret, length);

error:
	cyapa_release_i2c_bus(touch);

	return (ret < 0) ? ret : (ret - 1);
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
	unsigned long flags;
	struct cyapa_i2c *touch = global_touch;

	if (touch == NULL)
		return -ENODEV;
	file->private_data = (void *)touch;

	spin_lock_irqsave(&touch->miscdev_spinlock, flags);
	if (touch->misc_open_count) {
		spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);
		return -EBUSY;
	}
	count = ++touch->misc_open_count;
	spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);

	return 0;
}

static int cyapa_misc_close(struct inode *inode, struct file *file)
{
	int count;
	unsigned long flags;
	struct cyapa_i2c *touch = (struct cyapa_i2c *)file->private_data;

	spin_lock_irqsave(&touch->miscdev_spinlock, flags);
	count = --touch->misc_open_count;
	spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);

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
	int ret;
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
	int ret;
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

int cyapa_get_trackpad_run_mode(struct cyapa_i2c *touch,
		struct cyapa_trackpad_run_mode *run_mode)
{
	int ret;
	char status[BL_HEAD_BYTES];
	int tries = 5;

	/* reset to unknown status. */
	run_mode->run_mode = CYAPA_BOOTLOADER_INVALID_STATE;
	run_mode->bootloader_state = CYAPA_BOOTLOADER_INVALID_STATE;

	do {
		/* get trackpad status. */
		ret = cyapa_i2c_reg_read_block(touch, 0, BL_HEAD_BYTES, status);
		if ((ret != BL_HEAD_BYTES) && (tries > 0)) {
			/*
			 * maybe, firmware is switching its states,
			 * wait for a moment.
			 */
			msleep(300);
			continue;
		}

		/* verify run mode and status. */
		if ((status[REG_OP_STATUS] == OP_STATUS_MASK) &&
			(status[REG_OP_DATA1] & OP_DATA_VALID) &&
			!((status[REG_BL_STATUS] & BL_STATUS_RUNNING) &&
				(status[REG_BL_KEY1] == BL_KEY1) &&
				(status[REG_BL_KEY2] == BL_KEY2) &&
				(status[REG_BL_KEY3] == BL_KEY3))) {
			run_mode->run_mode = CYAPA_OPERATIONAL_MODE;
			return 0;
		}

		if ((status[REG_BL_STATUS] & BL_STATUS_BUSY) && (tries > 0)) {
			msleep(300);
			continue;
		}

		if (status[REG_BL_STATUS] & BL_STATUS_RUNNING) {
			run_mode->run_mode = CYAPA_BOOTLOADER_MODE;
			if (status[REG_BL_ERROR] & BL_ERROR_BOOTLOADING)
				run_mode->bootloader_state =
					CYAPA_BOOTLOADER_ACTIVE_STATE;
			else
				run_mode->bootloader_state =
					CYAPA_BOOTLOADER_IDLE_STATE;

			return 0;
		}
	} while (tries-- > 0);

	if (tries < 0) {
		/* firmware may be in an unknown state. */
		pr_err("cyapa unknown trackpad firmware state.\n");
		return -EINVAL;
	}

	return 0;
}

static int cyapa_send_mode_switch_cmd(struct cyapa_i2c *touch,
		struct cyapa_trackpad_run_mode *run_mode)
{
	int ret;
	unsigned long flags;
	unsigned short reset_offset;

	if (touch->pdata->gen == CYAPA_GEN3)
		reset_offset = CYAPA_GEN3_OFFSET_SOFT_RESET;
	else if (touch->pdata->gen == CYAPA_GEN2)
		reset_offset = CYAPA_GEN2_OFFSET_SOFT_RESET;
	else
		return -EINVAL;

	switch (run_mode->rev_cmd) {
	case CYAPA_CMD_APP_TO_IDLE:
		/* do reset operation to switch to bootloader idle mode. */
		cyapa_bl_disable_irq(touch);

		ret = cyapa_i2c_reg_write_byte(touch, reset_offset, 0x01);
		if (ret < 0) {
			pr_err("send firmware reset cmd failed, %d\n",
				ret);
			cyapa_bl_enable_irq(touch);
			return -EIO;
		}
		break;

	case CYAPA_CMD_IDLE_TO_ACTIVE:
		cyapa_bl_disable_irq(touch);
		/* send switch to active command. */
		ret = cyapa_i2c_reg_write_block(touch, 0,
				sizeof(bl_switch_active), bl_switch_active);
		if (ret != sizeof(bl_switch_active)) {
			pr_err("send active switch cmd failed, %d\n",
				ret);
			return -EIO;
		}
		break;

	case CYAPA_CMD_ACTIVE_TO_IDLE:
		cyapa_bl_disable_irq(touch);
		/* send switch to idle command.*/
		ret = cyapa_i2c_reg_write_block(touch, 0,
				sizeof(bl_switch_idle), bl_switch_idle);
		if (ret != sizeof(bl_switch_idle)) {
			pr_err("send idle switch cmd failed, %d\n",
				ret);
			return -EIO;
		}
		break;

	case CYAPA_CMD_IDLE_TO_APP:
		/* send command switch operational mode.*/
		ret = cyapa_i2c_reg_write_block(touch, 0,
				sizeof(bl_app_launch), bl_app_launch);
		if (ret != sizeof(bl_app_launch)) {
			pr_err("send applaunch cmd failed, %d\n",
				ret);
			return -EIO;
		}

		/*
		 * wait firmware completely launched its application,
		 * during this time, all read/write operations should
		 * be disabled.
		 *
		 * NOTES:
		 * When trackpad boots for the first time after being
		 * updating to new firmware, it must first calibrate
		 * its sensors.
		 * This sensor calibration takes about 2 seconds to complete.
		 * This calibration is ONLY required for the first
		 * post-firmware-update boot.
		 *
		 * On all boots the driver waits 300 ms after switching to
		 * operational mode.
		 * For the first post-firmware-update boot,
		 * additional waiting is done in cyapa_i2c_reconfig().
		 */
		msleep(300);

		/* update firmware working mode state in driver. */
		spin_lock_irqsave(&touch->miscdev_spinlock, flags);
		touch->fw_work_mode = CYAPA_STREAM_MODE;
		spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);

		/* reconfig and update firmware information. */
		cyapa_i2c_reconfig(touch, 0);

		cyapa_bl_enable_irq(touch);

		break;

	default:
		/* unknown command. */
		return -EINVAL;
	}

	/* update firmware working mode state in driver. */
	if (run_mode->rev_cmd != CYAPA_CMD_IDLE_TO_APP) {
		spin_lock_irqsave(&touch->miscdev_spinlock, flags);
		touch->fw_work_mode = CYAPA_BOOTLOAD_MODE;
		spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);
	}

	return 0;
}

static long cyapa_misc_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	int ret;
	int ioctl_len;
	struct cyapa_i2c *touch = (struct cyapa_i2c *)file->private_data;
	struct cyapa_misc_ioctl_data ioctl_data;
	struct cyapa_trackpad_run_mode run_mode;
	unsigned char buf[8];

	if (touch == NULL) {
		pr_err("cypress trackpad device does not exist.\n");
		return -ENODEV;
	}

	/* copy to kernel space. */
	ioctl_len = sizeof(struct cyapa_misc_ioctl_data);
	if (copy_from_user(&ioctl_data, (u8 *)arg, ioctl_len))
		return -EINVAL;

	switch (cmd) {
	case CYAPA_GET_PRODUCT_ID:
		if (!ioctl_data.buf || ioctl_data.len < 16)
			return -EINVAL;

		ret = cyapa_get_query_data(touch);
		if (ret < 0)
			return ret;
		ioctl_data.len = 16;
		if (copy_to_user(ioctl_data.buf, touch->product_id, 16))
				return -EIO;
		if (copy_to_user((void *)arg, &ioctl_data, ioctl_len))
			return -EIO;
		return ioctl_data.len;

	case CYAPA_GET_DRIVER_VER:
		if (!ioctl_data.buf || ioctl_data.len < 3)
			return -EINVAL;

		ioctl_data.len = 3;
		memset(buf, 0, sizeof(buf));
		buf[0] = (unsigned char)CYAPA_MAJOR_VER;
		buf[1] = (unsigned char)CYAPA_MINOR_VER;
		buf[2] = (unsigned char)CYAPA_REVISION_VER;
		if (copy_to_user(ioctl_data.buf, buf, ioctl_data.len))
			return -EIO;
		if (copy_to_user((void *)arg, &ioctl_data, ioctl_len))
			return -EIO;
		return ioctl_data.len;

	case CYAPA_GET_FIRMWARE_VER:
		if (!ioctl_data.buf || ioctl_data.len < 2)
			return -EINVAL;

		ret = cyapa_get_query_data(touch);
		if (ret < 0)
			return ret;
		ioctl_data.len = 2;
		memset(buf, 0, sizeof(buf));
		buf[0] = touch->fw_maj_ver;
		buf[1] = touch->fw_min_ver;
		if (copy_to_user(ioctl_data.buf, buf, ioctl_data.len))
			return -EIO;
		if (copy_to_user((void *)arg, &ioctl_data, ioctl_len))
			return -EIO;
		return ioctl_data.len;

	case CYAPA_GET_HARDWARE_VER:
		if (!ioctl_data.buf || ioctl_data.len < 2)
			return -EINVAL;

		ret = cyapa_get_query_data(touch);
		if (ret < 0)
			return ret;
		ioctl_data.len = 2;
		memset(buf, 0, sizeof(buf));
		buf[0] = touch->hw_maj_ver;
		buf[1] = touch->hw_min_ver;
		if (copy_to_user(ioctl_data.buf, buf, ioctl_data.len))
			return -EIO;
		if (copy_to_user((void *)arg, &ioctl_data, ioctl_len))
			return -EIO;
		return ioctl_data.len;

	case CYAPA_GET_PROTOCOL_VER:
		if (!ioctl_data.buf || ioctl_data.len < 1)
			return -EINVAL;

		if (cyapa_determine_firmware_gen(touch) < 0)
			return -EINVAL;
		cyapa_get_reg_offset(touch);
		ioctl_data.len = 1;
		memset(buf, 0, sizeof(buf));
		buf[0] = touch->pdata->gen;
		if (copy_to_user(ioctl_data.buf, buf, ioctl_data.len))
			return -EIO;
		if (copy_to_user((void *)arg, &ioctl_data, ioctl_len))
			return -EIO;
		return ioctl_data.len;


	case CYAPA_GET_TRACKPAD_RUN_MODE:
		if (!ioctl_data.buf || ioctl_data.len < 2)
			return -EINVAL;

		/* get trackpad status. */
		ret = cyapa_get_trackpad_run_mode(touch, &run_mode);
		if (ret < 0)
			return ret;

		ioctl_data.len = 2;
		memset(buf, 0, sizeof(buf));
		buf[0] = run_mode.run_mode;
		buf[1] = run_mode.bootloader_state;
		if (copy_to_user(ioctl_data.buf, buf, ioctl_data.len))
			return -EIO;

		if (copy_to_user((void *)arg, &ioctl_data, ioctl_len))
			return -EIO;

		return ioctl_data.len;

	case CYAYA_SEND_MODE_SWITCH_CMD:
		if (!ioctl_data.buf || ioctl_data.len < 3)
			return -EINVAL;

		ret = copy_from_user(&run_mode, (u8 *)ioctl_data.buf,
			sizeof(struct cyapa_trackpad_run_mode));
		if (ret)
			return -EINVAL;

		return cyapa_send_mode_switch_cmd(touch, &run_mode);

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
 *******************************************************************
 * below routines export interfaces to sysfs file system.
 * so user can get firmware/driver/hardware information using cat command.
 * e.g.: use below command to get firmware version
 *      cat /sys/devices/platfrom/tegra-i2c.0/i2c-0/0-0067/firmware_version
 *******************************************************************
 */
ssize_t cyapa_show_fm_ver(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	struct i2c_client *client = to_i2c_client(dev);
	struct cyapa_i2c *touch = i2c_get_clientdata(client);

	ret = cyapa_get_query_data(touch);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d.%d\n", touch->fw_maj_ver, touch->fw_min_ver);
}

ssize_t cyapa_show_driver_ver(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d.%d.%d\n",
		CYAPA_MAJOR_VER, CYAPA_MINOR_VER, CYAPA_REVISION_VER);
}

ssize_t cyapa_show_hw_ver(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	struct i2c_client *client = to_i2c_client(dev);
	struct cyapa_i2c *touch = i2c_get_clientdata(client);

	ret = cyapa_get_query_data(touch);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d.%d\n", touch->hw_maj_ver, touch->hw_min_ver);
}

ssize_t cyapa_show_product_id(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	struct i2c_client *client = to_i2c_client(dev);
	struct cyapa_i2c *touch = i2c_get_clientdata(client);

	ret = cyapa_get_query_data(touch);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%s\n", touch->product_id);
}

ssize_t cyapa_show_protocol_version(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cyapa_i2c *touch = i2c_get_clientdata(client);

	if (cyapa_determine_firmware_gen(touch) < 0)
		return -EINVAL;
	cyapa_get_reg_offset(touch);

	return sprintf(buf, "%d\n", touch->pdata->gen);
}

static DEVICE_ATTR(firmware_version, S_IRUGO, cyapa_show_fm_ver, NULL);
static DEVICE_ATTR(driver_version, S_IRUGO, cyapa_show_driver_ver, NULL);
static DEVICE_ATTR(hardware_version, S_IRUGO, cyapa_show_hw_ver, NULL);
static DEVICE_ATTR(product_id, S_IRUGO, cyapa_show_product_id, NULL);
static DEVICE_ATTR(protocol_version, S_IRUGO, cyapa_show_protocol_version, NULL);

static struct attribute *cyapa_sysfs_entries[] = {
	&dev_attr_firmware_version.attr,
	&dev_attr_driver_version.attr,
	&dev_attr_hardware_version.attr,
	&dev_attr_product_id.attr,
	&dev_attr_protocol_version.attr,
	NULL,
};

static struct attribute_group cyapa_sysfs_group = {
	.attrs = cyapa_sysfs_entries,
};

/*
 **************************************************************
 * Cypress i2c trackpad input device driver.
 **************************************************************
*/
static void cyapa_get_reg_offset(struct cyapa_i2c *touch)
{
	if (touch->pdata->gen == CYAPA_GEN2) {
		touch->data_base_offset = GEN2_REG_OFFSET_DATA_BASE;
		touch->control_base_offset = GEN2_REG_OFFSET_CONTROL_BASE;
		touch->command_base_offset = GEN2_REG_OFFSET_COMMAND_BASE;
		touch->query_base_offset = GEN2_REG_OFFSET_QUERY_BASE;
	} else {
		touch->data_base_offset = GEN3_REG_OFFSET_DATA_BASE;
		touch->control_base_offset = GEN3_REG_OFFSET_CONTROL_BASE;
		touch->command_base_offset = GEN3_REG_OFFSET_COMMAND_BASE;
		touch->query_base_offset = GEN3_REG_OFFSET_QUERY_BASE;
	}
}

/*
 * this function read product id from trackpad device
 * and use it to verify trackpad firmware protocol
 * is consistent with platform data setting or not.
 */
static int cyapa_get_and_verify_firmware(struct cyapa_i2c *touch,
	unsigned char *query_data, unsigned short offset, int length)
{
	int loop = 20;
	int ret_read_size;
	char unique_str[] = "CYTRA";

	while (loop--) {
		ret_read_size = cyapa_i2c_reg_read_block(touch,
				offset,
				length,
				(char *)query_data);
		if (ret_read_size == length)
			break;

		/*
		 * When trackpad boots for first time after firmware update,
		 * it needs to calibrate all sensors, which takes nearly
		 * 2 seconds. During this calibration period,
		 * the trackpad will not reply to the block read command.
		 * This delay ONLY occurs immediately after firmware update.
		 */
		msleep(250);
	}
	if (loop < 0)
		return -EIO;  /* i2c bus operation error. */

	if (strncmp(query_data, unique_str, strlen(unique_str)) == 0)
		return 1;  /* read and verify firmware successfully. */
	else
		return 0;  /* unknown firmware query data. */
}

static int cyapa_determine_firmware_gen(struct cyapa_i2c *touch)
{
	int ret;
	unsigned long flags;
	unsigned short offset;
	unsigned char query_data[40];

	spin_lock_irqsave(&touch->miscdev_spinlock, flags);
	if (touch->fw_work_mode != CYAPA_STREAM_MODE) {
		/* firmware works in bootloader mode. */
		spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);

	/* determine firmware protocol consistent with driver setting. */
	if (touch->pdata->gen == CYAPA_GEN2)
		offset = GEN2_REG_OFFSET_QUERY_BASE;
	else
		offset = GEN3_REG_OFFSET_QUERY_BASE;
	memset(query_data, 0, sizeof(query_data));
	ret = cyapa_get_and_verify_firmware(touch, query_data, offset, PRODUCT_ID_SIZE);
	if (ret == 1) {
		/*
		 * current firmware protocol is consistent with the generation
		 * set in platform data.
		 */
		return 0;
	}

	if (touch->pdata->gen == CYAPA_GEN2) {
		/* guess its gen3 firmware protocol. */
		offset = GEN3_REG_OFFSET_QUERY_BASE;
		memset(query_data, 0, sizeof(query_data));
		ret = cyapa_get_and_verify_firmware(touch,
					query_data, offset, GEN3_QUERY_DATA_SIZE);
		if (ret == 1) {
			/* gen3 firmware protocol is verified successfully. */
			touch->pdata->gen = query_data[REG_PROTOCOL_GEN_QUERY_OFFSET] & 0x0F;
		}
	} else {
		/* guess its gen2 firmware protocol. */
		offset = GEN2_REG_OFFSET_QUERY_BASE;
		memset(query_data, 0, sizeof(query_data));
		ret = cyapa_get_and_verify_firmware(touch,
					query_data, offset, PRODUCT_ID_SIZE);
		if (ret == 1) {
			/* gen2 firmware protocol is verified successfully. */
			touch->pdata->gen = CYAPA_GEN2;
		}
	}

	/*
	 * when i2c bus I/O failed, ret < 0,
	 * it's unable to guess firmware protocol,
	 * so keep the default gen setting in platform data.
	 *
	 * when not gen2, gen3 or later protocol firmware, ret == 0,
	 * this trackpad driver may unable to support this device,
	 * so, here also keep the default value set in platform data.
	 */

	return ret == 1 ? 0 : -1;
}

static int cyapa_get_query_data(struct cyapa_i2c *touch)
{
	unsigned long flags;
	char query_data[40];
	int query_bytes;
	int ret_read_size;
	int i;

	spin_lock_irqsave(&touch->miscdev_spinlock, flags);
	if (touch->fw_work_mode != CYAPA_STREAM_MODE) {
		/* firmware works in bootloader mode. */
		spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);

	/* query data is supported only in GEN2 or later firmware protocol. */
	if (touch->pdata->gen == CYAPA_GEN2)
		query_bytes = GEN2_QUERY_DATA_SIZE;
	else
		query_bytes = GEN3_QUERY_DATA_SIZE;
	ret_read_size = cyapa_i2c_reg_read_block(touch,
				touch->query_base_offset,
				query_bytes,
				query_data);
	if (ret_read_size < 0)
		return ret_read_size;

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

	if (touch->pdata->gen == CYAPA_GEN2) {
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
	} else {
		touch->max_abs_x =
			(((query_data[21] & 0xF0) << 4) | query_data[22]);
		touch->max_abs_y =
			(((query_data[21] & 0x0F) << 8) | query_data[23]);

		touch->physical_size_x =
			(((query_data[24] & 0xF0) << 4) | query_data[25]);
		touch->physical_size_y =
			(((query_data[24] & 0x0F) << 8) | query_data[26]);
	}

	return 0;
}

static int cyapa_i2c_reconfig(struct cyapa_i2c *touch, int boot)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&touch->miscdev_spinlock, flags);
	if (touch->fw_work_mode != CYAPA_STREAM_MODE) {
		/* firmware works in bootloader mode. */
		spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);

	/*
	 * only support trackpad firmware gen2 or later protocol.
	 */
	if (cyapa_determine_firmware_gen(touch) < 0)
		return -EINVAL;
	if (touch->pdata->gen < CYAPA_GEN2) {
		pr_info("cyapa driver unsupported firmware protocol version.\n");
		return -EINVAL;
	}

	cyapa_get_reg_offset(touch);
	ret = cyapa_get_query_data(touch);
	if (ret < 0) {
		pr_err("Failed to get trackpad query data, %d.\n", ret);
		return ret;
	}

	if (boot) {
		/* output in one time, avoid multi-lines output be separated. */
		pr_info("Cypress Trackpad Information:\n" \
			"    Product ID:  %s\n" \
			"    Protocol Generation:  %d\n" \
			"    Firmware Version:  %d.%d\n" \
			"    Hardware Version:  %d.%d\n" \
			"    Driver Version:  %d.%d.%d\n" \
			"    Max ABS X,Y:   %d,%d\n" \
			"    Physical Size X,Y:   %d,%d\n",
			touch->product_id,
			touch->pdata->gen,
			touch->fw_maj_ver, touch->fw_min_ver,
			touch->hw_maj_ver, touch->hw_min_ver,
			CYAPA_MAJOR_VER, CYAPA_MINOR_VER, CYAPA_REVISION_VER,
			touch->max_abs_x, touch->max_abs_y,
			touch->physical_size_x, touch->physical_size_y
			);
	}

	return 0;
}

static int cyapa_i2c_reset_config(struct cyapa_i2c *touch)
{
	return 0;
}

static int cyapa_verify_data_device(struct cyapa_i2c *touch,
				union cyapa_reg_data *reg_data)
{
	unsigned char device_status;
	unsigned char flag;
	unsigned char *reg = (unsigned char *)reg_data;

	device_status = reg[REG_OP_STATUS];
	flag = reg[REG_OP_DATA1];
	if ((device_status & OP_STATUS_SRC) != OP_STATUS_SRC)
		return -EINVAL;

	if ((flag & OP_DATA_VALID) != OP_DATA_VALID)
		return -EINVAL;

	if ((device_status & OP_STATUS_DEV) != CYAPA_DEV_NORMAL)
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
	report_data->button = reg_data->relative_flags & OP_DATA_BTN_MASK;

	/* get relative delta X and delta Y. */
	report_data->rel_deltaX = reg_data->deltax;
	/* The Y direction of trackpad is opposite of screen. */
	report_data->rel_deltaY = -reg_data->deltay;

	/* copy fingers touch data */
	report_data->avg_pressure = reg_data->avg_pressure;
	report_data->touch_fingers =
		min(CYAPA_MAX_TOUCHES, (int)reg_data->touch_fingers);
	for (i = 0; i < report_data->touch_fingers; i++) {
		report_data->touches[i].x =
			((reg_data->touches[i].xy & 0xF0) << 4)
				| reg_data->touches[i].x;
		report_data->touches[i].y =
			((reg_data->touches[i].xy & 0x0F) << 8)
				| reg_data->touches[i].y;
		report_data->touches[i].pressure = reg_data->touches[i].pressure;
		report_data->touches[i].tracking_id = -1;
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

static void cyapa_parse_gen3_data(struct cyapa_i2c *touch,
		struct cyapa_reg_data_gen3 *reg_data,
		struct cyapa_report_data *report_data)
{
	int i;
	int fingers;

	/* only report left button. */
	report_data->button = reg_data->finger_btn & OP_DATA_BTN_MASK;
	report_data->avg_pressure = 0;
	/* parse number of touching fingers. */
	fingers = (reg_data->finger_btn >> 4) & 0x0F;
	report_data->touch_fingers = min(CYAPA_MAX_TOUCHES, fingers);

	/* parse data for each touched finger. */
	for (i = 0; i < report_data->touch_fingers; i++) {
		report_data->touches[i].x =
			((reg_data->touches[i].xy & 0xF0) << 4) |
				reg_data->touches[i].x;
		report_data->touches[i].y =
			((reg_data->touches[i].xy & 0x0F) << 8) |
				reg_data->touches[i].y;
		report_data->touches[i].pressure =
			reg_data->touches[i].pressure;
		report_data->touches[i].tracking_id =
			reg_data->touches[i].tracking_id;
	}
	report_data->gesture_count = 0;

	/* DEBUG: dump parsed report data */
	cyapa_dump_report_data(__func__, report_data);
}


static int cyapa_find_mt_slot(struct cyapa_i2c *touch,
		struct cyapa_touch *contact)
{
	int i;
	int empty_slot = -1;

	for (i = 0; i < MAX_MT_SLOTS; i++) {
		if ((touch->mt_slots[i].contact.tracking_id == contact->tracking_id) &&
			touch->mt_slots[i].touch_state)
			return i;

		if (!touch->mt_slots[i].touch_state && empty_slot == -1)
			empty_slot = i;
	}

	return empty_slot;
}

static void cyapa_update_mt_slots(struct cyapa_i2c *touch,
		struct cyapa_report_data *report_data)
{
	int i;
	int slotnum;

	for (i = 0; i < report_data->touch_fingers; i++) {
		slotnum = cyapa_find_mt_slot(touch, &report_data->touches[i]);
		if (slotnum < 0)
			continue;

		memcpy(&touch->mt_slots[slotnum].contact,
				&report_data->touches[i],
				sizeof(struct cyapa_touch));
		touch->mt_slots[slotnum].slot_updated = true;
		touch->mt_slots[slotnum].touch_state = true;
	}
}

static void cyapa_send_mtb_event(struct cyapa_i2c *touch,
		struct cyapa_report_data *report_data)
{
	int i;
	struct cyapa_mt_slot *slot;
	struct input_dev *input = touch->input;

	cyapa_update_mt_slots(touch, report_data);

	for (i = 0; i < MAX_MT_SLOTS; i++) {
		slot = &touch->mt_slots[i];
		if (!slot->slot_updated)
			slot->touch_state = false;

		input_mt_slot(input, i);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, slot->touch_state);
		if (slot->touch_state) {
			input_report_abs(input, ABS_MT_POSITION_X, slot->contact.x);
			input_report_abs(input, ABS_MT_POSITION_Y, slot->contact.y);
			input_report_abs(input, ABS_MT_PRESSURE, slot->contact.pressure);
		}
		slot->slot_updated = false;
	}

	input_mt_report_pointer_emulation(input, true);
	input_report_key(input, BTN_LEFT, report_data->button);
	input_sync(input);
}

/*
 * for compatible with gen2 and previous firmware
 * that do not support MT-B protocol
 */
static void cyapa_send_mta_event(struct cyapa_i2c *touch,
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
}

static int cyapa_handle_input_report_data(struct cyapa_i2c *touch,
		struct cyapa_report_data *report_data)
{
	if (touch->pdata->gen > CYAPA_GEN2)
		cyapa_send_mtb_event(touch, report_data);
	else
		cyapa_send_mta_event(touch, report_data);

	return report_data->touch_fingers | report_data->button;
}

static bool cyapa_i2c_get_input(struct cyapa_i2c *touch)
{
	int ret_read_size;
	int read_length;
	union cyapa_reg_data reg_data;
	struct cyapa_reg_data_gen2 *gen2_data;
	struct cyapa_reg_data_gen3 *gen3_data;
	struct cyapa_report_data report_data;

	/* read register data from trackpad. */
	gen2_data = &reg_data.gen2_data;
	gen3_data = &reg_data.gen3_data;
	if (touch->pdata->gen == CYAPA_GEN2)
		read_length = (int)sizeof(struct cyapa_reg_data_gen2);
	else
		read_length = (int)sizeof(struct cyapa_reg_data_gen3);

	ret_read_size = cyapa_i2c_reg_read_block(touch,
					DATA_REG_START_OFFSET,
					read_length,
					(char *)&reg_data);
	if (ret_read_size < 0)
		return 0;

	if (cyapa_verify_data_device(touch, &reg_data) < 0)
		return 0;

	/* process and parse raw data read from Trackpad. */
	if (touch->pdata->gen == CYAPA_GEN2)
		cyapa_parse_gen2_data(touch, gen2_data, &report_data);
	else
		cyapa_parse_gen3_data(touch, gen3_data, &report_data);

	/* report data to input subsystem. */
	return cyapa_handle_input_report_data(touch, &report_data);
}

/* Control driver polling read rate and work handler sleep time */
static unsigned long cyapa_i2c_adjust_delay(struct cyapa_i2c *touch,
		bool have_data)
{
	unsigned long delay, nodata_count_thres;

	if (!touch->polling_mode_enabled) {
		delay = msecs_to_jiffies(CYAPA_THREAD_IRQ_SLEEP_MSECS);
		return round_jiffies_relative(delay);
	}

	if (touch->scan_ms <= 0)
		touch->scan_ms = CYAPA_POLLING_REPORTRATE_DEFAULT;
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
	unsigned long flags;

	/*
	 * use spinlock to avoid confict accessing
	 * when firmware switching into bootloader mode.
	 */
	spin_lock_irqsave(&touch->miscdev_spinlock, flags);
	if (touch->detect_status != CYAPA_DETECT_DONE_SUCCESS) {
		/* still detecting trackpad device in work queue. */
		spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);
		return;
	}
	if (touch->fw_work_mode == CYAPA_BOOTLOAD_MODE) {
		spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);
		cyapa_update_firmware_dispatch(touch);
	} else {
		spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);

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
		if (touch->polling_mode_enabled)
			cyapa_i2c_reschedule_work(touch, delay);
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
	/*
	 * check bl_irq_enable value to avoid mistriggered interrupt
	 * when switching from operational mode
	 * to bootloader mode.
	 */
	if (touch->polling_mode_enabled || touch->bl_irq_enable)
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
	int ret;

	if (0 == touch->open_count) {
		ret = cyapa_i2c_reset_config(touch);
		if (ret < 0) {
			pr_err("reset i2c trackpad error code, %d.\n", ret);
			return ret;
		}
	}
	touch->open_count++;

	if (touch->polling_mode_enabled) {
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
	unsigned long flags;
	struct cyapa_i2c *touch = input_get_drvdata(input);

	touch->open_count--;

	if (0 == touch->open_count) {
		spin_lock_irqsave(&touch->lock, flags);
		cancel_delayed_work_sync(&touch->dwork);
		spin_unlock_irqrestore(&touch->lock, flags);
	}
}

static struct cyapa_i2c *cyapa_i2c_touch_create(struct i2c_client *client)
{
	struct cyapa_i2c *touch;

	touch = kzalloc(sizeof(struct cyapa_i2c), GFP_KERNEL);
	if (!touch)
		return NULL;

	touch->pdata = (struct cyapa_platform_data *)client->dev.platform_data;

	touch->scan_ms = touch->pdata->report_rate ?
		(1000 / touch->pdata->report_rate) : 0;
	touch->open_count = 0;
	touch->client = client;
	touch->polling_mode_enabled = false;
	global_touch = touch;
	touch->fw_work_mode = CYAPA_BOOTLOAD_MODE;
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
	int ret;
	struct input_dev *input = NULL;

	input = touch->input = input_allocate_device();
	if (!touch->input) {
		pr_err("Allocate memory for Input device failed\n");
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

	/* finger position */
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, touch->max_abs_x, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, touch->max_abs_y, 0, 0);
	input_set_abs_params(input, ABS_MT_PRESSURE, 0, 255, 0, 0);
	if (touch->pdata->gen > CYAPA_GEN2) {
		ret = input_mt_init_slots(input, MAX_MT_SLOTS);
		if (ret < 0)
			return ret;

	} else
		input_set_events_per_packet(input, 60);

	if (touch->physical_size_x && touch->physical_size_y) {
		input_abs_set_res(input, ABS_X,
			touch->max_abs_x / touch->physical_size_x);
		input_abs_set_res(input, ABS_Y,
			touch->max_abs_y / touch->physical_size_y);
		input_abs_set_res(input, ABS_MT_POSITION_X,
			touch->max_abs_x / touch->physical_size_x);
		input_abs_set_res(input, ABS_MT_POSITION_Y,
			touch->max_abs_y / touch->physical_size_y);
	}

	__set_bit(EV_KEY, input->evbit);
	__set_bit(BTN_TOUCH, input->keybit);
	__set_bit(BTN_TOOL_FINGER, input->keybit);
	__set_bit(BTN_TOOL_DOUBLETAP, input->keybit);
	__set_bit(BTN_TOOL_TRIPLETAP, input->keybit);
	__set_bit(BTN_TOOL_QUADTAP, input->keybit);

	__set_bit(BTN_LEFT, input->keybit);

	/* Register the device in input subsystem */
	ret = input_register_device(touch->input);
	if (ret) {
		pr_err("Input device register failed, %d\n", ret);
		input_free_device(input);
	}

	return ret;
}

static int cyapa_check_exit_bootloader(struct cyapa_i2c *touch)
{
	int ret;
	int tries = 15;
	unsigned long flags;
	struct cyapa_trackpad_run_mode run_mode;

	do {
		if ((cyapa_get_trackpad_run_mode(touch, &run_mode) < 0) &&
			(tries > 0)) {
			msleep(300);
			continue;
		}

		if (run_mode.run_mode == CYAPA_OPERATIONAL_MODE) {
			spin_lock_irqsave(&touch->miscdev_spinlock, flags);
			touch->fw_work_mode = CYAPA_STREAM_MODE;
			spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);
			break;
		}

		if ((run_mode.run_mode == CYAPA_BOOTLOADER_MODE) &&
			(run_mode.bootloader_state ==
				CYAPA_BOOTLOADER_ACTIVE_STATE)) {
			/* bootloader active state. */
			ret = cyapa_i2c_reg_write_block(touch, 0,
				sizeof(bl_switch_idle), bl_switch_idle);

			if (ret != sizeof(bl_switch_idle))
				continue;

			/* wait bootloader switching to idle state. */
			msleep(300);
			continue;
		}

		if ((run_mode.run_mode == CYAPA_BOOTLOADER_MODE) &&
			(run_mode.bootloader_state ==
				CYAPA_BOOTLOADER_IDLE_STATE)) {
			/* send command switch to operational mode. */
			ret = cyapa_i2c_reg_write_block(touch, 0,
				sizeof(bl_app_launch), bl_app_launch);

			if (ret != sizeof(bl_app_launch))
				continue;

			/* wait firmware ready. */
			msleep(300);
			continue;
		}
	} while (tries--);

	if (tries < 0)
		return -EIO;

	return 0;
}

static int cyapa_set_power_mode(struct cyapa_i2c *touch, u8 power_mode)
{
	int ret;
	u8 power;
	int tries = 3;

	power = cyapa_i2c_reg_read_byte(touch, REG_OFFSET_POWER_MODE);
	power &= ~OP_POWER_MODE_MASK;
	power |= ((power_mode << OP_POWER_MODE_SHIFT) & OP_POWER_MODE_MASK);
	do {
		ret = cyapa_i2c_reg_write_byte(touch,
				REG_OFFSET_POWER_MODE, power);
		/* sleep at least 10 ms. */
		usleep_range(SET_POWER_MODE_DELAY, 2 * SET_POWER_MODE_DELAY);
	} while ((ret != 0) && (tries-- > 0));

	return ret;
}

static void cyapa_probe_detect_work_handler(struct work_struct *work)
{
	int ret;
	unsigned long flags;
	struct cyapa_i2c *touch =
		container_of(work, struct cyapa_i2c, detect_work);
	struct i2c_client *client = touch->client;

	ret = cyapa_check_exit_bootloader(touch);
	if (ret < 0) {
		pr_err("cyapa check and exit bootloader failed.\n");
		goto out_probe_err;
	}

	/*
	 * set irq number for interrupt mode.
	 * normally, polling mode only will be used
	 * when special platform that do not support slave interrupt.
	 * or allocate irq number to it failed.
	 */
	if (touch->pdata->irq_gpio <= 0)
		touch->irq = client->irq ? client->irq : -1;
	else
		touch->irq = gpio_to_irq(touch->pdata->irq_gpio);

	if (touch->irq <= 0) {
		pr_err("failed to allocate irq\n");
		ret = -EBUSY;
		goto out_probe_err;
	}

	set_irq_type(touch->irq, IRQF_TRIGGER_FALLING);
	ret = request_irq(touch->irq,
			cyapa_i2c_irq,
			0,
			CYAPA_I2C_NAME,
			touch);
	if (ret) {
		pr_warning("IRQ request failed: %d, "
			"falling back to polling mode.\n", ret);

		spin_lock_irqsave(&touch->miscdev_spinlock, flags);
		touch->polling_mode_enabled = true;
		touch->bl_irq_enable = false;
		touch->irq_enabled = false;
		spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);
	} else {
		spin_lock_irqsave(&touch->miscdev_spinlock, flags);
		touch->polling_mode_enabled = false;
		touch->bl_irq_enable = false;
		touch->irq_enabled = true;
		enable_irq_wake(touch->irq);
		spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);
	}

	/*
	 * reconfig trackpad depending on platform setting.
	 *
	 * always pass through after reconfig returned to given a chance
	 * that user can update trackpad firmware through cyapa interface
	 * when current firmware protocol is not supported.
	 */
	cyapa_i2c_reconfig(touch, true);

	/* create an input_dev instance for trackpad device. */
	ret = cyapa_create_input_dev(touch);
	if (ret) {
		free_irq(touch->irq, touch);
		pr_err("create input_dev instance failed.\n");
		goto out_probe_err;
	}

	i2c_set_clientdata(client, touch);

	ret = sysfs_create_group(&client->dev.kobj, &cyapa_sysfs_group);
	if (ret)
		pr_warning("error creating sysfs entries.\n");

	spin_lock_irqsave(&touch->miscdev_spinlock, flags);
	touch->detect_status = CYAPA_DETECT_DONE_SUCCESS;
	if (touch->irq_enabled)
		touch->bl_irq_enable = true;
	spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);

	return;

out_probe_err:
	spin_lock_irqsave(&touch->miscdev_spinlock, flags);
	touch->detect_status = CYAPA_DETECT_DONE_FAILED;
	spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);

	/* release previous allocated input_dev instances. */
	if (touch->input) {
		if (touch->input->mt)
			input_mt_destroy_slots(touch->input);
		input_free_device(touch->input);
		touch->input = NULL;
	}

	kfree(touch);
	global_touch = NULL;
}

static int cyapa_probe_detect(struct cyapa_i2c *touch)
{
	/*
	 * Maybe trackpad device is not connected,
	 * or firmware is doing sensor calibration,
	 * it will take max 2 seconds to be completed.
	 * So use work queue to wait for it ready
	 * to avoid block system booting or resuming.
	 */
	INIT_WORK(&touch->detect_work, cyapa_probe_detect_work_handler);
	return queue_work(touch->detect_wq, &touch->detect_work);
}

static void cyapa_resume_detect_work_handler(struct work_struct *work)
{
	int ret;
	unsigned long flags;
	struct cyapa_i2c *touch =
		container_of(work, struct cyapa_i2c, detect_work);

	/*
	 * when waking up, the first step that driver should do is to
	 * set trackpad device to full active mode. Do other read/write
	 * operations may get invalid data or get failed.
	 * And if set power mode failed, maybe the reason is that trackpad
	 * is working in bootloader mode, so do not check the return
	 * result here.
	 */
	ret = cyapa_set_power_mode(touch, PWR_MODE_FULL_ACTIVE);
	if (ret < 0)
		pr_warning("set wake up power mode to trackpad failed\n");

	ret = cyapa_check_exit_bootloader(touch);
	if (ret < 0) {
		pr_err("cyapa check and exit bootloader failed.\n");
		goto out_resume_err;
	}

	/* re-enable interrupt work handler routine. */
	spin_lock_irqsave(&touch->miscdev_spinlock, flags);
	if (touch->irq_enabled)
		touch->bl_irq_enable = true;
	spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);

	ret = cyapa_i2c_reset_config(touch);
	if (ret < 0) {
		pr_err("reset and config trackpad device failed.\n");
		goto out_resume_err;
	}

	cyapa_i2c_reschedule_work(touch,
		msecs_to_jiffies(CYAPA_NO_DATA_SLEEP_MSECS));

out_resume_err:
	/* trackpad device resumed from sleep state successfully. */
	spin_lock_irqsave(&touch->miscdev_spinlock, flags);
	touch->detect_status = ret ? CYAPA_DETECT_DONE_FAILED :
					CYAPA_DETECT_DONE_SUCCESS;
	spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);

	return;
}

static int cyapa_resume_detect(struct cyapa_i2c *touch)
{
	unsigned long flags;

	spin_lock_irqsave(&touch->miscdev_spinlock, flags);
	touch->bl_irq_enable = false;
	touch->fw_work_mode = CYAPA_BOOTLOAD_MODE;
	spin_unlock_irqrestore(&touch->miscdev_spinlock, flags);

	/*
	 * Maybe trackpad device is not connected,
	 * or firmware is doing sensor calibration,
	 * it will take max 2 seconds to be completed.
	 * So use work queue to wait for it ready
	 * to avoid block system booting or resuming.
	 */
	INIT_WORK(&touch->detect_work, cyapa_resume_detect_work_handler);
	return queue_work(touch->detect_wq, &touch->detect_work);
}

static int __devinit cyapa_i2c_probe(struct i2c_client *client,
			       const struct i2c_device_id *dev_id)
{
	int ret;
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
		ret = touch->pdata->init();
		if (ret) {
			pr_err("board initialize failed: %d\n", ret);
			goto err_mem_free;
		}
	}

	touch->detect_wq = create_singlethread_workqueue("cyapa_detect_wq");
	if (!touch->detect_wq) {
		pr_err("failed to create cyapa trackpad detect workqueue.\n");
		goto err_mem_free;
	}

	ret = cyapa_probe_detect(touch);
	if (ret < 0) {
		pr_err("cyapa i2c trackpad device detect failed, %d\n", ret);
		goto err_mem_free;
	}

	return 0;

err_mem_free:
	if (touch->detect_wq)
		destroy_workqueue(touch->detect_wq);
	kfree(touch);
	global_touch = NULL;

	return ret;
}

static int __devexit cyapa_i2c_remove(struct i2c_client *client)
{
	struct cyapa_i2c *touch = i2c_get_clientdata(client);

	sysfs_remove_group(&client->dev.kobj, &cyapa_sysfs_group);

	cancel_delayed_work_sync(&touch->dwork);

	if (!touch->polling_mode_enabled) {
		disable_irq_wake(touch->irq);
		free_irq(touch->irq, touch);
	}

	if (touch->input) {
		if (touch->input->mt)
			input_mt_destroy_slots(touch->input);
		input_unregister_device(touch->input);
	}

	if (touch->detect_wq)
		destroy_workqueue(touch->detect_wq);
	kfree(touch);
	global_touch = NULL;

	return 0;
}

#ifdef CONFIG_PM
static int cyapa_i2c_suspend(struct device *dev)
{
	int ret;
	struct i2c_client *client = to_i2c_client(dev);
	struct cyapa_i2c *touch = i2c_get_clientdata(client);

	/*
	 * When cyapa driver probing failed and haven't been removed,
	 * then when system do suspending, the value of touch is NULL.
	 * e.g.: this situation will happen when system booted
	 * without trackpad connected.
	 */
	if (!touch)
		return 0;

	if (touch->detect_wq)
		flush_workqueue(touch->detect_wq);

	cancel_delayed_work_sync(&touch->dwork);

	/* set trackpad device to light sleep mode. */
	ret = cyapa_set_power_mode(touch, PWR_MODE_LIGHT_SLEEP);
	if (ret < 0)
		pr_err("suspend cyapa trackpad device failed, %d\n", ret);

	return ret;
}

static int cyapa_i2c_resume(struct device *dev)
{
	int ret;
	struct i2c_client *client = to_i2c_client(dev);
	struct cyapa_i2c *touch = i2c_get_clientdata(client);

	/*
	 * When cyapa driver probing failed and haven't been removed,
	 * then when system do suspending, the value of touch is NULL.
	 * e.g.: this situation will happen when system booted
	 * without trackpad connected.
	 */
	if (!touch)
		return 0;

	if (touch->pdata->wakeup) {
		ret = touch->pdata->wakeup();
		if (ret) {
			pr_err("wakeup failed, %d\n", ret);
			return ret;
		}
	}

	ret = cyapa_resume_detect(touch);
	if (ret < 0) {
		pr_err("cyapa i2c trackpad device detect failed, %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops cyapa_pm_ops = {
	.suspend = cyapa_i2c_suspend,
	.resume = cyapa_i2c_resume,
};
#endif

static const struct i2c_device_id cypress_i2c_id_table[] = {
	{ CYAPA_I2C_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, cypress_i2c_id_table);

static struct i2c_driver cypress_i2c_driver = {
	.driver = {
		.name = CYAPA_I2C_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &cyapa_pm_ops,
#endif
	},

	.probe = cyapa_i2c_probe,
	.remove = __devexit_p(cyapa_i2c_remove),
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

	/*
	 * though misc cyapa interface device initialization may failed,
	 * but it won't affect the function of trackpad device when
	 * cypress_i2c_driver initialized successfully.
	 * misc init failure will only affect firmware upload function,
	 * so do not check cyapa_misc_init return value here.
	 */
	cyapa_misc_init();

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
