/*
 * Copyright 2015 Alban Bedel <alban.bedel@avionic-design.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/pcm_params.h>

#define CS53L30_DEVICE_ID_AB		0x01
#define CS53L30_POWER_CTRL		0x06
#define CS53L30_MCLK			0x07
#define CS53L30_INTERNAL_RATE_CTRL	0x08
#define CS53L30_MIC_BIAS_CTRL		0x0a
#define CS53L30_ASP_CFG_CTRL		0x0c
#define CS53L30_ASP_CTRL1		0x0d
#define CS53L30_ASP_TDM_TX_CTRL(x)	(0x0e + (x))
#define CS53L30_ASP_TDM_TX_ENABLE(x)	(0x17 - (x))

#define CS53L30_ADC1_CTRL1		0x25
#define CS53L30_ADC1_CTRL2		0x26
#define CS53L30_ADC1_CTRL3		0x27
#define CS53L30_ADC1_NG_CTRL		0x28
#define CS53L30_ADC1A_AFE_CTRL		0x29
#define CS53L30_ADC1B_AFE_CTRL		0x2a
#define CS53L30_ADC1A_DIGITAL_VOLUME	0x2b
#define CS53L30_ADC1B_DIGITAL_VOLUME	0x2c
#define CS53L30_ADC2_CTRL1		0x2d
#define CS53L30_ADC2_CTRL2		0x2e
#define CS53L30_ADC2_CTRL3		0x2f
#define CS53L30_ADC2_NG_CTRL		0x30
#define CS53L30_ADC2A_AFE_CTRL		0x31
#define CS53L30_ADC2B_AFE_CTRL		0x32
#define CS53L30_ADC2A_DIGITAL_VOLUME	0x33
#define CS53L30_ADC2B_DIGITAL_VOLUME	0x34
#define CS53L30_INTERRUPT_MASK		0x35
#define CS53L30_INTERRUPT_STATUS	0x36

#define CS53L30_MAX_REGISTERS		0x36


#define CS53L30_MCLK_SYNC_EN_SHIFT	1
#define CS53L30_MCLK_SYNC_EN_MASK	(1 << CS53L30_MCLK_SYNC_EN_SHIFT)
#define CS53L30_MCLK_DIV_SHIFT		2
#define CS53L30_MCLK_DIV_MASK		(3 << CS53L30_MCLK_DIV_SHIFT)
#define CS53L30_MCLK_INT_SCALE_SHIFT	6
#define CS53L30_MCLK_INT_SCALE_MASK	(1 << CS53L30_MCLK_INT_SCALE_SHIFT)
#define CS53L30_MCLK_DISABLE_SHIFT	7
#define CS53L30_MCLK_DISABLE_MASK	(1 << CS53L30_MCLK_DISABLE_SHIFT)

#define CS53L30_INTERNAL_RATE_CTRL_FS_RATIO_SHIFT	4
#define CS53L30_INTERNAL_RATE_CTRL_FS_RATIO_MASK	\
	(1 << CS53L30_INTERNAL_RATE_CTRL_FS_RATIO_SHIFT)

#define CS53L30_INTERNAL_RATE_CTRL_MCLK_19MHZ_SHIFT	0
#define CS53L30_INTERNAL_RATE_CTRL_MCLK_19MHZ_MASK	\
	(1 << CS53L30_INTERNAL_RATE_CTRL_MCLK_19MHZ_SHIFT)

#define CS53L30_ASP_TDM_TX_CTRL_STATE_SHIFT		7
#define CS53L30_ASP_TDM_TX_CTRL_STATE_MASK		\
	(1 << CS53L30_ASP_TDM_TX_CTRL_STATE_SHIFT)

#define CS53L30_ASP_CFG_CTRL_SCLK_INV_SHIFT		4
#define CS53L30_ASP_CFG_CTRL_SCLK_INV_MASK		\
	(1 << CS53L30_ASP_CFG_CTRL_SCLK_INV_SHIFT)

#define CS53L30_ASP_CFG_CTRL_MASTER_SHIFT		7
#define CS53L30_ASP_CFG_CTRL_MASTER_MASK		\
	(1 << CS53L30_ASP_CFG_CTRL_MASTER_SHIFT)

#define CS53L30_ASP_CTRL1_SHIFT_LEFT_SHIFT		4
#define CS53L30_ASP_CTRL1_SHIFT_LEFT_MASK		\
	(1 << CS53L30_ASP_CTRL1_SHIFT_LEFT_SHIFT)

#define CS53L30_ASP_CTRL1_TRISTATE_SHIFT		5
#define CS53L30_ASP_CTRL1_TRISTATE_MASK			\
	(1 << CS53L30_ASP_CTRL1_TRISTATE_SHIFT)

#define CS53L30_ASP_CTRL1_TDM_PDN_SHIFT			7
#define CS53L30_ASP_CTRL1_TDM_PDN_MASK			\
	(1 << CS53L30_ASP_CTRL1_TDM_PDN_SHIFT)


#define CS53L30_CHANNEL_COUNT				4

struct cs53l30_clock_rate {
	unsigned int lrck;
	unsigned int asp_rate;
};

struct cs53l30_clock_config {
	unsigned long mclk;
	unsigned int mclk_div;
	unsigned int fs_ratio;

	const struct cs53l30_clock_rate *rates;
	unsigned int rate_count;
};

struct cs53l30 {
	struct device *dev;
	struct snd_soc_codec *codec;
	struct regmap *regmap;
	struct gpio_desc *reset_gpio;

	const struct cs53l30_clock_config *clock_config;
	unsigned int dai_fmt;
};

static const struct regmap_range cs53l30_interrupt_status_range = {
	.range_min = CS53L30_INTERRUPT_STATUS,
	.range_max = CS53L30_INTERRUPT_STATUS,
};

static const struct regmap_access_table cs53l30_writable_regs = {
	.no_ranges = &cs53l30_interrupt_status_range,
	.n_no_ranges = 1,
};

static const struct regmap_access_table cs53l30_volatile_regs = {
	.yes_ranges = &cs53l30_interrupt_status_range,
	.n_yes_ranges = 1,
};

static const struct regmap_config cs53l30_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	/* The MSB of the register address must be set to enable
	 * address autoincrement. */
	.read_flag_mask = 0x80,
	.write_flag_mask = 0x80,

	.max_register = CS53L30_MAX_REGISTERS,
	.wr_table = &cs53l30_writable_regs,
	.volatile_table = &cs53l30_volatile_regs,
	.precious_table = &cs53l30_volatile_regs,
};


