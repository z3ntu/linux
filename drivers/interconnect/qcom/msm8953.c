// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018-2020 Linaro Ltd
 * Based on msm8916.c (author: Georgi Djakov <georgi.djakov@linaro.org>)
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <dt-bindings/interconnect/qcom,msm8953.h>

#include "smd-rpm.h"
#include "icc-rpm.h"

#define RPM_BUS_MASTER_REQ			0x73616d62
#define RPM_BUS_SLAVE_REQ			0x766c7362

#define BIMC_BKE_ENA_REG(qport)			(0x8300 + (qport) * 0x4000)
#define BIMC_BKE_ENA_MASK			GENMASK(1, 0)
#define BIMC_BKE_ENA_SHIFT			0
#define BIMC_BKE_HEALTH_REG(qport, hlvl)	(0x8340 + (qport) * 0x4000 \
						+ (hlvl) * 4)
#define BIMC_BKE_HEALTH_LIMIT_CMDS_MASK		GENMASK(31, 31)
#define BIMC_BKE_HEALTH_LIMIT_CMDS_SHIFT	31
#define BIMC_BKE_HEALTH_AREQPRIO_MASK		GENMASK(9, 8)
#define BIMC_BKE_HEALTH_AREQPRIO_SHIFT		8
#define BIMC_BKE_HEALTH_PRIOLVL_MASK		GENMASK(1, 0)
#define BIMC_BKE_HEALTH_PRIOLVL_SHIFT		0

#define NOC_QOS_PRIO_REG(qport)			(0x7008 + (qport) * 0x1000)
#define NOC_QOS_PRIO_P0_MASK			GENMASK(1, 0)
#define NOC_QOS_PRIO_P0_SHIFT			0
#define NOC_QOS_PRIO_P1_MASK			GENMASK(3, 2)
#define NOC_QOS_PRIO_P1_SHIFT			2

#define NOC_QOS_MODE_REG(qport)			(0x700c + (qport) * 0x1000)
#define NOC_QOS_MODE_MASK			GENMASK(1, 0)
#define NOC_QOS_MODE_SHIFT			0
#define NOC_QOS_MODE_FIXED			0
#define NOC_QOS_MODE_LIMITER			1
#define NOC_QOS_MODE_BYPASS			2
#define NOC_QOS_MODE_REGULATOR			3

enum {
	QNOC_NODE_NONE = 0,
	QNOC_MASTER_AMPSS_M0,
	QNOC_MASTER_GRAPHICS_3D,
	QNOC_SNOC_BIMC_0_MAS,
	QNOC_SNOC_BIMC_2_MAS,
	QNOC_SNOC_BIMC_1_MAS,
	QNOC_MASTER_TCU_0,
	QNOC_SLAVE_EBI_CH0,
	QNOC_BIMC_SNOC_SLV,
	QNOC_MASTER_SPDM,
	QNOC_MASTER_BLSP_1,
	QNOC_MASTER_BLSP_2,
	QNOC_MASTER_USB3,
	QNOC_MASTER_CRYPTO_CORE0,
	QNOC_MASTER_SDCC_1,
	QNOC_MASTER_SDCC_2,
	QNOC_SNOC_PNOC_MAS,
	QNOC_PNOC_M_0,
	QNOC_PNOC_M_1,
	QNOC_PNOC_INT_1,
	QNOC_PNOC_INT_2,
	QNOC_PNOC_SLV_0,
	QNOC_PNOC_SLV_1,
	QNOC_PNOC_SLV_2,
	QNOC_PNOC_SLV_3,
	QNOC_PNOC_SLV_4,
	QNOC_PNOC_SLV_6,
	QNOC_PNOC_SLV_7,
	QNOC_PNOC_SLV_8,
	QNOC_PNOC_SLV_9,
	QNOC_SLAVE_SPDM_WRAPPER,
	QNOC_SLAVE_PDM,
	QNOC_SLAVE_TCSR,
	QNOC_SLAVE_SNOC_CFG,
	QNOC_SLAVE_TLMM,
	QNOC_SLAVE_MESSAGE_RAM,
	QNOC_SLAVE_BLSP_1,
	QNOC_SLAVE_BLSP_2,
	QNOC_SLAVE_PRNG,
	QNOC_SLAVE_CAMERA_CFG,
	QNOC_SLAVE_DISPLAY_CFG,
	QNOC_SLAVE_VENUS_CFG,
	QNOC_SLAVE_GRAPHICS_3D_CFG,
	QNOC_SLAVE_SDCC_1,
	QNOC_SLAVE_SDCC_2,
	QNOC_SLAVE_CRYPTO_0_CFG,
	QNOC_SLAVE_PMIC_ARB,
	QNOC_SLAVE_USB3,
	QNOC_SLAVE_IPA_CFG,
	QNOC_SLAVE_TCU,
	QNOC_PNOC_SNOC_SLV,
	QNOC_MASTER_QDSS_BAM,
	QNOC_BIMC_SNOC_MAS,
	QNOC_PNOC_SNOC_MAS,
	QNOC_MASTER_IPA,
	QNOC_MASTER_QDSS_ETR,
	QNOC_SNOC_QDSS_INT,
	QNOC_SNOC_INT_0,
	QNOC_SNOC_INT_1,
	QNOC_SNOC_INT_2,
	QNOC_SLAVE_APPSS,
	QNOC_SLAVE_WCSS,
	QNOC_SNOC_BIMC_1_SLV,
	QNOC_SLAVE_OCIMEM,
	QNOC_SNOC_PNOC_SLV,
	QNOC_SLAVE_QDSS_STM,
	QNOC_SLAVE_OCMEM_64,
	QNOC_SLAVE_LPASS,
	QNOC_MASTER_JPEG,
	QNOC_MASTER_MDP_PORT0,
	QNOC_MASTER_VIDEO_P0,
	QNOC_MASTER_VFE,
	QNOC_MASTER_VFE1,
	QNOC_MASTER_CPP,
	QNOC_SNOC_BIMC_0_SLV,
	QNOC_SNOC_BIMC_2_SLV,
	QNOC_SLAVE_CATS_128,
};

