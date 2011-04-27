/*
 * Aptina MT9M114 Camera Driver
 *
 * Copyright (C) 2011 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <media/v4l2-chip-ident.h>
#include <media/soc_camera.h>

#define to_mt9m114(sd)		container_of(sd, struct mt9m114_priv, subdev)

/* Sysctl registers */
#define MT9M114_CHIP_ID					0x0000
#define MT9M114_COMMAND_REGISTER			0x0080
#define MT9M114_COMMAND_REGISTER_APPLY_PATCH		(1 << 0)
#define MT9M114_COMMAND_REGISTER_SET_STATE		(1 << 1)
#define MT9M114_COMMAND_REGISTER_REFRESH		(1 << 2)
#define MT9M114_COMMAND_REGISTER_WAIT_FOR_EVENT		(1 << 3)
#define MT9M114_COMMAND_REGISTER_OK			(1 << 15)
#define MT9M114_PAD_CONTROL				0x0032

/* XDMA registers */
#define MT9M114_ACCESS_CTL_STAT				0x0982
#define MT9M114_PHYSICAL_ADDRESS_ACCESS			0x098a
#define MT9M114_LOGICAL_ADDRESS_ACCESS			0x098e

/* Core registers */
#define MT9M114_RESET_REGISTER				0x301a
#define MT9M114_FLASH					0x3046
#define MT9M114_CUSTOMER_REV				0x31fe

/* Camera Control registers */
#define MT9M114_CAM_SENSOR_CFG_Y_ADDR_START		0xc800
#define MT9M114_CAM_SENSOR_CFG_X_ADDR_START		0xc802
#define MT9M114_CAM_SENSOR_CFG_Y_ADDR_END		0xc804
#define MT9M114_CAM_SENSOR_CFG_X_ADDR_END		0xc806
#define MT9M114_CAM_SENSOR_CFG_PIXCLK			0xc808
#define MT9M114_CAM_SENSOR_CFG_ROW_SPEED		0xc80c
#define MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN	0xc80e
#define MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX	0xc810
#define MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES	0xc812
#define MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK		0xc814
#define MT9M114_CAM_SENSOR_CFG_FINE_CORRECTION		0xc816
#define MT9M114_CAM_SENSOR_CFG_CPIPE_LAST_ROW		0xc818
#define MT9M114_CAM_SENSOR_CFG_REG_0_DATA		0xc826
#define MT9M114_CAM_SENSOR_CONTROL_READ_MODE		0xc834
#define MT9M114_CAM_CROP_WINDOW_XOFFSET			0xc854
#define MT9M114_CAM_CROP_WINDOW_YOFFSET			0xc856
#define MT9M114_CAM_CROP_WINDOW_WIDTH			0xc858
#define MT9M114_CAM_CROP_WINDOW_HEIGHT			0xc85a
#define MT9M114_CAM_CROP_CROPMODE			0xc85c
#define MT9M114_CAM_OUTPUT_WIDTH			0xc868
#define MT9M114_CAM_OUTPUT_HEIGHT			0xc86a
#define MT9M114_CAM_OUTPUT_FORMAT			0xc86c
#define MT9M114_CAM_AET_AEMODE				0xc878
#define MT9M114_CAM_AET_MAX_FRAME_RATE			0xc88c
#define MT9M114_CAM_AET_MIN_FRAME_RATE			0xc88e
#define MT9M114_CAM_AWB_AWB_XSCALE			0xc8f2
#define MT9M114_CAM_AWB_AWB_YSCALE			0xc8f3
#define MT9M114_CAM_AWB_AWB_XSHIFT_PRE_ADJ		0xc904
#define MT9M114_CAM_AWB_AWB_YSHIFT_PRE_ADJ		0xc906
#define MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART		0xc914
#define MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART		0xc916
#define MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND		0xc918
#define MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YEND		0xc91a
#define MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XSTART	0xc91c
#define MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YSTART	0xc91e
#define MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XEND		0xc920
#define MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YEND		0xc922
#define MT9M114_CAM_SYSCTL_PLL_ENABLE			0xc97e
#define MT9M114_CAM_SYSCTL_PLL_DIVIDER_M_N		0xc980
#define MT9M114_CAM_SYSCTL_PLL_DIVIDER_P		0xc982
#define MT9M114_CAM_PORT_OUTPUT_CONTROL			0xc984
#define MT9M114_CAM_PORT_MIPI_TIMING_T_HS_ZERO		0xc988
#define MT9M114_CAM_PORT_MIPI_TIMING_T_HS_EXIT_HS_TRAIL	0xc98a
#define MT9M114_CAM_PORT_MIPI_TIMING_T_CLK_POST_CLK_PRE	0xc98c
#define MT9M114_CAM_PORT_MIPI_TIMING_T_CLK_TRAIL_CLK_ZERO 0xc98e
#define MT9M114_CAM_PORT_MIPI_TIMING_T_LPX		0xc990
#define MT9M114_CAM_PORT_MIPI_TIMING_INIT_TIMING	0xc992

/* System Manager registers */
#define MT9M114_SYSMGR_NEXT_STATE			0xdc00
#define MT9M114_SYSMGR_CURRENT_STATE			0xdc01
#define MT9M114_SYSMGR_CMD_STATUS			0xdc02

/* Patch Loader registers */
#define MT9M114_PATCHLDR_LOADER_ADDRESS			0xe000
#define MT9M114_PATCHLDR_PATCH_ID			0xe002
#define MT9M114_PATCHLDR_FIRMWARE_ID			0xe004
#define MT9M114_PATCHLDR_APPLY_STATUS			0xe008
#define MT9M114_PATCHLDR_NUM_PATCHES			0xe009
#define MT9M114_PATCHLDR_PATCH_ID_0			0xe00a
#define MT9M114_PATCHLDR_PATCH_ID_1			0xe00c
#define MT9M114_PATCHLDR_PATCH_ID_2			0xe00e
#define MT9M114_PATCHLDR_PATCH_ID_3			0xe010
#define MT9M114_PATCHLDR_PATCH_ID_4			0xe012
#define MT9M114_PATCHLDR_PATCH_ID_5			0xe014
#define MT9M114_PATCHLDR_PATCH_ID_6			0xe016
#define MT9M114_PATCHLDR_PATCH_ID_7			0xe018

/* SYS_STATE values (for SYSMGR_NEXT_STATE and SYSMGR_CURRENT_STATE) */
#define MT9M114_SYS_STATE_ENTER_CONFIG_CHANGE		0x28
#define MT9M114_SYS_STATE_STREAMING			0x31
#define MT9M114_SYS_STATE_START_STREAMING		0x34
#define MT9M114_SYS_STATE_ENTER_SUSPEND			0x40
#define MT9M114_SYS_STATE_SUSPENDED			0x41
#define MT9M114_SYS_STATE_ENTER_STANDBY			0x50
#define MT9M114_SYS_STATE_STANDBY			0x52
#define MT9M114_SYS_STATE_LEAVE_STANDBY			0x54

/* Result status of last SET_STATE comamnd */
#define MT9M114_SET_STATE_RESULT_ENOERR			0x00
#define MT9M114_SET_STATE_RESULT_EINVAL			0x0c
#define MT9M114_SET_STATE_RESULT_ENOSPC			0x0d

/* Register read/write macros */
#define READ8(reg, val) \
	do { \
		err = mt9m114_reg_read8(client, reg, val); \
		if (err != 0) \
			goto err; \
	} while (0)

#define WRITE8(reg, val) \
	do { \
		err = mt9m114_reg_write8(client, reg, val); \
		if (err != 0) \
			goto err; \
	} while (0)

