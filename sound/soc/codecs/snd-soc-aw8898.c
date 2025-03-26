// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2018 AWINIC Technology CO., LTD
 * Author: Nick Li <liweilei@awinic.com.cn>
 *
 * Copyright (c) 2025 Luca Weiss <luca@lucaweiss.eu>
 */

#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

/* Chip ID */
#define AW8898_ID				0x00
#define AW8898_CHIP_ID				0x1702

/* System Status */
#define AW8898_SYSST				0x01
#define AW8898_SYSST_PLLS			BIT(0)

/* System Interrupt Mask */
#define AW8898_SYSINTM				0x03
#define AW8898_SYSINTM_OCDM			BIT(3)
#define AW8898_SYSINTM_OTHM			BIT(1)
#define AW8898_SYSINTM_PLLM			BIT(0)

/* System Control */
#define AW8898_SYSCTRL				0x04
#define AW8898_SYSCTRL_MODE_MASK		GENMASK(7, 7)
#define AW8898_SYSCTRL_RCV_MODE			(1<<7)
#define AW8898_SYSCTRL_SPK_MODE			(0<<7)
#define AW8898_SYSCTRL_PW_MASK			GENMASK(0, 0)
#define AW8898_SYSCTRL_PW_PDN			(1<<0)
#define AW8898_SYSCTRL_PW_ACTIVE		(0<<0)

/* I2S Interface Control */
#define AW8898_I2SCTRL				0x05
#define AW8898_I2SCTRL_FMS_MASK			GENMASK(7, 6)
#define AW8898_I2SCTRL_FMS_32BIT		(3<< 6)
#define AW8898_I2SCTRL_FMS_24BIT		(2<< 6)
#define AW8898_I2SCTRL_FMS_20BIT		(1<< 6)
#define AW8898_I2SCTRL_FMS_16BIT		(0<< 6)
#define AW8898_I2SCTRL_SR_MASK			GENMASK(3, 0)
#define AW8898_I2SCTRL_SR_48K			(8<<0)
#define AW8898_I2SCTRL_SR_44P1K			(7<<0)
#define AW8898_I2SCTRL_SR_32K			(6<<0)
#define AW8898_I2SCTRL_SR_16K			(3<<0)
#define AW8898_I2SCTRL_SR_8K			(0<<0)

/* PWM Control */
#define AW8898_PWMCTRL				0x08
#define AW8898_PWMCTRL_HMUTE_MASK		GENMASK(0, 0)
#define AW8898_PWMCTRL_HMUTE_ENABLE		(1<<0)
#define AW8898_PWMCTRL_HMUTE_DISABLE		(0<<0)

/* Hardware AGC Configuration 7 */
#define AW8898_HAGCCFG7				0x0f
#define AW8898_HAGCCFG7_VOL_MASK		GENMASK(15, 8)
#define AW8898_VOLUME_MAX			(0)
#define AW8898_VOLUME_MIN			(-255)
#define AW8898_VOL_REG_SHIFT			(8)

#define AW8898_MAX_REGISTER			0xff

#define AW8898_CFG_NAME				"aw8898_cfg.bin"

struct aw8898 {
	struct snd_soc_component *component;
	struct regmap *regmap;
	struct i2c_client *client;
	struct mutex cfg_lock;
	struct gpio_desc *reset;
	int dev_mode;
	bool init;
};

struct aw8898_container {
	int len;
	unsigned char data[];
};

static const char * const aw8898_dev_mode_text[] = {
	"Speaker", "Receiver"
};

enum aw8898_mode {
	AW8898_SPEAKER,
	AW8898_RECEIVER,
};

static const struct soc_enum aw8898_dev_mode_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(aw8898_dev_mode_text), aw8898_dev_mode_text);

static void aw8898_run_mute(struct aw8898 *aw8898, bool mute)
{
	unsigned int val = AW8898_PWMCTRL_HMUTE_DISABLE;

	if (mute)
		val = AW8898_PWMCTRL_HMUTE_ENABLE;

	regmap_update_bits(aw8898->regmap, AW8898_PWMCTRL,
			   AW8898_PWMCTRL_HMUTE_MASK, val);
}

// TODO invert - make clearer, maybe aw8898_set_power([...], bool on)
static void aw8898_run_pwd(struct aw8898 *aw8898, bool pwd)
{
	unsigned int val = AW8898_SYSCTRL_PW_ACTIVE;

	if (pwd)
		val = AW8898_SYSCTRL_PW_PDN;

	regmap_update_bits(aw8898->regmap, AW8898_SYSCTRL,
			   AW8898_SYSCTRL_PW_MASK, val);
}

