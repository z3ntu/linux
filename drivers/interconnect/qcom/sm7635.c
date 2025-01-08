// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2025, Luca Weiss <luca.weiss@fairphone.com>
 */

#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/interconnect.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <dt-bindings/interconnect/qcom,sm7635-rpmh.h>

#include "bcm-voter.h"
#include "icc-common.h"
#include "icc-rpmh.h"
#include "sm7635.h"

static struct qcom_icc_node qhm_qup1 = {
	.name = "qhm_qup1",
	.id = SM7635_MASTER_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM7635_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.id = SM7635_MASTER_UFS_MEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM7635_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = SM7635_MASTER_USB3_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM7635_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SM7635_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM7635_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qhm_qspi = {
	.name = "qhm_qspi",
	.id = SM7635_MASTER_QSPI_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM7635_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qhm_qup0 = {
	.name = "qhm_qup0",
	.id = SM7635_MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM7635_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qxm_crypto = {
	.name = "qxm_crypto",
	.id = SM7635_MASTER_CRYPTO,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM7635_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qxm_ipa = {
	.name = "qxm_ipa",
	.id = SM7635_MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM7635_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node xm_qdss_etr_0 = {
	.name = "xm_qdss_etr_0",
	.id = SM7635_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM7635_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node xm_qdss_etr_1 = {
	.name = "xm_qdss_etr_1",
	.id = SM7635_MASTER_QDSS_ETR_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM7635_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node xm_sdc1 = {
	.name = "xm_sdc1",
	.id = SM7635_MASTER_SDCC_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM7635_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node xm_sdc2 = {
	.name = "xm_sdc2",
	.id = SM7635_MASTER_SDCC_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM7635_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qup0_core_master = {
	.name = "qup0_core_master",
	.id = SM7635_MASTER_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM7635_SLAVE_QUP_CORE_0 },
};

static struct qcom_icc_node qup1_core_master = {
	.name = "qup1_core_master",
	.id = SM7635_MASTER_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM7635_SLAVE_QUP_CORE_1 },
};

static struct qcom_icc_node qsm_cfg = {
	.name = "qsm_cfg",
	.id = SM7635_MASTER_CNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 35,
	.links = { SM7635_SLAVE_AHB2PHY_SOUTH, SM7635_SLAVE_AHB2PHY_NORTH,
		   SM7635_SLAVE_CAMERA_CFG, SM7635_SLAVE_CLK_CTL,
		   SM7635_SLAVE_RBCPR_CX_CFG, SM7635_SLAVE_RBCPR_MXA_CFG,
		   SM7635_SLAVE_CRYPTO_0_CFG, SM7635_SLAVE_CX_RDPM,
		   SM7635_SLAVE_GFX3D_CFG, SM7635_SLAVE_IMEM_CFG,
		   SM7635_SLAVE_CNOC_MSS, SM7635_SLAVE_MX_2_RDPM,
		   SM7635_SLAVE_MX_RDPM, SM7635_SLAVE_PDM,
		   SM7635_SLAVE_QDSS_CFG, SM7635_SLAVE_QSPI_0,
		   SM7635_SLAVE_QUP_0, SM7635_SLAVE_QUP_1,
		   SM7635_SLAVE_SDC1, SM7635_SLAVE_SDCC_2,
		   SM7635_SLAVE_TCSR, SM7635_SLAVE_TLMM,
		   SM7635_SLAVE_UFS_MEM_CFG, SM7635_SLAVE_USB3_0,
		   SM7635_SLAVE_VENUS_CFG, SM7635_SLAVE_VSENSE_CTRL_CFG,
		   SM7635_SLAVE_WLAN, SM7635_SLAVE_CNOC_MNOC_HF_CFG,
		   SM7635_SLAVE_CNOC_MNOC_SF_CFG, SM7635_SLAVE_NSP_QTB_CFG,
		   SM7635_SLAVE_PCIE_ANOC_CFG, SM7635_SLAVE_WLAN_Q6_THROTTLE_CFG,
		   SM7635_SLAVE_SERVICE_CNOC_CFG, SM7635_SLAVE_QDSS_STM,
		   SM7635_SLAVE_TCU },
};

static struct qcom_icc_node qnm_gemnoc_cnoc = {
	.name = "qnm_gemnoc_cnoc",
	.id = SM7635_MASTER_GEM_NOC_CNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 14,
	.links = { SM7635_SLAVE_AOSS, SM7635_SLAVE_DISPLAY_CFG,
		   SM7635_SLAVE_IPA_CFG, SM7635_SLAVE_IPC_ROUTER_CFG,
		   SM7635_SLAVE_PCIE_0_CFG, SM7635_SLAVE_PCIE_1_CFG,
		   SM7635_SLAVE_PRNG, SM7635_SLAVE_TME_CFG,
		   SM7635_SLAVE_APPSS, SM7635_SLAVE_CNOC_CFG,
		   SM7635_SLAVE_DDRSS_CFG, SM7635_SLAVE_IMEM,
		   SM7635_SLAVE_PIMEM, SM7635_SLAVE_SERVICE_CNOC },
};

static struct qcom_icc_node qnm_gemnoc_pcie = {
	.name = "qnm_gemnoc_pcie",
	.id = SM7635_MASTER_GEM_NOC_PCIE_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM7635_SLAVE_PCIE_0, SM7635_SLAVE_PCIE_1 },
};

static struct qcom_icc_node alm_gpu_tcu = {
	.name = "alm_gpu_tcu",
	.id = SM7635_MASTER_GPU_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM7635_SLAVE_GEM_NOC_CNOC, SM7635_SLAVE_LLCC },
};

static struct qcom_icc_node alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.id = SM7635_MASTER_SYS_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM7635_SLAVE_GEM_NOC_CNOC, SM7635_SLAVE_LLCC },
};

static struct qcom_icc_node chm_apps = {
	.name = "chm_apps",
	.id = SM7635_MASTER_APPSS_PROC,
	.channels = 3,
	.buswidth = 32,
	.num_links = 3,
	.links = { SM7635_SLAVE_GEM_NOC_CNOC, SM7635_SLAVE_LLCC,
		   SM7635_SLAVE_MEM_NOC_PCIE_SNOC },
};

static struct qcom_icc_node qnm_gpu = {
	.name = "qnm_gpu",
	.id = SM7635_MASTER_GFX3D,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM7635_SLAVE_GEM_NOC_CNOC, SM7635_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_lpass_gemnoc = {
	.name = "qnm_lpass_gemnoc",
	.id = SM7635_MASTER_LPASS_GEM_NOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SM7635_SLAVE_GEM_NOC_CNOC, SM7635_SLAVE_LLCC,
		   SM7635_SLAVE_MEM_NOC_PCIE_SNOC },
};

static struct qcom_icc_node qnm_mdsp = {
	.name = "qnm_mdsp",
	.id = SM7635_MASTER_MSS_PROC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SM7635_SLAVE_GEM_NOC_CNOC, SM7635_SLAVE_LLCC,
		   SM7635_SLAVE_MEM_NOC_PCIE_SNOC },
};

static struct qcom_icc_node qnm_mnoc_hf = {
	.name = "qnm_mnoc_hf",
	.id = SM7635_MASTER_MNOC_HF_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM7635_SLAVE_GEM_NOC_CNOC, SM7635_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_mnoc_sf = {
	.name = "qnm_mnoc_sf",
	.id = SM7635_MASTER_MNOC_SF_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM7635_SLAVE_GEM_NOC_CNOC, SM7635_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_nsp_gemnoc = {
	.name = "qnm_nsp_gemnoc",
	.id = SM7635_MASTER_COMPUTE_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 3,
	.links = { SM7635_SLAVE_GEM_NOC_CNOC, SM7635_SLAVE_LLCC,
		   SM7635_SLAVE_MEM_NOC_PCIE_SNOC },
};

static struct qcom_icc_node qnm_pcie = {
	.name = "qnm_pcie",
	.id = SM7635_MASTER_ANOC_PCIE_GEM_NOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM7635_SLAVE_GEM_NOC_CNOC, SM7635_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_snoc_gc = {
	.name = "qnm_snoc_gc",
	.id = SM7635_MASTER_SNOC_GC_MEM_NOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM7635_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_snoc_sf = {
	.name = "qnm_snoc_sf",
	.id = SM7635_MASTER_SNOC_SF_MEM_NOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SM7635_SLAVE_GEM_NOC_CNOC, SM7635_SLAVE_LLCC,
		   SM7635_SLAVE_MEM_NOC_PCIE_SNOC },
};

static struct qcom_icc_node qxm_wlan_q6 = {
	.name = "qxm_wlan_q6",
	.id = SM7635_MASTER_WLAN_Q6,
	.channels = 1,
	.buswidth = 8,
	.num_links = 3,
	.links = { SM7635_SLAVE_GEM_NOC_CNOC, SM7635_SLAVE_LLCC,
		   SM7635_SLAVE_MEM_NOC_PCIE_SNOC },
};

static struct qcom_icc_node qxm_lpass_dsp = {
	.name = "qxm_lpass_dsp",
	.id = SM7635_MASTER_LPASS_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM7635_SLAVE_LPASS_GEM_NOC },
};

static struct qcom_icc_node llcc_mc = {
	.name = "llcc_mc",
	.id = SM7635_MASTER_LLCC,
	.channels = 2,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM7635_SLAVE_EBI1 },
};

static struct qcom_icc_node qnm_camnoc_hf = {
	.name = "qnm_camnoc_hf",
	.id = SM7635_MASTER_CAMNOC_HF,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM7635_SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qnm_camnoc_icp = {
	.name = "qnm_camnoc_icp",
	.id = SM7635_MASTER_CAMNOC_ICP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM7635_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qnm_camnoc_sf = {
	.name = "qnm_camnoc_sf",
	.id = SM7635_MASTER_CAMNOC_SF,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM7635_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qnm_mdp = {
	.name = "qnm_mdp",
	.id = SM7635_MASTER_MDP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM7635_SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qnm_video = {
	.name = "qnm_video",
	.id = SM7635_MASTER_VIDEO,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM7635_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qsm_hf_mnoc_cfg = {
	.name = "qsm_hf_mnoc_cfg",
	.id = SM7635_MASTER_CNOC_MNOC_HF_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM7635_SLAVE_SERVICE_MNOC_HF },
};

static struct qcom_icc_node qsm_sf_mnoc_cfg = {
	.name = "qsm_sf_mnoc_cfg",
	.id = SM7635_MASTER_CNOC_MNOC_SF_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM7635_SLAVE_SERVICE_MNOC_SF },
};

static struct qcom_icc_node qxm_nsp = {
	.name = "qxm_nsp",
	.id = SM7635_MASTER_CDSP_PROC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM7635_SLAVE_CDSP_MEM_NOC },
};

static struct qcom_icc_node qsm_pcie_anoc_cfg = {
	.name = "qsm_pcie_anoc_cfg",
	.id = SM7635_MASTER_PCIE_ANOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM7635_SLAVE_SERVICE_PCIE_ANOC },
};

static struct qcom_icc_node xm_pcie3_0 = {
	.name = "xm_pcie3_0",
	.id = SM7635_MASTER_PCIE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM7635_SLAVE_ANOC_PCIE_GEM_NOC },
};

static struct qcom_icc_node xm_pcie3_1 = {
	.name = "xm_pcie3_1",
	.id = SM7635_MASTER_PCIE_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM7635_SLAVE_ANOC_PCIE_GEM_NOC },
};

static struct qcom_icc_node qnm_aggre1_noc = {
	.name = "qnm_aggre1_noc",
	.id = SM7635_MASTER_A1NOC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM7635_SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node qnm_aggre2_noc = {
	.name = "qnm_aggre2_noc",
	.id = SM7635_MASTER_A2NOC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM7635_SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node qnm_apss_noc = {
	.name = "qnm_apss_noc",
	.id = SM7635_MASTER_APSS_NOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM7635_SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node qnm_cnoc_data = {
	.name = "qnm_cnoc_data",
	.id = SM7635_MASTER_CNOC_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM7635_SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node qxm_pimem = {
	.name = "qxm_pimem",
	.id = SM7635_MASTER_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM7635_SLAVE_SNOC_GEM_NOC_GC },
};

static struct qcom_icc_node xm_gic = {
	.name = "xm_gic",
	.id = SM7635_MASTER_GIC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM7635_SLAVE_SNOC_GEM_NOC_GC },
};

static struct qcom_icc_node qns_a1noc_snoc = {
	.name = "qns_a1noc_snoc",
	.id = SM7635_SLAVE_A1NOC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM7635_MASTER_A1NOC_SNOC },
};

static struct qcom_icc_node qns_a2noc_snoc = {
	.name = "qns_a2noc_snoc",
	.id = SM7635_SLAVE_A2NOC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM7635_MASTER_A2NOC_SNOC },
};

static struct qcom_icc_node qup0_core_slave = {
	.name = "qup0_core_slave",
	.id = SM7635_SLAVE_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qup1_core_slave = {
	.name = "qup1_core_slave",
	.id = SM7635_SLAVE_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ahb2phy0 = {
	.name = "qhs_ahb2phy0",
	.id = SM7635_SLAVE_AHB2PHY_SOUTH,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ahb2phy1 = {
	.name = "qhs_ahb2phy1",
	.id = SM7635_SLAVE_AHB2PHY_NORTH,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_camera_cfg = {
	.name = "qhs_camera_cfg",
	.id = SM7635_SLAVE_CAMERA_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SM7635_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = SM7635_SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_cpr_mxa = {
	.name = "qhs_cpr_mxa",
	.id = SM7635_SLAVE_RBCPR_MXA_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SM7635_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_cx_rdpm = {
	.name = "qhs_cx_rdpm",
	.id = SM7635_SLAVE_CX_RDPM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.id = SM7635_SLAVE_GFX3D_CFG,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SM7635_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_mss_cfg = {
	.name = "qhs_mss_cfg",
	.id = SM7635_SLAVE_CNOC_MSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_mx_2_rdpm = {
	.name = "qhs_mx_2_rdpm",
	.id = SM7635_SLAVE_MX_2_RDPM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_mx_rdpm = {
	.name = "qhs_mx_rdpm",
	.id = SM7635_SLAVE_MX_RDPM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pdm = {
	.name = "qhs_pdm",
	.id = SM7635_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SM7635_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qspi = {
	.name = "qhs_qspi",
	.id = SM7635_SLAVE_QSPI_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qup0 = {
	.name = "qhs_qup0",
	.id = SM7635_SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qup1 = {
	.name = "qhs_qup1",
	.id = SM7635_SLAVE_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_sdc1 = {
	.name = "qhs_sdc1",
	.id = SM7635_SLAVE_SDC1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = SM7635_SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SM7635_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_tlmm = {
	.name = "qhs_tlmm",
	.id = SM7635_SLAVE_TLMM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.id = SM7635_SLAVE_UFS_MEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_usb3_0 = {
	.name = "qhs_usb3_0",
	.id = SM7635_SLAVE_USB3_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = SM7635_SLAVE_VENUS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = SM7635_SLAVE_VSENSE_CTRL_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_wlan_q6 = {
	.name = "qhs_wlan_q6",
	.id = SM7635_SLAVE_WLAN,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qss_mnoc_hf_cfg = {
	.name = "qss_mnoc_hf_cfg",
	.id = SM7635_SLAVE_CNOC_MNOC_HF_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM7635_MASTER_CNOC_MNOC_HF_CFG },
};

static struct qcom_icc_node qss_mnoc_sf_cfg = {
	.name = "qss_mnoc_sf_cfg",
	.id = SM7635_SLAVE_CNOC_MNOC_SF_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM7635_MASTER_CNOC_MNOC_SF_CFG },
};

static struct qcom_icc_node qss_nsp_qtb_cfg = {
	.name = "qss_nsp_qtb_cfg",
	.id = SM7635_SLAVE_NSP_QTB_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qss_pcie_anoc_cfg = {
	.name = "qss_pcie_anoc_cfg",
	.id = SM7635_SLAVE_PCIE_ANOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM7635_MASTER_PCIE_ANOC_CFG },
};

static struct qcom_icc_node qss_wlan_q6_throttle_cfg = {
	.name = "qss_wlan_q6_throttle_cfg",
	.id = SM7635_SLAVE_WLAN_Q6_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node srvc_cnoc_cfg = {
	.name = "srvc_cnoc_cfg",
	.id = SM7635_SLAVE_SERVICE_CNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SM7635_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SM7635_SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node qhs_aoss = {
	.name = "qhs_aoss",
	.id = SM7635_SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_display_cfg = {
	.name = "qhs_display_cfg",
	.id = SM7635_SLAVE_DISPLAY_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ipa = {
	.name = "qhs_ipa",
	.id = SM7635_SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ipc_router = {
	.name = "qhs_ipc_router",
	.id = SM7635_SLAVE_IPC_ROUTER_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pcie0_cfg = {
	.name = "qhs_pcie0_cfg",
	.id = SM7635_SLAVE_PCIE_0_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pcie1_cfg = {
	.name = "qhs_pcie1_cfg",
	.id = SM7635_SLAVE_PCIE_1_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_prng = {
	.name = "qhs_prng",
	.id = SM7635_SLAVE_PRNG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_tme_cfg = {
	.name = "qhs_tme_cfg",
	.id = SM7635_SLAVE_TME_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qss_apss = {
	.name = "qss_apss",
	.id = SM7635_SLAVE_APPSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qss_cfg = {
	.name = "qss_cfg",
	.id = SM7635_SLAVE_CNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM7635_MASTER_CNOC_CFG },
};

static struct qcom_icc_node qss_ddrss_cfg = {
	.name = "qss_ddrss_cfg",
	.id = SM7635_SLAVE_DDRSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qxs_imem = {
	.name = "qxs_imem",
	.id = SM7635_SLAVE_IMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node qxs_pimem = {
	.name = "qxs_pimem",
	.id = SM7635_SLAVE_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node srvc_cnoc_main = {
	.name = "srvc_cnoc_main",
	.id = SM7635_SLAVE_SERVICE_CNOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node xs_pcie_0 = {
	.name = "xs_pcie_0",
	.id = SM7635_SLAVE_PCIE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node xs_pcie_1 = {
	.name = "xs_pcie_1",
	.id = SM7635_SLAVE_PCIE_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node qns_gem_noc_cnoc = {
	.name = "qns_gem_noc_cnoc",
	.id = SM7635_SLAVE_GEM_NOC_CNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM7635_MASTER_GEM_NOC_CNOC },
};

static struct qcom_icc_node qns_llcc = {
	.name = "qns_llcc",
	.id = SM7635_SLAVE_LLCC,
	.channels = 2,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM7635_MASTER_LLCC },
};

static struct qcom_icc_node qns_pcie = {
	.name = "qns_pcie",
	.id = SM7635_SLAVE_MEM_NOC_PCIE_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM7635_MASTER_GEM_NOC_PCIE_SNOC },
};

static struct qcom_icc_node qns_lpass_ag_noc_gemnoc = {
	.name = "qns_lpass_ag_noc_gemnoc",
	.id = SM7635_SLAVE_LPASS_GEM_NOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM7635_MASTER_LPASS_GEM_NOC },
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.id = SM7635_SLAVE_EBI1,
	.channels = 2,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qns_mem_noc_hf = {
	.name = "qns_mem_noc_hf",
	.id = SM7635_SLAVE_MNOC_HF_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM7635_MASTER_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qns_mem_noc_sf = {
	.name = "qns_mem_noc_sf",
	.id = SM7635_SLAVE_MNOC_SF_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM7635_MASTER_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node srvc_mnoc_hf = {
	.name = "srvc_mnoc_hf",
	.id = SM7635_SLAVE_SERVICE_MNOC_HF,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node srvc_mnoc_sf = {
	.name = "srvc_mnoc_sf",
	.id = SM7635_SLAVE_SERVICE_MNOC_SF,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qns_nsp_gemnoc = {
	.name = "qns_nsp_gemnoc",
	.id = SM7635_SLAVE_CDSP_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM7635_MASTER_COMPUTE_NOC },
};

static struct qcom_icc_node qns_pcie_mem_noc = {
	.name = "qns_pcie_mem_noc",
	.id = SM7635_SLAVE_ANOC_PCIE_GEM_NOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM7635_MASTER_ANOC_PCIE_GEM_NOC },
};

static struct qcom_icc_node srvc_pcie_aggre_noc = {
	.name = "srvc_pcie_aggre_noc",
	.id = SM7635_SLAVE_SERVICE_PCIE_ANOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qns_gemnoc_gc = {
	.name = "qns_gemnoc_gc",
	.id = SM7635_SLAVE_SNOC_GEM_NOC_GC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM7635_MASTER_SNOC_GC_MEM_NOC },
};

static struct qcom_icc_node qns_gemnoc_sf = {
	.name = "qns_gemnoc_sf",
	.id = SM7635_SLAVE_SNOC_GEM_NOC_SF,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM7635_MASTER_SNOC_SF_MEM_NOC },
};

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.enable_mask = 0x1,
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.num_nodes = 1,
	.nodes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.enable_mask = 0x1,
	.keepalive = true,
	.num_nodes = 51,
	.nodes = { &qsm_cfg, &qhs_ahb2phy0,
		   &qhs_ahb2phy1, &qhs_camera_cfg,
		   &qhs_clk_ctl, &qhs_cpr_cx,
		   &qhs_cpr_mxa, &qhs_crypto0_cfg,
		   &qhs_cx_rdpm, &qhs_gpuss_cfg,
		   &qhs_imem_cfg, &qhs_mss_cfg,
		   &qhs_mx_2_rdpm, &qhs_mx_rdpm,
		   &qhs_pdm, &qhs_qdss_cfg,
		   &qhs_qspi, &qhs_sdc1,
		   &qhs_sdc2, &qhs_tcsr,
		   &qhs_tlmm, &qhs_ufs_mem_cfg,
		   &qhs_usb3_0, &qhs_venus_cfg,
		   &qhs_vsense_ctrl_cfg, &qhs_wlan_q6,
		   &qss_mnoc_hf_cfg, &qss_mnoc_sf_cfg,
		   &qss_nsp_qtb_cfg, &qss_pcie_anoc_cfg,
		   &qss_wlan_q6_throttle_cfg, &srvc_cnoc_cfg,
		   &xs_qdss_stm, &xs_sys_tcu_cfg,
		   &qnm_gemnoc_cnoc, &qnm_gemnoc_pcie,
		   &qhs_aoss, &qhs_ipa,
		   &qhs_ipc_router, &qhs_pcie0_cfg,
		   &qhs_pcie1_cfg, &qhs_prng,
		   &qhs_tme_cfg, &qss_apss,
		   &qss_cfg, &qss_ddrss_cfg,
		   &qxs_imem, &qxs_pimem,
		   &srvc_cnoc_main, &xs_pcie_0,
		   &xs_pcie_1 },
};

static struct qcom_icc_bcm bcm_cn1 = {
	.name = "CN1",
	.num_nodes = 3,
	.nodes = { &qhs_qup0, &qhs_qup1,
		   &qhs_display_cfg },
};

static struct qcom_icc_bcm bcm_co0 = {
	.name = "CO0",
	.enable_mask = 0x1,
	.num_nodes = 2,
	.nodes = { &qxm_nsp, &qns_nsp_gemnoc },
};

static struct qcom_icc_bcm bcm_mc0 = {
	.name = "MC0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_mm0 = {
	.name = "MM0",
	.num_nodes = 1,
	.nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_bcm bcm_mm1 = {
	.name = "MM1",
	.enable_mask = 0x1,
	.num_nodes = 4,
	.nodes = { &qnm_camnoc_hf, &qnm_camnoc_icp,
		   &qnm_camnoc_sf, &qns_mem_noc_sf },
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.keepalive = true,
	.vote_scale = 1,
	.num_nodes = 1,
	.nodes = { &qup0_core_slave },
};

static struct qcom_icc_bcm bcm_qup1 = {
	.name = "QUP1",
	.keepalive = true,
	.vote_scale = 1,
	.num_nodes = 1,
	.nodes = { &qup1_core_slave },
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_sh1 = {
	.name = "SH1",
	.enable_mask = 0x1,
	.num_nodes = 14,
	.nodes = { &alm_gpu_tcu, &alm_sys_tcu,
		   &chm_apps, &qnm_gpu,
		   &qnm_mdsp, &qnm_mnoc_hf,
		   &qnm_mnoc_sf, &qnm_nsp_gemnoc,
		   &qnm_pcie, &qnm_snoc_gc,
		   &qnm_snoc_sf, &qxm_wlan_q6,
		   &qns_gem_noc_cnoc, &qns_pcie },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_nodes = 2,
	.nodes = { &qns_gemnoc_gc, &qns_gemnoc_sf },
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.enable_mask = 0x1,
	.num_nodes = 1,
	.nodes = { &qxm_pimem },
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.num_nodes = 1,
	.nodes = { &qnm_aggre1_noc },
};

static struct qcom_icc_bcm bcm_sn3 = {
	.name = "SN3",
	.num_nodes = 1,
	.nodes = { &qnm_aggre2_noc },
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.num_nodes = 1,
	.nodes = { &qns_pcie_mem_noc },
};

static struct qcom_icc_bcm * const aggre1_noc_bcms[] = {
};

static struct qcom_icc_node * const aggre1_noc_nodes[] = {
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_USB3_0] = &xm_usb3_0,
	[SLAVE_A1NOC_SNOC] = &qns_a1noc_snoc,
};

static const struct qcom_icc_desc sm7635_aggre1_noc = {
	.nodes = aggre1_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre1_noc_nodes),
	.bcms = aggre1_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_noc_bcms),
};

static struct qcom_icc_bcm * const aggre2_noc_bcms[] = {
	&bcm_ce0,
};

static struct qcom_icc_node * const aggre2_noc_nodes[] = {
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QSPI_0] = &qhm_qspi,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_QDSS_ETR] = &xm_qdss_etr_0,
	[MASTER_QDSS_ETR_1] = &xm_qdss_etr_1,
	[MASTER_SDCC_1] = &xm_sdc1,
	[MASTER_SDCC_2] = &xm_sdc2,
	[SLAVE_A2NOC_SNOC] = &qns_a2noc_snoc,
};

static const struct qcom_icc_desc sm7635_aggre2_noc = {
	.nodes = aggre2_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre2_noc_nodes),
	.bcms = aggre2_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_noc_bcms),
};

static struct qcom_icc_bcm * const clk_virt_bcms[] = {
	&bcm_qup0,
	&bcm_qup1,
};

static struct qcom_icc_node * const clk_virt_nodes[] = {
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[MASTER_QUP_CORE_1] = &qup1_core_master,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_QUP_CORE_1] = &qup1_core_slave,
};

static const struct qcom_icc_desc sm7635_clk_virt = {
	.nodes = clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(clk_virt_nodes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
};

static struct qcom_icc_bcm * const cnoc_cfg_bcms[] = {
	&bcm_cn0,
	&bcm_cn1,
};

static struct qcom_icc_node * const cnoc_cfg_nodes[] = {
	[MASTER_CNOC_CFG] = &qsm_cfg,
	[SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_NORTH] = &qhs_ahb2phy1,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MXA_CFG] = &qhs_cpr_mxa,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_CX_RDPM] = &qhs_cx_rdpm,
	[SLAVE_GFX3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_CNOC_MSS] = &qhs_mss_cfg,
	[SLAVE_MX_2_RDPM] = &qhs_mx_2_rdpm,
	[SLAVE_MX_RDPM] = &qhs_mx_rdpm,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QSPI_0] = &qhs_qspi,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_SDC1] = &qhs_sdc1,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB3_0] = &qhs_usb3_0,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_WLAN] = &qhs_wlan_q6,
	[SLAVE_CNOC_MNOC_HF_CFG] = &qss_mnoc_hf_cfg,
	[SLAVE_CNOC_MNOC_SF_CFG] = &qss_mnoc_sf_cfg,
	[SLAVE_NSP_QTB_CFG] = &qss_nsp_qtb_cfg,
	[SLAVE_PCIE_ANOC_CFG] = &qss_pcie_anoc_cfg,
	[SLAVE_WLAN_Q6_THROTTLE_CFG] = &qss_wlan_q6_throttle_cfg,
	[SLAVE_SERVICE_CNOC_CFG] = &srvc_cnoc_cfg,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sm7635_cnoc_cfg = {
	.nodes = cnoc_cfg_nodes,
	.num_nodes = ARRAY_SIZE(cnoc_cfg_nodes),
	.bcms = cnoc_cfg_bcms,
	.num_bcms = ARRAY_SIZE(cnoc_cfg_bcms),
};

static struct qcom_icc_bcm * const cnoc_main_bcms[] = {
	&bcm_cn0,
	&bcm_cn1,
};

static struct qcom_icc_node * const cnoc_main_nodes[] = {
	[MASTER_GEM_NOC_CNOC] = &qnm_gemnoc_cnoc,
	[MASTER_GEM_NOC_PCIE_SNOC] = &qnm_gemnoc_pcie,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[SLAVE_PCIE_0_CFG] = &qhs_pcie0_cfg,
	[SLAVE_PCIE_1_CFG] = &qhs_pcie1_cfg,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_TME_CFG] = &qhs_tme_cfg,
	[SLAVE_APPSS] = &qss_apss,
	[SLAVE_CNOC_CFG] = &qss_cfg,
	[SLAVE_DDRSS_CFG] = &qss_ddrss_cfg,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_CNOC] = &srvc_cnoc_main,
	[SLAVE_PCIE_0] = &xs_pcie_0,
	[SLAVE_PCIE_1] = &xs_pcie_1,
};

static const struct qcom_icc_desc sm7635_cnoc_main = {
	.nodes = cnoc_main_nodes,
	.num_nodes = ARRAY_SIZE(cnoc_main_nodes),
	.bcms = cnoc_main_bcms,
	.num_bcms = ARRAY_SIZE(cnoc_main_bcms),
};

static struct qcom_icc_bcm * const gem_noc_bcms[] = {
	&bcm_sh0,
	&bcm_sh1,
};

static struct qcom_icc_node * const gem_noc_nodes[] = {
	[MASTER_GPU_TCU] = &alm_gpu_tcu,
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_GFX3D] = &qnm_gpu,
	[MASTER_LPASS_GEM_NOC] = &qnm_lpass_gemnoc,
	[MASTER_MSS_PROC] = &qnm_mdsp,
	[MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[MASTER_MNOC_SF_MEM_NOC] = &qnm_mnoc_sf,
	[MASTER_COMPUTE_NOC] = &qnm_nsp_gemnoc,
	[MASTER_ANOC_PCIE_GEM_NOC] = &qnm_pcie,
	[MASTER_SNOC_GC_MEM_NOC] = &qnm_snoc_gc,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[MASTER_WLAN_Q6] = &qxm_wlan_q6,
	[SLAVE_GEM_NOC_CNOC] = &qns_gem_noc_cnoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MEM_NOC_PCIE_SNOC] = &qns_pcie,
};

static const struct qcom_icc_desc sm7635_gem_noc = {
	.nodes = gem_noc_nodes,
	.num_nodes = ARRAY_SIZE(gem_noc_nodes),
	.bcms = gem_noc_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_bcms),
};

static struct qcom_icc_bcm * const lpass_ag_noc_bcms[] = {
};

static struct qcom_icc_node * const lpass_ag_noc_nodes[] = {
	[MASTER_LPASS_PROC] = &qxm_lpass_dsp,
	[SLAVE_LPASS_GEM_NOC] = &qns_lpass_ag_noc_gemnoc,
};

static const struct qcom_icc_desc sm7635_lpass_ag_noc = {
	.nodes = lpass_ag_noc_nodes,
	.num_nodes = ARRAY_SIZE(lpass_ag_noc_nodes),
	.bcms = lpass_ag_noc_bcms,
	.num_bcms = ARRAY_SIZE(lpass_ag_noc_bcms),
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
};

static struct qcom_icc_node * const mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
};

static const struct qcom_icc_desc sm7635_mc_virt = {
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const mmss_noc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
};

static struct qcom_icc_node * const mmss_noc_nodes[] = {
	[MASTER_CAMNOC_HF] = &qnm_camnoc_hf,
	[MASTER_CAMNOC_ICP] = &qnm_camnoc_icp,
	[MASTER_CAMNOC_SF] = &qnm_camnoc_sf,
	[MASTER_MDP] = &qnm_mdp,
	[MASTER_VIDEO] = &qnm_video,
	[MASTER_CNOC_MNOC_HF_CFG] = &qsm_hf_mnoc_cfg,
	[MASTER_CNOC_MNOC_SF_CFG] = &qsm_sf_mnoc_cfg,
	[SLAVE_MNOC_HF_MEM_NOC] = &qns_mem_noc_hf,
	[SLAVE_MNOC_SF_MEM_NOC] = &qns_mem_noc_sf,
	[SLAVE_SERVICE_MNOC_HF] = &srvc_mnoc_hf,
	[SLAVE_SERVICE_MNOC_SF] = &srvc_mnoc_sf,
};

static const struct qcom_icc_desc sm7635_mmss_noc = {
	.nodes = mmss_noc_nodes,
	.num_nodes = ARRAY_SIZE(mmss_noc_nodes),
	.bcms = mmss_noc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_bcms),
};

static struct qcom_icc_bcm * const nsp_noc_bcms[] = {
	&bcm_co0,
};

static struct qcom_icc_node * const nsp_noc_nodes[] = {
	[MASTER_CDSP_PROC] = &qxm_nsp,
	[SLAVE_CDSP_MEM_NOC] = &qns_nsp_gemnoc,
};

static const struct qcom_icc_desc sm7635_nsp_noc = {
	.nodes = nsp_noc_nodes,
	.num_nodes = ARRAY_SIZE(nsp_noc_nodes),
	.bcms = nsp_noc_bcms,
	.num_bcms = ARRAY_SIZE(nsp_noc_bcms),
};

static struct qcom_icc_bcm * const pcie_anoc_bcms[] = {
	&bcm_sn4,
};

static struct qcom_icc_node * const pcie_anoc_nodes[] = {
	[MASTER_PCIE_ANOC_CFG] = &qsm_pcie_anoc_cfg,
	[MASTER_PCIE_0] = &xm_pcie3_0,
	[MASTER_PCIE_1] = &xm_pcie3_1,
	[SLAVE_ANOC_PCIE_GEM_NOC] = &qns_pcie_mem_noc,
	[SLAVE_SERVICE_PCIE_ANOC] = &srvc_pcie_aggre_noc,
};

static const struct qcom_icc_desc sm7635_pcie_anoc = {
	.nodes = pcie_anoc_nodes,
	.num_nodes = ARRAY_SIZE(pcie_anoc_nodes),
	.bcms = pcie_anoc_bcms,
	.num_bcms = ARRAY_SIZE(pcie_anoc_bcms),
};

static struct qcom_icc_bcm * const system_noc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn2,
	&bcm_sn3,
};

static struct qcom_icc_node * const system_noc_nodes[] = {
	[MASTER_A1NOC_SNOC] = &qnm_aggre1_noc,
	[MASTER_A2NOC_SNOC] = &qnm_aggre2_noc,
	[MASTER_APSS_NOC] = &qnm_apss_noc,
	[MASTER_CNOC_SNOC] = &qnm_cnoc_data,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_SNOC_GEM_NOC_GC] = &qns_gemnoc_gc,
	[SLAVE_SNOC_GEM_NOC_SF] = &qns_gemnoc_sf,
};

static const struct qcom_icc_desc sm7635_system_noc = {
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sm7635-aggre1-noc",
	  .data = &sm7635_aggre1_noc},
	{ .compatible = "qcom,sm7635-aggre2-noc",
	  .data = &sm7635_aggre2_noc},
	{ .compatible = "qcom,sm7635-clk-virt",
	  .data = &sm7635_clk_virt},
	{ .compatible = "qcom,sm7635-cnoc-cfg",
	  .data = &sm7635_cnoc_cfg},
	{ .compatible = "qcom,sm7635-cnoc-main",
	  .data = &sm7635_cnoc_main},
	{ .compatible = "qcom,sm7635-gem-noc",
	  .data = &sm7635_gem_noc},
	{ .compatible = "qcom,sm7635-lpass-ag-noc",
	  .data = &sm7635_lpass_ag_noc},
	{ .compatible = "qcom,sm7635-mc-virt",
	  .data = &sm7635_mc_virt},
	{ .compatible = "qcom,sm7635-mmss-noc",
	  .data = &sm7635_mmss_noc},
	{ .compatible = "qcom,sm7635-nsp-noc",
	  .data = &sm7635_nsp_noc},
	{ .compatible = "qcom,sm7635-pcie-anoc",
	  .data = &sm7635_pcie_anoc},
	{ .compatible = "qcom,sm7635-system-noc",
	  .data = &sm7635_system_noc},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-sm7635",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
	},
};

static int __init qnoc_driver_init(void)
{
	return platform_driver_register(&qnoc_driver);
}
core_initcall(qnoc_driver_init);

static void __exit qnoc_driver_exit(void)
{
	platform_driver_unregister(&qnoc_driver);
}
module_exit(qnoc_driver_exit);

MODULE_DESCRIPTION("SM7635 NoC driver");
MODULE_LICENSE("GPL");