#define READ16(reg, val) \
	do { \
		err = mt9m114_reg_read16(client, reg, val); \
		if (err != 0) \
			goto err; \
	} while (0)

#define WRITE16(reg, val) \
	do { \
		err = mt9m114_reg_write16(client, reg, val); \
		if (err != 0) \
			goto err; \
	} while (0)

#define WRITE32(reg, val) \
	do { \
		err = mt9m114_reg_write32(client, reg, val); \
		if (err != 0) \
			goto err; \
	} while (0)

#define POLL16(reg, mask, val) \
	do { \
		err = mt9m114_reg_poll16(client, reg, mask, val, 10, 100); \
		if (err != 0) \
			goto err; \
	} while (0)

#define RMW16(reg, set, unset) \
	do { \
		err = mt9m114_reg_rmw16(client, reg, set, unset); \
		if (err != 0) \
			goto err; \
	} while (0)

#define WRITEARRAY(array) \
	do { \
		err = mt9m114_reg_write_array(client, array, \
					      ARRAY_SIZE(array)); \
		if (err != 0) \
			goto err; \
	} while (0)

/* Misc. structures */
struct mt9m114_priv {
	struct v4l2_subdev		subdev;

	int				ident;
	u16				chip_id;
	u16				revision;

	bool				flag_vflip;
	bool				flag_hflip;

	/* For suspend/resume. */
	struct v4l2_mbus_framefmt	current_mf;
	int				current_enable;
};

enum reg_width {
	REG_U16 = 0,
	REG_U8,
	REG_U32,
};

struct mt9m114_reg {
	u16				reg;
	u32				val;
	enum reg_width			width;
};

static const struct mt9m114_reg mt9m114_defaults[] = {
	/* Reset and clocks. */
	{ MT9M114_RESET_REGISTER,			0x0234 },
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x1000 },
	{ MT9M114_CAM_SYSCTL_PLL_ENABLE,		0x01, REG_U8 },
	{ MT9M114_CAM_SYSCTL_PLL_DIVIDER_M_N,		0x0120 },
	{ MT9M114_CAM_SYSCTL_PLL_DIVIDER_P,		0x0700 },
	{ MT9M114_CAM_PORT_OUTPUT_CONTROL,		0x8041 },
	{ MT9M114_CAM_PORT_MIPI_TIMING_T_HS_ZERO,	0x0f00 },
	{ MT9M114_CAM_PORT_MIPI_TIMING_T_HS_EXIT_HS_TRAIL, 0x0b07 },
	{ MT9M114_CAM_PORT_MIPI_TIMING_T_CLK_POST_CLK_PRE, 0x0d01 },
	{ MT9M114_CAM_PORT_MIPI_TIMING_T_CLK_TRAIL_CLK_ZERO, 0x071d },
	{ MT9M114_CAM_PORT_MIPI_TIMING_T_LPX,		0x0006 },
	{ MT9M114_CAM_PORT_MIPI_TIMING_INIT_TIMING,	0x0a0c },

	/* Sensor optimization */
	{ 0x316a, 0x8270 },
	{ 0x316c, 0x8270 },
	{ 0x3ed0, 0x2305 },
	{ 0x3ed2, 0x77cf },
	{ 0x316e, 0x8202 },
	{ 0x3180, 0x87ff },
	{ 0x30d4, 0x6080 },
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x2802 },
	{ 0xa802, 0x0008 },

	/* Errata item 1 */
	{ 0x3e14, 0xff39 },

	/* Errata item 2 */
	{ MT9M114_RESET_REGISTER,			0x8234 },

	/* CCM */
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x4892 },
	{ 0xc892, 0x0267 }, { 0xc894, 0xff1a }, { 0xc896, 0xffb3 },
	{ 0xc898, 0xff80 }, { 0xc89a, 0x0166 }, { 0xc89c, 0x0003 },
	{ 0xc89e, 0xff9a }, { 0xc8a0, 0xfeb4 }, { 0xc8a2, 0x024d },
	{ 0xc8a4, 0x01bf }, { 0xc8a6, 0xff01 }, { 0xc8a8, 0xfff3 },
	{ 0xc8aa, 0xff75 }, { 0xc8ac, 0x0198 }, { 0xc8ae, 0xfffd },
	{ 0xc8b0, 0xff9a }, { 0xc8b2, 0xfee7 }, { 0xc8b4, 0x02a8 },
	{ 0xc8b6, 0x01d9 }, { 0xc8b8, 0xff26 }, { 0xc8ba, 0xfff3 },
	{ 0xc8bc, 0xffb3 }, { 0xc8be, 0x0132 }, { 0xc8c0, 0xffe8 },
	{ 0xc8c2, 0xffda }, { 0xc8c4, 0xfecd }, { 0xc8c6, 0x02c2 },
	{ 0xc8c8, 0x0075 }, { 0xc8ca, 0x011c }, { 0xc8cc, 0x009a },
	{ 0xc8ce, 0x0105 }, { 0xc8d0, 0x00a4 }, { 0xc8d2, 0x00ac },
	{ 0xc8d4, 0x0a8c }, { 0xc8d6, 0x0f0a }, { 0xc8d8, 0x1964 },

	/* AWB */
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x4914 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND,	0x04ff },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YEND,	0x02cf },
	{ MT9M114_CAM_AWB_AWB_XSHIFT_PRE_ADJ,		0x0033 },
	{ MT9M114_CAM_AWB_AWB_YSHIFT_PRE_ADJ,		0x0040 },
	{ MT9M114_CAM_AWB_AWB_XSCALE,			0x03, REG_U8 },
	{ MT9M114_CAM_AWB_AWB_YSCALE,			0x02, REG_U8 },
	{ MT9M114_CAM_AWB_AWB_YSHIFT_PRE_ADJ,		0x003c },
	{ 0xc8f4, 0x0000 },
	{ 0xc8f6, 0x0000 },
	{ 0xc8f8, 0x0000 },
	{ 0xc8fa, 0xe724 },
	{ 0xc8fc, 0x1583 },
	{ 0xc8fe, 0x2045 },
	{ 0xc900, 0x03ff },
	{ 0xc902, 0x007c },
	{ 0xc90c, 0x80, REG_U8 },
	{ 0xc90d, 0x80, REG_U8 },
	{ 0xc90e, 0x80, REG_U8 },
	{ 0xc90f, 0x88, REG_U8 },
	{ 0xc910, 0x80, REG_U8 },
	{ 0xc911, 0x80, REG_U8 },

	/* CPIPE Preference */
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x4926 },
	{ 0xc926, 0x0020 },
	{ 0xc928, 0x009a },
	{ 0xc946, 0x0070 },
	{ 0xc948, 0x00f3 },
	{ 0xc952, 0x0020 },
	{ 0xc954, 0x009a },
	{ 0xc92a, 0x80, REG_U8 },
	{ 0xc92b, 0x4b, REG_U8 },
	{ 0xc92c, 0x00, REG_U8 },
	{ 0xc92d, 0xff, REG_U8 },
	{ 0xc92e, 0x3c, REG_U8 },
	{ 0xc92f, 0x02, REG_U8 },
	{ 0xc930, 0x06, REG_U8 },
	{ 0xc931, 0x64, REG_U8 },
	{ 0xc932, 0x01, REG_U8 },
	{ 0xc933, 0x0c, REG_U8 },
	{ 0xc934, 0x3c, REG_U8 },
	{ 0xc935, 0x3c, REG_U8 },
	{ 0xc936, 0x3c, REG_U8 },
	{ 0xc937, 0x0f, REG_U8 },
	{ 0xc938, 0x64, REG_U8 },
	{ 0xc939, 0x64, REG_U8 },
	{ 0xc93a, 0x64, REG_U8 },
	{ 0xc93b, 0x32, REG_U8 },
	{ 0xc93c, 0x0020 },
	{ 0xc93e, 0x009a },
	{ 0xc940, 0x00dc },
	{ 0xc942, 0x38, REG_U8 },
	{ 0xc943, 0x30, REG_U8 },
	{ 0xc944, 0x50, REG_U8 },
	{ 0xc945, 0x19, REG_U8 },
	{ 0xc94a, 0x0230 },
	{ 0xc94c, 0x0010 },
	{ 0xc94e, 0x01cd },
	{ 0xc950, 0x05, REG_U8 },
	{ 0xc951, 0x40, REG_U8 },
	{ 0xc87b, 0x1b, REG_U8 },
	{ MT9M114_CAM_AET_AEMODE, 0x0e },
	{ 0xc890, 0x0080 },
	{ 0xc886, 0x0100 },
	{ 0xc87c, 0x005a },
	{ 0xb42a, 0x05, REG_U8 },
	{ 0xa80a, 0x20, REG_U8 },

	/* Speed up AE/AWB */
	{ MT9M114_LOGICAL_ADDRESS_ACCESS, 0x2802 },
	{ 0xa802, 0x0008 },
	{ 0xc908, 0x01, REG_U8 },
	{ 0xc879, 0x01, REG_U8 },
	{ 0xc909, 0x02, REG_U8 },
	{ 0xa80a, 0x18, REG_U8 },
	{ 0xa80b, 0x18, REG_U8 },
	{ 0xac16, 0x18, REG_U8 },
	{ MT9M114_CAM_AET_AEMODE, 0x0e, REG_U8 },

	/* For continuous clock mode, use 0x783e (the default) */
	{ 0x3c40, 0x783a },

	/* Enable LED */
	{ MT9M114_PAD_CONTROL,				0x0fd9 },
	{ MT9M114_FLASH,				0x0708 },
};

