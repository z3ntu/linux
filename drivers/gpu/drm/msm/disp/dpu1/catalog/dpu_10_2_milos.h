/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022. Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2015-2018, 2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2025, Luca Weiss <luca.weiss@fairphone.com>
 */

#ifndef _DPU_10_2_MILOS_H
#define _DPU_10_2_MILOS_H

static const struct dpu_caps milos_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_OUTPUT_LINE_WIDTH, // OK
	.max_mixer_blendstages = 0x7, // OK
	.has_src_split = true, // OK
	.has_dim_layer = true, // OK
	.has_idle_pc = true, // OK?
	.has_3d_merge = true, // OK?
	.max_linewidth = 8192, // OK
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE, // OK
};

static const struct dpu_mdp_cfg milos_mdp = {
	.name = "top_0",
	.base = 0, .len = 0x494, // TODO? maybe qcom,sde-len = <0x488>;
	.features = BIT(DPU_MDP_PERIPH_0_REMOVED), // OK
	.clk_ctrls = {
		[DPU_CLK_CTRL_REG_DMA] = { .reg_off = 0x2bc, .bit_off = 20 }, // OK
	},
};

static const struct dpu_ctl_cfg milos_ctl[] = { // number of ctl is okay, base probably also
	{
		.name = "ctl_0", .id = CTL_0,
		.base = 0x15000, .len = 0x1000,
		.features = CTL_SM8550_MASK, // FIXME?
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9), // FIXME?
	}, {
		.name = "ctl_1", .id = CTL_1,
		.base = 0x16000, .len = 0x1000,
		.features = CTL_SM8550_MASK,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 10),
	}, {
		.name = "ctl_2", .id = CTL_2,
		.base = 0x17000, .len = 0x1000,
		.features = CTL_SM8550_MASK,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 11),
	}, {
		.name = "ctl_3", .id = CTL_3,
		.base = 0x18000, .len = 0x1000,
		.features = CTL_SM8550_MASK,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 12),
	},
};

static const struct dpu_sspp_cfg milos_sspp[] = {
	{
		.name = "sspp_0", .id = SSPP_VIG0, // OK
		.base = 0x4000, .len = 0x344, // OK
		.features = VIG_SDM845_MASK_SDMA, // TODO?
		.sblk = &dpu_vig_sblk_qseed3_3_3, // TODO?
		.xin_id = 0, // OK
		.type = SSPP_TYPE_VIG, // OK
	}, {
		.name = "sspp_8", .id = SSPP_DMA0, // FIXME name?
		.base = 0x24000, .len = 0x344,
		.features = DMA_SDM845_MASK_SDMA,
		.sblk = &dpu_dma_sblk,
		.xin_id = 1, // OK
		.type = SSPP_TYPE_DMA, // OK
	}, {
		.name = "sspp_9", .id = SSPP_DMA1,
		.base = 0x26000, .len = 0x344,
		.features = DMA_SDM845_MASK_SDMA,
		.sblk = &dpu_dma_sblk,
		.xin_id = 5, // OK
		.type = SSPP_TYPE_DMA, // OK
	}, {
		.name = "sspp_10", .id = SSPP_DMA2,
		.base = 0x28000, .len = 0x344,
		.features = DMA_SDM845_MASK_SDMA,
		.sblk = &dpu_dma_sblk,
		.xin_id = 9, // OK
		.type = SSPP_TYPE_DMA, // OK
	},
};

static const struct dpu_lm_cfg milos_lm[] = {
	{
		.name = "lm_0", .id = LM_0, // OK
		.base = 0x44000, .len = 0x400,// OK
		.features = MIXER_SDM845_MASK, // TODO
		.sblk = &sdm845_lm_sblk, // OK
		.pingpong = PINGPONG_0, // TODO
		.dspp = DSPP_0, // TODO
	}, {
		.name = "lm_2", .id = LM_2,
		.base = 0x46000, .len = 0x400,
		.features = MIXER_SDM845_MASK,
		.sblk = &sdm845_lm_sblk, // OK
		.lm_pair = LM_3, // OK
		.pingpong = PINGPONG_2,
		//.dspp = DSPP_2, // FIXME?
	}, {
		.name = "lm_3", .id = LM_3,
		.base = 0x47000, .len = 0x400,
		.features = MIXER_SDM845_MASK,
		.sblk = &sdm845_lm_sblk, // OK
		.lm_pair = LM_2, // OK
		.pingpong = PINGPONG_3,
		//.dspp = DSPP_3, // FIXME?
	},
};