static const struct clk_bulk_data msm8953_bus_clocks[] = {
	{ .id = "bus" },
	{ .id = "bus_a" },
};

enum qos_mode {
	QOS_NONE,
	QOS_BYPASS,
	QOS_FIXED,
};

/**
 * struct msm8953_icc_node - Qualcomm specific interconnect nodes
 * @qport: the offset index into the masters QoS register space
 * @port0, @port1: priority low/high signal for NoC or prioity level for BIMC
 * @qos_mode: QoS mode to be programmed for this device.
 */
struct msm8953_icc_node {
	struct qcom_icc_node qn;
	u16 prio0;
	u16 prio1;
	u16 qport;
	enum qos_mode qos_mode;
};

static void msm8953_bimc_node_init(struct msm8953_icc_node *qn,
				  struct regmap* rmap);

static void msm8953_noc_node_init(struct msm8953_icc_node *qn,
				  struct regmap* rmap);

#define DEFINE_QNODE_AP(_name, _id, _qport, _buswidth, _qos_mode,	\
	      _prio0, _prio1, ...)					\
	      DEFINE_QNODE(_name, _id, _buswidth, -1, -1,  __VA_ARGS__)

#define DEFINE_QNODE_RPM(_name, _id, _qport, _buswidth,			\
		_mas_rpm_id, _slv_rpm_id, ...)				\
	      DEFINE_QNODE(_name, _id, _buswidth,			\
			   _mas_rpm_id, _slv_rpm_id,  __VA_ARGS__)

DEFINE_QNODE_AP(mas_apps_proc, QNOC_MASTER_AMPSS_M0, 0, 8, QOS_FIXED, 0, 0,
	QNOC_SLAVE_EBI_CH0, QNOC_BIMC_SNOC_SLV);
DEFINE_QNODE_AP(mas_oxili, QNOC_MASTER_GRAPHICS_3D, 2, 8, QOS_FIXED, 0, 0,
	QNOC_SLAVE_EBI_CH0, QNOC_BIMC_SNOC_SLV);
DEFINE_QNODE_AP(mas_snoc_bimc_0, QNOC_SNOC_BIMC_0_MAS, 3, 8, QOS_BYPASS, 0, 0,
	QNOC_SLAVE_EBI_CH0, QNOC_BIMC_SNOC_SLV);
