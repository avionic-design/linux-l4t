/*
 * Copyright (c) 2016 Avionic Design GmbH
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

#define IMX290_REG_STANDBY		0x3000
#define IMX290_REG_REGHOLD		0x3001
#define IMX290_REG_XMSTA		0x3002
#define IMX290_REG_VHREV_WINMODE	0x3007
#define IMX290_REG_FRSEL		0x3009
#define IMX290_REG_BLKLEVEL		0x300a
#define IMX290_REG_GAIN			0x3014
#define IMX290_REG_VMAX			0x3018
#define IMX290_REG_HMAX			0x301c
#define IMX290_REG_SHS1			0x3020
#define IMX290_REG_XVSLNG		0x3048
#define IMX290_REG_XVHSOUTSEL		0x304b
#define IMX290_REG_PGMODE		0x308c
#define IMX290_REG_PHYSICAL_LANE_NUM	0x3407
#define IMX290_REG_CSI_DT_FMT		0x3441
#define IMX290_REG_CSI_LANE_MODE	0x3443
#define IMX290_REG_EXTCK_FREQ		0x3444
#define IMX290_REG_TCLKPOST		0x3446

#define IMX290_PGREGEN_SHIFT		0
#define IMX290_PGMODE_SHIFT		4
#define IMX290_VREVERSE_MASK		BIT(0)
#define IMX290_HREVERSE_MASK		BIT(1)
#define IMX290_REGLEN_SHS1		18
#define IMX290_REGLEN_BLKLEVEL		9
#define IMX290_REGLEN_HMAX		16
#define IMX290_REGLEN_VMAX		18
#define IMX290_REGLEN_FRSEL		2
#define IMX290_XVSLNG_SHIFT		4
#define IMX290_XVSOUTSEL_MASK		0x03

#define IMX290_XVSOUTSEL_VSYNC		0x02
#define IMX290_XVSOUTSEL_HIGH		0x00
#define IMX290_BLACKLEVEL_DFT		0xf0
#define IMX290_BLACKLEVEL_MAX		0x1ff
#define IMX290_GAIN_MAX			0xf0
#define IMX290_INCK_RATE		37125000
#define IMX290_PIXEL_PERIOD_PS		6734

static const struct regmap_range imx290_regmap_rw_ranges[] = {
	regmap_reg_range(0x3000, 0x3022),
	regmap_reg_range(0x303a, 0x3043),
	regmap_reg_range(0x3046, 0x304b),
	regmap_reg_range(0x305c, 0x305f),
	regmap_reg_range(0x3070, 0x3071),
	regmap_reg_range(0x308c, 0x308c),
	regmap_reg_range(0x309b, 0x309c),
	regmap_reg_range(0x30a2, 0x30a2),
	regmap_reg_range(0x30a6, 0x30ac),
	regmap_reg_range(0x30b0, 0x30b0),
	regmap_reg_range(0x3119, 0x3119),
	regmap_reg_range(0x311c, 0x311e),
	regmap_reg_range(0x3128, 0x3129),
	regmap_reg_range(0x313d, 0x313d),
	regmap_reg_range(0x3150, 0x3150),
	regmap_reg_range(0x315e, 0x315e),
	regmap_reg_range(0x3164, 0x3164),
	regmap_reg_range(0x317c, 0x317e),
	regmap_reg_range(0x31ec, 0x31ec),
	regmap_reg_range(0x32b8, 0x32bb),
	regmap_reg_range(0x32c8, 0x32cb),
	regmap_reg_range(0x332c, 0x332e),
	regmap_reg_range(0x3358, 0x335a),
	regmap_reg_range(0x3360, 0x3362),
	regmap_reg_range(0x33b0, 0x33b3),
	regmap_reg_range(0x3405, 0x3407),
	regmap_reg_range(0x3414, 0x3414),
	regmap_reg_range(0x3418, 0x3419),
	regmap_reg_range(0x342c, 0x342d),
	regmap_reg_range(0x3430, 0x3431),
	regmap_reg_range(0x3441, 0x3455),
	regmap_reg_range(0x3472, 0x3473),
	regmap_reg_range(0x3480, 0x3480),
};

static const struct regmap_access_table imx290_regmap_access = {
	.yes_ranges = imx290_regmap_rw_ranges,
	.n_yes_ranges = ARRAY_SIZE(imx290_regmap_rw_ranges),
};

static struct regmap_config imx290_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	/* TODO: Maybe add .reg_defaults... */
	.cache_type = REGCACHE_RBTREE,
	.max_register = 0x34ff,
	.rd_table = &imx290_regmap_access,
	.wr_table = &imx290_regmap_access,
};

#define to_imx290(sd)		container_of(sd, struct imx290_priv, subdev)

static const struct regulator_bulk_data imx290_regulators[] = {
	{ "dvdd" },
	{ "ovdd" },
	{ "avdd" },
};

static const char * const imx290_test_pattern_menu[] = {
	"Disabled",
	"Sequence Pattern 1",
	"Horizontal Color-bar Chart",
	"Vertical Color-bar Chart",
	"Sequence Pattern 2",
	"Gradation Pattern 1",
	"Gradation Pattern 2",
	"Toggle Pattern",
};

struct imx290_csi_timing {
	const struct reg_default	*regs_datarate;
	unsigned			regs_datarate_size;
};

struct imx290_mode_rate {
	int				framerate;
	/* Timing for each possible number of lanes */
	const struct imx290_csi_timing	*csi_timing[2];

	u16				hmax;
	u8				frsel;
	unsigned			default_exposure;
};

struct imx290_mode {
	unsigned			width;
	unsigned			height;
	const struct reg_default	*regs;
	unsigned			regs_size;
	const struct imx290_mode_rate	*rates;
	unsigned			rates_size;
	unsigned			vmax;
};

struct imx290_priv {
	struct v4l2_subdev		subdev;
	struct v4l2_mbus_framefmt	mf;

