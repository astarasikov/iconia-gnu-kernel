#ifndef CYAPA_H
#define CYAPA_H


#define CYAPA_I2C_NAME  "cypress_i2c_apa"

/* Active power state scanning/processing refresh interval time. unit: ms. */
#define CYAPA_ACTIVE_POLLING_INTVAL_TIME  0x00
/* Low power state scanning/processing refresh interval time. unit: ms. */
#define CYAPA_LOWPOWER_POLLING_INTVAL_TIME 0x10
/* Touch timeout for active power state. unit: ms. */
#define CYAPA_ACTIVE_TOUCH_TIMEOUT  0xFF

/* Max report rate limited for Cypress Trackpad. */
#define CYAPA_NO_LIMITED_REPORT_RATE  0
#define CYAPA_REPORT_RATE  (CYAPA_NO_LIMITED_REPORT_RATE)
#define CYAPA_POLLING_REPORTRATE_DEFAULT 125


/* APA trackpad firmware generation */
enum cyapa_gen
{
	CYAPA_GEN1 = 0x01,
	CYAPA_GEN2 = 0x02,
};

/*
** APA trackpad power states.
** Used in register 0x00, bit3-2, PowerMode field.
*/
enum cyapa_powerstate
{
	CYAPA_PWR_ACTIVE = 0x01,
	CYAPA_PWR_LIGHT_SLEEP = 0x02,
	CYAPA_PWR_MEDIUM_SLEEP = 0x03,
	CYAPA_PWR_DEEP_SLEEP = 0x04,
};

struct cyapa_platform_data
{
	u32 flag;   /* reserved for future use. */
	enum cyapa_gen gen;  /* trackpad firmware generation. */
	enum cyapa_powerstate power_state;
	unsigned use_absolute_mode:1;  /* use absolute data report or relative data report. */
	unsigned use_polling_mode:1;  /* use polling mode or interrupt mode. */
	u8 polling_interval_time_active;  /* active mode, polling refresh interval; ms */
	u8 polling_interval_time_lowpower;  /* low power mode, polling refresh interval; ms */
	u8 active_touch_timeout;  /* active touch timeout; ms */
	char *name;  /* device name of Cypress I2C trackpad. */
	s16 irq_gpio;  /* the gpio id used for interrupt to notify host data is ready. */
	u32 report_rate;  /* max limitation of data report rate. */

	int (*wakeup)(void);
	int (*init)(void);
};


#endif  //#ifndef CYAPA_H