DEFINE_QNODE_AP(mas_snoc_bimc_2, QNOC_SNOC_BIMC_2_MAS, 4, 8, QOS_BYPASS, 0, 0,
	QNOC_SLAVE_EBI_CH0, QNOC_BIMC_SNOC_SLV);
DEFINE_QNODE_RPM(mas_snoc_bimc_1, QNOC_SNOC_BIMC_1_MAS, 5, 8, 76, -1,
	QNOC_SLAVE_EBI_CH0);
DEFINE_QNODE_AP(mas_tcu_0, QNOC_MASTER_TCU_0, 6, 8, QOS_FIXED, 2, 2,
	QNOC_SLAVE_EBI_CH0, QNOC_BIMC_SNOC_SLV);
DEFINE_QNODE_RPM(slv_ebi, QNOC_SLAVE_EBI_CH0, 0, 8, -1, 0,
	QNOC_NODE_NONE);
DEFINE_QNODE_RPM(slv_bimc_snoc, QNOC_BIMC_SNOC_SLV, 0, 8, -1, 2,
	QNOC_BIMC_SNOC_MAS);

static struct qcom_icc_node *msm8953_bimc_nodes[] = {
	[MAS_APPS_PROC] = &mas_apps_proc,
	[MAS_OXILI] = &mas_oxili,
	[MAS_SNOC_BIMC_0] = &mas_snoc_bimc_0,
	[MAS_SNOC_BIMC_2] = &mas_snoc_bimc_2,
	[MAS_SNOC_BIMC_1] = &mas_snoc_bimc_1,
	[MAS_TCU_0] = &mas_tcu_0,
	[SLV_EBI] = &slv_ebi,
	[SLV_BIMC_SNOC] = &slv_bimc_snoc,
};

static struct qcom_icc_desc msm8953_bimc = {
	.nodes = msm8953_bimc_nodes,
	.num_nodes = ARRAY_SIZE(msm8953_bimc_nodes),
	//.node_qos_init = msm8953_bimc_node_init,
};

DEFINE_QNODE_AP(mas_spdm, QNOC_MASTER_SPDM, 0, 4, QOS_NONE, 0, 0,
	QNOC_PNOC_M_0);
DEFINE_QNODE_RPM(mas_blsp_1, QNOC_MASTER_BLSP_1, 0, 4, 41, -1,
	QNOC_PNOC_M_1);
DEFINE_QNODE_RPM(mas_blsp_2, QNOC_MASTER_BLSP_2, 0, 4, 39, -1,
	QNOC_PNOC_M_1);
DEFINE_QNODE_AP(mas_usb3, QNOC_MASTER_USB3, 11, 8, QOS_FIXED, 1, 1,
	QNOC_PNOC_INT_1);
DEFINE_QNODE_AP(mas_crypto, QNOC_MASTER_CRYPTO_CORE0, 0, 8, QOS_FIXED, 1, 1,
	QNOC_PNOC_INT_1);
DEFINE_QNODE_RPM(mas_sdcc_1, QNOC_MASTER_SDCC_1, 7, 8, 33, -1,
	QNOC_PNOC_INT_1);
DEFINE_QNODE_RPM(mas_sdcc_2, QNOC_MASTER_SDCC_2, 8, 8, 35, -1,
	QNOC_PNOC_INT_1);
DEFINE_QNODE_RPM(mas_snoc_pcnoc, QNOC_SNOC_PNOC_MAS, 9, 8, 77, -1,
	QNOC_PNOC_INT_2);
DEFINE_QNODE_AP(pcnoc_m_0, QNOC_PNOC_M_0, 5, 4, QOS_FIXED, 1, 1,
	QNOC_PNOC_INT_1);
DEFINE_QNODE_RPM(pcnoc_m_1, QNOC_PNOC_M_1, 6, 4, 88, 117,
	QNOC_PNOC_INT_1);
DEFINE_QNODE_RPM(pcnoc_int_1, QNOC_PNOC_INT_1, 0, 8, 86, 115,
	QNOC_PNOC_INT_2, QNOC_PNOC_SNOC_SLV);
