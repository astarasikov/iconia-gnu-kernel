/*
 * seaboard.c - Seaboard machine ASoC driver
 *
 * Author: Stephen Warren <swarren@nvidia.com>
 * Copyright (C) 2010-2011 - NVIDIA, Inc.
 *
 * Based on code copyright/by:
 *
 * (c) 2009, 2010 Nvidia Graphics Pvt. Ltd.
 *
 * Copyright 2007 Wolfson Microelectronics PLC.
 * Author: Graeme Gregory
 *         graeme.gregory@wolfsonmicro.com or linux@wolfsonmicro.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <asm/mach-types.h>

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/gpio.h>

#include <mach/seaboard_audio.h>

#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "../codecs/wm8903.h"

#include "tegra_das.h"
#include "tegra_i2s.h"
#include "tegra_pcm.h"
#include "tegra_asoc_utils.h"

#define DRV_NAME "tegra-snd-seaboard"

#define GPIO_SPKR_EN	BIT(0)
#define GPIO_HP_MUTE	BIT(1)

struct tegra_seaboard {
	struct tegra_asoc_utils_data util_data;
	struct seaboard_audio_platform_data *pdata;
	int gpio_requested;
	struct regulator *vdd_dmic;
	bool vdd_dmic_enabled;
};

static int is_wm8903_codec(void)
{
	return	machine_is_seaboard() ||
		machine_is_kaen()     ||
		machine_is_aebl()     ||
		machine_is_asymptote();
}

static int is_max98095_codec(void)
{
	return machine_is_arthur();
}

static int seaboard_get_mclk(int srate)
{
	int mclk;
	switch (srate) {
	case 64000:
	case 88200:
	case 96000:
		mclk = 128 * srate;
	default:
		mclk = 256 * srate;
	}
	if (is_wm8903_codec())
		while (mclk < 6000000)
			mclk *= 2;
	return mclk;
}

static int seaboard_set_rate(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     int *mclk_out)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_card *card = codec->card;
	struct tegra_seaboard *seaboard = snd_soc_card_get_drvdata(card);
	int srate, mclk, mclk_change;
	int err;

	srate = params_rate(params);
	mclk = seaboard_get_mclk(srate);
	/* Set board PLLs */
	err = tegra_asoc_utils_set_rate(&seaboard->util_data, srate, mclk,
					&mclk_change);
	if (err) {
		dev_err(card->dev, "Can't configure clocks\n");
		return err;
	}
	*mclk_out = mclk;
	return 0;
}

static int seaboard_asoc_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	const int dai_format = (SND_SOC_DAIFMT_I2S   |
				SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_CBS_CFS);
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_card *card = codec->card;
	int err;
	int mclk;

	err = snd_soc_dai_set_fmt(codec_dai, dai_format);
	if (err < 0) {
		dev_err(card->dev, "codec_dai fmt not set\n");
		return err;
	}

	err = snd_soc_dai_set_fmt(cpu_dai, dai_format);
	if (err < 0) {
		dev_err(card->dev, "cpu_dai fmt not set\n");
		return err;
	}

	err = seaboard_set_rate(substream, params, &mclk);
	if (err < 0) {
		dev_err(card->dev, "Can't configure sound clocks\n");
		return err;
	}

	err = snd_soc_dai_set_sysclk(codec_dai, 0, mclk, SND_SOC_CLOCK_IN);
	if (err)
		dev_err(card->dev, "codec_dai clock not set\n");
	return err;
}

static struct snd_soc_ops seaboard_asoc_ops = {
	.hw_params = seaboard_asoc_hw_params,
};

static int seaboard_spdif_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	int mclk;
	return seaboard_set_rate(substream, params, &mclk);
}

static struct snd_soc_ops seaboard_spdif_ops = {
	.hw_params = seaboard_spdif_hw_params,
};

