/*
 * Copyright 2017-2018 Alban Bedel <alban.bedel@avionic-design.de>
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
#include <media/v4l2-async.h>
#include <media/v4l2-of.h>
#include <media/soc_camera.h>

#define CHIPID		0x0000
#define SYSCTL		0x0002
#define CONFCTL		0x0004
#define FIFOCTL		0x0006
#define DATAFMT		0x0008
#define PLLCTL0		0x0016
#define PLLCTL1		0x0018
#define CLKCTL		0x0020
#define WORDCNT		0x0022
#define PP_MISC		0x0032
#define STARTCNTRL	0x0204
#define PPISTATUS	0x0208
#define LINEINITCNT	0x0210
#define LPTXTIMECNT	0x0214
#define TCLK_HEADERCNT	0x0218
#define TCLK_TRAILCNT	0x021C
#define THS_HEADERCNT	0x0220
#define TWAKEUP		0x0224
#define TCLK_POSTCNT	0x0228
#define THS_TRAILCNT	0x022C
#define HSTXVREGCNT	0x0230
#define HSTXVREGEN	0x0234
#define TXOPTIONCNTRL	0x0238
#define CSI_CONFW	0x0500
#define CSI_START	0x0518

#define DBG_LCNT	0x00E0
#define DBG_WIDTH	0x00E2
#define DBG_VBLANK	0x00E4
#define DBG_DATA	0x00E8

/* Values used in the CSI_CONFW register */
#define CSI_SET_REGISTER	(5 << 29)
#define CSI_CLR_REGISTER	(6 << 29)
#define CSI_CONTROL_REG		(3 << 24)

#define TC358748_MAX_INPUT_MBUS_FMT 16

static const struct regulator_bulk_data tc358748_regulators[] = {
	{ "vddc" },
	{ "vdd_mipi" },
	{ "vddio" },
};

struct tc358748 {
	struct v4l2_subdev subdev;
	struct v4l2_subdev *input;

	struct regmap *ctl_regmap;
	struct regmap *tx_regmap;

	struct gpio_desc *reset_gpio;
	struct mutex lock;

	struct regulator_bulk_data regulators[ARRAY_SIZE(tc358748_regulators)];

	struct v4l2_mbus_framefmt framefmt;
	unsigned int refrate;

	struct v4l2_of_endpoint input_ep;
	struct v4l2_of_endpoint output_ep;

	struct v4l2_async_subdev input_asd;
	struct v4l2_async_subdev *async_subdevs[1];
	struct v4l2_async_notifier sd_notifier;
};

static const struct regmap_range ctl_regmap_rw_ranges[] = {
	regmap_reg_range(0x0000, 0x00ff),
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
	.max_register = 0x00ff,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.rd_table = &ctl_regmap_access,
	.wr_table = &ctl_regmap_access,
	.name = "tc358748-ctl",
};

static const struct regmap_range tx_regmap_rw_ranges[] = {
	regmap_reg_range(0x0100, 0x05ff),
};

static const struct regmap_access_table tx_regmap_access = {
	.yes_ranges = tx_regmap_rw_ranges,
	.n_yes_ranges = ARRAY_SIZE(tx_regmap_rw_ranges),
};

static const struct regmap_config tx_regmap_config = {
	.reg_bits = 16,
	.reg_stride = 4,
	.val_bits = 32,
	.cache_type = REGCACHE_NONE,
	.max_register = 0x05ff,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG_LITTLE,
	.rd_table = &tx_regmap_access,
	.wr_table = &tx_regmap_access,
	.name = "tc358748-tx",
};