static const struct mt9m114_reg mt9m114_regs_qsif[] = {
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x1000 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_START,		0x0030 },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_START,		0x0004 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_END,		0x039f },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_END,		0x050b },
	{ MT9M114_CAM_SENSOR_CFG_PIXCLK,		0x02dc6c00, REG_U32 },
	{ MT9M114_CAM_SENSOR_CFG_ROW_SPEED,		0x0001 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN,	0x00db },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX,	0x05bd },
	{ MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES,	0x03e8 },
	{ MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK,	0x0640 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_CORRECTION,	0x0060 },
	{ MT9M114_CAM_SENSOR_CFG_CPIPE_LAST_ROW,	0x036b },
	{ MT9M114_CAM_SENSOR_CFG_REG_0_DATA,		0x0020 },
	{ MT9M114_CAM_SENSOR_CONTROL_READ_MODE,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_XOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_YOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_WIDTH,		0x0500 },
	{ MT9M114_CAM_CROP_WINDOW_HEIGHT,		0x0368 },
	{ MT9M114_CAM_CROP_CROPMODE,			0x03, REG_U8 },
	{ MT9M114_CAM_OUTPUT_WIDTH,			0x00b0 },
	{ MT9M114_CAM_OUTPUT_HEIGHT,			0x0078 },
	{ MT9M114_CAM_AET_AEMODE,			0x00, REG_U8 },
	{ MT9M114_CAM_AET_MAX_FRAME_RATE,		0x1e00 },
	{ MT9M114_CAM_AET_MIN_FRAME_RATE,		0x0f00 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND,	0x00af },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YEND,	0x0077 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XEND,	0x0022 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YEND,	0x0017 },
};

static const struct mt9m114_reg mt9m114_regs_qcif[] = {
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x1000 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_START,		0x0030 },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_START,		0x0070 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_END,		0x039d },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_END,		0x049d },
	{ MT9M114_CAM_SENSOR_CFG_PIXCLK,		0x02dc6c00, REG_U32 },
	{ MT9M114_CAM_SENSOR_CFG_ROW_SPEED,		0x0001 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN,	0x01c3 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX,	0x03f7 },
	{ MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES,	0x0500 },
	{ MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK,	0x04e2 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_CORRECTION,	0x00e0 },
	{ MT9M114_CAM_SENSOR_CFG_CPIPE_LAST_ROW,	0x01b3 },
	{ MT9M114_CAM_SENSOR_CFG_REG_0_DATA,		0x0020 },
	{ MT9M114_CAM_SENSOR_CONTROL_READ_MODE,		0x0330 },
	{ MT9M114_CAM_CROP_WINDOW_XOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_YOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_WIDTH,		0x0210 },
	{ MT9M114_CAM_CROP_WINDOW_HEIGHT,		0x01b0 },
	{ MT9M114_CAM_CROP_CROPMODE,			0x03, REG_U8 },
	{ MT9M114_CAM_OUTPUT_WIDTH,			0x00b0 },
	{ MT9M114_CAM_OUTPUT_HEIGHT,			0x0090 },
	{ MT9M114_CAM_AET_AEMODE,			0x00, REG_U8 },
	{ MT9M114_CAM_AET_MAX_FRAME_RATE,		0x1e00 },
	{ MT9M114_CAM_AET_MIN_FRAME_RATE,		0x0f00 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND,	0x00af },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YEND,	0x008f },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XEND,	0x0022 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YEND,	0x001b },
};

static const struct mt9m114_reg mt9m114_regs_qvga[] = {
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x1000 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_START,		0x0000 },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_START,		0x0000 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_END,		0x03cd },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_END,		0x050d },
	{ MT9M114_CAM_SENSOR_CFG_PIXCLK,		0x02dc6c00, REG_U32 },
	{ MT9M114_CAM_SENSOR_CFG_ROW_SPEED,		0x0001 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN,	0x01c3 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX,	0x03f7 },
	{ MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES,	0x0500 },
	{ MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK,	0x04e2 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_CORRECTION,	0x00e0 },
	{ MT9M114_CAM_SENSOR_CFG_CPIPE_LAST_ROW,	0x01e3 },
	{ MT9M114_CAM_SENSOR_CFG_REG_0_DATA,		0x0020 },
	{ MT9M114_CAM_SENSOR_CONTROL_READ_MODE,		0x0330 },
	{ MT9M114_CAM_CROP_WINDOW_XOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_YOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_WIDTH,		0x0280 },
	{ MT9M114_CAM_CROP_WINDOW_HEIGHT,		0x01e0 },
	{ MT9M114_CAM_CROP_CROPMODE,			0x03, REG_U8 },
	{ MT9M114_CAM_OUTPUT_WIDTH,			0x0140 },
	{ MT9M114_CAM_OUTPUT_HEIGHT,			0x00f0 },
	{ MT9M114_CAM_AET_AEMODE,			0x00, REG_U8 },
	{ MT9M114_CAM_AET_MAX_FRAME_RATE,		0x1e00 },
	{ MT9M114_CAM_AET_MIN_FRAME_RATE,		0x0f00 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND,	0x013f },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YEND,	0x00ef },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XEND,	0x003f },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YEND,	0x002f },
};

