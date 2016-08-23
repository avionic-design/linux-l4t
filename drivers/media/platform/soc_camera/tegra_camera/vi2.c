/*
 * Copyright (c) 2013-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>

#include <media/soc_camera.h>
#include <media/soc_mediabus.h>
#include <media/tegra_v4l2_camera.h>

#include <mach/clk.h>

#include "nvhost_syncpt.h"
#include "common.h"

#define TEGRA_SYNCPT_CSI_WAIT_TIMEOUT                   200

#define TEGRA_VI_CFG_VI_INCR_SYNCPT			0x000
#define		VI_MWA_REQ_DONE				(4 << 8)
#define		VI_MWB_REQ_DONE				(5 << 8)
#define		VI_MWA_ACK_DONE				(6 << 8)
#define		VI_MWB_ACK_DONE				(7 << 8)
#define		VI_ISPA_DONE				(8 << 8)
#define		VI_CSI_PPA_FRAME_START			(9 << 8)
#define		VI_CSI_PPB_FRAME_START			(10 << 8)
#define		VI_CSI_PPA_LINE_START			(11 << 8)
#define		VI_CSI_PPB_LINE_START			(12 << 8)

#define TEGRA_VI_CFG_VI_INCR_SYNCPT_CNTRL		0x004
#define TEGRA_VI_CFG_VI_INCR_SYNCPT_ERROR		0x008
#define TEGRA_VI_CFG_CTXSW				0x020
#define TEGRA_VI_CFG_INTSTATUS				0x024
#define TEGRA_VI_CFG_PWM_CONTROL			0x038
#define TEGRA_VI_CFG_PWM_HIGH_PULSE			0x03c
#define TEGRA_VI_CFG_PWM_LOW_PULSE			0x040
#define TEGRA_VI_CFG_PWM_SELECT_PULSE_A			0x044
#define TEGRA_VI_CFG_PWM_SELECT_PULSE_B			0x048
#define TEGRA_VI_CFG_PWM_SELECT_PULSE_C			0x04c
#define TEGRA_VI_CFG_PWM_SELECT_PULSE_D			0x050
#define TEGRA_VI_CFG_VGP1				0x064
#define TEGRA_VI_CFG_VGP2				0x068
#define TEGRA_VI_CFG_VGP3				0x06c
#define TEGRA_VI_CFG_VGP4				0x070
#define TEGRA_VI_CFG_VGP5				0x074
#define TEGRA_VI_CFG_VGP6				0x078
#define TEGRA_VI_CFG_INTERRUPT_MASK			0x08c
#define TEGRA_VI_CFG_INTERRUPT_TYPE_SELECT		0x090
#define TEGRA_VI_CFG_INTERRUPT_POLARITY_SELECT		0x094
#define TEGRA_VI_CFG_INTERRUPT_STATUS			0x098
#define TEGRA_VI_CFG_VGP_SYNCPT_CONFIG			0x0ac
#define TEGRA_VI_CFG_VI_SW_RESET			0x0b4
#define TEGRA_VI_CFG_CG_CTRL				0x0b8
#define TEGRA_VI_CFG_VI_MCCIF_FIFOCTRL			0x0e4
#define TEGRA_VI_CFG_TIMEOUT_WCOAL_VI			0x0e8
#define TEGRA_VI_CFG_DVFS				0x0f0
#define TEGRA_VI_CFG_RESERVE				0x0f4
#define TEGRA_VI_CFG_RESERVE_1				0x0f8

#define TEGRA_VI_CSI_0_SW_RESET				0x100
#define TEGRA_VI_CSI_0_SINGLE_SHOT			0x104
#define TEGRA_VI_CSI_0_SINGLE_SHOT_STATE_UPDATE		0x108
#define TEGRA_VI_CSI_0_IMAGE_DEF			0x10c
#define TEGRA_VI_CSI_0_RGB2Y_CTRL			0x110
#define TEGRA_VI_CSI_0_MEM_TILING			0x114
#define TEGRA_VI_CSI_0_CSI_IMAGE_SIZE			0x118
#define TEGRA_VI_CSI_0_CSI_IMAGE_SIZE_WC		0x11c
#define TEGRA_VI_CSI_0_CSI_IMAGE_DT			0x120
#define TEGRA_VI_CSI_0_SURFACE0_OFFSET_MSB		0x124
#define TEGRA_VI_CSI_0_SURFACE0_OFFSET_LSB		0x128
#define TEGRA_VI_CSI_0_SURFACE1_OFFSET_MSB		0x12c
#define TEGRA_VI_CSI_0_SURFACE1_OFFSET_LSB		0x130
#define TEGRA_VI_CSI_0_SURFACE2_OFFSET_MSB		0x134
#define TEGRA_VI_CSI_0_SURFACE2_OFFSET_LSB		0x138
#define TEGRA_VI_CSI_0_SURFACE0_BF_OFFSET_MSB		0x13c
#define TEGRA_VI_CSI_0_SURFACE0_BF_OFFSET_LSB		0x140
#define TEGRA_VI_CSI_0_SURFACE1_BF_OFFSET_MSB		0x144
#define TEGRA_VI_CSI_0_SURFACE1_BF_OFFSET_LSB		0x148
#define TEGRA_VI_CSI_0_SURFACE2_BF_OFFSET_MSB		0x14c
#define TEGRA_VI_CSI_0_SURFACE2_BF_OFFSET_LSB		0x150
#define TEGRA_VI_CSI_0_SURFACE0_STRIDE			0x154
#define TEGRA_VI_CSI_0_SURFACE1_STRIDE			0x158
#define TEGRA_VI_CSI_0_SURFACE2_STRIDE			0x15c
#define TEGRA_VI_CSI_0_SURFACE_HEIGHT0			0x160
#define TEGRA_VI_CSI_0_ISPINTF_CONFIG			0x164
#define TEGRA_VI_CSI_0_ERROR_STATUS			0x184
#define TEGRA_VI_CSI_0_ERROR_INT_MASK			0x188
#define TEGRA_VI_CSI_0_WD_CTRL				0x18c
#define TEGRA_VI_CSI_0_WD_PERIOD			0x190

#define TEGRA_VI_CSI_1_SW_RESET				0x200
#define TEGRA_VI_CSI_1_SINGLE_SHOT			0x204
#define TEGRA_VI_CSI_1_SINGLE_SHOT_STATE_UPDATE		0x208
#define TEGRA_VI_CSI_1_IMAGE_DEF			0x20c
#define TEGRA_VI_CSI_1_RGB2Y_CTRL			0x210
#define TEGRA_VI_CSI_1_MEM_TILING			0x214
#define TEGRA_VI_CSI_1_CSI_IMAGE_SIZE			0x218
#define TEGRA_VI_CSI_1_CSI_IMAGE_SIZE_WC		0x21c
#define TEGRA_VI_CSI_1_CSI_IMAGE_DT			0x220
#define TEGRA_VI_CSI_1_SURFACE0_OFFSET_MSB		0x224
#define TEGRA_VI_CSI_1_SURFACE0_OFFSET_LSB		0x228
#define TEGRA_VI_CSI_1_SURFACE1_OFFSET_MSB		0x22c
#define TEGRA_VI_CSI_1_SURFACE1_OFFSET_LSB		0x230
#define TEGRA_VI_CSI_1_SURFACE2_OFFSET_MSB		0x234
#define TEGRA_VI_CSI_1_SURFACE2_OFFSET_LSB		0x238
#define TEGRA_VI_CSI_1_SURFACE0_BF_OFFSET_MSB		0x23c
#define TEGRA_VI_CSI_1_SURFACE0_BF_OFFSET_LSB		0x240
#define TEGRA_VI_CSI_1_SURFACE1_BF_OFFSET_MSB		0x244
#define TEGRA_VI_CSI_1_SURFACE1_BF_OFFSET_LSB		0x248
#define TEGRA_VI_CSI_1_SURFACE2_BF_OFFSET_MSB		0x24c
#define TEGRA_VI_CSI_1_SURFACE2_BF_OFFSET_LSB		0x250
#define TEGRA_VI_CSI_1_SURFACE0_STRIDE			0x254
#define TEGRA_VI_CSI_1_SURFACE1_STRIDE			0x258
#define TEGRA_VI_CSI_1_SURFACE2_STRIDE			0x25c
#define TEGRA_VI_CSI_1_SURFACE_HEIGHT0			0x260
#define TEGRA_VI_CSI_1_ISPINTF_CONFIG			0x264
#define TEGRA_VI_CSI_1_ERROR_STATUS			0x284
#define TEGRA_VI_CSI_1_ERROR_INT_MASK			0x288
#define TEGRA_VI_CSI_1_WD_CTRL				0x28c
#define TEGRA_VI_CSI_1_WD_PERIOD			0x290

#define TEGRA_CSI_CSI_CAP_CIL				0x808
#define TEGRA_CSI_CSI_CAP_CSI				0x818
#define TEGRA_CSI_CSI_CAP_PP				0x828
#define TEGRA_CSI_INPUT_STREAM_A_CONTROL		0x838
#define TEGRA_CSI_PIXEL_STREAM_A_CONTROL0		0x83c
#define TEGRA_CSI_PIXEL_STREAM_A_CONTROL1		0x840
#define TEGRA_CSI_PIXEL_STREAM_A_GAP			0x844
#define TEGRA_CSI_PIXEL_STREAM_PPA_COMMAND		0x848
#define TEGRA_CSI_PIXEL_STREAM_A_EXPECTED_FRAME		0x84c
#define TEGRA_CSI_CSI_PIXEL_PARSER_A_INTERRUPT_MASK	0x850
#define TEGRA_CSI_CSI_PIXEL_PARSER_A_STATUS		0x854
#define TEGRA_CSI_CSI_SW_SENSOR_A_RESET			0x858
#define TEGRA_CSI_INPUT_STREAM_B_CONTROL		0x86c
#define TEGRA_CSI_PIXEL_STREAM_B_CONTROL0		0x870
#define TEGRA_CSI_PIXEL_STREAM_B_CONTROL1		0x874
#define TEGRA_CSI_PIXEL_STREAM_B_GAP			0x878
#define TEGRA_CSI_PIXEL_STREAM_PPB_COMMAND		0x87c
#define TEGRA_CSI_PIXEL_STREAM_B_EXPECTED_FRAME		0x880
#define TEGRA_CSI_CSI_PIXEL_PARSER_B_INTERRUPT_MASK	0x884
#define TEGRA_CSI_CSI_PIXEL_PARSER_B_STATUS		0x888
#define TEGRA_CSI_CSI_SW_SENSOR_B_RESET			0x88c
#define TEGRA_CSI_PHY_CIL_COMMAND			0x908
#define TEGRA_CSI_CIL_PAD_CONFIG0			0x90c

#define TEGRA_CSI_CILA_PAD_CONFIG0			0x92c
#define TEGRA_CSI_CILA_PAD_CONFIG1			0x930
#define TEGRA_CSI_PHY_CILA_CONTROL0			0x934
#define TEGRA_CSI_CSI_CIL_A_INTERRUPT_MASK		0x938
#define TEGRA_CSI_CSI_CIL_A_STATUS			0x93c
#define TEGRA_CSI_CSI_CILA_STATUS			0x940
#define TEGRA_CSI_CIL_A_ESCAPE_MODE_COMMAND		0x944
#define TEGRA_CSI_CIL_A_ESCAPE_MODE_DATA		0x948
#define TEGRA_CSI_CSICIL_SW_SENSOR_A_RESET		0x94c

#define TEGRA_CSI_CILB_PAD_CONFIG0			0x960
#define TEGRA_CSI_CILB_PAD_CONFIG1			0x964
#define TEGRA_CSI_PHY_CILB_CONTROL0			0x968
#define TEGRA_CSI_CSI_CIL_B_INTERRUPT_MASK		0x96c
#define TEGRA_CSI_CSI_CIL_B_STATUS			0x970
#define TEGRA_CSI_CSI_CILB_STATUS			0x974
#define TEGRA_CSI_CIL_B_ESCAPE_MODE_COMMAND		0x978
#define TEGRA_CSI_CIL_B_ESCAPE_MODE_DATA		0x97c
#define TEGRA_CSI_CSICIL_SW_SENSOR_B_RESET		0x980

#define TEGRA_CSI_CILC_PAD_CONFIG0			0x994
#define TEGRA_CSI_CILC_PAD_CONFIG1			0x998
#define TEGRA_CSI_PHY_CILC_CONTROL0			0x99c
#define TEGRA_CSI_CSI_CIL_C_INTERRUPT_MASK		0x9a0
#define TEGRA_CSI_CSI_CIL_C_STATUS			0x9a4
#define TEGRA_CSI_CSI_CILC_STATUS			0x9a8
#define TEGRA_CSI_CIL_C_ESCAPE_MODE_COMMAND		0x9ac
#define TEGRA_CSI_CIL_C_ESCAPE_MODE_DATA		0x9b0
#define TEGRA_CSI_CSICIL_SW_SENSOR_C_RESET		0x9b4

#define TEGRA_CSI_CILD_PAD_CONFIG0			0x9c8
#define TEGRA_CSI_CILD_PAD_CONFIG1			0x9cc
#define TEGRA_CSI_PHY_CILD_CONTROL0			0x9d0
#define TEGRA_CSI_CSI_CIL_D_INTERRUPT_MASK		0x9d4
#define TEGRA_CSI_CSI_CIL_D_STATUS			0x9d8
#define TEGRA_CSI_CSI_CILD_STATUS			0x9dc
#define TEGRA_CSI_CIL_D_ESCAPE_MODE_COMMAND		0x9ec
#define TEGRA_CSI_CIL_D_ESCAPE_MODE_DATA		0x9f0
#define TEGRA_CSI_CSICIL_SW_SENSOR_D_RESET		0x9f4

#define TEGRA_CSI_CILE_PAD_CONFIG0			0xa08
#define TEGRA_CSI_CILE_PAD_CONFIG1			0xa0c
#define TEGRA_CSI_PHY_CILE_CONTROL0			0xa10
#define TEGRA_CSI_CSI_CIL_E_INTERRUPT_MASK		0xa14
#define TEGRA_CSI_CSI_CIL_E_STATUS			0xa18
#define TEGRA_CSI_CIL_E_ESCAPE_MODE_COMMAND		0xa1c
#define TEGRA_CSI_CIL_E_ESCAPE_MODE_DATA		0xa20
#define TEGRA_CSI_CSICIL_SW_SENSOR_E_RESET		0xa24

#define TEGRA_CSI_PATTERN_GENERATOR_CTRL_A		0xa68
#define TEGRA_CSI_PG_BLANK_A				0xa6c
#define TEGRA_CSI_PG_PHASE_A				0xa70
#define TEGRA_CSI_PG_RED_FREQ_A				0xa74
#define TEGRA_CSI_PG_RED_FREQ_RATE_A			0xa78
#define TEGRA_CSI_PG_GREEN_FREQ_A			0xa7c
#define TEGRA_CSI_PG_GREEN_FREQ_RATE_A			0xa80
#define TEGRA_CSI_PG_BLUE_FREQ_A			0xa84
#define TEGRA_CSI_PG_BLUE_FREQ_RATE_A			0xa88

#define TEGRA_CSI_PATTERN_GENERATOR_CTRL_B		0xa9c
#define TEGRA_CSI_PG_BLANK_B				0xaa0
#define TEGRA_CSI_PG_PHASE_B				0xaa4
#define TEGRA_CSI_PG_RED_FREQ_B				0xaa8
#define TEGRA_CSI_PG_RED_FREQ_RATE_B			0xaac
#define TEGRA_CSI_PG_GREEN_FREQ_B			0xab0
#define TEGRA_CSI_PG_GREEN_FREQ_RATE_B			0xab4
#define TEGRA_CSI_PG_BLUE_FREQ_B			0xab8
#define TEGRA_CSI_PG_BLUE_FREQ_RATE_B			0xabc

#define TEGRA_CSI_DPCM_CTRL_A				0xad0
#define TEGRA_CSI_DPCM_CTRL_B				0xad4
#define TEGRA_CSI_STALL_COUNTER				0xae8
#define TEGRA_CSI_CSI_READONLY_STATUS			0xaec
#define TEGRA_CSI_CSI_SW_STATUS_RESET			0xaf0
#define TEGRA_CSI_CLKEN_OVERRIDE			0xaf4
#define TEGRA_CSI_DEBUG_CONTROL				0xaf8
#define TEGRA_CSI_DEBUG_COUNTER_0			0xafc
#define TEGRA_CSI_DEBUG_COUNTER_1			0xb00
#define TEGRA_CSI_DEBUG_COUNTER_2			0xb04

/* These go into the TEGRA_VI_CSI_n_IMAGE_DEF registers bits 23:16 */
#define TEGRA_IMAGE_FORMAT_T_L8				16
#define TEGRA_IMAGE_FORMAT_T_R16_I			32
#define TEGRA_IMAGE_FORMAT_T_B5G6R5			33
#define TEGRA_IMAGE_FORMAT_T_R5G6B5			34
#define TEGRA_IMAGE_FORMAT_T_A1B5G5R5			35
#define TEGRA_IMAGE_FORMAT_T_A1R5G5B5			36
#define TEGRA_IMAGE_FORMAT_T_B5G5R5A1			37
#define TEGRA_IMAGE_FORMAT_T_R5G5B5A1			38
#define TEGRA_IMAGE_FORMAT_T_A4B4G4R4			39
#define TEGRA_IMAGE_FORMAT_T_A4R4G4B4			40
#define TEGRA_IMAGE_FORMAT_T_B4G4R4A4			41
#define TEGRA_IMAGE_FORMAT_T_R4G4B4A4			42
#define TEGRA_IMAGE_FORMAT_T_A8B8G8R8			64
#define TEGRA_IMAGE_FORMAT_T_A8R8G8B8			65
#define TEGRA_IMAGE_FORMAT_T_B8G8R8A8			66
#define TEGRA_IMAGE_FORMAT_T_R8G8B8A8			67
#define TEGRA_IMAGE_FORMAT_T_A2B10G10R10		68
#define TEGRA_IMAGE_FORMAT_T_A2R10G10B10		69
#define TEGRA_IMAGE_FORMAT_T_B10G10R10A2		70
#define TEGRA_IMAGE_FORMAT_T_R10G10B10A2		71
#define TEGRA_IMAGE_FORMAT_T_A8Y8U8V8			193
#define TEGRA_IMAGE_FORMAT_T_V8U8Y8A8			194
#define TEGRA_IMAGE_FORMAT_T_A2Y10U10V10		197
#define TEGRA_IMAGE_FORMAT_T_V10U10Y10A2		198
#define TEGRA_IMAGE_FORMAT_T_Y8_U8__Y8_V8		200
#define TEGRA_IMAGE_FORMAT_T_Y8_V8__Y8_U8		201
#define TEGRA_IMAGE_FORMAT_T_U8_Y8__V8_Y8		202
#define TEGRA_IMAGE_FORMAT_T_V8_Y8__U8_Y8		203
#define TEGRA_IMAGE_FORMAT_T_Y8__U8__V8_N444		224
#define TEGRA_IMAGE_FORMAT_T_Y8__U8V8_N444		225
#define TEGRA_IMAGE_FORMAT_T_Y8__V8U8_N444		226
#define TEGRA_IMAGE_FORMAT_T_Y8__U8__V8_N422		227
#define TEGRA_IMAGE_FORMAT_T_Y8__U8V8_N422		228
#define TEGRA_IMAGE_FORMAT_T_Y8__V8U8_N422		229
#define TEGRA_IMAGE_FORMAT_T_Y8__U8__V8_N420		230
#define TEGRA_IMAGE_FORMAT_T_Y8__U8V8_N420		231
#define TEGRA_IMAGE_FORMAT_T_Y8__V8U8_N420		232
#define TEGRA_IMAGE_FORMAT_T_X2Lc10Lb10La10		233
#define TEGRA_IMAGE_FORMAT_T_A2R6R6R6R6R6		234

