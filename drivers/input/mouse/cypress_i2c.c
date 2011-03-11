/*
 * Cypress APA touchpad with I2C interface
 *
 * Copyright (C) 2009 Compulab, Ltd.
 * Dudley Du <dudl@cypress.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */


#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/gpio.h>

#include <linux/cyapa.h>


/* Debug macro */
//#define CYAPA_DBG
#ifdef CYAPA_DBG
  #define DBGPRINTK(x) printk x
  #define DBG_CYAPA_READ_BLOCK_DATA
#else
  #define DBGPRINTK(x)
#endif


/* Cypress I2C APA trackpad driver version is defined as bellow:
** CYAPA_MAJOR_VER.CYAPA_MINOR_VER.CYAPA_REVISIOIN_VER  . */
#define CYAPA_MAJOR_VER		0
#define CYAPA_MINOR_VER		9
#define CYAPA_REVISIOIN_VER	2

/* macro definication for gestures. */
/* --------------------------------------------------------------- */
/* |-          bit 7 - 5         -|-           bit 4 -0         -| */
/* |------------------------------|----------------------------- | */
/* |-        finger number       -|-        gesture id          -| */
/* --------------------------------------------------------------- */
#define GESTURE_FINGERS(x) ((((x) & 0x07) << 5) & 0xE0)
#define GESTURE_INDEX(x) ((x) & 0x1F) 
#define GESTURE_ID_CODE(finger, index) (GESTURE_FINGERS(finger) | GESTURE_INDEX(index))

#define GESTURE_NONE			0x00
/* 0-finger gestures. */
#define GESTURE_PALM_REJECTIOIN	    GESTURE_ID_CODE(0, 1)
/* 1-finger gestures. */
#define GESTURE_SINGLE_TAP		    GESTURE_ID_CODE(1, 0)
#define GESTURE_DOUBLE_TAP		    GESTURE_ID_CODE(1, 1)
/* one finger click and hold for more than definitioin time, then to do something. */
#define GESTURE_TAP_AND_HOLD	    GESTURE_ID_CODE(1, 2)
#define GESTURE_EDGE_MOTION		    GESTURE_ID_CODE(1, 3)
#define GESTURE_FLICK			    GESTURE_ID_CODE(1, 4)
/* GESTURE_DRAG : double click and hold, then move for drag.*/
#define GESTURE_DRAG			    GESTURE_ID_CODE(1, 5)
/* Depending on PSOC user module, it will give four different ID when scroll.*/
#define GESTURE_SCROLL_UP		    GESTURE_ID_CODE(1, 6)
#define GESTURE_SCROLL_DOWN		    GESTURE_ID_CODE(1, 7)
#define GESTURE_SCROLL_LEFT		    GESTURE_ID_CODE(1, 8)
#define GESTURE_SCROLL_RIGHT		GESTURE_ID_CODE(1, 9)

/* 2-finger gestures */
#define GESTURE_2F_ZOOM_IN		    GESTURE_ID_CODE(2, 0)
#define GESTURE_2F_ZOOM_OUT		    GESTURE_ID_CODE(2, 1)
#define GESTURE_2F_SCROLL_UP		GESTURE_ID_CODE(2, 2)
#define GESTURE_2F_SCROLL_DOWN	    GESTURE_ID_CODE(2, 3)
#define GESTURE_2F_SCROLL_LEFT	    GESTURE_ID_CODE(2, 4)
#define GESTURE_2F_SCROLL_RIGHT	    GESTURE_ID_CODE(2, 5)
#define GESTURE_2F_ROTATE		    GESTURE_ID_CODE(2, 6)
#define GESTURE_2F_PINCH		    GESTURE_ID_CODE(2, 7)
/* Activates the Right Click action */
#define GESTURE_2F_TAP			    GESTURE_ID_CODE(2, 8)
/* Single-Finger click and hold while a second finger is moving for dragging. */
#define GESTURE_2F_DRAG			    GESTURE_ID_CODE(2, 9)
#define GESTURE_2F_FLICK			    GESTURE_ID_CODE(2, 10)

/* 3-finger gestures */
#define GESTURE_3F_FLICK		    GESTURE_ID_CODE(3, 0)

/* 4-finger gestures */
#define GESTURE_4F_FLICK		    GESTURE_ID_CODE(4, 0)

/* 5-finger gestures */
#define GESTURE_5F_FLICK		    GESTURE_ID_CODE(5, 0)

/* swith of the gesture, */
#define GESTURE_MULTI_TOUCH_ONE_CLICK   0

#define GESTURE_DECODE_FINGERS(x)	(((x) >> 5) & 0x07)
#define GESTURE_DECODE_INDEX(x)	((x) & 0x1F )

/* max gesture index value for each fingers type is 31. 0~21.*/
#define MAX_FINGERS	5


/* parameter value for input_report_key(BTN_TOOL_WIDTH) */
#define CYAPA_TOOL_WIDTH 50

/* When in IRQ mode read the device every THREAD_IRQ_SLEEP_SECS */
#define CYAPA_THREAD_IRQ_SLEEP_SECS	2
#define CYAPA_THREAD_IRQ_SLEEP_MSECS  (CYAPA_THREAD_IRQ_SLEEP_SECS * MSEC_PER_SEC)

/*
 * When in Polling mode and no data received for CYAPA_NO_DATA_THRES msecs
 * reduce the polling rate to CYAPA_NO_DATA_SLEEP_MSECS
 */
#define CYAPA_NO_DATA_THRES			(MSEC_PER_SEC)
#define CYAPA_NO_DATA_SLEEP_MSECS	(MSEC_PER_SEC / 4)

/* report data start reg offset address. */
#define DATA_REG_START_OFFSET  0x0000
/* relative data report data size. */
#define CYAPA_REL_REG_DATA_SIZE  5


/* Device Sleep Modes */
#define DEV_POWER_REG  0x0009
#define INTERRUPT_MODE_MASK  0x01
#define PWR_LEVEL_MASK  0x06
#define PWR_BITS_SHITF 1
#define GET_PWR_LEVEL(reg) ((((unsigned char)(reg))&PWR_LEVEL_MASK)>>PWR_BITS_SHITF)

// protocol V1.
#define REG_GESTURES 0x0B

/* definition to store platfrom data. */
static struct cyapa_platform_data cyapa_i2c_platform_data = {
	.flag = 0,
	.gen = CYAPA_GEN2,
	.power_state = CYAPA_PWR_ACTIVE,
	.use_absolute_mode = true,
	.use_polling_mode = false,
	.polling_interval_time_active = CYAPA_ACTIVE_POLLING_INTVAL_TIME,
	.polling_interval_time_lowpower = CYAPA_LOWPOWER_POLLING_INTVAL_TIME,
	.active_touch_timeout = CYAPA_ACTIVE_TOUCH_TIMEOUT,
	.name = CYAPA_I2C_NAME,
	.irq_gpio = -1,
	.report_rate = CYAPA_REPORT_RATE,
};


/*
** APA trackpad device states.
** Used in register 0x00, bit1-0, DeviceStatus field.
*/
enum cyapa_devicestate
{
	CYAPA_DEV_NORNAL = 0x03,
    /*
    ** After trackpad booted, and can report data, it should set this value.
    ** 0ther values stand for trackpad device is in abnormal state.
    ** maybe need to do reset operation to it.
    ** Other values are defined later if needed.
    */
};

#define CYAPA_MAX_TOUCHS (MAX_FINGERS)
#define CYAPA_ONE_TIME_GESTURES  (1)  //only 1 gesture can be reported one time right now.
struct cyapa_touch_gen1
{
	u8 rel_xy;
	u8 rel_x;
	u8 rel_y;
};

struct cyapa_reg_data_gen1
{
	u8 tap_motion;
	s8 deltax;
	s8 deltay;
	u8 reserved1;
	u8 reserved2;
	
	struct cyapa_touch_gen1 touch1;
	u8 touch_fingers;
	u8 feature_config;
	u8 avg_pressure;  /* average of all touched fingers. */
	u8 gesture_status;
	struct cyapa_touch_gen1 touchs[CYAPA_MAX_TOUCHS-1];
};

struct cyapa_touch_gen2
{
	u8 xy;
	u8 x;
	u8 y;
	u8 id;
};

struct cyapa_gesture
{
	u8 id;
	u8 param1;
	u8 param2;
};

struct cyapa_reg_data_gen2
{
	u8 device_status;
	u8 relative_flags;
	s8 deltax;
	s8 deltay;
	u8 avg_pressure;
	u8 touch_fingers;
	u8 reserved1;
	u8 reserved2;
	struct cyapa_touch_gen2 touchs[CYAPA_MAX_TOUCHS];
	u8 gesture_count;
	struct cyapa_gesture gesture[CYAPA_ONE_TIME_GESTURES];
};

union cyapa_reg_data
{
	struct cyapa_reg_data_gen1 gen1_data;
	struct cyapa_reg_data_gen2 gen2_data;
};

struct cyapa_touch
{
	int x;
	int y;
	int id;
};

struct cyapa_report_data
{
	u8 button;
	u8 reserved1;
	u8 reserved2;
	u8 avg_pressure;
	int rel_deltaX;
	int rel_deltaY;

	int touch_fingers;
	struct cyapa_touch touchs[CYAPA_MAX_TOUCHS];
	
	int gestures_count;  /* in gen1 and gen2, only 1 gesture one time supported. */
	struct cyapa_gesture gestures[CYAPA_ONE_TIME_GESTURES];
};

struct scroll_preferences {
	int default_threshold;   /* small scroll speed threshold. */
	int middle_threshold;
	int fast_threshold;
};

struct cyapa_preferences {
	struct scroll_preferences vscroll;
	struct scroll_preferences hscroll;
};

/* The main device structure */
struct cyapa_i2c {
	struct i2c_client	*client;
	struct input_dev	*input;
	struct input_dev	*input_wheel;
	struct input_dev	*input_kbd;
	struct delayed_work dwork;
	spinlock_t lock;
	int no_data_count;
	int scan_ms;
	int read_pending;
	int open_count;