// FIXME clean up
static void aw8898_spk_rcv_mode(struct aw8898 *aw8898)
{
	if (aw8898->dev_mode == AW8898_SPEAKER) {
		regmap_update_bits(aw8898->regmap, AW8898_SYSCTRL,
				      AW8898_SYSCTRL_MODE_MASK,
				      AW8898_SYSCTRL_SPK_MODE);
	} else if (aw8898->dev_mode == AW8898_RECEIVER) {
		regmap_update_bits(aw8898->regmap, AW8898_SYSCTRL,
				      AW8898_SYSCTRL_MODE_MASK,
				      AW8898_SYSCTRL_RCV_MODE);
	}
}

static void aw8898_start(struct aw8898 *aw8898)
{
	unsigned int val;
	int err;

	aw8898_run_pwd(aw8898, false);

	msleep(2);

	err = regmap_read_poll_timeout(aw8898->regmap, AW8898_SYSST,
				       val, val & AW8898_SYSST_PLLS,
				       2000, 1 * USEC_PER_SEC);
	if (err) {
		dev_err(&aw8898->client->dev, "iis signal check error: %d\n", err);
		aw8898_run_pwd(aw8898, true);
		return;
	}

	aw8898_run_mute(aw8898, false);
}

static void aw8898_stop(struct aw8898 *aw8898)
{
	aw8898_run_mute(aw8898, true);
	aw8898_run_pwd(aw8898, true);
}

// FIXME clean up
static void aw8898_container_update(struct aw8898 *aw8898,
				    struct aw8898_container *aw8898_cont)
{
	int i;
	int reg_addr;
	int reg_val;

	pr_debug("%s enter\n", __func__);

	for (i = 0; i < aw8898_cont->len; i += 4) {
		reg_addr = (aw8898_cont->data[i + 1] << 8) +
			   aw8898_cont->data[i + 0];
		reg_val = (aw8898_cont->data[i + 3] << 8) +
			  aw8898_cont->data[i + 2];
		pr_info("%s: reg=0x%04x, val = 0x%04x\n", __func__, reg_addr,
			reg_val);
		regmap_write(aw8898->regmap, (unsigned char)reg_addr,
				 (unsigned int)reg_val);
	}

	pr_debug("%s exit\n", __func__);
}

// FIXME clean up
static void aw8898_cfg_loaded(const struct firmware *cont, void *context)
{
	struct aw8898 *aw8898 = context;
	struct aw8898_container *aw8898_cfg;
	unsigned int i = 0;

	if (!cont) {
		pr_err("%s: failed to read %s\n", __func__, AW8898_CFG_NAME);
		release_firmware(cont);
		return;
	}

	pr_info("%s: loaded %s - size: %zu\n", __func__, AW8898_CFG_NAME,
		cont ? cont->size : 0);

	for (i = 0; i < cont->size; i++) {
		pr_info("%s: addr:0x%04x, data:0x%02x\n", __func__, i,
			*(cont->data + i));
	}

	aw8898_cfg = kzalloc(cont->size + sizeof(int), GFP_KERNEL);
	if (!aw8898_cfg) {
		release_firmware(cont);
		pr_err("%s: error allocating memory\n", __func__);
		return;
	}
	aw8898_cfg->len = cont->size;
	memcpy(aw8898_cfg->data, cont->data, cont->size);
	release_firmware(cont);

	aw8898_container_update(aw8898, aw8898_cfg);

	kfree(aw8898_cfg);

	aw8898->init = true;
	pr_info("%s: cfg update complete\n", __func__);

	aw8898_spk_rcv_mode(aw8898);
	aw8898_start(aw8898);
}

static void aw8898_cold_start(struct aw8898 *aw8898)
{
	int err;

	err = request_firmware_nowait(THIS_MODULE, FW_ACTION_UEVENT,
				      AW8898_CFG_NAME, &aw8898->client->dev, GFP_KERNEL,
				      aw8898, aw8898_cfg_loaded);
	if (err)
		dev_err(&aw8898->client->dev, "cfg loading requested failed: %d\n", err);
}

// FIXME clean up
static const DECLARE_TLV_DB_SCALE(digital_gain, 0, 50, 0);

// FIXME clean up
struct soc_mixer_control aw8898_mixer = {
	.reg = AW8898_HAGCCFG7,
	.shift = AW8898_VOL_REG_SHIFT,
	.max = AW8898_VOLUME_MAX,
	.min = AW8898_VOLUME_MIN,
};

// FIXME clean up
static int aw8898_volume_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mc->max - mc->min;
	return 0;
}

// FIXME clean up
static int aw8898_volume_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct aw8898 *aw8898 = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0;
	unsigned int value = 0;
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	regmap_read(aw8898->regmap, AW8898_HAGCCFG7, &reg_val);
	ucontrol->value.integer.value[0] = (value >> mc->shift) &
					   (AW8898_HAGCCFG7_VOL_MASK);
	return 0;
}