	int				ident;
	u8				revision;
	u8				num_data_lanes;
	u8				xvslng;
	u8				xvsoutsel;

	struct v4l2_ctrl_handler	ctrls;
	struct v4l2_ctrl		*exposure_ctrl;
	struct v4l2_ctrl		*black_level_ctrl;
	struct v4l2_ctrl		*test_pattern_ctrl;

	const struct imx290_mode	*mode;
	const struct imx290_mode_rate	*rate;

	struct regmap			*regmap;
	struct regulator_bulk_data	regulators[ARRAY_SIZE(imx290_regulators)];
	struct clk			*inck;
	unsigned long			inck_rate;
	struct gpio_desc		*xclr;
	struct mutex			lock;
};

#define csi_timing_for_rate(rate, priv) \
	((rate)->csi_timing[(priv)->num_data_lanes / 4])

static const struct reg_default imx290_720_regs[] = {
	/* WINMODE */
	{ 0x3007, 0x01 },
	/* INCKSEL */
	{ 0x305c, 0x20 },
	{ 0x305d, 0x00 },
	{ 0x305e, 0x20 },
	{ 0x305f, 0x01 },
	{ 0x315e, 0x1a },
	{ 0x3164, 0x1a },
	{ 0x3480, 0x49 },
};

static const struct reg_default imx290_1080_regs[] = {
	/* WINMODE */
	{ 0x3007, 0x00 },
	/* INCKSEL */
	{ 0x305c, 0x18 },
	{ 0x305d, 0x03 },
	{ 0x305e, 0x20 },
	{ 0x305f, 0x01 },
	{ 0x315e, 0x1a },
	{ 0x3164, 0x1a },
	{ 0x3480, 0x49 },
};

/* Some settings for frame rate and CSI lanes */
static const struct reg_default imx290_timing_149mbps_regs[] = {
	{ 0x3446, 0x47 },
	{ 0x3447, 0x00 },
	{ 0x3448, 0x17 },
	{ 0x3449, 0x00 },
	{ 0x344a, 0x0f },
	{ 0x344b, 0x00 },
	{ 0x344c, 0x0f },
	{ 0x344d, 0x00 },
	{ 0x344e, 0x0f },
	{ 0x344f, 0x00 },
	{ 0x3450, 0x2b },
	{ 0x3451, 0x00 },
	{ 0x3452, 0x0b },
	{ 0x3453, 0x00 },
	{ 0x3454, 0x0f },
	{ 0x3455, 0x00 },
	{ 0x3405, 0x20 }, /* REPETITION */
};

static const struct imx290_csi_timing imx290_timing_149mbps = {
	.regs_datarate = imx290_timing_149mbps_regs,
	.regs_datarate_size = ARRAY_SIZE(imx290_timing_149mbps_regs),
};

static const struct reg_default imx290_timing_223mbps_regs[] = {
	{ 0x3446, 0x47 },
	{ 0x3447, 0x00 },
	{ 0x3448, 0x1f },
	{ 0x3449, 0x00 },
	{ 0x344a, 0x17 },
	{ 0x344b, 0x00 },
	{ 0x344c, 0x0f },
	{ 0x344d, 0x00 },
	{ 0x344e, 0x17 },
	{ 0x344f, 0x00 },
	{ 0x3450, 0x47 },
	{ 0x3451, 0x00 },
	{ 0x3452, 0x0f },
	{ 0x3453, 0x00 },
	{ 0x3454, 0x0f },
	{ 0x3455, 0x00 },
	{ 0x3405, 0x20 }, /* REPETITION */
};

static const struct imx290_csi_timing imx290_timing_223mbps = {
	.regs_datarate = imx290_timing_223mbps_regs,
	.regs_datarate_size = ARRAY_SIZE(imx290_timing_223mbps_regs),
};

static const struct reg_default imx290_timing_297mbps_regs[] = {
	{ 0x3446, 0x4f },
	{ 0x3447, 0x00 },
	{ 0x3448, 0x2f },
	{ 0x3449, 0x00 },
	{ 0x344a, 0x17 },
	{ 0x344b, 0x00 },
	{ 0x344c, 0x17 },
	{ 0x344d, 0x00 },
	{ 0x344e, 0x17 },
	{ 0x344f, 0x00 },
	{ 0x3450, 0x57 },
	{ 0x3451, 0x00 },
	{ 0x3452, 0x17 },
	{ 0x3453, 0x00 },
	{ 0x3454, 0x17 },
	{ 0x3455, 0x00 },
	{ 0x3405, 0x10 }, /* REPETITION */
};

static const struct imx290_csi_timing imx290_timing_297mbps = {
	.regs_datarate = imx290_timing_297mbps_regs,
	.regs_datarate_size = ARRAY_SIZE(imx290_timing_297mbps_regs),
};

static const struct reg_default imx290_timing_446mbps_regs[] = {
	{ 0x3446, 0x57 },
	{ 0x3447, 0x00 },
	{ 0x3448, 0x37 },
	{ 0x3449, 0x00 },
	{ 0x344a, 0x1f },
	{ 0x344b, 0x00 },
	{ 0x344c, 0x1f },
	{ 0x344d, 0x00 },
	{ 0x344e, 0x1f },
	{ 0x344f, 0x00 },
	{ 0x3450, 0x77 },
	{ 0x3451, 0x00 },
	{ 0x3452, 0x1f },
	{ 0x3453, 0x00 },
	{ 0x3454, 0x17 },
	{ 0x3455, 0x00 },
	{ 0x3405, 0x10 }, /* REPETITION */
};

static const struct imx290_csi_timing imx290_timing_446mbps = {
	.regs_datarate = imx290_timing_446mbps_regs,
	.regs_datarate_size = ARRAY_SIZE(imx290_timing_446mbps_regs),
};

