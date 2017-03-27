/*
 * Copyright 2017 Alban Bedel <alban.bedel@avionic-design.de>
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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/clk.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-of.h>

#define OV468X_SC_CTRL0100		0x0100
#define OV468X_SC_CTRL0103		0x0103
#define OV468X_SC_CMMN_BIT_SEL		0x3031
#define OV468X_AEC_LONG_EXPO		0x3500
#define OV468X_AEC_LONG_GAIN		0x3507
#define OV468X_H_WIN_OFF		0x3810
#define OV468X_V_WIN_OFF		0x3812
#define OV468X_FORMAT1			0x3820
#define OV468X_FORMAT2			0x3821
#define OV468X_OTP_LOAD_CTRL		0x3d81
#define OV468X_OTP_MODE_CTRL		0x3d84
#define OV468X_MIPI_CTRL_00		0x4800
#define OV468X_ISP_CTRL0		0x5000
#define OV468X_PRE_CTRL00		0x5040
#define OV468X_OTP_SRAM(x)		(0x7000 + (x))

#define OV468X_EXTCLK_MIN_RATE		6000000
#define OV468X_EXTCLK_MAX_RATE		64000000
#define OV468X_EXTCLK_DEFAULT_RATE	24000000
#define OV468X_INIT_EXTCLK_CYCLES	8192

/* Custom controls */
#define V4L2_CID_OV468X_TEST_ROLLING_BAR	(V4L2_CID_USER_OV468X_BASE + 0)
#define V4L2_CID_OV468X_TEST_TRANSPARENT	(V4L2_CID_USER_OV468X_BASE + 1)

static const struct regmap_range ov468x_regmap_rw_ranges[] = {
	/* system control */
	regmap_reg_range(0x0100, 0x0100),
	regmap_reg_range(0x0103, 0x0103),
	/* PLL control */
	regmap_reg_range(0x0300, 0x0312),
	regmap_reg_range(0x031b, 0x031c),
	regmap_reg_range(0x031e, 0x031f),
	/* system control */
	regmap_reg_range(0x3000, 0x302a),
	regmap_reg_range(0x3030, 0x303f),
	/* sccb control */
	regmap_reg_range(0x3100, 0x3106),
	/* group hold */
	regmap_reg_range(0x3200, 0x320f),
	/* ASRAM control */
	regmap_reg_range(0x3300, 0x3318),
	/* ADC and analog control */
	regmap_reg_range(0x3600, 0x364c),
	/* sensor control */
	regmap_reg_range(0x3700, 0x379c),
	/* FREX control */
	regmap_reg_range(0x37c5, 0x37d6),
	regmap_reg_range(0x37de, 0x37df),
	/* timing control */
	regmap_reg_range(0x3800, 0x3836),
	regmap_reg_range(0x3841, 0x3841),
	regmap_reg_range(0x3846, 0x3847),
	/* strobe */
	regmap_reg_range(0x3b00, 0x3b00),
	regmap_reg_range(0x3b02, 0x3b05),
	/* PSRAM control */
	regmap_reg_range(0x3f00, 0x3f0a),
	/* ADC sync control */
	regmap_reg_range(0x4500, 0x4503),
	/* test mode */
	regmap_reg_range(0x8000, 0x8008),
	/* OTP control */
	regmap_reg_range(0x3d80, 0x3d8d),
	/* frame control */
	regmap_reg_range(0x4200, 0x4203),
	/* ISPFC */
	regmap_reg_range(0x4240, 0x4243),
	/* format clip */
	regmap_reg_range(0x4302, 0x4308),
	/* VFIFO */
	regmap_reg_range(0x4600, 0x4603),
	/* MIPI top */
	regmap_reg_range(0x4800, 0x4833),
	regmap_reg_range(0x4836, 0x483d),
	regmap_reg_range(0x484a, 0x484f),
	/* temperature monitor */
	regmap_reg_range(0x4d00, 0x4d23),
	/* AEC PK */
	regmap_reg_range(0x3500, 0x352b),
	/* BLC */
	regmap_reg_range(0x4000, 0x4033),
	/* ISP top */
	regmap_reg_range(0x5000, 0x5033),
	/* pre ISP control */
	regmap_reg_range(0x5040, 0x506c),
	/* bin control */
	regmap_reg_range(0x5301, 0x530f),
	/* OTP DPC control */
	regmap_reg_range(0x5000, 0x5000),
	regmap_reg_range(0x5500, 0x5509),
	regmap_reg_range(0x5524, 0x552a),
	/* windowing control */
	regmap_reg_range(0x5980, 0x598c),
	/* average control */
	regmap_reg_range(0x5680, 0x5693),
	/* OTP SRAM */
	regmap_reg_range(0x7000, 0x71FF),
};