static const DECLARE_TLV_DB_SCALE(preamp_gain, 0, 1000, 0);

static const DECLARE_TLV_DB_SCALE(pga_gain, -600, 50, 0);

static const DECLARE_TLV_DB_SCALE(digital_volume_gain, -9600, 100, 1);

static const struct snd_kcontrol_new cs53l30_snd_controls[] = {
	SOC_DOUBLE_R_TLV("ADC1 Preamp", CS53L30_ADC1A_AFE_CTRL,
			CS53L30_ADC1B_AFE_CTRL, 6, 2, 0, preamp_gain),
	SOC_DOUBLE_R_TLV("ADC2 Preamp", CS53L30_ADC2A_AFE_CTRL,
			CS53L30_ADC2B_AFE_CTRL, 6, 2, 0, preamp_gain),

	SOC_DOUBLE_R_S_TLV("ADC1 PGA Volume", CS53L30_ADC1A_AFE_CTRL,
			CS53L30_ADC1B_AFE_CTRL, 0, -12, 24, 5, 0, pga_gain),
	SOC_DOUBLE_R_S_TLV("ADC2 PGA Volume", CS53L30_ADC2A_AFE_CTRL,
			CS53L30_ADC2B_AFE_CTRL, 0, -12, 24, 5, 0, pga_gain),

	SOC_DOUBLE_R_S_TLV("ADC1 Digital Volume",
			CS53L30_ADC1A_DIGITAL_VOLUME,
			CS53L30_ADC1B_DIGITAL_VOLUME,
			0, -96, 12, 7, 0, digital_volume_gain),
	SOC_DOUBLE_R_S_TLV("ADC2 Digital Volume",
			CS53L30_ADC2A_DIGITAL_VOLUME,
			CS53L30_ADC2B_DIGITAL_VOLUME,
			0, -96, 12, 7, 0, digital_volume_gain),

	SOC_DOUBLE("ADC1 Digital Boost", CS53L30_ADC1_CTRL2, 0, 1, 1, 0),
	SOC_DOUBLE("ADC2 Digital Boost", CS53L30_ADC2_CTRL2, 0, 1, 1, 0),

	SOC_DOUBLE("ADC1 Invert Polarity", CS53L30_ADC1_CTRL2, 4, 5, 1, 0),
	SOC_DOUBLE("ADC2 Invert Polarity", CS53L30_ADC2_CTRL2, 4, 5, 1, 0),

	SOC_SINGLE("ADC1 Notch Filter", CS53L30_ADC1_CTRL2, 7, 1, 1),
	SOC_SINGLE("ADC2 Notch Filter", CS53L30_ADC2_CTRL2, 7, 1, 1),
};

