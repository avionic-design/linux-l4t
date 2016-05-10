/*
 * Copyright 2015 Alban Bedel <alban.bedel@avionic-design.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 *
 */

#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>

#include <media/v4l2-subdev.h>
#include <media/v4l2-event.h>
#include <media/v4l2-of.h>
#include <media/soc_camera.h>

#include <sound/soc.h>
#include <sound/pcm_params.h>

/* Global */
#define CHIPID					0x0000
#define SYSCTL					0x0002
#define CONFCTL0				0x0004
#define CONFCTL0_AUD_OUT_SEL_SHIFT		3
#define CONFCTL0_AUD_OUT_SEL_MASK		(3 << CONFCTL0_AUD_OUT_SEL_SHIFT)
#define CONFCTL0_AUD_OUT_SEL_CSI_TX0		(0 << CONFCTL0_AUD_OUT_SEL_SHIFT)
#define CONFCTL0_AUD_OUT_SEL_CSI_TX1		(1 << CONFCTL0_AUD_OUT_SEL_SHIFT)
#define CONFCTL0_AUD_OUT_SEL_I2S		(2 << CONFCTL0_AUD_OUT_SEL_SHIFT)
#define CONFCTL0_AUD_OUT_SEL_TDM		(3 << CONFCTL0_AUD_OUT_SEL_SHIFT)
#define CONFCTL1				0x0006

/* Interrupt Registers */
#define INT_STATUS				0x0014
#define INT_MASK				0x0016

/* CSI-TX Registers */
#define CSI_REG(n, r)				(((r) + 0x100) + ((n) * 0x200))

/* CSI-TX Control Registers */
#define CSITX_CLKEN(n)				CSI_REG(n, 0x008)
#define PPI_CLKSEL(n)				CSI_REG(n, 0x00C)
#define LANE_ENABLE(n)				CSI_REG(n, 0x018)
#define CSITX_START(n)				CSI_REG(n, 0x01C)
#define LINE_INIT_COUNT(n)			CSI_REG(n, 0x020)
#define HSTX_TO_COUNT(n)			CSI_REG(n, 0x024)

/* D-PHY Control Registers */
#define PPI_DPHY_LPTXTIMECNT(n)			CSI_REG(n, 0x154)
#define PPI_DPHY_TCLK_HEADERCNT(n)		CSI_REG(n, 0x158)
#define PPI_DPHY_TCLK_TRAILCNT(n)		CSI_REG(n, 0x15C)
#define PPI_DPHY_THS_HEADERCNT(n)		CSI_REG(n, 0x160)
#define PPI_DPHY_TWAKEUPCNT(n)			CSI_REG(n, 0x164)
#define PPI_DPHY_TCLK_POSTCNT(n)		CSI_REG(n, 0x168)
#define PPI_DPHY_THSTRAILCNT(n)			CSI_REG(n, 0x16C)
#define PPI_DPHY_HSTXVREGCNT(n)			CSI_REG(n, 0x170)
#define PPI_DPHY_HSTXVREGEN(n)			CSI_REG(n, 0x174)

/* MIPI PLL Control Registers */
#define MIPI_PLL_CTRL(n)			CSI_REG(n, 0x1A0)
#define MIPI_PLL_CONF(n)			CSI_REG(n, 0x1AC)

/* CSI-TX Wrapper Registers */
#define STX_MAXFCNT(n)				(0x0510 + ((n) * 4))

/* Splitter Control Registers */
#define STX_REG(n, r)				(((r) + 0x5000) + ((n) * 0x80))
#define STX_CTRL(n)				STX_REG(n, 0x0)
#define STX_PACKETID1(n)			STX_REG(n, 0x2)
#define STX_FPX(n)				STX_REG(n, 0xC)
#define STX_LPX(n)				STX_REG(n, 0xE)

/* HDMI Rx System Control */
#define PHY_CTL					0x8410
#define APLL_CTL				0x84F0
#define DDCIO_CTL				0x84F4

#define HDMI_INT0				0x8500
#define MISC_INT				0x850B
#define MISC_INTM				0x851B
#define SYS_STATUS				0x8520
#define VI_STATUS				0x8521
#define VI_STATUS1				0x8522
#define SYS_FREQ0				0x8540
#define SYS_FREQ1				0x8541
#define INIT_END				0x854A
#define DE_HSIZE				0x8582
#define DE_VSIZE				0x858C
#define V_MUTE1					0x857A
#define VMUTE_STATUS				0x857D

#define EDID_MODE				0x85E0
#define EDID_LEN1				0x85E3
#define EDID_LEN2				0x85E4

/* HDMI Rx Audio Control */
#define FORCE_MUTE				0x8600
#define FS_MUTE					0x8607
#define MUTE_MODE				0x8608
#define FS_IMODE				0x8620
#define FS_SET					0x8621
#define LOCK_REF_FREQA				0x8630
#define LOCK_REF_FREQB				0x8631
#define LOCK_REF_FREQC				0x8632
#define SDO_MODE0				0x8651
#define SDO_MODE1				0x8652
#define SDO_MODE1_FMT_SHIFT			0
#define SDO_MODE1_FMT_MASK			7
#define SDO_MODE1_FMT_RIGHT_J			(0 << SDO_MODE1_FMT_SHIFT)
#define SDO_MODE1_FMT_LEFT_J			(1 << SDO_MODE1_FMT_SHIFT)
#define SDO_MODE1_FMT_I2S			(2 << SDO_MODE1_FMT_SHIFT)
#define NCO_F0_MOD				0x8670
#define NCO_48F0A				0x8671
#define NCO_48F0B				0x8672
#define NCO_48F0C				0x8673
#define NCO_48F0D				0x8674
#define NCO_44F0A				0x8675
#define NCO_44F0B				0x8676
#define NCO_44F0C				0x8677
#define NCO_44F0D				0x8678

/* VIDEO Output Format Registers */
#define VOUT_FMT				0x8A00
#define VOUT_CSC				0x8A08
#define SCLK_CSC0				0x8A0C
#define SCLK_CSC1				0x8A0D

/* Others */
#define EDID_RAM				0x8C00
#define EDID_MAX_SIZE				0x400

static const struct regulator_bulk_data uh2c_regulators[] = {
	{ "vddc11" },
	{ "vdd11-hdmi" },
	{ "vdd12-mipi0" },
	{ "vdd12-mipi1" },
	{ "vddio18" },
	{ "vddio33" },
	{ "vdd33-hdmi" },
};

struct uh2c {
	struct v4l2_subdev subdev;

	struct regmap *ctl_regmap;
	struct regmap *csi_regmap;
	struct regmap *hdmi_regmap;
	struct regmap *edid_regmap;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *int_gpio;

	unsigned long refrate;

