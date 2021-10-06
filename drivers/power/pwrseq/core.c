// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021 (c) Linaro Ltd.
 * Author: Dmitry Baryshkov <dmitry.baryshkov@linaro.org>
 *
 * Based on phy-core.c:
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com
 */

#include <linux/device.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pwrseq/consumer.h>
#include <linux/pwrseq/driver.h>
#include <linux/pwrseq/fallback.h>
#include <linux/slab.h>

#define	to_pwrseq(a)	(container_of((a), struct pwrseq, dev))

static DEFINE_IDA(pwrseq_ida);
static DEFINE_MUTEX(pwrseq_provider_mutex);
static LIST_HEAD(pwrseq_provider_list);

/**
 * struct pwrseq_provider - a 
 */
struct pwrseq_provider {
	struct device		*dev;
	struct module		*owner;
	struct list_head	list;
	void			*data;
	struct pwrseq * (*of_xlate)(void *data, struct of_phandle_args *args);
};

/**
 * pwrseq_put() - release the pwrseq
 * @pwrseq: the pwrseq returned by pwrseq_get()
 *
 * Releases a refcount on the pwrseq instance received from pwrseq_get().
 */
void pwrseq_put(struct pwrseq *pwrseq)
{
	module_put(pwrseq->owner);
	put_device(&pwrseq->dev);
}
EXPORT_SYMBOL_GPL(pwrseq_put);

static struct pwrseq_provider *of_pwrseq_provider_lookup(struct device_node *node)
{
	struct pwrseq_provider *pwrseq_provider;

	list_for_each_entry(pwrseq_provider, &pwrseq_provider_list, list) {
		if (pwrseq_provider->dev->of_node == node)
			return pwrseq_provider;
	}

	return ERR_PTR(-EPROBE_DEFER);
}

static struct pwrseq *_of_pwrseq_get(struct device *dev, const char *id)
{
	struct pwrseq_provider *pwrseq_provider;
	struct pwrseq *pwrseq;
	struct of_phandle_args args;
	char prop_name[64]; /* 64 is max size of property name */
	int ret;

	snprintf(prop_name, sizeof(prop_name), "%s-pwrseq", id);
	ret = of_parse_phandle_with_args(dev->of_node, prop_name, "#pwrseq-cells", 0, &args);

	/*
	 * Parsing failed. Try locating old bindings for mmc-pwrseq, which did
	 * not use #pwrseq-cells.
	 */
	if (ret == -EINVAL && !strcmp(id, "mmc"))
		ret = of_parse_phandle_with_args(dev->of_node, prop_name, NULL, 0, &args);

	if (ret == -ENOENT)
		return NULL;
	else if (ret < 0)
		return ERR_PTR(ret);

	mutex_lock(&pwrseq_provider_mutex);
	pwrseq_provider = of_pwrseq_provider_lookup(args.np);
	if (IS_ERR(pwrseq_provider) || !try_module_get(pwrseq_provider->owner)) {
		pwrseq = ERR_PTR(-EPROBE_DEFER);
		goto out_unlock;
	}

	if (!of_device_is_available(args.np)) {
		dev_warn(pwrseq_provider->dev, "Requested pwrseq is disabled\n");
		pwrseq = ERR_PTR(-ENODEV);
		goto out_put_module;
	}

	pwrseq = pwrseq_provider->of_xlate(pwrseq_provider->data, &args);

out_put_module:
	module_put(pwrseq_provider->owner);

out_unlock:
	mutex_unlock(&pwrseq_provider_mutex);
	of_node_put(args.np);

	return pwrseq;
}

/**
 * pwrseq_get() - lookup and obtain a reference to a pwrseq
 * @dev: device for which to get the pwrseq
 * @id: name of the pwrseq from device's point of view
 *
 * Returns the pwrseq instance, after getting a refcount to it; or
 * NULL if there is no such pwrseq. The caller is responsible for
 * calling pwrseq_put() to release that count.
 */