static const struct reg_default imx290_timing_594mbps_regs[] = {
	{ 0x3446, 0x67 },
	{ 0x3447, 0x00 },
	{ 0x3448, 0x57 },
	{ 0x3449, 0x00 },
	{ 0x344a, 0x2f },
	{ 0x344b, 0x00 },
	{ 0x344c, 0x27 },
	{ 0x344d, 0x00 },
	{ 0x344e, 0x2f },
	{ 0x344f, 0x00 },
	{ 0x3450, 0xbf },
	{ 0x3451, 0x00 },
	{ 0x3452, 0x2f },
	{ 0x3453, 0x00 },
	{ 0x3454, 0x27 },
	{ 0x3455, 0x00 },
	{ 0x3405, 0x00 }, /* REPETITION */
};

static const struct imx290_csi_timing imx290_timing_594mbps = {
	.regs_datarate = imx290_timing_594mbps_regs,
	.regs_datarate_size = ARRAY_SIZE(imx290_timing_594mbps_regs),
};

static const struct reg_default imx290_timing_891mbps_regs[] = {
	{ 0x3446, 0x77 },
	{ 0x3447, 0x00 },
	{ 0x3448, 0x67 },
	{ 0x3449, 0x00 },
	{ 0x344a, 0x47 },
	{ 0x344b, 0x00 },
	{ 0x344c, 0x37 },
	{ 0x344d, 0x00 },
	{ 0x344e, 0x3f },
	{ 0x344f, 0x00 },
	{ 0x3450, 0xff },
	{ 0x3451, 0x00 },
	{ 0x3452, 0x3f },
	{ 0x3453, 0x00 },
	{ 0x3454, 0x37 },
	{ 0x3455, 0x00 },
	{ 0x3405, 0x00 }, /* REPETITION */
};

static const struct imx290_csi_timing imx290_timing_891mbps = {
	.regs_datarate = imx290_timing_891mbps_regs,
	.regs_datarate_size = ARRAY_SIZE(imx290_timing_891mbps_regs),
};

static const struct imx290_mode_rate imx290_720_rates[] = {
	{
		.framerate = 25,
		.csi_timing[0] = &imx290_timing_297mbps,
		.csi_timing[1] = &imx290_timing_149mbps,
		.hmax = 0x1ef0,
		.frsel = 0x02,
		.default_exposure = 398,
	},
	{
		.framerate = 30,
		.csi_timing[0] = &imx290_timing_297mbps,
		.csi_timing[1] = &imx290_timing_149mbps,
		.hmax = 0x19c8,
		.frsel = 0x02,
		.default_exposure = 332,
	},
	{
		.framerate = 50,
		.csi_timing[0] = &imx290_timing_594mbps,
		.csi_timing[1] = &imx290_timing_297mbps,
		.hmax = 0x0f78,
		.frsel = 0x01,
		.default_exposure = 199,
	},
	{
		.framerate = 60,
		.csi_timing[0] = &imx290_timing_594mbps,
		.csi_timing[1] = &imx290_timing_297mbps,
		.hmax = 0x0ce4,
		.frsel = 0x01,
		.default_exposure = 166,
	},
	{
		.framerate = 100,
		.csi_timing[1] = &imx290_timing_594mbps,
		.hmax = 0x07bc,
		.frsel = 0x00,
		.default_exposure = 99,
	},
	{
		.framerate = 120,
		.csi_timing[1] = &imx290_timing_594mbps,
		.hmax = 0x0672,
		.frsel = 0x00,
		.default_exposure = 83,
	},
};

static const struct imx290_mode_rate imx290_1080_rates[] = {
	{
		.framerate = 25,
		.csi_timing[0] = &imx290_timing_446mbps,
		.csi_timing[1] = &imx290_timing_223mbps,
		.hmax = 0x14a0,
		.frsel = 0x02,
		.default_exposure = 398,
	},
	{
		.framerate = 30,
		.csi_timing[0] = &imx290_timing_446mbps,
		.csi_timing[1] = &imx290_timing_223mbps,
		.hmax = 0x1130,
		.frsel = 0x02,
		.default_exposure = 332,
	},
	{
		.framerate = 50,
		.csi_timing[0] = &imx290_timing_891mbps,
		.csi_timing[1] = &imx290_timing_446mbps,
		.hmax = 0x0a50,
		.frsel = 0x01,
		.default_exposure = 199,
	},
	{
		.framerate = 60,
		.csi_timing[0] = &imx290_timing_891mbps,
		.csi_timing[1] = &imx290_timing_446mbps,
		.hmax = 0x0898,
		.frsel = 0x01,
		.default_exposure = 166,
	},
	{
		.framerate = 100,
		.csi_timing[1] = &imx290_timing_891mbps,
		.hmax = 0x0528,
		.frsel = 0x00,
		.default_exposure = 99,
	},
	{
		.framerate = 120,
		.csi_timing[1] = &imx290_timing_891mbps,
		.hmax = 0x044c,
		.frsel = 0x00,
		.default_exposure = 83,
	},
};

static const struct imx290_mode imx290_modes[] = {
	{
		.width = 1280,
		.height = 720,
		.regs = imx290_720_regs,
		.regs_size = ARRAY_SIZE(imx290_720_regs),
		.rates = imx290_720_rates,
		.rates_size = ARRAY_SIZE(imx290_720_rates),
		.vmax = 0x2ee,
	},
	{
		.width = 1920,
		.height = 1080,
		.regs = imx290_1080_regs,
		.regs_size = ARRAY_SIZE(imx290_1080_regs),
		.rates = imx290_1080_rates,
		.rates_size = ARRAY_SIZE(imx290_1080_rates),
		.vmax = 0x465,
	},
};

