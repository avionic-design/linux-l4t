/*
 * tlv320dac3100.c -- Codec driver for TI TLV320DAC3100
 *
 * Copyright (c) 2015 Avionic Design GmbH
 *
 * Authors: Alban Bedel <alban.bedel@avionic-design.de>
 *          Julian Scheel <julian@jusst.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>
#include <linux/log2.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/pcm_params.h>

#define DAC3100_PAGE_SIZE 128
#define DAC3100_MAX_PAGES 13
#define DAC3100_MAX_REGISTERS (DAC3100_MAX_PAGES * DAC3100_PAGE_SIZE)

#define DAC3100_REG(p, r) ((p) * DAC3100_PAGE_SIZE + (r))

#define DAC3100_REG_PAGE(r) ((r) / DAC3100_PAGE_SIZE)

#define DAC3100_RESET			DAC3100_REG(0, 1)

#define DAC3100_CLOCK_GEN_MUX		DAC3100_REG(0, 4)
#define DAC3100_PLL_P_R			DAC3100_REG(0, 5)
#define DAC3100_PLL_J			DAC3100_REG(0, 6)
#define DAC3100_PLL_D_MSB		DAC3100_REG(0, 7)
#define DAC3100_PLL_D_LSB		DAC3100_REG(0, 8)

#define DAC3100_DAC_NDAC		DAC3100_REG(0, 11)
#define DAC3100_DAC_MDAC		DAC3100_REG(0, 12)
#define DAC3100_DAC_DOSR_MSB		DAC3100_REG(0, 13)
#define DAC3100_DAC_DOSR_LSB		DAC3100_REG(0, 14)

#define DAC3100_CODEC_IFACE_CTRL1	DAC3100_REG(0, 27)
#define DAC3100_DATA_SLOT_OFFSET	DAC3100_REG(0, 28)
#define DAC3100_CODEC_IFACE_CTRL2	DAC3100_REG(0, 29)

#define DAC3100_DAC_FLAGS_0		DAC3100_REG(0, 37)
#define DAC3100_DAC_FLAGS_1		DAC3100_REG(0, 38)
#define DAC3100_OVERFLOW_FLAGS		DAC3100_REG(0, 39)
#define DAC3100_DAC_INT_FLAGS		DAC3100_REG(0, 44)
#define DAC3100_DAC_INT_STATUS		DAC3100_REG(0, 46)
#define DAC3100_GPIO1_CTRL		DAC3100_REG(0, 51)
#define DAC3100_DIN_CTRL		DAC3100_REG(0, 54)

#define DAC3100_DAC_PROCESSING_BLOCK	DAC3100_REG(0, 60)

#define DAC3100_DAC_DATA_PATH_SETUP	DAC3100_REG(0, 63)
#define DAC3100_DAC_VOLUME		DAC3100_REG(0, 64)
#define DAC3100_DAC_LEFT_VOLUME		DAC3100_REG(0, 65)
#define DAC3100_DAC_RIGHT_VOLUME	DAC3100_REG(0, 66)
#define DAC3100_HEADSET_DETECT		DAC3100_REG(0, 67)

#define DAC3100_LEFT_BEEP_GEN		DAC3100_REG(0, 71)
#define DAC3100_RIGHT_BEEP_GEN		DAC3100_REG(0, 72)

#define DAC3100_MICDET_GAIN		DAC3100_REG(0, 117)

#define DAC3100_HP_DRIVER		DAC3100_REG(1, 31)
#define DAC3100_SPK_AMP			DAC3100_REG(1, 32)

#define DAC3100_DAC_MIXER		DAC3100_REG(1, 35)
#define DAC3100_LEFT_VOL_HPL		DAC3100_REG(1, 36)
#define DAC3100_RIGHT_VOL_HPR		DAC3100_REG(1, 37)
#define DAC3100_LEFT_VOL_SPK		DAC3100_REG(1, 38)
#define DAC3100_HPL_DRIVER		DAC3100_REG(1, 40)
#define DAC3100_HPR_DRIVER		DAC3100_REG(1, 41)
#define DAC3100_SPK_DRIVER		DAC3100_REG(1, 42)

#define DAC3100_MICBIAS			DAC3100_REG(1, 46)

#define DAC3100_DAC_COEF_RAM		DAC3100_REG(8, 1)

#define DAC3100_PLL_CLK_MIN	80000000
#define DAC3100_PLL_CLK_MAX	110000000

#define DAC3100_DAC_MOD_CLK_MIN	2800000
#define DAC3100_DAC_MOD_CLK_MAX	6200000

struct dac3100 {
	struct device *dev;
	struct snd_soc_codec *codec;
	struct regmap *regmap;
	struct gpio_desc *reset_gpio;

	unsigned clkin_rate;
	unsigned clkin_src;
};

static bool dac3100_reg_page_is_valid(unsigned int reg)
{
	switch (DAC3100_REG_PAGE(reg)) {
	case 0:
	case 1:
	case 3:
	case 8:
	case 9:
	case 12:
	case 13:
		return true;
	default:
		return false;
	}
}

static bool dac3100_readable_register(struct device *dev, unsigned int reg)
{
	if (!dac3100_reg_page_is_valid(reg))
		return false;
	return true;
}

static bool dac3100_writable_register(struct device *dev, unsigned int reg)
{
	if (!dac3100_reg_page_is_valid(reg))
		return false;
	return true;
}

static bool dac3100_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case DAC3100_RESET:
	case DAC3100_DAC_FLAGS_0:
	case DAC3100_DAC_FLAGS_1:
	case DAC3100_OVERFLOW_FLAGS:
	case DAC3100_DAC_INT_FLAGS:
	case DAC3100_DAC_INT_STATUS:
	case DAC3100_GPIO1_CTRL:
	case DAC3100_DIN_CTRL:
	case DAC3100_HEADSET_DETECT:
	case DAC3100_LEFT_BEEP_GEN:
	case DAC3100_MICDET_GAIN:
	case DAC3100_HP_DRIVER:
	case DAC3100_SPK_AMP:
	case DAC3100_HPL_DRIVER:
	case DAC3100_HPR_DRIVER:
	case DAC3100_SPK_DRIVER:
	case DAC3100_DAC_COEF_RAM:
		return true;
	default:
		return false;
	}
}

static bool dac3100_precious_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case DAC3100_OVERFLOW_FLAGS:
	case DAC3100_DAC_INT_FLAGS:
		return true;
	default:
		return false;
	}
}

static const struct regmap_range_cfg dac3100_regmap_pages[] = {
	{
		.selector_reg = 0,
		.selector_mask  = 0xff,
		.window_start = 0,
		.window_len = DAC3100_PAGE_SIZE,
		.range_min = 0,
		.range_max = DAC3100_MAX_REGISTERS,
        },
};

static const struct regmap_config dac3100_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = DAC3100_MAX_REGISTERS,
	.readable_reg = dac3100_readable_register,
	.writeable_reg = dac3100_writable_register,
	.volatile_reg = dac3100_volatile_register,
	.precious_reg = dac3100_precious_register,

	.ranges = dac3100_regmap_pages,
	.num_ranges = ARRAY_SIZE(dac3100_regmap_pages),
};

static const DECLARE_TLV_DB_SCALE(dac_gain_tlv, -6350, 50, 0);
static const DECLARE_TLV_DB_SCALE(hp_gain_tlv, 0, 100, 0);
static const DECLARE_TLV_DB_SCALE(spk_gain_tlv, 600, 6, 0);

/* It is not supported to supply more than 10 scale items, so the scale is not
 * 100% exact */