static const char *cs53l30_channel_type_text[] = { "Analog", "Digital" };

static const struct soc_enum cs53l30_channel_type_enum =
	SOC_ENUM_SINGLE(CS53L30_ADC1_CTRL1, 0, 2, cs53l30_channel_type_text);

static const struct snd_kcontrol_new cs53l30_channel_type_control =
	SOC_DAPM_ENUM("Input Channel Type", cs53l30_channel_type_enum);

static const struct snd_soc_dapm_widget cs53l30_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("IN1", NULL),
	SND_SOC_DAPM_MIC("IN2", NULL),
	SND_SOC_DAPM_MIC("IN3", NULL),
	SND_SOC_DAPM_MIC("IN4", NULL),

	SND_SOC_DAPM_MICBIAS("Mic Bias 1", CS53L30_MIC_BIAS_CTRL, 4, 1),
	SND_SOC_DAPM_MICBIAS("Mic Bias 2", CS53L30_MIC_BIAS_CTRL, 5, 1),
	SND_SOC_DAPM_MICBIAS("Mic Bias 3", CS53L30_MIC_BIAS_CTRL, 6, 1),
	SND_SOC_DAPM_MICBIAS("Mic Bias 4", CS53L30_MIC_BIAS_CTRL, 7, 1),

	SND_SOC_DAPM_MUX("Input 1A Channel Type", SND_SOC_NOPM, 0, 0,
			&cs53l30_channel_type_control),
	SND_SOC_DAPM_MUX("Input 1B Channel Type", SND_SOC_NOPM, 0, 0,
			&cs53l30_channel_type_control),
	SND_SOC_DAPM_MUX("Input 2A Channel Type", SND_SOC_NOPM, 0, 0,
			&cs53l30_channel_type_control),
	SND_SOC_DAPM_MUX("Input 2B Channel Type", SND_SOC_NOPM, 0, 0,
			&cs53l30_channel_type_control),

	SND_SOC_DAPM_ADC("ADC1A", "Capture", CS53L30_ADC1_CTRL1, 6, 1),
	SND_SOC_DAPM_ADC("ADC1B", "Capture", CS53L30_ADC1_CTRL1, 7, 1),
	SND_SOC_DAPM_ADC("ADC2A", "Capture", CS53L30_ADC2_CTRL1, 6, 1),
	SND_SOC_DAPM_ADC("ADC2B", "Capture", CS53L30_ADC2_CTRL1, 7, 1),

	SND_SOC_DAPM_AIF_OUT("ASP1", "Capture", 0, CS53L30_ASP_CTRL1, 6, 1),
};