static const struct regmap_access_table ov468x_regmap_access = {
	.yes_ranges = ov468x_regmap_rw_ranges,
	.n_yes_ranges = ARRAY_SIZE(ov468x_regmap_rw_ranges),
};

static struct regmap_config ov468x_regmap_config = {
	.reg_bits = 16,
	.reg_stride = 1,
	.val_bits = 8,
	.cache_type = REGCACHE_NONE,
	.max_register = 0xFFFF,
	.rd_table = &ov468x_regmap_access,
	.wr_table = &ov468x_regmap_access,
};

#define to_ov468x(sd) container_of(sd, struct ov468x_priv, subdev)

static const struct regulator_bulk_data ov468x_regulators[] = {
	{ "avdd" },
	{ "dovdd" },
	{ "dvdd" },
};

struct ov468x_mode {
	unsigned int			width;
	unsigned int			height;
	const struct reg_default	*regs;
	unsigned int			num_regs;
};

struct ov468x_priv {
	struct v4l2_subdev		subdev;
	struct v4l2_dbg_chip_ident	ident;
	struct v4l2_mbus_framefmt	mf;
	struct v4l2_ctrl_handler	ctrls;

	struct regmap			*regmap;
	struct regulator_bulk_data	regulators[ARRAY_SIZE(ov468x_regulators)];
	struct gpio_desc		*pwdnb;
	struct gpio_desc		*xshutdown;
	struct clk			*extclk;
	unsigned long			extclk_rate;

	unsigned int			h_win_off;
	unsigned int			v_win_off;
};

static const enum v4l2_mbus_pixelcode ov468x_mbus_pixelcodes[] = {
	V4L2_MBUS_FMT_SBGGR8_1X8,
	V4L2_MBUS_FMT_SBGGR10_1X10,
};

static const enum v4l2_mbus_pixelcode ov4682_mbus_pixelcodes[] = {
	V4L2_MBUS_FMT_SBGIR8_1X8,
	V4L2_MBUS_FMT_SBGIR10_1X10,
};

/* Basic configuration to apply on the defaults */
static const struct reg_default ov468x_reg_default[] = {
	/* Reset */
	{ 0x0103, 0x01 },

	/* Enable the OTP load */
	{0x3d85, 0x36},
	{0x3d8c, 0x71},
	{0x3d8d, 0xcb},

	{0x3305, 0xf1},
	{0x3307, 0x04},
	{0x3309, 0x29},
};