static const struct mt9m114_reg mt9m114_regs_sif[] = {
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x1000 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_START,		0x0030 },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_START,		0x0004 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_END,		0x039f },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_END,		0x050b },
	{ MT9M114_CAM_SENSOR_CFG_PIXCLK,		0x02dc6c00, REG_U32 },
	{ MT9M114_CAM_SENSOR_CFG_ROW_SPEED,		0x0001 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN,	0x00db },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX,	0x05bd },
	{ MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES,	0x03e8 },
	{ MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK,	0x0640 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_CORRECTION,	0x0060 },
	{ MT9M114_CAM_SENSOR_CFG_CPIPE_LAST_ROW,	0x036b },
	{ MT9M114_CAM_SENSOR_CFG_REG_0_DATA,		0x0020 },
	{ MT9M114_CAM_SENSOR_CONTROL_READ_MODE,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_XOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_YOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_WIDTH,		0x0500 },
	{ MT9M114_CAM_CROP_WINDOW_HEIGHT,		0x0368 },
	{ MT9M114_CAM_CROP_CROPMODE,			0x03, REG_U8 },
	{ MT9M114_CAM_OUTPUT_WIDTH,			0x0160 },
	{ MT9M114_CAM_OUTPUT_HEIGHT,			0x00f0 },
	{ MT9M114_CAM_AET_AEMODE,			0x00, REG_U8 },
	{ MT9M114_CAM_AET_MAX_FRAME_RATE,		0x1e00 },
	{ MT9M114_CAM_AET_MIN_FRAME_RATE,		0x0f00 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND,	0x015f },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YEND,	0x00ef },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XEND,	0x0045 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YEND,	0x002f },
};

static const struct mt9m114_reg mt9m114_regs_cif[] = {
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x1000 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_START,		0x0030 },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_START,		0x0070 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_END,		0x039d },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_END,		0x049d },
	{ MT9M114_CAM_SENSOR_CFG_PIXCLK,		0x02dc6c00, REG_U32 },
	{ MT9M114_CAM_SENSOR_CFG_ROW_SPEED,		0x0001 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN,	0x01c3 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX,	0x03f7 },
	{ MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES,	0x0500 },
	{ MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK,	0x04e2 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_CORRECTION,	0x00e0 },
	{ MT9M114_CAM_SENSOR_CFG_CPIPE_LAST_ROW,	0x01b3 },
	{ MT9M114_CAM_SENSOR_CFG_REG_0_DATA,		0x0020 },
	{ MT9M114_CAM_SENSOR_CONTROL_READ_MODE,		0x0330 },
	{ MT9M114_CAM_CROP_WINDOW_XOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_YOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_WIDTH,		0x0210 },
	{ MT9M114_CAM_CROP_WINDOW_HEIGHT,		0x01b0 },
	{ MT9M114_CAM_CROP_CROPMODE,			0x03, REG_U8 },
	{ MT9M114_CAM_OUTPUT_WIDTH,			0x0160 },
	{ MT9M114_CAM_OUTPUT_HEIGHT,			0x0120 },
	{ MT9M114_CAM_AET_AEMODE,			0x00, REG_U8 },
	{ MT9M114_CAM_AET_MAX_FRAME_RATE,		0x1e00 },
	{ MT9M114_CAM_AET_MIN_FRAME_RATE,		0x0f00 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND,	0x015f },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YEND,	0x011f },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XEND,	0x0045 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YEND,	0x0038 },
};

static const struct mt9m114_reg mt9m114_regs_vga[] = {
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x1000 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_START,		0x0000 },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_START,		0x0000 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_END,		0x03cd },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_END,		0x050d },
	{ MT9M114_CAM_SENSOR_CFG_PIXCLK,		0x02dc6c00, REG_U32 },
	{ MT9M114_CAM_SENSOR_CFG_ROW_SPEED,		0x0001 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN,	0x01c3 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX,	0x03f7 },
	{ MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES,	0x0500 },
	{ MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK,	0x04e2 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_CORRECTION,	0x00e0 },
	{ MT9M114_CAM_SENSOR_CFG_CPIPE_LAST_ROW,	0x01e3 },
	{ MT9M114_CAM_SENSOR_CFG_REG_0_DATA,		0x0020 },
	{ MT9M114_CAM_SENSOR_CONTROL_READ_MODE,		0x0330 },
	{ MT9M114_CAM_CROP_WINDOW_XOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_YOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_WIDTH,		0x0280 },
	{ MT9M114_CAM_CROP_WINDOW_HEIGHT,		0x01e0 },
	{ MT9M114_CAM_CROP_CROPMODE,			0x03, REG_U8 },
	{ MT9M114_CAM_OUTPUT_WIDTH,			0x0280 },
	{ MT9M114_CAM_OUTPUT_HEIGHT,			0x01e0 },
	{ MT9M114_CAM_AET_AEMODE,			0x00, REG_U8 },
	{ MT9M114_CAM_AET_MAX_FRAME_RATE,		0x1e00 },
	{ MT9M114_CAM_AET_MIN_FRAME_RATE,		0x0f00 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND,	0x027f },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YEND,	0x01df },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XEND,	0x007f },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YEND,	0x005f },
};

static const struct mt9m114_reg mt9m114_regs_720p[] = {
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x1000 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_START,		0x007c },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_START,		0x0004 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_END,		0x0353 },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_END,		0x050b },
	{ MT9M114_CAM_SENSOR_CFG_PIXCLK,		0x02dc6c00, REG_U32 },
	{ MT9M114_CAM_SENSOR_CFG_ROW_SPEED,		0x0001 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN,	0x00db },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX,	0x05bd },
	{ MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES,	0x03e8 },
	{ MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK,	0x0640 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_CORRECTION,	0x0060 },
	{ MT9M114_CAM_SENSOR_CFG_CPIPE_LAST_ROW,	0x02d3 },
	{ MT9M114_CAM_SENSOR_CFG_REG_0_DATA,		0x0020 },
	{ MT9M114_CAM_SENSOR_CONTROL_READ_MODE,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_XOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_YOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_WIDTH,		0x0500 },
	{ MT9M114_CAM_CROP_WINDOW_HEIGHT,		0x02d0 },
	{ MT9M114_CAM_CROP_CROPMODE,			0x03, REG_U8 },
	{ MT9M114_CAM_OUTPUT_WIDTH,			0x0500 },
	{ MT9M114_CAM_OUTPUT_HEIGHT,			0x02d0 },
	{ MT9M114_CAM_AET_AEMODE,			0x00, REG_U8 },
	{ MT9M114_CAM_AET_MAX_FRAME_RATE,		0x1e00 },
	{ MT9M114_CAM_AET_MIN_FRAME_RATE,		0x0f00 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND,	0x04ff },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YEND,	0x02cf },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XEND,	0x00ff },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YEND,	0x008f },
};