/* These go into the TEGRA_VI_CSI_n_CSI_IMAGE_DT registers bits 7:0 */
#define TEGRA_IMAGE_DT_YUV420_8				24
#define TEGRA_IMAGE_DT_YUV420_10			25
#define TEGRA_IMAGE_DT_YUV420CSPS_8			28
#define TEGRA_IMAGE_DT_YUV420CSPS_10			29
#define TEGRA_IMAGE_DT_YUV422_8				30
#define TEGRA_IMAGE_DT_YUV422_10			31
#define TEGRA_IMAGE_DT_RGB444				32
#define TEGRA_IMAGE_DT_RGB555				33
#define TEGRA_IMAGE_DT_RGB565				34
#define TEGRA_IMAGE_DT_RGB666				35
#define TEGRA_IMAGE_DT_RGB888				36
#define TEGRA_IMAGE_DT_RAW6				40
#define TEGRA_IMAGE_DT_RAW7				41
#define TEGRA_IMAGE_DT_RAW8				42
#define TEGRA_IMAGE_DT_RAW10				43
#define TEGRA_IMAGE_DT_RAW12				44
#define TEGRA_IMAGE_DT_RAW14				45

#define MIPI_CAL_CTRL		0x00
#define		STARTCAL	(1 << 0)
#define		CLKEN_OVR	(1 << 4)
#define MIPI_CAL_AUTOCAL_CTRL0	0x04
#define CIL_MIPI_CAL_STATUS	0x08
#define		CAL_DONE	(1 << 16)
#define CIL_MIPI_CAL_STATUS_2	0x0c
#define CILA_MIPI_CAL_CONFIG	0x14
#define		SELA		(1 << 21)
#define CILB_MIPI_CAL_CONFIG	0x18
#define		SELB		(1 << 21)
#define CILC_MIPI_CAL_CONFIG	0x1c
#define		SELC		(1 << 21)
#define CILD_MIPI_CAL_CONFIG	0x20
#define		SELD		(1 << 21)
#define CILE_MIPI_CAL_CONFIG	0x24
#define		SELE		(1 << 21)
#define DSIA_MIPI_CAL_CONFIG	0x38
#define		SELDSIA		(1 << 21)
#define DSIB_MIPI_CAL_CONFIG	0x3c
#define		SELDSIB		(1 << 21)
#define MIPI_BIAS_PAD_CFG0	0x58
#define		E_VCLAMP_REF	(1 << 0)
#define MIPI_BIAS_PAD_CFG1	0x5c
#define MIPI_BIAS_PAD_CFG2	0x60
#define		PDVREG		(1 << 1)
#define DSIA_MIPI_CAL_CONFIG_2	0x64
#define		CLKSELDSIA	(1 << 21)
#define DSIB_MIPI_CAL_CONFIG_2	0x68
#define		CLKSELDSIB	(1 << 21)
#define CILC_MIPI_CAL_CONFIG_2	0x6c
#define		CLKSELC		(1 << 21)
#define CILD_MIPI_CAL_CONFIG_2	0x70
#define		CLKSELD		(1 << 21)
#define CSIE_MIPI_CAL_CONFIG_2	0x74
#define		CLKSELE		(1 << 21)