/* Basic configuration to apply on the defaults */
static const struct reg_default imx290_reg_default[] = {
	{ 0x300f, 0x00 },
	{ 0x3010, 0x21 },
	{ 0x3012, 0x64 },
	{ 0x3016, 0x09 },
	{ 0x3070, 0x02 },
	{ 0x3071, 0x11 },
	{ 0x309b, 0x10 },
	{ 0x309c, 0x22 },
	{ 0x30a2, 0x02 },
	{ 0x30a6, 0x20 },
	{ 0x30a8, 0x20 },
	{ 0x30aa, 0x20 },
	{ 0x30ac, 0x20 },
	{ 0x30b0, 0x43 },

	{ 0x3119, 0x9e },
	{ 0x311c, 0x1e },
	{ 0x311e, 0x08 },
	{ 0x3128, 0x05 },
	{ 0x313d, 0x83 },
	{ 0x3150, 0x03 },
	{ 0x317e, 0x00 },

	{ 0x32b8, 0x50 },
	{ 0x32b9, 0x10 },
	{ 0x32ba, 0x00 },
	{ 0x32bb, 0x04 },
	{ 0x32c8, 0x50 },
	{ 0x32c9, 0x10 },
	{ 0x32ca, 0x00 },
	{ 0x32cb, 0x04 },

	{ 0x332c, 0xd3 },
	{ 0x332d, 0x10 },
	{ 0x332e, 0x0d },
	{ 0x3358, 0x06 },
	{ 0x3359, 0xe1 },
	{ 0x335a, 0x11 },
	{ 0x3360, 0x1e },
	{ 0x3361, 0x61 },
	{ 0x3362, 0x10 },
	{ 0x33b0, 0x50 },
	{ 0x33b2, 0x1a },
	{ 0x33b3, 0x04 },

	/* Clock speed selection */
	{ 0x3444, 0x20 },
	{ 0x3445, 0x25 },

	/* CSI format RAW12 */
	{ 0x3441, 0x0c },
	{ 0x3442, 0x0c },

	/* A/D mode 12 bits */
	{ 0x3005, 0x01 },
	{ 0x3129, 0x00 },
	{ 0x317c, 0x00 },
	{ 0x31ec, 0x0e },
};

/* Helper function to write registers that span up to 4 addresses. */
static int imx290_write_regbits(struct regmap *regmap, unsigned reg,
		unsigned regbits, unsigned nbits)
{
	unsigned mask;
	int ret, ret2;

	ret = regmap_write(regmap, IMX290_REG_REGHOLD, 1);
	if (ret)
		return ret;

	while (nbits > 0) {
		mask = 0xff & (BIT(nbits)-1);
		ret = regmap_update_bits(regmap, reg, mask, regbits);
		if (ret)
			break;
		reg++;
		nbits -= min(nbits, 8u);
		regbits >>= 8;
	}

	ret2 = regmap_write(regmap, IMX290_REG_REGHOLD, 0);
	if (!ret2)
		return ret2;

	return ret;
}

static int imx290_set_patterngen(struct imx290_priv *priv, int mode)
{
	int enable = mode > 0;
	int black_level;
	int ret;

	/* Blacklevel must be set to 0 for pg */
	black_level = enable ? 0 : priv->black_level_ctrl->cur.val;
	ret = imx290_write_regbits(priv->regmap, IMX290_REG_BLKLEVEL,
				black_level, IMX290_REGLEN_BLKLEVEL);
	if (ret)
		return ret;

	ret = regmap_write(priv->regmap, 0x300e, !enable);
	if (ret)
		return ret;

	ret = regmap_write(priv->regmap, 0x300f, !enable);
	if (ret)
		return ret;

	ret = regmap_write(priv->regmap, IMX290_REG_PGMODE,
			(mode << IMX290_PGMODE_SHIFT) |
			(enable << IMX290_PGREGEN_SHIFT));

	return ret;
}

static int imx290_set_exposure(struct imx290_priv *priv,
			int exposure_100us)
{
	struct i2c_client *client = v4l2_get_subdevdata(&priv->subdev);
	int hmax, h_period_100ns, f_period_100ns, i_period_100ns;
	int shs1, vmax, ret;

	hmax = priv->rate->hmax;
	vmax = priv->mode->vmax;

	h_period_100ns = (IMX290_PIXEL_PERIOD_PS * hmax) / 100000;
	f_period_100ns = h_period_100ns * priv->mode->vmax;
	i_period_100ns = exposure_100us * 1000;

	if (i_period_100ns > (f_period_100ns - 2 * h_period_100ns)) {
		/* Long Exposure: Exposure time is longer than allowed
		   setting, increase vmax */
		shs1 = 1;
		vmax = i_period_100ns / h_period_100ns + 2;
	} else {
		/* Normal Exposure */
		shs1 = (f_period_100ns - i_period_100ns) / h_period_100ns - 1;
	}

	/* Sanity, shouldn't happen */
	if (shs1 < 1)
		shs1 = 1;
	if (shs1 > priv->mode->vmax - 2)
		shs1 = priv->mode->vmax - 2;

	dev_dbg(&client->dev,
		"set exposure: exp=%d, fps=%d, shs1=%d, vmax=%d\n",
		exposure_100us, priv->rate->framerate,
		shs1, vmax);

	ret = imx290_write_regbits(priv->regmap, IMX290_REG_VMAX,
				vmax, IMX290_REGLEN_VMAX);
	if (ret)
		return ret;

	ret = imx290_write_regbits(priv->regmap, IMX290_REG_SHS1,
				shs1, IMX290_REGLEN_SHS1);

	return ret;
}

