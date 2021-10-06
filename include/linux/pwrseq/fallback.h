/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Linaro Ltd.
 */

#ifndef __LINUX_PWRSEQ_FALLBACK_H__
#define __LINUX_PWRSEQ_FALLBACK_H__

#include <linux/list.h>

struct pwrseq;

struct device;
struct module;
struct of_device_id;

/**
 * struct pwrseq_fallback - structure providing fallback data/
 * @list: a list node for the fallback handlers
 * @owner: module containing fallback callback
 * @of_match_table: match table for this fallback
 *
 * Pwrseq fallback is a mechanism for handling backwards compatibility in the
 * case device tree was not updated to use proper pwrseq providers.
 *
 * In case the pwrseq instance is not registered, core will automatically try
 * locating and calling fallback getter. If the requesting device matches
 * against @of_match_table, the @get callback will be called to retrieve pwrseq
 * instance.
 *
 * The driver should fill of_match_table and @get fields only. @list and @owner
 * will be filled by the core code.
 */
struct pwrseq_fallback {
	struct list_head list;
	struct module *owner;

	const struct of_device_id *of_match_table;

	struct pwrseq *(*get)(struct device *dev, const char *id);
};

/* provider interface */

int __pwrseq_fallback_register(struct pwrseq_fallback *fallback, struct module *owner);

/**
 * pwrseq_fallback_register() - register fallback helper
 * @fallback - struct pwrseq_fallback to be registered
 *
 * Register pwrseq fallback handler to assist pwrseq core.
 */
#define pwrseq_fallback_register(fallback) __pwrseq_fallback_register(fallback, THIS_MODULE)

void pwrseq_fallback_unregister(struct pwrseq_fallback *fallback);

/* internal interface to be used by pwrseq core */
struct pwrseq *pwrseq_fallback_get(struct device *dev, const char *id);

#endif /* __LINUX_PWRSEQ_DRIVER_H__ */