static const struct reg_default ov468x_mode_2688x1520_regs[] = {
	/* Analog ctrl */
	{0x3602, 0x00},
	{0x3603, 0x40},
	{0x3604, 0x02},
	{0x3605, 0x00},
	{0x3606, 0x00},
	{0x3607, 0x00},
	{0x3609, 0x12},
	{0x360a, 0x40},
	{0x360c, 0x08},
	{0x360f, 0xe5},
	{0x3608, 0x8f},
	{0x3611, 0x00},
	{0x3613, 0xf7},
	{0x3616, 0x58},
	{0x3619, 0x99},
	{0x361b, 0x60},
	{0x361c, 0x7a},
	{0x361e, 0x79},
	{0x361f, 0x02},
	{0x3632, 0x00},
	{0x3633, 0x10},
	{0x3634, 0x10},
	{0x3635, 0x10},
	{0x3636, 0x15},
	{0x3646, 0x86},
	{0x364a, 0x0b},

	/* Sensor ctrl */
	{0x3700, 0x17},
	{0x3701, 0x22},
	{0x3703, 0x10},
	{0x370a, 0x37},
	{0x3705, 0x00},
	{0x3706, 0x63},
	{0x3709, 0x3c},
	{0x370b, 0x01},
	{0x370c, 0x30},
	{0x3710, 0x24},
	{0x3711, 0x0c},
	{0x3716, 0x00},
	{0x3720, 0x28},
	{0x3729, 0x7b},
	{0x372a, 0x84},
	{0x372b, 0xbd},
	{0x372c, 0xbc},
	{0x372e, 0x52},
	{0x373c, 0x0e},
	{0x373e, 0x33},
	{0x3743, 0x10},
	{0x3744, 0x88},
	{0x3745, 0xc0},
	{0x374a, 0x43},
	{0x374c, 0x00},
	{0x374e, 0x23},
	{0x3751, 0x7b},
	{0x3752, 0x84},
	{0x3753, 0xbd},
	{0x3754, 0xbc},
	{0x3756, 0x52},
	{0x375c, 0x00},
	{0x3760, 0x00},
	{0x3761, 0x00},
	{0x3762, 0x00},
	{0x3763, 0x00},
	{0x3764, 0x00},
	{0x3767, 0x04},
	{0x3768, 0x04},
	{0x3769, 0x08},
	{0x376a, 0x08},
	{0x376b, 0x20},
	{0x376c, 0x00},
	{0x376d, 0x00},
	{0x376e, 0x00},
	{0x3773, 0x00},
	{0x3774, 0x51},
	{0x3776, 0xbd},
	{0x3777, 0xbd},
	{0x3781, 0x18},
	{0x3783, 0x25},
	{0x3798, 0x1b},

	/* Setup the window size */
	{0x3800, 0x00},
	{0x3801, 0x08},
	{0x3802, 0x00},
	{0x3803, 0x04},
	{0x3804, 0x0a},
	{0x3805, 0x97},
	{0x3806, 0x05},
	{0x3807, 0xfb},
	{0x3808, 0x0a},
	{0x3809, 0x80},
	{0x380a, 0x05},
	{0x380b, 0xf0},
	{0x380c, 0x03},
	{0x380d, 0x5c},
	{0x380e, 0x06},
	{0x380f, 0x12},
	{0x3810, 0x00},
	{0x3811, 0x08},
	{0x3812, 0x00},
	{0x3813, 0x04},
	{0x3814, 0x01},
	{0x3815, 0x01},
	{0x3819, 0x01},
	{0x3820, 0x00},
	{0x3821, 0x06},
	{0x3829, 0x00},
	{0x382a, 0x01},
	{0x382b, 0x01},
	{0x382d, 0x7f},
	{0x3830, 0x04},
	{0x3836, 0x01},
	{0x3837, 0x00},
	{0x3841, 0x02},
	{0x3846, 0x08},
	{0x3847, 0x07},

	/* Fix the ADC sync */
	{ 0x4500, 0x6c },
	{ 0x4503, 0x01 },

	/* Disable bining */
	{ 0x3820, 0x00 },
	{ 0x3821, 0x00 },
};

static const struct ov468x_mode ov468x_modes[] = {
	{
		.width = 2688,
		.height = 1520,
		.regs = ov468x_mode_2688x1520_regs,
		.num_regs = ARRAY_SIZE(ov468x_mode_2688x1520_regs),
	},
};

static int regmap_write_u16(struct regmap *regmap,
			unsigned int reg, unsigned int val)
{
	int err;

	err = regmap_write(regmap, reg + 1, val & 0xFF);
	if (err)
		return err;

	return regmap_write(regmap, reg, (val >> 8) & 0xFF);
}

static int regmap_write_u24(struct regmap *regmap,
			unsigned int reg, unsigned int val)
{
	int err;

	err = regmap_write_u16(regmap, reg + 1, val & 0xFFFF);
	if (err)
		return err;

	return regmap_write(regmap, reg, (val >> 16) & 0xFF);
}

static void ov468x_get_pixelcodes(struct ov468x_priv *priv,
				const enum v4l2_mbus_pixelcode **codes,
				unsigned int *num_codes)
{
	if (priv->ident.ident == 0x4682) {
		*codes = ov4682_mbus_pixelcodes;
		*num_codes = ARRAY_SIZE(ov4682_mbus_pixelcodes);
	} else {
		*codes = ov468x_mbus_pixelcodes;
		*num_codes = ARRAY_SIZE(ov468x_mbus_pixelcodes);
	}
}

static enum v4l2_mbus_pixelcode ov468x_try_pixelcode(
	struct ov468x_priv *priv, enum v4l2_mbus_pixelcode code)
{
	const enum v4l2_mbus_pixelcode *pixelcodes;
	unsigned int i, num_pixelcodes;

	ov468x_get_pixelcodes(priv, &pixelcodes, &num_pixelcodes);