static int imx290_reconfigure(struct imx290_priv *priv,
			const struct imx290_mode *mode,
			const struct imx290_mode_rate *rate)
{
	const struct imx290_mode_rate *old_rate = priv->rate;
	const struct imx290_mode *old_mode = priv->mode;
	const struct imx290_csi_timing *csi_timing;
	int ret;

	csi_timing = csi_timing_for_rate(rate, priv);
	if (!csi_timing)
		return -EINVAL;

	ret = regmap_multi_reg_write(priv->regmap, mode->regs,
				mode->regs_size);
	if (ret < 0)
		goto error;

	ret = regmap_multi_reg_write(priv->regmap, csi_timing->regs_datarate,
				csi_timing->regs_datarate_size);
	if (ret < 0)
		goto error;

	ret = imx290_write_regbits(priv->regmap, IMX290_REG_HMAX,
				rate->hmax, IMX290_REGLEN_HMAX);
	if (ret < 0)
		goto error;

	ret = imx290_write_regbits(priv->regmap, IMX290_REG_FRSEL,
				rate->frsel, IMX290_REGLEN_FRSEL);
	if (ret < 0)
		goto error;

	/* Apply the new mode/rate */
	priv->mode = mode;
	priv->rate = rate;

	/* We need to release the lock to allow the ctrl updates to
	 * get the lock again */
	mutex_unlock(&priv->lock);
	if (rate != old_rate)
		v4l2_ctrl_s_ctrl(priv->exposure_ctrl, rate->default_exposure);
	ret = v4l2_ctrl_handler_setup(priv->subdev.ctrl_handler);
	mutex_lock(&priv->lock);
	if (ret < 0)
		goto error;

	return 0;

error:
	/* If we changed mode or rate try to restore the old one */
	if (mode != old_mode || rate != old_rate) {
		priv->mode = old_mode;
		priv->rate = old_rate;
		imx290_reconfigure(priv, priv->mode, priv->rate);
	}
	return ret;
}

static u32 imx290_get_framefmt_code(struct imx290_priv *priv, int bits)
{
	if (bits == 10) {
		switch (priv->ident) {
		case V4L2_IDENT_IMX290LLR:
			return V4L2_MBUS_FMT_Y10_1X10;
		case V4L2_IDENT_IMX290LQR:
		default:
			return V4L2_MBUS_FMT_SRGGB10_1X10;
		}
	} else {
		switch (priv->ident) {
		case V4L2_IDENT_IMX290LLR:
			return V4L2_MBUS_FMT_Y12_1X12;
		case V4L2_IDENT_IMX290LQR:
		default:
			return V4L2_MBUS_FMT_SRGGB12_1X12;
		}
	}
}

static void imx290_set_default_fmt(struct imx290_priv *priv)
{
	struct v4l2_mbus_framefmt *mf = &priv->mf;

	mf->width = imx290_modes[0].width;
	mf->height = imx290_modes[0].height;

	mf->code = imx290_get_framefmt_code(priv, 12);
	mf->field = V4L2_FIELD_NONE;
	mf->colorspace = V4L2_COLORSPACE_SRGB;

	priv->mode = &imx290_modes[0];
	priv->rate = &priv->mode->rates[0];
}

static const struct imx290_mode *imx290_get_framefmt(
	struct imx290_priv *priv,
	struct v4l2_mbus_framefmt *mf)
{
	static const struct imx290_mode *mode;
	int i;

	for (i = 0; i < ARRAY_SIZE(imx290_modes); i++) {
		if ((imx290_modes[i].width >= mf->width) &&
		    (imx290_modes[i].height >= mf->height))
			break;
	}
	/* If not found, use largest as best effort. */
	if (i >= ARRAY_SIZE(imx290_modes))
		i = ARRAY_SIZE(imx290_modes) - 1;
	mode = &imx290_modes[i];

	mf->width = mode->width;
	mf->height = mode->height;

	mf->field = V4L2_FIELD_NONE;
	mf->code = imx290_get_framefmt_code(priv, 12);
	mf->colorspace = V4L2_COLORSPACE_SRGB;

	return mode;
}

static int imx290_try_fmt(struct v4l2_subdev *sd,
			  struct v4l2_mbus_framefmt *mf)
{
	struct imx290_priv *priv = to_imx290(sd);

	imx290_get_framefmt(priv, mf);
	return 0;
}

static int imx290_s_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct imx290_priv *priv = to_imx290(sd);
	struct i2c_client *client = v4l2_get_subdevdata(&priv->subdev);
	const struct imx290_mode_rate *rate;
	const struct imx290_mode *mode;
	unsigned old_framerate;
	int i, ret;

	mutex_lock(&priv->lock);

	mode = imx290_get_framefmt(priv, mf);

	old_framerate = priv->rate->framerate;
	for (i = 0; i < mode->rates_size; i++) {
		if (mode->rates[i].framerate >= old_framerate) {
			rate = &mode->rates[i];
			break;
		}
	}
	if (i >= mode->rates_size)
		rate = &mode->rates[mode->rates_size - 1];

	ret = imx290_reconfigure(priv, mode, rate);
	if (!ret)
		priv->mf = *mf;

	dev_dbg(&client->dev,
		"set fmt: width=%d, height=%d\n",
		mf->width, mf->height);

	mutex_unlock(&priv->lock);

	return 0;
}

static int imx290_g_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct imx290_priv *priv = to_imx290(sd);

	*mf = priv->mf;

	return 0;
}

static int imx290_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			   enum v4l2_mbus_pixelcode *code)
{
	struct imx290_priv *priv = to_imx290(sd);

	if (index > 0)
		return -EINVAL;

	*code = imx290_get_framefmt_code(priv, 12);
	return 0;
}

static int imx290_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *cfg)
{
	struct imx290_priv *priv = to_imx290(sd);

	cfg->type = V4L2_MBUS_CSI2;
	cfg->flags = V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	if (priv->num_data_lanes == 2)
		cfg->flags |= V4L2_MBUS_CSI2_2_LANE;
	else
		cfg->flags |= V4L2_MBUS_CSI2_4_LANE;

	return 0;
}