struct pwrseq * pwrseq_get(struct device *dev, const char *id)
{
	struct pwrseq *pwrseq;

	pwrseq = _of_pwrseq_get(dev, id);
	if (pwrseq == NULL)
		pwrseq = pwrseq_fallback_get(dev, id);
	if (IS_ERR_OR_NULL(pwrseq))
		return pwrseq;

	if (!try_module_get(pwrseq->owner))
		return ERR_PTR(-EPROBE_DEFER);

	get_device(&pwrseq->dev);

	return pwrseq;
}
EXPORT_SYMBOL_GPL(pwrseq_get);

static void devm_pwrseq_release(struct device *dev, void *res)
{
	struct pwrseq *pwrseq = *(struct pwrseq **)res;

	pwrseq_put(pwrseq);
}

/**
 * devm_pwrseq_get() - lookup and obtain a reference to a pwrseq
 * @dev: device for which to get the pwrseq
 * @id: name of the pwrseq from device's point of view
 *
 * Devres-managed variant of pwrseq_get().
 * Returns the pwrseq instance, after getting a refcount to it; or
 * NULL if there is no such pwrseq. Gets the pwrseq using pwrseq_get(), and
 * associates it with the a device using devres. On driver detach, returned
 * pwrseq is automatically put using pwrseq_put(), removing the need to call it
 * manually.
 */
struct pwrseq * devm_pwrseq_get(struct device *dev, const char *id)
{
	struct pwrseq **ptr, *pwrseq;