	struct regulator_bulk_data regulators[ARRAY_SIZE(uh2c_regulators)];

	struct v4l2_of_bus_mipi_csi2 ep[2];

	struct v4l2_mbus_framefmt framefmt;
	struct v4l2_fract pixelaspect;
	bool vsync;

	struct mutex lock;
};

static const enum v4l2_mbus_pixelcode uh2c_pixelcode[] = {
	V4L2_MBUS_FMT_RGB888_1X24,
	V4L2_MBUS_FMT_UYVY8_2X8,
};

static const struct regmap_range ctl_regmap_rw_ranges[] = {
	regmap_reg_range(0x0000, 0x008f),
	/* csi-registers in between */
	regmap_reg_range(0x0510, 0x0514),
	regmap_reg_range(0x0600, 0x06cc),
	regmap_reg_range(0x5000, 0x5094),
	regmap_reg_range(0x7000, 0x7016),
	regmap_reg_range(0x7082, 0x7082),
};

static const struct regmap_access_table ctl_regmap_access = {
	.yes_ranges = ctl_regmap_rw_ranges,
	.n_yes_ranges = ARRAY_SIZE(ctl_regmap_rw_ranges),
};

static const struct regmap_config ctl_regmap_config = {
	.reg_bits = 16,
	.reg_stride = 2,
	.val_bits = 16,
	.cache_type = REGCACHE_NONE,
	.max_register = 0x7fff,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.rd_table = &ctl_regmap_access,
	.wr_table = &ctl_regmap_access,
	.name = "ctl",
};

static const struct regmap_range csi_regmap_rw_ranges[] = {
	regmap_reg_range(0x0100, 0x04ff),
};

static const struct regmap_access_table csi_regmap_access = {
	.yes_ranges = csi_regmap_rw_ranges,
	.n_yes_ranges = ARRAY_SIZE(csi_regmap_rw_ranges),
};

static const struct regmap_config csi_regmap_config = {
	.reg_bits = 16,
	.reg_stride = 4,
	.val_bits = 32,
	.cache_type = REGCACHE_NONE,
	.max_register = 0x04ff,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.rd_table = &csi_regmap_access,
	.wr_table = &csi_regmap_access,
	.name = "csi",
};

static const struct regmap_range hdmi_regmap_rw_ranges[] = {
	/* HDMI Rx System Control */
	regmap_reg_range(0x8410, 0x8414),
	regmap_reg_range(0x84F0, 0x84F4),
	regmap_reg_range(0x8500, 0x8528),
	regmap_reg_range(0x8540, 0x854A),
	regmap_reg_range(0x8560, 0x8561),
	regmap_reg_range(0x857A, 0x8593),
	regmap_reg_range(0x85E0, 0x85E4),
	/* HDMI Rx Audio Control */
	regmap_reg_range(0x8600, 0x8608),
	regmap_reg_range(0x8620, 0x8627),
	regmap_reg_range(0x862E, 0x8632),
	regmap_reg_range(0x8651, 0x8652),
	regmap_reg_range(0x8670, 0x8678),
	regmap_reg_range(0x8680, 0x8680),
	/* HDMI Rx InfoFrame Data */
	regmap_reg_range(0x8700, 0x87EE),
	/* HDMI Rx HDCP Registers */
	regmap_reg_range(0x8840, 0x8843),
	/* VIDEO Output Format */
	regmap_reg_range(0x8A00, 0x8A0D),
	regmap_reg_range(0x8AB0, 0x8AB0),
};

static const struct regmap_access_table hdmi_regmap_access = {
	.yes_ranges = hdmi_regmap_rw_ranges,
	.n_yes_ranges = ARRAY_SIZE(hdmi_regmap_rw_ranges),
};

static const struct regmap_config hdmi_regmap_config = {
	.reg_bits = 16,
	.reg_stride = 1,
	.val_bits = 8,
	.cache_type = REGCACHE_NONE,
	.max_register = 0x8fff,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.rd_table = &hdmi_regmap_access,
	.wr_table = &hdmi_regmap_access,
	.name = "hdmi",
};

static const struct regmap_range edid_regmap_rw_ranges[] = {
	regmap_reg_range(0x8c00, 0x8cff),
};

static const struct regmap_access_table edid_regmap_access = {
	.yes_ranges = edid_regmap_rw_ranges,
	.n_yes_ranges = ARRAY_SIZE(edid_regmap_rw_ranges),
};

static const struct regmap_config edid_regmap_config = {
	.reg_bits = 16,
	.reg_stride = 1,
	.val_bits = 32,
	.cache_type = REGCACHE_NONE,
	.max_register = 0x8fff,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.rd_table = &edid_regmap_access,
	.wr_table = &edid_regmap_access,
	.name = "edid",
};

static int uh2c_hdmi_read_u16(struct uh2c *priv, unsigned reg,
			unsigned int *val)
{
	unsigned low, high;
	int err;

	err = regmap_read(priv->hdmi_regmap, reg, &low);
	if (!err)
		err = regmap_read(priv->hdmi_regmap, reg + 1, &high);
	if (!err)
		*val = (high << 8) | low;

	return err;
}

static int uh2c_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
		enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(uh2c_pixelcode))
	    return -EINVAL;

	*code = uh2c_pixelcode[index];
	return 0;
}

static int uh2c_priv_try_fmt(struct uh2c *priv,
		struct v4l2_mbus_framefmt *fmt)
{
	if (!priv->vsync)
		return -ENODATA;

	switch(fmt->code) {
	case V4L2_MBUS_FMT_RGB888_1X24:
		fmt->colorspace = V4L2_COLORSPACE_SRGB;
		break;
	case V4L2_MBUS_FMT_UYVY8_2X8:
		switch (fmt->colorspace) {
		case V4L2_COLORSPACE_SMPTE170M:
		case V4L2_COLORSPACE_REC709:
			break;
		default:
			fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
		}
		break;
	default: /* Default to progressive RGB */
		fmt->code = V4L2_MBUS_FMT_RGB888_1X24;
		fmt->field = V4L2_FIELD_NONE;
		fmt->colorspace = V4L2_COLORSPACE_SRGB;
		break;
	}

	fmt->width = priv->framefmt.width;
	fmt->height = priv->framefmt.height;
	fmt->field = priv->framefmt.field;

	return 0;
}


static int uh2c_try_fmt(struct v4l2_subdev *sd,
		struct v4l2_mbus_framefmt *fmt)
{
	struct uh2c *priv = container_of(sd, struct uh2c, subdev);
	int err;

	mutex_lock(&priv->lock);
	err = uh2c_priv_try_fmt(priv, fmt);
	mutex_unlock(&priv->lock);

	return err;
}

static int uh2c_g_fmt(struct v4l2_subdev *sd,
		struct v4l2_mbus_framefmt *fmt)
{
	struct uh2c *priv = container_of(sd, struct uh2c, subdev);
	int err = 0;