DEFINE_QNODE_RPM(pcnoc_int_2, QNOC_PNOC_INT_2, 0, 8, 124, 184,
	QNOC_PNOC_SLV_1, QNOC_PNOC_SLV_2, QNOC_PNOC_SLV_0,
	QNOC_PNOC_SLV_4, QNOC_PNOC_SLV_6, QNOC_PNOC_SLV_7,
	QNOC_PNOC_SLV_8, QNOC_PNOC_SLV_9, QNOC_SLAVE_TCU,
	QNOC_SLAVE_GRAPHICS_3D_CFG, QNOC_PNOC_SLV_3);
DEFINE_QNODE_RPM(pcnoc_s_0, QNOC_PNOC_SLV_0, 0, 4, 89, 118,
	QNOC_SLAVE_PDM, QNOC_SLAVE_SPDM_WRAPPER);
DEFINE_QNODE_RPM(pcnoc_s_1, QNOC_PNOC_SLV_1, 0, 4, 90, 119,
	QNOC_SLAVE_TCSR);
DEFINE_QNODE_RPM(pcnoc_s_2, QNOC_PNOC_SLV_2, 0, 4, 91, 120,
	QNOC_SLAVE_SNOC_CFG);
DEFINE_QNODE_RPM(pcnoc_s_3, QNOC_PNOC_SLV_3, 0, 4, 92, 121,
	QNOC_SLAVE_TLMM, QNOC_SLAVE_PRNG, QNOC_SLAVE_BLSP_1,
	QNOC_SLAVE_BLSP_2, QNOC_SLAVE_MESSAGE_RAM);
DEFINE_QNODE_AP(pcnoc_s_4, QNOC_PNOC_SLV_4, 0, 4, QOS_NONE, 0, 0,
	QNOC_SLAVE_CAMERA_CFG, QNOC_SLAVE_DISPLAY_CFG, QNOC_SLAVE_VENUS_CFG);
DEFINE_QNODE_RPM(pcnoc_s_6, QNOC_PNOC_SLV_6, 0, 4, 94, 123,
	QNOC_SLAVE_CRYPTO_0_CFG, QNOC_SLAVE_SDCC_2, QNOC_SLAVE_SDCC_1);
DEFINE_QNODE_RPM(pcnoc_s_7, QNOC_PNOC_SLV_7, 0, 4, 95, 124,
	QNOC_SLAVE_PMIC_ARB);
DEFINE_QNODE_AP(pcnoc_s_8, QNOC_PNOC_SLV_8, 0, 4, QOS_NONE, 0, 0,
	QNOC_SLAVE_USB3);
DEFINE_QNODE_AP(pcnoc_s_9, QNOC_PNOC_SLV_9, 0, 4, QOS_NONE, 0, 0,
	QNOC_SLAVE_IPA_CFG);
DEFINE_QNODE_AP(slv_spdm, QNOC_SLAVE_SPDM_WRAPPER, 0, 4, QOS_NONE, 0, 0,
	QNOC_NODE_NONE);
DEFINE_QNODE_RPM(slv_pdm, QNOC_SLAVE_PDM, 0, 4, -1, 41,
	QNOC_NODE_NONE);
DEFINE_QNODE_RPM(slv_tcsr, QNOC_SLAVE_TCSR, 0, 4, -1, 50,
	QNOC_NODE_NONE);
DEFINE_QNODE_RPM(slv_snoc_cfg, QNOC_SLAVE_SNOC_CFG, 0, 4, -1, 70,
	QNOC_NODE_NONE);
DEFINE_QNODE_RPM(slv_tlmm, QNOC_SLAVE_TLMM, 0, 4, -1, 51,
	QNOC_NODE_NONE);
DEFINE_QNODE_RPM(slv_message_ram, QNOC_SLAVE_MESSAGE_RAM, 0, 4, -1, 55,
	QNOC_NODE_NONE);
DEFINE_QNODE_RPM(slv_blsp_1, QNOC_SLAVE_BLSP_1, 0, 4, -1, 39,
	QNOC_NODE_NONE);
DEFINE_QNODE_RPM(slv_blsp_2, QNOC_SLAVE_BLSP_2, 0, 4, -1, 37,
	QNOC_NODE_NONE);
DEFINE_QNODE_RPM(slv_prng, QNOC_SLAVE_PRNG, 0, 4, -1, 44,
	QNOC_NODE_NONE);
