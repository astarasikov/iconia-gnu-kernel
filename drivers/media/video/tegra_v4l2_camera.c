/*
 * V4L2 Driver for TEGRA camera host
 *
 * Copyright (C) 2011, NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <media/soc_camera.h>
#include <media/soc_mediabus.h>
#include <media/videobuf-dma-nvmap.h>
#include <media/tegra_v4l2_camera.h>

#include <mach/nvhost.h>

#include "../../video/tegra/host/dev.h"

#define TEGRA_CAM_DRV_NAME "tegra-camera"
#define TEGRA_CAM_VERSION_CODE KERNEL_VERSION(0, 0, 5)

#define TEGRA_SYNCPT_VI_WAIT_TIMEOUT                    1000
#define TEGRA_SYNCPT_CSI_WAIT_TIMEOUT                   1000

/* SYNCPTs 12-17 are reserved for VI. */
#define TEGRA_VI_SYNCPT_VI                              NVSYNCPT_VI_ISP_2
#define TEGRA_VI_SYNCPT_CSI                             NVSYNCPT_VI_ISP_3

/* Tegra CSI-MIPI registers. */
#define TEGRA_VI_OUT_1_INCR_SYNCPT			0x0000
#define TEGRA_VI_OUT_1_INCR_SYNCPT_CNTRL		0x0004
#define TEGRA_VI_OUT_1_INCR_SYNCPT_ERROR		0x0008
#define TEGRA_VI_OUT_2_INCR_SYNCPT			0x0020
#define TEGRA_VI_OUT_2_INCR_SYNCPT_CNTRL		0x0024
#define TEGRA_VI_OUT_2_INCR_SYNCPT_ERROR		0x0028
#define TEGRA_VI_MISC_INCR_SYNCPT			0x0040
#define TEGRA_VI_MISC_INCR_SYNCPT_CNTRL			0x0044
#define TEGRA_VI_MISC_INCR_SYNCPT_ERROR			0x0048
#define TEGRA_VI_CONT_SYNCPT_OUT_1			0x0060
#define TEGRA_VI_CONT_SYNCPT_OUT_2			0x0064
#define TEGRA_VI_CONT_SYNCPT_VIP_VSYNC			0x0068
#define TEGRA_VI_CONT_SYNCPT_VI2EPP			0x006c
#define TEGRA_VI_CONT_SYNCPT_CSI_PPA_FRAME_START	0x0070
#define TEGRA_VI_CONT_SYNCPT_CSI_PPA_FRAME_END		0x0074
#define TEGRA_VI_CONT_SYNCPT_CSI_PPB_FRAME_START	0x0078
#define TEGRA_VI_CONT_SYNCPT_CSI_PPB_FRAME_END		0x007c
#define TEGRA_VI_CTXSW					0x0080
#define TEGRA_VI_INTSTATUS				0x0084
#define TEGRA_VI_VI_INPUT_CONTROL			0x0088
#define TEGRA_VI_VI_CORE_CONTROL			0x008c
#define TEGRA_VI_VI_FIRST_OUTPUT_CONTROL		0x0090
#define TEGRA_VI_VI_SECOND_OUTPUT_CONTROL		0x0094
#define TEGRA_VI_HOST_INPUT_FRAME_SIZE			0x0098
#define TEGRA_VI_HOST_H_ACTIVE				0x009c
#define TEGRA_VI_HOST_V_ACTIVE				0x00a0
#define TEGRA_VI_VIP_H_ACTIVE				0x00a4
#define TEGRA_VI_VIP_V_ACTIVE				0x00a8
#define TEGRA_VI_VI_PEER_CONTROL			0x00ac
#define TEGRA_VI_VI_DMA_SELECT				0x00b0
#define TEGRA_VI_HOST_DMA_WRITE_BUFFER			0x00b4
#define TEGRA_VI_HOST_DMA_BASE_ADDRESS			0x00b8
#define TEGRA_VI_HOST_DMA_WRITE_BUFFER_STATUS		0x00bc
#define TEGRA_VI_HOST_DMA_WRITE_PEND_BUFCOUNT		0x00c0
#define TEGRA_VI_VB0_START_ADDRESS_FIRST		0x00c4
#define TEGRA_VI_VB0_BASE_ADDRESS_FIRST			0x00c8
#define TEGRA_VI_VB0_START_ADDRESS_U			0x00cc
#define TEGRA_VI_VB0_BASE_ADDRESS_U			0x00d0
#define TEGRA_VI_VB0_START_ADDRESS_V			0x00d4
#define TEGRA_VI_VB0_BASE_ADDRESS_V			0x00d8
#define TEGRA_VI_VB_SCRATCH_ADDRESS_UV			0x00dc
#define TEGRA_VI_FIRST_OUTPUT_FRAME_SIZE		0x00e0
#define TEGRA_VI_VB0_COUNT_FIRST			0x00e4
#define TEGRA_VI_VB0_SIZE_FIRST				0x00e8
#define TEGRA_VI_VB0_BUFFER_STRIDE_FIRST		0x00ec
#define TEGRA_VI_VB0_START_ADDRESS_SECOND		0x00f0
#define TEGRA_VI_VB0_BASE_ADDRESS_SECOND		0x00f4
#define TEGRA_VI_SECOND_OUTPUT_FRAME_SIZE		0x00f8
#define TEGRA_VI_VB0_COUNT_SECOND			0x00fc
#define TEGRA_VI_VB0_SIZE_SECOND			0x0100
#define TEGRA_VI_VB0_BUFFER_STRIDE_SECOND		0x0104
#define TEGRA_VI_H_LPF_CONTROL				0x0108
#define TEGRA_VI_H_DOWNSCALE_CONTROL			0x010c
#define TEGRA_VI_V_DOWNSCALE_CONTROL			0x0110
#define TEGRA_VI_CSC_Y					0x0114
#define TEGRA_VI_CSC_UV_R				0x0118
#define TEGRA_VI_CSC_UV_G				0x011c
#define TEGRA_VI_CSC_UV_B				0x0120
#define TEGRA_VI_CSC_ALPHA				0x0124
#define TEGRA_VI_HOST_VSYNC				0x0128
#define TEGRA_VI_COMMAND				0x012c
#define TEGRA_VI_HOST_FIFO_STATUS			0x0130
#define TEGRA_VI_INTERRUPT_MASK				0x0134
#define TEGRA_VI_INTERRUPT_TYPE_SELECT			0x0138
#define TEGRA_VI_INTERRUPT_POLARITY_SELECT		0x013c
#define TEGRA_VI_INTERRUPT_STATUS			0x0140
#define TEGRA_VI_VIP_INPUT_STATUS			0x0144
#define TEGRA_VI_VIDEO_BUFFER_STATUS			0x0148
#define TEGRA_VI_SYNC_OUTPUT				0x014c
#define TEGRA_VI_VVS_OUTPUT_DELAY			0x0150
#define TEGRA_VI_PWM_CONTROL				0x0154
#define TEGRA_VI_PWM_SELECT_PULSE_A			0x0158
#define TEGRA_VI_PWM_SELECT_PULSE_B			0x015c
#define TEGRA_VI_PWM_SELECT_PULSE_C			0x0160
#define TEGRA_VI_PWM_SELECT_PULSE_D			0x0164
#define TEGRA_VI_VI_DATA_INPUT_CONTROL			0x0168
#define TEGRA_VI_PIN_INPUT_ENABLE			0x016c
#define TEGRA_VI_PIN_OUTPUT_ENABLE			0x0170
#define TEGRA_VI_PIN_INVERSION				0x0174
#define TEGRA_VI_PIN_INPUT_DATA				0x0178
#define TEGRA_VI_PIN_OUTPUT_DATA			0x017c
#define TEGRA_VI_PIN_OUTPUT_SELECT			0x0180
#define TEGRA_VI_RAISE_VIP_BUFFER_FIRST_OUTPUT		0x0184
#define TEGRA_VI_RAISE_VIP_FRAME_FIRST_OUTPUT		0x0188
#define TEGRA_VI_RAISE_VIP_BUFFER_SECOND_OUTPUT		0x018c
#define TEGRA_VI_RAISE_VIP_FRAME_SECOND_OUTPUT		0x0190
#define TEGRA_VI_RAISE_HOST_FIRST_OUTPUT		0x0194
#define TEGRA_VI_RAISE_HOST_SECOND_OUTPUT		0x0198
#define TEGRA_VI_RAISE_EPP				0x019c
#define TEGRA_VI_CAMERA_CONTROL				0x01a0
#define TEGRA_VI_VI_ENABLE				0x01a4
#define TEGRA_VI_VI_ENABLE_2				0x01a8
#define TEGRA_VI_VI_RAISE				0x01ac
#define TEGRA_VI_Y_FIFO_WRITE				0x01b0
#define TEGRA_VI_U_FIFO_WRITE				0x01b4
#define TEGRA_VI_V_FIFO_WRITE				0x01b8
#define TEGRA_VI_VI_MCCIF_FIFOCTRL			0x01bc
#define TEGRA_VI_TIMEOUT_WCOAL_VI			0x01c0
#define TEGRA_VI_MCCIF_VIRUV_HP				0x01c4
#define TEGRA_VI_MCCIF_VIWSB_HP				0x01c8
#define TEGRA_VI_MCCIF_VIWU_HP				0x01cc
#define TEGRA_VI_MCCIF_VIWV_HP				0x01d0
#define TEGRA_VI_MCCIF_VIWY_HP				0x01d4
#define TEGRA_VI_CSI_PPA_RAISE_FRAME_START		0x01d8
#define TEGRA_VI_CSI_PPA_RAISE_FRAME_END		0x01dc
#define TEGRA_VI_CSI_PPB_RAISE_FRAME_START		0x01e0
#define TEGRA_VI_CSI_PBB_RAISE_FRAME_END		0x01e4
#define TEGRA_VI_CSI_PPA_H_ACTIVE			0x01e8
#define TEGRA_VI_CSI_PPA_V_ACTIVE			0x01ec
#define TEGRA_VI_CSI_PPB_H_ACTIVE			0x01f0
#define TEGRA_VI_CSI_PPB_V_ACTIVE			0x01f4
#define TEGRA_VI_ISP_H_ACTIVE				0x01f8
#define TEGRA_VI_ISP_V_ACTIVE				0x01fc
#define TEGRA_VI_STREAM_1_RESOURCE_DEFINE		0x0200
#define TEGRA_VI_STREAM_2_RESOURCE_DEFINE		0x0204
#define TEGRA_VI_RAISE_STREAM_1_DONE			0x0208
#define TEGRA_VI_RAISE_STREAM_2_DONE			0x020c
#define TEGRA_VI_TS_MODE				0x0210
#define TEGRA_VI_TS_CONTROL				0x0214
#define TEGRA_VI_TS_PACKET_COUNT			0x0218
#define TEGRA_VI_TS_ERROR_COUNT				0x021c
#define TEGRA_VI_TS_CPU_FLOW_CTL			0x0220
#define TEGRA_VI_VB0_CHROMA_BUFFER_STRIDE_FIRST		0x0224
#define TEGRA_VI_VB0_CHROMA_LINE_STRIDE_FIRST		0x0228
#define TEGRA_VI_EPP_LINES_PER_BUFFER			0x022c
#define TEGRA_VI_BUFFER_RELEASE_OUTPUT1			0x0230
#define TEGRA_VI_BUFFER_RELEASE_OUTPUT2			0x0234
#define TEGRA_VI_DEBUG_FLOW_CONTROL_COUNTER_OUTPUT1	0x0238
#define TEGRA_VI_DEBUG_FLOW_CONTROL_COUNTER_OUTPUT2	0x023c
#define TEGRA_VI_TERMINATE_BW_FIRST			0x0240
#define TEGRA_VI_TERMINATE_BW_SECOND			0x0244
#define TEGRA_VI_VB0_FIRST_BUFFER_ADDR_MODE		0x0248
#define TEGRA_VI_VB0_SECOND_BUFFER_ADDR_MODE		0x024c
#define TEGRA_VI_RESERVE_0				0x0250
#define TEGRA_VI_RESERVE_1				0x0254
#define TEGRA_VI_RESERVE_2				0x0258
#define TEGRA_VI_RESERVE_3				0x025c
#define TEGRA_VI_RESERVE_4				0x0260
#define TEGRA_VI_MCCIF_VIRUV_HYST			0x0264
#define TEGRA_VI_MCCIF_VIWSB_HYST			0x0268
#define TEGRA_VI_MCCIF_VIWU_HYST			0x026c
#define TEGRA_VI_MCCIF_VIWV_HYST			0x0270
#define TEGRA_VI_MCCIF_VIWY_HYST			0x0274