static int seaboard_event_int_spk(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *k, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct snd_soc_card *card = codec->card;
	struct tegra_seaboard *seaboard = snd_soc_card_get_drvdata(card);
	struct seaboard_audio_platform_data *pdata = seaboard->pdata;

	gpio_set_value_cansleep(pdata->gpio_spkr_en,
				SND_SOC_DAPM_EVENT_ON(event));

	return 0;
}

static int seaboard_event_hp(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct snd_soc_card *card = codec->card;
	struct tegra_seaboard *seaboard = snd_soc_card_get_drvdata(card);
	struct seaboard_audio_platform_data *pdata = seaboard->pdata;

	if (seaboard->gpio_requested & GPIO_HP_MUTE)
		gpio_set_value_cansleep(pdata->gpio_hp_mute,
					!SND_SOC_DAPM_EVENT_ON(event));

	return 0;
}

static int seaboard_event_dmic(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct snd_soc_card *card = codec->card;
	struct tegra_seaboard *seaboard = snd_soc_card_get_drvdata(card);
	bool new_enabled;
	int ret;

	if (IS_ERR(seaboard->vdd_dmic))
		return 0;

	new_enabled = !!SND_SOC_DAPM_EVENT_ON(event);
	if (seaboard->vdd_dmic_enabled == new_enabled)
		return 0;

	if (new_enabled)
		ret = regulator_enable(seaboard->vdd_dmic);
	else
		ret = regulator_disable(seaboard->vdd_dmic);

	if (!ret)
		seaboard->vdd_dmic_enabled = new_enabled;

	return ret;
}

static const struct snd_soc_dapm_widget seaboard_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Int Spk", seaboard_event_int_spk),
	SND_SOC_DAPM_HP("Headphone Jack", seaboard_event_hp),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_MIC("Digital Mic", seaboard_event_dmic),
};

static const struct snd_soc_dapm_route seaboard_audio_map[] = {
	{"Headphone Jack", NULL, "HPOUTR"},
	{"Headphone Jack", NULL, "HPOUTL"},
	{"Int Spk", NULL, "ROP"},
	{"Int Spk", NULL, "RON"},
	{"Int Spk", NULL, "LOP"},
	{"Int Spk", NULL, "LON"},
	{"Mic Bias", NULL, "Mic Jack"},
	{"IN1R", NULL, "Mic Bias"},
	{"DMICDAT", NULL, "Digital Mic"},
};

static const struct snd_soc_dapm_route kaen_audio_map[] = {
	{"Headphone Jack", NULL, "HPOUTR"},
	{"Headphone Jack", NULL, "HPOUTL"},
	{"Int Spk", NULL, "ROP"},
	{"Int Spk", NULL, "RON"},
	{"Int Spk", NULL, "LOP"},
	{"Int Spk", NULL, "LON"},
	{"Mic Bias", NULL, "Mic Jack"},
	{"IN2R", NULL, "Mic Bias"},
	{"DMICDAT", NULL, "Digital Mic"},
};

static const struct snd_soc_dapm_route aebl_audio_map[] = {
	{"Headphone Jack", NULL, "HPOUTR"},
	{"Headphone Jack", NULL, "HPOUTL"},
	{"Int Spk", NULL, "LINEOUTR"},
	{"Int Spk", NULL, "LINEOUTL"},
	{"Mic Bias", NULL, "Mic Jack"},
	{"IN1R", NULL, "Mic Bias"},
	{"DMICDAT", NULL, "Digital Mic"},
};

static const struct snd_kcontrol_new seaboard_controls[] = {
	SOC_DAPM_PIN_SWITCH("Int Spk"),
};

static int seaboard_request_gpio_hp_mute(const struct snd_soc_card *card,
					 struct tegra_seaboard *seaboard)
{
	int ret = 0;
	const struct seaboard_audio_platform_data *pdata = seaboard->pdata;
	if (pdata->gpio_hp_mute != -1) {
		ret = gpio_request(pdata->gpio_hp_mute, "hp_mute");
		if (ret) {
			dev_err(card->dev, "cannot get hp_mute gpio\n");
			return ret;
		}
		seaboard->gpio_requested |= GPIO_HP_MUTE;
		gpio_direction_output(pdata->gpio_hp_mute, 1);
	}
	return ret;
}