static const struct snd_soc_dapm_route cs53l30_intercon[] = {
	/* Mic Bias is for the input pins */
	{ "Mic Bias 1", NULL, "IN1" },
	{ "Mic Bias 2", NULL, "IN2" },
	{ "Mic Bias 3", NULL, "IN3" },
	{ "Mic Bias 4", NULL, "IN4" },

	/* The ADC get their signal from the input pins */
	{ "ADC1A", NULL, "IN1" },
	{ "ADC1B", NULL, "IN2" },
	{ "ADC2A", NULL, "IN3" },
	{ "ADC2B", NULL, "IN4" },

	/* The ADC must always be powered, even for digital input */
	{ "Input 1A Channel Type", NULL, "ADC1A" },
	{ "Input 2A Channel Type", NULL, "ADC2A" },
	{ "Input 2B Channel Type", NULL, "ADC2B" },
	{ "Input 1B Channel Type", NULL, "ADC1B" },

	/* The mic bias are only needed for analog input */
	{ "Input 1A Channel Type", "Analog", "Mic Bias 1" },
	{ "Input 1B Channel Type", "Analog", "Mic Bias 2" },
	{ "Input 2A Channel Type", "Analog", "Mic Bias 3" },
	{ "Input 2B Channel Type", "Analog", "Mic Bias 4" },

	/* The audio interface get its data from the 4 channels */
	{ "ASP1", NULL, "Input 1A Channel Type" },
	{ "ASP1", NULL, "Input 1B Channel Type" },
	{ "ASP1", NULL, "Input 2A Channel Type" },
	{ "ASP1", NULL, "Input 2B Channel Type" },
};

static const struct cs53l30_clock_rate cs53l30_clock_rate_6M[] = {
	{ .lrck =  8000, .asp_rate = 1, },
	{ .lrck = 11025, .asp_rate = 2, },
	{ .lrck = 12000, .asp_rate = 4, },
	{ .lrck = 16000, .asp_rate = 5, },
	{ .lrck = 22050, .asp_rate = 6, },
	{ .lrck = 24000, .asp_rate = 8, },
	{ .lrck = 32000, .asp_rate = 9, },
	{ .lrck = 44100, .asp_rate = 10, },
	{ .lrck = 48000, .asp_rate = 12, },
};

static const struct cs53l30_clock_rate cs53l30_clock_rate_5M[] = {
	{ .lrck =  11025, .asp_rate = 4, },
	{ .lrck =  22050, .asp_rate = 8, },
	{ .lrck =  44100, .asp_rate = 12, },
};

static const struct cs53l30_clock_config cs53l30_clock_config[] = {
	{
		.mclk		= 6000000,
		.mclk_div	= 0,
		.fs_ratio	= 0,
		.rates		= cs53l30_clock_rate_6M,
		.rate_count	= ARRAY_SIZE(cs53l30_clock_rate_6M),
	},
	{
		.mclk		= 12000000,
		.mclk_div	= 1,
		.fs_ratio	= 0,
		.rates		= cs53l30_clock_rate_6M,
		.rate_count	= ARRAY_SIZE(cs53l30_clock_rate_6M),
	},
	{
		.mclk		= 5644800,
		.mclk_div	= 0,
		.fs_ratio	= 1,
		.rates		= cs53l30_clock_rate_5M,
		.rate_count	= ARRAY_SIZE(cs53l30_clock_rate_5M),
	},
	{
		.mclk		= 11289600,
		.mclk_div	= 1,
		.fs_ratio	= 1,
		.rates		= cs53l30_clock_rate_5M,
		.rate_count	= ARRAY_SIZE(cs53l30_clock_rate_5M),
	},
	{
		.mclk		= 6144000,
		.mclk_div	= 0,
		.fs_ratio	= 1,
		.rates		= cs53l30_clock_rate_6M,
		.rate_count	= ARRAY_SIZE(cs53l30_clock_rate_6M),
	},
	{
		.mclk		= 12288000,
		.mclk_div	= 1,
		.fs_ratio	= 1,
		.rates		= cs53l30_clock_rate_6M,
		.rate_count	= ARRAY_SIZE(cs53l30_clock_rate_6M),
	},
	{
		.mclk		= 19200000,
		.mclk_div	= 2,
		.fs_ratio	= 1,
		.rates		= cs53l30_clock_rate_6M,
		.rate_count	= ARRAY_SIZE(cs53l30_clock_rate_6M),
	},
};