	int irq;
	struct cyapa_platform_data *platform_data;
	unsigned short data_base_offset;
	unsigned short control_base_offset;
	unsigned short command_base_offset;
	unsigned short query_base_offset;

	struct cyapa_preferences preferences;
	
	int zoomin_delta;
	int zoomout_delta;
	int hscroll_left;
	int hscroll_right;
	int delta_scroll_up;
	int delta_scroll_down;
	int delta_scroll_left;
	int delta_scroll_right;

	int abs_x;
	int abs_y;
	int prev_abs_x;
	int prev_abs_y;
	unsigned char xy_touchs_included_bits;
	unsigned char gesture_2F_drag_started;

	unsigned long cur_active_gestures[MAX_FINGERS];
	unsigned long prev_active_gestures[MAX_FINGERS];

	int prev_touch_fingers;

	/* read from query data region. */
	char product_id[16];
	unsigned char capability[14];
	unsigned char fm_maj_ver;  //firmware major version.
	unsigned char fm_min_ver;  //firmware minor version.
	unsigned char hw_maj_ver;  //hardware major version.
	unsigned char hw_min_ver;  //hardware minor version.
	int max_absolution_x;
	int max_absolution_y;
	int physical_size_x;
	int physical_size_y;
};


#ifdef DBG_CYAPA_READ_BLOCK_DATA
void cyapa_print_data_block(const char *func, u8 reg, u8 length, void *data)
{
	char buf[1024];
	unsigned buf_len = sizeof(buf);
	char *p = buf;
	int i;
	int l;

	l = snprintf(p, buf_len, "reg 0x%04x: ", reg);
	buf_len -= l;
	p += l;
	for (i = 0; i < length && buf_len; i++, p += l, buf_len -= l)
		l = snprintf(p, buf_len, "%02x ", *((char *)data + i));
	printk("%s: data block length = %d\n", func, length);
	printk("%s: %s\n", func, buf);
}

void cyapa_print_report_data(const char *func,
				struct cyapa_report_data *report_data)
{
	int i;

	printk("%s: -----------------------------------------\n", func);
	printk("%s: report_data.button = 0x%02x\n", func, report_data->button);
	printk("%s: report_data.avg_pressure = %d\n", func, report_data->avg_pressure);
	printk("%s: report_data.touch_fingers = %d\n", func, report_data->touch_fingers);
	for (i=0; i<report_data->touch_fingers; i++) {
		printk("%s: report_data.touchs[%d].x = %d\n", func, i, report_data->touchs[i].x);
		printk("%s: report_data.touchs[%d].y = %d\n", func, i, report_data->touchs[i].y);
		printk("%s: report_data.touchs[%d].id = %d\n",
							func, i, report_data->touchs[i].id);
	}
	printk("%s: report_data.gestures_count = %d\n", func, report_data->gestures_count);
	for (i=0; i<report_data->gestures_count; i++) {
		printk("%s: report_data.gestures[%d].id = 0x%02x\n",
							func, i, report_data->gestures[i].id);
		printk("%s: report_data.gestures[%d].param1 = 0x%02x\n",
							func, i, report_data->gestures[i].param1);
		printk("%s: report_data.gestures[%d].param2 = 0x%02x\n",
							func, i, report_data->gestures[i].param2);
	}
	printk("%s: -----------------------------------------\n", func);
}

void cyapa_print_paltform_data(const char *func,
		struct cyapa_platform_data *cyapa_i2c_platform_data)
{
	printk("%s: -----------------------------------------\n", func);
	printk("%s: cyapa_i2c_platform_data.max_touchpad_x = %d\n", func,
		cyapa_i2c_platform_data->max_touchpad_x);
	printk("%s: cyapa_i2c_platform_data.max_touchpad_y = %d\n", func,
		cyapa_i2c_platform_data->max_touchpad_y);
	printk("%s: cyapa_i2c_platform_data.min_touchpad_x = %d\n", func,
		cyapa_i2c_platform_data->min_touchpad_x);
	printk("%s: cyapa_i2c_platform_data.min_touchpad_y = %d\n", func,
		cyapa_i2c_platform_data->min_touchpad_y);
	printk("%s: cyapa_i2c_platform_data.flag = 0x%08x\n", func,
		cyapa_i2c_platform_data->flag);
	printk("%s: cyapa_i2c_platform_data.gen = 0x%02x\n", func,
		cyapa_i2c_platform_data->gen);
	printk("%s: cyapa_i2c_platform_data.power_state = 0x%02x\n", func,
		cyapa_i2c_platform_data->power_state);
	printk("%s: cyapa_i2c_platform_data.use_absolute_mode = %s\n", func,
		cyapa_i2c_platform_data->use_absolute_mode?"true":"false");
	printk("%s: cyapa_i2c_platform_data.use_polling_mode = %s\n", func,
		cyapa_i2c_platform_data->use_polling_mode?"true":"false");
	printk("%s: cyapa_i2c_platform_data.polling_interval_time_active = %d\n",
		func, cyapa_i2c_platform_data->polling_interval_time_active);
	printk("%s: cyapa_i2c_platform_data.polling_interval_time_lowpower = %d\n",
		func, cyapa_i2c_platform_data->polling_interval_time_lowpower);
	printk("%s: cyapa_i2c_platform_data.active_touch_timeout = %d\n", func,
		cyapa_i2c_platform_data->active_touch_timeout);
	printk("%s: cyapa_i2c_platform_data.name = %s\n", func,
		cyapa_i2c_platform_data->name);
	printk("%s: cyapa_i2c_platform_data.irq_gpio = %d\n", func,
		cyapa_i2c_platform_data->irq_gpio);
	printk("%s: cyapa_i2c_platform_data.report_rate = %d\n", func,
		cyapa_i2c_platform_data->report_rate);
	printk("%s: cyapa_i2c_platform_data.init = %s%p\n", func,
		cyapa_i2c_platform_data->init?"0x":"", cyapa_i2c_platform_data->init);
	printk("%s: cyapa_i2c_platform_data.wakeup = %s%p\n", func,
		cyapa_i2c_platform_data->wakeup?"0x":"", cyapa_i2c_platform_data->wakeup);
	printk("%s: -----------------------------------------\n", func);
}
#endif


/*
 * Driver's initial design makes no race condition possible on i2c bus,
 * so there is no need in any locking.
 * Keep it in mind, while playing with the code.
 */
static s32 cyapa_i2c_reg_read_byte(struct i2c_client *client, u16 reg)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, (u8)reg & 0xff);

	return ((ret < 0) ? 0 : ret);
}

static s32 cyapa_i2c_reg_write_byte(struct i2c_client *client, u16 reg, u8 val)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, (u8)reg & 0xff, val);

	return ((ret < 0) ? 0 : ret);
}

static s32 cyapa_i2c_reg_read_block(struct i2c_client *client, u16 reg,
				int length, u8 *values)
{
	int retval;
	u8 buf[1];
	
	/*
	** depending on PSOC easy I2C read operations.
	** step1: set read pointer of easy I2C.
	** step2: read data.
	*/
	// step1: set read pointer of easy I2C.
	memset(buf, 0, 1);
	buf[0] = (u8)(((u8)reg) & 0xff);
	retval = i2c_master_send(client, buf, 1);
	if (retval < 0) {
		DBGPRINTK(("%s: i2c_master_send error, retval=%d\n", __func__, retval));
		return retval;
	}
	
	//step2: read data.
	retval = i2c_master_recv(client, values, length);
	if (retval < 0) {
		DBGPRINTK(("%s: i2c_master_recv error, retval=%d\n", __func__, retval));
		return retval;
	}

#ifdef DBG_CYAPA_READ_BLOCK_DATA
	/* debug message */
	cyapa_print_data_block(__func__, (u8)reg, retval, values);
#endif

	if (retval != length) {
		dev_warn(&client->dev,
			"%s: warning I2C block read bytes [%d] not equal to required bytes [%d].\n",
			__func__, retval, length);
	}

	return retval;
}

static s32 cyapa_i2c_reg_write_block(struct i2c_client *client, u16 reg,
				u8 length, const u8 *values)

{
	int retval;
	int i;
	u8 buf[256];

 	if ((length+1) > 256) {
		DBGPRINTK(("%s: invalid write data length, length=%d\n", __func__, length));
		return -EINVAL;
	}

	/*
	** depending on PSOC easy I2C read operations.
	** step1: write data to easy I2C in one command.
	*/
	// step1: write data to easy I2C in one command.
	memset(buf, 0, 256);
	buf[0] = (u8)(((u8)reg) & 0xff);
	/* move data shoud be write to I2C slave device. */
	for (i=1; i<length; i++)
	{
		buf[i] = values[i-1];
	}
	retval = i2c_master_send(client, buf, length+1);
	if (retval < 0) {
		DBGPRINTK(("%s: i2c_master_send error, retval=%d\n", __func__, retval));
		return retval;
	}

	if (retval != (length+1)) {
		dev_warn(&client->dev,
			"%s: warning I2C block write bytes [%d] not equal to required bytes [%d].\n",
			__func__, retval, length);
	}

	return retval;
}

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
	unsigned char query_data[40];
	int ret_read_size = 0;
	int i;

	/* query data has been supported in GEN1 protocol.*/
	if (touch->platform_data->gen == CYAPA_GEN2)
	{
		memset(query_data, 0, 40);
		ret_read_size = cyapa_i2c_reg_read_block(touch->client,
					touch->query_base_offset,
					38,
					(u8 *)&query_data);

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

		touch->fm_maj_ver = query_data[15];
		touch->fm_min_ver = query_data[16];
		touch->hw_maj_ver = query_data[17];
		touch->hw_min_ver = query_data[18];

		for (i=0; i<13; i++) {
			touch->capability[i] = query_data[19+i];
		}

		touch->max_absolution_x = (((query_data[32] & 0xF0) << 4) | query_data[33]);
		touch->max_absolution_y = (((query_data[32] & 0x0F) << 8) | query_data[34]);
		if (!touch->max_absolution_x || !touch->max_absolution_y) {
			if (!strcmp(touch->product_id, "CYTRA-014001-00")) {
				touch->max_absolution_x = 1600;
				touch->max_absolution_y = 900;
			} else {
				touch->max_absolution_x = 1200;
				touch->max_absolution_y = 600;
			}
		}

		touch->physical_size_x = (((query_data[35] & 0xF0) << 4) | query_data[36]);
		touch->physical_size_y = (((query_data[35] & 0x0F) << 8) | query_data[37]);
		if (!touch->physical_size_x || !touch->physical_size_y) {
			touch->physical_size_x = 105;
			touch->physical_size_y = 60;
		}

		printk("Cypress Trackpad Information:\n");
		printk("\t\t\tProduction ID:  %s\n", touch->product_id);
		printk("\t\t\tFirmware version:  %d.%d\n", touch->fm_maj_ver, touch->fm_min_ver);
		printk("\t\t\tHardware version:  %d.%d\n", touch->hw_maj_ver, touch->hw_min_ver);
		printk("\t\t\tResolution X,Y:   %d,%d\n", touch->max_absolution_x, touch->max_absolution_y);
		printk("\t\t\tPhysical Size X,Y:   %d,%d\n", touch->physical_size_x, touch->physical_size_y);
	}
}

