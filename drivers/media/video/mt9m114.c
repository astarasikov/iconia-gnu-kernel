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

/* Supported resolutions */
enum {
	MT9M114_QSIF,
	MT9M114_QCIF,
	MT9M114_QVGA,
	MT9M114_SIF,
	MT9M114_CIF,
	MT9M114_VGA,
	MT9M114_720P,
};

struct mt9m114_resolution {
	unsigned int width;
	unsigned int height;
};

static struct mt9m114_resolution mt9m114_resolutions[] = {
	[MT9M114_QSIF] = {
		.width	= 176,
		.height	= 120,
	},
	[MT9M114_QCIF] = {
		.width	= 176,
		.height	= 144,
	},
	[MT9M114_QVGA] = {
		.width	= 320,
		.height	= 240,
	},
	[MT9M114_SIF] = {
		.width	= 352,
		.height	= 240,
	},
	[MT9M114_CIF] = {
		.width	= 352,
		.height	= 288,
	},
	[MT9M114_VGA] = {
		.width	= 640,
		.height	= 480,
	},
	[MT9M114_720P] = {
		.width	= 1280,
		.height	= 720,
	},
};

/* Misc. structures */
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

static const struct mt9m114_reg mt9m114_defaults[] = {
	/* Reset and clocks. */
	{ MT9M114_RESET_REGISTER,			0x0234 },
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x1000 },
	{ MT9M114_CAM_SYSCTL_PLL_ENABLE,		0x01, REG_U8 },
	{ MT9M114_CAM_SYSCTL_PLL_DIVIDER_M_N,		0x0120 },
	{ MT9M114_CAM_SYSCTL_PLL_DIVIDER_P,		0x0700 },
	{ MT9M114_CAM_PORT_OUTPUT_CONTROL,		0x8041 },
	{ MT9M114_CAM_PORT_MIPI_TIMING_T_HS_ZERO,	0x0F00 },
	{ MT9M114_CAM_PORT_MIPI_TIMING_T_HS_EXIT_HS_TRAIL, 0x0B07 },
	{ MT9M114_CAM_PORT_MIPI_TIMING_T_CLK_POST_CLK_PRE, 0x0D01 },
	{ MT9M114_CAM_PORT_MIPI_TIMING_T_CLK_TRAIL_CLK_ZERO, 0x071D },
	{ MT9M114_CAM_PORT_MIPI_TIMING_T_LPX,		0x0006 },
	{ MT9M114_CAM_PORT_MIPI_TIMING_INIT_TIMING,	0x0A0C },

	/* Sensor optimization */
	{ 0x316A, 0x8270 },
	{ 0x316C, 0x8270 },
	{ 0x3ED0, 0x2305 },
	{ 0x3ED2, 0x77CF },
	{ 0x316E, 0x8202 },
	{ 0x3180, 0x87FF },
	{ 0x30D4, 0x6080 },
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x2802 },
	{ 0xA802, 0x0008 },

	/* Errata item 1 */
	{ 0x3E14, 0xFF39 },

	/* Errata item 2 */
	{ MT9M114_RESET_REGISTER,			0x8234 },

	/* CCM */
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x4892 },
	{ 0xC892, 0x0267 }, { 0xC894, 0xFF1A }, { 0xC896, 0xFFB3 },
	{ 0xC898, 0xFF80 }, { 0xC89A, 0x0166 }, { 0xC89C, 0x0003 },
	{ 0xC89E, 0xFF9A }, { 0xC8A0, 0xFEB4 }, { 0xC8A2, 0x024D },
	{ 0xC8A4, 0x01BF }, { 0xC8A6, 0xFF01 }, { 0xC8A8, 0xFFF3 },
	{ 0xC8AA, 0xFF75 }, { 0xC8AC, 0x0198 }, { 0xC8AE, 0xFFFD },
	{ 0xC8B0, 0xFF9A }, { 0xC8B2, 0xFEE7 }, { 0xC8B4, 0x02A8 },
	{ 0xC8B6, 0x01D9 }, { 0xC8B8, 0xFF26 }, { 0xC8BA, 0xFFF3 },
	{ 0xC8BC, 0xFFB3 }, { 0xC8BE, 0x0132 }, { 0xC8C0, 0xFFE8 },
	{ 0xC8C2, 0xFFDA }, { 0xC8C4, 0xFECD }, { 0xC8C6, 0x02C2 },
	{ 0xC8C8, 0x0075 }, { 0xC8CA, 0x011C }, { 0xC8CC, 0x009A },
	{ 0xC8CE, 0x0105 }, { 0xC8D0, 0x00A4 }, { 0xC8D2, 0x00AC },
	{ 0xC8D4, 0x0A8C }, { 0xC8D6, 0x0F0A }, { 0xC8D8, 0x1964 },

	/* AWB */
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x4914 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND,	0x04FF },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YEND,	0x02CF },
	{ MT9M114_CAM_AWB_AWB_XSHIFT_PRE_ADJ,		0x0033 },
	{ MT9M114_CAM_AWB_AWB_YSHIFT_PRE_ADJ,		0x0040 },
	{ MT9M114_CAM_AWB_AWB_XSCALE,			0x03, REG_U8 },
	{ MT9M114_CAM_AWB_AWB_YSCALE,			0x02, REG_U8 },
	{ MT9M114_CAM_AWB_AWB_YSHIFT_PRE_ADJ,		0x003C },
	{ 0xC8F4, 0x0000 },
	{ 0xC8F6, 0x0000 },
	{ 0xC8F8, 0x0000 },
	{ 0xC8FA, 0xE724 },
	{ 0xC8FC, 0x1583 },
	{ 0xC8FE, 0x2045 },
	{ 0xC900, 0x03FF },
	{ 0xC902, 0x007C },
	{ 0xC90C, 0x80, REG_U8 },
	{ 0xC90D, 0x80, REG_U8 },
	{ 0xC90E, 0x80, REG_U8 },
	{ 0xC90F, 0x88, REG_U8 },
	{ 0xC910, 0x80, REG_U8 },
	{ 0xC911, 0x80, REG_U8 },

	/* CPIPE Preference */
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x4926 },
	{ 0xC926, 0x0020 },
	{ 0xC928, 0x009A },
	{ 0xC946, 0x0070 },
	{ 0xC948, 0x00F3 },
	{ 0xC952, 0x0020 },
	{ 0xC954, 0x009A },
	{ 0xC92A, 0x80, REG_U8 },
	{ 0xC92B, 0x4B, REG_U8 },
	{ 0xC92C, 0x00, REG_U8 },
	{ 0xC92D, 0xFF, REG_U8 },
	{ 0xC92E, 0x3C, REG_U8 },
	{ 0xC92F, 0x02, REG_U8 },
	{ 0xC930, 0x06, REG_U8 },
	{ 0xC931, 0x64, REG_U8 },
	{ 0xC932, 0x01, REG_U8 },
	{ 0xC933, 0x0C, REG_U8 },
	{ 0xC934, 0x3C, REG_U8 },
	{ 0xC935, 0x3C, REG_U8 },
	{ 0xC936, 0x3C, REG_U8 },
	{ 0xC937, 0x0F, REG_U8 },
	{ 0xC938, 0x64, REG_U8 },
	{ 0xC939, 0x64, REG_U8 },
	{ 0xC93A, 0x64, REG_U8 },
	{ 0xC93B, 0x32, REG_U8 },
	{ 0xC93C, 0x0020 },
	{ 0xC93E, 0x009A },
	{ 0xC940, 0x00DC },
	{ 0xC942, 0x38, REG_U8 },
	{ 0xC943, 0x30, REG_U8 },
	{ 0xC944, 0x50, REG_U8 },
	{ 0xC945, 0x19, REG_U8 },
	{ 0xC94A, 0x0230 },
	{ 0xC94C, 0x0010 },
	{ 0xC94E, 0x01CD },
	{ 0xC950, 0x05, REG_U8 },
	{ 0xC951, 0x40, REG_U8 },
	{ 0xC87B, 0x1B, REG_U8 },
	{ MT9M114_CAM_AET_AEMODE, 0x0E },
	{ 0xC890, 0x0080 },
	{ 0xC886, 0x0100 },
	{ 0xC87C, 0x005A },
	{ 0xB42A, 0x05, REG_U8 },
	{ 0xA80A, 0x20, REG_U8 },

	/* Speed up AE/AWB */
	{ MT9M114_LOGICAL_ADDRESS_ACCESS, 0x2802 },
	{ 0xA802, 0x0008 },
	{ 0xC908, 0x01, REG_U8 },
	{ 0xC879, 0x01, REG_U8 },
	{ 0xC909, 0x02, REG_U8 },
	{ 0xA80A, 0x18, REG_U8 },
	{ 0xA80B, 0x18, REG_U8 },
	{ 0xAC16, 0x18, REG_U8 },
	{ MT9M114_CAM_AET_AEMODE, 0x0E, REG_U8 },

	/* Enable LED */
	{ MT9M114_PAD_CONTROL,				0x0fd9 },
	{ MT9M114_FLASH,				0x0708 },
};