static int cs53l30_set_dai_sysclk(struct snd_soc_dai *dai,
				int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct cs53l30 *adc = snd_soc_codec_get_drvdata(codec);
	const struct cs53l30_clock_config *cfg;
	int is_19mhz = (freq == 19200000);
	int err;
	int i;

	for (i = 0; i < ARRAY_SIZE(cs53l30_clock_config); i++) {
		cfg = &cs53l30_clock_config[i];
		if (cfg->mclk == freq)
			break;
	}

	if (i >= ARRAY_SIZE(cs53l30_clock_config))
		return -EINVAL;

	/* Set MCLK_DIV, and clear INT_SCALE */
	err = regmap_update_bits(
		adc->regmap, CS53L30_MCLK,
		CS53L30_MCLK_DIV_MASK | CS53L30_MCLK_INT_SCALE_MASK,
		cfg->mclk_div << CS53L30_MCLK_DIV_SHIFT);
	if (err)
		return err;

	/* Set INTERNAL_FS_RATIO and MCLK_19MHZ_EN */
	err = regmap_update_bits(
		adc->regmap, CS53L30_INTERNAL_RATE_CTRL,
		CS53L30_INTERNAL_RATE_CTRL_FS_RATIO_MASK |
		CS53L30_INTERNAL_RATE_CTRL_MCLK_19MHZ_MASK,
		(cfg->fs_ratio << CS53L30_INTERNAL_RATE_CTRL_FS_RATIO_SHIFT) |
		(is_19mhz << CS53L30_INTERNAL_RATE_CTRL_MCLK_19MHZ_SHIFT));
	if (err)
		return err;

	adc->clock_config = cfg;

	return 0;
}

static int cs53l30_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct cs53l30 *adc = snd_soc_codec_get_drvdata(codec);

	/* Validate the DAI format, we apply it later as the rate is also needed */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
	case SND_SOC_DAIFMT_IB_NF:
		break;
	default:
		dev_err(codec->dev, "Unsupported polarity\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		dev_err(codec->dev, "Unsupported master mode\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_DSP_A:
		break;
	default:
		dev_err(codec->dev, "Unsupported data formating\n");
		return -EINVAL;
	}


	adc->dai_fmt = fmt;

	return 0;
}