	/* Check if the pixel code is valid */
	for (i = 0; i < num_pixelcodes; i++)
		if (code == pixelcodes[i])
			return pixelcodes[i];

	/* If not return the first one */
	return pixelcodes[0];
}

static const struct ov468x_mode *ov468x_get_mode(
	unsigned int width, unsigned int height)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ov468x_modes); i++)
		if (ov468x_modes[i].width >= width &&
		    ov468x_modes[i].height >= height)
			return &ov468x_modes[i];

	/* Return the largest mode as default */
	return &ov468x_modes[ARRAY_SIZE(ov468x_modes) - 1];
}

static int ov468x_try_mbus_fmt(struct v4l2_subdev *sd,
			       struct v4l2_mbus_framefmt *mf)
{
	struct ov468x_priv *priv = to_ov468x(sd);
	const struct ov468x_mode *mode;

	mode = ov468x_get_mode(mf->width, mf->height);
	mf->width = mode->width;
	mf->height = mode->height;

	mf->code = ov468x_try_pixelcode(priv, mf->code);
	mf->field = V4L2_FIELD_NONE;
	mf->colorspace = V4L2_COLORSPACE_SRGB;

	return 0;
}

static int ov468x_s_mbus_fmt(struct v4l2_subdev *sd,
			     struct v4l2_mbus_framefmt *mf)
{
	struct ov468x_priv *priv = to_ov468x(sd);
	const struct ov468x_mode *mode;
	unsigned int bits;
	int err;

	err = ov468x_try_mbus_fmt(sd, mf);
	if (err)
		return err;

	/* Setup the mode */
	mode = ov468x_get_mode(mf->width, mf->height);
	err = regmap_multi_reg_write(priv->regmap,
				mode->regs, mode->num_regs);
	if (err)
		return err;

	/* Set the MIPI bit depth */
	switch (mf->code) {
	case V4L2_MBUS_FMT_SBGGR8_1X8:
	case V4L2_MBUS_FMT_SBGIR8_1X8:
		bits = 8;
		break;
	case V4L2_MBUS_FMT_SBGGR10_1X10:
	case V4L2_MBUS_FMT_SBGIR10_1X10:
		bits = 10;
		break;
	default:
		return -EINVAL;
	}

	err = regmap_write(priv->regmap, OV468X_SC_CMMN_BIT_SEL, bits);
	if (err)
		return err;

	/* Read the window offsets as we need to adjust them flipping */
	err = regmap_read(priv->regmap, OV468X_H_WIN_OFF, &priv->h_win_off);
	if (err)
		return err;

	err = regmap_read(priv->regmap, OV468X_V_WIN_OFF, &priv->v_win_off);
	if (err)
		return err;

	/* Apply the controls */
	err = v4l2_ctrl_handler_setup(priv->subdev.ctrl_handler);
	if (err)
		return err;

	priv->mf = *mf;

	return 0;
}

static int ov468x_g_mbus_fmt(struct v4l2_subdev *sd,
			     struct v4l2_mbus_framefmt *mf)
{
	struct ov468x_priv *priv = to_ov468x(sd);

	*mf = priv->mf;

	return 0;
}

static int ov468x_enum_mbus_fmt(struct v4l2_subdev *sd, unsigned int index,
				enum v4l2_mbus_pixelcode *code)
{
	const enum v4l2_mbus_pixelcode *pixelcodes;
	struct ov468x_priv *priv = to_ov468x(sd);
	unsigned num_pixelcodes;

	ov468x_get_pixelcodes(priv, &pixelcodes, &num_pixelcodes);

	if (index >= num_pixelcodes)
		return -EINVAL;

	*code = pixelcodes[index];

	return 0;
}

static int ov468x_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_CSI2;
	cfg->flags = V4L2_MBUS_CSI2_4_LANE |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	return 0;
}

static int ov4689_enum_framesizes(
	struct v4l2_subdev *sd, struct v4l2_frmsizeenum *fsize)
{
	struct ov468x_priv *priv = to_ov468x(sd);
	enum v4l2_mbus_pixelcode code;

	/* Check the pixel format */
	code = ov468x_try_pixelcode(priv, fsize->pixel_format);
	if (code != fsize->pixel_format)
		return -EINVAL;

	if (fsize->index >=  ARRAY_SIZE(ov468x_modes))
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = ov468x_modes[fsize->index].width;
	fsize->discrete.height = ov468x_modes[fsize->index].height;

	return 0;
}