DEFINE_QNODE_AP(slv_camera_ss_cfg, QNOC_SLAVE_CAMERA_CFG, 0, 4, QOS_NONE, 0, 0,
	QNOC_NODE_NONE);
DEFINE_QNODE_AP(slv_disp_ss_cfg, QNOC_SLAVE_DISPLAY_CFG, 0, 4, QOS_NONE, 0, 0,
	QNOC_NODE_NONE);
DEFINE_QNODE_AP(slv_venus_cfg, QNOC_SLAVE_VENUS_CFG, 0, 4, QOS_NONE, 0, 0,
	QNOC_NODE_NONE);
DEFINE_QNODE_AP(slv_gpu_cfg, QNOC_SLAVE_GRAPHICS_3D_CFG, 0, 8, QOS_NONE, 0, 0,
	QNOC_NODE_NONE);
DEFINE_QNODE_RPM(slv_sdcc_1, QNOC_SLAVE_SDCC_1, 0, 4, -1, 31,
	QNOC_NODE_NONE);
DEFINE_QNODE_RPM(slv_sdcc_2, QNOC_SLAVE_SDCC_2, 0, 4, -1, 33,
	QNOC_NODE_NONE);
DEFINE_QNODE_AP(slv_crypto_0_cfg, QNOC_SLAVE_CRYPTO_0_CFG, 0, 4, QOS_NONE, 0, 0,
	QNOC_NODE_NONE);
DEFINE_QNODE_RPM(slv_pmic_arb, QNOC_SLAVE_PMIC_ARB, 0, 4, -1, 59,
	QNOC_NODE_NONE);
DEFINE_QNODE_AP(slv_usb3, QNOC_SLAVE_USB3, 0, 4, QOS_NONE, 0, 0,
	QNOC_NODE_NONE);
DEFINE_QNODE_AP(slv_ipa_cfg, QNOC_SLAVE_IPA_CFG, 0, 4, QOS_NONE, 0, 0,
	QNOC_NODE_NONE);
DEFINE_QNODE_AP(slv_tcu, QNOC_SLAVE_TCU, 0, 8, QOS_NONE, 0, 0,
	QNOC_NODE_NONE);
DEFINE_QNODE_RPM(slv_pcnoc_snoc, QNOC_PNOC_SNOC_SLV, 0, 8, -1, 45,
	QNOC_PNOC_SNOC_MAS);

static struct qcom_icc_node *msm8953_pcnoc_nodes[] = {
	[MAS_SPDM] = &mas_spdm,
	[MAS_BLSP_1] = &mas_blsp_1,
	[MAS_BLSP_2] = &mas_blsp_2,
	[MAS_USB3] = &mas_usb3,
	[MAS_CRYPTO] = &mas_crypto,
	[MAS_SDCC_1] = &mas_sdcc_1,
	[MAS_SDCC_2] = &mas_sdcc_2,
	[MAS_SNOC_PCNOC] = &mas_snoc_pcnoc,
	[PCNOC_M_0] = &pcnoc_m_0,
	[PCNOC_M_1] = &pcnoc_m_1,
	[PCNOC_INT_1] = &pcnoc_int_1,
	[PCNOC_INT_2] = &pcnoc_int_2,
	[PCNOC_S_0] = &pcnoc_s_0,
	[PCNOC_S_1] = &pcnoc_s_1,
	[PCNOC_S_2] = &pcnoc_s_2,
	[PCNOC_S_3] = &pcnoc_s_3,
	[PCNOC_S_4] = &pcnoc_s_4,
	[PCNOC_S_6] = &pcnoc_s_6,
	[PCNOC_S_7] = &pcnoc_s_7,
	[PCNOC_S_8] = &pcnoc_s_8,
	[PCNOC_S_9] = &pcnoc_s_9,
	[SLV_SPDM] = &slv_spdm,
	[SLV_PDM] = &slv_pdm,
	[SLV_TCSR] = &slv_tcsr,
	[SLV_SNOC_CFG] = &slv_snoc_cfg,
	[SLV_TLMM] = &slv_tlmm,
	[SLV_MESSAGE_RAM] = &slv_message_ram,
	[SLV_BLSP_1] = &slv_blsp_1,
	[SLV_BLSP_2] = &slv_blsp_2,
	[SLV_PRNG] = &slv_prng,
	[SLV_CAMERA_SS_CFG] = &slv_camera_ss_cfg,
	[SLV_DISP_SS_CFG] = &slv_disp_ss_cfg,
	[SLV_VENUS_CFG] = &slv_venus_cfg,
	[SLV_GPU_CFG] = &slv_gpu_cfg,
	[SLV_SDCC_1] = &slv_sdcc_1,
	[SLV_SDCC_2] = &slv_sdcc_2,
	[SLV_CRYPTO_0_CFG] = &slv_crypto_0_cfg,
	[SLV_PMIC_ARB] = &slv_pmic_arb,
	[SLV_USB3] = &slv_usb3,
	[SLV_IPA_CFG] = &slv_ipa_cfg,
	[SLV_TCU] = &slv_tcu,
	[SLV_PCNOC_SNOC] = &slv_pcnoc_snoc,
};

