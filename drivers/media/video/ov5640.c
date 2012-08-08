/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <media/v4l2-chip-ident.h>
#include <media/soc_camera.h>

#define to_ov5640(sd)		container_of(sd, struct ov5640_priv, subdev)

#define OV5640_SYSTEM_CTRL		0x3008
#define OV5640_CHIP_ID_HI		0x300a
#define OV5640_CHIP_ID_LO		0x300b
#define OV5640_PAD_OUTPUT_ENABLE00	0x3016
#define OV5640_PAD_OUTPUT_ENABLE01	0x3017
#define OV5640_PAD_OUTPUT_ENABLE02	0x3018
#define OV5640_SC_PLL_CTRL0		0x3034
#define OV5640_SC_PLL_CTRL1		0x3035
#define OV5640_SC_PLL_CTRL2		0x3036
#define OV5640_SC_PLL_CTRL3		0x3037

/* SCCB Control */
#define OV5640_SCCB_SYSTEM_CTRL1	0x3103
#define OV5640_SYSTEM_ROOT_DIVIDER	0x3108

/* Timing Control */
#define OV5640_TIMING_HS_HI		0x3800
#define OV5640_TIMING_HS_LO		0x3801
#define OV5640_TIMING_VS_HI		0x3802
#define OV5640_TIMING_VS_LO		0x3803
#define OV5640_TIMING_HW_HI		0x3804
#define OV5640_TIMING_HW_LO		0x3805
#define OV5640_TIMING_VH_HI		0x3806
#define OV5640_TIMING_VH_LO		0x3807
#define OV5640_TIMING_DVPHO_HI		0x3808
#define OV5640_TIMING_DVPHO_LO		0x3809
#define OV5640_TIMING_DVPVO_HI		0x380a
#define OV5640_TIMING_DVPVO_LO		0x380b
#define OV5640_TIMING_HTS_HI		0x380c
#define OV5640_TIMING_HTS_LO		0x380d
#define OV5640_TIMING_VTS_HI		0x380e
#define OV5640_TIMING_VTS_LO		0x380f
#define OV5640_TIMING_HOFFSET_HI	0x3810
#define OV5640_TIMING_HOFFSET_LO	0x3811
#define OV5640_TIMING_VOFFSET_HI	0x3812
#define OV5640_TIMING_VOFFSET_LO	0x3813
#define OV5640_TIMING_X_INC		0x3814
#define OV5640_TIMING_Y_INC		0x3815
#define OV5640_TIMING_TC_REG20		0x3820
#define OV5640_TIMING_TC_REG21		0x3821

/* AEC/AGC Power Down Domain Control */
#define OV5640_AEC_MAX_EXPO_60HZ_HI	0x3a02
#define OV5640_AEC_MAX_EXPO_60HZ_LO	0x3a03
#define OV5640_AEC_B50_STEP_HI		0x3a08
#define OV5640_AEC_B50_STEP_LO		0x3a09
#define OV5640_AEC_B60_STEP_HI		0x3a0a
#define OV5640_AEC_B60_STEP_LO		0x3a0b
#define OV5640_AEC_CTRL0C		0x3a0c
#define OV5640_AEC_CTRL0D		0x3a0d
#define OV5640_AEC_CTRL0E		0x3a0e
#define OV5640_AEC_CTRL0F		0x3a0f
#define OV5640_AEC_CTRL10		0x3a10
#define OV5640_AEC_CTRL11		0x3a11
#define OV5640_AEC_CTRL12		0x3a12
#define OV5640_AEC_CTRL13		0x3a13
#define OV5640_AEC_MAX_EXPO_50HZ_HI	0x3a14
#define OV5640_AEC_MAX_EXPO_50HZ_LO	0x3a15
#define OV5640_AEC_GAIN_CEILING_HI	0x3a18
#define OV5640_AEC_GAIN_CEILING_LO	0x3a19
#define OV5640_AEC_CTRL1B		0x3a1b
#define OV5640_AEC_CTRL1E		0x3a1e
#define OV5640_AEC_CTRL1F		0x3a1f

/* 50/60Hz Detector Control */
#define OV5640_5060HZ_CTRL00		0x3c00
#define OV5640_5060HZ_CTRL01		0x3c01
#define OV5640_5060HZ_CTRL02		0x3c02
#define OV5640_5060HZ_CTRL03		0x3c03
#define OV5640_5060HZ_CTRL04		0x3c04
#define OV5640_5060HZ_CTRL05		0x3c05
#define OV5640_LIGHT_METER1_THRESHOLD_HI 0x3c06
#define OV5640_LIGHT_METER1_THRESHOLD_LO 0x3c07
#define OV5640_LIGHT_METER2_THRESHOLD_HI 0x3c08
#define OV5640_LIGHT_METER2_THRESHOLD_LO 0x3c09
#define OV5640_SAMPLE_NUMBER_HI		0x3c0a
#define OV5640_SAMPLE_NUMBER_LO		0x3c0b

/* ISP General Controls */
#define OV5640_ISP_CTRL00		0x5000
#define OV5640_ISP_CTRL01		0x5001
#define OV5640_ISP_CTRL37		0x5025