static int cyapa_i2c_reconfig(struct cyapa_i2c *touch)
{
	struct i2c_client *client = touch->client;
	int regval = 0;
	int retval = 0;

	if (touch->platform_data->gen == CYAPA_GEN1) {
		/* trackpad gen1 firmware. */
		DBGPRINTK(("%s: trackpad support gen1 firmware. \n", __func__));
		
		regval = cyapa_i2c_reg_read_byte(client, DEV_POWER_REG);
		DBGPRINTK(("%s: read trackpad interrupt bit = 0x%02x \n",
					__func__, regval&INTERRUPT_MODE_MASK));

		if ( (touch->platform_data->use_polling_mode == true) &&
			((regval & INTERRUPT_MODE_MASK) == INTERRUPT_MODE_MASK) )
		{
			/* reset trackpad to polling mode. */
			regval &= (~INTERRUPT_MODE_MASK);
			retval = cyapa_i2c_reg_write_byte(client, DEV_POWER_REG, (u8)(regval & 0xff));
			if (retval) {
				DBGPRINTK(("%s: set to polliing mode failed, retval=%d.\n", __func__, retval));
				/*
				 * Though firmware has set interrupt mode bit.
				 * but since platfrom doesn't support interrupt mode,
				 * so also use polling mode here.
				 * do nothing.
				 */
			}
		}
		else if ( (touch->platform_data->use_polling_mode == false) &&
			((regval & INTERRUPT_MODE_MASK) != INTERRUPT_MODE_MASK) )
		{
			/* reset trackpad to interrupt mode. */
			regval |= INTERRUPT_MODE_MASK;
			retval = cyapa_i2c_reg_write_byte(client, DEV_POWER_REG, (u8)(regval & 0xff));
			if (retval) {
				DBGPRINTK(("%s: set to interrup mode failed, retval=%d.\n", __func__, retval));
				touch->platform_data->use_polling_mode = true;
			}
		}

		DBGPRINTK(("%s: trackpad interrupt bit = 0x%02x \n", __func__,
				(u8)cyapa_i2c_reg_read_byte(client, DEV_POWER_REG)));
	} else {
		/* trackpad gen2 firmware. default is interrupt mode. */
		DBGPRINTK(("%s: trackpad support gen2 firmware.\n", __func__));

		cyapa_get_reg_offset(touch);
		cyapa_get_query_data(touch);
	}

	DBGPRINTK(("%s: use %s mode.\n", __func__,
			((touch->platform_data->use_polling_mode == true)?"polling":"interrupt")));
	return retval;
}

static int cyapa_i2c_reset_config(struct cyapa_i2c *touch)
{
	int ret = 0;

	DBGPRINTK(("%s: ... \n", __func__));
	
	return ret;
}

static int cyapa_verify_data_device(struct cyapa_i2c *touch,
				union cyapa_reg_data *reg_data)
{
	struct cyapa_reg_data_gen1 *data_gen1 = NULL;
	struct cyapa_reg_data_gen2 *data_gen2 = NULL;

	if (touch->platform_data->gen == CYAPA_GEN1) {
		data_gen1 = &reg_data->gen1_data;
		if ((data_gen1->tap_motion & 0x08) != 0x08) {
			/* invalid data. */
			DBGPRINTK(("%s: invalid data reg address 0x00, bit3 is not set. \n", __func__));
			return -EINVAL;
		}
	} else {
		data_gen2 = &reg_data->gen2_data;
		if ((data_gen2->device_status & 0x80) != 0x80) {
			/* invalid data. */
			DBGPRINTK(("%s: invalid data reg address 0x00, bit7 is not set. \n", __func__));
			return -EINVAL;
		}

		if ((data_gen2->device_status & 0x03) != CYAPA_DEV_NORNAL) {
			DBGPRINTK(("%s: invalid device status = 0x%02x, wait for device ready. \n",
					__func__, (data_gen2->device_status & 0x03)));
			return -EBUSY;
		}
	}

	return 0;
}

static inline void cyapa_calculate_abs_xy(struct cyapa_i2c *touch, 
					struct cyapa_report_data *report_data)
{
	int i;
	int sum_x = 0, sum_y = 0;

	/* invalid input data. */
	if (!touch->xy_touchs_included_bits || !report_data->touch_fingers) {
		touch->prev_abs_x = -1;
		touch->prev_abs_y = -1;
		return;
	}

	for (i=0; i<CYAPA_MAX_TOUCHS; i++) {
		if (touch->xy_touchs_included_bits & (0x01 << i)) {
			sum_x += report_data->touchs[i].x;
			sum_y += report_data->touchs[i].y;
		}
	}

	touch->abs_x = sum_x / report_data->touch_fingers;
	touch->abs_y = sum_y / report_data->touch_fingers;
	/* x, y directory of Cypress trackpad is in negative direction of screen.
	** for some platform it maybe different. */
	//touch->abs_x = touch->platform_data->max_touchpad_x - touch->abs_x;
	//touch->abs_y = touch->platform_data->max_touchpad_y - touch->abs_y;

	/* use simple filtr to make cursor move smoother. */
	if (touch->prev_abs_x != -1) {
		touch->abs_x = (touch->abs_x * 3 + touch->prev_abs_x) >> 2;
		touch->abs_y = (touch->abs_y * 3 + touch->prev_abs_y) >> 2;
	}

	touch->prev_abs_x = touch->abs_x;
	touch->prev_abs_y = touch->abs_y;
}

static inline int cyapa_sqrt(int delta_x, int delta_y)
{
	int Xk0 = 0;
	int Xk1;
	int multi;

	multi = Xk1 = delta_x*delta_x + delta_y*delta_y;

	while( abs(Xk0 - Xk1) > 1)
	{
		Xk0 = Xk1;
		Xk1 = (Xk0 + (multi / Xk0) ) / 2;
	}

	return Xk1;
}

static void cyapa_parse_gen1_data(struct cyapa_i2c *touch,
			struct cyapa_reg_data_gen1 *reg_data,
			struct cyapa_report_data *report_data)
{
	int i;
	int gesture_report_index = 0;
	int gesture_fingers = 0;
	int gesture_index = 0;

	/* parse gestures and button data */
	report_data->button = reg_data->tap_motion & 0x01;

	/* get relative delta X and delta Y. */
	report_data->rel_deltaX = reg_data->deltax;
	/* The Y directory of trackpad is the oppsite of Screen. */
	report_data->rel_deltaY = -reg_data->deltay;

	if (reg_data->tap_motion & 0x02) {
		report_data->gestures[gesture_report_index++].id = GESTURE_SINGLE_TAP;
		
		gesture_fingers = GESTURE_DECODE_FINGERS(GESTURE_SINGLE_TAP);
		gesture_index = GESTURE_DECODE_INDEX(GESTURE_SINGLE_TAP);
		touch->cur_active_gestures[gesture_fingers] |= (1UL << gesture_index);
	}

	if (reg_data->tap_motion & 0x04) {
		report_data->gestures[gesture_report_index++].id = GESTURE_DOUBLE_TAP;
		
		gesture_fingers = GESTURE_DECODE_FINGERS(GESTURE_DOUBLE_TAP);
		gesture_index = GESTURE_DECODE_INDEX(GESTURE_DOUBLE_TAP);
		touch->cur_active_gestures[gesture_fingers] |= (1UL << gesture_index);
	}

	report_data->gestures_count = gesture_report_index;

	/* pase fingers touch data */
	report_data->touch_fingers = ((reg_data->touch_fingers > CYAPA_MAX_TOUCHS) ?
			(CYAPA_MAX_TOUCHS) : (reg_data->touch_fingers));
	report_data->avg_pressure = reg_data->avg_pressure;
	report_data->touchs[0].x =
			((reg_data->touch1.rel_xy & 0xF0) << 4) | reg_data->touch1.rel_x;
	report_data->touchs[0].y =
			((reg_data->touch1.rel_xy & 0x0F) << 8) | reg_data->touch1.rel_y;
	report_data->touchs[0].id = 0;

	for (i=0; i<(CYAPA_MAX_TOUCHS-1); i++) {
		report_data->touchs[i+1].x =
				((reg_data->touchs[i].rel_xy & 0xF0) << 4) | reg_data->touchs[i].rel_x;
		report_data->touchs[i+1].y =
				((reg_data->touchs[i].rel_xy & 0x0F) << 8) | reg_data->touchs[i].rel_y;
		report_data->touchs[i+1].id = i+1;
	}

	#ifdef DBG_CYAPA_READ_BLOCK_DATA
	cyapa_print_report_data(__func__, report_data);
	#endif
}