/* Black level correction firmware patch */
static const struct mt9m114_reg mt9m114_firmware_patch[] = {
	{ 0xd000, 0x70cf }, { 0xd002, 0xffff }, { 0xd004, 0xc5d4 },
	{ 0xd006, 0x903a }, { 0xd008, 0x2144 }, { 0xd00a, 0x0c00 },
	{ 0xd00c, 0x2186 }, { 0xd00e, 0x0ff3 }, { 0xd010, 0xb844 },
	{ 0xd012, 0xb948 }, { 0xd014, 0xe082 }, { 0xd016, 0x20cc },
	{ 0xd018, 0x80e2 }, { 0xd01a, 0x21cc }, { 0xd01c, 0x80a2 },
	{ 0xd01e, 0x21cc }, { 0xd020, 0x80e2 }, { 0xd022, 0xf404 },
	{ 0xd024, 0xd801 }, { 0xd026, 0xf003 }, { 0xd028, 0xd800 },
	{ 0xd02a, 0x7ee0 }, { 0xd02c, 0xc0f1 }, { 0xd02e, 0x08ba },
	{ 0xd030, 0x0600 }, { 0xd032, 0xc1a1 }, { 0xd034, 0x76cf },
	{ 0xd036, 0xffff }, { 0xd038, 0xc130 }, { 0xd03a, 0x6e04 },
	{ 0xd03c, 0xc040 }, { 0xd03e, 0x71cf }, { 0xd040, 0xffff },
	{ 0xd042, 0xc790 }, { 0xd044, 0x8103 }, { 0xd046, 0x77cf },
	{ 0xd048, 0xffff }, { 0xd04a, 0xc7c0 }, { 0xd04c, 0xe001 },
	{ 0xd04e, 0xa103 }, { 0xd050, 0xd800 }, { 0xd052, 0x0c6a },
	{ 0xd054, 0x04e0 }, { 0xd056, 0xb89e }, { 0xd058, 0x7508 },
	{ 0xd05a, 0x8e1c }, { 0xd05c, 0x0809 }, { 0xd05e, 0x0191 },
	{ 0xd060, 0xd801 }, { 0xd062, 0xae1d }, { 0xd064, 0xe580 },
	{ 0xd066, 0x20ca }, { 0xd068, 0x0022 }, { 0xd06a, 0x20cf },
	{ 0xd06c, 0x0522 }, { 0xd06e, 0x0c5c }, { 0xd070, 0x04e2 },
	{ 0xd072, 0x21ca }, { 0xd074, 0x0062 }, { 0xd076, 0xe580 },
	{ 0xd078, 0xd901 }, { 0xd07a, 0x79c0 }, { 0xd07c, 0xd800 },
	{ 0xd07e, 0x0be6 }, { 0xd080, 0x04e0 }, { 0xd082, 0xb89e },
	{ 0xd084, 0x70cf }, { 0xd086, 0xffff }, { 0xd088, 0xc8d4 },
	{ 0xd08a, 0x9002 }, { 0xd08c, 0x0857 }, { 0xd08e, 0x025e },
	{ 0xd090, 0xffdc }, { 0xd092, 0xe080 }, { 0xd094, 0x25cc },
	{ 0xd096, 0x9022 }, { 0xd098, 0xf225 }, { 0xd09a, 0x1700 },
	{ 0xd09c, 0x108a }, { 0xd09e, 0x73cf }, { 0xd0a0, 0xff00 },
	{ 0xd0a2, 0x3174 }, { 0xd0a4, 0x9307 }, { 0xd0a6, 0x2a04 },
	{ 0xd0a8, 0x103e }, { 0xd0aa, 0x9328 }, { 0xd0ac, 0x2942 },
	{ 0xd0ae, 0x7140 }, { 0xd0b0, 0x2a04 }, { 0xd0b2, 0x107e },
	{ 0xd0b4, 0x9349 }, { 0xd0b6, 0x2942 }, { 0xd0b8, 0x7141 },
	{ 0xd0ba, 0x2a04 }, { 0xd0bc, 0x10be }, { 0xd0be, 0x934a },
	{ 0xd0c0, 0x2942 }, { 0xd0c2, 0x714b }, { 0xd0c4, 0x2a04 },
	{ 0xd0c6, 0x10be }, { 0xd0c8, 0x130c }, { 0xd0ca, 0x010a },
	{ 0xd0cc, 0x2942 }, { 0xd0ce, 0x7142 }, { 0xd0d0, 0x2250 },
	{ 0xd0d2, 0x13ca }, { 0xd0d4, 0x1b0c }, { 0xd0d6, 0x0284 },
	{ 0xd0d8, 0xb307 }, { 0xd0da, 0xb328 }, { 0xd0dc, 0x1b12 },
	{ 0xd0de, 0x02c4 }, { 0xd0e0, 0xb34a }, { 0xd0e2, 0xed88 },
	{ 0xd0e4, 0x71cf }, { 0xd0e6, 0xff00 }, { 0xd0e8, 0x3174 },
	{ 0xd0ea, 0x9106 }, { 0xd0ec, 0xb88f }, { 0xd0ee, 0xb106 },
	{ 0xd0f0, 0x210a }, { 0xd0f2, 0x8340 }, { 0xd0f4, 0xc000 },
	{ 0xd0f6, 0x21ca }, { 0xd0f8, 0x0062 }, { 0xd0fa, 0x20f0 },
	{ 0xd0fc, 0x0040 }, { 0xd0fe, 0x0b02 }, { 0xd100, 0x0320 },
	{ 0xd102, 0xd901 }, { 0xd104, 0x07f1 }, { 0xd106, 0x05e0 },
	{ 0xd108, 0xc0a1 }, { 0xd10a, 0x78e0 }, { 0xd10c, 0xc0f1 },
	{ 0xd10e, 0x71cf }, { 0xd110, 0xffff }, { 0xd112, 0xc7c0 },
	{ 0xd114, 0xd840 }, { 0xd116, 0xa900 }, { 0xd118, 0x71cf },
	{ 0xd11a, 0xffff }, { 0xd11c, 0xd02c }, { 0xd11e, 0xd81e },
	{ 0xd120, 0x0a5a }, { 0xd122, 0x04e0 }, { 0xd124, 0xda00 },
	{ 0xd126, 0xd800 }, { 0xd128, 0xc0d1 }, { 0xd12a, 0x7ee0 },
};

/* Supported resolutions */
enum {
	MT9M114_QSIF = 0,
	MT9M114_QCIF,
	MT9M114_QVGA,
	MT9M114_SIF,
	MT9M114_CIF,
	MT9M114_VGA,
	MT9M114_720P,
};

struct mt9m114_resolution {
	unsigned int		width;
	unsigned int		height;
	const struct mt9m114_reg *reg_array;
	unsigned int		reg_array_size;
};

static struct mt9m114_resolution mt9m114_resolutions[] = {
	[MT9M114_QSIF] = {
		.width		= 176,
		.height		= 120,
		.reg_array	= mt9m114_regs_qsif,
		.reg_array_size	= ARRAY_SIZE(mt9m114_regs_qsif),
	},
	[MT9M114_QCIF] = {
		.width		= 176,
		.height		= 144,
		.reg_array	= mt9m114_regs_qcif,
		.reg_array_size	= ARRAY_SIZE(mt9m114_regs_qcif),
	},
	[MT9M114_QVGA] = {
		.width		= 320,
		.height		= 240,
		.reg_array	= mt9m114_regs_qvga,
		.reg_array_size	= ARRAY_SIZE(mt9m114_regs_qvga),
	},
	[MT9M114_SIF] = {
		.width		= 352,
		.height		= 240,
		.reg_array	= mt9m114_regs_sif,
		.reg_array_size	= ARRAY_SIZE(mt9m114_regs_sif),
	},
	[MT9M114_CIF] = {
		.width		= 352,
		.height		= 288,
		.reg_array	= mt9m114_regs_cif,
		.reg_array_size	= ARRAY_SIZE(mt9m114_regs_cif),
	},
	[MT9M114_VGA] = {
		.width		= 640,
		.height		= 480,
		.reg_array	= mt9m114_regs_vga,
		.reg_array_size	= ARRAY_SIZE(mt9m114_regs_vga),
	},
	[MT9M114_720P] = {
		.width		= 1280,
		.height		= 720,
		.reg_array	= mt9m114_regs_720p,
		.reg_array_size	= ARRAY_SIZE(mt9m114_regs_720p),
	},
};