/* AWB Control */
#define OV5640_AWB_CTRL00		0x5180
#define OV5640_AWB_CTRL01		0x5181
#define OV5640_AWB_CTRL02		0x5182
#define OV5640_AWB_CTRL03		0x5183
#define OV5640_AWB_CTRL04		0x5184
#define OV5640_AWB_CTRL05		0x5185
#define OV5640_AWB_CTRL06		0x5186
#define OV5640_AWB_CTRL07		0x5187
#define OV5640_AWB_CTRL08		0x5188
#define OV5640_AWB_CTRL09		0x5189
#define OV5640_AWB_CTRL10		0x518a
#define OV5640_AWB_CTRL11		0x518b
#define OV5640_AWB_CTRL12		0x518c
#define OV5640_AWB_CTRL13		0x518d
#define OV5640_AWB_CTRL14		0x518e
#define OV5640_AWB_CTRL15		0x518f
#define OV5640_AWB_CTRL16		0x5190
#define OV5640_AWB_CTRL17		0x5191
#define OV5640_AWB_CTRL18		0x5192
#define OV5640_AWB_CTRL19		0x5193
#define OV5640_AWB_CTRL20		0x5194
#define OV5640_AWB_CTRL21		0x5195
#define OV5640_AWB_CTRL22		0x5196
#define OV5640_AWB_CTRL23		0x5197
#define OV5640_AWB_CTRL24		0x5198
#define OV5640_AWB_CTRL25		0x5199
#define OV5640_AWB_CTRL26		0x519a
#define OV5640_AWB_CTRL27		0x519b
#define OV5640_AWB_CTRL28		0x519c
#define OV5640_AWB_CTRL29		0x519d
#define OV5640_AWB_CTRL30		0x519e

/* CIP Control */
#define OV5640_CIP_SHARPENMT_THRESHOLD_1 0x5300
#define OV5640_CIP_SHARPENMT_THRESHOLD_2 0x5301
#define OV5640_CIP_SHARPENMT_OFFSET_1	0x5302
#define OV5640_CIP_SHARPENMT_OFFSET_2	0x5303
#define OV5640_CIP_DNS_THRESHOLD_1	0x5304
#define OV5640_CIP_DNS_THRESHOLD_2	0x5305
#define OV5640_CIP_DNS_OFFSET_1		0x5306
#define OV5640_CIP_DNS_OFFSET_2		0x5307
#define OV5640_CIP_CTRL			0x5308
#define OV5640_CIP_SHARPENTH_THRESHOLD_1 0x5309
#define OV5640_CIP_SHARPENTH_THRESHOLD_2 0x530a
#define OV5640_CIP_SHARPENTH_OFFSET_1	0x530b
#define OV5640_CIP_SHARPENTH_OFFSET_2	0x530c
#define OV5640_CIP_EDGE_MT_AUTO		0x530d
#define OV5640_CIP_DNS_THRESHOLD_AUTO	0x530e
#define OF5640_CIP_SHARPEN_THRESHOLD_AUTO 0x530f

/* CMX Control */
#define OV5640_CMX_CTRL			0x5380
#define OV5640_CMX1			0x5381
#define OV5640_CMX2			0x5382
#define OV5640_CMX3			0x5383
#define OV5640_CMX4			0x5384
#define OV5640_CMX5			0x5385
#define OV5640_CMX6			0x5386
#define OV5640_CMX7			0x5387
#define OV5640_CMX8			0x5388
#define OV5640_CMX9			0x5389
#define OV5640_CMXSIGN_HI		0x538a
#define OV5640_CMXSIGN_LO		0x538b

/* Gamma Control */
#define OV5640_GAMMA_CTRL00		0x5480
#define OV5640_YST00			0x5481
#define OV5640_YST01			0x5482
#define OV5640_YST02			0x5483
#define OV5640_YST03			0x5484
#define OV5640_YST04			0x5485
#define OV5640_YST05			0x5486
#define OV5640_YST06			0x5487
#define OV5640_YST07			0x5488
#define OV5640_YST08			0x5489
#define OV5640_YST09			0x548a
#define OV5640_YST0A			0x548b
#define OV5640_YST0B			0x548c
#define OV5640_YST0C			0x548d
#define OV5640_YST0D			0x548e
#define OV5640_YST0E			0x548f
#define OV5640_YST0F			0x5490

/* SDE Control */
#define OV5640_SDE_CTRL_0		0x5580
#define OV5640_SDE_CTRL_1		0x5581
#define OV5640_SDE_CTRL_2		0x5582
#define OV5640_SDE_CTRL_3		0x5583
#define OV5640_SDE_CTRL_4		0x5584
#define OV5640_SDE_CTRL_5		0x5585
#define OV5640_SDE_CTRL_6		0x5586
#define OV5640_SDE_CTRL_7		0x5587
#define OV5640_SDE_CTRL_8		0x5588
#define OV5640_SDE_CTRL_9		0x5589
#define OV5640_SDE_CTRL_10		0x558a
#define OV5640_SDE_CTRL_11		0x558b
#define OV5640_SDE_CTRL_12		0x558c

