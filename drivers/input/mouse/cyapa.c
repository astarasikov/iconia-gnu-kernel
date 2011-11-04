/*
 * Cypress APA trackpad with I2C interface
 *
 * Author: Dudley Du <dudl@cypress.com>
 *
 * Copyright (C) 2011 Cypress Semiconductor, Inc.
 * Copyright (C) 2011 Google, Inc.
 *
 * Based on synaptics_i2c driver:
 *   Copyright (C) 2009 Compulab, Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */


#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/i2c/cyapa.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>


#define CYAPA_MAX_TOUCHES  5
/*
 * In the special case, where a finger is removed and makes contact
 * between two packets, there will be two touches for that finger,
 * with different tracking_ids.
 * Thus, the maximum number of slots must be twice the maximum number
 * of fingers.
 */
#define CYAPA_MAX_MT_SLOTS  (2 * CYAPA_MAX_TOUCHES)

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
 * bit 4: Bootloader running
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
#define QUERY_DATA_SIZE  27
#define REG_PROTOCOL_GEN_QUERY_OFFSET  20

#define REG_OFFSET_DATA_BASE     0x0000
#define REG_OFFSET_CONTROL_BASE  0x0000
#define REG_OFFSET_COMMAND_BASE  0x0028
#define REG_OFFSET_QUERY_BASE    0x002A

#define CYAPA_OFFSET_SOFT_RESET  REG_OFFSET_COMMAND_BASE

#define REG_OFFSET_POWER_MODE (REG_OFFSET_COMMAND_BASE + 1)
#define OP_POWER_MODE_MASK     0xC0
#define OP_POWER_MODE_SHIFT    6
#define PWR_MODE_FULL_ACTIVE   3
#define PWR_MODE_LIGHT_SLEEP   2
#define PWR_MODE_MEDIUM_SLEEP  1
#define PWR_MODE_DEEP_SLEEP    0
#define SET_POWER_MODE_DELAY   10000  /* unit: us */

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
 * CYAPA trackpad device states.
 * Used in register 0x00, bit1-0, DeviceStatus field.
 * After trackpad boots, and can report data, it sets this value.
 * Other values indicate device is in an abnormal state and must be reset.
 */
#define CYAPA_DEV_NORMAL  0x03

struct cyapa_touch {
	int x;
	int y;
	int pressure;
	int tracking_id;
};

struct cyapa_touch_data {
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

struct cyapa_reg_data {
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
	struct cyapa_touch_data touches[CYAPA_MAX_TOUCHES];
};

struct cyapa_report_data {
	u8 button;
	int touch_fingers;
	struct cyapa_touch touches[CYAPA_MAX_TOUCHES];
};


struct cyapa_mt_slot {
	struct cyapa_touch contact;
	bool touch_state;  /* true: is touched, false: not touched. */
	bool slot_updated;
};

/* The main device structure */
struct cyapa {
	/* synchronize accessing members of cyapa data structure. */
	spinlock_t miscdev_spinlock;
	/* synchronize accessing and updating file->f_pos. */
	struct mutex misc_mutex;
	int misc_open_count;
	/* indicate interrupt enabled by cyapa driver. */
	bool irq_enabled;
	/* indicate interrupt enabled by trackpad device. */
	bool bl_irq_enable;
	bool in_bootloader;

	struct i2c_client	*client;
	struct input_dev	*input;
	struct delayed_work dwork;
	struct work_struct detect_work;
	struct workqueue_struct *detect_wq;
	enum cyapa_detect_status detect_status;
	/* synchronize access to dwork. */
	spinlock_t lock;
	int irq;

	struct cyapa_mt_slot mt_slots[CYAPA_MAX_MT_SLOTS];

	/* read from query data region. */
	char product_id[16];
	u8 capability[14];
	u8 fw_maj_ver;  /* firmware major version. */
	u8 fw_min_ver;  /* firmware minor version. */
	u8 hw_maj_ver;  /* hardware major version. */
	u8 hw_min_ver;  /* hardware minor version. */
	enum cyapa_gen gen;
	int max_abs_x;
	int max_abs_y;
	int physical_size_x;
	int physical_size_y;
};

static const u8 bl_switch_active[] = { 0x00, 0xFF, 0x38, 0x00, 0x01, 0x02,
		0x03, 0x04, 0x05, 0x06, 0x07 };
static const u8 bl_switch_idle[] = { 0x00, 0xFF, 0x3B, 0x00, 0x01, 0x02, 0x03,
		0x04, 0x05, 0x06, 0x07 };
static const u8 bl_app_launch[] = { 0x00, 0xFF, 0xA5, 0x00, 0x01, 0x02, 0x03,
		0x04, 0x05, 0x06, 0x07 };

/* global pointer to trackpad touch data structure. */
static struct cyapa *global_cyapa;

static int cyapa_get_query_data(struct cyapa *cyapa);
static int cyapa_reconfig(struct cyapa *cyapa, int boot);
static int cyapa_determine_firmware_gen3(struct cyapa *cyapa);
static int cyapa_create_input_dev(struct cyapa *cyapa);
static void cyapa_reschedule_work(struct cyapa *cyapa, unsigned long delay);


#define BYTE_PER_LINE  8
void cyapa_dump_data(struct cyapa *cyapa, size_t length, const u8 *data)
{
	struct device *dev = &cyapa->client->dev;
	int i;
	char buf[BYTE_PER_LINE * 3 + 1];
	char *s = buf;

	for (i = 0; i < length; i++) {
		s += sprintf(s, " %02x", data[i]);
		if ((i + 1) == length || ((i + 1) % BYTE_PER_LINE) == 0) {
			dev_dbg(dev, "%s\n", buf);
			s = buf;
		}
	}
}
#undef BYTE_PER_LINE

void cyapa_dump_report(struct cyapa *cyapa,
		       const struct cyapa_report_data *report_data)
{
	struct device *dev = &cyapa->client->dev;
	int i;

	dev_dbg(dev, "------------------------------------\n");
	dev_dbg(dev, "button = 0x%02x\n",
		report_data->button);
	dev_dbg(dev, "touch_fingers = %d\n",
		report_data->touch_fingers);
	for (i = 0; i < report_data->touch_fingers; i++) {
		dev_dbg(dev, "touch[%d].x = %d\n",
			i, report_data->touches[i].x);
		dev_dbg(dev, "touch[%d].y = %d\n",
			i, report_data->touches[i].y);
		dev_dbg(dev, "touch[%d].pressure = %d\n",
			i, report_data->touches[i].pressure);
		if (report_data->touches[i].tracking_id != -1)
			dev_dbg(dev, "touch[%d].tracking_id = %d\n",
				i, report_data->touches[i].tracking_id);
	}
	dev_dbg(dev, "-------------------------------------\n");
}

static void cyapa_bl_enable_irq(struct cyapa *cyapa)
{
	unsigned long flags;

	spin_lock_irqsave(&cyapa->miscdev_spinlock, flags);
	cyapa->bl_irq_enable = true;
	if (!cyapa->irq_enabled) {
		cyapa->irq_enabled = true;
		enable_irq(cyapa->irq);
	}
	spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);
}