static const struct mt9m114_reg mt9m114_regs_qsif[] = {
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x1000 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_START,		0x0030 },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_START,		0x0004 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_END,		0x039F },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_END,		0x050B },
	{ MT9M114_CAM_SENSOR_CFG_PIXCLK,		0x2DC6C00, REG_U32 },
	{ MT9M114_CAM_SENSOR_CFG_ROW_SPEED,		0x0001 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN,	0x00DB },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX,	0x05BD },
	{ MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES,	0x03E8 },
	{ MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK,	0x0640 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_CORRECTION,	0x0060 },
	{ MT9M114_CAM_SENSOR_CFG_CPIPE_LAST_ROW,	0x036B },
	{ MT9M114_CAM_SENSOR_CFG_REG_0_DATA,		0x0020 },
	{ MT9M114_CAM_SENSOR_CONTROL_READ_MODE,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_XOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_YOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_WIDTH,		0x0500 },
	{ MT9M114_CAM_CROP_WINDOW_HEIGHT,		0x0368 },
	{ MT9M114_CAM_CROP_CROPMODE,			0x03, REG_U8 },
	{ MT9M114_CAM_OUTPUT_WIDTH,			0x00B0 },
	{ MT9M114_CAM_OUTPUT_HEIGHT,			0x0078 },
	{ MT9M114_CAM_AET_AEMODE,			0x00, REG_U8 },
	{ MT9M114_CAM_AET_MAX_FRAME_RATE,		0x1E00 },
	{ MT9M114_CAM_AET_MIN_FRAME_RATE,		0x0F00 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND,	0x00AF },
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
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_END,		0x039D },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_END,		0x049D },
	{ MT9M114_CAM_SENSOR_CFG_PIXCLK,		0x2DC6C00, REG_U32 },
	{ MT9M114_CAM_SENSOR_CFG_ROW_SPEED,		0x0001 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN,	0x01C3 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX,	0x03F7 },
	{ MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES,	0x0500 },
	{ MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK,	0x04E2 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_CORRECTION,	0x00E0 },
	{ MT9M114_CAM_SENSOR_CFG_CPIPE_LAST_ROW,	0x01B3 },
	{ MT9M114_CAM_SENSOR_CFG_REG_0_DATA,		0x0020 },
	{ MT9M114_CAM_SENSOR_CONTROL_READ_MODE,		0x0330 },
	{ MT9M114_CAM_CROP_WINDOW_XOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_YOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_WIDTH,		0x0210 },
	{ MT9M114_CAM_CROP_WINDOW_HEIGHT,		0x01B0 },
	{ MT9M114_CAM_CROP_CROPMODE,			0x03, REG_U8 },
	{ MT9M114_CAM_OUTPUT_WIDTH,			0x00B0 },
	{ MT9M114_CAM_OUTPUT_HEIGHT,			0x0090 },
	{ MT9M114_CAM_AET_AEMODE,			0x00, REG_U8 },
	{ MT9M114_CAM_AET_MAX_FRAME_RATE,		0x1E00 },
	{ MT9M114_CAM_AET_MIN_FRAME_RATE,		0x0F00 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND,	0x00AF },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YEND,	0x008F },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XEND,	0x0022 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YEND,	0x001B },
};