static void cyapa_parse_gen2_data(struct cyapa_i2c *touch,
			struct cyapa_reg_data_gen2 *reg_data,
			struct cyapa_report_data *report_data)
{
	int i;
	int gesture_fingers = 0;
	int gesture_index = 0;

	/* bit2-middle button; bit1-right button; bit0-left buttom. */
	report_data->button = reg_data->relative_flags & 0x07;

	/* get relative delta X and delta Y. */
	report_data->rel_deltaX = reg_data->deltax;
	/* The Y directory of trackpad is the oppsite of Screen. */
	report_data->rel_deltaY = -reg_data->deltay;

	/* copy fingers touch data */
	report_data->avg_pressure = reg_data->avg_pressure;
	report_data->touch_fingers = ((reg_data->touch_fingers > CYAPA_MAX_TOUCHS) ?
			(CYAPA_MAX_TOUCHS) : (reg_data->touch_fingers));
	for (i=0; i<report_data->touch_fingers; i++) {
		report_data->touchs[i].x =
				((reg_data->touchs[i].xy & 0xF0) << 4) | reg_data->touchs[i].x;
		report_data->touchs[i].y =
				((reg_data->touchs[i].xy & 0x0F) << 8) | reg_data->touchs[i].y;
		report_data->touchs[i].id = reg_data->touchs[i].id;
	}

	/* parse gestures */
	report_data->gestures_count =
			(((reg_data->gesture_count) > CYAPA_ONE_TIME_GESTURES) ?
				CYAPA_ONE_TIME_GESTURES : reg_data->gesture_count);
	for (i=0; i<report_data->gestures_count; i++) {
		report_data->gestures[i].id = reg_data->gesture[i].id;
		report_data->gestures[i].param1 = reg_data->gesture[i].param1;
		report_data->gestures[i].param2 = reg_data->gesture[i].param2;

		gesture_fingers = GESTURE_DECODE_FINGERS(report_data->gestures[i].id);
		gesture_index = GESTURE_DECODE_INDEX(report_data->gestures[i].id);
		touch->cur_active_gestures[gesture_fingers] |= (1UL << gesture_index);
	}

	#ifdef DBG_CYAPA_READ_BLOCK_DATA
	cyapa_print_report_data(__func__, report_data);
	#endif
}

static inline void cyapa_report_fingers(struct input_dev *input, int fingers)
{
	if (fingers) {
		input_report_key(input, BTN_TOOL_FINGER, (fingers == 1));
		input_report_key(input, BTN_TOOL_DOUBLETAP, (fingers == 2));
		input_report_key(input, BTN_TOOL_TRIPLETAP, (fingers == 3));
		input_report_key(input, BTN_TOOL_QUADTAP, (fingers > 3));
	} else {
		input_report_key(input, BTN_TOOL_FINGER, 0);
		input_report_key(input, BTN_TOOL_DOUBLETAP, 0);
		input_report_key(input, BTN_TOOL_TRIPLETAP, 0);
		input_report_key(input, BTN_TOOL_QUADTAP, 0);
	}
}

static void cyapa_process_prev_gesture_report(struct cyapa_i2c *touch,
					struct cyapa_report_data *report_data)
{
	int i, j;
	unsigned long gesture_diff;
	struct input_dev *input = touch->input;
	struct input_dev *input_kbd = touch->input_kbd;
	
	for (i=0; i<MAX_FINGERS; i++) {
		/* get all diffenent gestures in prev and cur. */
		gesture_diff = touch->prev_active_gestures[i] ^ touch->cur_active_gestures[i];
		/* get all prev gestures that has been canceled in cur. */
		gesture_diff = gesture_diff & touch->prev_active_gestures[i];
		if (gesture_diff) {
			for (j=0; j<(sizeof(unsigned long)*8); j++) {
				/* cancel previous exists gesture. */
				if ((gesture_diff >> j) && 1UL) {			
					switch (GESTURE_ID_CODE(i, j)) {
					case GESTURE_PALM_REJECTIOIN:
						break;
					case GESTURE_SINGLE_TAP	:
						break;
					case GESTURE_DOUBLE_TAP:
						break;
					case GESTURE_TAP_AND_HOLD:
						break;
					case GESTURE_EDGE_MOTION:
						break;
					case GESTURE_DRAG:
						touch->prev_abs_x = -1;
						touch->prev_abs_y = -1;

						if (touch->platform_data->use_absolute_mode) {
							input_report_key(input, BTN_TOUCH, 0);
							input_report_abs(input, ABS_PRESSURE, 0);
							cyapa_report_fingers(input, 0);
							input_report_key(input, BTN_LEFT, 0);
							input_sync(input);
						}
						break;
					case GESTURE_2F_ZOOM_IN:
						touch->zoomin_delta = 0;
						break;
					case GESTURE_2F_ZOOM_OUT:
						touch->zoomout_delta = 0;
						break;
					case GESTURE_SCROLL_UP:
					case GESTURE_2F_SCROLL_UP:
						touch->delta_scroll_up = 0;
						break;
					case GESTURE_SCROLL_DOWN:
					case GESTURE_2F_SCROLL_DOWN:
						touch->delta_scroll_down = 0;
						break;
					case GESTURE_SCROLL_LEFT:
					case GESTURE_2F_SCROLL_LEFT:
						input_report_key(input_kbd, KEY_LEFTSHIFT, 0);
						input_sync(input_kbd);
						touch->hscroll_left = 0;
						touch->delta_scroll_left = 0;
						break;
					case GESTURE_SCROLL_RIGHT:
					case GESTURE_2F_SCROLL_RIGHT:
						input_report_key(input_kbd, KEY_LEFTSHIFT, 0);
						input_sync(input_kbd);
						touch->hscroll_right = 0;
						touch->delta_scroll_right = 0;
						break;
					case GESTURE_2F_ROTATE:
						break;
					case GESTURE_2F_PINCH:
						break;
					case GESTURE_2F_TAP:
						break;
					case GESTURE_2F_DRAG:
						if (touch->platform_data->use_absolute_mode) {
							input_report_key(input, BTN_TOUCH, 0);
							input_report_abs(input, ABS_PRESSURE, 0);
							input_report_key(input, BTN_LEFT, 0);
							cyapa_report_fingers(input, 0);
							input_sync(input);
						}
						
						touch->gesture_2F_drag_started = 0;
						touch->prev_abs_x = -1;
						touch->prev_abs_y = -1;
						break;
					case GESTURE_FLICK:
					case GESTURE_2F_FLICK:
					case GESTURE_3F_FLICK:
					case GESTURE_4F_FLICK:
					case GESTURE_5F_FLICK:
						break;
					default:
						break;
					}
				}
			}
		}
	}
}

static void cyapa_gesture_report(struct cyapa_i2c *touch,
				struct cyapa_report_data *report_data,
				struct cyapa_gesture *gesture)
{
	struct input_dev *input = touch->input;
	struct input_dev *input_wheel = touch->input_wheel;
	struct input_dev *input_kbd = touch->input_kbd;
	int delta = 0;
	struct cyapa_preferences *preferences = &touch->preferences;
	int threshold = 0;
	int value = 0;