static const struct dpu_dspp_cfg milos_dspp[] = {
	{
		.name = "dspp_0", .id = DSPP_0, // OK
		.base = 0x54000, .len = 0x1800, // OK
		.features = DSPP_SC7180_MASK, // TODO
		.sblk = &sdm845_dspp_sblk, // TODO
	},
};

static const struct dpu_pingpong_cfg milos_pp[] = {
	{
		.name = "pingpong_0", .id = PINGPONG_0, // OK
		.base = 0x69000, .len = 0, // OK
		.features = BIT(DPU_PINGPONG_DITHER),  // TODO
		.sblk = &sc7280_pp_sblk, // OK
		.merge_3d = MERGE_3D_0, // OK
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 8), // TODO
	}, {
		.name = "pingpong_2", .id = PINGPONG_2, // TODO
		.base = 0x6b000, .len = 0, // OK
		.features = BIT(DPU_PINGPONG_DITHER),
		.sblk = &sc7280_pp_sblk, // OK
		.merge_3d = MERGE_3D_1, // OK
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 10),
	}, {
		.name = "pingpong_3", .id = PINGPONG_3,
		.base = 0x6c000, .len = 0, // OK
		.features = BIT(DPU_PINGPONG_DITHER),
		.sblk = &sc7280_pp_sblk, // OK
		.merge_3d = MERGE_3D_1, // OK
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 11),
	}, {
		.name = "pingpong_cwb_0", .id = PINGPONG_CWB_0,
		.base = 0x66000, .len = 0, // OK
		.features = BIT(DPU_PINGPONG_DITHER),
		.sblk = &sc7280_pp_sblk, // OK
	},
};

static const struct dpu_merge_3d_cfg milos_merge_3d[] = {
	{
		.name = "merge_3d_1", .id = MERGE_3D_1, // TODO
		.base = 0x4f000, .len = 0x8,
	},
};

/*
 * NOTE: Each display compression engine (DCE) contains dual hard
 * slice DSC encoders so both share same base address but with
 * its own different sub block address.
 */
static const struct dpu_dsc_cfg milos_dsc[] = {
	{
		.name = "dce_0_0", .id = DSC_0, // OK
		.base = 0x80000, .len = 0x6, // OK
		.features = BIT(DPU_DSC_HW_REV_1_2) | BIT(DPU_DSC_NATIVE_42x_EN), // TODO
		.sblk = &dsc_sblk_0, // TODO
	}, {
		.name = "dce_0_1", .id = DSC_1, // OK
		.base = 0x80000, .len = 0x6, // OK
		.features = BIT(DPU_DSC_HW_REV_1_2) | BIT(DPU_DSC_NATIVE_42x_EN), // TODO
		.sblk = &dsc_sblk_1, // TODO
	},
};

static const struct dpu_wb_cfg milos_wb[] = {
	{
		.name = "wb_2", .id = WB_2, // TODO
		.base = 0x65000, .len = 0x2c8, // OK
		.features = WB_SM8250_MASK, // TODO
		.format_list = wb2_formats_rgb_yuv, // TODO
		.num_formats = ARRAY_SIZE(wb2_formats_rgb_yuv), // TODO
		.xin_id = 6, // OK
		.vbif_idx = VBIF_RT, // TODO
		.maxlinewidth = 4096, // OK
		.intr_wb_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 4), // TODO
	},
};

static const struct dpu_cwb_cfg milos_cwb[] = {
	{
		.name = "cwb_0", .id = CWB_0,
		.base = 0x66200, .len = 0x8,
	},
};