#define TEGRA_CSI_VI_INPUT_STREAM_CONTROL		0x0800
#define TEGRA_CSI_HOST_INPUT_STREAM_CONTROL		0x0808
#define TEGRA_CSI_INPUT_STREAM_A_CONTROL		0x0810
#define TEGRA_CSI_PIXEL_STREAM_A_CONTROL0		0x0818
#define TEGRA_CSI_PIXEL_STREAM_A_CONTROL1		0x081c
#define TEGRA_CSI_PIXEL_STREAM_A_WORD_COUNT		0x0820
#define TEGRA_CSI_PIXEL_STREAM_A_GAP			0x0824
#define TEGRA_CSI_PIXEL_STREAM_PPA_COMMAND		0x0828
#define TEGRA_CSI_INPUT_STREAM_B_CONTROL		0x083c
#define TEGRA_CSI_PIXEL_STREAM_B_CONTROL0		0x0844
#define TEGRA_CSI_PIXEL_STREAM_B_CONTROL1		0x0848
#define TEGRA_CSI_PIXEL_STREAM_B_WORD_COUNT		0x084c
#define TEGRA_CSI_PIXEL_STREAM_B_GAP			0x0850
#define TEGRA_CSI_PIXEL_STREAM_PPB_COMMAND		0x0854
#define TEGRA_CSI_PHY_CIL_COMMAND			0x0868
#define TEGRA_CSI_PHY_CILA_CONTROL0			0x086c
#define TEGRA_CSI_PHY_CILB_CONTROL0			0x0870
#define TEGRA_CSI_CSI_PIXEL_PARSER_STATUS		0x0878
#define TEGRA_CSI_CSI_CIL_STATUS			0x087c
#define TEGRA_CSI_CSI_PIXEL_PARSER_INTERRUPT_MASK	0x0880
#define TEGRA_CSI_CSI_CIL_INTERRUPT_MASK		0x0884
#define TEGRA_CSI_CSI_READONLY_STATUS			0x0888
#define TEGRA_CSI_ESCAPE_MODE_COMMAND			0x088c
#define TEGRA_CSI_ESCAPE_MODE_DATA			0x0890
#define TEGRA_CSI_CILA_PAD_CONFIG0			0x0894
#define TEGRA_CSI_CILA_PAD_CONFIG1			0x0898
#define TEGRA_CSI_CILB_PAD_CONFIG0			0x089c
#define TEGRA_CSI_CILB_PAD_CONFIG1			0x08a0
#define TEGRA_CSI_CIL_PAD_CONFIG0			0x08a4
#define TEGRA_CSI_CILA_MIPI_CAL_CONFIG			0x08a8
#define TEGRA_CSI_CILB_MIPI_CAL_CONFIG			0x08ac
#define TEGRA_CSI_CIL_MIPI_CAL_STATUS			0x08b0
#define TEGRA_CSI_CLKEN_OVERRIDE			0x08b4
#define TEGRA_CSI_DEBUG_CONTROL				0x08b8
#define TEGRA_CSI_DEBUG_COUNTER_0			0x08bc
#define TEGRA_CSI_DEBUG_COUNTER_1			0x08c0
#define TEGRA_CSI_DEBUG_COUNTER_2			0x08c4
#define TEGRA_CSI_PIXEL_STREAM_A_EXPECTED_FRAME		0x08c8
#define TEGRA_CSI_PIXEL_STREAM_B_EXPECTED_FRAME		0x08cc
#define TEGRA_CSI_DSI_MIPI_CAL_CONFIG			0x08d0

