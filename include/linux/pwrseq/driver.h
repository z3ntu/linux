/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Linaro Ltd.
 */

#ifndef __LINUX_PWRSEQ_DRIVER_H__
#define __LINUX_PWRSEQ_DRIVER_H__

#include <linux/device.h>

struct pwrseq;

/**
 * struct pwrseq_ops - power sequencer operations.
 *
 * @pre_power_on: Perform pre powering operations (like ensuring that the
 *                device will be held in the reset)
 * @power_on: Power on the sequencer, making sure that the consumer
 *            devices can be operated
 * @power_off: Power off the sequencer, removing power from the comsumer
 *             device (if possible)
 * @reset: Reset the consumer device
 */
struct pwrseq_ops {
	int (*pre_power_on)(struct pwrseq *pwrseq);
	int (*power_on)(struct pwrseq *pwrseq);
	void (*power_off)(struct pwrseq *pwrseq);
	void (*reset)(struct pwrseq *pwrseq);
};

struct module;

/**
 * struct pwrseq - private pwrseq data
 *
 * Power sequencer device, one for each power sequencer.
 *
 * This should *not* be used directly by anything except the pwrseq core.
 */
struct pwrseq {
	struct device dev;
	const struct pwrseq_ops *ops;
	unsigned int id;
	struct module *owner;
};

struct pwrseq *__pwrseq_create(struct device *dev, struct module *owner, const struct pwrseq_ops *ops);
struct pwrseq *__devm_pwrseq_create(struct device *dev, struct module *owner, const struct pwrseq_ops *ops);

/**
 * pwrseq_create() - create pwrseq instance
 * @dev: parent device
 * @ops: pwrseq device callbacks
 *
 * Create new pwrseq instance parenting with @dev using provided @ops set of
 * callbacks. Create instance should be destroyed using @pwrseq_destroy().
 *
 * Return: created instance or the wrapped error code.
 */
#define pwrseq_create(dev, ops) __pwrseq_create((dev), THIS_MODULE, (ops))

/**
 * devm_pwrseq_create() - devres-managed version of pwrseq_create
 * @dev: parent device
 * @ops: pwrseq device callbacks
 *
 * This is the devres-managed version of pwrseq_create(). It creates new
 * pwrseq instance parenting with @dev using provided @ops set of
 * callbacks. Returned object is destroyed automatically, one must not call
 * pwrseq_destroy().
 *
 * Return: created instance or the wrapped error code.
 */
#define devm_pwrseq_create(dev, ops) __devm_pwrseq_create((dev), THIS_MODULE, (ops))

void pwrseq_destroy(struct pwrseq *pwrseq);

/**
 * pwrseq_set_data() - get drv-specific data for the pwrseq
 * @pwrseq: the pwrseq to get driver data for
 * @data: the data to set
 *
 * Sets the driver-specific data for the provided powrseq instance.
 */
static inline void pwrseq_set_drvdata(struct pwrseq *pwrseq, void *data)
{
	dev_set_drvdata(&pwrseq->dev, data);
}

/**
 * pwrseq_get_data() - get drv-specific data for the pwrseq
 * @pwrseq: the pwrseq to get driver data for
 *
 * Returns driver-specific data for the provided powrseq instance.
 */
static inline void *pwrseq_get_drvdata(struct pwrseq *pwrseq)
{
	return dev_get_drvdata(&pwrseq->dev);
}

/**
 * of_pwrseq_provider_register() - register OF pwrseq provider
 * @dev: handling device
 * @xlate: xlate function returning pwrseq instance corresponding to OF args
 * @data: provider-specific data to be passed to xlate function
 *
 * This macros registers OF-specific pwrseq provider. Pwrseq core will call
 * specified @xlate function to retrieve pwrseq instance corresponding to
 * device tree arguments. Returned pwrseq provider should be unregistered using
 * of_pwrseq_provider_unregister().
 */
#define of_pwrseq_provider_register(dev, xlate, data)	\
	__of_pwrseq_provider_register((dev), THIS_MODULE, (xlate), (data))

/**
 * devm_of_pwrseq_provider_register() - devres-managed version of of_pwrseq_provider_register
 * @dev: handling device
 * @xlate: xlate function returning pwrseq instance corresponding to OF args
 * @data: provider-specific data to be passed to xlate function
 *
 * This is a devres-managed version of of_pwrseq_provider_register().
 * This macros registers OF-specific pwrseq provider. Pwrseq core will call
 * specified @xlate function to retrieve pwrseq instance corresponding to
 * device tree arguments. Returned pwrseq provider is automatically
 * unregistered, without the need to call of_pwrseq_provider_unregister().
 */
#define devm_of_pwrseq_provider_register(dev, xlate, data)	\
	__devm_of_pwrseq_provider_register((dev), THIS_MODULE, (xlate), (data))

struct of_phandle_args;

struct pwrseq_provider *__of_pwrseq_provider_register(struct device *dev,
	struct module *owner,
	struct pwrseq * (*of_xlate)(void *data,
				    struct of_phandle_args *args),
	void *data);
struct pwrseq_provider *__devm_of_pwrseq_provider_register(struct device *dev,
	struct module *owner,
	struct pwrseq * (*of_xlate)(void *data,
				    struct of_phandle_args *args),
	void *data);
void of_pwrseq_provider_unregister(struct pwrseq_provider *pwrseq_provider);

/**
 * of_pwrseq_xlate_single() - returns the pwrseq instance from pwrseq provider
 * @data: the pwrseq provider data
 * @args: of_phandle_args (not used here)
 *
 * Intended to be used by pwrseq provider for the common case where
 * #pwrseq-cells is 0. For other cases where #pwrseq-cells is greater than '0',
 * the pwrseq provider should provide a custom of_xlate function that reads the
 * *args* and returns the appropriate pwrseq.
 */
static inline struct pwrseq *of_pwrseq_xlate_single(void *data,
						    struct of_phandle_args *args)
{
	return data;
}

/**
 * struct pwrseq_onecell_data - pwrseq data for of_pwrseq_xlate_onecell
 * @num: amount of instances in @owrseqs
 * @pwrseqs: array of pwrseq instances
 */
struct pwrseq_onecell_data {
	unsigned int num;
	struct pwrseq *pwrseqs[];
};

struct pwrseq *of_pwrseq_xlate_onecell(void *data, struct of_phandle_args *args);

#endif /* __LINUX_PWRSEQ_DRIVER_H__ */