static void cyapa_bl_disable_irq(struct cyapa *cyapa)
{
	unsigned long flags;

	spin_lock_irqsave(&cyapa->miscdev_spinlock, flags);
	cyapa->bl_irq_enable = false;
	if (cyapa->irq_enabled) {
		cyapa->irq_enabled = false;
		disable_irq(cyapa->irq);
	}
	spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);
}

static s32 cyapa_reg_read_byte(struct cyapa *cyapa, u8 reg)
{
	return i2c_smbus_read_byte_data(cyapa->client, reg);
}

/*
 * cyapa_reg_write_byte - write one byte to i2c register map.
 * @cyapa - private data structure of the trackpad driver.
 * @reg - the offset value of the i2c register map from offset 0.
 * @val - the value should be written to the register map.
 *
 * This function returns negative errno, else zero on success.
 */
static s32 cyapa_reg_write_byte(struct cyapa *cyapa, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(cyapa->client, reg, val);
}

/*
 * cyapa_reg_read_block - read a block of data from trackpad registers.
 * @cyapa - private data structure of trackpad driver.
 * @reg - register at which to start reading.
 * @length - length of block to read, in bytes.
 * @values - buffer to store values read from register block.
 *
 * Returns negative errno, else number of bytes read.
 *
 * Note: The trackpad register block is 256 bytes.
 */
static ssize_t cyapa_reg_read_block(struct cyapa *cyapa, u8 reg, size_t len,
				    u8 *values)
{
	struct device *dev = &cyapa->client->dev;
	ssize_t ret;

	ret = i2c_smbus_read_i2c_block_data(cyapa->client, reg, len, values);
	if (ret > 0) {
		dev_dbg(dev, "read block reg: 0x%02x length: %d\n", reg, len);
		cyapa_dump_data(cyapa, ret, values);
	}

	return ret;
}

/*
 * cyapa_reg_write_block - write a block of data to trackpad registers.
 * @cyapa - private data structure of trackpad driver.
 * @reg - register at which to start writing.
 * @length - length of block to write, in bytes.
 * @values - buffer to write to register block.
 *
 * Returns negative errno, else number of bytes written.
 *
 * Note: The trackpad register block is 256 bytes.
 */
static ssize_t cyapa_reg_write_block(struct cyapa *cyapa, u8 reg,
				     size_t len, const u8 *values)
{
	struct device *dev = &cyapa->client->dev;
	ssize_t ret;

	dev_dbg(dev, "write block reg: 0x%02x length: %d\n", reg, len);
	cyapa_dump_data(cyapa, len, values);

	ret = i2c_smbus_write_i2c_block_data(cyapa->client, reg, len, values);
	return (ret == 0) ? len : ret;
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
	struct cyapa *cyapa = global_cyapa;

	if (cyapa == NULL)
		return -ENODEV;
	file->private_data = (void *)cyapa;

	spin_lock_irqsave(&cyapa->miscdev_spinlock, flags);
	if (cyapa->misc_open_count) {
		spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);
		return -EBUSY;
	}
	count = ++cyapa->misc_open_count;
	spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);

	return 0;
}

static int cyapa_misc_close(struct inode *inode, struct file *file)
{
	int count;
	unsigned long flags;
	struct cyapa *cyapa = (struct cyapa *)file->private_data;

	spin_lock_irqsave(&cyapa->miscdev_spinlock, flags);
	count = --cyapa->misc_open_count;
	spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);

	return 0;
}

static int cyapa_pos_validate(unsigned int pos)
{
	return (pos >= 0) && (pos < CYAPA_REG_MAP_SIZE);
}

static loff_t cyapa_misc_llseek(struct file *file, loff_t offset, int origin)
{
	loff_t ret = -EINVAL;
	struct cyapa *cyapa = (struct cyapa *)file->private_data;
	struct device *dev = &cyapa->client->dev;

	if (cyapa == NULL) {
		dev_err(dev, "cypress trackpad device does not exit.\n");
		return -ENODEV;
	}

	mutex_lock(&cyapa->misc_mutex);
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
	mutex_unlock(&cyapa->misc_mutex);

	return ret;
}