static int seaboard_request_gpio_spkr_en(struct snd_soc_card *card)
{
	struct tegra_seaboard *seaboard = snd_soc_card_get_drvdata(card);
	struct seaboard_audio_platform_data *pdata = seaboard->pdata;
	int ret;

	if (pdata->gpio_spkr_en != -1) {
		ret = gpio_request(pdata->gpio_spkr_en, "spkr_en");
		if (ret) {
			dev_err(card->dev, "SPKR_EN gpio (%d) not found.\n",
				pdata->gpio_spkr_en);
			return ret;
		}
		seaboard->gpio_requested |= GPIO_SPKR_EN;
		gpio_direction_output(pdata->gpio_spkr_en, 0);
	}
	return 0;
}

static struct snd_soc_jack hp;
static struct snd_soc_jack mic;

static struct snd_soc_jack_pin hp_pins[] = {
	{
		.pin = "Headphone Jack",
		.mask = SND_JACK_HEADPHONE,
	},
};

static struct snd_soc_jack_gpio hp_gpios[] = {
	{
		.name = "Headphone Detect",
		.report = SND_JACK_HEADPHONE,
		.debounce_time = 150,
		.invert = 1,
	}
};

static struct snd_soc_jack_pin mic_pins[] = {
	{
		.pin = "Mic Jack",
		.mask = SND_JACK_MICROPHONE,
	},
};

static int seaboard_init_jacks(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm  = &codec->dapm;
	struct snd_soc_card *card  = codec->card;
	struct tegra_seaboard *board = snd_soc_card_get_drvdata(card);
	struct seaboard_audio_platform_data *pdata = board->pdata;
	int ret = 0;

	hp_gpios[0].gpio = pdata->gpio_hp_det;

	snd_soc_jack_new(codec, "Headphone Jack", SND_JACK_HEADPHONE,  &hp);
	snd_soc_jack_new(codec, "Mic Jack",	  SND_JACK_MICROPHONE, &mic);
	snd_soc_jack_add_pins(&hp,  ARRAY_SIZE(hp_pins),  hp_pins);
	snd_soc_jack_add_pins(&mic, ARRAY_SIZE(mic_pins), mic_pins);

	ret = snd_soc_jack_add_gpios(&hp, ARRAY_SIZE(hp_gpios), hp_gpios);
	if (ret)
		return ret;

	if (is_max98095_codec()) {
		snd_soc_dapm_force_enable_pin(dapm, "MICBIAS1");
		snd_soc_dapm_force_enable_pin(dapm, "MICBIAS2");
	} else {
		BUG_ON(!is_wm8903_codec());
		wm8903_mic_detect(codec, &mic, SND_JACK_MICROPHONE, 0);
		snd_soc_dapm_force_enable_pin(dapm, "Mic Bias");
	}
	return 0;
}

static int wm8903_board_asoc_init(struct snd_soc_card *card,
				  struct snd_soc_dapm_context *dapm)
{
	int ret;