static int ov468x_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov468x_priv *priv = to_ov468x(sd);
	int err;

	err = regmap_write(priv->regmap, OV468X_SC_CTRL0100, !!enable);
	if (err)
		return err;

	/* The first frame can need up to 10 ms */
	msleep(10);

	return 0;
}

static int ov468x_poweron(struct ov468x_priv *priv)
{
	struct i2c_client *client = v4l2_get_subdevdata(&priv->subdev);
	unsigned long extclk_rate;
	unsigned long init_wait;
	int ret;

	/* Make sure the shutdown are enabled */
	if (priv->pwdnb)
		gpiod_set_value_cansleep(priv->pwdnb, 1);
	if (priv->xshutdown)
		gpiod_set_value_cansleep(priv->xshutdown, 1);

	/* Turn on the power */
	ret = regulator_bulk_enable(
		ARRAY_SIZE(priv->regulators), priv->regulators);
	if (ret) {
		dev_err(&client->dev, "Failed to enable regulators\n");
		return ret;
	}

	/* Release the shutdown */
	if (priv->pwdnb)
		gpiod_set_value_cansleep(priv->pwdnb, 0);
	if (priv->xshutdown)
		gpiod_set_value_cansleep(priv->xshutdown, 0);

	/* Enable the clock */
	if (priv->extclk) {
		ret = clk_prepare_enable(priv->extclk);
		if (ret) {
			dev_err(&client->dev,
				"Error enabling clock: %d\n", ret);
			goto shutdown;
		}
	}

	/* Wait for the chip init */
	extclk_rate = priv->extclk_rate ?: OV468X_EXTCLK_MIN_RATE;
	init_wait = (OV468X_INIT_EXTCLK_CYCLES * 1000) / (extclk_rate / 1000);
	/* The datasheet gives 8192, but the OV4682 seems to need a bit more */
	init_wait *= 4;
	usleep_range(init_wait, 2 * init_wait);

	/* Run the init */
	ret = regmap_multi_reg_write(priv->regmap, ov468x_reg_default,
				ARRAY_SIZE(ov468x_reg_default));
	if (ret)
		goto disable_clock;

	return 0;

disable_clock:
	if (priv->extclk)
		clk_disable_unprepare(priv->extclk);
shutdown:
	if (priv->pwdnb)
		gpiod_set_value_cansleep(priv->pwdnb, 1);
	if (priv->xshutdown)
		gpiod_set_value_cansleep(priv->xshutdown, 1);
	regulator_bulk_disable(ARRAY_SIZE(priv->regulators), priv->regulators);

	return ret;
}

static void ov468x_poweroff(struct ov468x_priv *priv)
{
	if (priv->extclk)
		clk_disable_unprepare(priv->extclk);
	if (priv->pwdnb)
		gpiod_set_value_cansleep(priv->pwdnb, 1);
	if (priv->xshutdown)
		gpiod_set_value_cansleep(priv->xshutdown, 1);
	regulator_bulk_disable(ARRAY_SIZE(priv->regulators), priv->regulators);
}

static int ov468x_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov468x_priv *priv = to_ov468x(sd);
	int ret = 0;

	if (on)
		ret = ov468x_poweron(priv);
	else
		ov468x_poweroff(priv);

	return ret;
}

static int ov468x_g_chip_ident(struct v4l2_subdev *sd,
			struct v4l2_dbg_chip_ident *id)
{
	struct ov468x_priv *priv = to_ov468x(sd);

	*id = priv->ident;

	return 0;
}

static const char *ov468x_test_pattern_names[] = {
	"Off",
	"Color bar 1",
	"Color bar 2",
	"Color bar 3",
	"Color bar 4",
	"Random data",
	"Color Squares",
	"B/W Squares",
	"Black image",
};

static const unsigned int ov468x_test_pattern_values[] = {
	0x00,
	0x80,
	0x84,
	0x88,
	0x8c,
	0x81,
	0x82,
	0x92,
	0x83,
};