static int imx290_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx290_priv *priv = to_imx290(sd);
	int ret = 0;

	mutex_lock(&priv->lock);

	ret = regmap_write(priv->regmap, IMX290_REG_STANDBY, !enable);
	if (!ret)
		ret = regmap_write(
			priv->regmap, IMX290_REG_XMSTA, !enable);

	mutex_unlock(&priv->lock);

	return ret;
}

static int imx290_poweron(struct imx290_priv *priv)
{
	struct i2c_client *client = v4l2_get_subdevdata(&priv->subdev);
	int ret;

	gpiod_set_value(priv->xclr, 1);

	ret = regulator_bulk_enable(
		ARRAY_SIZE(priv->regulators), priv->regulators);
	if (ret) {
		dev_err(&client->dev, "failed to enable regulators\n");
		return ret;
	}

	if (priv->inck) {
		ret = clk_prepare_enable(priv->inck);
		if (ret) {
			dev_err(&client->dev,
				"Error enabling clock: %d\n", ret);
			goto disable_regulators;
		}
	}
	usleep_range(1, 5);

	gpiod_set_value(priv->xclr, 0);
	usleep_range(20, 100);

	ret = regmap_multi_reg_write(priv->regmap, imx290_reg_default,
				ARRAY_SIZE(imx290_reg_default));
	if (ret)
		goto set_xclr;

	ret = regmap_write(priv->regmap, IMX290_REG_PHYSICAL_LANE_NUM,
			priv->num_data_lanes - 1);
	if (ret)
		goto set_xclr;
	ret = regmap_write(priv->regmap, IMX290_REG_CSI_LANE_MODE,
			priv->num_data_lanes - 1);
	if (ret)
		goto set_xclr;

	ret = regmap_write(priv->regmap, IMX290_REG_XVSLNG, priv->xvslng);
	if (ret)
		goto set_xclr;

	ret = regmap_update_bits(priv->regmap, IMX290_REG_XVHSOUTSEL,
		IMX290_XVSOUTSEL_MASK, priv->xvsoutsel);
	if (ret)
		goto set_xclr;

	ret = imx290_reconfigure(priv, priv->mode, priv->rate);
	if (ret)
		goto set_xclr;

	return 0;

set_xclr:
	gpiod_set_value(priv->xclr, 1);
disable_regulators:
	regulator_bulk_disable(ARRAY_SIZE(priv->regulators), priv->regulators);
	dev_err(&client->dev, "Error powering on sensor: %d\n", ret);
	return ret;
}

static void imx290_poweroff(struct imx290_priv *priv)
{
	gpiod_set_value(priv->xclr, 1);
	if (priv->inck)
		clk_disable_unprepare(priv->inck);
	regulator_bulk_disable(ARRAY_SIZE(priv->regulators), priv->regulators);
}

static int imx290_s_power(struct v4l2_subdev *sd, int on)
{
	struct imx290_priv *priv = to_imx290(sd);
	int ret = 0;

	mutex_lock(&priv->lock);
	if (on)
		ret = imx290_poweron(priv);
	else
		imx290_poweroff(priv);

	mutex_unlock(&priv->lock);

	return ret;
}

static int imx290_g_chip_ident(struct v4l2_subdev *sd,
			       struct v4l2_dbg_chip_ident *id)
{
	struct imx290_priv *priv = to_imx290(sd);

	id->ident = priv->ident;
	id->revision = 0;	/* No such thing in the registers */

	return 0;
}

static int imx290_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx290_priv *priv = container_of(ctrl->handler,
			struct imx290_priv, ctrls);
	int ret = 0;

	mutex_lock(&priv->lock);
	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		ret = regmap_write(priv->regmap, IMX290_REG_GAIN,
				ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = regmap_update_bits(
			priv->regmap, IMX290_REG_VHREV_WINMODE,
			IMX290_HREVERSE_MASK,
			ctrl->val ? IMX290_HREVERSE_MASK : 0);
		break;
	case V4L2_CID_VFLIP:
		ret = regmap_update_bits(
			priv->regmap, IMX290_REG_VHREV_WINMODE,
			IMX290_VREVERSE_MASK,
			ctrl->val ? IMX290_VREVERSE_MASK : 0);
		break;
	case V4L2_CID_EXPOSURE_ABSOLUTE:
		ret = imx290_set_exposure(priv, ctrl->val);
		break;
	case V4L2_CID_BLACK_LEVEL:
		if (!priv->test_pattern_ctrl->cur.val)
			ret = imx290_write_regbits(
				priv->regmap, IMX290_REG_BLKLEVEL,
				ctrl->val, IMX290_REGLEN_BLKLEVEL);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = imx290_set_patterngen(priv, ctrl->val);
		break;
	}
	mutex_unlock(&priv->lock);

	return ret;
}

static int imx290_enum_framesizes(struct v4l2_subdev *sd,
				struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index >= ARRAY_SIZE(imx290_modes))
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = imx290_modes[fsize->index].width;
	fsize->discrete.height = imx290_modes[fsize->index].height;

	return 0;
}

static int imx290_enum_frameintervals(struct v4l2_subdev *sd,
				struct v4l2_frmivalenum *fival)
{
	struct imx290_priv *priv = to_imx290(sd);
	const struct imx290_mode_rate *rate;
	const struct imx290_mode *mode;
	struct v4l2_mbus_framefmt mf;

	mf.width = fival->width;
	mf.height = fival->height;
	mf.code = fival->pixel_format;
	mode = imx290_get_framefmt(priv, &mf);

	/* Check that we got a matching mode */
	if (mf.width != fival->width || mf.height != fival->height ||
			mf.code != fival->pixel_format)
		return -EINVAL;

	if (fival->index >= mode->rates_size)
		return -EINVAL;

	rate = &mode->rates[fival->index];
	if (!csi_timing_for_rate(rate, priv))
		return -EINVAL;

	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete.numerator = 1;
	fival->discrete.denominator = rate->framerate;

	return 0;
}

