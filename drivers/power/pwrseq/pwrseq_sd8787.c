// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * pwrseq_sd8787.c - power sequence support for Marvell SD8787 BT + Wifi chip
 *
 * Copyright (C) 2016 Matt Ranostay <matt@ranostay.consulting>
 *
 * Based on the original work pwrseq_sd8787.c
 *  Copyright (C) 2014 Linaro Ltd
 *  Author: Ulf Hansson <ulf.hansson@linaro.org>
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>

#include <linux/pwrseq/driver.h>

struct pwrseq_sd8787 {
	struct gpio_desc *reset_gpio;
	struct gpio_desc *pwrdn_gpio;
	u32 reset_pwrdwn_delay_ms;
};

static int pwrseq_sd8787_pre_power_on(struct pwrseq *pwrseq)
{
	struct pwrseq_sd8787 *pwrseq_sd8787 = pwrseq_get_drvdata(pwrseq);

	gpiod_set_value_cansleep(pwrseq_sd8787->reset_gpio, 1);

	msleep(pwrseq_sd8787->reset_pwrdwn_delay_ms);
	gpiod_set_value_cansleep(pwrseq_sd8787->pwrdn_gpio, 1);

	return 0;
}

static void pwrseq_sd8787_power_off(struct pwrseq *pwrseq)
{
	struct pwrseq_sd8787 *pwrseq_sd8787 = pwrseq_get_drvdata(pwrseq);

	gpiod_set_value_cansleep(pwrseq_sd8787->pwrdn_gpio, 0);
	gpiod_set_value_cansleep(pwrseq_sd8787->reset_gpio, 0);
}

static const struct pwrseq_ops pwrseq_sd8787_ops = {
	.pre_power_on = pwrseq_sd8787_pre_power_on,
	.power_off = pwrseq_sd8787_power_off,
};

static const u32 sd8787_delay_ms = 300;
static const u32 wilc1000_delay_ms = 5;

static const struct of_device_id pwrseq_sd8787_of_match[] = {
	{ .compatible = "mmc-pwrseq-sd8787", .data = &sd8787_delay_ms },
	{ .compatible = "mmc-pwrseq-wilc1000", .data = &wilc1000_delay_ms },
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, pwrseq_sd8787_of_match);

static int pwrseq_sd8787_probe(struct platform_device *pdev)
{
	struct pwrseq_sd8787 *pwrseq_sd8787;
	struct pwrseq *pwrseq;
	struct pwrseq_provider *provider;
	struct device *dev = &pdev->dev;

	pwrseq_sd8787 = devm_kzalloc(dev, sizeof(*pwrseq_sd8787), GFP_KERNEL);
	if (!pwrseq_sd8787)
		return -ENOMEM;

	pwrseq_sd8787->reset_pwrdwn_delay_ms = *(u32 *)of_device_get_match_data(dev);

	pwrseq_sd8787->pwrdn_gpio = devm_gpiod_get(dev, "powerdown", GPIOD_OUT_LOW);
	if (IS_ERR(pwrseq_sd8787->pwrdn_gpio))
		return PTR_ERR(pwrseq_sd8787->pwrdn_gpio);

	pwrseq_sd8787->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(pwrseq_sd8787->reset_gpio))
		return PTR_ERR(pwrseq_sd8787->reset_gpio);

	pwrseq = devm_pwrseq_create(dev, &pwrseq_sd8787_ops);
	if (IS_ERR(pwrseq))
		return PTR_ERR(pwrseq);

	pwrseq_set_drvdata(pwrseq, pwrseq_sd8787);

	provider = devm_of_pwrseq_provider_register(dev, of_pwrseq_xlate_single, pwrseq);

	return PTR_ERR_OR_ZERO(provider);
}

static struct platform_driver pwrseq_sd8787_driver = {
	.probe = pwrseq_sd8787_probe,
	.driver = {
		.name = "pwrseq_sd8787",
		.of_match_table = pwrseq_sd8787_of_match,
	},
};

module_platform_driver(pwrseq_sd8787_driver);
MODULE_LICENSE("GPL v2");