// FIXME clean up
static int aw8898_volume_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct aw8898 *aw8898 = snd_soc_component_get_drvdata(component);
	unsigned int value = 0;
	unsigned int reg_value = 0;

	//value is right
	value = ucontrol->value.integer.value[0];
	if (value > (mc->max - mc->min) || value < 0) {
		pr_err("%s:value over range \n", __func__);
		return -1;
	}

	//smartpa have clk
	regmap_read(aw8898->regmap, AW8898_SYSST, &reg_value);
	if (!(reg_value & AW8898_SYSST_PLLS)) {
		pr_err("%s: NO I2S CLK ,cat not write reg \n", __func__);
		return 0;
	}
	//cal real value
	value = value << mc->shift & AW8898_HAGCCFG7_VOL_MASK;
	regmap_read(aw8898->regmap, AW8898_HAGCCFG7, &reg_value);
	value = value | (reg_value & 0x00ff);

	//write value
	regmap_write(aw8898->regmap, AW8898_HAGCCFG7, value);

	return 0;
}

static struct snd_kcontrol_new aw8898_volume = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "aw8898_rx_volume",
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |
		  SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.tlv.p = (digital_gain),
	.info = aw8898_volume_info,
	.get = aw8898_volume_get,
	.put = aw8898_volume_put,
	.private_value = (unsigned long)&aw8898_mixer,
};

static int aw8898_dev_mode_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct aw8898 *aw8898 = snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = aw8898->dev_mode;

	return 0;
}

static int aw8898_dev_mode_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct aw8898 *aw8898 = snd_soc_component_get_drvdata(component);

	if (aw8898->dev_mode == ucontrol->value.enumerated.item[0])
		return 0;

	aw8898->dev_mode = ucontrol->value.enumerated.item[0];

	aw8898_spk_rcv_mode(aw8898);

	return 1;
}

static struct snd_kcontrol_new aw8898_controls[] = {
	SOC_ENUM_EXT("AMP MODE", aw8898_dev_mode_enum,
		     aw8898_dev_mode_get, aw8898_dev_mode_put)
};

static int aw8898_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct aw8898 *aw8898 = snd_soc_component_get_drvdata(dai->component);

	aw8898_run_pwd(aw8898, false);

	return 0;
}

static int aw8898_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		if ((fmt & SND_SOC_DAIFMT_MASTER_MASK)
				!= SND_SOC_DAIFMT_CBC_CFC) {
			dev_err(component->dev, "invalid codec master mode: %d\n",
				fmt & SND_SOC_DAIFMT_MASTER_MASK);
			return -EINVAL;
		}
		break;
	default:
		dev_err(component->dev, "unsupported DAI format %d\n",
			fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	return 0;
}

static int aw8898_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct aw8898 *aw8898 = snd_soc_component_get_drvdata(dai->component);
	unsigned int reg;

	switch (params_rate(params)) {
	case 8000:
		reg = AW8898_I2SCTRL_SR_8K;
		break;
	case 16000:
		reg = AW8898_I2SCTRL_SR_16K;
		break;
	case 32000:
		reg = AW8898_I2SCTRL_SR_32K;
		break;
	case 44100:
		reg = AW8898_I2SCTRL_SR_44P1K;
		break;
	case 48000:
		reg = AW8898_I2SCTRL_SR_48K;
		break;
	default:
		dev_err(component->dev, "Not supported sample rate: %d\n",
			params_rate(params));
		return -EINVAL;
	}

	regmap_update_bits(aw8898->regmap, AW8898_I2SCTRL,
			   AW8898_I2SCTRL_SR_MASK, reg);

	switch (params_width(params)) {
	case 16:
		reg = AW8898_I2SCTRL_FMS_16BIT;
		break;
	case 20:
		reg = AW8898_I2SCTRL_FMS_20BIT;
		break;
	case 24:
		reg = AW8898_I2SCTRL_FMS_24BIT;
		break;
	case 32:
		reg = AW8898_I2SCTRL_FMS_32BIT;
		break;
	default:
		dev_err(component->dev, "Not supported sample size: %d\n",
			params_width(params));
		return -EINVAL;
	}

	regmap_update_bits(aw8898->regmap, AW8898_I2SCTRL,
			   AW8898_I2SCTRL_FMS_MASK, reg);

	return 0;
}

static int aw8898_mute(struct snd_soc_dai *dai, int mute, int stream)
{
	struct aw8898 *aw8898 = snd_soc_component_get_drvdata(dai->component);

	mutex_lock(&aw8898->cfg_lock);

	if (mute) {
		aw8898_stop(aw8898);
	} else {
		if (!aw8898->init) {
			aw8898_cold_start(aw8898);
		} else {
			aw8898_spk_rcv_mode(aw8898);
			aw8898_start(aw8898);
		}
	}

	mutex_unlock(&aw8898->cfg_lock);

	return 0;
}