static const struct dpu_intf_cfg milos_intf[] = {
	{
		.name = "intf_0", .id = INTF_0, // OK
		.base = 0x34000, .len = 0x280, // OK size=0x300?
		.features = INTF_SC7280_MASK, // TODO
		.type = INTF_DP, // OK
		.controller_id = MSM_DP_CONTROLLER_0, // OK?
		.prog_fetch_lines_worst_case = 24, // TODO
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 24), // TODO
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 25), // TODO
	}, {
		.name = "intf_1", .id = INTF_1, // OK
		.base = 0x35000, .len = 0x300, // OK size=0x300?
		.features = INTF_SC7280_MASK, // TODO
		.type = INTF_DSI, // OK
		.controller_id = MSM_DSI_CONTROLLER_0, // OK?
		.prog_fetch_lines_worst_case = 24, // TODO
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 26), // TODO
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 27), // TODO
		.intr_tear_rd_ptr = DPU_IRQ_IDX(MDP_INTF1_TEAR_INTR, 2), // TODO
	}, {
		.name = "intf_3", .id = INTF_3, // TODO?
		.base = 0x37000, .len = 0x280, // OK size=0x300?
		.features = INTF_SC7280_MASK, // TODO
		.type = INTF_DP, // OK
		.controller_id = MSM_DP_CONTROLLER_1, // FIXME, only one DP controller?
		.prog_fetch_lines_worst_case = 24, // TODO
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 30), // TODO
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 31), // TODO
	},
};

static const struct dpu_perf_cfg milos_perf_data = {
	.max_bw_low = 7100000,
	.max_bw_high = 9800000,
	.min_core_ib = 2500000,
	.min_llcc_ib = 0,
	.min_dram_ib = 1600000,
	.min_prefill_lines = 35, // TODO
	/* FIXME: lut tables */
	.danger_lut_tbl = {0x3ffff, 0x3ffff, 0x0}, // TODO
	.safe_lut_tbl = {0xfe00, 0xfe00, 0xffff}, // TODO
	.qos_lut_tbl = { // TODO
		{.nentry = ARRAY_SIZE(sc7180_qos_linear),
		.entries = sc7180_qos_linear
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_macrotile),
		.entries = sc7180_qos_macrotile
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_nrt),
		.entries = sc7180_qos_nrt
		},
		/* TODO: macrotile-qseed is different from macrotile */
	},
	.cdp_cfg = { // TODO
		{.rd_enable = 1, .wr_enable = 1},
		{.rd_enable = 1, .wr_enable = 0}
	},
	.clk_inefficiency_factor = 105, // TODO
	.bw_inefficiency_factor = 120, // TODO
};

static const struct dpu_mdss_version milos_mdss_ver = {
	.core_major_ver = 10,
	.core_minor_ver = 2,
};

const struct dpu_mdss_cfg dpu_milos_cfg = {
	.mdss_ver = &milos_mdss_ver,
	.caps = &milos_dpu_caps,
	.mdp = &milos_mdp,
	.cdm = &dpu_cdm_5_x,
	.ctl_count = ARRAY_SIZE(milos_ctl),
	.ctl = milos_ctl,
	.sspp_count = ARRAY_SIZE(milos_sspp),
	.sspp = milos_sspp,
	.mixer_count = ARRAY_SIZE(milos_lm),
	.mixer = milos_lm,
	.dspp_count = ARRAY_SIZE(milos_dspp),
	.dspp = milos_dspp,
	.pingpong_count = ARRAY_SIZE(milos_pp),
	.pingpong = milos_pp,
	.dsc_count = ARRAY_SIZE(milos_dsc),
	.dsc = milos_dsc,
	.merge_3d_count = ARRAY_SIZE(milos_merge_3d),
	.merge_3d = milos_merge_3d,
	.wb_count = ARRAY_SIZE(milos_wb),
	.wb = milos_wb,
	.cwb_count = ARRAY_SIZE(milos_cwb),
	.cwb = milos_cwb,
	.intf_count = ARRAY_SIZE(milos_intf),
	.intf = milos_intf,
	.vbif_count = ARRAY_SIZE(milos_vbif),
	.vbif = milos_vbif,
	.perf = &milos_perf_data,
};

#endif