	if (machine_is_seaboard() || machine_is_asymptote()) {
		ret = snd_soc_dapm_add_routes(dapm, seaboard_audio_map,
					      ARRAY_SIZE(seaboard_audio_map));
		if (ret)
			return ret;
		snd_soc_dapm_nc_pin(dapm, "IN2R");
		snd_soc_dapm_nc_pin(dapm, "LINEOUTR");
		snd_soc_dapm_nc_pin(dapm, "LINEOUTL");
	} else if (machine_is_kaen()) {
		ret = snd_soc_dapm_add_routes(dapm, kaen_audio_map,
					      ARRAY_SIZE(kaen_audio_map));
		if (ret)
			return ret;
		snd_soc_dapm_nc_pin(dapm, "IN1R");
		snd_soc_dapm_nc_pin(dapm, "LINEOUTR");
		snd_soc_dapm_nc_pin(dapm, "LINEOUTL");
	} else {
		BUG_ON(!machine_is_aebl());
		ret = snd_soc_dapm_add_routes(dapm, aebl_audio_map,
					      ARRAY_SIZE(aebl_audio_map));
		if (ret)
			return ret;
		snd_soc_dapm_nc_pin(dapm, "IN2R");
		snd_soc_dapm_nc_pin(dapm, "LON");
		snd_soc_dapm_nc_pin(dapm, "RON");
		snd_soc_dapm_nc_pin(dapm, "ROP");
		snd_soc_dapm_nc_pin(dapm, "LOP");
	}
	snd_soc_dapm_nc_pin(dapm, "IN1L");
	snd_soc_dapm_nc_pin(dapm, "IN2L");
	snd_soc_dapm_nc_pin(dapm, "IN3R");
	snd_soc_dapm_nc_pin(dapm, "IN3L");
	return 0;
}

static int max98095_board_asoc_init(struct snd_soc_dapm_context *dapm)
{
	/* Arthur schematic shows unconnected pins. */
	snd_soc_dapm_nc_pin(dapm, "MIC2");
	snd_soc_dapm_nc_pin(dapm, "INA1");
	snd_soc_dapm_nc_pin(dapm, "INA2");
	snd_soc_dapm_nc_pin(dapm, "INB1");
	snd_soc_dapm_nc_pin(dapm, "INB2");
	snd_soc_dapm_nc_pin(dapm, "OUT1");
	snd_soc_dapm_nc_pin(dapm, "OUT2");
	snd_soc_dapm_nc_pin(dapm, "OUT3");
	snd_soc_dapm_nc_pin(dapm, "OUT4");
	snd_soc_dapm_nc_pin(dapm, "RCV");
	return 0;
}

static int seaboard_asoc_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	struct snd_soc_card *card = codec->card;
	struct tegra_seaboard *seaboard = snd_soc_card_get_drvdata(card);
	int ret;

	ret = seaboard_request_gpio_spkr_en(card);
	if (ret < 0)
		return ret;

	ret = seaboard_request_gpio_hp_mute(card, seaboard);
	if (ret < 0)
		return ret;

	ret = snd_soc_add_controls(codec, seaboard_controls,
				   ARRAY_SIZE(seaboard_controls));
	if (ret < 0)
		return ret;

	ret = snd_soc_dapm_new_controls(dapm, seaboard_dapm_widgets,
					ARRAY_SIZE(seaboard_dapm_widgets));
	if (ret < 0)
		return ret;

	if (is_wm8903_codec())
		ret = wm8903_board_asoc_init(card, dapm);
	else {
		BUG_ON(!is_max98095_codec());
		max98095_board_asoc_init(dapm);
	}

	ret = seaboard_init_jacks(codec);
	if (ret)
		return ret;

	snd_soc_dapm_sync(dapm);
	return ret;
}

static struct snd_soc_dai_link max98095_links[] = {
	{
		.name		= "MAX98095",
		.stream_name	= "MAX98095 PCM",
		.codec_name	= "max98095.0-0010",
		.platform_name	= "tegra-pcm-audio",
		.cpu_dai_name	= "tegra-i2s.0",
		.codec_dai_name = "HiFi",
		.init		= seaboard_asoc_init,
		.ops		= &seaboard_asoc_ops,
	},
};

static struct snd_soc_card snd_soc_max89095 = {
	.name      = "tegra-arthur",
	.dai_link  = max98095_links,
	.num_links = ARRAY_SIZE(max98095_links),
};