static void aw8898_shutdown(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct aw8898 *aw8898 = snd_soc_component_get_drvdata(dai->component);

	aw8898_run_pwd(aw8898, true);
}

static const struct snd_soc_dai_ops aw8898_dai_ops = {
	.startup	= aw8898_startup,
	.set_fmt	= aw8898_set_fmt,
	.hw_params	= aw8898_hw_params,
	.mute_stream	= aw8898_mute,
	.shutdown	= aw8898_shutdown,
};

#define AW8898_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE | \
			SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver aw8898_dai[] = {
	{
		.name = "aw8898-amplifier",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = AW8898_FORMATS,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = AW8898_FORMATS,
		},
		.ops = &aw8898_dai_ops,
		.symmetric_rate = 1,
		.symmetric_channels = 1,
		.symmetric_sample_bits = 1,
	},
};

static int aw8898_component_probe(struct snd_soc_component *component)
{
	struct aw8898 *aw8898 = snd_soc_component_get_drvdata(component);

	aw8898->component = component;

	// FIXME regulator_bulk_enable

	snd_soc_add_component_controls(component, aw8898_controls,
				       ARRAY_SIZE(aw8898_controls));

	snd_soc_add_component_controls(component, &aw8898_volume, 1);

	return 0;
}

static struct snd_soc_component_driver soc_component_dev_aw8898 = {
	.probe = aw8898_component_probe,
};

static const struct regmap_config aw8898_regmap = {
	.reg_bits = 8,
	.val_bits = 16,

	.max_register = AW8898_MAX_REGISTER,
	.cache_type = REGCACHE_RBTREE,
};

static void aw8898_reset(struct aw8898 *aw8898)
{
	gpiod_set_value_cansleep(aw8898->reset, 1);
	msleep(1);
	gpiod_set_value_cansleep(aw8898->reset, 0);
	msleep(1);
}

static int aw8898_check_chipid(struct aw8898 *aw8898)
{
	unsigned int reg;
	int ret;

	ret = regmap_read(aw8898->regmap, AW8898_ID, &reg);
	if (ret < 0) {
		dev_err(&aw8898->client->dev,
			"Failed to read register AW8898_ID: %d\n", ret);
		return ret;
	}

	if (reg != AW8898_CHIP_ID) {
		dev_err(&aw8898->client->dev, "Unexpected chip ID: 0x%x\n",
			reg);
		return -EINVAL;
	}

	return 0;
}

static int aw8898_probe(struct i2c_client *client)
{
	struct aw8898 *aw8898;
	int ret;

	aw8898 = devm_kzalloc(&client->dev, sizeof(*aw8898), GFP_KERNEL);
	if (!aw8898)
		return -ENOMEM;

	i2c_set_clientdata(client, aw8898);
	aw8898->client = client;

	aw8898->regmap = devm_regmap_init_i2c(client, &aw8898_regmap);
	if (IS_ERR(aw8898->regmap))
		return dev_err_probe(&client->dev, PTR_ERR(aw8898->regmap),
				     "failed to allocate register map\n");

	mutex_init(&aw8898->cfg_lock);

	aw8898->reset = devm_gpiod_get(&client->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(aw8898->reset))
		return dev_err_probe(&client->dev, PTR_ERR(aw8898->reset),
				     "failed to get reset GPIO\n");

	aw8898_reset(aw8898);

	ret = aw8898_check_chipid(aw8898);
	if (ret)
		return dev_err_probe(&client->dev, ret, "Chip ID check failed\n");

	// FIXME regulator_bulk

	dev_set_drvdata(&client->dev, aw8898);

	ret = devm_snd_soc_register_component(&client->dev, &soc_component_dev_aw8898,
					      aw8898_dai, ARRAY_SIZE(aw8898_dai));
	if (ret < 0)
		return dev_err_probe(&client->dev, ret, "Failed to register component\n");

	return 0;
}

static const struct i2c_device_id aw8898_id[] = {
	{ "aw8898" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, aw8898_id);

static struct of_device_id aw8898_of_match[] = {
	{ .compatible = "awinic,aw8898" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, aw8898_of_match);

static struct i2c_driver aw8898_driver = {
	.driver = {
		.name = "aw8898",
		.of_match_table = of_match_ptr(aw8898_of_match),
	},
	.probe = aw8898_probe,
	.id_table = aw8898_id,
};

module_i2c_driver(aw8898_driver);

MODULE_DESCRIPTION("AW8898 Audio amplifier driver");
MODULE_LICENSE("GPL");
