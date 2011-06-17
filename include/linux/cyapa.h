#ifndef _CYAPA_H
#define _CYAPA_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define CYAPA_I2C_NAME  "cypress_i2c_apa"
#define CYAPA_MISC_NAME  "cyapa"

/* Active power state scanning/processing refresh interval time. unit: ms. */
#define CYAPA_POLLING_INTERVAL_TIME_ACTIVE  0x00
/* Low power state scanning/processing refresh interval time. unit: ms. */
#define CYAPA_POLLING_INTERVAL_TIME_LOWPOWER 0x10
/* Touch timeout for active power state. unit: ms. */
#define CYAPA_ACTIVE_TOUCH_TIMEOUT  0xFF

/* Max report rate limited for Cypress Trackpad. */
#define CYAPA_NO_LIMITED_REPORT_RATE  0
#define CYAPA_REPORT_RATE  (CYAPA_NO_LIMITED_REPORT_RATE)
#define CYAPA_POLLING_REPORTRATE_DEFAULT 60

/* trackpad device */
enum cyapa_work_mode {
	CYAPA_STREAM_MODE = 0x00,
	CYAPA_BOOTLOAD_MODE = 0x01,
};

/* APA trackpad firmware generation */
enum cyapa_gen {
	CYAPA_GEN1 = 0x01,   /* only one finger supported. */
	CYAPA_GEN2 = 0x02,  /* max five fingers supported. */
	CYAPA_GEN3 = 0x03,  /* support MT-protocol with tracking ID. */
};

/*
 * APA trackpad power states.
 * Used in register 0x00, bit3-2, PowerMode field.
 */
enum cyapa_powerstate {
	CYAPA_PWR_ACTIVE = 0x01,
	CYAPA_PWR_LIGHT_SLEEP = 0x02,
	CYAPA_PWR_MEDIUM_SLEEP = 0x03,
	CYAPA_PWR_DEEP_SLEEP = 0x04,
};

struct cyapa_platform_data {
	__u32 flag;   /* reserved for future use. */
	enum cyapa_gen gen;  /* trackpad firmware generation. */
	enum cyapa_powerstate power_state;

	/* active mode, polling refresh interval; ms */
	__u8 polling_interval_time_active;
	/* low power mode, polling refresh interval; ms */
	__u8 polling_interval_time_lowpower;
	__u8 active_touch_timeout;  /* active touch timeout; ms */
	char *name;  /* device name of Cypress I2C trackpad. */
	/* the gpio id used for interrupt to notify host data is ready. */
	__s16 irq_gpio;
	__u32 report_rate;  /* max limitation of data report rate. */

	int (*wakeup)(void);
	int (*init)(void);
};


/*
 * Data structures for /dev/cyapa device ioclt read/write.
 */
struct cyapa_misc_ioctl_data {
	__u8 *buf;  /* pointer to a buffer for read/write data. */
	__u16 len;  /* valid data length in buf. */
	__u16 flag;  /* additional flag to special ioctl command. */
	__u16 rev;  /* reserved. */
};

struct cyapa_driver_ver {
	__u8 major_ver;
	__u8 minor_ver;
	__u8 revision;
};

struct cyapa_firmware_ver {
	__u8 major_ver;
	__u8 minor_ver;
};

struct cyapa_hardware_ver {
	__u8 major_ver;
	__u8 minor_ver;
};

/*
 * Macro codes for misc device ioctl functions.
 ***********************************************************
 |device type|serial num|direction| data  bytes |
 |-----------|----------|---------|-------------|
 | 8 bit     |  8 bit   |  2 bit  | 8~14 bit    |
 |-----------|----------|---------|-------------|
 ***********************************************************
 */
#define CYAPA_IOC_MAGIC 'C'
#define CYAPA_IOC(nr) _IOC(_IOC_NONE, CYAPA_IOC_MAGIC, nr, 0)
/* bytes value is the location of the data read/written by the ioctl. */
#define CYAPA_IOC_R(nr, bytes) _IOC(IOC_OUT, CYAPA_IOC_MAGIC, nr, bytes)
#define CYAPA_IOC_W(nr, bytes) _IOC(IOC_IN, CYAPA_IOC_MAGIC, nr, bytes)
#define CYAPA_IOC_RW(nr, bytes) _IOC(IOC_INOUT, CYAPA_IOC_MAGIC, nr, bytes)

/*
 * The following ioctl commands are only valid
 * when firmware working in operational mode.
 */
#define CYAPA_GET_PRODUCT_ID  CYAPA_IOC_R(0x00, 16)
#define CYAPA_GET_DRIVER_VER  CYAPA_IOC_R(0x01, 3)
#define CYAPA_GET_FIRMWARE_VER  CYAPA_IOC_R(0x02, 2)
#define CYAPA_GET_HARDWARE_VER  CYAPA_IOC_R(0x03, 2)

#define CYAPA_SET_BOOTLOADER_MODE  CYAPA_IOC(0x40)
#define CYAPA_SET_STREAM_MODE  CYAPA_IOC(0x41)

#endif  /* #ifndef _CYAPA_H */