static const struct mt9m114_reg mt9m114_regs_qvga[] = {
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x1000 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_START,		0x0000 },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_START,		0x0000 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_END,		0x03CD },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_END,		0x050D },
	{ MT9M114_CAM_SENSOR_CFG_PIXCLK,		0x2DC6C00, REG_U32 },
	{ MT9M114_CAM_SENSOR_CFG_ROW_SPEED,		0x0001 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN,	0x01C3 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX,	0x03F7 },
	{ MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES,	0x0500 },
	{ MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK,	0x04E2 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_CORRECTION,	0x00E0 },
	{ MT9M114_CAM_SENSOR_CFG_CPIPE_LAST_ROW,	0x01E3 },
	{ MT9M114_CAM_SENSOR_CFG_REG_0_DATA,		0x0020 },
	{ MT9M114_CAM_SENSOR_CONTROL_READ_MODE,		0x0330 },
	{ MT9M114_CAM_CROP_WINDOW_XOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_YOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_WIDTH,		0x0280 },
	{ MT9M114_CAM_CROP_WINDOW_HEIGHT,		0x01E0 },
	{ MT9M114_CAM_CROP_CROPMODE,			0x03, REG_U8 },
	{ MT9M114_CAM_OUTPUT_WIDTH,			0x0140 },
	{ MT9M114_CAM_OUTPUT_HEIGHT,			0x00F0 },
	{ MT9M114_CAM_AET_AEMODE,			0x00, REG_U8 },
	{ MT9M114_CAM_AET_MAX_FRAME_RATE,		0x1E00 },
	{ MT9M114_CAM_AET_MIN_FRAME_RATE,		0x0F00 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND,	0x013F },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YEND,	0x00EF },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XEND,	0x003F },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YEND,	0x002F },
};