static int ov468x_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov468x_priv *priv = container_of(
		ctrl->handler, struct ov468x_priv, ctrls);
	int err;

	compiletime_assert(ARRAY_SIZE(ov468x_test_pattern_names) ==
			ARRAY_SIZE(ov468x_test_pattern_values),
			"Test pattern names and values count mismatch");

	switch (ctrl->id) {
	case V4L2_CID_TEST_PATTERN:
		err = regmap_write(priv->regmap, OV468X_PRE_CTRL00,
				ov468x_test_pattern_values[ctrl->val]);
		break;
	case V4L2_CID_OV468X_TEST_ROLLING_BAR:
		err = regmap_update_bits(priv->regmap, OV468X_PRE_CTRL00,
					BIT(6), ctrl->val << 6);
		break;
	case V4L2_CID_OV468X_TEST_TRANSPARENT:
		err = regmap_update_bits(priv->regmap, OV468X_PRE_CTRL00,
					BIT(5), ctrl->val << 5);
		break;
	case V4L2_CID_VFLIP:
		err = regmap_update_bits(priv->regmap, OV468X_FORMAT1,
					BIT(2) | BIT(1), (!ctrl->val) - 1);
		if (err)
			break;
		err = regmap_write_u16(priv->regmap, OV468X_V_WIN_OFF,
				priv->v_win_off + ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		err = regmap_update_bits(priv->regmap, OV468X_FORMAT2,
					BIT(2) | BIT(1), (!ctrl->val) - 1);
		if (err)
			break;
		err = regmap_write_u16(priv->regmap, OV468X_H_WIN_OFF,
				priv->h_win_off + ctrl->val);
		break;
	case V4L2_CID_EXPOSURE:
		err = regmap_write_u24(priv->regmap, OV468X_AEC_LONG_EXPO,
				ctrl->val);
		break;
	case V4L2_CID_GAIN:
		err = regmap_write_u24(priv->regmap, OV468X_AEC_LONG_GAIN,
				ctrl->val);
		break;
	default:
		return -EINVAL;
	}

	return err;
}

static struct v4l2_subdev_video_ops ov468x_subdev_video_ops = {
	.g_mbus_config		= ov468x_g_mbus_config,
	.enum_framesizes	= ov4689_enum_framesizes,
	.enum_mbus_fmt		= ov468x_enum_mbus_fmt,
	.try_mbus_fmt		= ov468x_try_mbus_fmt,
	.s_mbus_fmt		= ov468x_s_mbus_fmt,
	.g_mbus_fmt		= ov468x_g_mbus_fmt,
	.s_stream		= ov468x_s_stream,
};

static struct v4l2_subdev_core_ops ov468x_subdev_core_ops = {
	.s_power		= ov468x_s_power,
	.g_chip_ident		= ov468x_g_chip_ident,
	.queryctrl		= v4l2_subdev_queryctrl,
	.querymenu		= v4l2_subdev_querymenu,
	.g_ctrl			= v4l2_subdev_g_ctrl,
	.s_ctrl			= v4l2_subdev_s_ctrl,
	.g_ext_ctrls		= v4l2_subdev_g_ext_ctrls,
	.try_ext_ctrls		= v4l2_subdev_try_ext_ctrls,
	.s_ext_ctrls		= v4l2_subdev_s_ext_ctrls,
};

static struct v4l2_subdev_ops ov468x_subdev_ops = {
	.core   = &ov468x_subdev_core_ops,
	.video  = &ov468x_subdev_video_ops,
};

static struct v4l2_ctrl_ops ov468x_ctrl_ops = {
	.s_ctrl = ov468x_s_ctrl,
};

static const struct v4l2_ctrl_config ov468x_ctrl_test_rolling_bar = {
	.ops = &ov468x_ctrl_ops,
	.id = V4L2_CID_OV468X_TEST_ROLLING_BAR,
	.name = "Test pattern with rolling bar",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 0,
};

static const struct v4l2_ctrl_config ov468x_ctrl_test_transparent = {
	.ops = &ov468x_ctrl_ops,
	.id = V4L2_CID_OV468X_TEST_TRANSPARENT,
	.name = "Transparent test pattern",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 0,
};

#ifdef CONFIG_OF
static int ov468x_of_parse(struct i2c_client *client,
			struct ov468x_priv *priv)
{
	struct v4l2_of_endpoint endpoint;
	struct device_node *ep;
	const char *clkname;
	u32 rate;
	int ret;

	ret = of_property_read_string(client->dev.of_node,
				"extclk-name", &clkname);
	if (!ret) {
		priv->extclk = devm_clk_get(&client->dev, clkname);
		if (IS_ERR(priv->extclk)) {
			dev_err(&client->dev,
				"Error getting clock %s: %ld\n",
				clkname, PTR_ERR(priv->extclk));
			return PTR_ERR(priv->extclk);
		}
	}