static enum v4l2_mbus_pixelcode mt9m114_codes[] = {
	V4L2_MBUS_FMT_YUYV8_2X8,
};

static const struct v4l2_queryctrl mt9m114_controls[] = {
	{
		.id		= V4L2_CID_VFLIP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Flip Vertically",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
	},
	{
		.id		= V4L2_CID_HFLIP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Flip Horizontally",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
	},
};

static int mt9m114_reg_read8(struct i2c_client *client, u16 reg, u8 *val)
{
	int ret;
	struct i2c_msg msg[] = {
		{
			.addr	= client->addr,
			.flags	= 0,
			.len	= 2,
			.buf	= (u8 *)&reg,
		},
		{
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= 1,
			.buf	= val,
		},
	};

	reg = swab16(reg);

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		dev_err(&client->dev, "Failed reading register 0x%04x!\n", reg);
		return ret;
	}

	return 0;
}

static int mt9m114_reg_write8(struct i2c_client *client, u16 reg, u8 val)
{
	struct i2c_msg msg;
	struct {
		u16 reg;
		u8 val;
	} __packed buf;
	int ret;

	buf.reg = swab16(reg);
	buf.val = val;

	msg.addr	= client->addr;
	msg.flags	= 0;
	msg.len		= 3;
	msg.buf		= (u8 *)&buf;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		dev_err(&client->dev, "Failed writing register 0x%04x!\n", reg);
		return ret;
	}

	return 0;
}

static int mt9m114_reg_read16(struct i2c_client *client, u16 reg, u16 *val)
{
	int ret;
	u16 rval;
	struct i2c_msg msg[] = {
		{
			.addr	= client->addr,
			.flags	= 0,
			.len	= 2,
			.buf	= (u8 *)&reg,
		},
		{
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= 2,
			.buf	= (u8 *)&rval,
		},
	};

	reg = swab16(reg);

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		dev_err(&client->dev, "Failed reading register 0x%04x!\n", reg);
		return ret;
	}

	*val = swab16(rval);

	return 0;
}

static int mt9m114_reg_write16(struct i2c_client *client, u16 reg, u16 val)
{
	struct i2c_msg msg;
	struct {
		u16 reg;
		u16 val;
	} __packed buf;
	int ret;

	buf.reg = swab16(reg);
	buf.val = swab16(val);

	msg.addr	= client->addr;
	msg.flags	= 0;
	msg.len		= 4;
	msg.buf		= (u8 *)&buf;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		dev_err(&client->dev, "Failed writing register 0x%04x!\n", reg);
		return ret;
	}

	return 0;
}

static int mt9m114_reg_write32(struct i2c_client *client, u16 reg, u32 val)
{
	struct i2c_msg msg;
	struct {
		u16 reg;
		u32 val;
	} __packed buf;
	int ret;

	buf.reg = swab16(reg);
	buf.val = swab32(val);

	msg.addr	= client->addr;
	msg.flags	= 0;
	msg.len		= 6;
	msg.buf		= (u8 *)&buf;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		dev_err(&client->dev, "Failed writing register 0x%04x!\n", reg);
		return ret;
	}

	return 0;
}

static int mt9m114_reg_poll16(struct i2c_client *client, u16 reg,
			      u16 mask, u16 val, int delay, int timeout)
{
	int err;

	while (timeout) {
		u16 currval;

		READ16(reg, &currval);

		if ((currval & mask) == val)
			return 0;

		msleep(delay);
		timeout--;
	}

	dev_err(&client->dev, "Failed polling register 0x%04x for 0x%04x\n",
		reg, val);

	return -ETIMEDOUT;

err:
	return err;
}

/* Read a register, alter its bits, write it back */
static int mt9m114_reg_rmw16(struct i2c_client *client, u16 reg,
			     u16 set, u16 unset)
{
	u16 val;
	int err;

	READ16(reg, &val);

	val |= set;
	val &= ~unset;

	WRITE16(reg, val);

	return 0;

err:
	return err;
}

static int mt9m114_reg_write_array(struct i2c_client *client,
				   const struct mt9m114_reg *regarray,
				   int regarraylen)
{
	int i;
	int err;

	for (i = 0; i < regarraylen; i++) {
		switch (regarray[i].width) {
		case REG_U16:
			WRITE16(regarray[i].reg, (u16)regarray[i].val);
			break;
		case REG_U8:
			WRITE8(regarray[i].reg, (u8)regarray[i].val);
			break;

		case REG_U32:
			WRITE32(regarray[i].reg, (u32)regarray[i].val);
			break;
		default:
			BUG_ON(1);
		}
	}

	return 0;

err:
	return err;
}

static int mt9m114_get_patch_id(struct i2c_client *client, u16 *patch_id)
{
	u8 patch_index;
	int err;

	/* Check that the patch has been applied. */
	READ8(MT9M114_PATCHLDR_NUM_PATCHES, &patch_index);

	if (patch_index == 0) {
		*patch_id = 0x0000;
		return 0;
	}

	if (patch_index > 8)
		return -EINVAL;

	READ16(MT9M114_PATCHLDR_PATCH_ID_0 + 2*(patch_index-1), patch_id);

err:
	return err;
}

static int mt9m114_apply_firmware_patch(struct i2c_client *client)
{
	u16 patch_id;
	u8 status;
	int err;

	WRITE16(MT9M114_ACCESS_CTL_STAT, 0x0001);
	WRITE16(MT9M114_PHYSICAL_ADDRESS_ACCESS, 0x5000);

	WRITEARRAY(mt9m114_firmware_patch);

	WRITE16(MT9M114_LOGICAL_ADDRESS_ACCESS, 0x0000);
	WRITE16(MT9M114_PATCHLDR_LOADER_ADDRESS, 0x010c);
	WRITE16(MT9M114_PATCHLDR_PATCH_ID, 0x0202);
	WRITE32(MT9M114_PATCHLDR_FIRMWARE_ID, 0x41030202);
	WRITE16(MT9M114_COMMAND_REGISTER, 0xfff0);

	POLL16(MT9M114_COMMAND_REGISTER, MT9M114_COMMAND_REGISTER_APPLY_PATCH,
	       0x0000);

	WRITE16(MT9M114_COMMAND_REGISTER, 0xfff1);

	POLL16(MT9M114_COMMAND_REGISTER, MT9M114_COMMAND_REGISTER_APPLY_PATCH,
	       0x0000);

	READ8(MT9M114_PATCHLDR_APPLY_STATUS, &status);
	if (status != 0x00) {
		err = -EFAULT;
		goto err;
	}

	/* Check that the patch has been applied. */
	err = mt9m114_get_patch_id(client, &patch_id);
	if (patch_id != 0x0202) {
		dev_err(&client->dev, "Failed patching FW, "
			"unexpected patch ID 0x%04x\n", patch_id);
		return -EFAULT;
	}

	dev_info(&client->dev, "Successfully patched FW, ID = 0x%04x\n",
		 patch_id);

	return 0;

err:
	dev_err(&client->dev, "Failed patching FW\n");

	return err;
}