static const unsigned int analog_att_tlv[] = {
	TLV_DB_RANGE_HEAD(10),
	0, 1, TLV_DB_SCALE_ITEM(-7830, 610, 0),
	2, 3, TLV_DB_SCALE_ITEM(-6870, 250, 0),
	4, 5, TLV_DB_SCALE_ITEM(-6430, 160, 0),
	6, 7, TLV_DB_SCALE_ITEM(-6020, 190, 0),
	8, 9, TLV_DB_SCALE_ITEM(-5670, 140, 0),
	10, 26, TLV_DB_SCALE_ITEM(-5420, 53, 0),
	27, 33, TLV_DB_SCALE_ITEM(-4520, 58, 0),
	34, 48, TLV_DB_SCALE_ITEM(-4170, 50, 0),
	49, 81, TLV_DB_SCALE_ITEM(-3410, 50, 0),
	82, 117, TLV_DB_SCALE_ITEM(-1750, 50, 0),
};

static const char *dac_route_text[] = {
	"Off", "Mixer", "Driver"
};
static const struct soc_enum dac_l_route =
	SOC_ENUM_SINGLE(DAC3100_DAC_MIXER, 6, 3, dac_route_text);

static const struct soc_enum dac_r_route =
	SOC_ENUM_SINGLE(DAC3100_DAC_MIXER, 2, 3, dac_route_text);