static int cs53l30_set_tdm_slot(struct snd_soc_dai *dai,
				unsigned int tx_mask, unsigned int rx_mask,
				int slots, int slot_width)
{
	struct snd_soc_codec *codec = dai->codec;
	u8 tx_enable[6] = {};
	int i, j, chan;
	int err;

	if (rx_mask) {
		dev_err(codec->dev, "ADC can't receive TDM data\n");
		return -EINVAL;
	}

	if (slots > CS53L30_CHANNEL_COUNT) {
		dev_err(codec->dev, "Only up to 4 TDM slots are supported\n");
		return -EINVAL;
	}

	if (slot_width % 8) {
		dev_err(codec->dev,
			"Slot width must be a multiple of 8\n");
		return -EINVAL;
	}

	if (slots * slot_width > 48 * 8) {
		dev_err(codec->dev,
			"Only up to 384 bits per frame are supported: "
			"%d slots of %d bits needs %d bits\n",
			slots, slot_width, slots * slot_width);
		return -EINVAL;
	}

	/* Count the slot width in bytes */
	slot_width /= 8;

	/* Setup the active channels and fill the enable bit mask */
	for (i = chan = 0; i < 32 && chan < slots; i++) {
		if (!(tx_mask & BIT(i)))
			continue;
		/* Fill the TX enable bitmask */
		for (j = 0; j < slot_width; j++) {
			int b = i * slot_width + j;
			tx_enable[b / 8] |= BIT(b % 8);
		}
		/* Enable the channel and set its position */
		err = snd_soc_write(
			codec, CS53L30_ASP_TDM_TX_CTRL(chan), i * slot_width);
		if (err)
			return err;
		chan++;
	}

	/* Disable the channels left */
	for (; chan < CS53L30_CHANNEL_COUNT; chan++) {
		err = snd_soc_write(codec, CS53L30_ASP_TDM_TX_CTRL(chan),
				CS53L30_ASP_TDM_TX_CTRL_STATE_MASK);
		if (err)
			return err;
	}

	/* Write the TX enable array */
	for (i = 0; i < ARRAY_SIZE(tx_enable); i++) {
		err = snd_soc_write(
			codec, CS53L30_ASP_TDM_TX_ENABLE(i), tx_enable[i]);
		if (err)
			return err;
	}

	return 0;
}

static int cs53l30_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params,
			struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct cs53l30 *adc = snd_soc_codec_get_drvdata(codec);
	const struct cs53l30_clock_rate *rate = NULL;
	int channels = params_channels(params);
	int fs = params_rate(params);
	int asp_cfg_ctrl;
	int asp_ctrl1;
	int mclk;
	int err;
	int i;

	if (channels > CS53L30_CHANNEL_COUNT) {
		dev_err(codec->dev, "Unsupported channel count\n");
		return -EINVAL;
	}

	if (!adc->clock_config) {
		dev_err(codec->dev, "MCLK hasn't been setup\n");
		return -EINVAL;
	}

	for (i = 0; i < adc->clock_config->rate_count; i++)
		if (adc->clock_config->rates[i].lrck == fs) {
			rate = &adc->clock_config->rates[i];
			break;
		}

	if (!rate) {
		dev_err(codec->dev, "Sample rate %d Hz is not supported\n",
			fs);
		return -EINVAL;
	}

	/* Set ASP control register */
	asp_cfg_ctrl = rate->asp_rate;
	asp_ctrl1 = snd_soc_read(codec, CS53L30_ASP_CTRL1);
	if (asp_ctrl1 < 0)
		return asp_ctrl1;

	/* Clear the DAI tri-state */
	asp_ctrl1 &= ~CS53L30_ASP_CTRL1_TRISTATE_MASK;

	switch (adc->dai_fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		asp_cfg_ctrl |= CS53L30_ASP_CFG_CTRL_MASTER_MASK;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	switch (adc->dai_fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		asp_ctrl1 |= CS53L30_ASP_CTRL1_TDM_PDN_MASK;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		asp_ctrl1 &= ~CS53L30_ASP_CTRL1_TDM_PDN_MASK;
		/* To start on the first clock we must invert the clock */
		asp_cfg_ctrl |= CS53L30_ASP_CFG_CTRL_SCLK_INV_MASK;
		/* And use the other edge */
		asp_ctrl1 |= CS53L30_ASP_CTRL1_SHIFT_LEFT_MASK;
		break;
	default:
		return -EINVAL;
	}

	switch (adc->dai_fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		/* XOR because this bit might already be set in DSP mode A */
		asp_cfg_ctrl ^= CS53L30_ASP_CFG_CTRL_SCLK_INV_MASK;
		break;
	default:
		return -EINVAL;
	}

	err = snd_soc_write(codec, CS53L30_ASP_CFG_CTRL, asp_cfg_ctrl);
	if (err)
		return err;

	err = snd_soc_write(codec, CS53L30_ASP_CTRL1, asp_ctrl1);
	if (err)
		return err;

	/* Power up the ADC */
	err = snd_soc_write(codec, CS53L30_POWER_CTRL, 0x10);
	if (err)
		return err;

	/* Start the MCLK */
	mclk = snd_soc_read(codec, CS53L30_MCLK);
	if (mclk < 0)
		return mclk;
	mclk &= ~CS53L30_MCLK_DISABLE_MASK;
	err = snd_soc_write(codec, CS53L30_MCLK, mclk);
	if (err)
		return err;

	return 0;
}