	if (!of_property_read_u32(client->dev.of_node,
					"extclk-rate", &rate))
		priv->extclk_rate = rate;

	ep = v4l2_of_get_next_endpoint(client->dev.of_node, NULL);
	if (!ep) {
		dev_err(&client->dev, "Couldn't get DT endpoint child node.\n");
		return -EINVAL;
	}

	v4l2_of_parse_endpoint(ep, &endpoint);
	of_node_put(ep);

	if (endpoint.bus_type != V4L2_MBUS_CSI2) {
		dev_err(&client->dev,
			"Only MIPI CSI-2 endpoint is supported.\n");
		return -EINVAL;
	}

	if (endpoint.bus.mipi_csi2.num_data_lanes != 4) {
		dev_err(&client->dev,
			"Only 4 lane MIPI interface is supported.\n");
		return -EINVAL;
	}

	if (endpoint.bus.mipi_csi2.flags & V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK) {
		dev_err(&client->dev, "Only continuous clock is supported.\n");
		return -EINVAL;
	}

	return 0;
}
#else
static int ov468x_of_parse(struct i2c_client *client,
			struct ov468x_priv *priv)
{
	return -EINVAL;
}
#endif

static int ov468x_read_ident(struct ov468x_priv *priv)
{
	const struct reg_default init_ident[] = {
		{ OV468X_ISP_CTRL0, 0xd3 },
		{ OV468X_SC_CTRL0100, 0x01 },
	};
	const struct reg_default read_ident[] = {
		{ OV468X_OTP_MODE_CTRL, 0x00 },
		{ OV468X_OTP_LOAD_CTRL, 0x01 },
	};
	unsigned low, high, rev;
	int ret;

	ret = ov468x_poweron(priv);
	if (ret)
		return ret;

	/* Disable ISP OTP and start streaming */
	ret = regmap_multi_reg_write(priv->regmap, init_ident,
				ARRAY_SIZE(init_ident));
	if (ret)
		goto poweroff;

	/* Wait for the streaming start to finish */
	msleep(10);

	/* Load the OTP data into the SRAM */
	ret = regmap_multi_reg_write(priv->regmap, read_ident,
				ARRAY_SIZE(read_ident));
	if (ret)
		goto poweroff;

	/* Read the OTP data from SRAM */
	ret = regmap_read(priv->regmap, OV468X_OTP_SRAM(1), &high);
	if (ret)
		goto poweroff;

	ret = regmap_read(priv->regmap, OV468X_OTP_SRAM(2), &low);
	if (ret)
		goto poweroff;

	ret = regmap_read(priv->regmap, OV468X_OTP_SRAM(14), &rev);
	if (ret)
		goto poweroff;

	priv->ident.ident = (high << 8) | low;
	priv->ident.revision = rev;

	/* Sometimes the camera ACK but still doesn't deliver any data */
	if (!priv->ident.ident)
		ret = -ENODEV;

poweroff:
	ov468x_poweroff(priv);
	return ret;
}