static const struct snd_kcontrol_new dac3100_snd_controls[] = {
	/* DAC Mixer */
	SOC_DOUBLE("DAC Switch", DAC3100_DAC_VOLUME, 3, 2, 1, 1),
	SOC_DOUBLE_R_S_TLV("DAC Volume", DAC3100_DAC_LEFT_VOLUME,
		DAC3100_DAC_RIGHT_VOLUME, 0, -127, 48, 7, 0, dac_gain_tlv),

	/* Analog attenuators */
	SOC_SINGLE("Speaker Switch", DAC3100_LEFT_VOL_SPK,
		7, 1, 0),
	SOC_SINGLE_TLV("Speaker Volume", DAC3100_LEFT_VOL_SPK,
		0, 117, 1, analog_att_tlv),
	SOC_DOUBLE_R("Headphone Switch", DAC3100_LEFT_VOL_HPL,
		DAC3100_RIGHT_VOL_HPR, 7, 1, 0),
	SOC_DOUBLE_R_TLV("Headphone Volume", DAC3100_LEFT_VOL_HPL,
		DAC3100_RIGHT_VOL_HPR, 0, 117, 1, analog_att_tlv),

	/* DAC Routing */
	SOC_ENUM("DACL Route", dac_l_route),
	SOC_ENUM("DACR Route", dac_r_route),

	/* Driver gains + mute */
	SOC_SINGLE_TLV("Speaker Driver Gain", DAC3100_SPK_DRIVER,
		3, 3, 0, spk_gain_tlv),
	SOC_SINGLE("Speaker Driver Switch", DAC3100_SPK_DRIVER,
		2, 1, 0),
	SOC_DOUBLE_R_TLV("Headphone Driver Gain", DAC3100_HPL_DRIVER,
		DAC3100_HPR_DRIVER, 3, 9, 0, hp_gain_tlv),
	SOC_DOUBLE_R("Headphone Driver Switch", DAC3100_HPL_DRIVER,
		DAC3100_HPR_DRIVER, 2, 1, 0),
};

static const struct snd_soc_dapm_widget dac3100_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("AIN1"),
	SND_SOC_DAPM_INPUT("AIN2"),

	SND_SOC_DAPM_OUTPUT("SPK"),
	SND_SOC_DAPM_OUTPUT("HPL"),
	SND_SOC_DAPM_OUTPUT("HPR"),

	SND_SOC_DAPM_DAC("DACL", "Left Playback", DAC3100_DAC_DATA_PATH_SETUP, 7, 0),
	SND_SOC_DAPM_DAC("DACR", "Right Playback", DAC3100_DAC_DATA_PATH_SETUP, 6, 0),

	SND_SOC_DAPM_PGA("Speaker Driver", DAC3100_SPK_AMP, 7, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPL Driver", DAC3100_HP_DRIVER, 7, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPR Driver", DAC3100_HP_DRIVER, 6, 0, NULL, 0),
};

static const struct snd_soc_dapm_route dac3100_intercon[] = {
	{ "HPL Driver", NULL, "DACL" },
	{ "HPR Driver", NULL, "DACR" },

	{ "Speaker Driver", NULL, "DACL" },
	{ "Speaker Driver", NULL, "DACR" },

	{ "HPL", NULL, "HPL Driver" },
	{ "HPR", NULL, "HPR Driver" },

	{ "SPK", NULL, "Speaker Driver" },
};

