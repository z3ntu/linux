/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/bitfield.h>
#include <linux/led-class-flash.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-flash-led-class.h>

#define SM5708_LED_BASE(n)		(0x17 + (n) * 4)
#define SM5708_NUM_LEDS			2
#define SM5708_REG_STATUS3		(8 + 3)
#define STATUS3_FLEDSHORT1_MASK		BIT(0)
#define STATUS3_FLEDOPEN1_MASK		BIT(1)
#define STATUS3_FLEDSHORT2_MASK		BIT(2)
#define STATUS3_FLEDOPEN2_MASK		BIT(3)

/* Offsets from SM5708_LED_BASE */
#define SM5708_REG_CFG			0x0
#define REG_CFG_MODE_MASK		GENMASK(1, 0)
#define REG_CFG_MODE_VAL_OFF		0x0
#define REG_CFG_MODE_VAL_TORCH		0x1
#define REG_CFG_MODE_VAL_FLASH		0x2
#define REG_CFG_MODE_VAL_GPIO_CTRL	0x3
#define SM5708_REG_TIMEOUT		0x1
#define REG_TIMEOUT_MASK		GENMASK(3, 0)
#define REG_TIMEOUT_EN			BIT(4)
#define SM5708_REG_FLASH_ISEL		0x2
#define SM5708_REG_TORCH_ISEL		0x3
#define ISEL_MAX			0x1f

struct sm5708_led {
	struct gpio_desc *torch_ctrl, *flash_ctrl;
	struct led_classdev_flash flash;
	struct sm5708_priv *priv;
	struct v4l2_flash *v4l2_flash;
	unsigned index;
	bool flash_en;
	bool torch_en;
};

struct sm5708_priv {
	struct regmap *regmap;
	struct regulator *boost_vreg;
	struct sm5708_led leds[SM5708_NUM_LEDS];
};

static inline struct sm5708_led *
to_sm5708_fled(struct led_classdev *led_cdev)
{
	return container_of(led_cdev, struct sm5708_led, flash.led_cdev);
}

static const struct led_flash_setting sm5708_led_brightness = {
	.min = 10000,
	.max = 320000,
	.step = 10000,
};

static const struct led_flash_setting sm5708_flash_timeout = {
	.min = 100000,
	.max = 1600000,
	.step = 100000,
};

static const struct led_flash_setting sm5708_flash_brightness[SM5708_NUM_LEDS] = {
	{
		.min = 300000,
		.max = 1500000,
		.step = 25000,
	},
	{
		.min = 25000,
		.max = 800000,
		.step = 25000,
	}
};

static u32 sm5708_encode(const struct led_flash_setting *s, u32 value)
{
	return (clamp(value, s->min, s->max) - s->min) / s->step;
}

static u32 sm5708_decode(const struct led_flash_setting *s, u32 value)
{
	return min(s->min + s->step * value, s->max);
}

static u32 sm5708_clamp_align(const struct led_flash_setting *s, u32 value)
{
	return sm5708_decode(s, sm5708_encode(s, value));
}

static int sm5708_torch_set(struct led_classdev *led_cdev,
			    enum led_brightness value)
{
	struct sm5708_led *led = to_sm5708_fled(led_cdev);
	u32 isel, reg_base = SM5708_LED_BASE(led->index);
	int ret = 0;

	/* Can't use torch if flash is active */
	if (led->flash_en)
		return -EBUSY;

	if (value == LED_OFF) {
		ret = regmap_update_bits(led->priv->regmap, reg_base + SM5708_REG_CFG,
					 REG_CFG_MODE_MASK, REG_CFG_MODE_VAL_OFF);
		if (ret)
			return ret;
	} else {
		isel = min((unsigned) value - 1, (unsigned) ISEL_MAX);
	}

	ret = regmap_write(led->priv->regmap, reg_base + SM5708_REG_TORCH_ISEL, isel);
	if (ret)
		return ret;

	led->torch_en = !!value;

	if (value != LED_OFF)
		ret = regmap_update_bits(led->priv->regmap, reg_base + SM5708_REG_CFG,
					 REG_CFG_MODE_MASK, REG_CFG_MODE_VAL_TORCH);