#define TC_VI_REG_RD(DEV, REG) readl(DEV->vi_base + REG)
#define TC_VI_REG_WT(DEV, REG, VAL) writel(VAL, DEV->vi_base + REG)

/*
 * Structures
 */

/* buffer for one video frame */
struct tegra_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer		vb;
	enum v4l2_mbus_pixelcode	code;
};

struct tegra_camera_dev {
	struct nvhost_device *ndev;
	struct soc_camera_host soc_host;
	struct soc_camera_device *icd;
	struct tegra_camera_platform_data *pdata;

	void __iomem *vi_base;
	spinlock_t videobuf_queue_lock;
	struct list_head capture;
	struct videobuf_buffer *active;

	struct work_struct work;
	struct mutex work_mutex;

	u32 syncpt_vi;
	u32 syncpt_csi;

	int bytes_per_line;
	int pixels_per_line;
	int lines;

	/* Debug */
	int num_frames;
};

static void tegra_camera_capture_setup(struct tegra_camera_dev *pcdev)
{
	/* Set up low pass filter.  Use 0x240 for chromaticity and 0x240
	   for luminance, which is the default and means not to touch
	   anything. */
	TC_VI_REG_WT(pcdev, TEGRA_VI_H_LPF_CONTROL, 0x02400240);

	/* Set up raise-on-edge, so we get an interrupt on end of frame. */
	TC_VI_REG_WT(pcdev, TEGRA_VI_VI_RAISE, 0x00000001);

	/* CSI_A_YUV_422 */
	TC_VI_REG_WT(pcdev, TEGRA_VI_VI_CORE_CONTROL, 0x02000000);

	TC_VI_REG_WT(pcdev, TEGRA_VI_VI_FIRST_OUTPUT_CONTROL,
		(pcdev->pdata->flip_v ? (0x1 << 20) : 0) |
		(pcdev->pdata->flip_h ? (0x1 << 19) : 0) |
		0x3); /* YUV422 non-planar after down-scaling */

	/* Set up frame size.  Bits 31:16 are the number of lines, and
	   bits 15:0 are the number of pixels per line. */
	TC_VI_REG_WT(pcdev, TEGRA_VI_FIRST_OUTPUT_FRAME_SIZE,
		(pcdev->lines << 16) | pcdev->pixels_per_line);

	/* CSIA */
	TC_VI_REG_WT(pcdev, TEGRA_VI_H_DOWNSCALE_CONTROL, 0x00000004);
	TC_VI_REG_WT(pcdev, TEGRA_VI_V_DOWNSCALE_CONTROL, 0x00000004);

	/* First output memory enabled */
	TC_VI_REG_WT(pcdev, TEGRA_VI_VI_ENABLE, 0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_VI_VI_ENABLE_2, 0x00000001);

	/* CSI-A H_ACTIVE and V_ACTIVE */
	TC_VI_REG_WT(pcdev, TEGRA_VI_CSI_PPA_H_ACTIVE,
		(pcdev->pixels_per_line << 16));
	TC_VI_REG_WT(pcdev, TEGRA_VI_CSI_PPA_V_ACTIVE,
		(pcdev->lines << 16));

	/* Set the number of frames in the buffer. */
	TC_VI_REG_WT(pcdev, TEGRA_VI_VB0_COUNT_FIRST, 0x00000001);

	/* Set up buffer frame size. */
	TC_VI_REG_WT(pcdev, TEGRA_VI_VB0_SIZE_FIRST,
		(pcdev->lines << 16) | pcdev->pixels_per_line);

	TC_VI_REG_WT(pcdev, TEGRA_VI_VB0_BUFFER_STRIDE_FIRST,
		(pcdev->lines * pcdev->bytes_per_line));

	TC_VI_REG_WT(pcdev, TEGRA_CSI_VI_INPUT_STREAM_CONTROL, 0x00000000);

	TC_VI_REG_WT(pcdev, TEGRA_CSI_HOST_INPUT_STREAM_CONTROL, 0x00000000);

	TC_VI_REG_WT(pcdev, TEGRA_CSI_INPUT_STREAM_A_CONTROL, 0x00000000);

	TC_VI_REG_WT(pcdev, TEGRA_CSI_PIXEL_STREAM_A_CONTROL0, 0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_PIXEL_STREAM_A_CONTROL1, 0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_PIXEL_STREAM_A_WORD_COUNT, 0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_PIXEL_STREAM_A_GAP, 0x00000000);

	TC_VI_REG_WT(pcdev, TEGRA_CSI_CSI_PIXEL_PARSER_STATUS, 0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_CSI_CIL_STATUS, 0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_CSI_PIXEL_PARSER_INTERRUPT_MASK,
		     0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_CSI_CIL_INTERRUPT_MASK, 0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_CSI_READONLY_STATUS, 0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_ESCAPE_MODE_COMMAND, 0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_ESCAPE_MODE_DATA, 0x00000000);

	TC_VI_REG_WT(pcdev, TEGRA_CSI_CILA_PAD_CONFIG0, 0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_CILA_PAD_CONFIG1, 0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_CIL_PAD_CONFIG0, 0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_CILA_MIPI_CAL_CONFIG, 0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_CIL_MIPI_CAL_STATUS, 0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_CLKEN_OVERRIDE, 0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_DEBUG_CONTROL, 0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_DEBUG_COUNTER_0, 0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_DEBUG_COUNTER_1, 0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_DEBUG_COUNTER_2, 0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_PIXEL_STREAM_A_EXPECTED_FRAME,
		     0x00000000);

	TC_VI_REG_WT(pcdev, TEGRA_CSI_CLKEN_OVERRIDE, 0x00000000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_CSI_PIXEL_PARSER_INTERRUPT_MASK,
		     0x00000000);

	TC_VI_REG_WT(pcdev, TEGRA_CSI_CSI_CIL_INTERRUPT_MASK, 0x00000000);

	/* pad1s enabled, virtual channel ID 00 */
	TC_VI_REG_WT(pcdev, TEGRA_CSI_PIXEL_STREAM_A_CONTROL0, 0x20011ef0);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_PIXEL_STREAM_A_CONTROL1, 0x00000001);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_PIXEL_STREAM_A_WORD_COUNT,
		pcdev->bytes_per_line);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_PIXEL_STREAM_A_GAP, 0x00140000);

	TC_VI_REG_WT(pcdev, TEGRA_CSI_PIXEL_STREAM_A_EXPECTED_FRAME,
		(pcdev->lines << 16));

	/* 1 data lane */
	TC_VI_REG_WT(pcdev, TEGRA_CSI_INPUT_STREAM_A_CONTROL, 0x007f0000);
	TC_VI_REG_WT(pcdev, TEGRA_CSI_PHY_CILA_CONTROL0, 0x00000022);
	TC_VI_REG_WT(pcdev, TEGRA_VI_VI_ENABLE, 0x00000000);

	TC_VI_REG_WT(pcdev, TEGRA_VI_CONT_SYNCPT_OUT_1,
		(0x1 << 8) | /* Enable continuous syncpt */
		TEGRA_VI_SYNCPT_VI);

	TC_VI_REG_WT(pcdev, TEGRA_VI_CONT_SYNCPT_CSI_PPA_FRAME_END,
		(0x1 << 8) | /* Enable continuous syncpt */
		TEGRA_VI_SYNCPT_CSI);

	TC_VI_REG_WT(pcdev, TEGRA_CSI_PHY_CIL_COMMAND, 0x00020001);
}