static const struct mt9m114_reg mt9m114_regs_sif[] = {
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x1000 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_START,		0x0030 },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_START,		0x0004 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_END,		0x039F },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_END,		0x050B },
	{ MT9M114_CAM_SENSOR_CFG_PIXCLK,		0x2DC6C00, REG_U32 },
	{ MT9M114_CAM_SENSOR_CFG_ROW_SPEED,		0x0001 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN,	0x00DB },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX,	0x05BD },
	{ MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES,	0x03E8 },
	{ MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK,	0x0640 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_CORRECTION,	0x0060 },
	{ MT9M114_CAM_SENSOR_CFG_CPIPE_LAST_ROW,	0x036B },
	{ MT9M114_CAM_SENSOR_CFG_REG_0_DATA,		0x0020 },
	{ MT9M114_CAM_SENSOR_CONTROL_READ_MODE,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_XOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_YOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_WIDTH,		0x0500 },
	{ MT9M114_CAM_CROP_WINDOW_HEIGHT,		0x0368 },
	{ MT9M114_CAM_CROP_CROPMODE,			0x03, REG_U8 },
	{ MT9M114_CAM_OUTPUT_WIDTH,			0x0160 },
	{ MT9M114_CAM_OUTPUT_HEIGHT,			0x00F0 },
	{ MT9M114_CAM_AET_AEMODE,			0x00, REG_U8 },
	{ MT9M114_CAM_AET_MAX_FRAME_RATE,		0x1E00 },
	{ MT9M114_CAM_AET_MIN_FRAME_RATE,		0x0F00 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND,	0x015F },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YEND,	0x00EF },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XEND,	0x0045 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YEND,	0x002F },
};

static const struct mt9m114_reg mt9m114_regs_cif[] = {
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x1000 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_START,		0x0030 },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_START,		0x0070 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_END,		0x039D },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_END,		0x049D },
	{ MT9M114_CAM_SENSOR_CFG_PIXCLK,		0x2DC6C00, REG_U32 },
	{ MT9M114_CAM_SENSOR_CFG_ROW_SPEED,		0x0001 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN,	0x01C3 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX,	0x03F7 },
	{ MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES,	0x0500 },
	{ MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK,	0x04E2 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_CORRECTION,	0x00E0 },
	{ MT9M114_CAM_SENSOR_CFG_CPIPE_LAST_ROW,	0x01B3 },
	{ MT9M114_CAM_SENSOR_CFG_REG_0_DATA,		0x0020 },
	{ MT9M114_CAM_SENSOR_CONTROL_READ_MODE,		0x0330 },
	{ MT9M114_CAM_CROP_WINDOW_XOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_YOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_WIDTH,		0x0210 },
	{ MT9M114_CAM_CROP_WINDOW_HEIGHT,		0x01B0 },
	{ MT9M114_CAM_CROP_CROPMODE,			0x03, REG_U8 },
	{ MT9M114_CAM_OUTPUT_WIDTH,			0x0160 },
	{ MT9M114_CAM_OUTPUT_HEIGHT,			0x0120 },
	{ MT9M114_CAM_AET_AEMODE,			0x00, REG_U8 },
	{ MT9M114_CAM_AET_MAX_FRAME_RATE,		0x1E00 },
	{ MT9M114_CAM_AET_MIN_FRAME_RATE,		0x0F00 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND,	0x015F },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YEND,	0x011F },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XEND,	0x0045 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YEND,	0x0038 },
};