static int tc358748_set_pll(struct tc358748 *priv, unsigned long *rate)
{
	unsigned long fbd = 0, prd = 0, frs = 0;
	unsigned long hclk_min, hclk_max;
	unsigned long pll_clk = *rate;
	unsigned long best_diff = -1;
	unsigned long sclk_div;
	unsigned long clk_div;
	unsigned long hclk;
	unsigned long d, m;
	int err;

	/* If no rate was give use a default */
	pll_clk = rate ? *rate : 243000000;

	if (priv->refrate < 6000000 || priv->refrate > 40000000)
		return -ERANGE;

	/* The PLL can go up to 1G, however the sys clock must stay
	 * under 100MHz and the maximum divider is 8, so 800MHz is
	 * the practical maximum PLL rate.
	 */
	if (pll_clk < 62500000 || pll_clk > 800000000)
		return -ERANGE;

	if (pll_clk >= 500000000)
		frs = 0;
	else if (pll_clk >= 250000000)
		frs = 1;
	else if (pll_clk >= 125000000)
		frs = 2;
	else
		frs = 3;

	hclk_max = 1000000000 >> frs;
	hclk_min = hclk_max - (hclk_max >> 1);

	/* Brute force the best PLL setting */
	for (d = 1; best_diff != 0 && d <= 16; d++) {
		unsigned long prediv_clk = priv->refrate / d;
		unsigned long unit_clk = prediv_clk >> frs;

		if (prediv_clk < 4000000 || prediv_clk > 40000000)
			continue;

		m = hclk_min / unit_clk;
		if (m < 1)
			m = 1;

		for ( ; best_diff != 0 && m <= 511; m++) {
			unsigned long diff, clk;

			if (unit_clk > hclk_max / m)
				break;

			clk = unit_clk * m;

			/* The effective clock shouldn't be slower! */
			if (clk < pll_clk)
				continue;

			diff = clk - pll_clk;

			if (diff < best_diff) {
				hclk = unit_clk * m;
				best_diff = diff;
				fbd = m;
				prd = d;
			}
		}
	}

	if (best_diff == (unsigned int)-1) {
		dev_err(priv->subdev.dev, "Failed to find proper PLL settings "
			"for CSI clock @ %lu Hz\n", pll_clk);
		return -EINVAL;
	}

	dev_dbg(priv->subdev.dev,
		 "PLL: (%u / %lu * %lu) >> %lu = %lu (wanted = %lu)\n",
		 priv->refrate, prd, fbd, frs, hclk, pll_clk);

	sclk_div = clk_div = frs > 2 ? 2 : frs;
	/* SCLK is limited to 100MHz instead of 125MHZ */
	if ((hclk >> (3 - clk_div)) > 100000000)
		sclk_div -= 1;
	/* Warn if we hit the PPI clock lower limit */
	if ((hclk >> (3 - clk_div)) < 66000000)
		dev_warn(priv->subdev.dev, "PPI clock will be too slow!\n");


	/* Setup the PLL divider */
	err = regmap_write(priv->ctl_regmap, PLLCTL0,
			   ((prd - 1) << 12) | (fbd - 1));


	/* Start the PLL */
	if (!err)
		err = regmap_write(priv->ctl_regmap, PLLCTL1,
				BIT(0) | /* PLL Enable */
				BIT(1) | /* PLL not reset */
				2 << 8 | /* loop bandwidth 50% */
				frs << 10);

	/* Wait for the PLL to lock */
	if (!err)
		usleep_range(10, 20);

	/* Setup the clocks dividers, all clocks have the same range
	 * requirements, so we use the same divider for all of them.
	 */
	if (!err)
		err = regmap_write(priv->ctl_regmap, CLKCTL,
				   (clk_div << 4) | (clk_div << 2) | sclk_div);

	/* Turn on the clocks */
	if (!err)
		err = regmap_update_bits(
			priv->ctl_regmap, PLLCTL1, BIT(4), BIT(4));

	/* Return the effective rate */
	if (!err && rate)
		*rate = hclk;

	return 0;
}

static int tc358748_g_mbus_config(
	struct v4l2_subdev *sd, struct v4l2_mbus_config *mbus)
{
	struct tc358748 *priv = container_of(sd, struct tc358748, subdev);
	struct v4l2_of_bus_mipi_csi2 *csi2;

	mbus->type = priv->output_ep.bus_type;

	switch (mbus->type) {
	case V4L2_MBUS_CSI2:
		csi2 = &priv->output_ep.bus.mipi_csi2;

		mbus->flags = V4L2_MBUS_CSI2_CONTINUOUS_CLOCK |
			V4L2_MBUS_CSI2_CHANNEL_0;

		if (csi2->num_data_lanes >= 4)
			mbus->flags |= V4L2_MBUS_CSI2_4_LANE;
		if (csi2->num_data_lanes >= 3)
			mbus->flags |= V4L2_MBUS_CSI2_3_LANE;
		if (csi2->num_data_lanes >= 2)
			mbus->flags |= V4L2_MBUS_CSI2_2_LANE;
		if (csi2->num_data_lanes >= 1)
			mbus->flags |= V4L2_MBUS_CSI2_1_LANE;

		return 0;

	default:
		return -EINVAL;
	}
}

static int v4l2_subdev_get_all_mbus_fmt(
	struct v4l2_subdev *sd, enum v4l2_mbus_pixelcode *codes,
	unsigned int max_codes)
{
	unsigned int i;
	int err;

	for (i = 0, err = 0; err == 0 && i < max_codes; i++, codes++)
		err = v4l2_subdev_call(
			sd, video, enum_mbus_fmt, i, codes);

	return i;
}