static int cyapa_miscdev_rw_params_check(struct cyapa *cyapa,
	unsigned long offset, unsigned int length)
{
	struct device *dev = &cyapa->client->dev;
	unsigned int max_offset;

	if (cyapa == NULL)
		return -ENODEV;

	/*
	 * application may read/write 0 length byte
	 * to reset read/write pointer to offset.
	 */
	max_offset = (length == 0) ? offset : (length - 1 + offset);

	/* max registers contained in one register map in bytes is 256. */
	if (cyapa_pos_validate(offset) && cyapa_pos_validate(max_offset))
		return 0;

	dev_warn(dev, "invalid parameters, length=%d, offset=0x%x\n", length,
		 (unsigned int)offset);

	return -EINVAL;
}

static ssize_t cyapa_misc_read(struct file *file, char __user *usr_buf,
		size_t count, loff_t *offset)
{
	int ret;
	int reg_len = (int)count;
	unsigned long reg_offset = *offset;
	u8 reg_buf[CYAPA_REG_MAP_SIZE];
	struct cyapa *cyapa = (struct cyapa *)file->private_data;
	struct device *dev = &cyapa->client->dev;

	ret = cyapa_miscdev_rw_params_check(cyapa, reg_offset, count);
	if (ret < 0)
		return ret;

	ret = cyapa_reg_read_block(cyapa, (u16)reg_offset, reg_len, reg_buf);
	if (ret < 0) {
		dev_err(dev, "I2C read FAILED.\n");
		return ret;
	}

	if (ret < reg_len)
		dev_warn(dev, "Expected %d bytes, read %d bytes.\n",
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
	u8 reg_buf[CYAPA_REG_MAP_SIZE];
	struct cyapa *cyapa = (struct cyapa *)file->private_data;

	ret = cyapa_miscdev_rw_params_check(cyapa, reg_offset, count);
	if (ret < 0)
		return ret;

	if (copy_from_user(reg_buf, usr_buf, (int)count))
		return -EINVAL;

	ret = cyapa_reg_write_block(cyapa,
					(u16)reg_offset,
					(int)count,
					reg_buf);

	*offset = (ret < 0) ? reg_offset : (reg_offset + ret);

	return ret;
}

int cyapa_get_trackpad_run_mode(struct cyapa *cyapa,
				struct cyapa_trackpad_run_mode *run_mode)
{
	struct device *dev = &cyapa->client->dev;
	int ret;
	u8 status[BL_HEAD_BYTES];
	int tries = 5;

	/* reset to unknown status. */
	run_mode->run_mode = CYAPA_BOOTLOADER_INVALID_STATE;
	run_mode->bootloader_state = CYAPA_BOOTLOADER_INVALID_STATE;

	do {
		/* get trackpad status. */
		ret = cyapa_reg_read_block(cyapa, 0, BL_HEAD_BYTES, status);
		if ((ret != BL_HEAD_BYTES) && (tries > 0)) {
			/*
			 * maybe firmware is switching its states,
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
		dev_err(dev, "unknown trackpad firmware state.\n");
		return -EINVAL;
	}

	return 0;
}

static int cyapa_send_mode_switch_cmd(struct cyapa *cyapa,
				      struct cyapa_trackpad_run_mode *run_mode)
{
	struct device *dev = &cyapa->client->dev;
	int ret;
	unsigned long flags;

	if (cyapa->gen != CYAPA_GEN3)
		return -EINVAL;

	switch (run_mode->rev_cmd) {
	case CYAPA_CMD_APP_TO_IDLE:
		/* do reset operation to switch to bootloader idle mode. */
		cyapa_bl_disable_irq(cyapa);

		ret = cyapa_reg_write_byte(cyapa, CYAPA_OFFSET_SOFT_RESET,
					   0x01);
		if (ret < 0) {
			dev_err(dev, "firmware reset cmd failed, %d\n", ret);
			cyapa_bl_enable_irq(cyapa);
			return -EIO;
		}
		break;

	case CYAPA_CMD_IDLE_TO_ACTIVE:
		cyapa_bl_disable_irq(cyapa);
		/* send switch to active command. */
		ret = cyapa_reg_write_block(cyapa, 0,
				sizeof(bl_switch_active), bl_switch_active);
		if (ret != sizeof(bl_switch_active)) {
			dev_err(dev, "idle to active cmd failed, %d\n", ret);
			return -EIO;
		}
		break;

	case CYAPA_CMD_ACTIVE_TO_IDLE:
		cyapa_bl_disable_irq(cyapa);
		/* send switch to idle command.*/
		ret = cyapa_reg_write_block(cyapa, 0,
				sizeof(bl_switch_idle), bl_switch_idle);
		if (ret != sizeof(bl_switch_idle)) {
			dev_err(dev, "active to idle cmd failed, %d\n", ret);
			return -EIO;
		}
		break;

	case CYAPA_CMD_IDLE_TO_APP:
		/* send command switch operational mode.*/
		ret = cyapa_reg_write_block(cyapa, 0,
				sizeof(bl_app_launch), bl_app_launch);
		if (ret != sizeof(bl_app_launch)) {
			dev_err(dev, "idle to app cmd failed, %d\n", ret);
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
		 * additional waiting is done in cyapa_reconfig().
		 */
		msleep(300);

		/* update firmware working mode state in driver. */
		spin_lock_irqsave(&cyapa->miscdev_spinlock, flags);
		cyapa->in_bootloader = false;
		spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);

		/* reconfig and update firmware information. */
		cyapa_reconfig(cyapa, 0);

		cyapa_bl_enable_irq(cyapa);

		break;

	default:
		/* unknown command. */
		return -EINVAL;
	}

	/* update firmware working mode state in driver. */
	if (run_mode->rev_cmd != CYAPA_CMD_IDLE_TO_APP) {
		spin_lock_irqsave(&cyapa->miscdev_spinlock, flags);
		cyapa->in_bootloader = true;
		spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);
	}

	return 0;
}

static long cyapa_misc_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	int ret;
	int ioctl_len;
	struct cyapa *cyapa = (struct cyapa *)file->private_data;
	struct device *dev = &cyapa->client->dev;
	struct cyapa_misc_ioctl_data ioctl_data;
	struct cyapa_trackpad_run_mode run_mode;
	u8 buf[8];

	if (cyapa == NULL) {
		dev_err(dev, "device does not exist.\n");
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

		ioctl_data.len = 16;
		if (copy_to_user(ioctl_data.buf, cyapa->product_id, 16))
				return -EIO;
		if (copy_to_user((void *)arg, &ioctl_data, ioctl_len))
			return -EIO;
		return ioctl_data.len;

	case CYAPA_GET_FIRMWARE_VER:
		if (!ioctl_data.buf || ioctl_data.len < 2)
			return -EINVAL;

		ioctl_data.len = 2;
		memset(buf, 0, sizeof(buf));
		buf[0] = cyapa->fw_maj_ver;
		buf[1] = cyapa->fw_min_ver;
		if (copy_to_user(ioctl_data.buf, buf, ioctl_data.len))
			return -EIO;
		if (copy_to_user((void *)arg, &ioctl_data, ioctl_len))
			return -EIO;
		return ioctl_data.len;

	case CYAPA_GET_HARDWARE_VER:
		if (!ioctl_data.buf || ioctl_data.len < 2)
			return -EINVAL;

		ioctl_data.len = 2;
		memset(buf, 0, sizeof(buf));
		buf[0] = cyapa->hw_maj_ver;
		buf[1] = cyapa->hw_min_ver;
		if (copy_to_user(ioctl_data.buf, buf, ioctl_data.len))
			return -EIO;
		if (copy_to_user((void *)arg, &ioctl_data, ioctl_len))
			return -EIO;
		return ioctl_data.len;

	case CYAPA_GET_PROTOCOL_VER:
		if (!ioctl_data.buf || ioctl_data.len < 1)
			return -EINVAL;

		ioctl_data.len = 1;
		memset(buf, 0, sizeof(buf));
		buf[0] = cyapa->gen;
		if (copy_to_user(ioctl_data.buf, buf, ioctl_data.len))
			return -EIO;
		if (copy_to_user((void *)arg, &ioctl_data, ioctl_len))
			return -EIO;
		return ioctl_data.len;


	case CYAPA_GET_TRACKPAD_RUN_MODE:
		if (!ioctl_data.buf || ioctl_data.len < 2)
			return -EINVAL;

		/* get trackpad status. */
		ret = cyapa_get_trackpad_run_mode(cyapa, &run_mode);
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

		return cyapa_send_mode_switch_cmd(cyapa, &run_mode);

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

static void cyapa_update_firmware_dispatch(struct cyapa *cyapa)
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
	struct i2c_client *client = to_i2c_client(dev);
	struct cyapa *cyapa = i2c_get_clientdata(client);
	return sprintf(buf, "%d.%d\n", cyapa->fw_maj_ver, cyapa->fw_min_ver);
}

ssize_t cyapa_show_hw_ver(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cyapa *cyapa = i2c_get_clientdata(client);
	return sprintf(buf, "%d.%d\n", cyapa->hw_maj_ver, cyapa->hw_min_ver);
}

ssize_t cyapa_show_product_id(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cyapa *cyapa = i2c_get_clientdata(client);
	return sprintf(buf, "%s\n", cyapa->product_id);
}

ssize_t cyapa_show_protocol_version(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cyapa *cyapa = i2c_get_clientdata(client);
	return sprintf(buf, "%d\n", cyapa->gen);
}

static DEVICE_ATTR(firmware_version, S_IRUGO, cyapa_show_fm_ver, NULL);
static DEVICE_ATTR(hardware_version, S_IRUGO, cyapa_show_hw_ver, NULL);
static DEVICE_ATTR(product_id, S_IRUGO, cyapa_show_product_id, NULL);
static DEVICE_ATTR(protocol_version, S_IRUGO, cyapa_show_protocol_version, NULL);

static struct attribute *cyapa_sysfs_entries[] = {
	&dev_attr_firmware_version.attr,
	&dev_attr_hardware_version.attr,
	&dev_attr_product_id.attr,
	&dev_attr_protocol_version.attr,
	NULL,
};

static const struct attribute_group cyapa_sysfs_group = {
	.attrs = cyapa_sysfs_entries,
};

/*
 **************************************************************
 * Cypress i2c trackpad input device driver.
 **************************************************************
*/
static int cyapa_get_query_data(struct cyapa *cyapa)
{
	unsigned long flags;
	u8 query_data[QUERY_DATA_SIZE];
	int ret;

	spin_lock_irqsave(&cyapa->miscdev_spinlock, flags);
	if (cyapa->in_bootloader) {
		/* firmware is in bootloader mode. */
		spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);

	ret = cyapa_reg_read_block(cyapa, REG_OFFSET_QUERY_BASE,
				   QUERY_DATA_SIZE, query_data);
	if (ret < 0)
		return ret;

	cyapa->product_id[0] = query_data[0];
	cyapa->product_id[1] = query_data[1];
	cyapa->product_id[2] = query_data[2];
	cyapa->product_id[3] = query_data[3];
	cyapa->product_id[4] = query_data[4];
	cyapa->product_id[5] = '-';
	cyapa->product_id[6] = query_data[5];
	cyapa->product_id[7] = query_data[6];
	cyapa->product_id[8] = query_data[7];
	cyapa->product_id[9] = query_data[8];
	cyapa->product_id[10] = query_data[9];
	cyapa->product_id[11] = query_data[10];
	cyapa->product_id[12] = '-';
	cyapa->product_id[13] = query_data[11];
	cyapa->product_id[14] = query_data[12];
	cyapa->product_id[15] = '\0';

	cyapa->fw_maj_ver = query_data[15];
	cyapa->fw_min_ver = query_data[16];
	cyapa->hw_maj_ver = query_data[17];
	cyapa->hw_min_ver = query_data[18];

	cyapa->gen = query_data[20] & 0x0F;

	cyapa->max_abs_x = ((query_data[21] & 0xF0) << 4) | query_data[22];
	cyapa->max_abs_y = ((query_data[21] & 0x0F) << 8) | query_data[23];

	cyapa->physical_size_x =
		((query_data[24] & 0xF0) << 4) | query_data[25];
	cyapa->physical_size_y =
		((query_data[24] & 0x0F) << 8) | query_data[26];

	return 0;
}

/*
 * determine if device firmware supports protocol generation 3
 *
 * Returns:
 *   -EIO    firmware protocol could be read => no device or in bootloader
 *   -EINVAL protocol is not GEN3, or product_id doesn't start with "CYTRA"
 *   0       protocol is GEN3
 */
static int cyapa_determine_firmware_gen3(struct cyapa *cyapa)
{
	int loop = 8;
	const char unique_str[] = "CYTRA";

	while (loop--) {
		if (!cyapa_get_query_data(cyapa))
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

	if (cyapa->gen != CYAPA_GEN3 || memcmp(cyapa->product_id, unique_str,
					       sizeof(unique_str)-1))
		return -EINVAL;

	return 0;
}

static int cyapa_reconfig(struct cyapa *cyapa, int boot)
{
	struct device *dev = &cyapa->client->dev;
	unsigned long flags;

	spin_lock_irqsave(&cyapa->miscdev_spinlock, flags);
	if (cyapa->in_bootloader) {
		/* firmware is in bootloader mode. */
		spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);

	/* only support trackpad firmware gen3 or later protocol. */
	if (cyapa_determine_firmware_gen3(cyapa)) {
		dev_err(dev, "unsupported firmware protocol version (%d) or "
			"product ID (%s).\n", cyapa->gen, cyapa->product_id);
		return -EINVAL;
	}

	if (boot) {
		/* output in one time, avoid multi-lines output be separated. */
		dev_info(dev, "Cypress Trackpad Information:\n" \
			"    Product ID:  %s\n" \
			"    Protocol Generation:  %d\n" \
			"    Firmware Version:  %d.%d\n" \
			"    Hardware Version:  %d.%d\n" \
			"    Max ABS X,Y:   %d,%d\n" \
			"    Physical Size X,Y:   %d,%d\n",
			cyapa->product_id,
			cyapa->gen,
			cyapa->fw_maj_ver, cyapa->fw_min_ver,
			cyapa->hw_maj_ver, cyapa->hw_min_ver,
			cyapa->max_abs_x, cyapa->max_abs_y,
			cyapa->physical_size_x, cyapa->physical_size_y
			);
	}

	return 0;
}

static int cyapa_verify_data_device(struct cyapa *cyapa,
				    struct cyapa_reg_data *reg_data)
{
	if ((reg_data->device_status & OP_STATUS_SRC) != OP_STATUS_SRC)
		return -EINVAL;

	if ((reg_data->finger_btn & OP_DATA_VALID) != OP_DATA_VALID)
		return -EINVAL;

	if ((reg_data->device_status & OP_STATUS_DEV) != CYAPA_DEV_NORMAL)
		return -EBUSY;

	return 0;
}

static void cyapa_parse_data(struct cyapa *cyapa,
			     struct cyapa_reg_data *reg_data,
			     struct cyapa_report_data *report_data)
{
	int i;
	int fingers;

	/* only report left button. */
	report_data->button = reg_data->finger_btn & OP_DATA_BTN_MASK;
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
}


static int cyapa_find_mt_slot(struct cyapa *cyapa, struct cyapa_touch *contact)
{
	int i;
	int empty_slot = -1;

	for (i = 0; i < CYAPA_MAX_MT_SLOTS; i++) {
		if ((cyapa->mt_slots[i].contact.tracking_id == contact->tracking_id) &&
			cyapa->mt_slots[i].touch_state)
			return i;

		if (!cyapa->mt_slots[i].touch_state && empty_slot == -1)
			empty_slot = i;
	}

	return empty_slot;
}

static void cyapa_update_mt_slots(struct cyapa *cyapa,
		struct cyapa_report_data *report_data)
{
	int i;
	int slotnum;

	for (i = 0; i < report_data->touch_fingers; i++) {
		slotnum = cyapa_find_mt_slot(cyapa, &report_data->touches[i]);
		if (slotnum < 0)
			continue;

		memcpy(&cyapa->mt_slots[slotnum].contact,
				&report_data->touches[i],
				sizeof(struct cyapa_touch));
		cyapa->mt_slots[slotnum].slot_updated = true;
		cyapa->mt_slots[slotnum].touch_state = true;
	}
}

static void cyapa_send_mtb_event(struct cyapa *cyapa,
		struct cyapa_report_data *report_data)
{
	int i;
	struct cyapa_mt_slot *slot;
	struct input_dev *input = cyapa->input;

	cyapa_update_mt_slots(cyapa, report_data);

	for (i = 0; i < CYAPA_MAX_MT_SLOTS; i++) {
		slot = &cyapa->mt_slots[i];
		if (!slot->slot_updated)
			slot->touch_state = false;

		input_mt_slot(input, i);
		input_mt_report_slot_state(input, MT_TOOL_FINGER,
					   slot->touch_state);
		if (slot->touch_state) {
			input_report_abs(input, ABS_MT_POSITION_X,
					 slot->contact.x);
			input_report_abs(input, ABS_MT_POSITION_Y,
					 slot->contact.y);
			input_report_abs(input, ABS_MT_PRESSURE,
					 slot->contact.pressure);
		}
		slot->slot_updated = false;
	}

	input_mt_report_pointer_emulation(input, true);
	input_report_key(input, BTN_LEFT, report_data->button);
	input_sync(input);
}

static bool cyapa_get_input(struct cyapa *cyapa)
{
	int ret_read_size;
	struct cyapa_reg_data reg_data;
	struct cyapa_report_data report_data;

	/* read register data from trackpad. */
	ret_read_size = cyapa_reg_read_block(cyapa,
					DATA_REG_START_OFFSET,
					sizeof(struct cyapa_reg_data),
					(u8 *)&reg_data);
	if (ret_read_size < 0)
		return false;

	if (cyapa_verify_data_device(cyapa, &reg_data) < 0)
		return false;

	/* process and parse raw data read from Trackpad. */
	cyapa_parse_data(cyapa, &reg_data, &report_data);

	cyapa_dump_report(cyapa, &report_data);

	/* report data to input subsystem. */
	cyapa_send_mtb_event(cyapa, &report_data);
	return report_data.touch_fingers | report_data.button;
}

/* Work Handler */
static void cyapa_work_handler(struct work_struct *work)
{
	struct cyapa *cyapa = container_of(work, struct cyapa, dwork.work);
	unsigned long flags;

	/*
	 * use spinlock to avoid conflict accessing
	 * when firmware switching into bootloader mode.
	 */
	spin_lock_irqsave(&cyapa->miscdev_spinlock, flags);
	if (cyapa->detect_status != CYAPA_DETECT_DONE_SUCCESS) {
		/* still detecting trackpad device in work queue. */
		spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);
		return;
	}
	if (cyapa->in_bootloader) {
		spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);
		cyapa_update_firmware_dispatch(cyapa);
	} else {
		spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);

		cyapa_get_input(cyapa);
	}
}

