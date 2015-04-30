/*
 * Copyright 2015 Alban Bedel <alban.bedel@avionic-design.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "tegra_asoc_utils.h"

struct tobor_adc {
	struct platform_device *codec;
	struct tegra_asoc_utils_data util_data;
	int adc_en_gpio;
};

static int tobor_adc_set_tdm_params(struct snd_soc_dai *codec_dai,
				unsigned int base_channel,
				unsigned int channels,
				unsigned int slot_width,
				unsigned int *ret_mask)
{
	unsigned int tx_mask = 0;
	int err;
	int i;

	for (i = 0; i < channels; i++)
		tx_mask |= BIT(i + base_channel);

	err = snd_soc_dai_set_tdm_slot(codec_dai, tx_mask, 0,
				channels, slot_width);
	if (err < 0)
		return err;

	*ret_mask |= tx_mask;
	return 0;
}

static int tobor_adc_asoc_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *runtime = substream->private_data;
	struct tobor_adc *tobor_adc = snd_soc_card_get_drvdata(runtime->card);
	struct snd_soc_dai *cpu_dai = runtime->cpu_dai;
	struct snd_soc_card *card = runtime->card;
	int channels = params_channels(params);
	int srate = params_rate(params);
	const int mclk = 12288000;
	unsigned int rx_mask = 0;
	int total_channels = 0;
	int channel_base = 0;
	int slot_width;
	int err;
	int i;

	if (substream->stream != SNDRV_PCM_STREAM_CAPTURE) {
		dev_err(card->dev, "Only capture is supported\n");
		return -EINVAL;
	}

	for (i = 0; i < runtime->num_codecs; i++) {
		struct snd_soc_dai *dai = runtime->codec_dais[i];
		total_channels += dai->driver->capture.channels_max;
	}

	if (channels > total_channels) {
		dev_err(card->dev, "Unsupported number of channels\n");
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		slot_width = 8;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		slot_width = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		slot_width = 24;
		break;
	default:
		dev_err(card->dev, "Unsupported sample format\n");
		return -EINVAL;
	}

	err = tegra_asoc_utils_set_rate(&tobor_adc->util_data, srate, mclk);
	if (err < 0) {
		dev_err(card->dev, "Can't configure clocks\n");
		return err;
	}

	for (i = 0; i < runtime->num_codecs; i++) {
		struct snd_soc_dai *dai = runtime->codec_dais[i];
		int c = min(channels - channel_base,
			(int)dai->driver->capture.channels_max);

		err = snd_soc_dai_set_fmt(dai, SND_SOC_DAIFMT_DSP_A |
					SND_SOC_DAIFMT_NB_NF |
					SND_SOC_DAIFMT_CBS_CFS);
		if (err < 0) {
			dev_err(card->dev,
				"Can't set DAI format of codec %d\n", i);
			return err;
		}

		err = snd_soc_dai_set_sysclk(dai, 0, mclk, SND_SOC_CLOCK_IN);
		if (err < 0) {
			dev_err(card->dev,
				"Can't set system clock of codec %d\n", i);
			return err;
		}

		err = tobor_adc_set_tdm_params(dai, channel_base, c,
					slot_width, &rx_mask);
		if (err < 0) {
			dev_err(card->dev,
				"Failed to setup TDM slots of codec %d\n", i);
			return err;
		}

		channel_base += c;
	}

	err = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_DSP_A |
				SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (err < 0) {
		dev_err(card->dev, "Can't set codec_dai format");
		return err;
	}

	err = snd_soc_dai_set_tdm_slot(cpu_dai, 0, rx_mask,
				channels, slot_width);
	if (err < 0) {
		dev_err(card->dev, "Failed to set CPU TDM slots\n");
		return err;
	}

	return 0;
}

static struct snd_soc_ops tobor_adc_asoc_ops = {
	.hw_params = tobor_adc_asoc_hw_params,
};

static struct snd_soc_dai_link tobor_adc_dai = {
	.name = "Tobor ADC",
	.stream_name = "Tobor ADC PCM",
	.ops = &tobor_adc_asoc_ops,
	.dai_fmt = SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBS_CFS,
};

static const struct snd_soc_dapm_widget tobor_adc_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Mic 1", NULL),
	SND_SOC_DAPM_MIC("Mic 2", NULL),
	SND_SOC_DAPM_MIC("Mic 3", NULL),
	SND_SOC_DAPM_MIC("Mic 4", NULL),
	SND_SOC_DAPM_MIC("Mic 5", NULL),
	SND_SOC_DAPM_MIC("Mic 6", NULL),
	SND_SOC_DAPM_LINE("Loopback", NULL),
};

static struct snd_soc_codec_conf snd_soc_tobor_adc_codec_conf[] = {
	{
		.name_prefix = "A",
	},
	{
		.name_prefix = "B",
	},
};

static struct snd_soc_card snd_soc_tobor_adc = {
	.name = "tegra-tobor_adc",
	.owner = THIS_MODULE,
	.codec_conf = snd_soc_tobor_adc_codec_conf,
	.num_configs = ARRAY_SIZE(snd_soc_tobor_adc_codec_conf),
	.dai_link = &tobor_adc_dai,
	.num_links = 1,
	.dapm_widgets = tobor_adc_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tobor_adc_dapm_widgets),
	.fully_routed = true,
};

static int tobor_adc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct snd_soc_card *card = &snd_soc_tobor_adc;
	struct tobor_adc *machine;
	int i, err;

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "device tree init required\n");
		return -EINVAL;
	}

	machine = devm_kzalloc(&pdev->dev, sizeof(struct tobor_adc),
			GFP_KERNEL);
	if (!machine) {
		dev_err(&pdev->dev, "Can't allocate tobor_adc struct\n");
		return -ENOMEM;
	}

	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, machine);


	machine->adc_en_gpio = of_get_named_gpio(np, "adc-enable-gpio", 0);
	if (!gpio_is_valid(machine->adc_en_gpio)) {
		dev_err(&pdev->dev, "Failed to get ADC enable GPIO\n");
		return -EINVAL;
	}

	err = devm_gpio_request(&pdev->dev, machine->adc_en_gpio,
				"adc-enable");
	if (err) {
		if (err != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"Failed to request ADC enable GPIO\n");
		return err;
	}

	err = gpio_direction_output(machine->adc_en_gpio, 1);
	if (err) {
		dev_err(&pdev->dev,
			"Failed to set ADC enable GPIO as output\n");
			return err;
	}

	err = snd_soc_of_parse_card_name(card, "nvidia,model");
	if (err)
		return err;

	err = snd_soc_of_parse_audio_routing(card, "nvidia,audio-routing");
	if (err)
		return err;

	err = snd_soc_of_get_dai_link_codecs(&pdev->dev, pdev->dev.of_node,
					&tobor_adc_dai);
	if (err) {
		dev_err(&pdev->dev, "Failed to parse DAI link to codecs\n");
		return err;
	}

	tobor_adc_dai.cpu_of_node = of_parse_phandle(
		pdev->dev.of_node, "nvidia,i2s-controller", 0);
	if (!tobor_adc_dai.cpu_of_node) {
		dev_err(&pdev->dev,
			"Property 'nvidia,i2s-controller' missing or invalid\n");
		return -EINVAL;
	}

	tobor_adc_dai.platform_of_node = tobor_adc_dai.cpu_of_node;

	/* Add the codec prefixes */
	for (i = 0; i < tobor_adc_dai.num_codecs &&
			i < ARRAY_SIZE(snd_soc_tobor_adc_codec_conf);
			i++)
		snd_soc_tobor_adc.codec_conf[i].of_node =
			tobor_adc_dai.codecs[i].of_node;

	err = tegra_asoc_utils_init(&machine->util_data, &pdev->dev, card);
	if (err)
		return err;

	err = snd_soc_register_card(card);
	if (err) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
			err);
		goto asoc_utils_fini;
	}

	err = tegra_asoc_utils_set_parent(&machine->util_data, 1);
	if (err) {
		dev_err(&pdev->dev, "tegra_asoc_utils_set_parent failed (%d)\n",
			err);
		snd_soc_unregister_card(card);
		goto asoc_utils_fini;
	}

	return 0;

asoc_utils_fini:
	tegra_asoc_utils_fini(&machine->util_data);
	return err;
}

static int tobor_adc_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct tobor_adc *machine = snd_soc_card_get_drvdata(card);

	snd_soc_unregister_card(card);
	tegra_asoc_utils_fini(&machine->util_data);

	return 0;
}

static const struct of_device_id tobor_adc_of_match[] = {
	{ .compatible = "ad,tobor-adc", },
	{},
};

static struct platform_driver tobor_adc_driver = {
	.driver = {
		.name = "tegra-tobor_adc",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = tobor_adc_of_match,
	},
	.probe = tobor_adc_probe,
	.remove = tobor_adc_remove,
};
module_platform_driver(tobor_adc_driver);

MODULE_AUTHOR("Alban Bedel <alban.bedel@avionic-design.de>");
MODULE_DESCRIPTION("Tobor ADC ASoC driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, tobor_adc_of_match);