static int dac3100_set_dai_sysclk(struct snd_soc_dai *dai,
				int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct dac3100 *dac = snd_soc_codec_get_drvdata(codec);

	dac->clkin_src = clk_id;
	dac->clkin_rate = freq;

	return 0;
}

static int dac3100_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params,
			struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct dac3100 *dac = snd_soc_codec_get_drvdata(codec);
	int channels = params_channels(params);
	int fs = params_rate(params);
	int dosr, dosr_round;
	int mdiv, ndac, mdac;
	int filter, pb, rc;
	int jd = 10000;
	int word_len;
	int clkmux;
	int clkin;
	int err;

	/* Check the word length */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		word_len = 0;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		word_len = 1;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		word_len = 2;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		word_len = 3;
		break;
	default:
		return -EINVAL;
	}

	/* Select the filter and DOSR rounding according to the samplerate */
	if (fs > 96000) {
		filter = 2;
		dosr_round = 2;
	} else if (fs > 48000) {
		filter = 1;
		dosr_round = 4;
	} else {
		filter = 0;
		dosr_round = 8;
	}

	/* TODO: When implementing filters replace this with a table lookup
	 * to choose the best processing block. */
	switch(filter) {
	case 0:
		if (channels > 1) {
			pb = 1;
			rc = 8;
		} else {
			pb = 4;
			rc = 4;
		}
		break;
	case 1:
		if (channels > 1) {
			pb = 7;
			rc = 6;
		} else {
			pb = 12;
			rc = 3;
		}
		break;
	case 2:
		if (channels > 1) {
			pb = 17;
			rc = 3;
		} else {
			pb = 20;
			rc = 2;
		}
		break;
	default:
		return -EINVAL;
	}

	/* If the mclk is not a multiple of the samplerate
	 * we need to use the fractional PLL to produce such a rate */
	if (dac->clkin_rate % fs) {
		/* The PLL output must be between 80 and 110MHz */
		int mult = roundup_pow_of_two(DAC3100_PLL_CLK_MIN / fs);
		while (mult <= 1024 * 128 * 128 &&
				fs * mult <= DAC3100_PLL_CLK_MAX) {
			u64 c = (u64)fs * mult * 10000;
			if (!do_div(c, dac->clkin_rate) && c >= 10000) {
				jd = c;
				break;
			}
			mult *= 2;
		}

		if (mult > 1024 * 128 * 128 ||
				fs * mult > DAC3100_PLL_CLK_MAX) {
			dev_err(codec->dev,
				"Couldn't setup fractional divider\n");
			return -EINVAL;
		}
		clkin = fs * mult;
	} else {
		clkin = dac->clkin_rate;
		/* Check that the clock is fast enough, if not add a multiplier */
		if (clkin < rc * fs * 32) {
			int mult = DIV_ROUND_UP(80000000, clkin);

			/* Check that we are still in the range of the PLL */
			if (clkin * mult > 110000000 || mult < 4 || mult > 63) {
				dev_err(codec->dev, "Couldn't find multiplier\n");
				return -EINVAL;
			}

			jd = mult * 10000;
			clkin *= mult;
		}
	}

	/* Find the highest possible DOSR value */
	dosr = DAC3100_DAC_MOD_CLK_MAX / fs;
	dosr = dosr / dosr_round * dosr_round;

	/* Look for a DOSR value that is a multiple of FS
	 * and need an acceptable divider */
	while (dosr * fs >= DAC3100_DAC_MOD_CLK_MIN) {
		mdiv = clkin / (dosr * fs);
		if (mdiv * dosr * fs == clkin && mdiv < 128 * 128)
			break;
		dosr -= dosr_round;
	}

	if (dosr * fs < DAC3100_DAC_MOD_CLK_MIN) {
		dev_err(codec->dev, "Failed to find clock setup\n");
		return -EINVAL;
	}

	/* Get the smallest possible MDAC with a valid NDAC */
	for (mdac = max(rc * 32 / dosr, 1); mdac <= 128; mdac++) {
		if (mdiv % mdac == 0 && mdiv / mdac <= 128)
			break;
	}

	if (mdac > 128) {
		dev_err(codec->dev, "Failed to find divider setup\n");
		return -EINVAL;
	}

	ndac = mdiv / mdac;

	if (clkin / ndac > 48000000) {
		dev_err(codec->dev, "Failed to find divider setup\n");
		return -EINVAL;
	}

	dev_dbg(codec->dev, "codec settings: sysclk=%d, clkin=%d, "
		"jd=%d, ndac=%d, mdac=%d, dosr=%d, pb=%d, rc=%d\n",
		dac->clkin_rate, clkin, jd, ndac, mdac, dosr, pb, rc);

	/* Make sure the dividers and PLL are stopped */
	err = snd_soc_write(codec, DAC3100_DAC_MDAC, 0);
	if (err)
		goto error;
	err = snd_soc_write(codec, DAC3100_DAC_NDAC, 0);
	if (err)
		goto error;
	err = snd_soc_write(codec, DAC3100_PLL_P_R, 0x11);
	if (err)
		goto error;

	/* Setup the clock mux */
	clkmux = dac->clkin_src & 3;
	if (jd > 10000)
		clkmux = (clkmux << 2) | 3;

	err = snd_soc_write(codec, DAC3100_CLOCK_GEN_MUX, clkmux);
	if (err)
		goto error;

	/* Setup the PLL if needed */
	if (jd > 10000) {
		err = snd_soc_write(codec, DAC3100_PLL_J, jd / 10000);
		if (err)
			goto error;

		err = snd_soc_write(codec, DAC3100_PLL_D_MSB,
				(jd % 10000) >> 8);
		if (err)
			goto error;
		err = snd_soc_write(codec, DAC3100_PLL_D_LSB,
				(jd % 10000) & 0xFF);
		if (err)
			goto error;

		/* Start the PLL and wait for the lock */
		err = snd_soc_write(codec, DAC3100_PLL_P_R, 0x91);
		if (err)
			goto error;
		msleep(10);
	}

	/* Configure the dividers */
	err = snd_soc_write(codec, DAC3100_DAC_NDAC, (ndac & 0x7f) | BIT(7));
	if (err)
		goto error_stop_pll;
	err = snd_soc_write(codec, DAC3100_DAC_MDAC, (mdac & 0x7f) | BIT(7));
	if (err)
		goto error_ndac;
	err = snd_soc_write(codec, DAC3100_DAC_DOSR_MSB, (dosr >> 8) & 3);
	if (err)
		goto error_mdac;
	err = snd_soc_write(codec, DAC3100_DAC_DOSR_LSB, dosr & 0xff);
	if (err)
		goto error_mdac;

	/* Setup the word size */
	err = snd_soc_update_bits(codec, DAC3100_CODEC_IFACE_CTRL1,
				3 << 4, word_len << 4);
	if (err < 0)
		goto error_mdac;

	/* Setup the processing block */
	err = snd_soc_write(codec, DAC3100_DAC_PROCESSING_BLOCK, pb);
	if (err)
		goto error_mdac;

	return 0;