static int tegra_camera_capture_start(struct tegra_camera_dev *pcdev,
				      dma_addr_t buffer_addr)
{
	dma_addr_t final_addr;
	int err;

	pcdev->syncpt_csi++;
	pcdev->syncpt_vi++;

	final_addr = buffer_addr;

	if (pcdev->pdata->flip_v)
		final_addr += pcdev->bytes_per_line * (pcdev->lines-1);

	if (pcdev->pdata->flip_h)
		final_addr += pcdev->bytes_per_line - 1;

	TC_VI_REG_WT(pcdev, TEGRA_VI_VB0_START_ADDRESS_FIRST, final_addr);
	TC_VI_REG_WT(pcdev, TEGRA_VI_VB0_BASE_ADDRESS_FIRST, buffer_addr);

	TC_VI_REG_WT(pcdev, TEGRA_CSI_PIXEL_STREAM_PPA_COMMAND, 0x0000f005);

	err = nvhost_syncpt_wait_timeout(&pcdev->ndev->host->syncpt,
					 TEGRA_VI_SYNCPT_CSI,
					 pcdev->syncpt_csi,
					 TEGRA_SYNCPT_CSI_WAIT_TIMEOUT);

	return err;
}

static int tegra_camera_capture_stop(struct tegra_camera_dev *pcdev)
{
	int err;

	TC_VI_REG_WT(pcdev, TEGRA_CSI_PIXEL_STREAM_PPA_COMMAND, 0x0000f002);

	err = nvhost_syncpt_wait_timeout(&pcdev->ndev->host->syncpt,
					 TEGRA_VI_SYNCPT_VI,
					 pcdev->syncpt_vi,
					 TEGRA_SYNCPT_VI_WAIT_TIMEOUT);

	return err;
}