	switch (gesture->id) {
		case GESTURE_PALM_REJECTIOIN:
			/* when palm rejection gesture is trigged, do not move cursor any more,
			** just operation as no finger touched on trackpad.
			*/
			if (touch->platform_data->use_absolute_mode) {
				input_report_key(input, BTN_TOUCH, 0);
				input_report_abs(input, ABS_PRESSURE, 0);
				input_report_abs(input, ABS_TOOL_WIDTH, 0);
				cyapa_report_fingers(input, 0);
			}

			touch->prev_abs_x = -1;
			touch->prev_abs_y = -1;

			input_report_key(input, BTN_LEFT, report_data->button & 0x01);
			input_report_key(input, BTN_RIGHT, report_data->button & 0x02);
			input_report_key(input, BTN_MIDDLE, report_data->button & 0x04);

			input_sync(input);
		
			DBGPRINTK(("%s: report palm rejection\n", __func__));
			break;
		case GESTURE_SINGLE_TAP	:
			if (touch->platform_data->use_absolute_mode) {
				input_report_key(input, BTN_TOUCH, 0);
				input_report_abs(input, ABS_PRESSURE, 0);
				input_report_key(input, BTN_LEFT, 0);
				input_sync(input);
                
                /* in absolute mode use BTN_FINGER to trigger click. */
                break;
			}
			
			input_report_key(input, BTN_LEFT, 1);
			input_sync(input);

			input_report_key(input, BTN_LEFT, 0);
			input_sync(input);

			DBGPRINTK(("%s: report single tap\n", __func__));
			break;
		case GESTURE_DOUBLE_TAP:
			if (touch->platform_data->use_absolute_mode) {
				input_report_key(input, BTN_TOUCH, 0);
				input_report_abs(input, ABS_PRESSURE, 0);
				input_report_key(input, BTN_LEFT, 0);
				input_report_key(input, BTN_RIGHT, 0);
				input_sync(input);
			}
			
			input_report_key(input, BTN_LEFT, 1);
			input_sync(input);

			input_report_key(input, BTN_LEFT, 0);
			input_sync(input);
			
			input_report_key(input, BTN_LEFT, 1);
			input_sync(input);

			input_report_key(input, BTN_LEFT, 0);
			input_sync(input);

			DBGPRINTK(("%s: report double tap\n", __func__));
			break;
		case GESTURE_TAP_AND_HOLD:
			/* one finger click and hold for more than definitioin time, then to do something. */
			DBGPRINTK(("%s: no gesture for Tap and hold yet.\n", __func__));
			break;
		case GESTURE_EDGE_MOTION:
			DBGPRINTK(("%s: no gesture for edge motion yet.\n", __func__));
			break;
		case GESTURE_DRAG:
			/* 1-finger drag. 1-finger double click and hold, then move the finger. */
			if (touch->platform_data->use_absolute_mode) {
				touch->xy_touchs_included_bits = 0x01;
				cyapa_calculate_abs_xy(touch, report_data);

				input_report_key(input, BTN_TOUCH, 1);
				input_report_abs(input, ABS_X, touch->abs_x);
				input_report_abs(input, ABS_Y, touch->abs_y);
				input_report_abs(input, ABS_PRESSURE, report_data->avg_pressure);
				cyapa_report_fingers(input, 1);
				input_report_key(input, BTN_LEFT, 1);
				input_sync(input);
			} else {				
				input_report_rel(input, REL_X, report_data->rel_deltaX);
				input_report_rel(input, REL_Y, report_data->rel_deltaY);
				input_report_key(input, BTN_LEFT, 1);
				input_sync(input);
			}

			DBGPRINTK(("%s: 1 finger drag. \n", __func__));
			break;
		case GESTURE_2F_ZOOM_IN:
			delta = gesture->param2;
			touch->zoomin_delta += delta;
			while (touch->zoomin_delta > 0) {
				input_report_key(input_kbd, KEY_LEFTCTRL, 1);
				input_report_key(input_kbd, KEY_KPPLUS, 1);
				input_sync(input_kbd);

				input_report_key(input_kbd, KEY_LEFTCTRL, 0);
				input_report_key(input_kbd, KEY_KPPLUS, 0);
				input_sync(input_kbd);

				touch->zoomin_delta -= 1;
			}
			
			DBGPRINTK(("%s: 2F zoom in \n", __func__));
			break;
		case GESTURE_2F_ZOOM_OUT:
			delta = gesture->param2;
			touch->zoomout_delta += delta;
			while (touch->zoomout_delta > 0) {
				input_report_key(input_kbd, KEY_LEFTCTRL, 1);
				input_report_key(input_kbd, KEY_KPMINUS, 1);
				input_sync(input_kbd);

				input_report_key(input_kbd, KEY_LEFTCTRL, 0);
				input_report_key(input_kbd, KEY_KPMINUS, 0);
				input_sync(input_kbd);

				touch->zoomout_delta -= 1;
			}

			DBGPRINTK(("%s: 2F zoom out \n", __func__));
			break;
		case GESTURE_SCROLL_UP:
		case GESTURE_2F_SCROLL_UP:
			delta = gesture->param2;

			threshold = preferences->vscroll.default_threshold;
			value = 1;
			touch->delta_scroll_up += delta;

			if (touch->delta_scroll_up < threshold) {
				/* keep small movement also can work. */
				input_report_rel(input_wheel, REL_WHEEL, value);
				input_sync(input_wheel);

				touch->delta_scroll_up = 0;
				break;
			}
			
			if (touch->delta_scroll_up > preferences->vscroll.fast_threshold) {
				/* fast scroll, reset threshold value. */
				threshold = 1;
				value = 16;
			} else {
				/* middle scroll speed. */
				threshold = 2;
				value = 2;
			}

			while (touch->delta_scroll_up >= threshold) {
				input_report_rel(input_wheel, REL_WHEEL, value*2/threshold);
				input_sync(input_wheel);

				touch->delta_scroll_up -= threshold*value;
			}
			
			DBGPRINTK(("%s: scroll up, fingers=%d\n",
						__func__, report_data->touch_fingers));
			break;
		case GESTURE_SCROLL_DOWN:
		case GESTURE_2F_SCROLL_DOWN:
			delta = gesture->param2;
			threshold = preferences->vscroll.default_threshold;
			value = 1;
			touch->delta_scroll_down += delta;

			if (touch->delta_scroll_down < threshold) {
				/* keep small movement also can work. */
				input_report_rel(input_wheel, REL_WHEEL, -value);
				input_sync(input_wheel);

				touch->delta_scroll_down = 0;
				break;
			}

			if (touch->delta_scroll_down > preferences->hscroll.fast_threshold) {
				/* fast scroll, reset threshold value. */
				threshold = 1;
				value = 16;
			} else {
				/* middle scroll speed. */
				threshold = 2;
				value = 2;
			}
			
			while (touch->delta_scroll_down >= threshold) {
				input_report_rel(input_wheel, REL_WHEEL, -value*2/threshold);
				input_sync(input_wheel);

				touch->delta_scroll_down -= threshold*value;
			}

			DBGPRINTK(("%s: scroll down, finger=%d\n",
						__func__, report_data->touch_fingers));
			break;
		case GESTURE_SCROLL_LEFT:
		case GESTURE_2F_SCROLL_LEFT:
			delta = gesture->param2;
			#if 1
			while (delta > 0) {
				input_report_key(input_kbd, KEY_LEFT, 1);
				input_sync(input_kbd);

				input_report_key(input_kbd, KEY_LEFT, 0);
				input_sync(input_kbd);

				delta -= 4;
			}
			#else
			if (0 == touch->hscroll_left) {
				/* Don't why, when report kbd and mouse/wheel event
				** in the same routine, kbd event will be delayed to take effect.
				** that is when kbd and wheel events are reported at same time,
				** wheel event will take effect immediatelly, but kdb event will be
				** delayed some time to take effect, so the combination of kbd and
				** wheel won't take effect any more here when kbd is delayed.
				** horizontal scroll won't take effect for some time.
				** So, we delay some time to report wheel event also, here.
				** but it still not accurate enouch, so horizontal scroll will also miss,
				** and become a vertical scroll.
				*/
				input_report_key(input_kbd, KEY_LEFTSHIFT, 1);
				input_sync(input_kbd);
				touch->hscroll_left = delta;
			} else {
				while (delta > 0) {
					input_report_rel(input_wheel, REL_WHEEL, 1);
					input_sync(input_wheel);

					delta -= 1;
				}
			}
			#endif
			DBGPRINTK(("%s: scroll left, finger=%d\n",
						__func__, report_data->touch_fingers));
			break;
		case GESTURE_SCROLL_RIGHT:
		case GESTURE_2F_SCROLL_RIGHT:
			delta = gesture->param2;
			#if 1
			while (delta > 0) {
				input_report_key(input_kbd, KEY_RIGHT, 1);
				input_sync(input_kbd);

				input_report_key(input_kbd, KEY_RIGHT, 0);
				input_sync(input_kbd);

				delta -= 4;
			}
			#else
			if (0 == touch->hscroll_right) {
				/* Don't why, when report kbd and mouse/wheel event
				** in the same routine, kbd event will be delayed to take effect.
				** that is when kbd and wheel events are reported at same time,
				** wheel event will take effect immediatelly, but kdb event will be
				** delayed some time to take effect, so the combination of kbd and
				** wheel won't take effect any more here when kbd is delayed.
				** horizontal scroll won't take effect for some time.
				** So, we delay some time to report wheel event also, here.
				** but it still not accurate enouch, so horizontal scroll will also miss,
				** and become a vertical scroll.
				*/
				input_report_key(input_kbd, KEY_LEFTSHIFT, 1);
				input_sync(input_kbd);
				touch->hscroll_right = delta;
			} else {
				while (delta > 0) {
					input_report_rel(input_wheel, REL_WHEEL, -1);
					input_sync(input_wheel);

					delta -= 1;
				}
			}
			#endif
			DBGPRINTK(("%s: scroll right, finger=%d\n",
						__func__, report_data->touch_fingers));
			break;
		case GESTURE_2F_ROTATE:
			DBGPRINTK(("%s: 2 finger rotate \n", __func__));
			break;
		case GESTURE_2F_PINCH:
			DBGPRINTK(("%s: 2 finger pinch\n", __func__));
			break;
		case GESTURE_2F_TAP:
			/* 2-finger tap, active like right button press and relase. */
			if (touch->platform_data->use_absolute_mode) {
				input_report_key(input, BTN_TOUCH, 0);
				input_report_abs(input, ABS_PRESSURE, 0);
				input_report_key(input, BTN_LEFT, 0);
				input_report_key(input, BTN_RIGHT, 0);
				input_sync(input);
			}
			
			input_report_key(input, BTN_RIGHT, 1);
			input_sync(input);

			input_report_key(input, BTN_RIGHT, 0);
			input_sync(input);

			DBGPRINTK(("%s: report 2 fingers tap, active like right button.\n", __func__));
			break;
		case GESTURE_2F_DRAG:
			/* first finger click and hold, and second finger moving for dragging. */
			if (touch->gesture_2F_drag_started == 0) {
				touch->xy_touchs_included_bits = 0x01;
				touch->prev_abs_x = -1;
				touch->prev_abs_y = -1;
				cyapa_calculate_abs_xy(touch, report_data);

				/* firstly, move move cursor to the target for drag. */
				input_report_key(input, BTN_TOUCH, 1);
				if (touch->platform_data->use_absolute_mode) {
					input_report_abs(input, ABS_X, touch->abs_x);
					input_report_abs(input, ABS_Y, touch->abs_y);
					input_report_abs(input, ABS_PRESSURE, report_data->avg_pressure);
					cyapa_report_fingers(input, 1);
				}
				input_report_key(input, BTN_LEFT, 0);
				input_report_key(input, BTN_RIGHT, 0);
				input_sync(input);

				/* second, stop cursor on the target for drag. */
				touch->prev_abs_x = -1;
				touch->prev_abs_y = -1;
				if (touch->platform_data->use_absolute_mode) {
					input_report_key(input, BTN_TOUCH, 0);
					input_report_abs(input, ABS_PRESSURE, 0);
					input_sync(input);
				}

				/* third, select the target for drag. */
				input_report_key(input, BTN_LEFT, 1);
				input_sync(input);

				/* go to step four. */
				touch->gesture_2F_drag_started = 1;
			}

			/* fourth, move cursor for dragging. */
			touch->xy_touchs_included_bits = 0x02;
			cyapa_calculate_abs_xy(touch, report_data);

			if (touch->platform_data->use_absolute_mode) {
				input_report_key(input, BTN_TOUCH, 1);
				input_report_abs(input, ABS_X, touch->abs_x);
				input_report_abs(input, ABS_Y, touch->abs_y);
				input_report_abs(input, ABS_PRESSURE, report_data->avg_pressure);
				cyapa_report_fingers(input, 1);
			} else {
				input_report_rel(input, REL_X, report_data->rel_deltaX);
				input_report_rel(input, REL_Y, report_data->rel_deltaY);
				input_sync(input);
			}
			input_report_key(input, BTN_LEFT, 1);
			input_sync(input);

			DBGPRINTK(("%s: report 2 fingers drag\n", __func__));
			break;
		case GESTURE_FLICK:
		case GESTURE_2F_FLICK:
		case GESTURE_3F_FLICK:
		case GESTURE_4F_FLICK:
		case GESTURE_5F_FLICK:
			touch->xy_touchs_included_bits = report_data->touch_fingers;
			DBGPRINTK(("%s: no flick gesture supported yet, , finger=%d\n",
						__func__, report_data->touch_fingers));
			break;
		default:
			DBGPRINTK(("%s: default, unknown gesture for reporting.\n", __func__));
			break;
	}
}