static const struct mt9m114_reg mt9m114_regs_vga[] = {
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x1000 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_START,		0x0000 },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_START,		0x0000 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_END,		0x03CD },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_END,		0x050D },
	{ MT9M114_CAM_SENSOR_CFG_PIXCLK,		0x2DC6C00, REG_U32 },
	{ MT9M114_CAM_SENSOR_CFG_ROW_SPEED,		0x0001 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN,	0x01C3 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX,	0x03F7 },
	{ MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES,	0x0500 },
	{ MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK,	0x04E2 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_CORRECTION,	0x00E0 },
	{ MT9M114_CAM_SENSOR_CFG_CPIPE_LAST_ROW,	0x01E3 },
	{ MT9M114_CAM_SENSOR_CFG_REG_0_DATA,		0x0020 },
	{ MT9M114_CAM_SENSOR_CONTROL_READ_MODE,		0x0330 },
	{ MT9M114_CAM_CROP_WINDOW_XOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_YOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_WIDTH,		0x0280 },
	{ MT9M114_CAM_CROP_WINDOW_HEIGHT,		0x01E0 },
	{ MT9M114_CAM_CROP_CROPMODE,			0x03, REG_U8 },
	{ MT9M114_CAM_OUTPUT_WIDTH,			0x0280 },
	{ MT9M114_CAM_OUTPUT_HEIGHT,			0x01E0 },
	{ MT9M114_CAM_AET_AEMODE,			0x00, REG_U8 },
	{ MT9M114_CAM_AET_MAX_FRAME_RATE,		0x1E00 },
	{ MT9M114_CAM_AET_MIN_FRAME_RATE,		0x0F00 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND,	0x027F },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YEND,	0x01DF },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XEND,	0x007F },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YEND,	0x005F },
};

static const struct mt9m114_reg mt9m114_regs_720p[] = {
	{ MT9M114_LOGICAL_ADDRESS_ACCESS,		0x1000 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_START,		0x007C },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_START,		0x0004 },
	{ MT9M114_CAM_SENSOR_CFG_Y_ADDR_END,		0x0353 },
	{ MT9M114_CAM_SENSOR_CFG_X_ADDR_END,		0x050B },
	{ MT9M114_CAM_SENSOR_CFG_PIXCLK,		0x2DC6C00, REG_U32 },
	{ MT9M114_CAM_SENSOR_CFG_ROW_SPEED,		0x0001 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN,	0x00DB },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX,	0x05BD },
	{ MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES,	0x03E8 },
	{ MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK,	0x0640 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_CORRECTION,	0x0060 },
	{ MT9M114_CAM_SENSOR_CFG_CPIPE_LAST_ROW,	0x02D3 },
	{ MT9M114_CAM_SENSOR_CFG_REG_0_DATA,		0x0020 },
	{ MT9M114_CAM_SENSOR_CONTROL_READ_MODE,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_XOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_YOFFSET,		0x0000 },
	{ MT9M114_CAM_CROP_WINDOW_WIDTH,		0x0500 },
	{ MT9M114_CAM_CROP_WINDOW_HEIGHT,		0x02D0 },
	{ MT9M114_CAM_CROP_CROPMODE,			0x03, REG_U8 },
	{ MT9M114_CAM_OUTPUT_WIDTH,			0x0500 },
	{ MT9M114_CAM_OUTPUT_HEIGHT,			0x02D0 },
	{ MT9M114_CAM_AET_AEMODE,			0x00, REG_U8 },
	{ MT9M114_CAM_AET_MAX_FRAME_RATE,		0x1E00 },
	{ MT9M114_CAM_AET_MIN_FRAME_RATE,		0x0F00 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND,	0x04FF },
	{ MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YEND,	0x02CF },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YSTART,	0x0000 },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XEND,	0x00FF },
	{ MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YEND,	0x008F },
};