	return ret;
}

static int sm5708_flash_brightness_set(struct led_classdev_flash *flash,
				u32 brightness)
{
	struct sm5708_led *led = to_sm5708_fled(&flash->led_cdev);
	u32 isel;

	isel = sm5708_encode(&sm5708_flash_brightness[led->index], brightness);

	/* LED0 Flash current step is 50 mA above 650 mA but we pretend
	 * it's 25 mA everywhere and map register value here for simplicity */
	if (led->index == 0 && isel >= 16)
		isel = 15 + ((isel - 16) >> 1);

	return regmap_write(led->priv->regmap,
			SM5708_LED_BASE(led->index) + SM5708_REG_FLASH_ISEL,
			min(isel, 0x1fu));
}

static int sm5708_strobe_get(struct led_classdev_flash *flash, bool *state)
{
	struct sm5708_led *led = to_sm5708_fled(&flash->led_cdev);

	*state = led->flash_en;

	return 0;
}

static int sm5708_strobe_set(struct led_classdev_flash *flash, bool state)
{
	struct sm5708_led *led = to_sm5708_fled(&flash->led_cdev);
	int ret;

	/* Can't use flash if torch is active too */
	if (led->torch_en)
		return -EBUSY;

	if (state == led->flash_en)
		return 0;

	if (!led->flash_en) {
		ret = regulator_enable(led->priv->boost_vreg);
		if (ret)
			return ret;
	}

	ret = regmap_update_bits(led->priv->regmap,
			SM5708_LED_BASE(led->index) + SM5708_REG_CFG,
			REG_CFG_MODE_MASK,
			state ? REG_CFG_MODE_VAL_FLASH : REG_CFG_MODE_VAL_OFF);
	if (ret)
		return ret;

	if (led->flash_en)
		regulator_disable(led->priv->boost_vreg);

	led->flash_en = state;

	return 0;
}

static int sm5708_timeout_set(struct led_classdev_flash *flash, u32 timeout)
{
	struct sm5708_led *led = to_sm5708_fled(&flash->led_cdev);
	u32 tosel;

	tosel = sm5708_encode(&sm5708_flash_timeout, timeout);

	return regmap_write(led->priv->regmap,
			SM5708_LED_BASE(led->index) + SM5708_REG_TIMEOUT,
			REG_TIMEOUT_EN | FIELD_PREP(REG_TIMEOUT_MASK, tosel));
}

static int sm5708_fault_get(struct led_classdev_flash *flash, u32 *fault)
{
	struct sm5708_led *led = to_sm5708_fled(&flash->led_cdev);
	u32 regval;
	int ret;

	ret = regmap_read(led->priv->regmap, SM5708_REG_STATUS3, &regval);
	if (ret)
		return ret;

	*fault = 0;
	if (regval & BIT(2 * led->index))
		*fault |= LED_FAULT_SHORT_CIRCUIT;

	return 0;
}

static const struct led_flash_ops sm5708_flash_ops = {
	.flash_brightness_set = sm5708_flash_brightness_set,
	.strobe_get = sm5708_strobe_get,
	.strobe_set = sm5708_strobe_set,
	.timeout_set = sm5708_timeout_set,
	.fault_get = sm5708_fault_get,
};

static void sm5708_led_cleanup(void *data)
{
	struct sm5708_led *led = data;

	if (!IS_ERR_OR_NULL(led->v4l2_flash))
		v4l2_flash_release(led->v4l2_flash);

	if (led->flash_en)
		regulator_disable(led->priv->boost_vreg);

	led->flash_en = 0;
}

static int sm5708_led_init(struct device *dev,
			   struct fwnode_handle *fwnode,
			   struct sm5708_led *led)
{
	struct led_classdev_flash *flash = &led->flash;
	struct led_classdev *lcdev = &flash->led_cdev;
	struct led_init_data init_data = { 0 };
	struct v4l2_flash_config sd_cfg = { 0 };
	const char *prop;
	u32 value;
	int ret;