#define MIPI_CAL_BASE	0x700e3000

static const struct regmap_config mipi_cal_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.cache_type = REGCACHE_RBTREE,
};

static int vi2_port_is_valid(int port)
{
	return (((port) >= TEGRA_CAMERA_PORT_CSI_A) &&
		((port) <= TEGRA_CAMERA_PORT_CSI_C));
}

/* Clock settings for camera */
static struct tegra_camera_clk vi2_clks0[] = {
	{
		.name = "vi",
		.freq = 408000000,
		.use_devname = 1,
	},
	{
		.name = "vi_sensor",
		.freq = 24000000,
	},
	{
		.name = "csi",
		.freq = 408000000,
		.use_devname = 1,
	},
	{
		.name = "isp",
		.freq = 0,
	},
	{
		.name = "csus",
		.freq = 0,
		.use_devname = 1,
	},
	{
		.name = "sclk",
		.freq = 80000000,
	},
	{
		.name = "emc",
		.freq = 300000000,
	},
	{
		.name = "cilab",
		.freq = 102000000,
		.use_devname = 1,
	},
	/* Always put "p11_d" at the end */
	{
		.name = "pll_d",
		.freq = 927000000,
	},
};

static struct tegra_camera_clk vi2_clks1[] = {
	{
		.name = "vi",
		.freq = 408000000,
		.use_devname = 1,
	},
	{
		.name = "vi_sensor2",
		.freq = 24000000,
	},
	{
		.name = "csi",
		.freq = 408000000,
		.use_devname = 1,
	},
	{
		.name = "isp",
		.freq = 0,
	},
	{
		.name = "sclk",
		.freq = 80000000,
	},
	{
		.name = "emc",
		.freq = 300000000,
	},
	{
		.name = "cilcd",
		.freq = 102000000,
		.use_devname = 1,
	},
	{
		.name = "cile",
		.freq = 102000000,
		.use_devname = 1,
	},
	/* Always put "p11_d" at the end */
	{
		.name = "pll_d",
		.freq = 927000000,
	},
};

#define MAX_DEVID_LENGTH	16

static int vi2_clks_init(struct tegra_camera_dev *cam, int port)
{
	struct platform_device *pdev = cam->ndev;
	struct tegra_camera_clk *clks;
	int i;

	switch (port) {
	case TEGRA_CAMERA_PORT_CSI_A:
		cam->num_clks = ARRAY_SIZE(vi2_clks0);
		cam->clks = vi2_clks0;
		break;
	case TEGRA_CAMERA_PORT_CSI_B:
	case TEGRA_CAMERA_PORT_CSI_C:
		cam->num_clks = ARRAY_SIZE(vi2_clks1);
		cam->clks = vi2_clks1;
		break;
	default:
		dev_err(&pdev->dev, "Wrong port number %d\n", port);
		return -ENODEV;
	}

	for (i = 0; i < cam->num_clks; i++) {
		clks = &cam->clks[i];

		if (clks->use_devname) {
			char devname[MAX_DEVID_LENGTH];
			snprintf(devname, MAX_DEVID_LENGTH,
				 "tegra_%s", dev_name(&pdev->dev));
			clks->clk = clk_get_sys(devname, clks->name);
		} else
			clks->clk = clk_get(&pdev->dev, clks->name);
		if (IS_ERR_OR_NULL(clks->clk)) {
			dev_err(&pdev->dev, "Failed to get clock %s.\n",
				clks->name);
			return PTR_ERR(clks->clk);
		}
	}

	return 0;
}