	mutex_lock(&priv->lock);
	if (priv->vsync)
		*fmt = priv->framefmt;
	else
		err = -ENODATA;
	mutex_unlock(&priv->lock);

	return err;
}

static int uh2c_s_fmt(struct v4l2_subdev *sd,
		struct v4l2_mbus_framefmt *fmt)
{
	struct uh2c *priv = container_of(sd, struct uh2c, subdev);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	unsigned int mode, colorspace;
	unsigned csi_fmt;
	int err;

	mutex_lock(&priv->lock);
	err = uh2c_priv_try_fmt(priv, fmt);
	if (err)
		goto finish;

	switch(fmt->code) {
	case V4L2_MBUS_FMT_RGB888_1X24:
		mode = 0;
		csi_fmt = 0x24;
		break;
	case V4L2_MBUS_FMT_UYVY8_2X8:
		mode = 1;
		csi_fmt = 0x1E;
		break;
	default:
		err = -EINVAL;
		goto finish;
	}

	switch(fmt->colorspace) {
	case V4L2_COLORSPACE_SRGB:
		colorspace = 0; /* RGB Full */
		break;
	case V4L2_COLORSPACE_SMPTE170M:
		colorspace = 3; /* 601 YCbCr Limited */
		break;
	case V4L2_COLORSPACE_REC709:
		colorspace = 5; /* 709 YCbCr Limited */
		break;
	default:
		err = -EINVAL;
		goto finish;
	}

	err = regmap_write(priv->hdmi_regmap, VOUT_FMT, mode);
	if (!err)
		err = regmap_write(priv->hdmi_regmap, VOUT_CSC,
				(colorspace << 4) | 1);
	if (!err)
		err = regmap_update_bits(priv->ctl_regmap, CONFCTL0,
					3 << 6, (mode ? 3 : 0) << 6);
	/* Set the packet type for interlaced formats */
	if (!err)
		err = regmap_write(priv->ctl_regmap, STX_PACKETID1(0),
				(csi_fmt << 8) | csi_fmt);
	if (!err)
		err = regmap_write(priv->ctl_regmap, STX_PACKETID1(1),
				(csi_fmt << 8) | csi_fmt);

	/* Setup the splitter */
	if (priv->ep[0].flags) {
		if (!err)
			err = regmap_write(priv->ctl_regmap,
					STX_CTRL(0), BIT(8) | BIT(0));
		if (!err)
			err = regmap_write(priv->ctl_regmap,
					STX_FPX(0), 0);
		if (!err)
			err = regmap_write(priv->ctl_regmap,
					STX_LPX(0), fmt->width);
		if (!err)
			err = regmap_write(priv->ctl_regmap,
					STX_CTRL(1), 0);
	} else {
		if (!err)
			err = regmap_write(priv->ctl_regmap,
					STX_CTRL(0), 0);
		if (!err)
			err = regmap_write(priv->ctl_regmap,
					STX_FPX(0), BIT(14));
		if (!err)
			err = regmap_write(priv->ctl_regmap,
					STX_CTRL(1), BIT(8) | BIT(0));
		if (!err)
			err = regmap_write(priv->ctl_regmap,
					STX_FPX(1), 0);
		if (!err)
			err = regmap_write(priv->ctl_regmap,
					STX_LPX(1), fmt->width);
	}

	if (err)
		dev_err(&client->dev, "Failed to set format\n");

finish:
	mutex_unlock(&priv->lock);
	return err;
}

static int uh2c_g_mbus_config(struct v4l2_subdev *sd,
		struct v4l2_mbus_config *cfg)
{
	struct uh2c *priv = container_of(sd, struct uh2c, subdev);
	struct v4l2_of_bus_mipi_csi2 *ep =
		priv->ep[0].flags ? &priv->ep[0] : &priv->ep[1];

	if (ep->num_data_lanes < 1 || ep->num_data_lanes > 4)
		return -EINVAL;

	cfg->type = V4L2_MBUS_CSI2;
	cfg->flags = ep->flags | V4L2_MBUS_CSI2_CHANNEL_0;
	cfg->flags |= V4L2_MBUS_CSI2_1_LANE << (ep->num_data_lanes - 1);

	return 0;
}

static int uh2c_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *cc)
{
	struct uh2c *priv = container_of(sd, struct uh2c, subdev);

	cc->bounds.left = 0;
	cc->bounds.top = 0;
	cc->bounds.width = priv->framefmt.width;
	cc->bounds.height = priv->framefmt.height;
	cc->defrect = cc->bounds;
	cc->pixelaspect = priv->pixelaspect;

	return 0;
}

static int uh2c_s_stream(struct v4l2_subdev *sd, int on)
{
	struct uh2c *priv = container_of(sd, struct uh2c, subdev);
	int i, err = 0;

	mutex_lock(&priv->lock);

	if (!priv->vsync) {
		mutex_unlock(&priv->lock);
		return -ENODATA;
	}

	if (on) {
		unsigned long enable = BIT(15);

		for (i = 0; !err && i < ARRAY_SIZE(priv->ep); i++) {
			if (!priv->ep[i].flags)
				continue;
			enable |= BIT(i);
			err = regmap_write(priv->csi_regmap,
					CSITX_START(i), 1);
		}

		if (!err)
			err = regmap_update_bits(priv->ctl_regmap, CONFCTL0,
						BIT(0) | BIT(1) | BIT(15),
						enable);
		/* Disable the video mute */
		if (!err)
			err = regmap_write(priv->hdmi_regmap,
					VMUTE_STATUS, 0);
	} else {
		err = regmap_update_bits(priv->ctl_regmap, CONFCTL0,
					BIT(0) | BIT(1), 0);
	}

	mutex_unlock(&priv->lock);

	return err;
}


static int uh2c_g_chip_ident(struct v4l2_subdev *sd,
		struct v4l2_dbg_chip_ident *id)
{
	struct uh2c *priv = container_of(sd, struct uh2c, subdev);
	unsigned int val;
	int err;

	err = regmap_read(priv->ctl_regmap, CHIPID, &val);
	if (err)
		return err;

	id->ident = (val >> 8) & 0xFF;
	id->revision = val & 0xFF;

	return 0;
}

static int uh2c_s_power(struct v4l2_subdev *sd, int on)
{
	return 0;
}

static struct v4l2_subdev_video_ops uh2c_subdev_video_ops = {
	.s_mbus_fmt = uh2c_s_fmt,
	.g_mbus_fmt = uh2c_g_fmt,
	.try_mbus_fmt = uh2c_try_fmt,
	.enum_mbus_fmt = uh2c_enum_fmt,
	.cropcap = uh2c_cropcap,
	.g_mbus_config = uh2c_g_mbus_config,
	.s_stream = uh2c_s_stream,
};