static struct snd_soc_dai_link wm8903_links[] = {
	{
		.name = "WM8903",
		.stream_name = "WM8903 PCM",
		.codec_name = "wm8903.0-001a",
		.platform_name = "tegra-pcm-audio",
		.cpu_dai_name = "tegra-i2s.0",
		.codec_dai_name = "wm8903-hifi",
		.init = seaboard_asoc_init,
		.ops = &seaboard_asoc_ops,
	},
	{
		.name = "SPDIF",
		.stream_name = "spdif",
		.codec_name = "spdif-dit",
		.platform_name = "tegra-pcm-audio",
		.cpu_dai_name = "tegra-spdif",
		.codec_dai_name = "dit-hifi",
		.ops = &seaboard_spdif_ops,
	},
};

static struct snd_soc_card snd_soc_wm8903 = {
	.name = "tegra-seaboard",
	.dai_link = wm8903_links,
	.num_links = ARRAY_SIZE(wm8903_links),
};

static __devinit int tegra_snd_seaboard_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct tegra_seaboard *seaboard;
	struct seaboard_audio_platform_data *pdata;
	int ret;

	if (is_wm8903_codec())
		card = &snd_soc_wm8903;
	else if (is_max98095_codec())
		card = &snd_soc_max89095;
	else {
		dev_err(&pdev->dev, "Not running on a supported board.\n");
		return -ENODEV;
	}

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "No platform data supplied.\n");
		return -EINVAL;
	}

	seaboard = kzalloc(sizeof(struct tegra_seaboard), GFP_KERNEL);
	if (!seaboard) {
		dev_err(&pdev->dev, "Can't allocate tegra_seaboard.\n");
		return -ENOMEM;
	}

	seaboard->pdata = pdata;

	ret = tegra_asoc_utils_init(&seaboard->util_data, &pdev->dev);
	if (ret)
		goto err_free_seaboard;

	seaboard->vdd_dmic = regulator_get(&pdev->dev, "vdd_dmic");
	if (IS_ERR(seaboard->vdd_dmic)) {
		dev_info(&pdev->dev, "regulator_get() returned error %ld\n",
			 PTR_ERR(seaboard->vdd_dmic));
		ret = PTR_ERR(seaboard->vdd_dmic);
		goto err_fini_utils;
	}

	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, seaboard);

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (result: %d).\n",
			ret);
		goto err_clear_drvdata;
	}

	return 0;

err_clear_drvdata:
	snd_soc_card_set_drvdata(card, NULL);
	platform_set_drvdata(pdev, NULL);
	card->dev = NULL;
	regulator_put(seaboard->vdd_dmic);
err_fini_utils:
	tegra_asoc_utils_fini(&seaboard->util_data);
err_free_seaboard:
	kfree(seaboard);
	return ret;
}

static int __devexit tegra_snd_seaboard_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct tegra_seaboard *seaboard = snd_soc_card_get_drvdata(card);
	struct seaboard_audio_platform_data *pdata = seaboard->pdata;

	snd_soc_unregister_card(card);

	snd_soc_card_set_drvdata(card, NULL);
	platform_set_drvdata(pdev, NULL);
	card->dev = NULL;

	regulator_put(seaboard->vdd_dmic);

	tegra_asoc_utils_fini(&seaboard->util_data);

	if (seaboard->gpio_requested & GPIO_HP_MUTE)
		gpio_free(pdata->gpio_hp_mute);
	if (seaboard->gpio_requested & GPIO_SPKR_EN)
		gpio_free(pdata->gpio_spkr_en);

	kfree(seaboard);

	return 0;
}

static struct platform_driver tegra_snd_seaboard_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
	},
	.probe = tegra_snd_seaboard_probe,
	.remove = __devexit_p(tegra_snd_seaboard_remove),
};

static int __init snd_tegra_seaboard_init(void)
{
	return platform_driver_register(&tegra_snd_seaboard_driver);
}
module_init(snd_tegra_seaboard_init);

static void __exit snd_tegra_seaboard_exit(void)
{
	platform_driver_unregister(&tegra_snd_seaboard_driver);
}
module_exit(snd_tegra_seaboard_exit);

MODULE_AUTHOR("Stephen Warren <swarren@nvidia.com>");
MODULE_DESCRIPTION("Seaboard machine ASoC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