static int cs53l30_hw_free(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	int asp_ctrl1;
	int mclk;
	int err;

	mclk = snd_soc_read(codec, CS53L30_MCLK);
	if (mclk < 0)
		return mclk;

	asp_ctrl1 = snd_soc_read(codec, CS53L30_ASP_CTRL1);
	if (asp_ctrl1 < 0)
		return asp_ctrl1;

	/* Stop the internal MCLK */
	mclk |= CS53L30_MCLK_DISABLE_MASK;
	err = snd_soc_write(codec, CS53L30_MCLK, mclk);
	if (err)
		return err;

	/* Power down the ADC */
	err = snd_soc_write(codec, CS53L30_POWER_CTRL, 0x50);
	if (err)
		return err;

	/* Tri-state the DAI */
	asp_ctrl1 |= CS53L30_ASP_CTRL1_TRISTATE_MASK;
	return snd_soc_write(codec, CS53L30_ASP_CTRL1, asp_ctrl1);
}

static int cs53l30_xlate_tdm_slot_mask(unsigned int slots,
				unsigned int *tx_mask, unsigned int *rx_mask)
{
	return 0;
}

static struct snd_soc_dai_ops cs53l30_dai_ops = {
	.set_sysclk		= cs53l30_set_dai_sysclk,
	.set_fmt		= cs53l30_set_dai_fmt,
	.set_tdm_slot		= cs53l30_set_tdm_slot,
	.hw_params		= cs53l30_hw_params,
	.hw_free		= cs53l30_hw_free,
	.xlate_tdm_slot_mask	= cs53l30_xlate_tdm_slot_mask,
};

static struct snd_soc_dai_driver cs53l30_dai = {
	.name = "cs53l30-hifi",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 4,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FORMAT_S8 |
			SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S24_LE,
	},
	.ops = &cs53l30_dai_ops,
};

static int cs53l30_set_bias_level(struct snd_soc_codec *codec,
				  enum snd_soc_bias_level level)
{
	int bias_ctrl = snd_soc_read(codec, CS53L30_MIC_BIAS_CTRL);

	if (bias_ctrl < 0)
		return bias_ctrl;

	bias_ctrl &= 3;

	if (level >= SND_SOC_BIAS_STANDBY)
		bias_ctrl |= 2;

	return snd_soc_write(codec, CS53L30_MIC_BIAS_CTRL, bias_ctrl);
}

static struct snd_soc_codec_driver soc_codec_cs53l30 = {
	.controls = cs53l30_snd_controls,
	.num_controls = ARRAY_SIZE(cs53l30_snd_controls),
	.dapm_widgets = cs53l30_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cs53l30_dapm_widgets),
	.dapm_routes = cs53l30_intercon,
	.num_dapm_routes = ARRAY_SIZE(cs53l30_intercon),
	.set_bias_level = cs53l30_set_bias_level,
};

static int cs53l30_i2c_probe(
	struct i2c_client *client, const struct i2c_device_id *id)
{
	struct cs53l30 *adc;
	u8 dev_id[4];
	int err;

	adc = devm_kzalloc(&client->dev, sizeof(*adc), GFP_KERNEL);
	if (!adc)
		return -ENOMEM;

	i2c_set_clientdata(client, adc);