static int cyapa_rel_input_report_data(struct cyapa_i2c *touch, struct cyapa_report_data *report_data)
{
	int i;
	struct input_dev *input = touch->input;

	/* step 1: process gestures firstly if trigged. */
	cyapa_process_prev_gesture_report(touch, report_data);
	if (report_data->gestures_count > 0) {
		DBGPRINTK(("%s: do gesture report, gestures_count = %d\n",
				__func__, report_data->gestures_count));
		/* gesture trigged */
		for (i=0; i<report_data->gestures_count; i++) {
			cyapa_gesture_report(touch, report_data, &report_data->gestures[i]);
		}

		/* when gestures are trigged, cursor should be fixed. */
		return report_data->gestures_count;
	}

	/* when multi-fingers touched, cursour should also be fixed. */
	if (report_data->touch_fingers == 1) {
		/* Report the deltas */
		input_report_rel(input, REL_X, report_data->rel_deltaX);
		input_report_rel(input, REL_Y, report_data->rel_deltaY);
	}

	/* Report the button event */
	input_report_key(input, BTN_LEFT, (report_data->button & 0x01));
	input_report_key(input, BTN_RIGHT, (report_data->button & 0x02));
	input_report_key(input, BTN_MIDDLE, (report_data->button & 0x04));
	input_sync(input);

	DBGPRINTK(("%s: deltax = %d \n", __func__, report_data->rel_deltaX));
	DBGPRINTK(("%s: deltay = %d \n", __func__, report_data->rel_deltaY));
	DBGPRINTK(("%s: left_btn = %d \n", __func__, report_data->button & 0x01));
	DBGPRINTK(("%s: right_btn = %d \n", __func__, report_data->button & 0x02));
	DBGPRINTK(("%s: middle_btn = %d \n", __func__, report_data->button & 0x04));
	
	return report_data->rel_deltaX |report_data->rel_deltaY | report_data->button;
}

static int cyapa_abs_input_report_data(struct cyapa_i2c *touch, struct cyapa_report_data *report_data)
{
	int i;
	int have_data = 0;
	struct input_dev *input = touch->input;

	DBGPRINTK(("%s: ... \n", __func__));

	cyapa_process_prev_gesture_report(touch, report_data);
	if (report_data->gestures_count > 0) {
		DBGPRINTK(("%s: do gesture report, gestures_count = %d\n",
			__func__, report_data->gestures_count));
		/* gesture trigged */
		for (i=0; i<report_data->gestures_count; i++) {
			cyapa_gesture_report(touch, report_data, &report_data->gestures[i]);
		}
	} else if (report_data->touch_fingers) {
		/* no gesture trigged, report touchs move data. */
		if (report_data->touch_fingers > 1) {
			DBGPRINTK(("%s: more then 1 finger touch, touch_fingers = %d\n",
				__func__, report_data->touch_fingers));
			/*
			** two and much more finger on trackpad are used for gesture only,
			** so even no gesture are trigged, do not make cursor move also.
			** Here, must keep on report finger touched, otherwise, when multi-finger
			** touch not in same time will triiged clikc.
			*/
			input_report_key(input, BTN_TOUCH, 1);
			input_report_abs(input, ABS_PRESSURE, report_data->avg_pressure);
			input_report_abs(input, ABS_TOOL_WIDTH, CYAPA_TOOL_WIDTH);
			#if GESTURE_MULTI_TOUCH_ONE_CLICK
			cyapa_report_fingers(input, report_data->touch_fingers);
			#else
			cyapa_report_fingers(input, 1);
			#endif
			
			touch->prev_abs_x = -1;
			touch->prev_abs_y = -1;

			input_report_key(input, BTN_LEFT, report_data->button & 0x01);
			input_report_key(input, BTN_RIGHT, report_data->button & 0x02);
			input_report_key(input, BTN_MIDDLE, report_data->button & 0x04);

			input_sync(input);
		} else {
			DBGPRINTK(("%s: 1 finger touch, make cursor move\n", __func__));
			/* avoid cursor jump, when touched finger changed from multi-touch
			** to one finger touch. */
			if (touch->prev_touch_fingers > 1) {
				/* cheat system or application that no finger has touched to may
				** them lock the cursor when later only one finger touched on trackpad. */
				input_report_key(input, BTN_TOUCH, 0);
				input_report_abs(input, ABS_PRESSURE, 0);
				input_report_abs(input, ABS_TOOL_WIDTH, 0);
				cyapa_report_fingers(input, 0);
				touch->prev_abs_x = -1;
				touch->prev_abs_y = -1;
				input_report_key(input, BTN_LEFT, report_data->button & 0x01);
				input_report_key(input, BTN_RIGHT, report_data->button & 0x02);
				input_report_key(input, BTN_MIDDLE, report_data->button & 0x04);
				input_sync(input);
			} else {
				/* only 1 finger can make cursor move. */
				touch->xy_touchs_included_bits = 0x01;
				cyapa_calculate_abs_xy(touch, report_data);

				input_report_key(input, BTN_TOUCH, 1);
				input_report_abs(input, ABS_X, touch->abs_x);
				input_report_abs(input, ABS_Y, touch->abs_y);
				input_report_abs(input, ABS_PRESSURE, report_data->avg_pressure);
				input_report_abs(input, ABS_TOOL_WIDTH, CYAPA_TOOL_WIDTH);

				cyapa_report_fingers(input, report_data->touch_fingers);

				input_report_key(input, BTN_LEFT, report_data->button & 0x01);
				input_report_key(input, BTN_RIGHT, report_data->button & 0x02);
				input_report_key(input, BTN_MIDDLE, report_data->button & 0x04);

				input_sync(input);
			}
		}
	} else {
		/*
		** 1. two or more fingers on trackpad are used for gesture only,
		**     so even no gesture are trigged, do not make cursor move also.
		** 2. no gesture and no touch on trackpad.
		*/
		DBGPRINTK(("%s: no finger touch.\n", __func__));

		input_report_key(input, BTN_TOUCH, 0);
		input_report_abs(input, ABS_PRESSURE, 0);
		input_report_abs(input, ABS_TOOL_WIDTH, 0);
		cyapa_report_fingers(input, 0);

		touch->prev_abs_x = -1;
		touch->prev_abs_y = -1;

		input_report_key(input, BTN_LEFT, report_data->button & 0x01);
		input_report_key(input, BTN_RIGHT, report_data->button & 0x02);
		input_report_key(input, BTN_MIDDLE, report_data->button & 0x04);

		input_sync(input);
	}

	/* store current active gestures array into prev active gesture array. */
	for (i=0; i<MAX_FINGERS; i++) {
		touch->prev_active_gestures[i] = touch->cur_active_gestures[i];
	}
	touch->prev_touch_fingers = report_data->touch_fingers;

	have_data = (report_data->gestures_count +
				report_data->touch_fingers + report_data->button);

	DBGPRINTK(("%s: gesture count = %d, touch finger =%d, button = 0x%02x\n",
				__func__, report_data->gestures_count,
				report_data->touch_fingers, report_data->button));
	return have_data;
}

static bool cyapa_i2c_get_input(struct cyapa_i2c *touch)
{
	int i;
	int ret_read_size = -1;
	int read_length = 0;
	union cyapa_reg_data reg_data;
	struct cyapa_reg_data_gen1 *gen1_data;
	struct cyapa_reg_data_gen2 *gen2_data;
	struct cyapa_report_data report_data;

	DBGPRINTK(("%s: start ... \n", __func__));

	memset(&reg_data, 0, sizeof(union cyapa_reg_data));

	/* read register data from trackpad. */
	gen1_data = &reg_data.gen1_data;
	gen2_data = &reg_data.gen2_data;
	read_length = CYAPA_REL_REG_DATA_SIZE;
	if (touch->platform_data->gen == CYAPA_GEN1)
		read_length = (int)sizeof(struct cyapa_reg_data_gen1);
	else
		read_length = (int)sizeof(struct cyapa_reg_data_gen2);
	DBGPRINTK(("%s: read gen%d data, read length=%d \n", __func__,
		((touch->platform_data->gen == CYAPA_GEN1)?1:2), read_length));
	ret_read_size = cyapa_i2c_reg_read_block(touch->client,
					DATA_REG_START_OFFSET,
					read_length,
					(u8 *)&reg_data);
	if (ret_read_size < 0) {
		DBGPRINTK(("%s: I2C read data from trackpad error = %d \n",
					__func__, ret_read_size));
		return 0;
	}

	if (cyapa_verify_data_device(touch, &reg_data)) {
		DBGPRINTK(("%s: verify data device failed, invalid data, skip.\n", __func__));
		return 0;
	}

	/* process and parse raw data that read from Trackpad. */
	memset(&report_data, 0, sizeof(struct cyapa_report_data));
	touch->xy_touchs_included_bits = 0;
	/* initialize current active gestures array. */
	for (i=0; i<MAX_FINGERS; i++) {
		touch->cur_active_gestures[i] = 0;
	}
	
	if (touch->platform_data->gen == CYAPA_GEN1) {
		cyapa_parse_gen1_data(touch, gen1_data, &report_data);
	} else {
		cyapa_parse_gen2_data(touch, gen2_data, &report_data);
	}

	/* report data to input subsystem. */
	if (touch->platform_data->use_absolute_mode == false) {
		return cyapa_rel_input_report_data(touch, &report_data);
	} else {
		return cyapa_abs_input_report_data(touch, &report_data);
	}
}