static struct v4l2_subdev_core_ops uh2c_subdev_core_ops = {
	.g_chip_ident = uh2c_g_chip_ident,
	.s_power = uh2c_s_power,
	.subscribe_event = v4l2_src_change_event_subdev_subscribe,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static struct v4l2_subdev_ops uh2c_subdev_ops = {
	.core = &uh2c_subdev_core_ops,
	.video = &uh2c_subdev_video_ops,
};

#if IS_ENABLED(CONFIG_SND_SOC)
static int uh2c_dai_set_fmt(struct snd_soc_dai *dai,
			      unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct uh2c *priv = snd_soc_codec_get_drvdata(codec);
	unsigned mode0 = 0, mode1 = 0, confctl0 = 0;
	int err;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_LEFT_J:
		mode1 |= SDO_MODE1_FMT_LEFT_J;
		confctl0 |= CONFCTL0_AUD_OUT_SEL_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		mode1 |= SDO_MODE1_FMT_RIGHT_J;
		confctl0 |= CONFCTL0_AUD_OUT_SEL_I2S;
		break;
	case SND_SOC_DAIFMT_I2S:
		mode1 |= SDO_MODE1_FMT_I2S;
		confctl0 |= CONFCTL0_AUD_OUT_SEL_I2S;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		confctl0 |= BIT(8);
	case SND_SOC_DAIFMT_DSP_B:
		confctl0 |= CONFCTL0_AUD_OUT_SEL_TDM;
		mode1 |= SDO_MODE1_FMT_LEFT_J;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_NB_IF:
		mode0 |= BIT(0);
		break;
	case SND_SOC_DAIFMT_IB_NF:
		mode0 |= BIT(2);
		break;
	case SND_SOC_DAIFMT_IB_IF:
		mode0 |= BIT(0) | BIT(2);
		break;
	default:
		return -EINVAL;
	}

	/* Update AudOutSel and I2SDlyOpt */
	err = regmap_update_bits(priv->ctl_regmap, CONFCTL0,
				CONFCTL0_AUD_OUT_SEL_MASK | BIT(8),
				confctl0);
	if (err)
		return err;
	/* Update LR_POL and BCK_POL */
	err = regmap_update_bits(priv->hdmi_regmap, SDO_MODE0,
				BIT(0) | BIT(2), mode0);
	if (err)
		return err;
	/* Update SDO_FMT */
	err = regmap_update_bits(priv->hdmi_regmap, SDO_MODE1,
				SDO_MODE1_FMT_MASK, mode1);
	if (err)
		return err;

	return 0;
}

static unsigned uh2c_audio_rates[16] = {
	/* 0 */
	44100,
	0,
	48000,
	32000,
	/* 4 */
	22050,
	384000,
	24000,
	352800,
	/* 8 */
	88200,
	768000,
	96000,
	705600,
	/* C */
	176400,
	0,
	192000,
	0,
};

static int uh2c_dai_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params,
			struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct uh2c *priv = snd_soc_codec_get_drvdata(codec);
	unsigned channels = params_channels(params);
	unsigned rate = params_rate(params);
	unsigned max_channels;
	unsigned mode1 = 0;
	unsigned confctl0;
	unsigned fs_set;
	int err;

	/* Check that we have a signal */
	mutex_lock(&priv->lock);

	if (!priv->vsync) {
		mutex_unlock(&priv->lock);
		return -ENODATA;
	}

	mutex_unlock(&priv->lock);

	err = regmap_read(priv->ctl_regmap, CONFCTL0, &confctl0);
	if (err)
		return err;

	/* I2S mode only supports stereo, TDM up to 8 */
	max_channels = (confctl0 & CONFCTL0_AUD_OUT_SEL_MASK) ==
			CONFCTL0_AUD_OUT_SEL_I2S ? 2 : 8;
	if (channels > max_channels) {
		dev_err(codec->dev, "Too many channels\n");
		return -EINVAL;
	}

	err = regmap_read(priv->hdmi_regmap, FS_SET, &fs_set);
	if (err)
		return err;

	/* Check that we have PCM audio at the requested rate */
	if (fs_set & BIT(4)) {
		dev_err(codec->dev, "Audio is compressed\n");
		return -EINVAL;
	}

	if (rate != uh2c_audio_rates[fs_set & 0xF]) {
		dev_err(codec->dev, "Current rate is %d, requested %d\n",
			uh2c_audio_rates[fs_set & 0xF], rate);
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S18_3LE:
		mode1 |= 2 << 4;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		mode1 |= 4 << 4;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		mode1 |= 6 << 4;
		break;
	default:
		return -EINVAL;
	}

	err = regmap_update_bits(priv->hdmi_regmap, SDO_MODE1, 7 << 4, mode1);
	if (err)
		return err;

	return 0;
}

static int uh2c_dai_startup(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct uh2c *priv = snd_soc_codec_get_drvdata(codec);
	int err;

	err = regmap_update_bits(priv->ctl_regmap, CONFCTL0, BIT(5), BIT(5));
	if (err)
		return err;

	err = regmap_write(priv->hdmi_regmap, FORCE_MUTE, 0);
	if (err)
		return err;

	return 0;
}

static void uh2c_dai_shutdown(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct uh2c *priv = snd_soc_codec_get_drvdata(codec);

	regmap_write(priv->hdmi_regmap, FORCE_MUTE, BIT(0) | BIT(4));
	regmap_update_bits(priv->ctl_regmap, CONFCTL0, BIT(5), 0);
}

static const struct snd_soc_dai_ops uh2c_dai_ops = {
	.hw_params	= uh2c_dai_hw_params,
	.set_fmt	= uh2c_dai_set_fmt,
	.startup	= uh2c_dai_startup,
	.shutdown	= uh2c_dai_shutdown,
};

static struct snd_soc_dai_driver uh2c_dai = {
	.name = "uh2c-hifi",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
			SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_22050 |
			/* SNDRV_PCM_RATE_24000 |*/ SNDRV_PCM_RATE_88200 |
			SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
			SNDRV_PCM_RATE_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S18_3LE |
			SNDRV_PCM_FMTBIT_S20_3LE |
			SNDRV_PCM_FMTBIT_S24_LE,
	},
	.ops = &uh2c_dai_ops,
	.symmetric_rates = 0,
};

static struct snd_soc_codec_driver soc_codec_dev_uh2c = {
};