static int tc358748_input_mbus_fmt_supported(
	struct tc358748 *priv, enum v4l2_mbus_pixelcode code)
{
	switch (code) {
	/* RGB formats */
	case V4L2_MBUS_FMT_RGB888_1X24:
	case V4L2_MBUS_FMT_RGB666_1X18:
	case V4L2_MBUS_FMT_RGB565_1X16:
		return 1;

	/* RAW formats */
	case V4L2_MBUS_FMT_SBGGR8_1X8:
	case V4L2_MBUS_FMT_SGBRG8_1X8:
	case V4L2_MBUS_FMT_SGRBG8_1X8:
	case V4L2_MBUS_FMT_SRGGB8_1X8:
	case V4L2_MBUS_FMT_SBGGR10_1X10:
	case V4L2_MBUS_FMT_SGBRG10_1X10:
	case V4L2_MBUS_FMT_SGRBG10_1X10:
	case V4L2_MBUS_FMT_SRGGB10_1X10:
	case V4L2_MBUS_FMT_SBGGR12_1X12:
	case V4L2_MBUS_FMT_SGBRG12_1X12:
	case V4L2_MBUS_FMT_SGRBG12_1X12:
	case V4L2_MBUS_FMT_SRGGB12_1X12:
	/* Also RAW14 */
		return 1;

	/* YUV formats */
	case V4L2_MBUS_FMT_UYVY8_2X8:
	case V4L2_MBUS_FMT_UYVY8_1X16:
	case V4L2_MBUS_FMT_UYVY10_2X10:
		return 1;

	default:
		return 0;
	}
}

static int tc358748_get_next_valid_input_code(
	struct tc358748 *priv, const enum v4l2_mbus_pixelcode *codes,
	int num_codes, int pos)
{
	for (pos++; pos < num_codes; pos++) {
		if (tc358748_input_mbus_fmt_supported(priv, codes[pos]))
			return pos;
	}

	return -EINVAL;
}

static int tc358748_enum_mbus_fmt(struct v4l2_subdev *sd, unsigned int index,
				enum v4l2_mbus_pixelcode *code)
{
	struct tc358748 *priv = container_of(sd, struct tc358748, subdev);
	enum v4l2_mbus_pixelcode codes[TC358748_MAX_INPUT_MBUS_FMT];
	int pos = -1, i, num_codes;

	/* Get all the formats supported by the input */
	num_codes = v4l2_subdev_get_all_mbus_fmt(
		priv->input, codes, ARRAY_SIZE(codes));

	/* Get the n-th valid code from what the input support */
	for (i = 0; i <= index; i++) {
		pos = tc358748_get_next_valid_input_code(
			priv, codes, num_codes, pos);
		if (pos < 0)
			return -EINVAL;
	}

	*code = codes[pos];
	return 0;
}

static unsigned int clk_count(u64 rate, unsigned int ns)
{
	rate *= ns;
	if (do_div(rate, 1000000000))
		rate++; /* Round up the count */
	return rate;
}

static unsigned int clk_ns(unsigned long rate, u64 count)
{
	count *= 1000000000u;
	if (do_div(count, rate))
		count++; /* Round up the time */
	return count;
}