static int tegra_camera_capture_frame(struct tegra_camera_dev *pcdev)
{
	struct videobuf_buffer *vb;
	dma_addr_t buffer_addr;
	unsigned long flags;
	int err;

	if (!pcdev->active)
		return 0;

	vb = pcdev->active;

	buffer_addr = videobuf_to_dma_nvmap(vb);

	err = tegra_camera_capture_start(pcdev, buffer_addr);
	if (IS_ERR_VALUE(err))
		return err;

	err = tegra_camera_capture_stop(pcdev);
	if (IS_ERR_VALUE(err))
		return err;

	spin_lock_irqsave(&pcdev->videobuf_queue_lock, flags);

	/* If vb->state is VIDEOBUF_ERROR, then the vb has already been
	   removed, so we shouldn't remove it again. */
	if ((vb->state != VIDEOBUF_ERROR) && (vb->state != VIDEOBUF_NEEDS_INIT))
		list_del_init(&vb->queue);

	if (!list_empty(&pcdev->capture))
		pcdev->active = list_entry(pcdev->capture.next,
					   struct videobuf_buffer, queue);
	else
		pcdev->active = NULL;

	vb->state = VIDEOBUF_DONE;
	do_gettimeofday(&vb->ts);
	vb->field_count++;
	wake_up(&vb->done);

	pcdev->num_frames++;

	spin_unlock_irqrestore(&pcdev->videobuf_queue_lock, flags);

	return err;
}

static void tegra_camera_work(struct work_struct *work)
{
	struct tegra_camera_dev *pcdev =
		container_of(work, struct tegra_camera_dev, work);

	mutex_lock(&pcdev->work_mutex);

	while (pcdev->active)
		tegra_camera_capture_frame(pcdev);

	mutex_unlock(&pcdev->work_mutex);
}

static void free_buffer(struct videobuf_queue *vq,
			struct tegra_buffer *buf)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct device *dev = icd->dev.parent;

	dev_dbg(dev, "%s (vb=0x%p) 0x%08lx %zd\n", __func__,
		&buf->vb, buf->vb.baddr, buf->vb.bsize);

	videobuf_waiton(vq, &buf->vb, 0, 0);
	videobuf_dma_nvmap_free(vq, &buf->vb);
	dev_dbg(dev, "%s freed\n", __func__);
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

/*
 *  Videobuf operations
 */
static int tegra_videobuf_setup(struct videobuf_queue *vq,
				unsigned int *count, unsigned int *size)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct tegra_camera_dev *pcdev = ici->priv;
	int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
						icd->current_fmt->host_fmt);

	dev_dbg(icd->dev.parent, "In tegra_videobuf_setup()\n");

	if (bytes_per_line < 0)
		return bytes_per_line;

	*size = bytes_per_line * icd->user_height;

	if (0 == *count)
		*count = 2;

	dev_info(icd->dev.parent, "count=%d, size=%d\n", *count, *size);

	/* Save some values we're going to use to program VI/CSI. */
	pcdev->bytes_per_line = bytes_per_line;
	pcdev->pixels_per_line = icd->user_width;
	pcdev->lines = icd->user_height;

	tegra_camera_capture_setup(pcdev);

	dev_dbg(icd->dev.parent, "Finished tegra_videobuf_setup()\n");

	return 0;
}