static void cyapa_reschedule_work(struct cyapa *cyapa, unsigned long delay)
{
	unsigned long flags;

	spin_lock_irqsave(&cyapa->lock, flags);

	/*
	 * If work is already scheduled then subsequent schedules will not
	 * change the scheduled time that's why we have to cancel it first.
	 */
	__cancel_delayed_work(&cyapa->dwork);
	/*
	 * check bl_irq_enable value to avoid mistriggered interrupt
	 * when switching from operational mode
	 * to bootloader mode.
	 */
	if (cyapa->bl_irq_enable)
		schedule_delayed_work(&cyapa->dwork, delay);

	spin_unlock_irqrestore(&cyapa->lock, flags);
}

static irqreturn_t cyapa_irq(int irq, void *dev_id)
{
	struct cyapa *cyapa = dev_id;

	cyapa_reschedule_work(cyapa, 0);

	return IRQ_HANDLED;
}

static int cyapa_open(struct input_dev *input)
{
	return 0;
}

static void cyapa_close(struct input_dev *input)
{
	struct cyapa *cyapa = input_get_drvdata(input);

	cancel_delayed_work_sync(&cyapa->dwork);
}

static int cyapa_create_input_dev(struct cyapa *cyapa)
{
	struct device *dev = &cyapa->client->dev;
	int ret;
	struct input_dev *input = NULL;

	input = cyapa->input = input_allocate_device();
	if (!cyapa->input) {
		dev_err(dev, "Allocate memory for input device failed\n");
		return -ENOMEM;
	}

	input->name = "cyapa_trackpad";
	input->phys = cyapa->client->adapter->name;
	input->id.bustype = BUS_I2C;
	input->id.version = 1;
	input->id.product = 0;  /* means any product in eventcomm. */
	input->dev.parent = &cyapa->client->dev;

	input->open = cyapa_open;
	input->close = cyapa_close;
	input_set_drvdata(input, cyapa);

	__set_bit(EV_ABS, input->evbit);

	/*
	 * set and report not-MT axes to support synaptics X Driver.
	 * When multi-fingers on trackpad, only the first finger touch
	 * will be reported as X/Y axes values.
	 */
	input_set_abs_params(input, ABS_X, 0, cyapa->max_abs_x, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, cyapa->max_abs_y, 0, 0);
	input_set_abs_params(input, ABS_PRESSURE, 0, 255, 0, 0);

	/* finger position */
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, cyapa->max_abs_x, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, cyapa->max_abs_y, 0, 0);
	input_set_abs_params(input, ABS_MT_PRESSURE, 0, 255, 0, 0);
	ret = input_mt_init_slots(input, CYAPA_MAX_MT_SLOTS);
	if (ret < 0)
		return ret;

	if (cyapa->physical_size_x && cyapa->physical_size_y) {
		input_abs_set_res(input, ABS_X,
			cyapa->max_abs_x / cyapa->physical_size_x);
		input_abs_set_res(input, ABS_Y,
			cyapa->max_abs_y / cyapa->physical_size_y);
		input_abs_set_res(input, ABS_MT_POSITION_X,
			cyapa->max_abs_x / cyapa->physical_size_x);
		input_abs_set_res(input, ABS_MT_POSITION_Y,
			cyapa->max_abs_y / cyapa->physical_size_y);
	}

	__set_bit(EV_KEY, input->evbit);
	__set_bit(BTN_TOUCH, input->keybit);
	__set_bit(BTN_TOOL_FINGER, input->keybit);
	__set_bit(BTN_TOOL_DOUBLETAP, input->keybit);
	__set_bit(BTN_TOOL_TRIPLETAP, input->keybit);
	__set_bit(BTN_TOOL_QUADTAP, input->keybit);

	__set_bit(BTN_LEFT, input->keybit);

	/* Register the device in input subsystem */
	ret = input_register_device(cyapa->input);
	if (ret) {
		dev_err(dev, "input device register failed, %d\n", ret);
		input_free_device(input);
	}

	return ret;
}