static int tc358748_setup(
	struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *fmt, int set)
{
	struct tc358748 *priv = container_of(sd, struct tc358748, subdev);
	struct v4l2_dv_timings timings = {};
	struct v4l2_bt_timings *bt = &timings.bt;
	struct v4l2_of_bus_mipi_csi2 *csi_bus;
	unsigned long csi_lane_rate, csi_rate;
	unsigned int pclk_per_pixel = 1, bpp;
	struct v4l2_of_bus_parallel *pl_bus;
	unsigned int pdformat, confctl = 0;
	unsigned int tclk_prepare;
	unsigned int ths_prepare;
	unsigned long hsbyte_clk;
	unsigned int tclk_trail;
	unsigned int tclk_zero;
	unsigned int tclk_post;
	unsigned int ths_trail;
	unsigned int lptxtime;
	unsigned int t_wakeup;
	unsigned int ths_zero;
	unsigned int linecnt;
	int err;

	/* Make sure we can handle this input format */
	if (fmt->field != V4L2_FIELD_NONE)
		return -EMEDIUMTYPE;

	switch (fmt->code) {
	case V4L2_MBUS_FMT_SBGGR8_1X8:
	case V4L2_MBUS_FMT_SGBRG8_1X8:
	case V4L2_MBUS_FMT_SGRBG8_1X8:
	case V4L2_MBUS_FMT_SRGGB8_1X8:
		pdformat = 0;
		bpp = 8;
		break;

	case V4L2_MBUS_FMT_SBGGR10_1X10:
	case V4L2_MBUS_FMT_SGBRG10_1X10:
	case V4L2_MBUS_FMT_SGRBG10_1X10:
	case V4L2_MBUS_FMT_SRGGB10_1X10:
		pdformat = 1;
		bpp = 10;
		break;

	case V4L2_MBUS_FMT_SBGGR12_1X12:
	case V4L2_MBUS_FMT_SGBRG12_1X12:
	case V4L2_MBUS_FMT_SGRBG12_1X12:
	case V4L2_MBUS_FMT_SRGGB12_1X12:
		pdformat = 2;
		bpp = 12;
		break;

	case V4L2_MBUS_FMT_RGB888_1X24:
		pdformat = 3;
		bpp = 24;
		break;

	case V4L2_MBUS_FMT_RGB666_1X18:
		pdformat = 4;
		bpp = 18;
		confctl |= 1 << 8;
		break;

	case V4L2_MBUS_FMT_RGB565_1X16:
		pdformat = 5;
		bpp = 16;
		confctl |= 2 << 8;
		break;

	case V4L2_MBUS_FMT_UYVY8_2X8:
		pdformat = 6;
		bpp = 16;
		pclk_per_pixel = 2;
		break;

	case V4L2_MBUS_FMT_UYVY8_1X16:
		pdformat = 6;
		bpp = 16;
		confctl |= 1 << 8;
		break;

	case V4L2_MBUS_FMT_UYVY10_2X10:
		pdformat = 9;
		bpp = 20;
		pclk_per_pixel = 2;
		break;

	default:
		dev_err(sd->dev, "Input data format is not supported\n");
		return -EMEDIUMTYPE;
	}

	/* Then check the timings */
	err = v4l2_subdev_call(priv->input, video, query_dv_timings, &timings);
	if (err) {
		dev_err(sd->dev, "Failed to get input timings: %d\n", err);
		return err;
	}

	/* Check that we got BT.656 / 1120 timing */
	if (timings.type != V4L2_DV_BT_656_1120) {
		dev_err(sd->dev, "Input timings are not BT.656-1120\n");
		return -EMEDIUMTYPE;
	}

	/* Validate the timings */
	if (bt->interlaced) {
		dev_err(sd->dev, "Input signal is interlaced!\n");
		return -EMEDIUMTYPE;
	}

	/* Check the PCLK rate */
	if (bt->pixelclock * pclk_per_pixel > 166000000) {
		dev_err(sd->dev, "Input pixel clock is too fast\n");
		return -ERANGE;
	}

	/* Compute the bit rate needed on the CSI link for this pixel clock */
	csi_bus = &priv->output_ep.bus.mipi_csi2;
	csi_rate = bpp * (unsigned long)bt->pixelclock;
	csi_lane_rate = csi_rate / csi_bus->num_data_lanes;

	dev_dbg(sd->dev, "CSI Rate: %u * %lu * %u = %lu\n",
		 bpp, (unsigned long)bt->pixelclock, pclk_per_pixel, csi_rate);

	/* Check the CSI rate */
	if (csi_lane_rate < 62500000 || csi_lane_rate > 1000000000) {
		dev_err(sd->dev, "The required CSI rate is out of range\n");
		return -ERANGE;
	}

	/* TODO: We need to make sure all the constraints are respected,
	 * however the spreadsheet seems to have various bug :(
	 *
	 * The algorithm should be:
	 *
	 * 1. Pick a CSI rate
	 * 2. Compute LP duration (depend only on CSI rate)
	 * 3. Compute CSI active time w/o fifo (depend only on CSI rate)
	 * 4. Compute FIFO time min = input_h_active - CSI_active_time_wo_fifo
	 * 5. Compute FIFO time max = input_h_total - CSI_active_time_wo_fifo - lp_duration
	 * 6. If fifo time is out of range pick another
	 */

	/* If we don't really apply the settings we are done */
	if (!set)
		return 0;

	/* Get the parallel bus settings */
	pl_bus = &priv->input_ep.bus.parallel;
	if (pl_bus->flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH)
		confctl |= BIT(4);
	if (pl_bus->flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH)
		confctl |= BIT(5);
	if (pl_bus->flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)
		confctl |= BIT(6);

	/* Set the number of CSI lanes */
	confctl |= csi_bus->num_data_lanes - 1;

	/* Reset the whole chip */
	err = regmap_write(priv->ctl_regmap, SYSCTL, 1);
	usleep_range(10, 100);
	if (!err)
		err = regmap_write(priv->ctl_regmap, SYSCTL, 0);
	if (err) {
		dev_err(sd->dev, "Failed to reset chip, can't update CSI config\n");
		return err;
	}

	/* Setup the PLL, we might not get exactly what we requested */
	err = tc358748_set_pll(priv, &csi_lane_rate);
	if (err) {
		dev_err(sd->dev, "Failed to setup PLL at %lu\n", csi_lane_rate);
		return err;
	}

	/* Compute the D-PHY settings */
	hsbyte_clk = csi_lane_rate / 8;

	/* LINEINITCOUNT >= 100us */
	linecnt = clk_count(hsbyte_clk / 2, 100000);

	/* LPTX clk must be less than 20MHz -> LPTXTIMECNT >= 50 ns */
	lptxtime = clk_count(hsbyte_clk, 50);

	/* TWAKEUP >= 1ms (in LPTX clock count) */
	t_wakeup = clk_count(hsbyte_clk / lptxtime, 1000000);

	/* 38ns <= TCLK_PREPARE <= 95ns */
	tclk_prepare = clk_count(hsbyte_clk, 38);
	if (tclk_prepare > clk_count(hsbyte_clk, 95))
		dev_warn(sd->dev, "TCLK_PREPARE is too long (%u ns)\n",
			clk_ns(hsbyte_clk, tclk_prepare));
	// TODO: Check that TCLK_PREPARE <= 95ns

	/* TCLK_ZERO + TCLK_PREPARE >= 300ns */
	tclk_zero = clk_count(hsbyte_clk, 300) - tclk_prepare;

	/* TCLK_TRAIL >= 60ns */
	tclk_trail = clk_count(hsbyte_clk, 60);

	/* TCLK_POST >= 60ns + 52*UI */
	tclk_post = clk_count(hsbyte_clk, 60 + clk_ns(csi_lane_rate, 52));

	/* 40ns + 4*UI <= THS_PREPARE <= 85ns + 6*UI */
	ths_prepare = clk_count(hsbyte_clk, 40 + clk_ns(csi_lane_rate, 4));
	if (ths_prepare > 85 + clk_ns(csi_lane_rate, 6))
		dev_warn(sd->dev, "THS_PREPARE is too long (%u ns)\n",
			clk_ns(hsbyte_clk, ths_prepare));

	/* THS_ZERO + THS_PREPARE >= 145ns + 10*UI */
	ths_zero = clk_count(hsbyte_clk, 145 +
			clk_ns(csi_lane_rate, 10)) - ths_prepare;

	/* 105ns + 12*UI > THS_TRAIL >= max(8*UI, 60ns + 4*UI) */
	ths_trail = clk_count(hsbyte_clk,
			max(clk_ns(csi_lane_rate, 8),
				60 + clk_ns(csi_lane_rate, 4)));

	/* Setup the data format */
	if (!err)
		err = regmap_update_bits(
			priv->ctl_regmap, CONFCTL,
			BIT(0) | BIT(1) | /* CSI lanes */
			BIT(3) | BIT(4) | BIT(5) | /* polarities */
			BIT(8) | BIT(9), /* parallel port mode */
			confctl);
	if (!err)
		err = regmap_write(priv->ctl_regmap, FIFOCTL, 16);
	if (!err)
		err = regmap_write(priv->ctl_regmap, DATAFMT, pdformat << 4);
	if (!err) /* Do we need to round somehow? */
		err = regmap_write(priv->ctl_regmap, WORDCNT,
				bt->width * bpp / 8);

	/* Setup the D-PHY */
	if (!err)
		err = regmap_write(priv->tx_regmap, LINEINITCNT, linecnt);

	if (!err)
		err = regmap_write(priv->tx_regmap, LPTXTIMECNT,
				lptxtime);
	if (!err)
		err = regmap_write(priv->tx_regmap, TCLK_HEADERCNT,
				tclk_prepare | (tclk_zero << 8));
	if (!err)
		err = regmap_write(priv->tx_regmap, TCLK_TRAILCNT,
				tclk_trail);
	if (!err)
		err = regmap_write(priv->tx_regmap, THS_HEADERCNT,
				ths_prepare | (ths_zero << 8));
	if (!err)
		err = regmap_write(priv->tx_regmap, TWAKEUP,
				t_wakeup);
	if (!err)
		err = regmap_write(priv->tx_regmap, TCLK_POSTCNT,
				tclk_post);
	if (!err)
		err = regmap_write(priv->tx_regmap, THS_TRAILCNT,
				ths_trail);

	if (!err) /* TX voltage regulators setup time */
		err = regmap_write(priv->tx_regmap, HSTXVREGCNT, 5);

	if (!err) /* Enable the TX voltage regulators */
		err = regmap_write(
			priv->tx_regmap, HSTXVREGEN,
			(((1 << csi_bus->num_data_lanes) - 1) << 1) |
			BIT(0));

	if (!err) /* Continuous clock */
		err = regmap_write(priv->tx_regmap, TXOPTIONCNTRL, 1);

	if (!err) /* Start the PPI */
		err = regmap_write(priv->tx_regmap, STARTCNTRL, 1);

	if (!err) /* CSI Start */
		err = regmap_write(priv->tx_regmap, CSI_START, 1);

	if (!err) /* Configure the CSI transmitter */
		err = regmap_write(priv->tx_regmap, CSI_CONFW,
				CSI_SET_REGISTER | CSI_CONTROL_REG |
				(csi_bus->num_data_lanes - 1) << 1 |
				BIT(7) | /* High-speed mode */
				BIT(15)); /* CSI mode */


	/* Setup the debug output */
	if (!err)
		err = regmap_update_bits(
			priv->ctl_regmap, DBG_LCNT, 0x3FF, bt->height - 1);
	if (!err)
		err = regmap_write( // FIXME!
			priv->ctl_regmap, DBG_WIDTH, 0x407);

	if (!err)
		err = regmap_write(
			priv->ctl_regmap, DBG_VBLANK, bt->vsync - 1);

	return err;
}