static int uh2c_audio_register(struct uh2c *priv)
{
	struct i2c_client *client = v4l2_get_subdevdata(&priv->subdev);
	int err;

	/* Enable the I2S/TDM clock only when needed */
	err = regmap_update_bits(priv->ctl_regmap,
				CONFCTL0, BIT(12), BIT(12));
	if (err)
		return err;

	/* FS_IMODE: FS/NLPCM from AUD_Info */
	err = regmap_write(priv->hdmi_regmap, FS_IMODE, BIT(1) | BIT(5));
	if (err)
		return err;

	/* Mute unsupported sample rates */
	err = regmap_write(priv->hdmi_regmap, FS_MUTE,
			BIT(0) | BIT(5) | BIT(7));
	if (err)
		return err;

	/* Mute all I2S lines on MUTE */
	err = regmap_update_bits(priv->hdmi_regmap, MUTE_MODE,
				BIT(0) | BIT(1) | BIT(2),
				BIT(0) | BIT(1) | BIT(2));
	if (err)
		return err;

	/* Enable the I2S interface */
	err = regmap_update_bits(priv->ctl_regmap,
				SYSCTL, BIT(7), 0);
	if (err)
		return err;

	return snd_soc_register_codec(&client->dev,
				&soc_codec_dev_uh2c, &uh2c_dai, 1);

}
#else
static int uh2c_audio_register(struct uh2c *priv)
{
	return 0;
}
#endif

static int uh2c_hdmi_vsync_changed_irq_handler(struct uh2c *priv)
{
	struct i2c_client *client = v4l2_get_subdevdata(&priv->subdev);
	unsigned int status, width, height, vi_status, vi_status1;
	struct v4l2_event ev = {};
	unsigned int repeat = 0;
	int err;

	err = regmap_read(priv->hdmi_regmap, SYS_STATUS, &status);
	if (err)
		return 0;

	dev_dbg(&client->dev, "VSync changed: %s (0x%02x)\n",
		(status & BIT(7)) ? "found" : "lost", status);

	mutex_lock(&priv->lock);
	priv->vsync = false;

	if (status & BIT(7)) {
		if (!err)
			err = regmap_read(priv->hdmi_regmap,
					VI_STATUS, &vi_status);
		if (!err)
			err = regmap_read(priv->hdmi_regmap,
					VI_STATUS1, &vi_status1);
		if (!err)
			err = uh2c_hdmi_read_u16(priv, DE_HSIZE, &width);
		if (!err)
			err = uh2c_hdmi_read_u16(priv, DE_VSIZE, &height);
		if (!err) {
			repeat = ((vi_status >> 4) & 0xF) + 1;
			priv->framefmt.width = width / repeat;
			priv->framefmt.height = height;
			if (vi_status1 & BIT(0)) {
				priv->framefmt.height *= 2;
				priv->framefmt.field = V4L2_FIELD_INTERLACED_TB;
			} else {
				priv->framefmt.field = V4L2_FIELD_NONE;
			}
			/* 480i/p and 576i/p have special aspect ratio */
			switch(priv->framefmt.height) {
			case 576:
				priv->pixelaspect.numerator = 16;
				priv->pixelaspect.denominator = 15;
				break;
			case 480:
				priv->pixelaspect.numerator = 8;
				priv->pixelaspect.denominator = 9;
				break;
			default:
				priv->pixelaspect.numerator = 1;
				priv->pixelaspect.denominator = 1;
				break;
			}
			/* Correct the aspect to account for pixel repeating */
			priv->pixelaspect.numerator *= repeat;
			priv->vsync = true;
			ev.type = V4L2_EVENT_SOURCE_CHANGE;
			ev.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION;

			dev_dbg(&client->dev,
				"Got new resolution: %ux%u%c (repeat %d)\n",
				width, height,
				(vi_status1 & BIT(0)) ? 'i' : 'p',
				repeat);
		}
	}

	/* If any of the above failed, report an End Of Stream */
	if (!priv->vsync) {
		priv->framefmt.width = 0;
		priv->framefmt.height = 0;
		priv->framefmt.field = V4L2_FIELD_ANY;
		priv->pixelaspect.numerator = 1;
		priv->pixelaspect.denominator = 1;
		ev.type = V4L2_EVENT_EOS;
	}

	mutex_unlock(&priv->lock);

	v4l2_subdev_notify(&priv->subdev, V4L2_DEVICE_NOTIFY_EVENT, &ev);

	return 1;
}

static int uh2c_hdmi_misc_irq_handler(struct uh2c *priv)
{
	unsigned int status, mask;
	int err;

	/* Get the current status and mask */
	err = regmap_read(priv->hdmi_regmap, MISC_INT, &status);
	if (!err)
		err = regmap_read(priv->hdmi_regmap, MISC_INTM, &mask);
	if (err)
		return 0;

	status &= ~mask;
	if (!status)
		return 0;

	if (status & BIT(1))
		uh2c_hdmi_vsync_changed_irq_handler(priv);

	/* And clear it */
	regmap_write(priv->hdmi_regmap, MISC_INT, status);

	return 1;
}

static int uh2c_hdmi_irq_handler(struct uh2c *priv)
{
	unsigned int int0;
	int err, ret = 0;

	err = regmap_read(priv->hdmi_regmap, HDMI_INT0, &int0);
	if (err)
		return 0;

	if (int0 & BIT(1))
		ret += uh2c_hdmi_misc_irq_handler(priv);

	return ret;
}

static irqreturn_t uh2c_irq_handler(int irq, void *ctx)
{
	struct uh2c *priv = ctx;
	struct i2c_client *client = v4l2_get_subdevdata(&priv->subdev);
	unsigned int status;
	int err, ret = 0;

	err = regmap_read(priv->ctl_regmap, INT_STATUS, &status);
	if (err) {
		dev_err(&client->dev, "Failed to read IRQ status\n");
		return IRQ_NONE;
	}

	if (status & BIT(9))
		ret += uh2c_hdmi_irq_handler(priv);

	/* Clear the status */
	regmap_write(priv->ctl_regmap, INT_STATUS, status);

	return ret > 0 ? IRQ_HANDLED : IRQ_NONE;
}

static int uh2c_load_edid(struct uh2c *priv, const void *data, unsigned size)
{
	int err;

	/* Max 1k */
	if (size > EDID_MAX_SIZE)
		return -EFBIG;
	/* Size must be multiple of 4 */
	if (size & 3)
		return -EINVAL;

	err = regmap_bulk_write(priv->edid_regmap,
				EDID_RAM, data, size / 4);

	if (!err)
		err = regmap_write(priv->hdmi_regmap,
				EDID_LEN1, size & 0xFF);
	if (!err)
		err = regmap_write(priv->hdmi_regmap,
				EDID_LEN2, (size >> 8) & 0xFF);

	return err;
}