/* Black level correction firmware patch */
static const struct mt9m114_reg mt9m114_firmware_patch[] = {
	{ 0xD000, 0x70CF }, { 0xD002, 0xFFFF }, { 0xD004, 0xC5D4 },
	{ 0xD006, 0x903A }, { 0xD008, 0x2144 }, { 0xD00A, 0x0C00 },
	{ 0xD00C, 0x2186 }, { 0xD00E, 0x0FF3 }, { 0xD010, 0xB844 },
	{ 0xD012, 0xB948 }, { 0xD014, 0xE082 }, { 0xD016, 0x20CC },
	{ 0xD018, 0x80E2 }, { 0xD01A, 0x21CC }, { 0xD01C, 0x80A2 },
	{ 0xD01E, 0x21CC }, { 0xD020, 0x80E2 }, { 0xD022, 0xF404 },
	{ 0xD024, 0xD801 }, { 0xD026, 0xF003 }, { 0xD028, 0xD800 },
	{ 0xD02A, 0x7EE0 }, { 0xD02C, 0xC0F1 }, { 0xD02E, 0x08BA },
	{ 0xD030, 0x0600 }, { 0xD032, 0xC1A1 }, { 0xD034, 0x76CF },
	{ 0xD036, 0xFFFF }, { 0xD038, 0xC130 }, { 0xD03A, 0x6E04 },
	{ 0xD03C, 0xC040 }, { 0xD03E, 0x71CF }, { 0xD040, 0xFFFF },
	{ 0xD042, 0xC790 }, { 0xD044, 0x8103 }, { 0xD046, 0x77CF },
	{ 0xD048, 0xFFFF }, { 0xD04A, 0xC7C0 }, { 0xD04C, 0xE001 },
	{ 0xD04E, 0xA103 }, { 0xD050, 0xD800 }, { 0xD052, 0x0C6A },
	{ 0xD054, 0x04E0 }, { 0xD056, 0xB89E }, { 0xD058, 0x7508 },
	{ 0xD05A, 0x8E1C }, { 0xD05C, 0x0809 }, { 0xD05E, 0x0191 },
	{ 0xD060, 0xD801 }, { 0xD062, 0xAE1D }, { 0xD064, 0xE580 },
	{ 0xD066, 0x20CA }, { 0xD068, 0x0022 }, { 0xD06A, 0x20CF },
	{ 0xD06C, 0x0522 }, { 0xD06E, 0x0C5C }, { 0xD070, 0x04E2 },
	{ 0xD072, 0x21CA }, { 0xD074, 0x0062 }, { 0xD076, 0xE580 },
	{ 0xD078, 0xD901 }, { 0xD07A, 0x79C0 }, { 0xD07C, 0xD800 },
	{ 0xD07E, 0x0BE6 }, { 0xD080, 0x04E0 }, { 0xD082, 0xB89E },
	{ 0xD084, 0x70CF }, { 0xD086, 0xFFFF }, { 0xD088, 0xC8D4 },
	{ 0xD08A, 0x9002 }, { 0xD08C, 0x0857 }, { 0xD08E, 0x025E },
	{ 0xD090, 0xFFDC }, { 0xD092, 0xE080 }, { 0xD094, 0x25CC },
	{ 0xD096, 0x9022 }, { 0xD098, 0xF225 }, { 0xD09A, 0x1700 },
	{ 0xD09C, 0x108A }, { 0xD09E, 0x73CF }, { 0xD0A0, 0xFF00 },
	{ 0xD0A2, 0x3174 }, { 0xD0A4, 0x9307 }, { 0xD0A6, 0x2A04 },
	{ 0xD0A8, 0x103E }, { 0xD0AA, 0x9328 }, { 0xD0AC, 0x2942 },
	{ 0xD0AE, 0x7140 }, { 0xD0B0, 0x2A04 }, { 0xD0B2, 0x107E },
	{ 0xD0B4, 0x9349 }, { 0xD0B6, 0x2942 }, { 0xD0B8, 0x7141 },
	{ 0xD0BA, 0x2A04 }, { 0xD0BC, 0x10BE }, { 0xD0BE, 0x934A },
	{ 0xD0C0, 0x2942 }, { 0xD0C2, 0x714B }, { 0xD0C4, 0x2A04 },
	{ 0xD0C6, 0x10BE }, { 0xD0C8, 0x130C }, { 0xD0CA, 0x010A },
	{ 0xD0CC, 0x2942 }, { 0xD0CE, 0x7142 }, { 0xD0D0, 0x2250 },
	{ 0xD0D2, 0x13CA }, { 0xD0D4, 0x1B0C }, { 0xD0D6, 0x0284 },
	{ 0xD0D8, 0xB307 }, { 0xD0DA, 0xB328 }, { 0xD0DC, 0x1B12 },
	{ 0xD0DE, 0x02C4 }, { 0xD0E0, 0xB34A }, { 0xD0E2, 0xED88 },
	{ 0xD0E4, 0x71CF }, { 0xD0E6, 0xFF00 }, { 0xD0E8, 0x3174 },
	{ 0xD0EA, 0x9106 }, { 0xD0EC, 0xB88F }, { 0xD0EE, 0xB106 },
	{ 0xD0F0, 0x210A }, { 0xD0F2, 0x8340 }, { 0xD0F4, 0xC000 },
	{ 0xD0F6, 0x21CA }, { 0xD0F8, 0x0062 }, { 0xD0FA, 0x20F0 },
	{ 0xD0FC, 0x0040 }, { 0xD0FE, 0x0B02 }, { 0xD100, 0x0320 },
	{ 0xD102, 0xD901 }, { 0xD104, 0x07F1 }, { 0xD106, 0x05E0 },
	{ 0xD108, 0xC0A1 }, { 0xD10A, 0x78E0 }, { 0xD10C, 0xC0F1 },
	{ 0xD10E, 0x71CF }, { 0xD110, 0xFFFF }, { 0xD112, 0xC7C0 },
	{ 0xD114, 0xD840 }, { 0xD116, 0xA900 }, { 0xD118, 0x71CF },
	{ 0xD11A, 0xFFFF }, { 0xD11C, 0xD02C }, { 0xD11E, 0xD81E },
	{ 0xD120, 0x0A5A }, { 0xD122, 0x04E0 }, { 0xD124, 0xDA00 },
	{ 0xD126, 0xD800 }, { 0xD128, 0xC0D1 }, { 0xD12A, 0x7EE0 },
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

	*width = mt9m114_resolutions[MT9M114_720P].width;
	*height = mt9m114_resolutions[MT9M114_720P].height;
}