static struct qcom_icc_desc msm8953_pcnoc = {
	.nodes = msm8953_pcnoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8953_pcnoc_nodes),
	//.node_qos_init = msm8953_noc_node_init,
};

DEFINE_QNODE_AP(mas_qdss_bam, QNOC_MASTER_QDSS_BAM, 11, 4, QOS_FIXED, 1, 1,
	QNOC_SNOC_QDSS_INT);
DEFINE_QNODE_RPM(mas_bimc_snoc, QNOC_BIMC_SNOC_MAS, 0, 8, 21, -1,
	QNOC_SNOC_INT_0, QNOC_SNOC_INT_1, QNOC_SNOC_INT_2);
DEFINE_QNODE_RPM(mas_pcnoc_snoc, QNOC_PNOC_SNOC_MAS, 5, 8, 29, -1,
	QNOC_SNOC_INT_0, QNOC_SNOC_INT_1, QNOC_SNOC_BIMC_1_SLV);
DEFINE_QNODE_AP(mas_ipa, QNOC_MASTER_IPA, 14, 8, QOS_FIXED, 0, 0,
	QNOC_SNOC_INT_0, QNOC_SNOC_INT_1, QNOC_SNOC_BIMC_1_SLV);
DEFINE_QNODE_AP(mas_qdss_etr, QNOC_MASTER_QDSS_ETR, 10, 8, QOS_FIXED, 1, 1,
	QNOC_SNOC_QDSS_INT);
DEFINE_QNODE_AP(qdss_int, QNOC_SNOC_QDSS_INT, 0, 8, QOS_NONE, 0, 0,
	QNOC_SNOC_INT_1, QNOC_SNOC_BIMC_1_SLV);
DEFINE_QNODE_AP(snoc_int_0, QNOC_SNOC_INT_0, 0, 8, QOS_NONE, 0, 0,
	QNOC_SLAVE_LPASS, QNOC_SLAVE_WCSS, QNOC_SLAVE_APPSS);
DEFINE_QNODE_RPM(snoc_int_1, QNOC_SNOC_INT_1, 0, 8, 100, 131,
	QNOC_SLAVE_QDSS_STM, QNOC_SLAVE_OCIMEM, QNOC_SNOC_PNOC_SLV);
DEFINE_QNODE_AP(snoc_int_2, QNOC_SNOC_INT_2, 0, 8, QOS_NONE, 0, 0,
	QNOC_SLAVE_CATS_128, QNOC_SLAVE_OCMEM_64);
DEFINE_QNODE_AP(slv_kpss_ahb, QNOC_SLAVE_APPSS, 0, 4, QOS_NONE, 0, 0,
	QNOC_NODE_NONE);
DEFINE_QNODE_AP(slv_wcss, QNOC_SLAVE_WCSS, 0, 4, QOS_NONE, 0, 0,
	QNOC_NODE_NONE);
DEFINE_QNODE_RPM(slv_snoc_bimc_1, QNOC_SNOC_BIMC_1_SLV, 0, 8, -1, 104,
	QNOC_SNOC_BIMC_1_MAS);
DEFINE_QNODE_RPM(slv_imem, QNOC_SLAVE_OCIMEM, 0, 8, -1, 26,
	QNOC_NODE_NONE);