static const u32 uh2c_default_edid[] = {
	0xFFFFFF00, 0x00FFFFFF, 0x02096252, 0x01010101,
	0x030114FF, 0x785AA080, 0xA0C90D0A, 0x27984757,
	0x2F4C4812, 0x808100CF, 0x01010101, 0x01010101,
	0x01010101, 0x3A020101, 0x38711880, 0x2C58402D,
	0x84400045, 0x1E000063, 0xB0502166, 0x301B0051,
	0x00367040, 0x0063843A, 0x00001E00, 0x5400FC00,
	0x4948534F, 0x542D4142, 0x20200A56, 0xFD000000,
	0x0F4C1700, 0x0A000F51, 0x20202020, 0xA9012020,
	0x70220302, 0x04051049, 0x06020703, 0x09232001,
	0x036C077F, 0x0030000C, 0x2BCF2D38, 0xE233332B,
	0x1D017F00, 0x1C711880, 0x2C582016, 0x84400025,
	0x9E000063, 0x8AD00A8C, 0x102DE020, 0x00963E10,
	0x004384B0, 0x1F0E1800, 0x00518000, 0x8040301E,
	0x84DC0037, 0x1C000053, 0xA00027F1, 0x30250051,
	0x00378050, 0x005384DC, 0x1AA91C00, 0x0050A000,
	0x20303016, 0x84DC0037, 0x1A000053, 0x0C000000,
};

static unsigned int clk_count(u64 rate, unsigned int ns)
{
	rate *= ns;
	if(do_div(rate, 1000000000))
		rate++; /* Round up the count */
	return rate;
}

static int uh2c_init_csi_tx(struct uh2c *priv, unsigned id,
			    unsigned long csi_rate)
{
	unsigned int tclk_pre, tclk_prepare, tclk_zero, tclk_exit, tclk_trail;
	struct i2c_client *client = v4l2_get_subdevdata(&priv->subdev);
	unsigned int ths_prepare, ths_zero, ths_exit, ths_trail;
	unsigned int lptxcnt, t_wakeup, tclk_post;
	unsigned long hsck_rate = csi_rate * 2;
	u32 frs = -1, prd, diff, best_diff = -1;
	u32 best_prd = -1, best_fbd = -1;
	int err, csi_shift;
	u64 fbd;

	for (csi_shift = 3; csi_shift > 0; csi_shift -= 1) {
		hsck_rate = csi_rate << csi_shift;

		/* Too fast, try the next rate */
		if (hsck_rate > 1000000000)
			continue;
		/* Too slow, abort the next rate will be lower */
		if (hsck_rate < 62500000)
			return -EINVAL;

		if (hsck_rate > 500000000)
			frs = 0;
		else if (hsck_rate > 250000000)
			frs = 1;
		else if (hsck_rate > 125000000)
			frs = 2;
		else
			frs = 3;
		break;
	}

	/* Make sure we have valid settings */
	if (frs == -1)
		return -EINVAL;

	/* Try all pre divider values and find the best one */
	for (prd = 1; prd <= 16 && best_diff; prd++) {
		fbd = ((u64)hsck_rate * prd) << frs;
		diff = do_div(fbd, priv->refrate);
		if (fbd > 0x100)
			continue;
		if (diff < best_diff) {
			best_prd = prd;
			best_fbd = fbd;
			best_diff = diff;
		}
	}

	/* Make sure have valid settings */
	if (best_prd == -1 || best_fbd == -1) {
		dev_err(&client->dev, "Failed to configure CSI PLL\n");
		return -EINVAL;
	}

	/* Compute the effective CSI rate and various timings */
	csi_rate = (priv->refrate / best_prd * best_fbd) >> (frs + csi_shift);

	/* LPTX clk must be less than 20MHz */
	lptxcnt = (csi_rate + 20000000 - 1) / 20000000;
	if (lptxcnt < 3)
		lptxcnt = 3;
	/* TWAKEUP >= 1ms (in LPTX clock count) */
	t_wakeup = clk_count(csi_rate / lptxcnt, 1000000);

	/* TCLK_PRE >= 8UI */
	tclk_pre = 4;
	/* 38ns <= TCLK_PREPARE <= 95ns */
	tclk_prepare = clk_count(csi_rate, 60);
	/* TCLK_ZERO + TCLK_PREPARE >= 300ns */
	tclk_zero = clk_count(csi_rate, 300) - tclk_prepare;
	/* TCLK_EXIT >= 100 ns */
	tclk_exit = clk_count(csi_rate, 100);
	/* TCLK_TRAIL >= 60ns */
	tclk_trail = clk_count(csi_rate, 60);
	/* TCLK_POST >= 60ns + 52*UI */
	tclk_post = clk_count(csi_rate, 60) + 26;

	/* 40ns + 4*UI <= THS_PREPARE <= 85ns + 6*UI, use 60ns + 4 UI */
	ths_prepare = clk_count(csi_rate, 60) + 2;
	/* THS_ZERO + THS_PREPARE >= 145ns + 10*UI */
	ths_zero = clk_count(csi_rate, 145) + 5 - ths_prepare;
	/* THS_EXIT >= 100ns */
	ths_exit = clk_count(csi_rate, 100);
	/* THS_TRAIL >= max(8*UI, 60ns + 4*UI) */
	ths_trail = clk_count(csi_rate, 60) + 2;
	if (ths_trail < 4)
		ths_trail = 4;

	/* Power the block */
	err = regmap_write(priv->csi_regmap, CSITX_CLKEN(id), 1);
	if (err)
		return err;

	/* Configure the PLL */
	if (!err)
		err = regmap_write(priv->csi_regmap, MIPI_PLL_CONF(id),
				((best_prd - 1) << 16) | (frs << 10) |
				(best_fbd - 1));

	/* Select the clocks for CSI clock and data */
	if (!err)
		err = regmap_write(priv->csi_regmap, PPI_CLKSEL(id),
				(3 - csi_shift) << 10 | (3 - csi_shift) << 8 |
				BIT(0));

	/* LINEINITCOUNT >= 100us */
	if (!err)
		err = regmap_write(priv->csi_regmap, LINE_INIT_COUNT(id),
				clk_count(csi_rate, 100000));
	/* HSTX_TO_COUNT = 0 */
	if (!err)
		err = regmap_write(priv->csi_regmap, HSTX_TO_COUNT(id), 0);
	/* Write the MIPI timings */
	if (!err)
		err = regmap_write(priv->csi_regmap,
				PPI_DPHY_LPTXTIMECNT(id),
				lptxcnt - 1);
	if (!err)
		err = regmap_write(priv->csi_regmap,
				PPI_DPHY_TCLK_HEADERCNT(id),
				(tclk_prepare << 16) | (tclk_pre << 8) |
				tclk_prepare);
	if (!err)
		err = regmap_write(priv->csi_regmap,
				PPI_DPHY_TCLK_TRAILCNT(id),
				(tclk_exit << 16) | tclk_trail);
	if (!err)
		err = regmap_write(priv->csi_regmap,
				PPI_DPHY_THS_HEADERCNT(id),
				(ths_zero << 16) | ths_prepare);
	if (!err)
		err = regmap_write(priv->csi_regmap,
				PPI_DPHY_TWAKEUPCNT(id),
				t_wakeup);
	if (!err)
		err = regmap_write(priv->csi_regmap,
				PPI_DPHY_TCLK_POSTCNT(id),
				tclk_post);
	if (!err)
		err = regmap_write(priv->csi_regmap,
				PPI_DPHY_THSTRAILCNT(id),
				(ths_exit << 16) | ths_trail);
	/* TODO: Find out how to compute this value. Which clock drives
	 * the counter, and how long should we wait?  */
	if (!err)
		err = regmap_write(priv->csi_regmap,
				PPI_DPHY_HSTXVREGCNT(id), 0x20);

	/* Enable all the voltage regulators */
	if (!err)
		err = regmap_write(priv->csi_regmap,
				PPI_DPHY_HSTXVREGEN(id), 0x1F);

	/* Enable the PLL */
	if (!err)
		err = regmap_write(priv->csi_regmap,
				MIPI_PLL_CTRL(id), 3);
	/* Enable the HSTX clock */
	if (!err)
		err = regmap_update_bits(priv->csi_regmap,
					PPI_CLKSEL(id), 1, 1);

	/* Set the wrappers for interlaced */
	if (!err)
		err = regmap_write(priv->ctl_regmap, STX_MAXFCNT(id), 2);

	/* Set the number of lanes */
	if (!err)
		err = regmap_write(priv->csi_regmap, LANE_ENABLE(id),
				BIT(4) | priv->ep[id].num_data_lanes);

	return err;
}