static int imx290_g_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_frame_interval *sdi)
{
	struct imx290_priv *priv = to_imx290(sd);

	if (sdi->pad)
		return -EINVAL;

	sdi->interval.numerator = 1;
	sdi->interval.denominator = priv->rate->framerate;

	return 0;
}

static int imx290_s_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_frame_interval *sdi)
{
	struct imx290_priv *priv = to_imx290(sd);
	const struct imx290_mode *mode = priv->mode;
	const struct imx290_mode_rate *rate = NULL;
	int ret, i;

	if (sdi->pad)
		return -EINVAL;

	if ((sdi->interval.numerator != 1))
		return -EINVAL;

	mutex_lock(&priv->lock);

	for (i = 0; i < mode->rates_size; i++) {
		if (!csi_timing_for_rate(&mode->rates[i], priv))
			continue;
		rate = &mode->rates[i];
		if (rate->framerate >= sdi->interval.denominator)
			break;
	}
	if (!rate) {
		mutex_unlock(&priv->lock);
		return -EINVAL;
	}
	sdi->interval.denominator = rate->framerate;

	ret = imx290_reconfigure(priv, priv->mode, rate);

	mutex_unlock(&priv->lock);

	return ret;
}

static struct v4l2_subdev_video_ops imx290_video_ops = {
	.s_stream		= imx290_s_stream,
	.s_mbus_fmt		= imx290_s_fmt,
	.g_mbus_fmt		= imx290_g_fmt,
	.try_mbus_fmt		= imx290_try_fmt,
	.enum_mbus_fmt		= imx290_enum_fmt,
	.g_mbus_config		= imx290_g_mbus_config,
	.enum_framesizes	= imx290_enum_framesizes,
	.enum_frameintervals	= imx290_enum_frameintervals,
	.g_frame_interval	= imx290_g_frame_interval,
	.s_frame_interval	= imx290_s_frame_interval,
};

static struct v4l2_subdev_core_ops imx290_core_ops = {
	.g_chip_ident		= imx290_g_chip_ident,
	.s_power		= imx290_s_power,
	.queryctrl		= v4l2_subdev_queryctrl,
	.querymenu		= v4l2_subdev_querymenu,
	.g_ctrl			= v4l2_subdev_g_ctrl,
	.s_ctrl			= v4l2_subdev_s_ctrl,
	.g_ext_ctrls		= v4l2_subdev_g_ext_ctrls,
	.try_ext_ctrls		= v4l2_subdev_try_ext_ctrls,
	.s_ext_ctrls		= v4l2_subdev_s_ext_ctrls,
};

static struct v4l2_subdev_ops imx290_subdev_ops = {
	.core			= &imx290_core_ops,
	.video			= &imx290_video_ops,
};

static struct v4l2_ctrl_ops imx290_ctrl_ops = {
	.s_ctrl = imx290_s_ctrl,
};

#ifdef CONFIG_OF
static const struct of_device_id imx290_of_match[];

static int imx290_of_parse(struct i2c_client *client,
	struct imx290_priv *priv)
{
	const struct of_device_id *of_id;
	struct v4l2_of_endpoint endpoint;
	struct device_node *ep;
	const char *clkname;
	u32 xvs_output_len;
	int ret;

	of_id = of_match_node(imx290_of_match, client->dev.of_node);
	if (!of_id) {
		dev_err(&client->dev, "Failed to match DT node\n");
		return -ENODEV;
	}
	priv->ident = (int)of_id->data;

	/* inck_name */
	priv->inck = NULL;
	ret = of_property_read_string(client->dev.of_node,
		"inck-name", &clkname);
	if (ret) {
		dev_dbg(&client->dev,
			"Missing inck-name in DT. Assuming external clock.\n");
		ret = 0;
	} else {
		priv->inck = devm_clk_get(&client->dev, clkname);
		if (IS_ERR(priv->inck)) {
			dev_err(&client->dev,
				"Error getting clock %s: %ld\n",
				clkname, PTR_ERR(priv->inck));
			return PTR_ERR(priv->inck);
		}
	}

	/* xvs_output_len */
	/* valid values: 0, 1, 2, 4, 8 */
	/* 0 means off */
	ret = of_property_read_u32_array(client->dev.of_node,
		"xvs-output-len", &xvs_output_len, 1);
	if (!ret) {
		priv->xvsoutsel = (xvs_output_len > 0) ?
			IMX290_XVSOUTSEL_VSYNC : IMX290_XVSOUTSEL_HIGH;

		switch (xvs_output_len) {
		case 0:
		case 1:
			priv->xvslng = (0 << IMX290_XVSLNG_SHIFT);
			break;
		case 2:
			priv->xvslng = (1 << IMX290_XVSLNG_SHIFT);
			break;
		case 4:
			priv->xvslng = (2 << IMX290_XVSLNG_SHIFT);
			break;
		case 8:
			priv->xvslng = (3 << IMX290_XVSLNG_SHIFT);
			break;
		default:
			dev_err(&client->dev,
				"Invalid value for xvs-output-len. Ignoring.\n");
			priv->xvsoutsel = IMX290_XVSOUTSEL_HIGH;
			break;
		}
	}

	/* Assume a single port and endpoint child for now. */
	ep = v4l2_of_get_next_endpoint(client->dev.of_node, NULL);
	if (!ep) {
		dev_err(&client->dev, "Couldn't get DT endpoint child node.\n");
		return -EINVAL;
	}

	v4l2_of_parse_endpoint(ep, &endpoint);
	of_node_put(ep);

	if (endpoint.bus_type != V4L2_MBUS_CSI2) {
		dev_err(&client->dev,
			"Only MIPI CSI-2 endpoint supported.\n");
		return -EINVAL;
	}

