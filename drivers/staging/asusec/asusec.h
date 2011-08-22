#ifndef _ASUSEC_H
#define _ASUSEC_H

#include "../../../arch/arm/mach-tegra/gpio-names.h"

#define GPIOPIN_CHARGER_ENABLE                TEGRA_GPIO_PR6

/*
 * compiler option
 */
#define ASUSEC_DEBUG			0

/*
 * Debug Utility
 */
#if ASUSEC_DEBUG
#define ASUSEC_INFO(format, arg...)	\
	printk(KERN_INFO "asusec: [%s] " format , __FUNCTION__ , ## arg)
#define ASUSEC_I2C_DATA(array, i)	\
					do {		\
						for (i = 0; i < array[0]+1; i++) \
							ASUSEC_INFO("ec_data[%d] = 0x%x\n", i, array[i]);	\
					} while(0)
#else
#define ASUSEC_INFO(format, arg...)	 
#define ASUSEC_I2C_DATA(array, i)
#endif

#define ASUSEC_NOTICE(format, arg...)	\
	printk(KERN_NOTICE "asusec: [%s] " format , __FUNCTION__ , ## arg)

#define ASUSEC_ERR(format, arg...)	\
	printk(KERN_ERR "asusec: [%s] " format , __FUNCTION__ , ## arg)

//-----------------------------------------	       

#define DRIVER_DESC     		"ASUS EC driver"
#define DOCK_SDEV_NAME			"dock"
#define CONVERSION_TIME_MS		50

#define ASUSEC_RETRY_COUNT		3
#define ASUSEC_POLLING_RATE		80

#define ASUSEC_OBF_MASK			0x1
#define ASUSEC_KEY_MASK			0x4
#define ASUSEC_KBC_MASK			0x8
#define ASUSEC_AUX_MASK			0x20
#define ASUSEC_SCI_MASK			0x40
#define ASUSEC_SMI_MASK			0x80

#define ASUSEC_PS2_ACK			0xFA

struct asusec_chip {
	struct i2c_client	*client;
	struct mutex		lock;
	struct mutex		kbc_lock;
	struct mutex		input_lock;
	struct mutex		dock_init_lock;
	struct delayed_work asusec_work;
	struct delayed_work asusec_dock_init_work;

	int ret_val;
	u8 ec_data[32];
	u8 i2c_data[32];
	u8 i2c_dm_data[32];
	int bc;			// byte counter
	int index;		// for message
	int status;
	int dock_in;	// 0: without dock, 1: with dock
	int dock_init;	// 0: dock not init, 1: dock init successfully
	int init_success; // 0: ps/2 not ready. 1: init OK
	struct atomic_notifier_head notifier_list;

};

extern int asusec_register_notifier(struct asusec_chip *asusec,
		struct notifier_block *nb, unsigned int events);


#endif
