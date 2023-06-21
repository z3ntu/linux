// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"PBS: %s: " fmt, __func__

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spmi.h>
#include <linux/soc/qcom/qcom-pbs.h>

#define PBS_CLIENT_TRIG_CTL		0x42
#define PBS_CLIENT_SW_TRIG_BIT		BIT(7)
#define PBS_CLIENT_SCRATCH1		0x50
#define PBS_CLIENT_SCRATCH2		0x51

static LIST_HEAD(pbs_dev_list);
static DEFINE_MUTEX(pbs_list_lock);

struct pbs_dev {
	struct device		*dev;
	struct device_node	*dev_node;
	struct regmap		*regmap;
	struct mutex		lock;
	struct list_head	link;

	u32			base;
};

static int qcom_pbs_read(struct pbs_dev *pbs, u32 address, u8 *val)
{
	int ret;

	address += pbs->base;
	ret = regmap_bulk_read(pbs->regmap, address, val, 1);
	if (ret)
		pr_err("Failed to read address=%#x sid=%#x ret=%d\n",
			address, to_spmi_device(pbs->dev->parent)->usid, ret);

	return ret;
}

static int qcom_pbs_write(struct pbs_dev *pbs, u16 address, u8 val)
{
	int ret;

	address += pbs->base;
	ret = regmap_bulk_write(pbs->regmap, address, &val, 1);
	if (ret < 0)
		pr_err("Failed to write address=%#x sid=%#x ret=%d\n",
			  address, to_spmi_device(pbs->dev->parent)->usid, ret);
	else
		pr_debug("Wrote %#x to addr %#x\n", val, address);

	return ret;
}

static int qcom_pbs_masked_write(struct pbs_dev *pbs, u16 address, u8 mask, u8 val)
{
	int ret;

	address += pbs->base;
	ret = regmap_update_bits(pbs->regmap, address, mask, val);
	if (ret < 0)
		pr_err("Failed to write address=%#x ret=%d\n", address, ret);
	else
		pr_debug("Wrote %#x to addr %#x\n", val, address);

	return ret;
}

static int qcom_pbs_wait_for_ack(struct pbs_dev *pbs, u8 bit_pos)
{
	u16 retries = 2000, delay = 1000;
	int ret;
	u8 val;

	while (retries--) {
		ret = qcom_pbs_read(pbs, PBS_CLIENT_SCRATCH2, &val);
		if (ret < 0)
			return ret;

		if (val == 0xFF) {
			/* PBS error - clear SCRATCH2 register */
			ret = qcom_pbs_write(pbs, PBS_CLIENT_SCRATCH2, 0);
			if (ret < 0)
				return ret;

			pr_err("NACK from PBS for bit %u\n", bit_pos);
			return -EINVAL;
		}

		if (val & BIT(bit_pos)) {
			pr_debug("PBS sequence for bit %u executed!\n", bit_pos);
			break;
		}

		usleep_range(delay, delay + 100);
	}

	if (!retries) {
		pr_err("Timeout for PBS ACK/NACK for bit %u\n", bit_pos);
		return -ETIMEDOUT;
	}

	return 0;
}

/**
 * qcom_pbs_trigger_single_event() - Trigger PBS sequence without using bitmap.
 * @pbs: Pointer to PBS device
 *
 * This function is used to trigger the PBS that is hooked on the
 * SW_TRIGGER directly in PBS client.
 *
 * Return: 0 on success, < 0 on failure
 */
int qcom_pbs_trigger_single_event(struct pbs_dev *pbs)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(pbs))
		return -EINVAL;

	mutex_lock(&pbs->lock);
	ret = qcom_pbs_masked_write(pbs, PBS_CLIENT_TRIG_CTL, PBS_CLIENT_SW_TRIG_BIT,
				PBS_CLIENT_SW_TRIG_BIT);
	if (ret < 0)
		pr_err("Failed to write register %x ret=%d\n", PBS_CLIENT_TRIG_CTL, ret);
	mutex_unlock(&pbs->lock);

	return ret;
}
EXPORT_SYMBOL(qcom_pbs_trigger_single_event);

/**
 * qcom_pbs_trigger_event() - Trigger the PBS RAM sequence
 * @pbs: Pointer to PBS device
 * @bitmap: bitmap
 *
 * This function is used to trigger the PBS RAM sequence to be
 * executed by the client driver.
 *
 * The PBS trigger sequence involves
 * 1. setting the PBS sequence bit in PBS_CLIENT_SCRATCH1
 * 2. Initiating the SW PBS trigger
 * 3. Checking the equivalent bit in PBS_CLIENT_SCRATCH2 for the
 *    completion of the sequence.
 * 4. If PBS_CLIENT_SCRATCH2 == 0xFF, the PBS sequence failed to execute
 *
 * Returns: 0 on success, < 0 on failure
 */