	if ((endpoint.bus.mipi_csi2.num_data_lanes != 4) &&
		(endpoint.bus.mipi_csi2.num_data_lanes != 2)) {
		dev_err(&client->dev,
			"Only 2 or 4 lane MIPI interface supported.\n");
		return -EINVAL;
	}
	priv->num_data_lanes = endpoint.bus.mipi_csi2.num_data_lanes;

	return 0;
}
#else
static int imx290_of_parse(struct i2c_client *client,
			struct imx290_priv *priv)
{
	return -EINVAL;
}
#endif

static int imx290_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct imx290_priv *priv;
	int ret;

	priv = devm_kzalloc(&client->dev, sizeof(struct imx290_priv),
				GFP_KERNEL);
	if (!priv) {
		dev_err(&client->dev, "Failed to allocate private data!\n");
		return -ENOMEM;
	}

	memcpy(priv->regulators, &imx290_regulators,
		sizeof(priv->regulators));
	ret = devm_regulator_bulk_get(
		&client->dev, ARRAY_SIZE(priv->regulators), priv->regulators);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(&client->dev, "failed to get regulators\n");
		return ret;
	}

	priv->xclr = devm_gpiod_get(&client->dev, "xclr", GPIOD_OUT_LOW);
	if (IS_ERR(priv->xclr)) {
		dev_err(&client->dev, "Error requesting xclr gpio: %ld\n",
				PTR_ERR(priv->xclr));
		return PTR_ERR(priv->xclr);
	}

	if (client->dev.of_node) {
		ret = imx290_of_parse(client, priv);
		if (ret)
			return ret;
	} else {
		dev_err(&client->dev, "Only DT configuration supported.\n");
		return -EINVAL;
	}

	priv->regmap = devm_regmap_init_i2c(client, &imx290_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(&client->dev, "regmap_init failed: %ld\n",
				PTR_ERR(priv->regmap));
		return -ENODEV;
	}

	mutex_init(&priv->lock);

	priv->inck_rate = IMX290_INCK_RATE;
	if (priv->inck) {
		ret = clk_set_rate(priv->inck, priv->inck_rate);
		if (ret) {
			dev_err(&client->dev,
				"Error setting clock rate: %d\n", ret);
			goto destroy_mutex;
		}
	}

	imx290_set_default_fmt(priv);

	v4l2_i2c_subdev_init(&priv->subdev, client, &imx290_subdev_ops);

	v4l2_ctrl_handler_init(&priv->ctrls, 6);
	priv->subdev.ctrl_handler = &priv->ctrls;

	/* Gain control. Gain dB * 10/3 = GAIN reg value */
	priv->exposure_ctrl = v4l2_ctrl_new_std(
		&priv->ctrls, &imx290_ctrl_ops,
		V4L2_CID_EXPOSURE_ABSOLUTE, 1, 10000, 1,
		priv->rate->default_exposure);
	v4l2_ctrl_new_std(&priv->ctrls, &imx290_ctrl_ops,
			V4L2_CID_GAIN, 0x00, IMX290_GAIN_MAX, 1, 0);
	v4l2_ctrl_new_std(&priv->ctrls, &imx290_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->ctrls, &imx290_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	priv->black_level_ctrl = v4l2_ctrl_new_std(
		&priv->ctrls, &imx290_ctrl_ops, V4L2_CID_BLACK_LEVEL,
		0, IMX290_BLACKLEVEL_MAX, 1, IMX290_BLACKLEVEL_DFT);
	priv->test_pattern_ctrl = v4l2_ctrl_new_std_menu_items(
		&priv->ctrls, &imx290_ctrl_ops, V4L2_CID_TEST_PATTERN,
		ARRAY_SIZE(imx290_test_pattern_menu) - 1, 0, 0,
		imx290_test_pattern_menu);

	if (priv->ctrls.error) {
		ret = priv->ctrls.error;
		dev_err(&client->dev, "control initialization error %d\n",
				ret);
		goto free_ctrls;
	}

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret) {
		dev_err(&client->dev, "Failed to register async subdev: %d\n",
				ret);
		goto free_ctrls;
	}

	dev_dbg(&client->dev, "added imx290, identity=%d\n", priv->ident);

	return 0;

free_ctrls:
	v4l2_ctrl_handler_free(&priv->ctrls);
destroy_mutex:
	mutex_destroy(&priv->lock);

	return ret;
}

static int imx290_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx290_priv *priv = to_imx290(sd);

	v4l2_async_unregister_subdev(&priv->subdev);
	v4l2_ctrl_handler_free(&priv->ctrls);
	mutex_destroy(&priv->lock);

	return 0;
}

static const struct i2c_device_id imx290_id[] = {
	{ "imx290lqr", V4L2_IDENT_IMX290LQR },
	{ "imx290llr", V4L2_IDENT_IMX290LLR },
	{ },
};
MODULE_DEVICE_TABLE(i2c, imx290_id);

#ifdef CONFIG_OF
static const struct of_device_id imx290_of_match[] = {
	{
		.compatible = "sony,imx290lqr",
		.data = (void *)V4L2_IDENT_IMX290LQR
	},
	{
		.compatible = "sony,imx290llr",
		.data = (void *)V4L2_IDENT_IMX290LLR
	},
	{ },
};
MODULE_DEVICE_TABLE(of, imx290_of_match);
#endif

static struct i2c_driver imx290_i2c_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = of_match_ptr(imx290_of_match),
	},
	.probe    = imx290_probe,
	.remove   = imx290_remove,
	.id_table = imx290_id,
};

module_i2c_driver(imx290_i2c_driver);

MODULE_DESCRIPTION("Camera sensor driver for the Sony IMX290LQR-C and IMX290LLR-C");
MODULE_AUTHOR("Nikolaus Schulz <nikolaus.schulz@avionic-design.de>");
MODULE_AUTHOR("Alban Bedel <alban.bedel@avionic-design.de>");
MODULE_AUTHOR("Marc Andre <marc.andre@netline.ch>");
MODULE_LICENSE("GPL v2");