static int uh2c_priv_init(struct uh2c *priv)
{
	u64 nco;
	int err;

	/* Disable everything, but enable I2C address increments */
	err = regmap_write(priv->ctl_regmap, CONFCTL0, BIT(2));

	/* Take out of sleep */
	if (!err)
		err = regmap_write(priv->ctl_regmap, SYSCTL, BIT(7));

	/* HDMI system clock */
	if (!err)
		err = regmap_write(priv->hdmi_regmap, SYS_FREQ0,
				   (priv->refrate / 10000) & 0xFF);
	if (!err)
		err = regmap_write(priv->hdmi_regmap, SYS_FREQ1,
				   ((priv->refrate / 10000) >> 8) & 0xFF);
	/* Audio system clock */
	if (!err)
		err = regmap_write(priv->hdmi_regmap, LOCK_REF_FREQA,
				   (priv->refrate / 100) & 0xFF);
	if (!err)
		err = regmap_write(priv->hdmi_regmap, LOCK_REF_FREQB,
				   ((priv->refrate / 100) >> 8) & 0xFF);
	if (!err)
		err = regmap_write(priv->hdmi_regmap, LOCK_REF_FREQC,
				   ((priv->refrate / 100) >> 16) & 0xFF);
	/* Audio PLL */
	if (priv->refrate == 42000000) {
		if (!err)
			err = regmap_write(priv->hdmi_regmap, NCO_F0_MOD, 0);
	} else {
		if (!err)
			err = regmap_write(priv->hdmi_regmap, NCO_F0_MOD, 2);

		nco = (u64)6144000 * (1 << 28);
		do_div(nco, priv->refrate);
		if (!err)
			err = regmap_write(priv->hdmi_regmap, NCO_48F0A,
					   nco & 0xFF);
		if (!err)
			err = regmap_write(priv->hdmi_regmap, NCO_48F0B,
					   (nco >> 8) & 0xFF);
		if (!err)
			err = regmap_write(priv->hdmi_regmap, NCO_48F0C,
					   (nco >> 16) & 0xFF);
		if (!err)
			err = regmap_write(priv->hdmi_regmap, NCO_48F0D,
					   (nco >> 24) & 0xFF);

		nco = (u64)5644800 * (1 << 28);
		do_div(nco, priv->refrate);
		if (!err)
			err = regmap_write(priv->hdmi_regmap, NCO_44F0A,
					   nco & 0xFF);
		if (!err)
			err = regmap_write(priv->hdmi_regmap, NCO_44F0B,
					   (nco >> 8) & 0xFF);
		if (!err)
			err = regmap_write(priv->hdmi_regmap, NCO_44F0C,
					   (nco >> 16) & 0xFF);
		if (!err)
			err = regmap_write(priv->hdmi_regmap, NCO_44F0D,
					   (nco >> 24) & 0xFF);
	}

	/* CSC controller */
	if (!err)
		err = regmap_write(priv->hdmi_regmap, SCLK_CSC0,
				   (priv->refrate / 10000) & 0xFF);
	if (!err)
		err = regmap_write(priv->hdmi_regmap, SCLK_CSC1,
				   ((priv->refrate / 10000) >> 8) & 0xFF);

	/* Enable the audio PLL */
	if (!err)
		err = regmap_write(priv->hdmi_regmap, APLL_CTL, 0x31);

	/* Load the EDID data */
	if (!err)
		err = uh2c_load_edid(priv, uh2c_default_edid,
				     sizeof(uh2c_default_edid));
	/* Set the EDID mode to RAM */
	if (!err)
		err = regmap_write(priv->hdmi_regmap, EDID_MODE, 1);

	/* Link the PHY to DDC */
	if (!err)
		err = regmap_write(priv->hdmi_regmap, PHY_CTL, 3);
	/* Enable the DCC */
	if (!err)
		err = regmap_write(priv->hdmi_regmap, DDCIO_CTL, 1);

	/* Enable auto video mute */
	if (!err)
		err = regmap_write(priv->hdmi_regmap, V_MUTE1, 3);

	/* Enable the HDMI misc IRQ we need */
	if (!err)
		err = regmap_write(priv->hdmi_regmap, MISC_INTM, ~BIT(1));

	/* Enable the HDMI IRQ */
	if (!err)
		err = regmap_write(priv->ctl_regmap, INT_MASK, ~(BIT(9)));

	/* Finish the HDMI init */
	if (!err)
		err = regmap_write(priv->hdmi_regmap, INIT_END, 1);

	/* Setup the CSI transmiters */
	if (!err && priv->ep[0].flags)
		err = uh2c_init_csi_tx(priv, 0, 480000000);
	if (!err && priv->ep[1].flags)
		err = uh2c_init_csi_tx(priv, 1, 480000000);

	/* Set the default format to RGB */
	if (!err)
		err = regmap_write(priv->hdmi_regmap, VOUT_FMT, 0);
	priv->framefmt.code = V4L2_MBUS_FMT_RGB888_1X24;

	/* Enable the CSC */
	if (!err)
		err = regmap_write(priv->hdmi_regmap, VOUT_CSC, 1);

	return err;
}