int qcom_pbs_trigger_event(struct pbs_dev *pbs, u8 bitmap)
{
	u8 val, mask;
	u16 bit_pos;
	int ret;

	if (!bitmap) {
		pr_err("Invalid bitmap passed by client\n");
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(pbs))
		return -EINVAL;

	mutex_lock(&pbs->lock);
	ret = qcom_pbs_read(pbs, PBS_CLIENT_SCRATCH2, &val);
	if (ret < 0)
		goto out;

	if (val == 0xFF) {
		/* PBS error - clear SCRATCH2 register */
		ret = qcom_pbs_write(pbs, PBS_CLIENT_SCRATCH2, 0);
		if (ret < 0)
			goto out;
	}

	for (bit_pos = 0; bit_pos < 8; bit_pos++) {
		if (bitmap & BIT(bit_pos)) {
			/*
			 * Clear the PBS sequence bit position in
			 * PBS_CLIENT_SCRATCH2 mask register.
			 */
			ret = qcom_pbs_masked_write(pbs, PBS_CLIENT_SCRATCH2, BIT(bit_pos), 0);
			if (ret < 0)
				goto error;

			/*
			 * Set the PBS sequence bit position in
			 * PBS_CLIENT_SCRATCH1 register.
			 */
			val = mask = BIT(bit_pos);
			ret = qcom_pbs_masked_write(pbs, PBS_CLIENT_SCRATCH1, mask, val);
			if (ret < 0)
				goto error;

			/* Initiate the SW trigger */
			val = mask = PBS_CLIENT_SW_TRIG_BIT;
			ret = qcom_pbs_masked_write(pbs, PBS_CLIENT_TRIG_CTL, mask, val);
			if (ret < 0)
				goto error;

			ret = qcom_pbs_wait_for_ack(pbs, bit_pos);
			if (ret < 0)
				goto error;

			/*
			 * Clear the PBS sequence bit position in
			 * PBS_CLIENT_SCRATCH1 register.
			 */
			ret = qcom_pbs_masked_write(pbs, PBS_CLIENT_SCRATCH1, BIT(bit_pos), 0);
			if (ret < 0)
				goto error;

			/*
			 * Clear the PBS sequence bit position in
			 * PBS_CLIENT_SCRATCH2 mask register.
			 */
			ret = qcom_pbs_masked_write(pbs, PBS_CLIENT_SCRATCH2, BIT(bit_pos), 0);
			if (ret < 0)
				goto error;
		}
	}

error:
	/* Clear all the requested bitmap */
	ret = qcom_pbs_masked_write(pbs, PBS_CLIENT_SCRATCH1, bitmap, 0);

out:
	mutex_unlock(&pbs->lock);

	return ret;
}
EXPORT_SYMBOL(qcom_pbs_trigger_event);

/**
 * get_pbs_client_device() - Get the PBS device used by client
 * @dev: Client device
 *
 * This function is used to get the PBS device that is being
 * used by the client.
 *
 * Returns: pbs_dev on success, ERR_PTR on failure
 */
struct pbs_dev *get_pbs_client_device(struct device *dev)
{
	struct device_node *pbs_dev_node;
	struct pbs_dev *pbs;

	pbs_dev_node = of_parse_phandle(dev->of_node, "qcom,pbs-client", 0);
	if (!pbs_dev_node) {
		pr_err("Missing qcom,pbs-client property\n");
		return ERR_PTR(-ENODEV);
	}

	mutex_lock(&pbs_list_lock);
	list_for_each_entry(pbs, &pbs_dev_list, link) {
		if (pbs_dev_node == pbs->dev_node) {
			of_node_put(pbs_dev_node);
			mutex_unlock(&pbs_list_lock);
			return pbs;
		}
	}
	mutex_unlock(&pbs_list_lock);

	pr_debug("Unable to find PBS dev_node\n");
	of_node_put(pbs_dev_node);
	return ERR_PTR(-EPROBE_DEFER);
}
EXPORT_SYMBOL(get_pbs_client_device);

static int qcom_pbs_probe(struct platform_device *pdev)
{
	struct pbs_dev *pbs;
	u32 val;
	int ret;

	pbs = devm_kzalloc(&pdev->dev, sizeof(*pbs), GFP_KERNEL);
	if (!pbs)
		return -ENOMEM;

	pbs->dev = &pdev->dev;
	pbs->dev_node = pdev->dev.of_node;
	pbs->regmap = dev_get_regmap(pbs->dev->parent, NULL);
	if (!pbs->regmap) {
		dev_err(pbs->dev, "Couldn't get parent's regmap\n");
		return -EINVAL;
	}

	ret = device_property_read_u32(pbs->dev, "reg", &val);
	if (ret < 0) {
		dev_err(pbs->dev, "Couldn't find reg, ret = %d\n", ret);
		return ret;
	}

	pbs->base = val;
	mutex_init(&pbs->lock);

	platform_set_drvdata(pdev, pbs);

	mutex_lock(&pbs_list_lock);
	list_add(&pbs->link, &pbs_dev_list);
	mutex_unlock(&pbs_list_lock);

	return 0;
}

static int qcom_pbs_remove(struct platform_device *pdev)
{
	struct pbs_dev *pbs = platform_get_drvdata(pdev);

	mutex_lock(&pbs_list_lock);
	list_del(&pbs->link);
	mutex_unlock(&pbs_list_lock);

	return 0;
}

static const struct of_device_id qcom_pbs_match_table[] = {
	{ .compatible = "qcom,pbs" },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_pbs_match_table);

static struct platform_driver qcom_pbs_driver = {
	.driver = {
		.name		= "qcom-pbs",
		.of_match_table	= qcom_pbs_match_table,
	},
	.probe = qcom_pbs_probe,
	.remove = qcom_pbs_remove,
};
module_platform_driver(qcom_pbs_driver)

MODULE_DESCRIPTION("QCOM PBS DRIVER");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:qcom-pbs");