error_mdac:
	snd_soc_write(codec, DAC3100_DAC_MDAC, 0);
error_ndac:
	snd_soc_write(codec, DAC3100_DAC_NDAC, 0);
error_stop_pll:
	snd_soc_write(codec, DAC3100_PLL_P_R, 0x11);
error:
	return err;
}

static int dac3100_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	u8 ctrl1 = snd_soc_read(codec, DAC3100_CODEC_IFACE_CTRL1);
	u8 ctrl2 = snd_soc_read(codec, DAC3100_CODEC_IFACE_CTRL2);
	int err;

	/* Clear everything except the bit per samples */
	ctrl1 &= ~(3 << 4);

	/* Set the clocks direction */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		ctrl1 |= 1 << 2;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		ctrl1 |= 2 << 2;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		ctrl1 |= 3 << 2;
		break;
	default:
		return -EINVAL;
	}

	/* Set the data format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_DSP_A:
		ctrl1 |= 1 << 6;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		ctrl1 |= 2 << 6;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		ctrl1 |= 3 << 6;
	default:
		return -EINVAL;
	}

	/* Set the clocks inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		ctrl2 &= ~BIT(3);
		break;
	case SND_SOC_DAIFMT_IB_NF:
		ctrl2 |= BIT(3);
		break;
	default:
		return -EINVAL;
	}

	err = snd_soc_write(codec, DAC3100_CODEC_IFACE_CTRL1, ctrl1);
	if (err)
		return err;

	err = snd_soc_write(codec, DAC3100_CODEC_IFACE_CTRL1, ctrl2);
	if (err)
		return err;

	return 0;
}

static struct snd_soc_dai_ops dac3100_dai_ops = {
	.set_sysclk	= dac3100_set_dai_sysclk,
	.set_fmt	= dac3100_set_dai_fmt,
	.hw_params	= dac3100_hw_params,
};

static struct snd_soc_dai_driver dac3100_dai = {
	.name = "dac3100-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_CONTINUOUS |
			SNDRV_PCM_RATE_8000_192000,
		.formats =  SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S20_3LE |
			SNDRV_PCM_FMTBIT_S24_LE |
			SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &dac3100_dai_ops,
};

static struct snd_soc_codec_driver soc_codec_dac3100 = {
	.controls = dac3100_snd_controls,
	.num_controls = ARRAY_SIZE(dac3100_snd_controls),
	.dapm_widgets = dac3100_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(dac3100_dapm_widgets),
	.dapm_routes = dac3100_intercon,
	.num_dapm_routes = ARRAY_SIZE(dac3100_intercon),
};

static int dac3100_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct dac3100 *dac;
	int err;

	dac = devm_kzalloc(&client->dev, sizeof(*dac), GFP_KERNEL);
	if (dac == NULL)
		return -ENOMEM;

	dac->dev = &client->dev;
	dac->regmap = devm_regmap_init_i2c(client, &dac3100_regmap);
	if (IS_ERR(dac->regmap)) {
		dev_err(&client->dev, "Failed to create regmap: %ld\n",
			PTR_ERR(dac->regmap));
		return PTR_ERR(dac->regmap);
	}

	i2c_set_clientdata(client, dac);

	/* Hard reset the chip if possible */
	dac->reset_gpio = devm_gpiod_get_optional(&client->dev, "reset",
						GPIOD_OUT_HIGH);
	if (IS_ERR(dac->reset_gpio)) {
		dev_err(&client->dev, "Failed to get reset GPIO: %ld\n",
			PTR_ERR(dac->reset_gpio));
		return PTR_ERR(dac->reset_gpio);
	} else if (dac->reset_gpio) {
		usleep_range(1, 1000);
		gpiod_set_value(dac->reset_gpio, 0);
	}

	/* Soft reset the chip to also check the I2C bus */
	err = regmap_write(dac->regmap, DAC3100_RESET, 1);
	if (err) {
		dev_err(&client->dev, "Failed to reset: %d\n", err);
		return err;
	}
	msleep(1);

	/* Register the codec */
	return snd_soc_register_codec(&client->dev,
				&soc_codec_dac3100, &dac3100_dai, 1);

}