static int tc358748_try_mbus_fmt(
	struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *fmt)
{
	struct tc358748 *priv = container_of(sd, struct tc358748, subdev);
	int err;

	/* Get the format from the input */
	err = v4l2_subdev_call(priv->input, video, try_mbus_fmt, fmt);
	if (err)
		return err;

	return tc358748_setup(sd, fmt, 0);
}

static int tc358748_s_mbus_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *fmt)
{
	struct tc358748 *priv = container_of(sd, struct tc358748, subdev);
	int err;

	err = v4l2_subdev_call(priv->input, video, s_mbus_fmt, fmt);
	if (err)
		return err;

	err = tc358748_setup(sd, fmt, 1);
	if (err)
		return err;

	priv->framefmt = *fmt;

	return 0;
}

static int tc358748_g_mbus_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *fmt)
{
	struct tc358748 *priv = container_of(sd, struct tc358748, subdev);

	if (priv->framefmt.width != 0 && priv->framefmt.height != 0) {
		*fmt = priv->framefmt;
		return 0;
	}

	return -EINVAL;
}

static int tc358748_s_stream(struct v4l2_subdev *sd, int on)
{
	struct tc358748 *priv = container_of(sd, struct tc358748, subdev);
	int err;

	if (on) {
		/* Make sure we have setup a format before we start */
		if (priv->framefmt.width == 0 || priv->framefmt.height == 0)
			return -EINVAL;

		/* 1 Start Video to TC358746A */
		err = v4l2_subdev_call(priv->input, video, s_stream, 1);
		if (err && err != -ENOIOCTLCMD)
			return err;

		/* 2 Clear RstPtr and FrmStop to 1’b0 */
		err = regmap_write(priv->ctl_regmap, PP_MISC, 0);
		if (err) {
			v4l2_subdev_call(priv->input, video, s_stream, 0);
			return err;
		}

		/* 3 Set PP_En to 1’b1 */
		err = regmap_update_bits(priv->ctl_regmap, CONFCTL,
					BIT(6), BIT(6));
		if (err) {
			v4l2_subdev_call(priv->input, video, s_stream, 0);
			return err;
		}

		return 0;
	} else {
		/* 1 Set FrmStop to 1’b1, wait for at least one
		 * frame time for TC358746A to stop properly */
		err = regmap_update_bits(priv->ctl_regmap, PP_MISC,
					BIT(15), BIT(15));
		if (err)
			return err;
		/* Assume we have at least 20Hz referesh rate */
		usleep_range(50000, 100000);

		/* 2 Clear PP_En to 1’b0 */
		err = regmap_update_bits(priv->ctl_regmap, CONFCTL,
					BIT(6), 0);
		if (err)
			return err;

		/* 3 Set RstPtr to 1’b1 */
		err = regmap_update_bits(priv->ctl_regmap, PP_MISC,
					BIT(16), BIT(16));
		if (err)
			return err;

		/* 4 Stop Video to TC358746A (optional) */
		err = v4l2_subdev_call(priv->input, video, s_stream, 0);
		return err == -ENOIOCTLCMD ? 0 : err;
	}
}