/* LENC Control */
#define OV5640_GMTRX00			0x5800
#define OV5640_GMTRX01			0x5801
#define OV5640_GMTRX02			0x5802
#define OV5640_GMTRX03			0x5803
#define OV5640_GMTRX04			0x5804
#define OV5640_GMTRX05			0x5805
#define OV5640_GMTRX10			0x5806
#define OV5640_GMTRX11			0x5807
#define OV5640_GMTRX12			0x5808
#define OV5640_GMTRX13			0x5809
#define OV5640_GMTRX14			0x580a
#define OV5640_GMTRX15			0x580b
#define OV5640_GMTRX20			0x580c
#define OV5640_GMTRX21			0x580d
#define OV5640_GMTRX22			0x580e
#define OV5640_GMTRX23			0x580f
#define OV5640_GMTRX24			0x5810
#define OV5640_GMTRX25			0x5811
#define OV5640_GMTRX30			0x5812
#define OV5640_GMTRX31			0x5813
#define OV5640_GMTRX32			0x5814
#define OV5640_GMTRX33			0x5815
#define OV5640_GMTRX34			0x5816
#define OV5640_GMTRX35			0x5817
#define OV5640_GMTRX40			0x5818
#define OV5640_GMTRX41			0x5819
#define OV5640_GMTRX42			0x581a
#define OV5640_GMTRX43			0x581b
#define OV5640_GMTRX44			0x581c
#define OV5640_GMTRX45			0x581d
#define OV5640_GMTRX50			0x581e
#define OV5640_GMTRX51			0x581f
#define OV5640_GMTRX52			0x5820
#define OV5640_GMTRX53			0x5821
#define OV5640_GMTRX54			0x5822
#define OV5640_GMTRX55			0x5823
#define OV5640_BRMATRX00		0x5824
#define OV5640_BRMATRX01		0x5825
#define OV5640_BRMATRX02		0x5826
#define OV5640_BRMATRX03		0x5827
#define OV5640_BRMATRX04		0x5828
#define OV5640_BRMATRX05		0x5829
#define OV5640_BRMATRX06		0x582a
#define OV5640_BRMATRX07		0x582b
#define OV5640_BRMATRX08		0x582c
#define OV5640_BRMATRX09		0x582d
#define OV5640_BRMATRX20		0x582e
#define OV5640_BRMATRX21		0x582f
#define OV5640_BRMATRX22		0x5830
#define OV5640_BRMATRX23		0x5831
#define OV5640_BRMATRX24		0x5832
#define OV5640_BRMATRX30		0x5833
#define OV5640_BRMATRX31		0x5834
#define OV5640_BRMATRX32		0x5835
#define OV5640_BRMATRX33		0x5836
#define OV5640_BRMATRX34		0x5837
#define OV5640_BRMATRX40		0x5838
#define OV5640_BRMATRX41		0x5839
#define OV5640_BRMATRX42		0x583a
#define OV5640_BRMATRX43		0x583b
#define OV5640_BRMATRX44		0x583c
#define OV5640_LENC_BR_OFFSET		0x583d

#define OV5640_MAX_WIDTH		640
#define OV5640_MAX_HEIGHT		480

/* Misc. structures */
struct ov5640_reg {
	u16				reg;
	u8				val;
};

struct ov5640_priv {
	struct v4l2_subdev		subdev;

	int				ident;
	u16				chip_id;
	u8				revision;
	u8				manid;
	u8				smiaver;

	bool				flag_vflip;
	bool				flag_hflip;

	/* For suspend/resume. */
	struct v4l2_mbus_framefmt	current_mf;
	bool				current_enable;
};