static int ov468x_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct ov468x_priv *priv;
	int ret;

	priv = devm_kzalloc(&client->dev, sizeof(struct ov468x_priv),
				GFP_KERNEL);
	if (!priv) {
		dev_err(&client->dev, "Failed to allocate private data!\n");
		return -ENOMEM;
	}

	memcpy(priv->regulators, &ov468x_regulators,
		sizeof(priv->regulators));
	ret = devm_regulator_bulk_get(
		&client->dev, ARRAY_SIZE(priv->regulators), priv->regulators);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(&client->dev, "failed to get regulators\n");
		return ret;
	}

	priv->pwdnb = devm_gpiod_get_optional(
		&client->dev, "pwdnb", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->pwdnb)) {
		dev_err(&client->dev,
			"Error requesting pwdnb gpio: %ld\n",
			PTR_ERR(priv->pwdnb));
		return PTR_ERR(priv->pwdnb);
	}

	priv->xshutdown = devm_gpiod_get_optional(
		&client->dev, "xshutdown", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->xshutdown)) {
		dev_err(&client->dev,
			"Error requesting xshutdown gpio: %ld\n",
			PTR_ERR(priv->xshutdown));
		return PTR_ERR(priv->xshutdown);
	}

	if (client->dev.of_node) {
		ret = ov468x_of_parse(client, priv);
		if (ret)
			return ret;
	}

	priv->regmap = devm_regmap_init_i2c(client, &ov468x_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(&client->dev, "regmap_init failed: %ld\n",
				PTR_ERR(priv->regmap));
		return -ENODEV;
	}

	if (priv->extclk) {
		/* Take the current rate if none was given */
		if (priv->extclk_rate == 0)
			priv->extclk_rate = clk_get_rate(priv->extclk);

		/* If not take the default rate */
		if (priv->extclk_rate < OV468X_EXTCLK_MIN_RATE ||
				priv->extclk_rate > OV468X_EXTCLK_MAX_RATE)
			priv->extclk_rate = OV468X_EXTCLK_DEFAULT_RATE;

		/* Set the rate */
		ret = clk_set_rate(priv->extclk, priv->extclk_rate);
		if (ret) {
			dev_err(&client->dev,
				"Error setting clock rate: %d\n", ret);
			return ret;
		}

		/* Read it back to get the acctual rate */
		priv->extclk_rate = clk_get_rate(priv->extclk);
	}

	v4l2_i2c_subdev_init(&priv->subdev, client, &ov468x_subdev_ops);

	ret = ov468x_read_ident(priv);
	if (ret) {
		dev_err(&client->dev, "Failed to read chip ident\n");
		return ret;
	}

	v4l2_ctrl_handler_init(&priv->ctrls, 7);
	priv->subdev.ctrl_handler = &priv->ctrls;

	v4l2_ctrl_new_std_menu_items(&priv->ctrls, &ov468x_ctrl_ops,
				V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(ov468x_test_pattern_names) - 1,
				0, 0, ov468x_test_pattern_names);
	v4l2_ctrl_new_custom(&priv->ctrls,
			&ov468x_ctrl_test_rolling_bar, NULL);
	v4l2_ctrl_new_custom(&priv->ctrls,
			&ov468x_ctrl_test_transparent, NULL);
	v4l2_ctrl_new_std(&priv->ctrls, &ov468x_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->ctrls, &ov468x_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->ctrls, &ov468x_ctrl_ops,
			V4L2_CID_EXPOSURE, 0, 0xFFFFF, 1, 25000);
	v4l2_ctrl_new_std(&priv->ctrls, &ov468x_ctrl_ops,
			V4L2_CID_GAIN, 0, 0x3FFFF, 1, 0x80);

	if (priv->ctrls.error) {
		ret = priv->ctrls.error;
		dev_err(&client->dev, "control initialization error %d\n",
			ret);
		goto free_ctrls;
	}

	/* Set the default format */
	priv->mf.width = -1;
	priv->mf.height = -1;
	ov468x_try_mbus_fmt(&priv->subdev, &priv->mf);

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret) {
		dev_err(&client->dev, "Failed to register async subdev: %d\n",
				ret);
		goto free_ctrls;
	}

	return 0;

free_ctrls:
	v4l2_ctrl_handler_free(&priv->ctrls);
	return ret;
}

static int ov468x_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov468x_priv *priv = to_ov468x(sd);

	v4l2_async_unregister_subdev(&priv->subdev);
	v4l2_ctrl_handler_free(&priv->ctrls);

	return 0;
}

static const struct i2c_device_id ov468x_id[] = {
	{ "ov4682" },
	{ "ov4685" },
	{ "ov4686" },
	{ "ov4688" },
	{ "ov4689" },
	{ },
};
MODULE_DEVICE_TABLE(i2c, ov468x_id);

#ifdef CONFIG_OF
static const struct of_device_id ov468x_of_match[] = {
	{
		.compatible = "ovti,ov4682",
	},
	{
		.compatible = "ovti,ov4685",
	},
	{
		.compatible = "ovti,ov4686",
	},
	{
		.compatible = "ovti,ov4688",
	},
	{
		.compatible = "ovti,ov4689",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, ov468x_of_match);
#endif

static struct i2c_driver ov468x_i2c_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = of_match_ptr(ov468x_of_match),
	},
	.probe    = ov468x_probe,
	.remove   = ov468x_remove,
	.id_table = ov468x_id,
};

module_i2c_driver(ov468x_i2c_driver);

MODULE_DESCRIPTION("Camera sensor driver for the Omnivision 4MP familly");
MODULE_AUTHOR("Alban Bedel <alban.bedel@avionic-design.de>");
MODULE_LICENSE("GPL v2");