static int mt9m114_set_state(struct i2c_client *client,
			     u8 next_state, u8 final_state)
{
	u8 state;
	u8 status;
	int err;

	/* Set the next desired state. */
	WRITE16(MT9M114_LOGICAL_ADDRESS_ACCESS, MT9M114_SYSMGR_NEXT_STATE);
	WRITE8(MT9M114_SYSMGR_NEXT_STATE, next_state);

	/* Make sure FW is ready to set state. */
	POLL16(MT9M114_COMMAND_REGISTER,
	       MT9M114_COMMAND_REGISTER_SET_STATE,
	       0x0000);

	/* Start state transition. */
	WRITE16(MT9M114_COMMAND_REGISTER, (MT9M114_COMMAND_REGISTER_OK |
					   MT9M114_COMMAND_REGISTER_SET_STATE));

	/* Wait for the state transition to complete. */
	POLL16(MT9M114_COMMAND_REGISTER,
	       MT9M114_COMMAND_REGISTER_SET_STATE,
	       0x0000);

	/* Check that the last set state command succeeded. */
	READ8(MT9M114_SYSMGR_CMD_STATUS, &status);
	if (status != MT9M114_SET_STATE_RESULT_ENOERR) {
		dev_err(&client->dev, "Set state failure, result = 0x%02x\n",
			status);
		return -EFAULT;
	}

	/* Make sure we are now at the desired state. */
	READ8(MT9M114_SYSMGR_CURRENT_STATE, &state);
	if (state != final_state) {
		dev_err(&client->dev, "Failed setting state to 0x%02x\n",
			final_state);
		return -EFAULT;
	}

err:
	return err;
}

static int mt9m114_change_config(struct i2c_client *client)
{
	u16 cmd;
	u8 old_state;
	u8 state;
	u8 status;
	int err;

	/* Save old state so we can check it when change config is done. */
	READ8(MT9M114_SYSMGR_CURRENT_STATE, &old_state);

	/* Set state to change-config. */
	WRITE8(MT9M114_SYSMGR_NEXT_STATE,
	       MT9M114_SYS_STATE_ENTER_CONFIG_CHANGE);

	/* Make sure FW is ready to accept a new command. */
	POLL16(MT9M114_COMMAND_REGISTER,
	       MT9M114_COMMAND_REGISTER_SET_STATE,
	       0x0000);

	/* Issue set-state command. */
	WRITE16(MT9M114_COMMAND_REGISTER, (MT9M114_COMMAND_REGISTER_OK |
					   MT9M114_COMMAND_REGISTER_SET_STATE));

	/* Wait for the set-state command to complete. */
	POLL16(MT9M114_COMMAND_REGISTER,
	       MT9M114_COMMAND_REGISTER_SET_STATE,
	       0x0000);

	/* Check that the command completed successfully. */
	READ16(MT9M114_COMMAND_REGISTER, &cmd);
	if (!(cmd & MT9M114_COMMAND_REGISTER_OK)) {
		dev_err(&client->dev, "Change-Config failed, "
			"cmd = 0x%04x\n", cmd);
		return -EINVAL;
	}

	/* Check that the last set state command succeeded. */
	READ8(MT9M114_SYSMGR_CMD_STATUS, &status);
	if (status != MT9M114_SET_STATE_RESULT_ENOERR) {
		dev_err(&client->dev, "Set state failure, result = 0x%02x\n",
			status);
		return -EFAULT;
	}

	/* Check that the old state has been restored. */
	READ8(MT9M114_SYSMGR_CURRENT_STATE, &state);
	if (state != old_state) {
		dev_err(&client->dev, "Failed restoring state 0x%02x\n",
			old_state);
		return -EFAULT;
	}

err:
	return err;
}

/* Start/Stop streaming from the device */
static int mt9m114_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m114_priv *priv = to_mt9m114(sd);
	int err;

	/* Program orientation register. */
	WRITE16(MT9M114_LOGICAL_ADDRESS_ACCESS, 0x4834);

	if (priv->flag_vflip)
		RMW16(MT9M114_CAM_SENSOR_CONTROL_READ_MODE, 0x0002, 0x0000);
	else
		RMW16(MT9M114_CAM_SENSOR_CONTROL_READ_MODE, 0x0000, 0x0002);

	if (priv->flag_hflip)
		RMW16(MT9M114_CAM_SENSOR_CONTROL_READ_MODE, 0x0001, 0x0000);
	else
		RMW16(MT9M114_CAM_SENSOR_CONTROL_READ_MODE, 0x0000, 0x0001);

	err = mt9m114_change_config(client);
	if (err < 0)
		return err;

	if (enable) {
		dev_dbg(&client->dev, "Enabling Streaming\n");
		err = mt9m114_set_state(client,
					MT9M114_SYS_STATE_START_STREAMING,
					MT9M114_SYS_STATE_STREAMING);
	} else {
		dev_dbg(&client->dev, "Disabling Streaming\n");
		err = mt9m114_set_state(client,
					MT9M114_SYS_STATE_ENTER_SUSPEND,
					MT9M114_SYS_STATE_SUSPENDED);
	}

	priv->current_enable = enable;

err:
	return err;
}

/* Alter bus settings on camera side */
static int mt9m114_set_bus_param(struct soc_camera_device *icd,
				 unsigned long flags)
{
	return 0;
}

/* Request bus settings on camera side */
static unsigned long mt9m114_query_bus_param(struct soc_camera_device *icd)
{
	struct soc_camera_link *icl = to_soc_camera_link(icd);

	unsigned long flags = SOCAM_PCLK_SAMPLE_RISING | SOCAM_MASTER |
		SOCAM_VSYNC_ACTIVE_HIGH | SOCAM_HSYNC_ACTIVE_HIGH |
		SOCAM_DATA_ACTIVE_HIGH | SOCAM_DATAWIDTH_8;

	return soc_camera_apply_sensor_flags(icl, flags);
}

/* Get status of additional camera capabilities */
static int mt9m114_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct mt9m114_priv *priv = to_mt9m114(sd);

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		ctrl->value = priv->flag_vflip;
		break;
	case V4L2_CID_HFLIP:
		ctrl->value = priv->flag_hflip;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/* Set status of additional camera capabilities */
static int mt9m114_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct mt9m114_priv *priv = to_mt9m114(sd);

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		priv->flag_vflip = ctrl->value;
		break;
	case V4L2_CID_HFLIP:
		priv->flag_hflip = ctrl->value;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/* Get chip identification */
static int mt9m114_g_chip_ident(struct v4l2_subdev *sd,
				struct v4l2_dbg_chip_ident *id)
{
	struct mt9m114_priv *priv = to_mt9m114(sd);

	id->ident = priv->ident;
	id->revision = priv->revision;

	return 0;
}

/* select nearest higher resolution for capture */
static void mt9m114_res_roundup(u32 *width, u32 *height)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mt9m114_resolutions); i++)
		if ((mt9m114_resolutions[i].width >= *width) &&
		    (mt9m114_resolutions[i].height >= *height)) {
			*width = mt9m114_resolutions[i].width;
			*height = mt9m114_resolutions[i].height;
			return;
		}

	/* If nearest higher resolution isn't found, default to the largest. */
	*width = mt9m114_resolutions[ARRAY_SIZE(mt9m114_resolutions)-1].width;
	*height = mt9m114_resolutions[ARRAY_SIZE(mt9m114_resolutions)-1].height;
}

/* Setup registers according to resolution */
static int mt9m114_set_res(struct i2c_client *client, u32 width, u32 height)
{
	int k;

	/* select register configuration for given resolution */
	for (k = 0; k < ARRAY_SIZE(mt9m114_resolutions); k++) {
		struct mt9m114_resolution *res = &mt9m114_resolutions[k];

		if ((width == res->width) && (height == res->height)) {
			dev_dbg(&client->dev, "Setting image size to %dx%d\n",
				res->width, res->height);
			return mt9m114_reg_write_array(client, res->reg_array,
						       res->reg_array_size);
		}
	}

	dev_err(&client->dev, "Failed to select resolution %dx%d!\n",
		width, height);

	WARN_ON(1);

	return -EINVAL;
}