/* Setup registers according to resolution */
static int mt9m114_set_res(struct i2c_client *client, u32 width, u32 height)
{
	int err;

	/* select register configuration for given resolution */
	if ((width == mt9m114_resolutions[MT9M114_QSIF].width) &&
	    (height == mt9m114_resolutions[MT9M114_QSIF].height)) {
		dev_dbg(&client->dev, "Setting image size to 176x120\n");
		WRITEARRAY(mt9m114_regs_qsif);
	} else if ((width == mt9m114_resolutions[MT9M114_QCIF].width) &&
		   (height == mt9m114_resolutions[MT9M114_QCIF].height)) {
		dev_dbg(&client->dev, "Setting image size to 176x144\n");
		WRITEARRAY(mt9m114_regs_qcif);
	} else if ((width == mt9m114_resolutions[MT9M114_QVGA].width) &&
		   (height == mt9m114_resolutions[MT9M114_QVGA].height)) {
		dev_dbg(&client->dev, "Setting image size to 320x240\n");
		WRITEARRAY(mt9m114_regs_qvga);
	} else if ((width == mt9m114_resolutions[MT9M114_SIF].width) &&
		   (height == mt9m114_resolutions[MT9M114_SIF].height)) {
		dev_dbg(&client->dev, "Setting image size to 352x240\n");
		WRITEARRAY(mt9m114_regs_sif);
	} else if ((width == mt9m114_resolutions[MT9M114_CIF].width) &&
		   (height == mt9m114_resolutions[MT9M114_CIF].height)) {
		dev_dbg(&client->dev, "Setting image size to 352x288\n");
		WRITEARRAY(mt9m114_regs_cif);
	} else if ((width == mt9m114_resolutions[MT9M114_VGA].width) &&
		   (height == mt9m114_resolutions[MT9M114_VGA].height)) {
		dev_dbg(&client->dev, "Setting image size to 640x480\n");
		WRITEARRAY(mt9m114_regs_vga);
	} else if ((width == mt9m114_resolutions[MT9M114_720P].width) &&
		   (height == mt9m114_resolutions[MT9M114_720P].height)) {
		dev_dbg(&client->dev, "Setting image size to 1280x720\n");
		WRITEARRAY(mt9m114_regs_720p);
	} else {
		dev_err(&client->dev, "Failed to select resolution!\n");
		WARN_ON(1);
		err = -EINVAL;
	}

err:
	return err;
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
	a->bounds.width		= mt9m114_resolutions[MT9M114_720P].width;
	a->bounds.height	= mt9m114_resolutions[MT9M114_720P].height;
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
	a->c.width		= mt9m114_resolutions[MT9M114_720P].width;
	a->c.height		= mt9m114_resolutions[MT9M114_720P].height;
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