static int tc358748_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct tc358748 *priv = container_of(sd, struct tc358748, subdev);

	return v4l2_subdev_call(priv->input, video, g_input_status, status);
}

static int tc358748_g_chip_ident(struct v4l2_subdev *sd,
		struct v4l2_dbg_chip_ident *id)
{
	struct tc358748 *priv = container_of(sd, struct tc358748, subdev);
	unsigned int val;
	int err;

	err = regmap_read(priv->ctl_regmap, CHIPID, &val);
	if (err)
		return err;

	id->ident = (val >> 8) & 0xFF;
	id->revision = val & 0xFF;

	return 0;
}

static struct v4l2_subdev_video_ops tc358748_subdev_video_ops = {
	.g_mbus_config = tc358748_g_mbus_config,
	.s_mbus_fmt = tc358748_s_mbus_fmt,
	.g_mbus_fmt = tc358748_g_mbus_fmt,
	.try_mbus_fmt = tc358748_try_mbus_fmt,
	.enum_mbus_fmt = tc358748_enum_mbus_fmt,
	.s_stream = tc358748_s_stream,
	.g_input_status = tc358748_g_input_status,
};

static struct v4l2_subdev_core_ops tc358748_subdev_core_ops = {
	.g_chip_ident = tc358748_g_chip_ident,
};