/* set the format we will capture in */
static int mt9m114_s_fmt(struct v4l2_subdev *sd,
			 struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m114_priv *priv = to_mt9m114(sd);
	enum v4l2_colorspace cspace;
	enum v4l2_mbus_pixelcode code = mf->code;
	u16 patch_id;
	int err = 0;

	mt9m114_res_roundup(&mf->width, &mf->height);

	switch (code) {
	case V4L2_MBUS_FMT_YUYV8_2X8:
		cspace = V4L2_COLORSPACE_SRGB;
		break;
	default:
		return -EINVAL;
	}

	err = mt9m114_get_patch_id(client, &patch_id);
	if (err < 0)
		goto err;

	if (patch_id != 0x0202) {
		err = mt9m114_apply_firmware_patch(client);
		if (err < 0)
			goto err;
	}

	WRITEARRAY(mt9m114_defaults);

	err = mt9m114_set_res(client, mf->width, mf->height);
	if (err < 0)
		goto err;

	mf->code	= code;
	mf->colorspace	= cspace;

	memcpy(&priv->current_mf, mf, sizeof(struct v4l2_mbus_framefmt));

err:
	return err;
}

static int mt9m114_try_fmt(struct v4l2_subdev *sd,
			   struct v4l2_mbus_framefmt *mf)
{
	mt9m114_res_roundup(&mf->width, &mf->height);

	mf->field = V4L2_FIELD_NONE;
	mf->code = V4L2_MBUS_FMT_YUYV8_2X8;
	mf->colorspace = V4L2_COLORSPACE_SRGB;

	return 0;
}

static int mt9m114_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			    enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(mt9m114_codes))
		return -EINVAL;

	*code = mt9m114_codes[index];

	return 0;
}

static int mt9m114_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	a->bounds.left		= 0;
	a->bounds.top		= 0;
	a->bounds.width		=
		mt9m114_resolutions[ARRAY_SIZE(mt9m114_resolutions)-1].width;
	a->bounds.height	=
		mt9m114_resolutions[ARRAY_SIZE(mt9m114_resolutions)-1].height;
	a->defrect		= a->bounds;
	a->type			= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static int mt9m114_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	a->c.left		= 0;
	a->c.top		= 0;
	a->c.width		=
		mt9m114_resolutions[ARRAY_SIZE(mt9m114_resolutions)-1].width;
	a->c.height		=
		mt9m114_resolutions[ARRAY_SIZE(mt9m114_resolutions)-1].height;
	a->type			= V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

static int mt9m114_video_probe(struct soc_camera_device *icd,
			       struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct mt9m114_priv *priv = to_mt9m114(sd);
	int err;

	/*
	 * We must have a parent by now. And it cannot be a wrong one.
	 * So this entire test is completely redundant.
	 */
	if (!icd->dev.parent ||
	    to_soc_camera_host(icd->dev.parent)->nr != icd->iface) {
		dev_err(&client->dev, "Parent missing or invalid!\n");
		err = -ENODEV;
		goto err;
	}

	/*
	 * Check and show chip ID and revision.
	 */
	READ16(MT9M114_CHIP_ID, &priv->chip_id);
	READ16(MT9M114_CUSTOMER_REV, &priv->revision);

	if (priv->chip_id != 0x2481) {
		err = -ENODEV;
		goto err;
	}

	priv->ident = V4L2_IDENT_MT9M114;

	dev_info(&client->dev, "mt9m114 Chip ID 0x%04x, Revision 0x%04x\n",
		 priv->chip_id, priv->revision);

	return 0;

err:
	dev_err(&client->dev, "video_probe failed!\n");
	return err;
}

static int mt9m114_suspend(struct soc_camera_device *icd, pm_message_t state)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct mt9m114_priv *priv = to_mt9m114(sd);

	if (priv->current_enable) {
		int current_enable = priv->current_enable;

		mt9m114_s_stream(sd, 0);
		priv->current_enable = current_enable;
	}

	return 0;
}

static int mt9m114_resume(struct soc_camera_device *icd)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct mt9m114_priv *priv = to_mt9m114(sd);

	if (priv->current_enable) {
		mt9m114_s_fmt(sd, &priv->current_mf);
		mt9m114_s_stream(sd, priv->current_enable);
	}

	return 0;
}

static struct soc_camera_ops mt9m114_ops = {
	.suspend		= mt9m114_suspend,
	.resume			= mt9m114_resume,
	.set_bus_param		= mt9m114_set_bus_param,
	.query_bus_param	= mt9m114_query_bus_param,
	.controls		= mt9m114_controls,
	.num_controls		= ARRAY_SIZE(mt9m114_controls),
};

static struct v4l2_subdev_core_ops mt9m114_core_ops = {
	.g_ctrl			= mt9m114_g_ctrl,
	.s_ctrl			= mt9m114_s_ctrl,
	.g_chip_ident		= mt9m114_g_chip_ident,
};

static struct v4l2_subdev_video_ops mt9m114_video_ops = {
	.s_stream		= mt9m114_s_stream,
	.s_mbus_fmt		= mt9m114_s_fmt,
	.try_mbus_fmt		= mt9m114_try_fmt,
	.enum_mbus_fmt		= mt9m114_enum_fmt,
	.cropcap		= mt9m114_cropcap,
	.g_crop			= mt9m114_g_crop,
};

static struct v4l2_subdev_ops mt9m114_subdev_ops = {
	.core			= &mt9m114_core_ops,
	.video			= &mt9m114_video_ops,
};

/*
 * i2c_driver function
 */
static int mt9m114_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
	struct mt9m114_priv *priv;
	struct soc_camera_device *icd	= client->dev.platform_data;
	struct soc_camera_link *icl;
	int ret;

	if (!icd) {
		dev_err(&client->dev, "Missing soc-camera data!\n");
		return -EINVAL;
	}

	icl = to_soc_camera_link(icd);
	if (!icl) {
		dev_err(&client->dev, "Missing platform_data for driver\n");
		return -EINVAL;
	}

	priv = kzalloc(sizeof(struct mt9m114_priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&client->dev, "Failed to allocate private data!\n");
		return -ENOMEM;
	}

	v4l2_i2c_subdev_init(&priv->subdev, client, &mt9m114_subdev_ops);

	icd->ops = &mt9m114_ops;

	ret = mt9m114_video_probe(icd, client);
	if (ret < 0) {
		icd->ops = NULL;
		kfree(priv);
	}

	return ret;
}

static int mt9m114_remove(struct i2c_client *client)
{
	struct mt9m114_priv *priv = i2c_get_clientdata(client);

	kfree(priv);

	return 0;
}

static const struct i2c_device_id mt9m114_id[] = {
	{ "mt9m114", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mt9m114_id);

static struct i2c_driver mt9m114_i2c_driver = {
	.driver = {
		.name = "mt9m114",
	},
	.probe    = mt9m114_probe,
	.remove   = mt9m114_remove,
	.id_table = mt9m114_id,
};

static int __init mt9m114_module_init(void)
{
	return i2c_add_driver(&mt9m114_i2c_driver);
}

static void __exit mt9m114_module_exit(void)
{
	i2c_del_driver(&mt9m114_i2c_driver);
}

module_init(mt9m114_module_init);
module_exit(mt9m114_module_exit);

MODULE_DESCRIPTION("SoC Camera driver for Aptina MT9M114");
MODULE_AUTHOR("Andrew Chew <achew@nvidia.com>");
MODULE_LICENSE("GPL v2");
