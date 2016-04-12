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

#define DAIFMT SND_SOC_DAIFMT_I2S

struct uh2c_adc {
	struct platform_device *codec;
	struct tegra_asoc_utils_data util_data;
};

static int uh2c_adc_asoc_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *runtime = substream->private_data;
	struct snd_soc_dai *codec_dai = runtime->codec_dai;
	struct snd_soc_dai *cpu_dai = runtime->cpu_dai;
	struct snd_soc_card *card = runtime->card;
	int channels = params_channels(params);
	int err;

	if (substream->stream != SNDRV_PCM_STREAM_CAPTURE) {
		dev_err(card->dev, "Only capture is supported\n");
		return -EINVAL;
	}

	err = snd_soc_dai_set_fmt(codec_dai, DAIFMT |
				SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_CBM_CFM);
	if (err < 0) {
		dev_err(card->dev, "Can't set DAI format of codec\n");
		return err;
	}

	err = snd_soc_dai_set_fmt(cpu_dai, DAIFMT |
				SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_CBM_CFM);
	if (err < 0) {
		dev_err(card->dev, "Can't set codec_dai format");
		return err;
	}

	if (DAIFMT == SND_SOC_DAIFMT_DSP_A || DAIFMT == SND_SOC_DAIFMT_DSP_B) {
		err = snd_soc_dai_set_tdm_slot(cpu_dai, 0, (1 << channels) - 1, 8, 32);
		if (err < 0) {
			dev_err(card->dev, "Failed to set CPU TDM slots\n");
			return err;
		}
	}

	return 0;
}

static struct snd_soc_ops uh2c_adc_asoc_ops = {
	.hw_params = uh2c_adc_asoc_hw_params,
};

static struct snd_soc_dai_link uh2c_adc_dai = {
	.name = "UH2C ADC",
	.stream_name = "UH2C ADC PCM",
	.ops = &uh2c_adc_asoc_ops,
	.dai_fmt = DAIFMT |
		SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBM_CFM,
};

static struct snd_soc_card snd_soc_uh2c_adc = {
	.name = "tegra-uh2c-adc",
	.owner = THIS_MODULE,
	.dai_link = &uh2c_adc_dai,
	.num_links = 1,
	.fully_routed = true,
};

static int uh2c_adc_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_uh2c_adc;
	struct uh2c_adc *machine;
	int err;

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "device tree init required\n");
		return -EINVAL;
	}

	machine = devm_kzalloc(&pdev->dev, sizeof(struct uh2c_adc),
			GFP_KERNEL);
	if (!machine) {
		dev_err(&pdev->dev, "Can't allocate uh2c_adc struct\n");
		return -ENOMEM;
	}

	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, machine);

	err = snd_soc_of_parse_card_name(card, "nvidia,model");
	if (err)
		return err;

	err = snd_soc_of_get_dai_link_codecs(&pdev->dev, pdev->dev.of_node,
					&uh2c_adc_dai);
	if (err) {
		dev_err(&pdev->dev, "Failed to parse DAI link to codecs\n");
		return err;
	}

	uh2c_adc_dai.cpu_of_node = of_parse_phandle(
		pdev->dev.of_node, "nvidia,i2s-controller", 0);
	if (!uh2c_adc_dai.cpu_of_node) {
		dev_err(&pdev->dev,
			"Property 'nvidia,i2s-controller' missing or invalid\n");
		return -EINVAL;
	}

	uh2c_adc_dai.platform_of_node = uh2c_adc_dai.cpu_of_node;

	err = tegra_asoc_utils_init(&machine->util_data, &pdev->dev, card);
	if (err)
		return err;

	err = snd_soc_register_card(card);
	if (err) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
			err);
		goto asoc_utils_fini;
	}

	err = tegra_asoc_utils_set_parent(&machine->util_data, 0);
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

static int uh2c_adc_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct uh2c_adc *machine = snd_soc_card_get_drvdata(card);

	snd_soc_unregister_card(card);
	tegra_asoc_utils_fini(&machine->util_data);

	return 0;
}

static const struct of_device_id uh2c_adc_of_match[] = {
	{ .compatible = "ad,uh2c-adc", },
	{},
};

static struct platform_driver uh2c_adc_driver = {
	.driver = {
		.name = "tegra-uh2c-adc",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = uh2c_adc_of_match,
	},
	.probe = uh2c_adc_probe,
	.remove = uh2c_adc_remove,
};
module_platform_driver(uh2c_adc_driver);

MODULE_AUTHOR("Alban Bedel <alban.bedel@avionic-design.de>");
MODULE_DESCRIPTION("UH2C ADC ASoC driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, uh2c_adc_of_match);