static struct v4l2_subdev_ops tc358748_subdev_ops = {
	.core = &tc358748_subdev_core_ops,
	.video = &tc358748_subdev_video_ops,
};

static int tc358748_input_bound(struct v4l2_async_notifier *notifier,
				struct v4l2_subdev *subdev,
				struct v4l2_async_subdev *asd)
{
	struct tc358748 *priv = container_of(
		notifier, struct tc358748, sd_notifier);

	if (priv->input)
		return -EBUSY;

	priv->input = subdev;

	return 0;
}

static void tc358748_input_unbind(struct v4l2_async_notifier *notifier,
				struct v4l2_subdev *subdev,
				struct v4l2_async_subdev *asd)
{
	struct tc358748 *priv = container_of(
		notifier, struct tc358748, sd_notifier);

	if (subdev == priv->input)
		priv->input = NULL;
}

const struct v4l2_async_notifier_operations tc358748_async_ops = {
	.bound = tc358748_input_bound,
	.unbind = tc358748_input_unbind,
};

static int tc358748_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device_node *np = NULL;
	struct tc358748 *priv;
	u32 rate;
	int err;

	priv = devm_kzalloc(&client->dev, sizeof(struct tc358748), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Read the ports config from OF */
	while ((np = v4l2_of_get_next_endpoint(client->dev.of_node, np))) {
		struct v4l2_of_endpoint ep;
		struct device_node *sd;

		if (!of_device_is_available(np)) {
			of_node_put(np);
			continue;
		}

		v4l2_of_parse_endpoint(np, &ep);

		if (ep.port == 0) { /* Input port */
			if (ep.id > 0) {
				dev_err(&client->dev,
					"Too many input endpoints\n");
				of_node_put(np);
				return -EINVAL;
			}
			priv->input_ep = ep;

			/* Get the input subdev for the async match */
			sd = v4l2_of_get_remote_port_parent(np);
			if (!sd || !of_device_is_available(sd)) {
				of_node_put(sd);
				continue;
			}

			priv->input_asd.match_type = V4L2_ASYNC_MATCH_OF;
			priv->input_asd.match.of.node = sd;
			priv->async_subdevs[0] = &priv->input_asd;
			priv->sd_notifier.num_subdevs = 1;
		} else if (ep.port == 1) {/* Output port */
			if (ep.id > 0) {
				dev_err(&client->dev,
					"Too many output endpoints\n");
				of_node_put(np);
				return -EINVAL;
			}
			priv->output_ep = ep;
		} else {
			dev_err(&client->dev, "Too many ports\n");
			of_node_put(np);
			return -EINVAL;
		}

		of_node_put(np);
	}

	if (priv->input_ep.bus_type != V4L2_MBUS_PARALLEL) {
		dev_err(&client->dev, "Only parallel input is supported\n");
		return -EINVAL;
	}
	if (priv->output_ep.bus_type != V4L2_MBUS_CSI2) {
		dev_err(&client->dev, "Only CSI2 output is supported\n");
		return -EINVAL;
	}

	/* FIXME: We should use a clock here, but the generic clock framework
	 * is not supported on Tegra with this kernel. */
	err = of_property_read_u32(client->dev.of_node, "clock-rate", &rate);
	if (err) {
		dev_err(&client->dev, "failed to get clock rate\n");
		return -EINVAL;
	}
	if (rate < 6000000 || rate > 40000000) {
		dev_err(&client->dev, "reference is out of range: %u\n", rate);
		return -EINVAL;
	}
	priv->refrate = rate;

	memcpy(priv->regulators, &tc358748_regulators, sizeof(priv->regulators));
	err = devm_regulator_bulk_get(&client->dev, ARRAY_SIZE(priv->regulators),
				priv->regulators);
	if (err < 0) {
		if (err != -EPROBE_DEFER)
			dev_err(&client->dev, "failed to get regulators\n");
		return err;
	}

	priv->reset_gpio = devm_gpiod_get_optional(
		&client->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset_gpio) && PTR_ERR(priv->reset_gpio) == -ENOENT)
		priv->reset_gpio = NULL;
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

	priv->ctl_regmap = devm_regmap_init_i2c(client, &ctl_regmap_config);
	if (IS_ERR(priv->ctl_regmap)) {
		dev_err(&client->dev,
			"regmap ctl init failed: %ld\n",
			PTR_ERR(priv->ctl_regmap));
		err = PTR_ERR(priv->ctl_regmap);
		goto reset;
	}

	priv->tx_regmap = devm_regmap_init_i2c(client, &tx_regmap_config);
	if (IS_ERR(priv->tx_regmap)) {
		dev_err(&client->dev,
			"regmap csi init failed: %ld\n",
			PTR_ERR(priv->tx_regmap));
		err = PTR_ERR(priv->tx_regmap);
		goto reset;
	}

	v4l2_i2c_subdev_init(&priv->subdev, client, &tc358748_subdev_ops);

	/* Reset and take out of sleep */
	if (!priv->reset_gpio) {
		err = regmap_write(priv->ctl_regmap, SYSCTL, BIT(1));
		if (err) {
			dev_err(&client->dev, "Failed set reset bit\n");
			return err;
		}
	} else {
		gpiod_set_value_cansleep(priv->reset_gpio, 0);
	}

	usleep_range(10, 100);

	err = regmap_write(priv->ctl_regmap, SYSCTL, 0);
	if (err) {
		dev_err(&client->dev, "Failed init wakeup\n");
		goto reset;
	}

	/* Disable everything, but enable I2C address increments */
	err = regmap_write(priv->ctl_regmap, CONFCTL, BIT(2));
	if (err) {
		dev_err(&client->dev,
			"Failed to setup I2C address increments\n");
		goto reset;
	}

	/* Start the clocks to allow access to the TX registers */
	err = tc358748_set_pll(priv, NULL);
	if (err) {
		dev_err(&client->dev, "Failed to setup PLL\n");
		goto reset;
	}

	/* Setup the subdev notifier, it will be registerd once
	 * we are registered as we need the v4l2 device for this.
	 */
	priv->sd_notifier.subdevs = priv->async_subdevs;
	priv->sd_notifier.ops = &tc358748_async_ops;
	err = v4l2_async_subdev_notifier_register(
		&priv->subdev, &priv->sd_notifier);
	if (err) {
		dev_err(&client->dev, "failed to register async notifier\n");
		return err;
	}

	err = v4l2_async_register_subdev(&priv->subdev);
	if (err) {
		dev_err(&client->dev, "Failed to register async subdev\n");
		goto unregister_async_notifier;
	}


	return 0;