static const struct ov5640_reg ov5640_defaults[] = {
	{ OV5640_SCCB_SYSTEM_CTRL1,		0x11},
	{ OV5640_SYSTEM_CTRL,			0x82},
	{ OV5640_SYSTEM_CTRL,			0x42},
	{ OV5640_SCCB_SYSTEM_CTRL1,		0x03},
	{ OV5640_PAD_OUTPUT_ENABLE01,		0x00},
	{ OV5640_PAD_OUTPUT_ENABLE02,		0x00},
	{ OV5640_SC_PLL_CTRL0,			0x18},
	{ OV5640_SC_PLL_CTRL1,			0x14},
	{ OV5640_SC_PLL_CTRL2,			0x38},
	{ OV5640_SC_PLL_CTRL3,			0x13},
	{ 0x4800,	0x24}, /* noncontinuous clock */
	{ OV5640_SYSTEM_ROOT_DIVIDER,		0x01},
	{ 0x3630,	0x36},
	{ 0x3631,	0x0e},
	{ 0x3632,	0xe2},
	{ 0x3633,	0x12},
	{ 0x3621,	0xe0},
	{ 0x3704,	0xa0},
	{ 0x3703,	0x5a},
	{ 0x3715,	0x78},
	{ 0x3717,	0x01},
	{ 0x370b,	0x60},
	{ 0x3705,	0x1a},
	{ 0x3905,	0x02},
	{ 0x3906,	0x10},
	{ 0x3901,	0x0a},
	{ 0x3731,	0x12},
	{ 0x3600,	0x08},
	{ 0x3601,	0x33},
	{ 0x302d,	0x60},
	{ 0x3620,	0x52},
	{ 0x371b,	0x20},
	{ 0x471c,	0x50},
	{ 0x3a13,	0x43},
	{ 0x3a18,	0x00},
	{ 0x3a19,	0xf8},
	{ 0x3635,	0x13},
	{ 0x3636,	0x03},
	{ 0x3634,	0x40},
	{ 0x3622,	0x01},
	{ 0x3c01,	0x34},
	{ 0x3c04,	0x28},
	{ 0x3c05,	0x98},
	{ 0x3c06,	0x00},
	{ 0x3c07,	0x08},
	{ 0x3c08,	0x00},
	{ 0x3c09,	0x1c},
	{ 0x3c0a,	0x9c},
	{ 0x3c0b,	0x40},
	{ OV5640_TIMING_TC_REG20,	0x41},
	{ OV5640_TIMING_TC_REG21,	0x01},
	{ 0x3814,	0x31},
	{ 0x3815,	0x31},
	{ 0x3800,	0x00},
	{ 0x3801,	0x00},
	{ 0x3802,	0x00},
	{ 0x3803,	0x04},
	{ 0x3804,	0x0a},
	{ 0x3805,	0x3f},
	{ 0x3806,	0x07},
	{ 0x3807,	0x9b},
	{ 0x3808,	0x02},
	{ 0x3809,	0x80},
	{ 0x380a,	0x01},
	{ 0x380b,	0xe0},
	{ 0x380c,	0x07},
	{ 0x380d,	0x68},
	{ 0x380e,	0x03},
	{ 0x380f,	0xd8},
	{ 0x3810,	0x00},
	{ 0x3811,	0x10},
	{ 0x3812,	0x00},
	{ 0x3813,	0x06},
	{ 0x3618,	0x00},
	{ 0x3612,	0x29},
	{ 0x3708,	0x64},
	{ 0x3709,	0x52},
	{ 0x370c,	0x03},

	/* AEC/AGC Power Down Domain Control */
	{ OV5640_AEC_MAX_EXPO_60HZ_HI,	0x03},
	{ OV5640_AEC_MAX_EXPO_60HZ_LO,	0xd8},
	{ OV5640_AEC_B50_STEP_HI,	0x01},
	{ OV5640_AEC_B50_STEP_LO,	0x27},
	{ OV5640_AEC_B60_STEP_HI,	0x00},
	{ OV5640_AEC_B60_STEP_LO,	0xf6},
	{ OV5640_AEC_CTRL0E,		0x03},
	{ OV5640_AEC_CTRL0D,		0x04},
	{ OV5640_AEC_MAX_EXPO_50HZ_HI,	0x03},
	{ OV5640_AEC_MAX_EXPO_50HZ_LO,	0xd8},

	{ 0x4001,	0x02},
	{ 0x4004,	0x02},
	{ 0x3000,	0x00},
	{ 0x3002,	0x1c},
	{ 0x3004,	0xff},
	{ 0x3006,	0xc3},
	{ 0x300e,	0x45},
	{ 0x302e,	0x08},
	/* org:30 bit[3:0]
	   0x0:YUYV 0x1:YVYU 0x2:UYVY
	   0x3:VYUY 0xF:UYVY 0x4~0xE:Not-allowed
	 */
	{ 0x4300,	0x32},
	{ 0x501f,	0x00},
	{ 0x4713,	0x03},
	{ 0x4407,	0x04},
	{ 0x440e,	0x00},
	{ 0x460b,	0x35},
	{ 0x460c,	0x22},
	{ 0x4837,	0x44},
	{ 0x3824,	0x02},
	{ 0x5000,	0xa7},
	{ 0x5001,	0xa3},

	/* AWB Control */
	{ OV5640_AWB_CTRL00,	0xff},	{ OV5640_AWB_CTRL01,	0xf2},
	{ OV5640_AWB_CTRL02,	0x00},	{ OV5640_AWB_CTRL03,	0x14},
	{ OV5640_AWB_CTRL04,	0x25},	{ OV5640_AWB_CTRL05,	0x24},
	{ OV5640_AWB_CTRL06,	0x09},	{ OV5640_AWB_CTRL07,	0x09},
	{ OV5640_AWB_CTRL08,	0x09},	{ OV5640_AWB_CTRL09,	0x75},
	{ OV5640_AWB_CTRL10,	0x54},	{ OV5640_AWB_CTRL11,	0xe0},
	{ OV5640_AWB_CTRL12,	0xb2},	{ OV5640_AWB_CTRL13,	0x42},
	{ OV5640_AWB_CTRL14,	0x3d},	{ OV5640_AWB_CTRL15,	0x56},
	{ OV5640_AWB_CTRL16,	0x46},	{ OV5640_AWB_CTRL17,	0xf8},
	{ OV5640_AWB_CTRL18,	0x04},	{ OV5640_AWB_CTRL19,	0x70},
	{ OV5640_AWB_CTRL20,	0xf0},	{ OV5640_AWB_CTRL21,	0xf0},
	{ OV5640_AWB_CTRL22,	0x03},	{ OV5640_AWB_CTRL23,	0x01},
	{ OV5640_AWB_CTRL24,	0x04},	{ OV5640_AWB_CTRL25,	0x12},
	{ OV5640_AWB_CTRL26,	0x04},	{ OV5640_AWB_CTRL27,	0x00},
	{ OV5640_AWB_CTRL28,	0x06},	{ OV5640_AWB_CTRL29,	0x82},
	{ OV5640_AWB_CTRL30,	0x38},

	/* CMX Control */
	{ OV5640_CMX1,		0x1e},
	{ OV5640_CMX2,		0x5b},
	{ OV5640_CMX3,		0x08},
	{ OV5640_CMX4,		0x0a},
	{ OV5640_CMX5,		0x7e},
	{ OV5640_CMX6,		0x88},
	{ OV5640_CMX7,		0x7c},
	{ OV5640_CMX8,		0x6c},
	{ OV5640_CMX9,		0x10},
	{ OV5640_CMXSIGN_HI,	0x01},
	{ OV5640_CMXSIGN_LO,	0x98},

	/* CIP Control */
	{ OV5640_CIP_SHARPENMT_THRESHOLD_1,	0x08},
	{ OV5640_CIP_SHARPENMT_THRESHOLD_2,	0x30},
	{ OV5640_CIP_SHARPENMT_OFFSET_1,	0x10},
	{ OV5640_CIP_SHARPENMT_OFFSET_2,	0x00},
	{ OV5640_CIP_DNS_THRESHOLD_1,		0x08},
	{ OV5640_CIP_DNS_THRESHOLD_2,		0x30},
	{ OV5640_CIP_DNS_OFFSET_1,		0x08},
	{ OV5640_CIP_DNS_OFFSET_2,		0x16},
	{ OV5640_CIP_SHARPENTH_THRESHOLD_1,	0x08},
	{ OV5640_CIP_SHARPENTH_THRESHOLD_2,	0x30},
	{ OV5640_CIP_SHARPENTH_OFFSET_1,	0x04},
	{ OV5640_CIP_SHARPENTH_OFFSET_2,	0x06},

	/* Gamma Control */
	{ OV5640_GAMMA_CTRL00,	0x01},
	{ OV5640_YST00,		0x08},	{ OV5640_YST01,		0x14},
	{ OV5640_YST02,		0x28},	{ OV5640_YST03,		0x51},
	{ OV5640_YST04,		0x65},	{ OV5640_YST05,		0x71},
	{ OV5640_YST06,		0x7d},	{ OV5640_YST07,		0x87},
	{ OV5640_YST08,		0x91},	{ OV5640_YST09,		0x9a},
	{ OV5640_YST0A,		0xaa},	{ OV5640_YST0B,		0xb8},
	{ OV5640_YST0C,		0xcd},	{ OV5640_YST0D,		0xdd},
	{ OV5640_YST0E,		0xea},	{ OV5640_YST0F,		0x1d},

	/* SDE Control */
	{ OV5640_SDE_CTRL_0,	0x02},
	{ OV5640_SDE_CTRL_3,	0x40},
	{ OV5640_SDE_CTRL_4,	0x10},
	{ OV5640_SDE_CTRL_9,	0x10},
	{ OV5640_SDE_CTRL_10,	0x00},
	{ OV5640_SDE_CTRL_11,	0xf8},

	/* LENC Control */
	{ OV5640_GMTRX00,	0x23},	{ OV5640_GMTRX01,	0x14},
	{ OV5640_GMTRX02,	0x0f},	{ OV5640_GMTRX03,	0x0f},
	{ OV5640_GMTRX04,	0x12},	{ OV5640_GMTRX05,	0x26},
	{ OV5640_GMTRX10,	0x0c},	{ OV5640_GMTRX11,	0x08},
	{ OV5640_GMTRX12,	0x05},	{ OV5640_GMTRX13,	0x05},
	{ OV5640_GMTRX14,	0x08},	{ OV5640_GMTRX15,	0x0d},
	{ OV5640_GMTRX20,	0x08},	{ OV5640_GMTRX21,	0x03},
	{ OV5640_GMTRX22,	0x00},	{ OV5640_GMTRX23,	0x00},
	{ OV5640_GMTRX24,	0x03},	{ OV5640_GMTRX25,	0x09},
	{ OV5640_GMTRX30,	0x07},	{ OV5640_GMTRX31,	0x03},
	{ OV5640_GMTRX32,	0x00},	{ OV5640_GMTRX33,	0x01},
	{ OV5640_GMTRX34,	0x03},	{ OV5640_GMTRX35,	0x08},
	{ OV5640_GMTRX40,	0x0d},	{ OV5640_GMTRX41,	0x08},
	{ OV5640_GMTRX42,	0x05},	{ OV5640_GMTRX43,	0x06},
	{ OV5640_GMTRX44,	0x08},	{ OV5640_GMTRX45,	0x0e},
	{ OV5640_GMTRX50,	0x29},	{ OV5640_GMTRX51,	0x17},
	{ OV5640_GMTRX52,	0x11},	{ OV5640_GMTRX53,	0x11},
	{ OV5640_GMTRX54,	0x15},	{ OV5640_GMTRX55,	0x28},
	{ OV5640_BRMATRX00,	0x46},	{ OV5640_BRMATRX01,	0x26},
	{ OV5640_BRMATRX02,	0x08},	{ OV5640_BRMATRX03,	0x26},
	{ OV5640_BRMATRX04,	0x64},	{ OV5640_BRMATRX05,	0x26},
	{ OV5640_BRMATRX06,	0x24},	{ OV5640_BRMATRX07,	0x22},
	{ OV5640_BRMATRX08,	0x24},	{ OV5640_BRMATRX09,	0x24},
	{ OV5640_BRMATRX20,	0x06},	{ OV5640_BRMATRX21,	0x22},
	{ OV5640_BRMATRX22,	0x40},	{ OV5640_BRMATRX23,	0x42},
	{ OV5640_BRMATRX24,	0x24},	{ OV5640_BRMATRX30,	0x26},
	{ OV5640_BRMATRX31,	0x24},	{ OV5640_BRMATRX32,	0x22},
	{ OV5640_BRMATRX33,	0x22},	{ OV5640_BRMATRX34,	0x26},
	{ OV5640_BRMATRX40,	0x44},	{ OV5640_BRMATRX41,	0x24},
	{ OV5640_BRMATRX42,	0x26},	{ OV5640_BRMATRX43,	0x28},
	{ OV5640_BRMATRX44,	0x42},	{ OV5640_LENC_BR_OFFSET, 0xce},

	{ OV5640_ISP_CTRL37,	0x00},
	{ OV5640_AEC_CTRL0F,	0x30},
	{ OV5640_AEC_CTRL10,	0x28},
	{ OV5640_AEC_CTRL1B,	0x30},
	{ OV5640_AEC_CTRL1E,	0x26},
	{ OV5640_AEC_CTRL11,	0x60},
	{ OV5640_AEC_CTRL1F,	0x14},
	{ OV5640_SYSTEM_CTRL,	0x02},
};