static void cyapa_i2c_reschedule_work(struct cyapa_i2c *touch, unsigned long delay)
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

	DBGPRINTK(("%s: trackpad interrupt captured. report_rate=%d; read_pending=%d\n",
		__func__, touch->platform_data->report_rate, touch->read_pending));

	if (touch->platform_data->report_rate == 0) {
		/*
		** no limitatioin for data reporting.
		** the report rate depending on trackpad max report rate.
		** this is the default report mode.
		*/
		cyapa_i2c_reschedule_work(touch, 0);
	} else {
		/*
		** when use limited report rate, some important data packages may be lost.
		** Such as a tap or double tap gesture may be lost.
		** So firmware need to keep this data until there data is read.
		*/
		if (!touch->read_pending) {
			touch->read_pending = 1;
			cyapa_i2c_reschedule_work(touch, touch->scan_ms);
		}
	}

	return IRQ_HANDLED;
}

/* Control the Device polling rate / Work Handler sleep time */
static unsigned long cyapa_i2c_adjust_delay(struct cyapa_i2c *touch, bool have_data)
{
	unsigned long delay, nodata_count_thres;

	if (touch->platform_data->use_polling_mode) {
		delay =touch->platform_data->polling_interval_time_active;
		if (have_data) {
			touch->no_data_count = 0;
		} else {
			nodata_count_thres = CYAPA_NO_DATA_THRES / touch->scan_ms;
			if (touch->no_data_count < nodata_count_thres)
				touch->no_data_count++;
			else
				delay = CYAPA_NO_DATA_SLEEP_MSECS;
		}
		return msecs_to_jiffies(delay);
	} else {
		delay = msecs_to_jiffies(CYAPA_THREAD_IRQ_SLEEP_MSECS);
		return round_jiffies_relative(delay);
	}
}

/* Work Handler */
static void cyapa_i2c_work_handler(struct work_struct *work)
{
	bool have_data;
	struct cyapa_i2c *touch = container_of(work, struct cyapa_i2c, dwork.work);
	unsigned long delay;

	DBGPRINTK(("%s: start ... \n", __func__));

	have_data = cyapa_i2c_get_input(touch);

	/*
	 * While interrupt driven, there is no real need to poll the device.
	 * But touchpads are very sensitive, so there could be errors
	 * related to physical environment and the attention line isn't
	 * neccesarily asserted. In such case we can lose the touchpad.
	 * We poll the device once in CYAPA_THREAD_IRQ_SLEEP_SECS and
	 * if error is detected, we try to reset and reconfigure the touchpad.
	 */
	delay = cyapa_i2c_adjust_delay(touch, have_data);
	//cyapa_i2c_reschedule_work(touch, delay);

	touch->read_pending = 0;
	
	DBGPRINTK(("%s: done ... \n", __func__));
}

static int cyapa_i2c_open(struct input_dev *input)
{
	struct cyapa_i2c *touch = input_get_drvdata(input);
	int retval;

	if (0 == touch->open_count) {
		/* Since input_dev mouse, wheel, and kbd will all use same open and close routines.
		** But indeed, reset config to trackpad once is enought,
		** So when trackpad is open for the first time, reset it.
		** for other time not do it.
		*/
		retval = cyapa_i2c_reset_config(touch);
		if (retval) {
			DBGPRINTK(("%s: failed to reset i2c trackpad. error = %d \n", __func__, retval));
			return retval;
		}
	}
	touch->open_count++;

	if (touch->platform_data->use_polling_mode) {
		/*
		** for the firstly time, it is set to CYAPA_NO_DATA_SLEEP_MSECS,
		** when data is read from trackpad, the read speed will
		** be pull up.
		*/
		cyapa_i2c_reschedule_work(touch, msecs_to_jiffies(CYAPA_NO_DATA_SLEEP_MSECS));
	}

	DBGPRINTK(("%s: touch->open_count = %d ... \n", __func__, touch->open_count));

	return 0;
}

static void cyapa_i2c_close(struct input_dev *input)
{
	struct cyapa_i2c *touch = input_get_drvdata(input);

	touch->open_count--;

	if (0 == touch->open_count) {
		/* Since input_dev mouse, wheel, and kbd will all use same open and close routines.
		** so when all mouse, wheel and kbd input_dev is closed,
		** then cancel the delayed work routine.
		*/
		cancel_delayed_work_sync(&touch->dwork);
	}

	DBGPRINTK(("%s: touch->open_count ... \n", __func__, touch->open_count));
}

void cyapa_set_preferences(struct cyapa_preferences *preferences)
{
	/* set default setting for hscroll. */
	preferences->vscroll.default_threshold = 4;
	preferences->vscroll.middle_threshold = 8;
	preferences->vscroll.fast_threshold = 16;

	/* set default setting for vscroll. */
	preferences->hscroll.default_threshold = 4;
	preferences->hscroll.middle_threshold = 8;
	preferences->hscroll.fast_threshold = 16;
}

static struct cyapa_i2c *cyapa_i2c_touch_create(struct i2c_client *client)
{
	struct cyapa_i2c *touch;

	touch = kzalloc(sizeof(struct cyapa_i2c), GFP_KERNEL);
	if (!touch)
		return NULL;

	DBGPRINTK(("%s: client=0x%p, allocate memory for touch successfully.\n",
					__func__, client));

	touch->platform_data = &cyapa_i2c_platform_data;
	if (client->dev.platform_data) {
		DBGPRINTK(("%s: client->dev.platform_data is set, copy it.\n", __func__));
		*touch->platform_data = *(struct cyapa_platform_data *)client->dev.platform_data;
	}

	#ifdef DBG_CYAPA_READ_BLOCK_DATA
	cyapa_print_paltform_data(__func__, touch->platform_data);
	#endif

	if (touch->platform_data->use_polling_mode &&
		(touch->platform_data->report_rate == 0)) {
		/* when user miss setting platform data,
		** ensure that system is robust.
		** no divid zero error. */
		touch->platform_data->report_rate = CYAPA_POLLING_REPORTRATE_DEFAULT;
	}
	touch->scan_ms = touch->platform_data->report_rate?(1000 / touch->platform_data->report_rate):0;
	touch->open_count = 0;
	touch->prev_abs_x = -1;
	touch->prev_abs_y = -1;
	touch->client = client;
	touch->zoomin_delta = 0;
	touch->zoomout_delta = 0;
	touch->hscroll_left = 0;
	touch->hscroll_right = 0;
	touch->prev_touch_fingers = 0;

	cyapa_set_preferences(&touch->preferences);

	INIT_DELAYED_WORK(&touch->dwork, cyapa_i2c_work_handler);
	spin_lock_init(&touch->lock);

	return touch;
}

static int cyapa_create_input_dev_mouse(struct cyapa_i2c *touch)
{
	int retval = 0;
	struct input_dev *input = NULL;

	input = touch->input = input_allocate_device();
	if (!touch->input) {
		dev_err(&touch->client->dev, "%s: Allocate memory for Input device failed: %d\n",
					__func__, retval);
		return -ENOMEM;
	}

	input->name = "cyapa_i2c_trackpad";
	input->phys = touch->client->adapter->name;
	input->id.bustype = BUS_I2C;
	input->id.version = 1;
	input->dev.parent = &touch->client->dev;

	input->open = cyapa_i2c_open;
	input->close = cyapa_i2c_close;
	input_set_drvdata(input, touch);

	if (touch->platform_data->use_absolute_mode)
	{
		/* absolution data report mode. */
		__set_bit(EV_ABS, input->evbit);
		__set_bit(EV_KEY, input->evbit);
		
		input_set_abs_params(input, ABS_X, touch->max_absolution_x/10,
						touch->max_absolution_x/2, 0, 0);
		input_set_abs_params(input, ABS_Y, touch->max_absolution_y/10,
						touch->max_absolution_y/2, 0, 0);
		input_set_abs_params(input, ABS_PRESSURE, 0, 255, 0, 0);
		input_set_abs_params(input, ABS_TOOL_WIDTH, 0, 255, 0, 0);
		
		__set_bit(BTN_TOUCH, input->keybit);
		__set_bit(BTN_TOOL_FINGER, input->keybit);
		__set_bit(BTN_TOOL_DOUBLETAP, input->keybit);
		__set_bit(BTN_TOOL_TRIPLETAP, input->keybit);
		__set_bit(BTN_TOOL_QUADTAP, input->keybit);

		__set_bit(BTN_LEFT, input->keybit);
		__set_bit(BTN_RIGHT, input->keybit);
		__set_bit(BTN_MIDDLE, input->keybit);

		__clear_bit(EV_REL, input->evbit);
		__clear_bit(REL_X, input->relbit);
		__clear_bit(REL_Y, input->relbit);
		__clear_bit(BTN_TRIGGER, input->keybit);

		input_abs_set_res(input, ABS_X, touch->max_absolution_x/touch->physical_size_x);
		input_abs_set_res(input, ABS_Y, touch->max_absolution_y/touch->physical_size_y);

		DBGPRINTK(("%s: Use absolute data reporting mode. \n", __func__));
	}
	else
	{
		/* relative data reporting mode. */
		__set_bit(EV_REL, input->evbit);
		__set_bit(REL_X, input->relbit);
		__set_bit(REL_Y, input->relbit);

		__set_bit(EV_KEY, input->evbit);
		__set_bit(BTN_LEFT, input->keybit);
		__set_bit(BTN_RIGHT, input->keybit);
		__set_bit(BTN_MIDDLE, input->keybit);

		__clear_bit(EV_ABS, input->evbit);

		DBGPRINTK(("%s: Use relative data reporting mode. \n", __func__));
	}

	/* Register the device in input subsystem */
	retval = input_register_device(touch->input);
	if (retval) {
		dev_err(&touch->client->dev, "%s: Input device register failed: %d\n", __func__, retval);

		input_free_device(input);
		return retval;
	}

	return 0;
}