unregister_async_notifier:
	v4l2_async_notifier_unregister(&priv->sd_notifier);
reset:
	if (priv->reset_gpio)
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(priv->regulators), priv->regulators);
	return err;
}

static int tc358748_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct tc358748 *priv = container_of(sd, struct tc358748, subdev);

	v4l2_async_notifier_unregister(&priv->sd_notifier);
	v4l2_async_unregister_subdev(sd);

	return 0;
}

static const struct i2c_device_id tc358748_id[] = {
	{ "tc358746axbg", 0 },
	{ "tc358748xbg", 0 },
	{ "tc358748ixbg", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tc358748_id);

#ifdef CONFIG_OF
static const struct of_device_id tc358748_of_table[] = {
	{ .compatible = "toshiba,tc358746axbg" },
	{ .compatible = "toshiba,tc358748xbg" },
	{ .compatible = "toshiba,tc358748ixbg" },
	{ }
};
MODULE_DEVICE_TABLE(of, tc358748_of_table);
#endif

static struct i2c_driver tc358748_driver = {
	.driver = {
		.of_match_table = of_match_ptr(tc358748_of_table),
		.name = "tc358748",
		.owner = THIS_MODULE,
	},
	.probe = tc358748_probe,
	.remove = tc358748_remove,
	.id_table = tc358748_id,
};
module_i2c_driver(tc358748_driver);

MODULE_DESCRIPTION("Driver for Toshiba TC358846/8 Parallel-CSI bridge");
MODULE_AUTHOR("Alban Bedel <alban.bedel@avionic-design.de>");
MODULE_LICENSE("GPL");