static int cyapa_check_exit_bootloader(struct cyapa *cyapa)
{
	int ret;
	int tries = 15;
	unsigned long flags;
	struct cyapa_trackpad_run_mode run_mode;

	do {
		if ((cyapa_get_trackpad_run_mode(cyapa, &run_mode) < 0) &&
			(tries > 0)) {
			msleep(300);
			continue;
		}

		if (run_mode.run_mode == CYAPA_OPERATIONAL_MODE) {
			spin_lock_irqsave(&cyapa->miscdev_spinlock, flags);
			cyapa->in_bootloader = false;
			spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);
			break;
		}

		if ((run_mode.run_mode == CYAPA_BOOTLOADER_MODE) &&
			(run_mode.bootloader_state ==
				CYAPA_BOOTLOADER_ACTIVE_STATE)) {
			/* bootloader active state. */
			ret = cyapa_reg_write_block(cyapa, 0,
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
			ret = cyapa_reg_write_block(cyapa, 0,
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

static int cyapa_set_power_mode(struct cyapa *cyapa, u8 power_mode)
{
	int ret;
	u8 power;
	int tries = 3;

	power = cyapa_reg_read_byte(cyapa, REG_OFFSET_POWER_MODE);
	power &= ~OP_POWER_MODE_MASK;
	power |= ((power_mode << OP_POWER_MODE_SHIFT) & OP_POWER_MODE_MASK);
	do {
		ret = cyapa_reg_write_byte(cyapa,
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
	struct cyapa *cyapa = container_of(work, struct cyapa, detect_work);
	struct i2c_client *client = cyapa->client;
	struct device *dev = &cyapa->client->dev;

	ret = cyapa_check_exit_bootloader(cyapa);
	if (ret < 0) {
		dev_err(dev, "check and exit bootloader failed.\n");
		goto out_probe_err;
	}

	cyapa->irq = client->irq;
	irq_set_irq_type(cyapa->irq, IRQF_TRIGGER_FALLING);
	ret = request_irq(cyapa->irq,
			cyapa_irq,
			0,
			CYAPA_I2C_NAME,
			cyapa);
	if (ret) {
		dev_err(dev, "IRQ request failed: %d\n, ", ret);
		goto out_probe_err;
	}

	spin_lock_irqsave(&cyapa->miscdev_spinlock, flags);
	cyapa->bl_irq_enable = false;
	cyapa->irq_enabled = true;
	enable_irq_wake(cyapa->irq);
	spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);

	/*
	 * reconfig trackpad depending on platform setting.
	 *
	 * always pass through after reconfig returned to given a chance
	 * that user can update trackpad firmware through cyapa interface
	 * when current firmware protocol is not supported.
	 */
	cyapa_reconfig(cyapa, true);

	/* create an input_dev instance for trackpad device. */
	ret = cyapa_create_input_dev(cyapa);
	if (ret) {
		free_irq(cyapa->irq, cyapa);
		dev_err(dev, "create input_dev instance failed.\n");
		goto out_probe_err;
	}

	i2c_set_clientdata(client, cyapa);

	ret = sysfs_create_group(&client->dev.kobj, &cyapa_sysfs_group);
	if (ret)
		dev_warn(dev, "error creating sysfs entries.\n");

	spin_lock_irqsave(&cyapa->miscdev_spinlock, flags);
	cyapa->detect_status = CYAPA_DETECT_DONE_SUCCESS;
	if (cyapa->irq_enabled)
		cyapa->bl_irq_enable = true;
	spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);

	return;

out_probe_err:
	spin_lock_irqsave(&cyapa->miscdev_spinlock, flags);
	cyapa->detect_status = CYAPA_DETECT_DONE_FAILED;
	spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);

	/* release previous allocated input_dev instances. */
	if (cyapa->input) {
		if (cyapa->input->mt)
			input_mt_destroy_slots(cyapa->input);
		input_free_device(cyapa->input);
		cyapa->input = NULL;
	}

	kfree(cyapa);
	global_cyapa = NULL;
}

static void cyapa_resume_detect_work_handler(struct work_struct *work)
{
	int ret;
	unsigned long flags;
	struct cyapa *cyapa = container_of(work, struct cyapa, detect_work);
	struct device *dev = &cyapa->client->dev;

	/*
	 * when waking up, the first step that driver should do is to
	 * set trackpad device to full active mode. Do other read/write
	 * operations may get invalid data or get failed.
	 * And if set power mode failed, maybe the reason is that trackpad
	 * is working in bootloader mode, so do not check the return
	 * result here.
	 */
	ret = cyapa_set_power_mode(cyapa, PWR_MODE_FULL_ACTIVE);
	if (ret < 0)
		dev_warn(dev, "set wake up power mode to trackpad failed\n");

	ret = cyapa_check_exit_bootloader(cyapa);
	if (ret < 0) {
		dev_err(dev, "check and exit bootloader failed.\n");
		goto out_resume_err;
	}

	/* re-enable interrupt work handler routine. */
	spin_lock_irqsave(&cyapa->miscdev_spinlock, flags);
	if (cyapa->irq_enabled)
		cyapa->bl_irq_enable = true;
	spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);

out_resume_err:
	/* trackpad device resumed from sleep state successfully. */
	spin_lock_irqsave(&cyapa->miscdev_spinlock, flags);
	cyapa->detect_status = ret ? CYAPA_DETECT_DONE_FAILED :
					CYAPA_DETECT_DONE_SUCCESS;
	spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);
}

static int cyapa_resume_detect(struct cyapa *cyapa)
{
	unsigned long flags;

	spin_lock_irqsave(&cyapa->miscdev_spinlock, flags);
	cyapa->bl_irq_enable = false;
	cyapa->in_bootloader = true;
	spin_unlock_irqrestore(&cyapa->miscdev_spinlock, flags);

	/*
	 * Maybe trackpad device is not connected,
	 * or firmware is doing sensor calibration,
	 * it will take max 2 seconds to be completed.
	 * So use work queue to wait for it ready
	 * to avoid block system booting or resuming.
	 */
	INIT_WORK(&cyapa->detect_work, cyapa_resume_detect_work_handler);
	return queue_work(cyapa->detect_wq, &cyapa->detect_work);
}

static int __devinit cyapa_probe(struct i2c_client *client,
				 const struct i2c_device_id *dev_id)
{
	int ret;
	struct cyapa *cyapa;
	struct device *dev = &client->dev;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EIO;

	cyapa = kzalloc(sizeof(struct cyapa), GFP_KERNEL);
	if (!cyapa) {
		dev_err(dev, "allocate memory for cyapa failed\n");
		return -ENOMEM;
	}

	cyapa->gen = CYAPA_GEN3;
	cyapa->client = client;
	global_cyapa = cyapa;
	cyapa->in_bootloader = true;
	cyapa->misc_open_count = 0;
	spin_lock_init(&cyapa->miscdev_spinlock);
	mutex_init(&cyapa->misc_mutex);

	INIT_DELAYED_WORK(&cyapa->dwork, cyapa_work_handler);
	spin_lock_init(&cyapa->lock);

	/*
	 * At boot it can take up to 2 seconds for firmware to complete sensor
	 * calibration. Probe in a workqueue so as not to block system boot.
	 */
	cyapa->detect_wq = create_singlethread_workqueue("cyapa_detect_wq");
	if (!cyapa->detect_wq) {
		ret = -ENOMEM;
		dev_err(dev, "create detect workqueue failed\n");
		goto err_mem_free;
	}

	INIT_WORK(&cyapa->detect_work, cyapa_probe_detect_work_handler);
	ret = queue_work(cyapa->detect_wq, &cyapa->detect_work);
	if (ret < 0) {
		dev_err(dev, "device detect failed, %d\n", ret);
		goto err_wq_free;
	}

	return 0;

err_wq_free:
	destroy_workqueue(cyapa->detect_wq);
err_mem_free:
	kfree(cyapa);
	global_cyapa = NULL;

	return ret;
}

static int __devexit cyapa_remove(struct i2c_client *client)
{
	struct cyapa *cyapa = i2c_get_clientdata(client);

	sysfs_remove_group(&client->dev.kobj, &cyapa_sysfs_group);

	cancel_delayed_work_sync(&cyapa->dwork);

	disable_irq_wake(cyapa->irq);
	free_irq(cyapa->irq, cyapa);

	if (cyapa->input)
		input_unregister_device(cyapa->input);

	if (cyapa->detect_wq)
		destroy_workqueue(cyapa->detect_wq);
	kfree(cyapa);
	global_cyapa = NULL;

	return 0;
}

#ifdef CONFIG_PM
static int cyapa_suspend(struct device *dev)
{
	int ret;
	struct i2c_client *client = to_i2c_client(dev);
	struct cyapa *cyapa = i2c_get_clientdata(client);

	/*
	 * When cyapa driver probing failed and haven't been removed,
	 * then when system do suspending, the value of cyapa is NULL.
	 * e.g.: this situation will happen when system booted
	 * without trackpad connected.
	 */
	if (!cyapa)
		return 0;

	if (cyapa->detect_wq)
		flush_workqueue(cyapa->detect_wq);

	cancel_delayed_work_sync(&cyapa->dwork);

	/* set trackpad device to light sleep mode. */
	ret = cyapa_set_power_mode(cyapa, PWR_MODE_LIGHT_SLEEP);
	if (ret < 0)
		dev_err(dev, "suspend trackpad device failed, %d\n", ret);

	return ret;
}

static int cyapa_resume(struct device *dev)
{
	int ret;
	struct i2c_client *client = to_i2c_client(dev);
	struct cyapa *cyapa = i2c_get_clientdata(client);

	/*
	 * When cyapa driver probing failed and haven't been removed,
	 * then when system do suspending, the value of cyapa is NULL.
	 * e.g.: this situation will happen when system booted
	 * without trackpad connected.
	 */
	if (!cyapa)
		return 0;

	ret = cyapa_resume_detect(cyapa);
	if (ret < 0) {
		dev_err(dev, "trackpad detect failed, %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops cyapa_pm_ops = {
	.suspend = cyapa_suspend,
	.resume = cyapa_resume,
};
#endif

static const struct i2c_device_id cyapa_id_table[] = {
	{ CYAPA_I2C_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, cyapa_id_table);

static struct i2c_driver cyapa_driver = {
	.driver = {
		.name = CYAPA_I2C_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &cyapa_pm_ops,
#endif
	},

	.probe = cyapa_probe,
	.remove = __devexit_p(cyapa_remove),
	.id_table = cyapa_id_table,
};

static int __init cyapa_init(void)
{
	int ret;

	ret = i2c_add_driver(&cyapa_driver);
	if (ret) {
		pr_err("cyapa driver register FAILED.\n");
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

static void __exit cyapa_exit(void)
{
	cyapa_misc_exit();

	i2c_del_driver(&cyapa_driver);
}

module_init(cyapa_init);
module_exit(cyapa_exit);

MODULE_DESCRIPTION("Cypress APA I2C Trackpad Driver");
MODULE_AUTHOR("Dudley Du <dudl@cypress.com>");
MODULE_LICENSE("GPL");