static int uh2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device_node *np = NULL;
	struct uh2c *priv;
	u32 rate;
	int err;

	priv = devm_kzalloc(&client->dev, sizeof(struct uh2c), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Read the ports config from OF */
	while ((np = v4l2_of_get_next_endpoint(client->dev.of_node, np))) {
		struct v4l2_of_endpoint ep;

		if (!of_device_is_available(np))
			continue;

		v4l2_of_parse_endpoint(np, &ep);
		of_node_put(np);

		if (ep.bus_type != V4L2_MBUS_CSI2 || ep.port > 0 || ep.id > 1) {
			dev_err(&client->dev, "Endpoint is invalid\n");
			return -EINVAL;
		}

		priv->ep[ep.id] = ep.bus.mipi_csi2;
	}

	if (priv->ep[0].flags == 0 && priv->ep[1].flags == 0) {
		dev_err(&client->dev, "No port configured\n");
		return -EINVAL;
	}

	if (priv->ep[0].flags && priv->ep[1].flags) {
		dev_err(&client->dev,
			"Dual port configuration not yet supported\n");
		return -EINVAL;
	}

	memcpy(priv->regulators, &uh2c_regulators, sizeof(priv->regulators));
	err = devm_regulator_bulk_get(&client->dev, ARRAY_SIZE(priv->regulators),
				priv->regulators);
	if (err < 0) {
		if (err != -EPROBE_DEFER)
			dev_err(&client->dev, "failed to get regulators\n");
		return err;
	}

	/* FIXME: We should use a clock here, but the generic clock framework
	 * is not supported on Tegra with this kernel. */
	err = of_property_read_u32(client->dev.of_node, "clock-rate", &rate);
	if (err) {
		dev_err(&client->dev, "failed to get clock rate\n");
		return -EINVAL;
	}
	if (rate < 40000000 || rate > 50000000) {
		dev_err(&client->dev, "reference is out of range: %lu\n",
			priv->refrate);
		return -EINVAL;
	}
	priv->refrate = rate;

	priv->reset_gpio = devm_gpiod_get_optional(
		&client->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset_gpio)) {
		if (PTR_ERR(priv->reset_gpio) != -EPROBE_DEFER)
			dev_err(&client->dev, "failed to get reset GPIO\n");
		return PTR_ERR(priv->reset_gpio);
	}

	mutex_init(&priv->lock);

	err = regulator_bulk_enable(
		ARRAY_SIZE(priv->regulators), priv->regulators);
	if (err) {
		dev_err(&client->dev, "failed to enable regulators\n");
		return err;
	}

	if (priv->reset_gpio) {
		gpiod_set_value_cansleep(priv->reset_gpio, 0);
		usleep_range(10, 20);
	}

	priv->ctl_regmap = devm_regmap_init_i2c(client, &ctl_regmap_config);
	if (IS_ERR(priv->ctl_regmap)) {
		dev_err(&client->dev,
			"regmap ctl init failed: %ld\n",
			PTR_ERR(priv->ctl_regmap));
		err = PTR_ERR(priv->ctl_regmap);
		goto reset;
	}

	priv->csi_regmap = devm_regmap_init_i2c(client, &csi_regmap_config);
	if (IS_ERR(priv->csi_regmap)) {
		dev_err(&client->dev,
			"regmap csi init failed: %ld\n",
			PTR_ERR(priv->csi_regmap));
		err = PTR_ERR(priv->csi_regmap);
		goto reset;
	}

	priv->hdmi_regmap = devm_regmap_init_i2c(client, &hdmi_regmap_config);
	if (IS_ERR(priv->hdmi_regmap)) {
		dev_err(&client->dev,
			"regmap hdmi init failed: %ld\n",
			PTR_ERR(priv->hdmi_regmap));
		err = PTR_ERR(priv->hdmi_regmap);
		goto reset;
	}

	priv->edid_regmap = devm_regmap_init_i2c(client, &edid_regmap_config);
	if (IS_ERR(priv->edid_regmap)) {
		dev_err(&client->dev,
			"regmap edid init failed: %ld\n",
			PTR_ERR(priv->edid_regmap));
		err = PTR_ERR(priv->edid_regmap);
		goto reset;
	}

	v4l2_i2c_subdev_init(&priv->subdev, client, &uh2c_subdev_ops);

	err = request_threaded_irq(client->irq, NULL, uh2c_irq_handler,
				IRQF_ONESHOT, dev_name(&client->dev), priv);
	if (err) {
		dev_err(&client->dev, "failed to request IRQ %d: %d\n",
			client->irq, err);
		goto reset;
	}

	err = uh2c_priv_init(priv);
	if (err) {
		dev_err(&client->dev, "failed to init chip: %d\n", err);
		goto free_irq;
	}

	err = v4l2_async_register_subdev(&priv->subdev);
	if (err) {
		dev_err(&client->dev, "Failed to register async subdev\n");
		goto free_irq;
	}

	err = uh2c_audio_register(priv);
	if (err) {
		dev_err(&client->dev, "Failed to register audio codec\n");
		goto v4l2_async_unregister;
	}

	return 0;

v4l2_async_unregister:
	v4l2_async_unregister_subdev(&priv->subdev);
free_irq:
	regmap_write(priv->ctl_regmap, INT_MASK, ~0);
	free_irq(client->irq, priv);
reset:
	if (priv->reset_gpio)
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(priv->regulators), priv->regulators);
	return err;
}

static int uh2c_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct uh2c *priv = container_of(sd, struct uh2c, subdev);

	v4l2_async_unregister_subdev(&priv->subdev);

	/* Make sure we get no stray interrupt when going into reset */
	regmap_write(priv->ctl_regmap, INT_MASK, ~0);
	free_irq(client->irq, priv);

	if (priv->reset_gpio)
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(priv->regulators), priv->regulators);

	return 0;
}

static const struct i2c_device_id uh2c_id[] = {
	{ "tc358840xbg", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, uh2c_id);

#ifdef CONFIG_OF
static const struct of_device_id uh2c_of_table[] = {
	{ .compatible = "toshiba,tc358840xbg" },
	{ }
};
MODULE_DEVICE_TABLE(of, uh2c_of_table);
#endif

static struct i2c_driver uh2c_driver = {
	.driver = {
		.of_match_table = of_match_ptr(uh2c_of_table),
		.name = "uh2c",
		.owner = THIS_MODULE,
	},
	.probe = uh2c_probe,
	.remove = uh2c_remove,
	.id_table = uh2c_id,
};
module_i2c_driver(uh2c_driver);

MODULE_DESCRIPTION("Driver for Toshiba TC358840 HDMI-CSI bridge");
MODULE_AUTHOR("Alban Bedel <alban.bedel@avionic-design.de>");
MODULE_LICENSE("GPL");