static int tegra_videobuf_prepare(struct videobuf_queue *vq,
				  struct videobuf_buffer *vb,
				  enum v4l2_field field)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct tegra_buffer *buf;
	int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
						icd->current_fmt->host_fmt);
	int ret;

	dev_dbg(icd->dev.parent, "In tegra_videobuf_prepare()\n");

	if (bytes_per_line < 0)
		return bytes_per_line;

	buf = container_of(vb, struct tegra_buffer, vb);

	dev_dbg(icd->dev.parent, "%s (vb=0x%p) 0x%08lx %zd\n", __func__,
		vb, vb->baddr, vb->bsize);

	memset((void *)vb->baddr, 0xaa, vb->bsize);

	BUG_ON(NULL == icd->current_fmt);

	if (buf->code	!= icd->current_fmt->code ||
	    vb->width	!= icd->user_width ||
	    vb->height	!= icd->user_height ||
	    vb->field	!= field) {
		buf->code	= icd->current_fmt->code;
		vb->width	= icd->user_width;
		vb->height	= icd->user_height;
		vb->field	= field;
		vb->state	= VIDEOBUF_NEEDS_INIT;
	}

	vb->size = vb->height * bytes_per_line;
	if (0 != vb->baddr && vb->bsize < vb->size) {
		ret = -EINVAL;
		goto out;
	}

	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		ret = videobuf_iolock(vq, vb, NULL);
		if (IS_ERR_VALUE(ret))
			goto fail_free_buffer;
		vb->state = VIDEOBUF_PREPARED;
	}

	dev_dbg(icd->dev.parent, "Finished tegra_videobuf_prepare()\n");

	return 0;

fail_free_buffer:
	free_buffer(vq, buf);
out:
	return ret;
}

/* Called under spinlock_irqsave(&pcdev->videobuf_queue_lock, ...) */
static void tegra_videobuf_queue(struct videobuf_queue *vq,
				 struct videobuf_buffer *vb)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct tegra_camera_dev *pcdev = ici->priv;

	dev_dbg(icd->dev.parent, "In tegra_videobuf_queue()\n");

	dev_dbg(icd->dev.parent, "%s (vb=0x%p) 0x%08lx %zd\n", __func__,
		vb, vb->baddr, vb->bsize);

	vb->state = VIDEOBUF_QUEUED;
	list_add_tail(&vb->queue, &pcdev->capture);

	if (!pcdev->active) {
		pcdev->active = vb;
		schedule_work(&pcdev->work);
	}

	dev_dbg(icd->dev.parent, "Finished tegra_videobuf_queue()\n");
}

static void tegra_videobuf_release(struct videobuf_queue *vq,
				   struct videobuf_buffer *vb)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct tegra_camera_dev *pcdev = ici->priv;
	unsigned long flags;

	dev_dbg(icd->dev.parent, "In tegra_videobuf_release()\n");

	mutex_lock(&pcdev->work_mutex);

	spin_lock_irqsave(&pcdev->videobuf_queue_lock, flags);

	if (pcdev->active == vb)
		pcdev->active = NULL;

	if ((vb->state == VIDEOBUF_ACTIVE || vb->state == VIDEOBUF_QUEUED) &&
	    !list_empty(&vb->queue)) {
		vb->state = VIDEOBUF_ERROR;
		list_del_init(&vb->queue);
	}

	spin_unlock_irqrestore(&pcdev->videobuf_queue_lock, flags);

	mutex_unlock(&pcdev->work_mutex);

	free_buffer(vq, container_of(vb, struct tegra_buffer, vb));

	dev_dbg(icd->dev.parent, "Finished tegra_videobuf_release()\n");
}

static struct videobuf_queue_ops tegra_videobuf_ops = {
	.buf_setup      = tegra_videobuf_setup,
	.buf_prepare    = tegra_videobuf_prepare,
	.buf_queue      = tegra_videobuf_queue,
	.buf_release    = tegra_videobuf_release,
};

static void tegra_camera_init_videobuf(struct videobuf_queue *vq,
				       struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct tegra_camera_dev *pcdev = ici->priv;

	dev_dbg(icd->dev.parent, "In tegra_camera_init_videobuf()\n");

	/*
	 * We must pass NULL as dev pointer, then all pci_* dma operations
	 * transform to normal dma_* ones.
	 */
	videobuf_queue_dma_nvmap_init(vq, &tegra_videobuf_ops, NULL,
				&pcdev->videobuf_queue_lock,
				V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_FIELD_NONE,
				sizeof(struct tegra_buffer), icd, NULL);

	dev_dbg(icd->dev.parent, "Finished tegra_camera_init_videobuf()\n");
}

static void tegra_camera_activate(struct tegra_camera_dev *pcdev)
{
	/* Save current syncpt values. */
	pcdev->syncpt_csi = nvhost_syncpt_read(&pcdev->ndev->host->syncpt,
					       TEGRA_VI_SYNCPT_CSI);
	pcdev->syncpt_vi = nvhost_syncpt_read(&pcdev->ndev->host->syncpt,
					      TEGRA_VI_SYNCPT_VI);
}

static void tegra_camera_deactivate(struct tegra_camera_dev *pcdev)
{
	mutex_lock(&pcdev->work_mutex);

	/* Cancel active buffer. */
	if (pcdev->active) {
		list_del(&pcdev->active->queue);
		pcdev->active->state = VIDEOBUF_ERROR;
		wake_up_all(&pcdev->active->done);
		pcdev->active = NULL;
	}

	mutex_unlock(&pcdev->work_mutex);
}

/*
 * Called with .video_lock held
 */
static int tegra_camera_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct tegra_camera_dev *pcdev = ici->priv;
	int err;

	if (pcdev->icd)
		return -EBUSY;

	pm_runtime_get_sync(ici->v4l2_dev.dev);

	err = pcdev->pdata->enable_camera(pcdev->ndev);
	if (IS_ERR_VALUE(err))
		return err;

	tegra_camera_activate(pcdev);

	pcdev->icd = icd;

	pcdev->num_frames = 0;

	dev_info(icd->dev.parent, "TEGRA Camera host attached to camera %d\n",
		 icd->devnum);

	return 0;
}