static void vi2_clks_deinit(struct tegra_camera_dev *cam)
{
	struct tegra_camera_clk *clks;
	int i;

	for (i = 0; i < cam->num_clks; i++) {
		clks = &cam->clks[i];
		if (clks->clk)
			clk_put(clks->clk);
	}
}

static void vi2_clks_enable(struct tegra_camera_dev *cam)
{
	struct tegra_camera_clk *clks;
	int i;

	for (i = 0; i < cam->num_clks - 1; i++) {
		clks = &cam->clks[i];
		if (clks->clk)
			clk_prepare_enable(clks->clk);
		if (clks->freq > 0)
			clk_set_rate(clks->clk, clks->freq);
	}

	if (cam->tpg_mode) {
		clks = &cam->clks[i];
		if (clks->clk) {
			clk_prepare_enable(clks->clk);
			if (clks->freq > 0)
				clk_set_rate(clks->clk, clks->freq);
			tegra_clk_cfg_ex(clks->clk,
					 TEGRA_CLK_PLLD_CSI_OUT_ENB, 1);
			tegra_clk_cfg_ex(clks->clk,
					 TEGRA_CLK_PLLD_DSI_OUT_ENB, 1);
			tegra_clk_cfg_ex(clks->clk,
					 TEGRA_CLK_MIPI_CSI_OUT_ENB, 0);
		}
	}
}

static void vi2_clks_disable(struct tegra_camera_dev *cam)
{
	struct tegra_camera_clk *clks;
	int i;

	for (i = 0; i < cam->num_clks - 1; i++) {
		clks = &cam->clks[i];
		if (clks->clk)
			clk_disable_unprepare(clks->clk);
	}

	if (cam->tpg_mode) {
		clks = &cam->clks[i];
		if (clks->clk) {
			tegra_clk_cfg_ex(clks->clk,
					 TEGRA_CLK_MIPI_CSI_OUT_ENB, 1);
			tegra_clk_cfg_ex(clks->clk,
					 TEGRA_CLK_PLLD_CSI_OUT_ENB, 0);
			tegra_clk_cfg_ex(clks->clk,
					 TEGRA_CLK_PLLD_DSI_OUT_ENB, 0);
			clk_disable_unprepare(clks->clk);
		}
	}
}

static void vi2_init_syncpts(struct tegra_camera_dev *cam)
{
	cam->syncpt_id_csi_a = nvhost_get_syncpt_client_managed("vi_csi_A");

	cam->syncpt_id_csi_b = nvhost_get_syncpt_client_managed("vi_csi_B");
}

static void vi2_free_syncpts(struct tegra_camera_dev *cam)
{
	nvhost_free_syncpt(cam->syncpt_id_csi_a);

	nvhost_free_syncpt(cam->syncpt_id_csi_b);
}

static void vi2_incr_syncpts(struct tegra_camera_dev *cam)
{
	return;
}

static void vi2_capture_clean(struct tegra_camera_dev *cam)
{
	/* Clean up status */
	TC_VI_REG_WT(cam, TEGRA_CSI_CSI_CIL_A_STATUS, 0xFFFFFFFF);
	TC_VI_REG_WT(cam, TEGRA_CSI_CSI_CIL_B_STATUS, 0xFFFFFFFF);
	TC_VI_REG_WT(cam, TEGRA_CSI_CSI_CIL_C_STATUS, 0xFFFFFFFF);
	TC_VI_REG_WT(cam, TEGRA_CSI_CSI_CIL_D_STATUS, 0xFFFFFFFF);
	TC_VI_REG_WT(cam, TEGRA_CSI_CSI_CIL_E_STATUS, 0xFFFFFFFF);
	TC_VI_REG_WT(cam, TEGRA_CSI_CSI_CILA_STATUS, 0xFFFFFFFF);
	TC_VI_REG_WT(cam, TEGRA_CSI_CSI_CILB_STATUS, 0xFFFFFFFF);
	TC_VI_REG_WT(cam, TEGRA_CSI_CSI_CILC_STATUS, 0xFFFFFFFF);
	TC_VI_REG_WT(cam, TEGRA_CSI_CSI_CILD_STATUS, 0xFFFFFFFF);
	TC_VI_REG_WT(cam, TEGRA_CSI_CSI_PIXEL_PARSER_A_STATUS, 0xFFFFFFFF);
	TC_VI_REG_WT(cam, TEGRA_CSI_CSI_PIXEL_PARSER_B_STATUS, 0xFFFFFFFF);
	TC_VI_REG_WT(cam, TEGRA_VI_CSI_0_ERROR_STATUS, 0xFFFFFFFF);
	TC_VI_REG_WT(cam, TEGRA_VI_CSI_1_ERROR_STATUS, 0xFFFFFFFF);
}