static int dac3100_i2c_remove(struct i2c_client *i2c)
{
	struct dac3100 *dac = i2c_get_clientdata(i2c);

	snd_soc_unregister_codec(&i2c->dev);

	if (dac->reset_gpio)
		gpiod_set_value(dac->reset_gpio, 1);

	return 0;
}

static const struct of_device_id dac3100_of_match[] = {
	{ .compatible = "ti,tlv320dac3100", },
	{},
};
MODULE_DEVICE_TABLE(of, dac3100_of_match);

static const struct i2c_device_id dac3100_i2c_id[] = {
	{ "tlv320dac3100", 0x18 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, dac3100_i2c_id);

static struct i2c_driver dac3100_i2c_driver = {
	.driver = {
		.name = "tlv320dac3100-codec",
		.owner = THIS_MODULE,
		.of_match_table = dac3100_of_match,
	},
	.probe = dac3100_i2c_probe,
	.remove = dac3100_i2c_remove,
	.id_table = dac3100_i2c_id,
};

module_i2c_driver(dac3100_i2c_driver);

MODULE_DESCRIPTION("ASoC TLV320DAC3100 codec driver");
MODULE_AUTHOR("Alban Bedel <alban.bedel@avionic-design.de>");
MODULE_LICENSE("GPL");