/* Called with .video_lock held */
static void tegra_camera_remove_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct tegra_camera_dev *pcdev = ici->priv;

	tegra_camera_deactivate(pcdev);

	pcdev->icd = NULL;

	pcdev->pdata->disable_camera(pcdev->ndev);

	pm_runtime_put_sync(ici->v4l2_dev.dev);

	dev_info(icd->dev.parent, "Frames captured: %d\n", pcdev->num_frames);

	dev_info(icd->dev.parent, "TEGRA camera host detached from camera %d\n",
		 icd->devnum);
}

static int tegra_camera_set_bus_param(struct soc_camera_device *icd,
				      __u32 pixfmt)
{
	return 0;
}

static const struct soc_mbus_pixelfmt tegra_camera_formats[] = {
	{
		.fourcc			= V4L2_PIX_FMT_YUYV,
		.name			= "YUV 4:2:2 (YUYV 16 bit)",
		.bits_per_sample	= 16,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
	},
};

static int tegra_camera_get_formats(struct soc_camera_device *icd,
				    unsigned int idx,
				    struct soc_camera_format_xlate *xlate)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct device *dev = icd->dev.parent;
	int formats = 0;
	int ret;
	enum v4l2_mbus_pixelcode code;
	const struct soc_mbus_pixelfmt *fmt;

	ret = v4l2_subdev_call(sd, video, enum_mbus_fmt, idx, &code);
	if (IS_ERR_VALUE(ret))
		/* No more formats */
		return 0;

	fmt = soc_mbus_get_fmtdesc(code);
	if (!fmt) {
		dev_err(dev, "Invalid format code #%u: %d\n", idx, code);
		return 0;
	}

	switch (code) {
	case V4L2_MBUS_FMT_YUYV8_2X8:
		if (xlate)
			dev_info(dev, "Providing format %s packed\n",
				fmt->name);
		break;
	default:
		dev_info(dev, "Not supporting %s\n", fmt->name);
		return 0;
	}

	/* Generic pass-through */
	formats++;
	if (xlate) {
		xlate->host_fmt = fmt;
		xlate->code	= code;
		xlate++;
	}

	return formats;
}

static void tegra_camera_put_formats(struct soc_camera_device *icd)
{
	kfree(icd->host_priv);
	icd->host_priv = NULL;
}

static int tegra_camera_set_fmt(struct soc_camera_device *icd,
			      struct v4l2_format *f)
{
	struct device *dev = icd->dev.parent;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate = NULL;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	int ret;

	dev_dbg(dev, "In tegra_camera_set_fmt()\n");

	xlate = soc_camera_xlate_by_fourcc(icd, pix->pixelformat);
	if (!xlate) {
		dev_warn(dev, "Format %x not found\n", pix->pixelformat);
		return -EINVAL;
	}

	mf.width	= pix->width;
	mf.height	= pix->height;
	mf.field	= pix->field;
	mf.colorspace	= pix->colorspace;
	mf.code		= xlate->code;

	ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mf);
	if (IS_ERR_VALUE(ret)) {
		dev_warn(dev, "Failed to configure for format %x\n",
			 pix->pixelformat);
		return ret;
	}

	if (mf.code != xlate->code) {
		dev_warn(dev, "WTF! mf.code = %d, xlate->code = %d, mismatch\n",
			mf.code, xlate->code);
		return -EINVAL;
	}

	icd->sense = NULL;


	icd->user_width		= mf.width;
	icd->user_height	= mf.height;

	dev_dbg(dev, "Finished tegra_camera_set_fmt(), returning %d\n", ret);

	return ret;
}

static int tegra_camera_try_fmt(struct soc_camera_device *icd,
				struct v4l2_format *f)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	__u32 pixfmt = pix->pixelformat;
	int ret;

	dev_dbg(icd->dev.parent, "In tegra_camera_try_fmt()\n");

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (!xlate) {
		dev_warn(icd->dev.parent, "Format %x not found\n", pixfmt);
		return -EINVAL;
	}

	pix->bytesperline = soc_mbus_bytes_per_line(pix->width,
						    xlate->host_fmt);
	if (pix->bytesperline < 0)
		return pix->bytesperline;
	pix->sizeimage = pix->height * pix->bytesperline;

	/* limit to sensor capabilities */
	mf.width	= pix->width;
	mf.height	= pix->height;
	mf.field	= pix->field;
	mf.colorspace	= pix->colorspace;
	mf.code		= xlate->code;

	ret = v4l2_subdev_call(sd, video, try_mbus_fmt, &mf);
	if (IS_ERR_VALUE(ret))
		return ret;

	pix->width	= mf.width;
	pix->height	= mf.height;
	pix->colorspace	= mf.colorspace;

	switch (mf.field) {
	case V4L2_FIELD_ANY:
	case V4L2_FIELD_NONE:
		pix->field	= V4L2_FIELD_NONE;
		break;
	default:
		/* TODO: support interlaced at least in pass-through mode */
		dev_err(icd->dev.parent, "Field type %d unsupported.\n",
			mf.field);
		return -EINVAL;
	}

	dev_dbg(icd->dev.parent,
		"Finished tegra_camera_try_fmt(), returning %d\n", ret);

	return ret;
}

static int tegra_camera_reqbufs(struct soc_camera_device *icd,
				struct v4l2_requestbuffers *p)
{
	return 0;
}

static unsigned int tegra_camera_poll(struct file *file, poll_table *pt)
{
	struct soc_camera_device *icd = file->private_data;
	struct tegra_buffer *buf;

	buf = list_entry(icd->vb_vidq.stream.next,
			 struct tegra_buffer, vb.stream);

	poll_wait(file, &buf->vb.done, pt);

	if (buf->vb.state == VIDEOBUF_DONE ||
	    buf->vb.state == VIDEOBUF_ERROR)
		return POLLIN|POLLRDNORM;

	return 0;
}