DEFINE_QNODE_RPM(slv_snoc_pcnoc, QNOC_SNOC_PNOC_SLV, 0, 8, -1, 28,
	QNOC_SNOC_PNOC_MAS);
DEFINE_QNODE_RPM(slv_qdss_stm, QNOC_SLAVE_QDSS_STM, 0, 4, -1, 30,
	QNOC_NODE_NONE);
DEFINE_QNODE_AP(slv_cats_1, QNOC_SLAVE_OCMEM_64, 0, 8, QOS_NONE, 0, 0,
	QNOC_NODE_NONE);
DEFINE_QNODE_AP(slv_lpass, QNOC_SLAVE_LPASS, 0, 4, QOS_NONE, 0, 0,
	QNOC_NODE_NONE);

static struct qcom_icc_node *msm8953_snoc_nodes[] = {
	[MAS_QDSS_BAM] = &mas_qdss_bam,
	[MAS_BIMC_SNOC] = &mas_bimc_snoc,
	[MAS_PCNOC_SNOC] = &mas_pcnoc_snoc,
	[MAS_IPA] = &mas_ipa,
	[MAS_QDSS_ETR] = &mas_qdss_etr,
	[QDSS_INT] = &qdss_int,
	[SNOC_INT_0] = &snoc_int_0,
	[SNOC_INT_1] = &snoc_int_1,
	[SNOC_INT_2] = &snoc_int_2,
	[SLV_KPSS_AHB] = &slv_kpss_ahb,
	[SLV_WCSS] = &slv_wcss,
	[SLV_SNOC_BIMC_1] = &slv_snoc_bimc_1,
	[SLV_IMEM] = &slv_imem,
	[SLV_SNOC_PCNOC] = &slv_snoc_pcnoc,
	[SLV_QDSS_STM] = &slv_qdss_stm,
	[SLV_CATS_1] = &slv_cats_1,
	[SLV_LPASS] = &slv_lpass,
};

static struct qcom_icc_desc msm8953_snoc = {
	.nodes = msm8953_snoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8953_snoc_nodes),
	//.node_qos_init = msm8953_noc_node_init,
};

DEFINE_QNODE_AP(mas_jpeg, QNOC_MASTER_JPEG, 6, 16, QOS_BYPASS, 0, 0,
	QNOC_SNOC_BIMC_2_SLV);
DEFINE_QNODE_AP(mas_mdp, QNOC_MASTER_MDP_PORT0, 7, 16, QOS_BYPASS, 0, 0,
	QNOC_SNOC_BIMC_0_SLV);
DEFINE_QNODE_AP(mas_venus, QNOC_MASTER_VIDEO_P0, 8, 16, QOS_BYPASS, 0, 0,
	QNOC_SNOC_BIMC_2_SLV);
DEFINE_QNODE_AP(mas_vfe0, QNOC_MASTER_VFE, 9, 16, QOS_BYPASS, 0, 0,
	QNOC_SNOC_BIMC_0_SLV);
DEFINE_QNODE_AP(mas_vfe1, QNOC_MASTER_VFE1, 13, 16, QOS_BYPASS, 0, 0,
	QNOC_SNOC_BIMC_0_SLV);
DEFINE_QNODE_AP(mas_cpp, QNOC_MASTER_CPP, 12, 16, QOS_BYPASS, 0, 0,
	QNOC_SNOC_BIMC_2_SLV);
DEFINE_QNODE_AP(slv_snoc_bimc_0, QNOC_SNOC_BIMC_0_SLV, 0, 16, QOS_NONE, 0, 0,
	QNOC_SNOC_BIMC_0_MAS);
DEFINE_QNODE_AP(slv_snoc_bimc_2, QNOC_SNOC_BIMC_2_SLV, 0, 16, QOS_NONE, 0, 0,
	QNOC_SNOC_BIMC_2_MAS);
DEFINE_QNODE_AP(slv_cats_0, QNOC_SLAVE_CATS_128, 0, 16, QOS_NONE, 0, 0,
	QNOC_NODE_NONE);

