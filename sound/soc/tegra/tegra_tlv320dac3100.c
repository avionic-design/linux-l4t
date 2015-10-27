/*
 * tegra_tlv320dac3100.c -- Machine ASoC driver for TI TLV320DAC3100 driver
 *
 * Copyright (c) 2015 Avionic Design GmbH
 *
 * Authors: Julian Scheel <julian@jusst.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "tegra_asoc_utils.h"

struct tegra_tlv320dac3100 {
	struct platform_device *codec;
	struct tegra_asoc_utils_data util_data;
};

static int tegra_tlv320dac3100_asoc_hw_params(struct snd_pcm_substream *substream,
					       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *runtime = substream->private_data;
	struct snd_soc_dai *codec_dai = runtime->codec_dai;
	struct snd_soc_codec *codec = codec_dai->codec;
	struct snd_soc_dai *cpu_dai = runtime->cpu_dai;
	struct snd_soc_card *card = codec->card;

	struct tegra_tlv320dac3100 *tlv320dac3100 = snd_soc_card_get_drvdata(card);
	int srate;
	int mclk;
	int err;

	srate = params_rate(params);

	/* We can't just use any rate otherwise the MCLK might not get
	 * properly rounded, leading to wrong playback frequency.
	 * This is because the parent of the MCLK is set according to
	 * the samplerate, so we also choose an MCLK that is a multiple
	 * of the rate that will be set for the MCLK parent.
	 */
	switch (srate) {
	case 11025:
	case 22050:
	case 44100:
	case 88200:
		mclk = 11289600;
		break;
	case 8000:
	case 16000:
	case 32000:
	case 48000:
	case 64000:
	case 96000:
	case 192000:
		mclk = 12288000;
		break;
	default:
		return -EINVAL;
	}

	err = tegra_asoc_utils_set_rate(&tlv320dac3100->util_data, srate, mclk);
	if (err < 0) {
		dev_err(card->dev, "Can't configure clocks\n");
		return err;
	}

	err = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (err < 0) {
		dev_err(card->dev, "Can't set codec_dai format");
		return err;
	}

	err = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (err < 0) {
		dev_err(card->dev, "Can't set codec_dai format");
		return err;
	}

	err = snd_soc_dai_set_sysclk(codec_dai, 0, mclk, SND_SOC_CLOCK_IN);
	if (err < 0) {
		dev_err(card->dev, "Can't set codec_dai system clock\n");
		return err;
	}

	return 0;
}

static struct snd_soc_ops tegra_tlv320dac3100_asoc_ops = {
	.hw_params = tegra_tlv320dac3100_asoc_hw_params,
};

static int tegra_tlv320dac3100_asoc_init(struct snd_soc_pcm_runtime *runtime)
{
	/* TODO: Make use of reset gpio once hardware with proper reset gpio
	 * is available */

	return 0;
}

static struct snd_soc_dai_link tegra_tlv320dac3100_dai = {
	.name = "TLV320DAC3100",
	.stream_name = "TLV320DAC3100 PCM",
	.codec_dai_name = "dac3100-hifi",
	.init = tegra_tlv320dac3100_asoc_init,
	.ops = &tegra_tlv320dac3100_asoc_ops,
	.dai_fmt = SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
};

static const struct snd_soc_dapm_widget tegra_tlv320dac3100_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_REGULATOR_SUPPLY("Amp", 0, 0),
};

static struct snd_soc_card snd_soc_tegra_tlv320dac3100 = {
	.name = "tegra-tlv320dac3100",
	.owner = THIS_MODULE,
	.dai_link = &tegra_tlv320dac3100_dai,
	.num_links = 1,
	.dapm_widgets = tegra_tlv320dac3100_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tegra_tlv320dac3100_dapm_widgets),
	.fully_routed = true,
};

static int tegra_tlv320dac3100_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct snd_soc_card *card = &snd_soc_tegra_tlv320dac3100;
	struct tegra_tlv320dac3100 *machine;
	int ret;

	machine = devm_kzalloc(&pdev->dev, sizeof(struct tegra_tlv320dac3100),
			       GFP_KERNEL);
	if (!machine) {
		dev_err(&pdev->dev, "Can't allocate tegra_tlv320dac3100 struct\n");
		ret = -ENOMEM;
		goto err;
	}

	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, machine);

	if (!(pdev->dev.of_node)) {
		dev_err(&pdev->dev, "device tree init required\n");
		ret = -EINVAL;
		goto err;
	}

	ret = snd_soc_of_parse_card_name(card, "nvidia,model");
	if (ret)
		goto err;

	ret = snd_soc_of_parse_audio_routing(card, "nvidia,audio-routing");
	if (ret)
		goto err;

	tegra_tlv320dac3100_dai.codec_of_node = of_parse_phandle(np,
						       "nvidia,audio-codec", 0);
	if (!tegra_tlv320dac3100_dai.codec_of_node) {
		dev_err(&pdev->dev,
			"Property 'nvidia,audio-codec' missing or invalid\n");
		ret = -EINVAL;
		goto err;
	}

	tegra_tlv320dac3100_dai.cpu_of_node = of_parse_phandle(
			pdev->dev.of_node, "nvidia,i2s-controller", 0);
	if (!tegra_tlv320dac3100_dai.cpu_of_node) {
		dev_err(&pdev->dev,
		"Property 'nvidia,i2s-controller' missing or invalid\n");
		ret = -EINVAL;
		goto err;
	}

	tegra_tlv320dac3100_dai.platform_of_node =
		tegra_tlv320dac3100_dai.cpu_of_node;

	ret = tegra_asoc_utils_init(&machine->util_data, &pdev->dev, card);
	if (ret)
		goto err;

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
			ret);
		goto asoc_utils_fini;
	}

	ret = tegra_asoc_utils_set_parent(&machine->util_data, 1);
	if (ret) {
		dev_err(&pdev->dev, "tegra_asoc_utils_set_parent failed (%d)\n",
			ret);
		snd_soc_unregister_card(card);
		goto asoc_utils_fini;
	}

	return 0;

asoc_utils_fini:
	tegra_asoc_utils_fini(&machine->util_data);
err:
	return ret;
}

static int tegra_tlv320dac3100_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct tegra_tlv320dac3100 *machine = snd_soc_card_get_drvdata(card);

	snd_soc_unregister_card(card);
	tegra_asoc_utils_fini(&machine->util_data);

	return 0;
}

static const struct of_device_id tegra_tlv320dac3100_of_match[] = {
	{ .compatible = "nvidia,tegra-audio-tlv320dac3100", },
	{},
};

static struct platform_driver tegra_tlv320dac3100_driver = {
	.driver = {
		.name = "tegra-tlv320dac3100",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = tegra_tlv320dac3100_of_match,
	},
	.probe = tegra_tlv320dac3100_probe,
	.remove = tegra_tlv320dac3100_remove,
};
module_platform_driver(tegra_tlv320dac3100_driver);

MODULE_AUTHOR("Julian Scheel <julian@jusst.de>");
MODULE_DESCRIPTION("Tegra+TLV320DAC3100 machine ASoC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, tegra_tlv320dac3100_of_match);