static int tegra_camera_querycap(struct soc_camera_host *ici,
				 struct v4l2_capability *cap)
{
	strlcpy(cap->card, TEGRA_CAM_DRV_NAME, sizeof(cap->card));
	cap->version = TEGRA_CAM_VERSION_CODE;
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

	return 0;
}

static struct soc_camera_host_ops tegra_soc_camera_host_ops = {
	.owner		= THIS_MODULE,
	.add		= tegra_camera_add_device,
	.remove		= tegra_camera_remove_device,
	.get_formats	= tegra_camera_get_formats,
	.put_formats	= tegra_camera_put_formats,
	.set_fmt	= tegra_camera_set_fmt,
	.try_fmt	= tegra_camera_try_fmt,
	.init_videobuf	= tegra_camera_init_videobuf,
	.reqbufs	= tegra_camera_reqbufs,
	.poll		= tegra_camera_poll,
	.querycap	= tegra_camera_querycap,
	.set_bus_param	= tegra_camera_set_bus_param,
};

static int __devinit tegra_camera_probe(struct nvhost_device *ndev)
{
	struct tegra_camera_dev *pcdev;
	struct resource *res;
	u32 vi_base_phys;
	int err = 0;

	pcdev = kzalloc(sizeof(struct tegra_camera_dev), GFP_KERNEL);
	if (!pcdev) {
		dev_err(&ndev->dev, "Could not allocate pcdev\n");
		err = -ENOMEM;
		goto exit;
	}

	pcdev->pdata			= ndev->dev.platform_data;
	pcdev->ndev			= ndev;
	pcdev->soc_host.drv_name	= TEGRA_CAM_DRV_NAME;
	pcdev->soc_host.ops		= &tegra_soc_camera_host_ops;
	pcdev->soc_host.priv		= pcdev;
	pcdev->soc_host.v4l2_dev.dev	= &ndev->dev;
	pcdev->soc_host.nr		= ndev->id;
	INIT_LIST_HEAD(&pcdev->capture);
	INIT_WORK(&pcdev->work, tegra_camera_work);
	spin_lock_init(&pcdev->videobuf_queue_lock);
	mutex_init(&pcdev->work_mutex);

	nvhost_set_drvdata(ndev, pcdev);

	res = nvhost_get_resource_byname(ndev, IORESOURCE_MEM, "regs");
	if (!res) {
		dev_err(&ndev->dev,
			"Unable to allocate resources for device.\n");
		err = -EBUSY;
		goto exit_free_pcdev;
	}

	if (!request_mem_region(res->start, resource_size(res), ndev->name)) {
		dev_err(&ndev->dev,
			"Unable to request mem region for device.\n");
		err = -EBUSY;
		goto exit_free_pcdev;
	}

	vi_base_phys = res->start;
	pcdev->vi_base = ioremap_nocache(res->start, resource_size(res));
	if (!pcdev->vi_base) {
		dev_err(&ndev->dev, "Unable to grab IOs for device.\n");
		err = -EBUSY;
		goto exit_release_mem_region;
	}

	pm_suspend_ignore_children(&ndev->dev, true);
	pm_runtime_enable(&ndev->dev);
	pm_runtime_resume(&ndev->dev);

	err = soc_camera_host_register(&pcdev->soc_host);
	if (IS_ERR_VALUE(err))
		goto exit_iounmap;

	dev_notice(&ndev->dev, "Tegra camera driver loaded.\n");

	return err;

/*exit_host_unregister: */
/*	soc_camera_host_unregister(&pcdev->soc_host); */
exit_iounmap:
	pm_runtime_disable(&ndev->dev);
	iounmap(pcdev->vi_base);
exit_release_mem_region:
	release_mem_region(res->start, resource_size(res));
exit_free_pcdev:
	kfree(pcdev);
exit:
	return err;
}

static int __devexit tegra_camera_remove(struct nvhost_device *ndev)
{
	struct soc_camera_host *soc_host = to_soc_camera_host(&ndev->dev);
	struct tegra_camera_dev *pcdev = container_of(soc_host,
					struct tegra_camera_dev, soc_host);
	struct resource *res;

	res = nvhost_get_resource_byname(ndev, IORESOURCE_MEM, "regs");
	if (!res)
		return -EBUSY;

	soc_camera_host_unregister(soc_host);

	pm_runtime_disable(&ndev->dev);

	iounmap(pcdev->vi_base);

	release_mem_region(res->start, resource_size(res));

	kfree(pcdev);

	dev_info(&ndev->dev, "TEGRA Camera driver unloaded\n");

	return 0;
}

static int tegra_camera_runtime_nop(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops tegra_camera_dev_pm_ops = {
	.runtime_suspend = tegra_camera_runtime_nop,
	.runtime_resume = tegra_camera_runtime_nop,
};

static struct nvhost_driver tegra_camera_driver = {
	.driver	= {
		.name	= TEGRA_CAM_DRV_NAME,
		.owner	= THIS_MODULE,
		.pm	= &tegra_camera_dev_pm_ops,
	},
	.probe		= tegra_camera_probe,
	.remove		= __devexit_p(tegra_camera_remove),
};


static int __init tegra_camera_init(void)
{
	return nvhost_driver_register(&tegra_camera_driver);
}

static void __exit tegra_camera_exit(void)
{
	nvhost_driver_unregister(&tegra_camera_driver);
}

module_init(tegra_camera_init);
module_exit(tegra_camera_exit);

MODULE_DESCRIPTION("TEGRA SoC Camera Host driver");
MODULE_AUTHOR("Andrew Chew <achew@nvidia.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" TEGRA_CAM_DRV_NAME);