static int vi2_capture_get_format(struct tegra_camera_dev *cam,
				struct soc_camera_device *icd,
				int *format, int *data_type, int *image_size)
{
	if (cam->tpg_mode) {
		*data_type = TEGRA_IMAGE_DT_RGB888;
		*image_size = icd->user_width * 3;
	} else if ((icd->current_fmt->code == V4L2_MBUS_FMT_UYVY8_2X8) ||
		   (icd->current_fmt->code == V4L2_MBUS_FMT_VYUY8_2X8) ||
		   (icd->current_fmt->code == V4L2_MBUS_FMT_YUYV8_2X8) ||
		   (icd->current_fmt->code == V4L2_MBUS_FMT_YVYU8_2X8)) {
		*data_type = TEGRA_IMAGE_DT_YUV422_8;
		*image_size = icd->user_width * 2;
	} else if ((icd->current_fmt->code == V4L2_MBUS_FMT_SBGGR8_1X8) ||
		   (icd->current_fmt->code == V4L2_MBUS_FMT_SRGGB8_1X8)) {
		*data_type = TEGRA_IMAGE_DT_RAW8;
		*image_size = icd->user_width;
	} else if ((icd->current_fmt->code == V4L2_MBUS_FMT_SBGGR10_1X10) ||
		   (icd->current_fmt->code == V4L2_MBUS_FMT_SRGGB10_1X10)) {
		*data_type = TEGRA_IMAGE_DT_RAW10;
		*image_size = (icd->user_width * 10) >> 3;
	} else if (icd->current_fmt->code == V4L2_MBUS_FMT_RGB888_1X24) {
		*data_type = TEGRA_IMAGE_DT_RGB888;
		*image_size = icd->user_width * 3;
	} else {
		return -EINVAL;
	}

	switch (icd->current_fmt->host_fmt->fourcc) {
	case V4L2_PIX_FMT_UYVY:
		*format = TEGRA_IMAGE_FORMAT_T_U8_Y8__V8_Y8;
		break;
	case V4L2_PIX_FMT_VYUY:
		*format = TEGRA_IMAGE_FORMAT_T_V8_Y8__U8_Y8;
		break;
	case V4L2_PIX_FMT_YUYV:
		*format = TEGRA_IMAGE_FORMAT_T_Y8_U8__Y8_V8;
		break;
	case V4L2_PIX_FMT_YVYU:
		*format = TEGRA_IMAGE_FORMAT_T_Y8_V8__Y8_U8;
		break;
	case V4L2_PIX_FMT_GREY:
		*format = TEGRA_IMAGE_FORMAT_T_L8;
		break;
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SRGGB8:
		*format = TEGRA_IMAGE_FORMAT_T_L8;
		break;
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SRGGB10:
		*format = TEGRA_IMAGE_FORMAT_T_R16_I;
		break;
	case V4L2_PIX_FMT_RGB32:
		*format = TEGRA_IMAGE_FORMAT_T_A8B8G8R8;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int vi2_capture_setup_csi_0(struct tegra_camera_dev *cam,
				    struct soc_camera_device *icd)
{
	struct soc_camera_subdev_desc *ssdesc = &icd->sdesc->subdev_desc;
	struct tegra_camera_platform_data *pdata = ssdesc->drv_priv;
	int format = 0, data_type = 0, image_size = 0;
	u32 val;
	int err;

	/* Allow bad frames */
	TC_VI_REG_WT(cam, TEGRA_VI_CSI_0_SINGLE_SHOT_STATE_UPDATE, 0);

	/*
	 * PAD_CILA_PDVCLAMP 0, PAD_CILA_PDIO_CLK 0,
	 * PAD_CILA_PDIO 0, PAD_AB_BK_MODE 1
	 */
	TC_VI_REG_WT(cam, TEGRA_CSI_CILA_PAD_CONFIG0, 0x10000);

	/* PAD_CILB_PDVCLAMP 0, PAD_CILB_PDIO_CLK 0, PAD_CILB_PDIO 0 */
	TC_VI_REG_WT(cam, TEGRA_CSI_CILB_PAD_CONFIG0, 0x0);

	TC_VI_REG_WT(cam, TEGRA_CSI_CSI_CIL_A_INTERRUPT_MASK, 0x0);
	TC_VI_REG_WT(cam, TEGRA_CSI_CSI_CIL_B_INTERRUPT_MASK, 0x0);

#ifdef DEBUG
	TC_VI_REG_WT(cam, TEGRA_CSI_DEBUG_CONTROL,
			0x3 | (0x1 << 5) | (0x40 << 8));
#endif
	TC_VI_REG_WT(cam, TEGRA_CSI_PHY_CILA_CONTROL0,
		(pdata->continuous_clk ? BIT(6) : 0) | 0x6);
	TC_VI_REG_WT(cam, TEGRA_CSI_PHY_CILB_CONTROL0,
		(pdata->continuous_clk ? BIT(6) : 0) | 0x6);
	TC_VI_REG_WT(cam, TEGRA_CSI_PIXEL_STREAM_PPA_COMMAND, 0xf007);
	TC_VI_REG_WT(cam, TEGRA_CSI_CSI_PIXEL_PARSER_A_INTERRUPT_MASK, 0x0);
	TC_VI_REG_WT(cam, TEGRA_CSI_PIXEL_STREAM_A_CONTROL0, 0x280301f0);
	TC_VI_REG_WT(cam, TEGRA_CSI_PIXEL_STREAM_PPA_COMMAND, 0xf007);
	TC_VI_REG_WT(cam, TEGRA_CSI_PIXEL_STREAM_A_CONTROL1, 0x11);
	TC_VI_REG_WT(cam, TEGRA_CSI_PIXEL_STREAM_A_GAP, 0x140000);
	TC_VI_REG_WT(cam, TEGRA_CSI_PIXEL_STREAM_A_EXPECTED_FRAME, 0x0);

	TC_VI_REG_WT(cam, TEGRA_CSI_INPUT_STREAM_A_CONTROL,
			0x3f0000 | (pdata->lanes - 1));

	/* Shared register */
	val = TC_VI_REG_RD(cam, TEGRA_CSI_PHY_CIL_COMMAND);
	if (pdata->lanes == 4)
		TC_VI_REG_WT(cam, TEGRA_CSI_PHY_CIL_COMMAND,
			     (val & 0xFFFF0000) | 0x0101);
	else
		TC_VI_REG_WT(cam, TEGRA_CSI_PHY_CIL_COMMAND,
			     (val & 0xFFFF0000) | 0x0201);

	if (cam->tpg_mode) {
		TC_VI_REG_WT(cam, TEGRA_CSI_PATTERN_GENERATOR_CTRL_A,
				((cam->tpg_mode - 1) << 2) | 0x1);
		TC_VI_REG_WT(cam, TEGRA_CSI_PG_PHASE_A, 0x0);
		TC_VI_REG_WT(cam, TEGRA_CSI_PG_RED_FREQ_A, 0x100010);
		TC_VI_REG_WT(cam, TEGRA_CSI_PG_RED_FREQ_RATE_A, 0x0);
		TC_VI_REG_WT(cam, TEGRA_CSI_PG_GREEN_FREQ_A, 0x100010);
		TC_VI_REG_WT(cam, TEGRA_CSI_PG_GREEN_FREQ_RATE_A, 0x0);
		TC_VI_REG_WT(cam, TEGRA_CSI_PG_BLUE_FREQ_A, 0x100010);
		TC_VI_REG_WT(cam, TEGRA_CSI_PG_BLUE_FREQ_RATE_A, 0x0);
		TC_VI_REG_WT(cam, TEGRA_CSI_PHY_CIL_COMMAND, 0x22020202);
	}

	err = vi2_capture_get_format(cam, icd, &format,
				&data_type, &image_size);
	if (err)
		return err;

	TC_VI_REG_WT(cam, TEGRA_VI_CSI_0_IMAGE_DEF, (format << 16) | 0x1);

	TC_VI_REG_WT(cam, TEGRA_VI_CSI_0_CSI_IMAGE_DT, data_type);

	TC_VI_REG_WT(cam, TEGRA_VI_CSI_0_CSI_IMAGE_SIZE_WC, image_size);

	TC_VI_REG_WT(cam, TEGRA_VI_CSI_0_CSI_IMAGE_SIZE,
			(icd->user_height << 16) | icd->user_width);

	/* Start pixel parser in single shot mode at beginning */
	TC_VI_REG_WT(cam, TEGRA_CSI_PIXEL_STREAM_PPA_COMMAND, 0xf005);

	return 0;
}

static int vi2_capture_setup_csi_1(struct tegra_camera_dev *cam,
				     struct soc_camera_device *icd)
{
	struct soc_camera_subdev_desc *ssdesc = &icd->sdesc->subdev_desc;
	struct tegra_camera_platform_data *pdata = ssdesc->drv_priv;
	int format = 0, data_type = 0, image_size = 0;
	u32 val;
	int err;

	/* Allow bad frames */
	TC_VI_REG_WT(cam, TEGRA_VI_CSI_1_SINGLE_SHOT_STATE_UPDATE, 0);

	/*
	 * PAD_CILC_PDVCLAMP 0, PAD_CILC_PDIO_CLK 0,
	 * PAD_CILC_PDIO 0, PAD_CD_BK_MODE 1
	 */
	TC_VI_REG_WT(cam, TEGRA_CSI_CILC_PAD_CONFIG0, 0x10000);

	/* PAD_CILD_PDVCLAMP 0, PAD_CILD_PDIO_CLK 0, PAD_CILD_PDIO 0 */
	TC_VI_REG_WT(cam, TEGRA_CSI_CILD_PAD_CONFIG0, 0x0);

	/* PAD_CILE_PDVCLAMP 0, PAD_CILE_PDIO_CLK 0, PAD_CILE_PDIO 0 */
	TC_VI_REG_WT(cam, TEGRA_CSI_CILE_PAD_CONFIG0, 0x0);

	TC_VI_REG_WT(cam, TEGRA_CSI_CSI_CIL_C_INTERRUPT_MASK, 0x0);
	TC_VI_REG_WT(cam, TEGRA_CSI_CSI_CIL_D_INTERRUPT_MASK, 0x0);
	TC_VI_REG_WT(cam, TEGRA_CSI_CSI_CIL_E_INTERRUPT_MASK, 0x0);

#ifdef DEBUG
	TC_VI_REG_WT(cam, TEGRA_CSI_DEBUG_CONTROL,
			0x5 | (0x1 << 5) | (0x50 << 8));
#endif

	if (pdata->port == TEGRA_CAMERA_PORT_CSI_B) {
		TC_VI_REG_WT(cam, TEGRA_CSI_PHY_CILC_CONTROL0,
			(pdata->continuous_clk ? BIT(6) : 0) | 0x6);
		TC_VI_REG_WT(cam, TEGRA_CSI_PHY_CILD_CONTROL0,
			(pdata->continuous_clk ? BIT(6) : 0) | 0x6);
	} else if (pdata->port == TEGRA_CAMERA_PORT_CSI_C)
		TC_VI_REG_WT(cam, TEGRA_CSI_PHY_CILE_CONTROL0,
			(pdata->continuous_clk ? BIT(6) : 0) | 0x6);

	TC_VI_REG_WT(cam, TEGRA_CSI_PIXEL_STREAM_PPB_COMMAND, 0xf007);
	TC_VI_REG_WT(cam, TEGRA_CSI_CSI_PIXEL_PARSER_B_INTERRUPT_MASK, 0x0);
	TC_VI_REG_WT(cam, TEGRA_CSI_PIXEL_STREAM_B_CONTROL0, 0x280301f1);
	TC_VI_REG_WT(cam, TEGRA_CSI_PIXEL_STREAM_PPB_COMMAND, 0xf007);
	TC_VI_REG_WT(cam, TEGRA_CSI_PIXEL_STREAM_B_CONTROL1, 0x11);
	TC_VI_REG_WT(cam, TEGRA_CSI_PIXEL_STREAM_B_GAP, 0x140000);
	TC_VI_REG_WT(cam, TEGRA_CSI_PIXEL_STREAM_B_EXPECTED_FRAME, 0x0);

	TC_VI_REG_WT(cam, TEGRA_CSI_INPUT_STREAM_B_CONTROL,
			0x3f0000 | (pdata->lanes - 1));

	/* Shared register */
	val = TC_VI_REG_RD(cam, TEGRA_CSI_PHY_CIL_COMMAND);
	if (pdata->lanes == 4)
		TC_VI_REG_WT(cam, TEGRA_CSI_PHY_CIL_COMMAND,
			     (val & 0x0000FFFF) | 0x21010000);
	else if (pdata->lanes == 1 && pdata->port == TEGRA_CAMERA_PORT_CSI_C)
		TC_VI_REG_WT(cam, TEGRA_CSI_PHY_CIL_COMMAND,
			     (val & 0x0000FFFF) | 0x12020000);
	else
		TC_VI_REG_WT(cam, TEGRA_CSI_PHY_CIL_COMMAND,
			     (val & 0x0000FFFF) | 0x22010000);

	if (cam->tpg_mode) {
		TC_VI_REG_WT(cam, TEGRA_CSI_PATTERN_GENERATOR_CTRL_B,
				((cam->tpg_mode - 1) << 2) | 0x1);
		TC_VI_REG_WT(cam, TEGRA_CSI_PG_PHASE_B, 0x0);
		TC_VI_REG_WT(cam, TEGRA_CSI_PG_RED_FREQ_B, 0x100010);
		TC_VI_REG_WT(cam, TEGRA_CSI_PG_RED_FREQ_RATE_B, 0x0);
		TC_VI_REG_WT(cam, TEGRA_CSI_PG_GREEN_FREQ_B, 0x100010);
		TC_VI_REG_WT(cam, TEGRA_CSI_PG_GREEN_FREQ_RATE_B, 0x0);
		TC_VI_REG_WT(cam, TEGRA_CSI_PG_BLUE_FREQ_B, 0x100010);
		TC_VI_REG_WT(cam, TEGRA_CSI_PG_BLUE_FREQ_RATE_B, 0x0);
		TC_VI_REG_WT(cam, TEGRA_CSI_PHY_CIL_COMMAND, 0x22020202);
	}

	err = vi2_capture_get_format(cam, icd, &format,
				&data_type, &image_size);
	if (err)
		return err;

	TC_VI_REG_WT(cam, TEGRA_VI_CSI_1_IMAGE_DEF, (format << 16) | 0x1);

	TC_VI_REG_WT(cam, TEGRA_VI_CSI_1_CSI_IMAGE_DT, data_type);

	TC_VI_REG_WT(cam, TEGRA_VI_CSI_1_CSI_IMAGE_SIZE_WC, image_size);

	TC_VI_REG_WT(cam, TEGRA_VI_CSI_1_CSI_IMAGE_SIZE,
			(icd->user_height << 16) | icd->user_width);

	/* Start pixel parser in single shot mode at beginning */
	TC_VI_REG_WT(cam, TEGRA_CSI_PIXEL_STREAM_PPB_COMMAND, 0xf005);

	return 0;
}

static int vi2_capture_setup(struct tegra_camera_dev *cam,
			     struct tegra_camera_buffer *buf)
{
	struct soc_camera_device *icd = buf->icd;
	struct soc_camera_subdev_desc *ssdesc = &icd->sdesc->subdev_desc;
	struct tegra_camera_platform_data *pdata = ssdesc->drv_priv;
	int port = pdata->port;

	/* Skip VI2/CSI2 setup for second and later frame capture */
	if (!cam->sof)
		return 0;

	/* Setup registers for CSI-A and CSI-B inputs */
	if (port == TEGRA_CAMERA_PORT_CSI_A)
		return vi2_capture_setup_csi_0(cam, icd);
	else if (port == TEGRA_CAMERA_PORT_CSI_B ||
			port == TEGRA_CAMERA_PORT_CSI_C)
		return vi2_capture_setup_csi_1(cam, icd);
	else
		return -ENODEV;
}

static s32 vi2_bytes_per_line(u32 width, const struct soc_mbus_pixelfmt *mf)
{
	s32 bytes_per_line = soc_mbus_bytes_per_line(width, mf);

	if (bytes_per_line % 64)
		bytes_per_line = bytes_per_line + (64 - (bytes_per_line % 64));

	return bytes_per_line;
}

static int vi2_capture_buffer_setup(struct tegra_camera_dev *cam,
			struct tegra_camera_buffer *buf)
{
	struct soc_camera_device *icd = buf->icd;
	int bytes_per_line = vi2_bytes_per_line(icd->user_width,
						icd->current_fmt->host_fmt);
	struct soc_camera_subdev_desc *ssdesc = &icd->sdesc->subdev_desc;
	struct tegra_camera_platform_data *pdata = ssdesc->drv_priv;
	int port = pdata->port;

	switch (icd->current_fmt->host_fmt->fourcc) {
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		/* FIXME: Setup YUV buffer */

	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SRGGB8:
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SRGGB10:
	case V4L2_PIX_FMT_RGB32:
		if (port == TEGRA_CAMERA_PORT_CSI_A) {
			switch (buf->output_channel) {
			case 0:
				TC_VI_REG_WT(cam,
					     TEGRA_VI_CSI_0_SURFACE0_OFFSET_MSB,
					     0x0);
				TC_VI_REG_WT(cam,
					     TEGRA_VI_CSI_0_SURFACE0_OFFSET_LSB,
					     buf->buffer_addr);
				TC_VI_REG_WT(cam,
					     TEGRA_VI_CSI_0_SURFACE0_STRIDE,
					     bytes_per_line);
				break;
			case 1:
				TC_VI_REG_WT(cam,
					     TEGRA_VI_CSI_0_SURFACE1_OFFSET_MSB,
					     0x0);
				TC_VI_REG_WT(cam,
					     TEGRA_VI_CSI_0_SURFACE1_OFFSET_LSB,
					     buf->buffer_addr);
				TC_VI_REG_WT(cam,
					     TEGRA_VI_CSI_0_SURFACE1_STRIDE,
					     bytes_per_line);
				break;
			case 2:
				TC_VI_REG_WT(cam,
					     TEGRA_VI_CSI_0_SURFACE2_OFFSET_MSB,
					     0x0);
				TC_VI_REG_WT(cam,
					     TEGRA_VI_CSI_0_SURFACE2_OFFSET_LSB,
					     buf->buffer_addr);
				TC_VI_REG_WT(cam,
					     TEGRA_VI_CSI_0_SURFACE2_STRIDE,
					     bytes_per_line);
				break;
			}
		} else if (port == TEGRA_CAMERA_PORT_CSI_B ||
			port == TEGRA_CAMERA_PORT_CSI_C) {
			switch (buf->output_channel) {
			case 0:
				TC_VI_REG_WT(cam,
					     TEGRA_VI_CSI_1_SURFACE0_OFFSET_MSB,
					     0x0);
				TC_VI_REG_WT(cam,
					     TEGRA_VI_CSI_1_SURFACE0_OFFSET_LSB,
					     buf->buffer_addr);
				TC_VI_REG_WT(cam,
					     TEGRA_VI_CSI_1_SURFACE0_STRIDE,
					     bytes_per_line);
				break;
			case 1:
				TC_VI_REG_WT(cam,
					     TEGRA_VI_CSI_1_SURFACE1_OFFSET_MSB,
					     0x0);
				TC_VI_REG_WT(cam,
					     TEGRA_VI_CSI_1_SURFACE1_OFFSET_LSB,
					     buf->buffer_addr);
				TC_VI_REG_WT(cam,
					     TEGRA_VI_CSI_1_SURFACE1_STRIDE,
					     bytes_per_line);
				break;
			case 2:
				TC_VI_REG_WT(cam,
					     TEGRA_VI_CSI_1_SURFACE2_OFFSET_MSB,
					     0x0);
				TC_VI_REG_WT(cam,
					     TEGRA_VI_CSI_1_SURFACE2_OFFSET_LSB,
					     buf->buffer_addr);
				TC_VI_REG_WT(cam,
					     TEGRA_VI_CSI_1_SURFACE2_STRIDE,
					     bytes_per_line);
				break;
			}
		}
		break;

	default:
		dev_err(&cam->ndev->dev, "Wrong host format %d\n",
			icd->current_fmt->host_fmt->fourcc);
		return -EINVAL;
	}

	return 0;
}

static void vi2_capture_error_status(struct tegra_camera_dev *cam)
{
	u32 val;

#ifdef DEBUG
	val = TC_VI_REG_RD(cam, TEGRA_CSI_DEBUG_COUNTER_0);
	pr_err("TEGRA_CSI_DEBUG_COUNTER_0 0x%08x\n", val);
#endif
	val = TC_VI_REG_RD(cam, TEGRA_CSI_CSI_CIL_A_STATUS);
	pr_err("TEGRA_CSI_CSI_CIL_A_STATUS 0x%08x\n", val);
	val = TC_VI_REG_RD(cam, TEGRA_CSI_CSI_CILA_STATUS);
	pr_err("TEGRA_CSI_CSI_CILA_STATUS 0x%08x\n", val);
	val = TC_VI_REG_RD(cam, TEGRA_CSI_CSI_CIL_B_STATUS);
	pr_err("TEGRA_CSI_CSI_CIL_B_STATUS 0x%08x\n", val);
	val = TC_VI_REG_RD(cam, TEGRA_CSI_CSI_CIL_C_STATUS);
	pr_err("TEGRA_CSI_CSI_CIL_C_STATUS 0x%08x\n", val);
	val = TC_VI_REG_RD(cam, TEGRA_CSI_CSI_CIL_D_STATUS);
	pr_err("TEGRA_CSI_CSI_CIL_D_STATUS 0x%08x\n", val);
	val = TC_VI_REG_RD(cam, TEGRA_CSI_CSI_CIL_E_STATUS);
	pr_err("TEGRA_CSI_CSI_CIL_E_STATUS 0x%08x\n", val);
	val = TC_VI_REG_RD(cam, TEGRA_CSI_CSI_PIXEL_PARSER_A_STATUS);
	pr_err("TEGRA_CSI_CSI_PIXEL_PARSER_A_STATUS 0x%08x\n", val);
	val = TC_VI_REG_RD(cam, TEGRA_CSI_CSI_PIXEL_PARSER_B_STATUS);
	pr_err("TEGRA_CSI_CSI_PIXEL_PARSER_B_STATUS 0x%08x\n", val);
	val = TC_VI_REG_RD(cam, TEGRA_VI_CSI_0_ERROR_STATUS);
	pr_err("TEGRA_VI_CSI_0_ERROR_STATUS 0x%08x\n", val);
	val = TC_VI_REG_RD(cam, TEGRA_VI_CSI_1_ERROR_STATUS);
	pr_err("TEGRA_VI_CSI_1_ERROR_STATUS 0x%08x\n", val);
}

static int vi2_capture_start(struct tegra_camera_dev *cam,
			     struct tegra_camera_buffer *buf)
{
	struct soc_camera_device *icd = buf->icd;
	struct soc_camera_subdev_desc *ssdesc = &icd->sdesc->subdev_desc;
	struct tegra_camera_platform_data *pdata = ssdesc->drv_priv;
	int port = pdata->port;
	int err;
	u32 val;

	err = vi2_capture_buffer_setup(cam, buf);
	if (err < 0)
		return err;

	if (port == TEGRA_CAMERA_PORT_CSI_A) {
		if (!nvhost_syncpt_read_ext_check(cam->ndev,
					cam->syncpt_id_csi_a, &val))
			cam->syncpt_csi_a = nvhost_syncpt_incr_max_ext(
						cam->ndev,
						cam->syncpt_id_csi_a, 1);

		TC_VI_REG_WT(cam, TEGRA_VI_CFG_VI_INCR_SYNCPT,
			     VI_CSI_PPA_FRAME_START | cam->syncpt_id_csi_a);
		TC_VI_REG_WT(cam, TEGRA_VI_CSI_0_SINGLE_SHOT, 0x1);
	} else if (port == TEGRA_CAMERA_PORT_CSI_B ||
			port == TEGRA_CAMERA_PORT_CSI_C) {
		if (!nvhost_syncpt_read_ext_check(cam->ndev,
					cam->syncpt_id_csi_b, &val))
			cam->syncpt_csi_b = nvhost_syncpt_incr_max_ext(
						cam->ndev,
						cam->syncpt_id_csi_b, 1);

		TC_VI_REG_WT(cam, TEGRA_VI_CFG_VI_INCR_SYNCPT,
			     VI_CSI_PPB_FRAME_START | cam->syncpt_id_csi_b);
		TC_VI_REG_WT(cam, TEGRA_VI_CSI_1_SINGLE_SHOT, 0x1);
	}

	return err;
}

static int vi2_capture_wait(struct tegra_camera_dev *cam,
			    struct tegra_camera_buffer *buf)
{
	struct soc_camera_device *icd = buf->icd;
	struct soc_camera_subdev_desc *ssdesc = &icd->sdesc->subdev_desc;
	struct tegra_camera_platform_data *pdata = ssdesc->drv_priv;
	int port = pdata->port;
	int err = 0;

	/* Only wait on CSI frame end syncpt if we're using CSI. */
	if (port == TEGRA_CAMERA_PORT_CSI_A) {
		err = nvhost_syncpt_wait_timeout_ext(cam->ndev,
				cam->syncpt_id_csi_a,
				cam->syncpt_csi_a,
				TEGRA_SYNCPT_CSI_WAIT_TIMEOUT,
				NULL,
				NULL);
	} else if (port == TEGRA_CAMERA_PORT_CSI_B ||
			port == TEGRA_CAMERA_PORT_CSI_C) {
		err = nvhost_syncpt_wait_timeout_ext(cam->ndev,
				cam->syncpt_id_csi_b,
				cam->syncpt_csi_b,
				TEGRA_SYNCPT_CSI_WAIT_TIMEOUT,
				NULL,
				NULL);
	}

	/* Mark SOF flag to Zero after we captured the FIRST frame */
	if (cam->sof)
		cam->sof = 0;

	/* Capture syncpt timeout err, then dump error status */
	if (err) {
		if (port == TEGRA_CAMERA_PORT_CSI_A)
			dev_err(&cam->ndev->dev,
				"CSI_A syncpt timeout, syncpt = %d, err = %d\n",
				cam->syncpt_csi_a, err);
		else if (port == TEGRA_CAMERA_PORT_CSI_B ||
				port == TEGRA_CAMERA_PORT_CSI_C)
			dev_err(&cam->ndev->dev,
				"CSI_B/CSI_C syncpt timeout, syncpt = %d, err = %d\n",
				cam->syncpt_csi_b, err);
		vi2_capture_error_status(cam);
	}

	return err;
}

static int vi2_capture_done(struct tegra_camera_dev *cam, int port)
{
	u32 val;
	int err = 0;

	if (port == TEGRA_CAMERA_PORT_CSI_A) {
		if (!nvhost_syncpt_read_ext_check(cam->ndev,
						  cam->syncpt_id_csi_a, &val))
			cam->syncpt_csi_a = nvhost_syncpt_incr_max_ext(
						cam->ndev,
						cam->syncpt_id_csi_a, 1);

		/*
		 * Make sure recieve VI_MWA_ACK_DONE of the last frame before
		 * stop and dequeue buffer, otherwise MC error will shows up
		 * for the last frame.
		 */
		TC_VI_REG_WT(cam, TEGRA_VI_CFG_VI_INCR_SYNCPT,
			     VI_MWA_ACK_DONE | cam->syncpt_id_csi_a);

		/*
		 * Ignore error here and just stop pixel parser after waiting,
		 * even if it's timeout
		 */
		err = nvhost_syncpt_wait_timeout_ext(cam->ndev,
				cam->syncpt_id_csi_a,
				cam->syncpt_csi_a,
				TEGRA_SYNCPT_CSI_WAIT_TIMEOUT,
				NULL,
				NULL);
	} else if (port == TEGRA_CAMERA_PORT_CSI_B ||
			port == TEGRA_CAMERA_PORT_CSI_C) {
		if (!nvhost_syncpt_read_ext_check(cam->ndev,
						  cam->syncpt_id_csi_b, &val))
			cam->syncpt_csi_b = nvhost_syncpt_incr_max_ext(
						cam->ndev,
						cam->syncpt_id_csi_b, 1);

		/*
		 * Make sure recieve VI_MWB_ACK_DONE of the last frame before
		 * stop and dequeue buffer, otherwise MC error will shows up
		 * for the last frame.
		 */
		TC_VI_REG_WT(cam, TEGRA_VI_CFG_VI_INCR_SYNCPT,
			     VI_MWB_ACK_DONE | cam->syncpt_id_csi_b);

		/*
		 * Ignore error here and just stop pixel parser after waiting,
		 * even if it's timeout
		 */
		err = nvhost_syncpt_wait_timeout_ext(cam->ndev,
				cam->syncpt_id_csi_b,
				cam->syncpt_csi_b,
				TEGRA_SYNCPT_CSI_WAIT_TIMEOUT,
				NULL,
				NULL);
	}

	return err;
}

static int vi2_capture_stop(struct tegra_camera_dev *cam, int port)
{
	u32 reg = (port == TEGRA_CAMERA_PORT_CSI_A) ?
		  TEGRA_CSI_PIXEL_STREAM_PPA_COMMAND :
		  TEGRA_CSI_PIXEL_STREAM_PPB_COMMAND;

	TC_VI_REG_WT(cam, reg, 0xf002);

	return 0;
}

/* Reset VI2/CSI2 when activating, no sepecial ops for deactiving  */
static void vi2_sw_reset(struct tegra_camera_dev *cam)
{
	/* T12_CG_2ND_LEVEL_EN */
	TC_VI_REG_WT(cam, TEGRA_VI_CFG_CG_CTRL, 1);

	TC_VI_REG_WT(cam, TEGRA_CSI_CLKEN_OVERRIDE, 0x0);

	udelay(10);
}

static int vi2_mipi_calibration(struct tegra_camera_dev *cam,
				struct tegra_camera_buffer *buf)
{
	void __iomem *mipi_cal;
	struct regmap *regs;
	struct platform_device *pdev = cam->ndev;
	struct soc_camera_device *icd = buf->icd;
	struct soc_camera_subdev_desc *ssdesc = &icd->sdesc->subdev_desc;
	struct tegra_camera_platform_data *pdata = ssdesc->drv_priv;
	int port = pdata->port;
	u32 val;
	struct clk *clk_mipi_cal = NULL, *clk_72mhz = NULL;
	int retry = 500;

	/* TPG mode doesn't need any calibration */
	if (cam->tpg_mode)
		return 0;

	/* Get clks for MIPI Calibration */
	clk_mipi_cal = clk_get_sys("mipi-cal", NULL);
	if (IS_ERR_OR_NULL(clk_mipi_cal)) {
		dev_err(&pdev->dev, "cannot get mipi-cal clk.\n");
		return PTR_ERR(clk_mipi_cal);
	}

	clk_72mhz = clk_get_sys("clk72mhz", NULL);
	if (IS_ERR_OR_NULL(clk_72mhz)) {
		dev_err(&pdev->dev, "cannot get 72MHz clk.\n");
		return PTR_ERR(clk_72mhz);
	}

	/* Map registers */
	mipi_cal = ioremap(MIPI_CAL_BASE, 0x100);
	if (!mipi_cal)
		return -ENOMEM;

	regs = regmap_init_mmio(&pdev->dev, mipi_cal, &mipi_cal_config);
	if (IS_ERR(regs)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		iounmap(mipi_cal);
		return PTR_ERR(regs);
	}

	/* Enable MIPI Calibration clocks */
	if (clk_mipi_cal)
		clk_prepare_enable(clk_mipi_cal);
	if (clk_72mhz)
		clk_prepare_enable(clk_72mhz);

	/* MIPI_CAL_CLKEN_OVR = 1 */
	regmap_update_bits(regs, MIPI_CAL_CTRL, CLKEN_OVR, CLKEN_OVR);

	/* Clear MIPI CAL status flags */
	regmap_write(regs, CIL_MIPI_CAL_STATUS, 0xF1F10000);
	regmap_update_bits(regs, DSIA_MIPI_CAL_CONFIG, SELDSIA, 0);
	regmap_update_bits(regs, DSIB_MIPI_CAL_CONFIG, SELDSIB, 0);
	regmap_update_bits(regs, MIPI_BIAS_PAD_CFG0,
			   E_VCLAMP_REF, E_VCLAMP_REF);
	regmap_update_bits(regs, MIPI_BIAS_PAD_CFG2, PDVREG, 0);
	regmap_update_bits(regs, CILA_MIPI_CAL_CONFIG, SELA, 0);
	regmap_update_bits(regs, DSIA_MIPI_CAL_CONFIG_2, CLKSELDSIA, 0);
	regmap_update_bits(regs, CILB_MIPI_CAL_CONFIG, SELB, 0);
	regmap_update_bits(regs, DSIB_MIPI_CAL_CONFIG_2, CLKSELDSIB, 0);
	regmap_update_bits(regs, CILC_MIPI_CAL_CONFIG, SELC, 0);
	regmap_update_bits(regs, CILC_MIPI_CAL_CONFIG_2, CLKSELC, 0);
	regmap_update_bits(regs, CILD_MIPI_CAL_CONFIG, SELD, 0);
	regmap_update_bits(regs, CILD_MIPI_CAL_CONFIG_2, CLKSELD, 0);
	regmap_update_bits(regs, CILE_MIPI_CAL_CONFIG, SELE, 0);
	regmap_update_bits(regs, CSIE_MIPI_CAL_CONFIG_2, CLKSELE, 0);

	/* Select the CIL pad for auto calibration */
	switch (port) {
	case TEGRA_CAMERA_PORT_CSI_A:
		regmap_update_bits(regs, CILA_MIPI_CAL_CONFIG, SELA, SELA);
		regmap_update_bits(regs, DSIA_MIPI_CAL_CONFIG_2, CLKSELDSIA, 0);
		if (pdata->lanes > 2) {
			regmap_update_bits(regs, CILB_MIPI_CAL_CONFIG,
					   SELB, SELB);
			regmap_update_bits(regs, DSIB_MIPI_CAL_CONFIG_2,
					   CLKSELDSIB, 0);
		}
		break;
	case TEGRA_CAMERA_PORT_CSI_B:
		regmap_update_bits(regs, CILC_MIPI_CAL_CONFIG, SELC, SELC);
		regmap_update_bits(regs, CILC_MIPI_CAL_CONFIG_2, CLKSELC, 0);
		if (pdata->lanes > 2) {
			regmap_update_bits(regs, CILD_MIPI_CAL_CONFIG,
					   SELD, SELD);
			regmap_update_bits(regs, CILD_MIPI_CAL_CONFIG_2,
					   CLKSELD, 0);
		}
		break;
	case TEGRA_CAMERA_PORT_CSI_C:
		regmap_update_bits(regs, CILE_MIPI_CAL_CONFIG, SELE, SELE);
		regmap_update_bits(regs, CSIE_MIPI_CAL_CONFIG_2,
				   CLKSELE, CLKSELE);
		break;
	default:
		dev_err(&pdev->dev, "wrong port %d\n", port);
	}

	/* Trigger calibration */
	regmap_update_bits(regs, MIPI_CAL_CTRL, STARTCAL, STARTCAL);
	while (--retry) {
		regmap_read(regs, CIL_MIPI_CAL_STATUS, &val);
		if (val & CAL_DONE)
			break;
		usleep_range(200, 300);
	}

	/* Cleanup: un-select to avoid interference with DSI */
	regmap_update_bits(regs, CILA_MIPI_CAL_CONFIG, SELA, 0);
	regmap_update_bits(regs, DSIA_MIPI_CAL_CONFIG_2,
			   CLKSELDSIA, CLKSELDSIA);
	regmap_update_bits(regs, CILB_MIPI_CAL_CONFIG, SELB, 0);
	regmap_update_bits(regs, DSIB_MIPI_CAL_CONFIG_2,
			   CLKSELDSIB, CLKSELDSIB);
	regmap_update_bits(regs, CILC_MIPI_CAL_CONFIG, SELC, 0);
	regmap_update_bits(regs, CILC_MIPI_CAL_CONFIG_2, CLKSELC, CLKSELC);
	regmap_update_bits(regs, CILD_MIPI_CAL_CONFIG, SELD, 0);
	regmap_update_bits(regs, CILD_MIPI_CAL_CONFIG_2, CLKSELD, CLKSELD);
	regmap_update_bits(regs, CILE_MIPI_CAL_CONFIG, SELE, 0);
	regmap_update_bits(regs, CSIE_MIPI_CAL_CONFIG_2, CLKSELE, 0);

	regmap_exit(regs);
	iounmap(mipi_cal);

	/* Disable clocks */
	if (clk_mipi_cal)
		clk_disable_unprepare(clk_mipi_cal);
	if (clk_72mhz)
		clk_disable_unprepare(clk_72mhz);

	if (!retry) {
		dev_err(&pdev->dev, "MIPI calibration timeout!\n");
		return -EBUSY;
	}

	dev_dbg(&pdev->dev, "MIPI calibration for CSI is done\n");
	return 0;
}

struct tegra_camera_ops vi2_ops = {
	.clks_init = vi2_clks_init,
	.clks_deinit = vi2_clks_deinit,
	.clks_enable = vi2_clks_enable,
	.clks_disable = vi2_clks_disable,

	.capture_clean = vi2_capture_clean,
	.capture_setup = vi2_capture_setup,
	.capture_start = vi2_capture_start,
	.capture_wait = vi2_capture_wait,
	.capture_done = vi2_capture_done,
	.capture_stop = vi2_capture_stop,

	.activate = vi2_sw_reset,

	.init_syncpts = vi2_init_syncpts,
	.free_syncpts = vi2_free_syncpts,
	.incr_syncpts = vi2_incr_syncpts,

	.port_is_valid = vi2_port_is_valid,

	.mipi_calibration = vi2_mipi_calibration,
};

int vi2_register(struct tegra_camera_dev *cam)
{
	/* Init regulator */
	cam->regulator_name = "avdd_dsi_csi";

	/* Init VI2/CSI2 ops */
	cam->ops = &vi2_ops;

	return 0;
}
