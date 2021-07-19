// SPDX-License-Identifier: GPL-2.0

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

struct pwm_clk_chip {
	struct pwm_chip chip;
	struct clk *clk;
};

#define to_pwm_clk_chip(_chip) container_of(_chip, struct pwm_clk_chip, chip)

static int pwm_clk_apply(struct pwm_chip *pwm_chip, struct pwm_device *pwm,
			 const struct pwm_state *state)
{
	struct pwm_clk_chip *chip = to_pwm_clk_chip(pwm_chip);
	int ret;
	u32 rate;

	if (!state->enabled && !pwm->state.enabled)
		return 0;

	if (state->enabled && !pwm->state.enabled) {
		ret = clk_enable(chip->clk);
		if (ret)
			return ret;
	}

	if (!state->enabled && pwm->state.enabled) {
		clk_disable(chip->clk);
		return 0;
	}

	rate = div64_u64(NSEC_PER_SEC, state->period);
	ret = clk_set_rate(chip->clk, rate);
	if (ret)
		return ret;

	ret = clk_set_duty_cycle(chip->clk, state->duty_cycle, state->period);
	return ret;
}

static const struct pwm_ops pwm_clk_ops = {
	.apply = pwm_clk_apply,
	.owner = THIS_MODULE,
};

static int pwm_clk_probe(struct platform_device *pdev)
{
	struct pwm_clk_chip *chip;
	int ret;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(chip->clk)) {
		dev_err(&pdev->dev, "Failed to get clock: %ld\n", PTR_ERR(chip->clk));
		return PTR_ERR(chip->clk);
	}

	chip->chip.dev = &pdev->dev;
	chip->chip.ops = &pwm_clk_ops;
	chip->chip.of_xlate = of_pwm_xlate_with_flags;
	chip->chip.of_pwm_n_cells = 2;
	chip->chip.base = 0;
	chip->chip.npwm = 1;

	ret = clk_prepare(chip->clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to prepare clock: %d\n", ret);
		return ret;
	}

	ret = pwmchip_add(&chip->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to add pwm chip: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, chip);
	return 0;
}

static int pwm_clk_remove(struct platform_device *pdev)
{
	struct pwm_clk_chip *chip = platform_get_drvdata(pdev);

	clk_unprepare(chip->clk);

	return pwmchip_remove(&chip->chip);
}

static const struct of_device_id pwm_clk_dt_ids[] = {
	{ .compatible = "clk-pwm", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pwm_clk_dt_ids);

static struct platform_driver pwm_clk_driver = {
	.driver = {
		.name = "clk-pwm",
		.of_match_table = pwm_clk_dt_ids,
	},
	.probe = pwm_clk_probe,
	.remove = pwm_clk_remove,
};
module_platform_driver(pwm_clk_driver);

MODULE_ALIAS("platform:clk-pwm");
MODULE_AUTHOR("Nikita Travkin <nikita@trvn.ru>");
MODULE_DESCRIPTION("Clock based PWM driver");
MODULE_LICENSE("GPL v2");
