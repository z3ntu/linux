// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Linaro Ltd
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "smd-rpm.h"
#include "icc-rpm.h"

static int qcom_rpm_send_bw(struct device *dev, bool master,
			    int rpm_id, u64 bandwidth)
{
	int ret;

	if (rpm_id == -1)
		return 0;

	ret = qcom_icc_rpm_smd_send(QCOM_SMD_RPM_ACTIVE_STATE,
				    master ?
				    RPM_BUS_MASTER_REQ :
				    RPM_BUS_SLAVE_REQ,
				    rpm_id,
				    icc_units_to_bps(bandwidth));
	if (ret)
		dev_err(dev, "Set bandwidth failed (%s_id=%d): error %d\n",
			master ? "mas" : "slv", rpm_id, ret);

	dev_vdbg(dev, "Set bandwidth (%s_id=%d): %llu\n",
		master ? "mas" : "slv", rpm_id, bandwidth);

	return ret;
}

static int qcom_node_update_bw(struct icc_node *node)
{
	struct qcom_icc_provider *qp;
	struct qcom_icc_node *qn;
	struct icc_provider *provider;
	struct icc_node *n;
	struct device *dev;
	u64 rate;
	u32 agg_avg = 0;
	u32 agg_peak = 0;
	int ret, i;

	qn = node->data;
	provider = node->provider;
	dev = provider->dev;
	qp = to_qcom_provider(provider);

	/* send bandwidth request message to the RPM processor */
	if (node->avg_bw != qn->applied_avg) {
		ret = qcom_rpm_send_bw(dev, true, qn->mas_rpm_id,
				       node->avg_bw);
		if (ret)
			return ret;

		ret = qcom_rpm_send_bw(dev, false, qn->slv_rpm_id,
				       node->avg_bw);
		if (ret)
			return ret;

		qn->applied_avg = node->avg_bw;
	}

	/* check if we already changed bus rate for this settings */
	if (qn->applied_bus_avg == node->avg_bw &&
	    qn->applied_bus_peak == node->peak_bw)
		return 0;

	/* aggregate provider bandwidth for bus rate calculation */
	list_for_each_entry(n, &provider->nodes, node_list) {
		struct qcom_icc_node *qn = n->data;

		agg_avg += n->avg_bw / qn->buswidth;
		agg_peak = max(agg_peak, n->peak_bw / qn->buswidth);
	}

	rate = icc_units_to_bps(max(agg_avg, agg_peak));

	if (qp->rate != rate) {
		for (i = 0; i < qp->num_clks; i++) {
			ret = clk_set_rate(qp->bus_clks[i].clk, rate);
			if (ret) {
				dev_err(dev, "Failed to set \"%s\" clk: %d\n",
					qp->bus_clks[i].id, ret);
				return ret;
			}
		}

		dev_vdbg(dev, "Set rate: %lld\n", rate);
		qp->rate = rate;
	}

	list_for_each_entry(n, &provider->nodes, node_list) {
		struct qcom_icc_node *qn = n->data;

		qn->applied_bus_avg = n->avg_bw;
		qn->applied_bus_peak = n->peak_bw;
	}

	return 0;
}

static int qcom_icc_set(struct icc_node *src, struct icc_node *dst)
{
	int ret;

	ret = qcom_node_update_bw(src);
	if (ret)
		return ret;

	if (dst && src != dst) {
		ret = qcom_node_update_bw(dst);
		if (ret)
			return ret;
	}

	return 0;
}

int qnoc_probe(struct platform_device *pdev, size_t cd_size, int cd_num,
	       const struct clk_bulk_data *cd)
{
	struct device *dev = &pdev->dev;
	const struct qcom_icc_desc *desc;
	struct icc_onecell_data *data;
	struct icc_provider *provider;
	struct qcom_icc_node **qnodes;
	struct qcom_icc_provider *qp;
	struct icc_node *node;
	size_t num_nodes, i;
	int ret;

	/* wait for the RPM proxy */
	if (!qcom_icc_rpm_smd_available())
		return -EPROBE_DEFER;

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	qnodes = desc->nodes;
	num_nodes = desc->num_nodes;

	qp = devm_kzalloc(dev, sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return -ENOMEM;

	data = devm_kzalloc(dev, struct_size(data, nodes, num_nodes),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	qp->bus_clks = devm_kmemdup(dev, cd, cd_size,
				    GFP_KERNEL);
	if (!qp->bus_clks)
		return -ENOMEM;

	qp->num_clks = cd_num;
	ret = devm_clk_bulk_get(dev, qp->num_clks, qp->bus_clks);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(qp->num_clks, qp->bus_clks);
	if (ret)
		return ret;

	provider = &qp->provider;
	INIT_LIST_HEAD(&provider->nodes);
	provider->dev = dev;
	provider->set = qcom_icc_set;
	provider->aggregate = icc_std_aggregate;
	provider->xlate = of_icc_xlate_onecell;
	provider->data = data;

	ret = icc_provider_add(provider);
	if (ret) {
		dev_err(dev, "error adding interconnect provider: %d\n", ret);
		clk_bulk_disable_unprepare(qp->num_clks, qp->bus_clks);
		return ret;
	}

	for (i = 0; i < num_nodes; i++) {
		size_t j;

		node = icc_node_create(qnodes[i]->id);
		if (IS_ERR(node)) {
			ret = PTR_ERR(node);
			goto err;
		}

		node->name = qnodes[i]->name;
		node->data = qnodes[i];
		icc_node_add(node, provider);

		for (j = 0; j < qnodes[i]->num_links; j++)
			icc_link_create(node, qnodes[i]->links[j]);

		data->nodes[i] = node;
	}
	data->num_nodes = num_nodes;

	platform_set_drvdata(pdev, qp);

	return 0;
err:
	icc_nodes_remove(provider);
	clk_bulk_disable_unprepare(qp->num_clks, qp->bus_clks);
	icc_provider_del(provider);

	return ret;
}
EXPORT_SYMBOL(qnoc_probe);

int qnoc_remove(struct platform_device *pdev)
{
	struct qcom_icc_provider *qp = platform_get_drvdata(pdev);

	icc_nodes_remove(&qp->provider);
	clk_bulk_disable_unprepare(qp->num_clks, qp->bus_clks);
	return icc_provider_del(&qp->provider);
}
EXPORT_SYMBOL(qnoc_remove);
