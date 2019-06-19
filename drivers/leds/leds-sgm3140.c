// SPDX-License-Identifier: GPL-2.0

#include <linux/gpio/consumer.h>
#include <linux/led-class-flash.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

struct sgm3140 {
	struct gpio_desc *flash_gpio;
	struct gpio_desc *enable_gpio;

	struct led_classdev_flash fled_cdev;
};

static struct sgm3140 *flcdev_to_sgm3140(struct led_classdev_flash *flcdev)
{
	return container_of(flcdev, struct sgm3140, fled_cdev);
}

int sgm3140_strobe_set(struct led_classdev_flash *fled_cdev, bool state)
{
	struct sgm3140 *priv = flcdev_to_sgm3140(fled_cdev);

	if (state) {
		gpiod_set_value_cansleep(priv->flash_gpio, 1);
		gpiod_set_value_cansleep(priv->enable_gpio, 1);
	} else {
		gpiod_set_value_cansleep(priv->enable_gpio, 0);
		gpiod_set_value_cansleep(priv->flash_gpio, 0);
	}

	return 0;
}

static int sgm3140_timeout_set(struct led_classdev_flash *fled_cdev,
			       u32 timeout)
{
	return 0;
}

struct led_flash_ops sgm3140_flash_ops = {
	.strobe_set = sgm3140_strobe_set,
	.timeout_set = sgm3140_timeout_set,
};

int sgm3140_brightness_set(struct led_classdev *led_cdev,
			   enum led_brightness brightness)
{
	struct led_classdev_flash *fled_cdev = lcdev_to_flcdev(led_cdev);
	struct sgm3140 *priv = flcdev_to_sgm3140(fled_cdev);

	if (brightness == LED_OFF)
		gpiod_set_value_cansleep(priv->enable_gpio, 0);
	else
		gpiod_set_value_cansleep(priv->enable_gpio, 1);

	return 0;
}

static int sgm3140_probe(struct platform_device *pdev)
{
	struct sgm3140 *priv;
	struct led_classdev *led_cdev;
	struct led_classdev_flash *fled_cdev;
	struct device_node *child_node;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->flash_gpio = devm_gpiod_get(&pdev->dev, "flash", GPIOD_OUT_LOW);
	ret = PTR_ERR_OR_ZERO(priv->flash_gpio);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to request flash gpio: %d\n",
				ret);
		return ret;
	}

	priv->enable_gpio = devm_gpiod_get(&pdev->dev, "enable", GPIOD_OUT_LOW);
	ret = PTR_ERR_OR_ZERO(priv->enable_gpio);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to request enable gpio: %d\n",
				ret);
		return ret;
	}

	child_node = of_get_next_available_child(pdev->dev.of_node, NULL);
	if (!child_node) {
		dev_err(&pdev->dev, "No DT child node found for connected LED.\n");
		return -EINVAL;
	}

	fled_cdev = &priv->fled_cdev;
	led_cdev = &fled_cdev->led_cdev;

	fled_cdev->ops = &sgm3140_flash_ops;
	fled_cdev->timeout.max = 300000; /* 300ms */

	led_cdev->brightness_set_blocking = sgm3140_brightness_set;
	led_cdev->max_brightness = LED_ON;
	led_cdev->flags |= LED_DEV_CAP_FLASH;
	led_cdev->name =
		of_get_property(child_node, "label", NULL) ? : child_node->name;

	platform_set_drvdata(pdev, priv);

	/* Register in the LED subsystem */
	ret = led_classdev_flash_register(&pdev->dev, fled_cdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register flash device: %d\n",
			ret);
		goto err;
	}

err:
	of_node_put(child_node);
	return ret;
}

static int sgm3140_remove(struct platform_device *pdev)
{
	struct sgm3140 *priv = platform_get_drvdata(pdev);

	led_classdev_flash_unregister(&priv->fled_cdev);

	return 0;
}

static const struct of_device_id sgm3140_dt_match[] = {
	{ .compatible = "sgmicro,sgm3140" },
	{},
};
MODULE_DEVICE_TABLE(of, sgm3140_dt_match);

static struct platform_driver sgm3140_driver = {
	.probe	= sgm3140_probe,
	.remove	= sgm3140_remove,
	.driver	= {
		.name	= "sgm3140",
		.of_match_table = sgm3140_dt_match,
	},
};

module_platform_driver(sgm3140_driver);

MODULE_AUTHOR("Luca Weiss <luca@z3ntu.xyz>");
MODULE_DESCRIPTION("SG Micro SGM3140 charge pump led driver");
MODULE_LICENSE("GPL v2");