static int cyapa_create_input_dev_wheel(struct cyapa_i2c *touch)
{
	int retval =0;
	struct input_dev *input_wheel = NULL;

	input_wheel = touch->input_wheel = input_allocate_device();
	if (!touch->input_wheel) {
		dev_err(&touch->client->dev, "%s: Allocate memory for Input device failed: %d\n",
					__func__, retval);
		return -ENOMEM;
	}
	
	input_wheel->name = "cyapa_i2c_wheel";
	input_wheel->phys = touch->client->adapter->name;
	input_wheel->id.bustype = BUS_I2C;
	input_wheel->id.version = 1;
	input_wheel->dev.parent = &touch->client->dev;
	input_wheel->open = cyapa_i2c_open;
	input_wheel->close = cyapa_i2c_close;
	input_set_drvdata(input_wheel, touch);

	__set_bit(EV_KEY, input_wheel->evbit);
	__set_bit(EV_REL, input_wheel->evbit);
	__set_bit(REL_WHEEL, input_wheel->relbit);

	retval = input_register_device(touch->input_wheel);
	if (retval) {
		dev_err(&touch->client->dev, "%s: Input device register failed: %d\n", __func__, retval);

		input_free_device(input_wheel);
		return retval;
	}

	return 0;
}

#define MAX_NR_SCANCODES  128

static unsigned char cyapa_virtual_keycode[MAX_NR_SCANCODES] = {
/* Bellow keys are supported.
KEY_ENTER		28
KEY_LEFTCTRL	29
KEY_LEFTSHIFT	42
KEY_RIGHTSHIFT	54
KEY_LEFTALT		56
KEY_KPMINUS  	74
KEY_KPPLUS   	78
KEY_RIGHTCTRL	97
KEY_RIGHTALT  	100
KEY_HOME		102
KEY_UP			103
KEY_PAGEUP		104
KEY_LEFT			105
KEY_RIGHT		106
KEY_END			107
KEY_DOWN		108
KEY_PAGEDOWN	109
*/
	28, 29, 42, 54, 56, 74, 78, 97, 100, 102, 103, 104, 105, 106, 107, 108, 109
};

static int cyapa_create_input_dev_kbd(struct cyapa_i2c *touch)
{
	int retval =0;
	int i;
	struct input_dev *input_kbd = NULL;

	input_kbd = touch->input_kbd = input_allocate_device();
	if (!touch->input_kbd) {
		dev_err(&touch->client->dev, "%s: Allocate memory for Input device failed: %d\n",
					__func__, retval);
		return -ENOMEM;
	}
	
	input_kbd->name = "cyapa_i2c_virtual_kbd";
	input_kbd->phys = touch->client->adapter->name;
	input_kbd->id.bustype = BUS_I2C;
	input_kbd->id.version = 1;
	input_kbd->dev.parent = &touch->client->dev;
	input_kbd->open = cyapa_i2c_open;
	input_kbd->close = cyapa_i2c_close;
	input_set_drvdata(input_kbd, touch);

	input_kbd->keycode = &cyapa_virtual_keycode;
	input_kbd->keycodesize = sizeof(unsigned char);
	input_kbd->keycodemax = ARRAY_SIZE(cyapa_virtual_keycode);

	__set_bit(EV_KEY, input_kbd->evbit);
	__set_bit(EV_REP, input_kbd->evbit);

	for (i = 0; i < ARRAY_SIZE(cyapa_virtual_keycode); i++) {
		__set_bit(cyapa_virtual_keycode[i], input_kbd->keybit);
	}
	__clear_bit(KEY_RESERVED, input_kbd->keybit);

	retval = input_register_device(touch->input_kbd);
	if (retval) {
		dev_err(&touch->client->dev, "%s: Input device register failed: %d\n", __func__, retval);

		input_free_device(input_kbd);
		return retval;
	}

	return 0;
}

static int __devinit cyapa_i2c_probe(struct i2c_client *client,
			       const struct i2c_device_id *dev_id)
{
	int retval = 0;
	struct cyapa_i2c *touch;

	DBGPRINTK(("%s: start ... \n", __func__));
	touch = cyapa_i2c_touch_create(client);
	if (!touch)
		return -ENOMEM;

	/* do platfrom initialize firstly. */
	if (touch->platform_data->init)
		retval = touch->platform_data->init();
	if (retval)
		goto err_mem_free;

	/* set irq number if not using polling mode. */
	if (touch->platform_data->use_polling_mode == true) {
		touch->irq = -1;
	} else {
		if (touch->platform_data->irq_gpio == -1) {
			if (client->irq) {
				touch->irq = client->irq;
			} else {
				/* irq mode is not supported by system. */
				touch->platform_data->use_polling_mode = true;
				touch->irq = -1;
			}
		} else {
			touch->irq = gpio_to_irq(touch->platform_data->irq_gpio);
		}
	}
	DBGPRINTK(("%s: irq=%d, client->irq=%d\n", __func__, touch->irq, client->irq));

	if (touch->platform_data->use_polling_mode == false) {
		DBGPRINTK(("%s: request interrupt riq. \n", __func__));

		set_irq_type(touch->irq, IRQF_TRIGGER_FALLING);
		retval = request_irq(touch->irq,
					cyapa_i2c_irq,
					0,
					CYAPA_I2C_NAME,
					touch);
		if (retval) {
			dev_warn(&touch->client->dev,
					"%s: IRQ request failed: %d, falling back to polling mode. \n",
					__func__, retval);

			touch->platform_data->use_polling_mode = true;
		}
	}

	/* reconfig trackpad depending on platfrom setting. */
	/* Should disable interrupt to protect this polling read operation.
	** Ohterwise, this I2C read will be interrupt by other reading, and failed. */
	disable_irq(touch->irq);
	cyapa_i2c_reconfig(touch);
	enable_irq(touch->irq);

	/* create an input_dev instance for virtual mouse trackpad. */
	if ((retval = cyapa_create_input_dev_mouse(touch))) {
		DBGPRINTK(("%s: create input_dev instance for mouse trackpad filed. \n", __func__));
		goto err_mem_free;
	}

	/* create an input_dev instances for virtual wheel device and virtual keyboard device. */
	if ((retval = cyapa_create_input_dev_wheel(touch))) {
		DBGPRINTK(("%s: create input_dev instance for wheel filed. \n", __func__));
		goto err_mem_free;
	}

	if ((retval = cyapa_create_input_dev_kbd(touch))) {
		DBGPRINTK(("%s: create input_dev instance for virtual keyboad filed. \n", __func__));
		goto err_mem_free;
	}

	i2c_set_clientdata(client, touch);

	DBGPRINTK(("%s: Done successfully. \n", __func__));

	return 0;

err_mem_free:
	/* release previous allocated input_dev instances. */
	if (touch->input) {
		input_free_device(touch->input);
		touch->input = NULL;
	}

	if (touch->input_wheel) {
		input_free_device(touch->input_wheel);
		touch->input_wheel = NULL;
	}

	if (touch->input_kbd) {
		input_free_device(touch->input_kbd);
		touch->input_kbd = NULL;
	}

	if (touch) {
		kfree(touch);
		touch = NULL;
	}
	DBGPRINTK(("%s: exist with error %d. \n", __func__, retval));
	return retval;
}

static int __devexit cyapa_i2c_remove(struct i2c_client *client)
{
	struct cyapa_i2c *touch = i2c_get_clientdata(client);

	if (!touch->platform_data->use_polling_mode)
		free_irq(client->irq, touch);

	if (touch->input)
		input_unregister_device(touch->input);
	if (touch->input_wheel)
		input_unregister_device(touch->input);
	if (touch->input_kbd)
		input_unregister_device(touch->input);
	if (touch)
		kfree(touch);

	DBGPRINTK(("%s: ... \n", __func__));

	return 0;
}

#ifdef CONFIG_PM
static int cyapa_i2c_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct cyapa_i2c *touch = i2c_get_clientdata(client);

	DBGPRINTK(("%s: ... \n", __func__));
	cancel_delayed_work_sync(&touch->dwork);

	return 0;
}

static int cyapa_i2c_resume(struct i2c_client *client)
{
	int ret;
	struct cyapa_i2c *touch = i2c_get_clientdata(client);

	ret = cyapa_i2c_reset_config(touch);
	DBGPRINTK(("%s: ... \n", __func__));
	if (ret)
		return ret;

	cyapa_i2c_reschedule_work(touch, msecs_to_jiffies(CYAPA_NO_DATA_SLEEP_MSECS));

	return 0;
}
#else
#define cyapa_i2c_suspend	NULL
#define cyapa_i2c_resume	NULL
#endif

static const struct i2c_device_id cypress_i2c_id_table[] = {
	{ CYAPA_I2C_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, cypress_i2c_id_table);

static struct i2c_driver cypress_i2c_driver = {
	.driver = {
		.name	= CYAPA_I2C_NAME,
		.owner	= THIS_MODULE,
	},

	.probe		= cyapa_i2c_probe,
	.remove		= __devexit_p(cyapa_i2c_remove),

	.suspend	= cyapa_i2c_suspend,
	.resume		= cyapa_i2c_resume,
	.id_table	= cypress_i2c_id_table,
};

static int __init cyapa_i2c_init(void)
{
	DBGPRINTK(("%s: start ... \n", __func__));
	return i2c_add_driver(&cypress_i2c_driver);
}

static void __exit cyapa_i2c_exit(void)
{
	DBGPRINTK(("%s: exit ... \n", __func__));
	i2c_del_driver(&cypress_i2c_driver);
}

module_init(cyapa_i2c_init);
module_exit(cyapa_i2c_exit);

MODULE_DESCRIPTION("Cypress I2C Trackpad Driver");
MODULE_AUTHOR("Dudley Du <dudl@cypress.com>");
MODULE_LICENSE("GPL");