	ptr = devres_alloc(devm_pwrseq_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	pwrseq = pwrseq_get(dev, id);
	if (!IS_ERR_OR_NULL(pwrseq)) {
		*ptr = pwrseq;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return pwrseq;
}
EXPORT_SYMBOL_GPL(devm_pwrseq_get);

/**
 * pwrseq_pre_power_on() - perform pre-power on actions
 * @pwrseq: pwrseq instance
 *
 * Perform pre-powering on actions, like pulling reset pin.  This function
 * should be called before device is being powered on. Typical usage would
 * include MMC cards, where pwrseq subsystem is combined with the MMC power
 * controls.
 * In most cases there is no need to call it directly, use
 * @pwrseq_full_power_on() instead.
 */
int pwrseq_pre_power_on(struct pwrseq *pwrseq)
{
	if (pwrseq && pwrseq->ops->pre_power_on)
		return pwrseq->ops->pre_power_on(pwrseq);

	return 0;
}
EXPORT_SYMBOL_GPL(pwrseq_pre_power_on);

/**
 * pwrseq_power_on() - power on the device
 * @pwrseq: pwrseq instance
 *
 * Power on the device and perform post-power on actions, like pulling reset
 * or enable pin. In most cases there is no need to call it directly, use
 * @pwrseq_full_power_on() instead.
 */
int pwrseq_power_on(struct pwrseq *pwrseq)
{
	if (pwrseq && pwrseq->ops->power_on)
		return pwrseq->ops->power_on(pwrseq);

	return 0;
}
EXPORT_SYMBOL_GPL(pwrseq_power_on);

/**
 * pwrseq_power_off() - power off the device
 * @pwrseq: pwrseq instance
 *
 * Power off the device clearly.
 */
void pwrseq_power_off(struct pwrseq *pwrseq)
{
	if (pwrseq && pwrseq->ops->power_off)
		pwrseq->ops->power_off(pwrseq);
}
EXPORT_SYMBOL_GPL(pwrseq_power_off);

/**
 * pwrseq_reset() - reset powered device
 * @pwrseq: pwrseq instance
 *
 * Reset the device controlled by the power sequencer.
 */
void pwrseq_reset(struct pwrseq *pwrseq)
{
	if (pwrseq && pwrseq->ops->reset)
		pwrseq->ops->reset(pwrseq);
}
EXPORT_SYMBOL_GPL(pwrseq_reset);

static void pwrseq_dev_release(struct device *dev)
{
	struct pwrseq *pwrseq = to_pwrseq(dev);

	ida_free(&pwrseq_ida, pwrseq->id);
	of_node_put(dev->of_node);
	kfree(pwrseq);
}

static struct class pwrseq_class = {
	.name = "pwrseq",
	.dev_release = pwrseq_dev_release,
};

/**
 * __pwrseq_create() - internal helper for pwrseq_create
 * @dev: parent device
 * @owner: the module providing callbacks.
 * @ops: pwrseq device callbacks
 *
 * This is an internal helper for pwrseq_create which should not be called
 * directly.
 *
 * Return: created instance or the wrapped error code.
 */
struct pwrseq *__pwrseq_create(struct device *dev, struct module *owner, const struct pwrseq_ops *ops)
{
	struct pwrseq *pwrseq;
	int ret;

	if (WARN_ON(!dev))
		return ERR_PTR(-EINVAL);

	pwrseq = kzalloc(sizeof(*pwrseq), GFP_KERNEL);
	if (!pwrseq)
		return ERR_PTR(-ENOMEM);

	ret = ida_alloc(&pwrseq_ida, GFP_KERNEL);
	if (ret < 0)
		goto free_pwrseq;

	pwrseq->id = ret;

	device_initialize(&pwrseq->dev);

	pwrseq->dev.class = &pwrseq_class;
	pwrseq->dev.parent = dev;
	pwrseq->dev.of_node = of_node_get(dev->of_node);
	pwrseq->ops = ops;
	pwrseq->owner = owner;

	ret = dev_set_name(&pwrseq->dev, "pwrseq-%s.%u", dev_name(dev), pwrseq->id);
	if (ret)
		goto put_dev;

	ret = device_add(&pwrseq->dev);
	if (ret)
		goto put_dev;

	return pwrseq;

put_dev:
	/* will call pwrseq_dev_release() to free resources */
	put_device(&pwrseq->dev);

	return ERR_PTR(ret);

free_pwrseq:
	kfree(pwrseq);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(__pwrseq_create);

void pwrseq_destroy(struct pwrseq *pwrseq)
{
	device_unregister(&pwrseq->dev);
}
EXPORT_SYMBOL_GPL(pwrseq_destroy);

static void devm_pwrseq_destroy(struct device *dev, void *res)
{
	struct pwrseq *pwrseq = *(struct pwrseq **)res;

	pwrseq_destroy(pwrseq);
}

/**
 * __devm_pwrseq_create() - devres-managed version of __pwrseq_create
 * @dev: parent device
 * @owner: the module providing callbacks.
 * @ops: pwrseq device callbacks
 *
 * This is the devres-managed version of __pwrseq_create(). It is an internal
 * helper which should not be called directly.
 *
 * Return: created instance or the wrapped error code.
 */
struct pwrseq *__devm_pwrseq_create(struct device *dev, struct module *owner, const struct pwrseq_ops *ops)
{
	struct pwrseq **ptr, *pwrseq;

	ptr = devres_alloc(devm_pwrseq_destroy, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	pwrseq = __pwrseq_create(dev, owner, ops);
	if (!IS_ERR(pwrseq)) {
		*ptr = pwrseq;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return pwrseq;
}
EXPORT_SYMBOL_GPL(__devm_pwrseq_create);

/**
 * __of_pwrseq_provider_register - internal version of of_pwrseq_provider_register
 * @dev: handling device
 * @owner: the module providing callbacks.
 * @xlate: xlate function returning pwrseq instance corresponding to OF args
 * @data: provider-specific data to be passed to xlate function
 *
 * This is an internal helper of of_pwrseq_provider_register, it should not be
 * called directly.
 */
struct pwrseq_provider *__of_pwrseq_provider_register(struct device *dev,
	struct module *owner,
	struct pwrseq * (*of_xlate)(void *data,
				    struct of_phandle_args *args),
	void *data)
{
	struct pwrseq_provider *pwrseq_provider;

	pwrseq_provider = kzalloc(sizeof(*pwrseq_provider), GFP_KERNEL);
	if (!pwrseq_provider)
		return ERR_PTR(-ENOMEM);

	if (!fwnode_property_present(dev->fwnode, "#pwrseq-cells"))
		dev_warn(dev, "no #pwrseq-cells property found, please add the property to the provider\n");

	pwrseq_provider->dev = dev;
	pwrseq_provider->owner = owner;
	pwrseq_provider->of_xlate = of_xlate;
	pwrseq_provider->data = data;

	mutex_lock(&pwrseq_provider_mutex);
	list_add_tail(&pwrseq_provider->list, &pwrseq_provider_list);
	mutex_unlock(&pwrseq_provider_mutex);

	return pwrseq_provider;
}
EXPORT_SYMBOL_GPL(__of_pwrseq_provider_register);

/**
 * of_pwrseq_provider_unregister() - unregister pwrseq provider
 * @pwrseq_provider: pwrseq provider to unregister
 *
 * Unregister pwrseq provider previously registered by of_pwrseq_provider_register().
 */
void of_pwrseq_provider_unregister(struct pwrseq_provider *pwrseq_provider)
{
	if (IS_ERR(pwrseq_provider))
		return;

	mutex_lock(&pwrseq_provider_mutex);
	list_del(&pwrseq_provider->list);
	kfree(pwrseq_provider);
	mutex_unlock(&pwrseq_provider_mutex);
}
EXPORT_SYMBOL_GPL(of_pwrseq_provider_unregister);

static void devm_pwrseq_provider_unregister(struct device *dev, void *res)
{
	struct pwrseq_provider *pwrseq_provider = *(struct pwrseq_provider **)res;

	of_pwrseq_provider_unregister(pwrseq_provider);
}

/**
 * __devm_of_pwrseq_provider_register - internal version of devm_of_pwrseq_provider_register
 * @dev: handling device
 * @owner: the module providing callbacks.
 * @xlate: xlate function returning pwrseq instance corresponding to OF args
 * @data: provider-specific data to be passed to xlate function
 *
 * This is an internal helper of devm_of_pwrseq_provider_register, it should
 * not be called directly.
 */
struct pwrseq_provider *__devm_of_pwrseq_provider_register(struct device *dev,
	struct module *owner,
	struct pwrseq * (*of_xlate)(void *data,
				    struct of_phandle_args *args),
	void *data)
{
	struct pwrseq_provider **ptr, *pwrseq_provider;

	ptr = devres_alloc(devm_pwrseq_provider_unregister, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	pwrseq_provider = __of_pwrseq_provider_register(dev, owner, of_xlate, data);
	if (!IS_ERR(pwrseq_provider)) {
		*ptr = pwrseq_provider;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return pwrseq_provider;
}
EXPORT_SYMBOL_GPL(__devm_of_pwrseq_provider_register);

/**
 * of_pwrseq_xlate_single() - returns the pwrseq instance from pwrseq provider using single index
 * @data: the pwrseq provider data, struct pwrseq_onecell_data
 * @args: of_phandle_args containing single integer index
 *
 * Intended to be used by pwrseq provider for the common case where
 * #pwrseq-cells is 1. It will return corresponding pwrseq instance.
 */
struct pwrseq *of_pwrseq_xlate_onecell(void *data, struct of_phandle_args *args)
{
	struct pwrseq_onecell_data *pwrseq_data = data;
	unsigned int idx;

	if (args->args_count != 1)
		return ERR_PTR(-EINVAL);

	idx = args->args[0];
	if (idx >= pwrseq_data->num) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return pwrseq_data->pwrseqs[idx];
}
EXPORT_SYMBOL_GPL(of_pwrseq_xlate_onecell);

static int __init pwrseq_core_init(void)
{
	return class_register(&pwrseq_class);
}
device_initcall(pwrseq_core_init);