static struct qcom_icc_node *msm8953_sysmmnoc_nodes[] = {
	[MAS_JPEG] = &mas_jpeg,
	[MAS_MDP] = &mas_mdp,
	[MAS_VENUS] = &mas_venus,
	[MAS_VFE0] = &mas_vfe0,
	[MAS_VFE1] = &mas_vfe1,
	[MAS_CPP] = &mas_cpp,
	[SLV_SNOC_BIMC_0] = &slv_snoc_bimc_0,
	[SLV_SNOC_BIMC_2] = &slv_snoc_bimc_2,
	[SLV_CATS_0] = &slv_cats_0,
};

static struct qcom_icc_desc msm8953_sysmmnoc = {
	.nodes = msm8953_sysmmnoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8953_sysmmnoc_nodes),
	//.node_qos_init = msm8953_noc_node_init,
};

static void msm8953_bimc_node_init(struct msm8953_icc_node *qn,
				  struct regmap* rmap)
{
	int health_lvl;
	u32 bke_en = 0;

	switch (qn->qos_mode) {
	case QOS_FIXED:
		for (health_lvl = 0; health_lvl < 4; health_lvl++) {
			regmap_update_bits(rmap, BIMC_BKE_HEALTH_REG(qn->qport, health_lvl),
						 BIMC_BKE_HEALTH_AREQPRIO_MASK,
						 qn->prio1 << BIMC_BKE_HEALTH_AREQPRIO_SHIFT);

			regmap_update_bits(rmap, BIMC_BKE_HEALTH_REG(qn->qport, health_lvl),
						 BIMC_BKE_HEALTH_PRIOLVL_MASK,
						 qn->prio0 << BIMC_BKE_HEALTH_PRIOLVL_SHIFT);

			if (health_lvl < 3)
				regmap_update_bits(rmap,
						   BIMC_BKE_HEALTH_REG(qn->qport, health_lvl),
						   BIMC_BKE_HEALTH_LIMIT_CMDS_MASK, 0);
		}
		bke_en = 1 << BIMC_BKE_ENA_SHIFT;
		break;
	case QOS_BYPASS:
		break;
	default:
		return;
	}

	regmap_update_bits(rmap, BIMC_BKE_ENA_REG(qn->qport), BIMC_BKE_ENA_MASK, bke_en);
}

static void msm8953_noc_node_init(struct msm8953_icc_node *qn,
				  struct regmap* rmap)
{
	u32 mode = 0;

	switch (qn->qos_mode) {
	case QOS_BYPASS:
		mode = NOC_QOS_MODE_BYPASS;
		break;
	case QOS_FIXED:
		regmap_update_bits(rmap,
				NOC_QOS_PRIO_REG(qn->qport),
				NOC_QOS_PRIO_P0_MASK,
				qn->prio0 & NOC_QOS_PRIO_P0_SHIFT);
		regmap_update_bits(rmap,
				NOC_QOS_PRIO_REG(qn->qport),
				NOC_QOS_PRIO_P1_MASK,
				qn->prio1 & NOC_QOS_PRIO_P0_SHIFT);
		mode = NOC_QOS_MODE_FIXED;
		break;
	default:
		return;
	}

	regmap_update_bits(rmap,
			   NOC_QOS_MODE_REG(qn->qport),
			   NOC_QOS_MODE_MASK,
			   mode);
}

static int msm8953_qnoc_probe(struct platform_device *pdev)
{
	return qnoc_probe(pdev, sizeof(msm8953_bus_clocks),
			  ARRAY_SIZE(msm8953_bus_clocks), msm8953_bus_clocks);
}

static const struct of_device_id msm8953_noc_of_match[] = {
	{ .compatible = "qcom,msm8953-bimc", .data = &msm8953_bimc },
	{ .compatible = "qcom,msm8953-pcnoc", .data = &msm8953_pcnoc },
	{ .compatible = "qcom,msm8953-snoc", .data = &msm8953_snoc },
	{ .compatible = "qcom,msm8953-sysmmnoc", .data = &msm8953_sysmmnoc },
	{ }
};
MODULE_DEVICE_TABLE(of, msm8953_noc_of_match);

static struct platform_driver msm8953_noc_driver = {
	.probe = msm8953_qnoc_probe,
	.remove = qnoc_remove,
	.driver = {
		.name = "qnoc-msm8953",
		.of_match_table = msm8953_noc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(msm8953_noc_driver);
MODULE_DESCRIPTION("Qualcomm MSM8953 NoC driver");
MODULE_LICENSE("GPL v2");