static enum v4l2_mbus_pixelcode ov5640_codes[] = {
	V4L2_MBUS_FMT_YUYV8_2X8,
};

static const struct v4l2_queryctrl ov5640_controls[] = {
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

/* read a register */
static int ov5640_reg_read(struct i2c_client *client, u16 reg, u8 *val)
{
	int ret;
	unsigned char data[2] = { reg >> 8, reg & 0xff };

	ret = i2c_master_send(client, data, 2);
	if (ret < 2) {
		dev_err(&client->dev, "%s: i2c read error, reg: %x\n",
				__func__, reg);
		return ret < 0 ? ret : -EIO;
	}

	ret = i2c_master_recv(client, val, 1);
	if (ret < 1) {
		dev_err(&client->dev, "%s: i2c read error, reg: %x\n",
				__func__, reg);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

/* write a register */
static int ov5640_reg_write(struct i2c_client *client, u16 reg, u8 val)
{
	int ret;
	unsigned char data[3] = { reg >> 8, reg & 0xff, val };

	ret = i2c_master_send(client, data, 3);
	if (ret < 3) {
		dev_err(&client->dev, "%s: i2c write error, reg: %x\n",
				__func__, reg);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}


/* Read a register, alter its bits, write it back */
static int ov5640_reg_rmw(struct i2c_client *client, u16 reg, u8 set, u8 unset)
{
	u8 val;
	int ret;

	ret = ov5640_reg_read(client, reg, &val);
	if (ret < 0) {
		dev_err(&client->dev,
				"[Read]-Modify-Write of register 0x%04x failed!\n",
				reg);
		return ret;
	}

	val |= set;
	val &= ~unset;

	ret = ov5640_reg_write(client, reg, val);
	if (ret < 0) {
		dev_err(&client->dev,
				"Read-Modify-[Write] of register 0x%04x failed!\n",
				reg);
		return ret;
	}

	return 0;
}

static int ov5640_reg_write_array(struct i2c_client *client,
		const struct ov5640_reg *regarray,
		int regarraylen)
{
	int i;
	int ret;

	for (i = 0; i < regarraylen; i++) {
		ret = ov5640_reg_write(client,
				regarray[i].reg, regarray[i].val);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/* Start/Stop streaming from the device */
static int ov5640_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5640_priv *priv = to_ov5640(sd);
	int ret;

	/* Program orientation register. */
	if (priv->flag_vflip)
		ret = ov5640_reg_rmw(client, OV5640_TIMING_TC_REG20, 0x2, 0);
	else
		ret = ov5640_reg_rmw(client, OV5640_TIMING_TC_REG20, 0, 0x2);
	if (ret < 0)
		return ret;

	if (priv->flag_hflip)
		ret = ov5640_reg_rmw(client, OV5640_TIMING_TC_REG21, 0x2, 0);
	else
		ret = ov5640_reg_rmw(client, OV5640_TIMING_TC_REG21, 0, 0x2);
	if (ret < 0)
		return ret;

	if (!enable) {
		/* Software Reset */
		ret = ov5640_reg_write(client, OV5640_SYSTEM_CTRL, 0x82);
		if (!ret)
			/* Setting Streaming to Standby */
			ret = ov5640_reg_write(client, OV5640_SYSTEM_CTRL,
					0x42);
	}

	priv->current_enable = enable;

	return ret;
}

/* Alter bus settings on camera side */
static int ov5640_set_bus_param(struct soc_camera_device *icd,
		unsigned long flags)
{
	return 0;
}

/* Request bus settings on camera side */
static unsigned long ov5640_query_bus_param(struct soc_camera_device *icd)
{
	struct soc_camera_link *icl = to_soc_camera_link(icd);

	unsigned long flags = SOCAM_PCLK_SAMPLE_RISING | SOCAM_MASTER |
		SOCAM_VSYNC_ACTIVE_HIGH | SOCAM_HSYNC_ACTIVE_HIGH |
		SOCAM_DATA_ACTIVE_HIGH | SOCAM_DATAWIDTH_8;

	return soc_camera_apply_sensor_flags(icl, flags);
}

/* select nearest higher resolution for capture */
static void ov5640_res_roundup(u32 *width, u32 *height)
{
	/* Width must be a multiple of 4 pixels. */
	*width = ALIGN(*width, 4);

	/* Max resolution is 1280x720 (720p). */
	if (*width > OV5640_MAX_WIDTH)
		*width = OV5640_MAX_WIDTH;

	if (*height > OV5640_MAX_HEIGHT)
		*height = OV5640_MAX_HEIGHT;
}

/* Setup registers according to resolution and color encoding */
static int ov5640_set_res(struct i2c_client *client, u32 width, u32 height)
{
	/* Note, this stuff is bogus.  It's just copied from ov9740.c. */
#if 0
	u32 x_start;
	u32 y_start;
	u32 x_end;
	u32 y_end;
	bool scaling = 0;
	u32 scale_input_x;
	u32 scale_input_y;
	int ret;

	if ((width != OV5640_MAX_WIDTH) || (height != OV5640_MAX_HEIGHT))
		scaling = 1;

	/*
	 * Try to use as much of the sensor area as possible when supporting
	 * smaller resolutions.  Depending on the aspect ratio of the
	 * chosen resolution, we can either use the full width of the sensor,
	 * or the full height of the sensor (or both if the aspect ratio is
	 * the same as 1280x720.
	 */
	if ((OV5640_MAX_WIDTH * height) > (OV5640_MAX_HEIGHT * width)) {
		scale_input_x = (OV5640_MAX_HEIGHT * width) / height;
		scale_input_y = OV5640_MAX_HEIGHT;
	} else {
		scale_input_x = OV5640_MAX_WIDTH;
		scale_input_y = (OV5640_MAX_WIDTH * height) / width;
	}

	/* These describe the area of the sensor to use. */
	x_start = (OV5640_MAX_WIDTH - scale_input_x) / 2;
	y_start = (OV5640_MAX_HEIGHT - scale_input_y) / 2;
	x_end = x_start + scale_input_x - 1;
	y_end = y_start + scale_input_y - 1;

	ret = ov5640_reg_write(client, OV5640_X_ADDR_START_HI, x_start >> 8);
	if (ret)
		goto done;
	ret = ov5640_reg_write(client, OV5640_X_ADDR_START_LO, x_start & 0xff);
	if (ret)
		goto done;
	ret = ov5640_reg_write(client, OV5640_Y_ADDR_START_HI, y_start >> 8);
	if (ret)
		goto done;
	ret = ov5640_reg_write(client, OV5640_Y_ADDR_START_LO, y_start & 0xff);
	if (ret)
		goto done;

	ret = ov5640_reg_write(client, OV5640_X_ADDR_END_HI, x_end >> 8);
	if (ret)
		goto done;
	ret = ov5640_reg_write(client, OV5640_X_ADDR_END_LO, x_end & 0xff);
	if (ret)
		goto done;
	ret = ov5640_reg_write(client, OV5640_Y_ADDR_END_HI, y_end >> 8);
	if (ret)
		goto done;
	ret = ov5640_reg_write(client, OV5640_Y_ADDR_END_LO, y_end & 0xff);
	if (ret)
		goto done;

	ret = ov5640_reg_write(client, OV5640_X_OUTPUT_SIZE_HI, width >> 8);
	if (ret)
		goto done;
	ret = ov5640_reg_write(client, OV5640_X_OUTPUT_SIZE_LO, width & 0xff);
	if (ret)
		goto done;
	ret = ov5640_reg_write(client, OV5640_Y_OUTPUT_SIZE_HI, height >> 8);
	if (ret)
		goto done;
	ret = ov5640_reg_write(client, OV5640_Y_OUTPUT_SIZE_LO, height & 0xff);
	if (ret)
		goto done;

	ret = ov5640_reg_write(client, OV5640_ISP_CTRL1E, scale_input_x >> 8);
	if (ret)
		goto done;
	ret = ov5640_reg_write(client, OV5640_ISP_CTRL1F, scale_input_x & 0xff);
	if (ret)
		goto done;
	ret = ov5640_reg_write(client, OV5640_ISP_CTRL20, scale_input_y >> 8);
	if (ret)
		goto done;
	ret = ov5640_reg_write(client, OV5640_ISP_CTRL21, scale_input_y & 0xff);
	if (ret)
		goto done;

	ret = ov5640_reg_write(client, OV5640_VFIFO_READ_START_HI,
			       (scale_input_x - width) >> 8);
	if (ret)
		goto done;
	ret = ov5640_reg_write(client, OV5640_VFIFO_READ_START_LO,
			       (scale_input_x - width) & 0xff);
	if (ret)
		goto done;

	ret = ov5640_reg_write(client, OV5640_ISP_CTRL00, 0xff);
	if (ret)
		goto done;
	ret = ov5640_reg_write(client, OV5640_ISP_CTRL01, 0xef |
							  (scaling << 4));
	if (ret)
		goto done;
	ret = ov5640_reg_write(client, OV5640_ISP_CTRL03, 0xff);

done:
	return ret;
#endif
	return 0;
}

/* set the format we will capture in */
static int ov5640_s_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5640_priv *priv = to_ov5640(sd);
	enum v4l2_colorspace cspace;
	enum v4l2_mbus_pixelcode code = mf->code;
	int ret;

	ov5640_res_roundup(&mf->width, &mf->height);

	switch (code) {
	case V4L2_MBUS_FMT_YUYV8_2X8:
		cspace = V4L2_COLORSPACE_SRGB;
		break;
	default:
		return -EINVAL;
	}

	ret = ov5640_reg_write_array(client, ov5640_defaults,
				     ARRAY_SIZE(ov5640_defaults));
	if (ret < 0)
		return ret;

	ret = ov5640_set_res(client, mf->width, mf->height);
	if (ret < 0)
		return ret;

	mf->code	= code;
	mf->colorspace	= cspace;

	memcpy(&priv->current_mf, mf, sizeof(struct v4l2_mbus_framefmt));

	return ret;
}

static int ov5640_try_fmt(struct v4l2_subdev *sd,
			  struct v4l2_mbus_framefmt *mf)
{
	ov5640_res_roundup(&mf->width, &mf->height);

	mf->field = V4L2_FIELD_NONE;
	mf->code = V4L2_MBUS_FMT_YUYV8_2X8;
	mf->colorspace = V4L2_COLORSPACE_SRGB;

	return 0;
}

static int ov5640_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			   enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(ov5640_codes))
		return -EINVAL;

	*code = ov5640_codes[index];

	return 0;
}

static int ov5640_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	a->bounds.left		= 0;
	a->bounds.top		= 0;
	a->bounds.width		= OV5640_MAX_WIDTH;
	a->bounds.height	= OV5640_MAX_HEIGHT;
	a->defrect		= a->bounds;
	a->type			= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static int ov5640_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	a->c.left		= 0;
	a->c.top		= 0;
	a->c.width		= OV5640_MAX_WIDTH;
	a->c.height		= OV5640_MAX_HEIGHT;
	a->type			= V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

/* Get status of additional camera capabilities */
static int ov5640_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct ov5640_priv *priv = to_ov5640(sd);

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
static int ov5640_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct ov5640_priv *priv = to_ov5640(sd);

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
static int ov5640_g_chip_ident(struct v4l2_subdev *sd,
			       struct v4l2_dbg_chip_ident *id)
{
	struct ov5640_priv *priv = to_ov5640(sd);

	id->ident = priv->ident;
	id->revision = priv->revision;

	return 0;
}

static int ov5640_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov5640_priv *priv = to_ov5640(sd);

	if (!priv->current_enable)
		return 0;

	if (on) {
		ov5640_s_fmt(sd, &priv->current_mf);
		ov5640_s_stream(sd, priv->current_enable);
	} else {
		ov5640_s_stream(sd, 0);
		priv->current_enable = true;
	}

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ov5640_get_register(struct v4l2_subdev *sd,
			       struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	u8 val;

	if (reg->reg & ~0xffff)
		return -EINVAL;

	reg->size = 2;

	ret = ov5640_reg_read(client, reg->reg, &val);
	if (ret)
		return ret;

	reg->val = (__u64)val;

	return ret;
}

static int ov5640_set_register(struct v4l2_subdev *sd,
			       struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->reg & ~0xffff || reg->val & ~0xff)
		return -EINVAL;

	return ov5640_reg_write(client, reg->reg, reg->val);
}
#endif

static int ov5640_video_probe(struct soc_camera_device *icd,
			      struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5640_priv *priv = to_ov5640(sd);
	u8 chip_id_hi, chip_id_lo;
	int ret;

	/* We must have a parent by now. And it cannot be a wrong one. */
	BUG_ON(!icd->parent ||
	       to_soc_camera_host(icd->parent)->nr != icd->iface);

	/*
	 * check and show product ID and manufacturer ID
	 */
	ret = ov5640_reg_read(client, OV5640_CHIP_ID_HI, &chip_id_hi);
	if (ret < 0)
		goto err;

	ret = ov5640_reg_read(client, OV5640_CHIP_ID_LO, &chip_id_lo);
	if (ret < 0)
		goto err;

	priv->chip_id = (chip_id_hi << 8) | chip_id_lo;

	if (priv->chip_id != 0x5640) {
		ret = -ENODEV;
		goto err;
	}

	priv->ident = V4L2_IDENT_OV5640;

	dev_info(&client->dev, "Chip ID 0x%04x\n", priv->chip_id);

err:
	return ret;
}

static struct soc_camera_ops ov5640_ops = {
	.set_bus_param		= ov5640_set_bus_param,
	.query_bus_param	= ov5640_query_bus_param,
	.controls		= ov5640_controls,
	.num_controls		= ARRAY_SIZE(ov5640_controls),
};

static struct v4l2_subdev_video_ops ov5640_video_ops = {
	.s_stream		= ov5640_s_stream,
	.s_mbus_fmt		= ov5640_s_fmt,
	.try_mbus_fmt		= ov5640_try_fmt,
	.enum_mbus_fmt		= ov5640_enum_fmt,
	.cropcap		= ov5640_cropcap,
	.g_crop			= ov5640_g_crop,
};

static struct v4l2_subdev_core_ops ov5640_core_ops = {
	.g_ctrl			= ov5640_g_ctrl,
	.s_ctrl			= ov5640_s_ctrl,
	.g_chip_ident		= ov5640_g_chip_ident,
	.s_power		= ov5640_s_power,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register		= ov5640_get_register,
	.s_register		= ov5640_set_register,
#endif
};

static struct v4l2_subdev_ops ov5640_subdev_ops = {
	.core			= &ov5640_core_ops,
	.video			= &ov5640_video_ops,
};

/*
 * i2c_driver function
 */
static int ov5640_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct ov5640_priv *priv;
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

	priv = kzalloc(sizeof(struct ov5640_priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&client->dev, "Failed to allocate private data!\n");
		return -ENOMEM;
	}

	v4l2_i2c_subdev_init(&priv->subdev, client, &ov5640_subdev_ops);

	icd->ops = &ov5640_ops;

	ret = ov5640_video_probe(icd, client);
	if (ret < 0) {
		icd->ops = NULL;
		kfree(priv);
	}

	return ret;
}

static int ov5640_remove(struct i2c_client *client)
{
	struct ov5640_priv *priv = i2c_get_clientdata(client);

	kfree(priv);

	return 0;
}

static const struct i2c_device_id ov5640_id[] = {
	{ "ov5640", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov5640_id);

static struct i2c_driver ov5640_i2c_driver = {
	.driver = {
		.name = "ov5640",
	},
	.probe    = ov5640_probe,
	.remove   = ov5640_remove,
	.id_table = ov5640_id,
};

static int __init ov5640_module_init(void)
{
	return i2c_add_driver(&ov5640_i2c_driver);
}

static void __exit ov5640_module_exit(void)
{
	i2c_del_driver(&ov5640_i2c_driver);
}

module_init(ov5640_module_init);
module_exit(ov5640_module_exit);

MODULE_DESCRIPTION("SoC Camera driver for OmniVision OV5640");
MODULE_AUTHOR("Andrew Chew <achew@nvidia.com>");
MODULE_LICENSE("GPL v2");