	ret = fwnode_property_read_u32(fwnode, prop = "led-max-microamp", &value);
	if (ret)
		goto prop_err;

	value = sm5708_encode(&sm5708_led_brightness, value);

	init_data.fwnode = fwnode;
	lcdev->brightness = LED_OFF;
	lcdev->brightness_set_blocking = sm5708_torch_set;
	lcdev->flags = LED_DEV_CAP_FLASH;
	lcdev->max_brightness = 1 + value;
	flash->brightness = sm5708_flash_brightness[led->index];
	flash->ops = &sm5708_flash_ops;
	flash->timeout = sm5708_flash_timeout;

	/* V4L2 Flash Subdev configuration */
	sd_cfg.intensity = sm5708_led_brightness;
	sd_cfg.intensity.max = sm5708_decode(&sm5708_led_brightness, value);
	sd_cfg.intensity.val = sd_cfg.intensity.max;
	sd_cfg.flash_faults = LED_FAULT_SHORT_CIRCUIT;
	/* No known boards implementing this */
	sd_cfg.has_external_strobe = false;

	ret = fwnode_property_read_u32(fwnode, prop = "flash-max-microamp", &value);
	if (ret)
		goto prop_err;

	value = sm5708_clamp_align(&flash->brightness, value);
	flash->brightness.val = flash->brightness.max = value;

	ret = fwnode_property_read_u32(fwnode, prop = "flash-max-timeout-us", &value);
	if (ret)
		goto prop_err;

	value = sm5708_clamp_align(&flash->timeout, value);
	flash->timeout.val = flash->timeout.max = value;

	ret = devm_led_classdev_flash_register_ext(dev, flash, &init_data);
	if (ret)
		return ret;

	snprintf(sd_cfg.dev_name, sizeof(sd_cfg.dev_name), "%s %s", dev_name(lcdev->dev),
			dev_name(regmap_get_device(led->priv->regmap)));
	led->v4l2_flash = v4l2_flash_init(dev, fwnode, flash, NULL, &sd_cfg);
	if (IS_ERR(led->v4l2_flash))
		return PTR_ERR(led->v4l2_flash);

	ret = devm_add_action_or_reset(dev, sm5708_led_cleanup, led);
	if (ret)
		return ret;

	return 0;

prop_err:
	dev_err(dev, "failed to parse \"%s\" property\n", prop);
	return ret;
}

static int sm5708_fled_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fwnode_handle *fwnode;
	struct sm5708_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(struct sm5708_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regmap = dev_get_regmap(dev->parent, NULL);
	if (!priv->regmap)
		return -EINVAL;

	priv->boost_vreg = devm_regulator_get(dev, "flash-boost");
	if (IS_ERR(priv->boost_vreg))
		return dev_err_probe(dev, PTR_ERR(priv->boost_vreg),
				"failed to get flash-boost regulator\n");

	device_for_each_child_node(dev, fwnode) {
		u32 reg;

		ret = fwnode_property_read_u32(fwnode, "reg", &reg);
		if (ret || reg >= SM5708_NUM_LEDS || priv->leds[reg].priv)
			return ret;

		priv->leds[reg].priv = priv;
		priv->leds[reg].index = reg;

		ret = sm5708_led_init(dev, fwnode, &priv->leds[reg]);
		if (ret < 0)
			return dev_err_probe(dev, ret, "failed to init led index %d\n", reg);
	}

	return 0;
}

static const struct of_device_id sm5708_fled_match[] = {
	{ .compatible = "siliconmitus,sm5708-fled" },
	{ },
};

MODULE_DEVICE_TABLE(of, sm5708_fled_match);

static struct platform_driver sm5708_fled_driver = {
	.probe		= sm5708_fled_probe,
	.driver		= {
		.name	= "leds-sm5708-fled",
		.of_match_table = sm5708_fled_match,
	},
};

module_platform_driver(sm5708_fled_driver);

MODULE_AUTHOR("Vladimir Lypak <vladimir.lypak@gmail.com>");
MODULE_DESCRIPTION("SM5708 Flash LED driver");
MODULE_LICENSE("GPL v2");