	adc->dev = &client->dev;
	adc->regmap = devm_regmap_init_i2c(client, &cs53l30_regmap);
	if (IS_ERR(adc->regmap)) {
		dev_err(adc->dev, "Failed to create regmap: %ld\n",
			PTR_ERR(adc->regmap));
		return PTR_ERR(adc->regmap);
	}

	/* Hard reset the chip if possible */
	adc->reset_gpio = devm_gpiod_get_optional(&client->dev, "reset",
						GPIOD_OUT_HIGH);
	if (IS_ERR(adc->reset_gpio)) {
		dev_err(&client->dev, "Failed to get reset GPIO: %ld\n",
			PTR_ERR(adc->reset_gpio));
		return PTR_ERR(adc->reset_gpio);
	} else if (adc->reset_gpio) {
		usleep_range(1, 1000);
		gpiod_set_value(adc->reset_gpio, 0);
	}

	/* TODO: Add power up sequence:
	 * - Assert reset
	 * - Power up VP and VA
	 * - Enable MCLK
	 * - Deassert reset
	 */

	/* Read the device ID */
	err = regmap_bulk_read(adc->regmap, CS53L30_DEVICE_ID_AB,
			dev_id, ARRAY_SIZE(dev_id));
	if (err) {
		dev_err(adc->dev, "Failed to read device ID and revision\n");
		return err;
	}

	dev_info(adc->dev, "Found device %02x%02x%02x, revision %02x\n",
		dev_id[0], dev_id[1], dev_id[2], dev_id[3]);

	/* Tristate the DAI for multicodec configs */
	err = regmap_update_bits(adc->regmap, CS53L30_ASP_CTRL1,
				CS53L30_ASP_CTRL1_TRISTATE_MASK,
				CS53L30_ASP_CTRL1_TRISTATE_MASK);
	if (err) {
		dev_err(adc->dev, "Failed to tri-state the DAI\n");
		return err;
	}

	/* Power down the ADC */
	err = regmap_write(adc->regmap, CS53L30_POWER_CTRL, 0x50);
	if (err) {
		dev_err(adc->dev, "Failed to power down ADC\n");
		return err;
	}

	/* And stop the internal MCLK */
	err = regmap_update_bits(adc->regmap, CS53L30_MCLK,
				CS53L30_MCLK_DISABLE_MASK,
				CS53L30_MCLK_DISABLE_MASK);
	if (err) {
		dev_err(adc->dev, "Failed to stop internal MCLK\n");
		return err;
	}

	/* Register the codec */
	return snd_soc_register_codec(
		adc->dev, &soc_codec_cs53l30, &cs53l30_dai, 1);
}

static int cs53l30_i2c_remove(struct i2c_client *client)
{
	struct cs53l30 *adc = i2c_get_clientdata(client);

	snd_soc_unregister_codec(&client->dev);

	if (adc->reset_gpio)
		gpiod_set_value(adc->reset_gpio, 1);

	return 0;
}

static const struct of_device_id cs53l30_of_match[] = {
	{ .compatible = "cirrus,cs53l30", },
	{},
};
MODULE_DEVICE_TABLE(of, cs53l30_of_match);

static const struct i2c_device_id cs53l30_i2c_id[] = {
	{ "cs53l30", 0x18 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cs53l30_i2c_id);


static struct i2c_driver cs53l30_i2c_driver = {
	.driver = {
		.name = "cs53l30",
		.owner = THIS_MODULE,
		.of_match_table = cs53l30_of_match,
	},
	.id_table = cs53l30_i2c_id,
	.probe = cs53l30_i2c_probe,
	.remove = cs53l30_i2c_remove,
};


module_i2c_driver(cs53l30_i2c_driver);

MODULE_AUTHOR("Alban Bedel <alban.bedel@avionic-design.de>");
MODULE_DESCRIPTION("Cirrus Logic CS53L30 ALSA SoC Codec Driver");
MODULE_LICENSE("GPL");
