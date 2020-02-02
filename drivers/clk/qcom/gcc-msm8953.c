/*
 * Copyright (c) 2015-2020, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#include <dt-bindings/clock/qcom,gcc-msm8953.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "common.h"
#include "gdsc.h"
#include "reset.h"

enum {
	P_XO,
	P_GPLL0,
	P_GPLL2,
	P_GPLL4,
	P_GPLL6,
	P_GPLL0_DIV2,
	P_GPLL0_DIV2_CCI,
	P_GPLL0_DIV2_MM,
	P_GPLL0_DIV2_USB3,
	P_GPLL6_DIV2,
	P_GPLL6_DIV2_GFX,
	P_GPLL6_DIV2_MOCK,
};

static const struct parent_map gcc_parent_map_0[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL4, 2 },
	{ P_GPLL0_DIV2, 4 },
};

static const char * const gcc_parent_names_0[] = {
	"xo",
	"gpll0_early",
	"gpll4_clk_src",
	"gpll0_early_div",
};

static const struct parent_map gcc_parent_map_1[] = {
	{ P_GPLL0, 1 },
	{ P_GPLL0_DIV2, 4 },
	{ P_GPLL2, 5 },
};

static const char * const gcc_parent_names_1[] = {
	"gpll0_early",
	"gpll0_early_div",
	"gpll2_clk_src",
};

static const struct parent_map gcc_parent_map_2[] = {
	{ P_GPLL0, 1 },
	{ P_GPLL0_DIV2_USB3, 2 },
	{ P_GPLL2, 4 },
	{ P_GPLL0_DIV2_MM, 5 },
};

static const char * const gcc_parent_names_2[] = {
	"gpll0", //
	"gpll0_early_div",
	"gpll2_clk_src",
	"gpll0_early_div",
};


static const struct parent_map gcc_parent_map_3[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 1 },
	{ P_GPLL6_DIV2_MOCK, 2 },
	{ P_GPLL0_DIV2_CCI, 3 },
	{ P_GPLL4, 4 },
	{ P_GPLL0_DIV2_MM, 5 },
	{ P_GPLL6_DIV2_GFX, 6 },
};

static const char * const gcc_parent_names_3[] = {
	"xo",
	"gpll0_early",
	"gpll6_div2",
	"gpll0_early_div",
	"gpll4_clk_src",
	"gpll0_early_div",
	"gpll6_div2",
};

static const struct parent_map gcc_parent_map_4[] = {
	{ P_GPLL0, 1 },
	{ P_GPLL6, 2 },
	{ P_GPLL2, 3 },
	{ P_GPLL0_DIV2, 4 },
	{ P_GPLL6_DIV2, 5 },
};

static const char * const gcc_parent_names_4[] = {
	"gpll0_early",
	"gpll6_clk_src",
	"gpll2_clk_src",
	"gpll0_early_div",
	"gpll6_div2"
};

static struct clk_fixed_factor xo = {
	.mult = 1,
	.div = 1,
	.hw.init = &(struct clk_init_data){
		.name = "xo",
		.parent_names = (const char *[]){ "xo_board" },
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_alpha_pll gpll0_early = {
	.offset = 0x21000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.enable_reg = 0x45000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data) {
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name = "gpll0_early",
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_fixed_factor gpll0_early_div = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "gpll0_early_div",
		.parent_names = (const char *[]){ "gpll0_early" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_alpha_pll_postdiv gpll0 = {
	.offset = 0x21000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll0",
		.parent_names = (const char *[]){ "gpll0_early" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
	},
};

static struct clk_alpha_pll gpll2_clk_src = {
	.offset = 0x4A000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.enable_reg = 0x45000,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gpll2_clk_src",
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct pll_vco gpll3_p_vco[] = {
	{ 1000000000, 2000000000, 0 },
};

static struct clk_alpha_pll gpll3_clk_src = {
	.offset = 0x22000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.vco_table = gpll3_p_vco,
	.num_vco = ARRAY_SIZE(gpll3_p_vco),
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gpll3_clk_src",
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_fixed_factor gpll3_div2 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "gpll3_div2",
		.parent_names = (const char *[]){ "gpll3_clk_src" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_alpha_pll_postdiv gpll3 = {
	.offset = 0x22000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll3",
		.parent_names = (const char *[]){ "gpll3_clk_src" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
	},
};

static struct clk_alpha_pll gpll4_clk_src = {
	.offset = 0x24000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.enable_reg = 0x45000,
		.enable_mask = BIT(5),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gpll4_clk_src",
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_alpha_pll gpll6_clk_src = {
	.offset = 0x37000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.enable_reg = 0x45000,
		.enable_mask = BIT(7),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gpll6_clk_src",
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_fixed_factor gpll6_div2 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "gpll6_div2",
		.parent_names = (const char *[]){ "gpll6_clk_src" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

#if 0
DEFINE_EXT_CLK(ext_pclk0_clk_src, NULL);
DEFINE_EXT_CLK(ext_byte0_clk_src, NULL);
DEFINE_EXT_CLK(ext_pclk1_clk_src, NULL);
DEFINE_EXT_CLK(ext_byte1_clk_src, NULL);
#endif


static struct freq_tbl ftbl_camss_top_ahb_clk_src[] = {
	F(40000000, P_GPLL0_DIV2, 10, 0, 0),
	F(80000000, P_GPLL0, 10, 0, 0),
	{ }
};

static struct clk_rcg2 camss_top_ahb_clk_src = {
	.cmd_rcgr = 0x5A000,
	.parent_map = gcc_parent_map_1,
	.hid_width = 5,
	.mnd_width = 16,
	.freq_tbl = ftbl_camss_top_ahb_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_1,
		.name ="camss_top_ahb_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_csi0_clk_src[] = {
	F(100000000, P_GPLL0_DIV2_MM, 4, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(465000000, P_GPLL2, 2, 0, 0),
	{ }
};

static struct clk_rcg2 csi0_clk_src = {
	.cmd_rcgr = 0x4E020,
	.freq_tbl = ftbl_csi0_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_2,
		.name ="csi0_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_apss_ahb_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(25000000, P_GPLL0_DIV2, 16, 0, 0),
	F(50000000, P_GPLL0, 16, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(133330000, P_GPLL0, 6, 0, 0),
	{ }
};

static struct clk_rcg2 apss_ahb_clk_src = {
	.cmd_rcgr = 0x46000,
	.freq_tbl = ftbl_apss_ahb_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="apss_ahb_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_csi1_clk_src[] = {
	F(100000000, P_GPLL0_DIV2, 4, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(465000000, P_GPLL2, 2, 0, 0),
	{ }
};

static struct clk_rcg2 csi1_clk_src = {
	.cmd_rcgr = 0x4F020,
	.freq_tbl = ftbl_csi1_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_1,
		.name ="csi1_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_csi2_clk_src[] = {
	F(100000000, P_GPLL0_DIV2, 4, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(465000000, P_GPLL2, 2, 0, 0),
	{ }
};

static struct clk_rcg2 csi2_clk_src = {
	.cmd_rcgr = 0x3C020,
	.freq_tbl = ftbl_csi2_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_1,
		.name ="csi2_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_vfe0_clk_src[] = {
	F(50000000, P_GPLL0_DIV2_MM, 8, 0, 0),
	F(100000000, P_GPLL0_DIV2_MM, 4, 0, 0),
	F(133330000, P_GPLL0, 6, 0, 0),
	F(160000000, P_GPLL0, 5, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(465000000, P_GPLL2, 2, 0, 0),
	{ }
};

static struct clk_rcg2 vfe0_clk_src = {
	.cmd_rcgr = 0x58000,
	.freq_tbl = ftbl_vfe0_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_2,
		.name ="vfe0_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_gfx3d_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0_DIV2_MM, 8, 0, 0),
	F(80000000, P_GPLL0_DIV2_MM, 5, 0, 0),
	F(100000000, P_GPLL0_DIV2_MM, 4, 0, 0),
	F(133330000, P_GPLL0_DIV2_MM, 3, 0, 0),
	F(160000000, P_GPLL0_DIV2_MM, 2.5, 0, 0),
	F(200000000, P_GPLL0_DIV2_MM, 2, 0, 0),
	F(216000000, P_GPLL6_DIV2_GFX, 2.5, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(320000000, P_GPLL0, 2.5, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(460800000, P_GPLL4, 2.5, 0, 0),

	{ }
};


static struct freq_tbl ftbl_gfx3d_clk_src_sdm450[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0_DIV2_MM, 8, 0, 0),
	F(80000000, P_GPLL0_DIV2_MM, 5, 0, 0),
	F(100000000, P_GPLL0_DIV2_MM, 4, 0, 0),
	F(133330000, P_GPLL0_DIV2_MM, 3, 0, 0),
	F(160000000, P_GPLL0_DIV2_MM, 2.5, 0, 0),
	F(200000000, P_GPLL0_DIV2_MM, 2, 0, 0),
	F(216000000, P_GPLL6_DIV2_GFX, 2.5, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(320000000, P_GPLL0, 2.5, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(460800000, P_GPLL4, 2.5, 0, 0),
	{ }
};


static struct freq_tbl ftbl_gfx3d_clk_src_sdm632[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0_DIV2_MM, 8, 0, 0),
	F(80000000, P_GPLL0_DIV2_MM, 5, 0, 0),
	F(100000000, P_GPLL0_DIV2_MM, 4, 0, 0),
	F(133330000, P_GPLL0_DIV2_MM, 3, 0, 0),
	F(160000000, P_GPLL0_DIV2_MM, 2.5, 0, 0),
	F(200000000, P_GPLL0_DIV2_MM, 2, 0, 0),
	F(216000000, P_GPLL6_DIV2_GFX, 2.5, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(320000000, P_GPLL0, 2.5, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(460800000, P_GPLL4, 2.5, 0, 0),

	{ }
};

static struct clk_rcg2 gfx3d_clk_src = {
	.cmd_rcgr = 0x59000,
	.freq_tbl = ftbl_gfx3d_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	//.non_local_control_timeout = 1000,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_3,
		.name ="gfx3d_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_vcodec0_clk_src[] = {
	F(114290000, P_GPLL0_DIV2, 3.5, 0, 0),
	F(228570000, P_GPLL0, 3.5, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	F(360000000, P_GPLL6, 3, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(465000000, P_GPLL2, 2, 0, 0),
	{ }
};


static struct freq_tbl ftbl_vcodec0_clk_src_540MHz[] = {
	F(114290000, P_GPLL0_DIV2, 3.5, 0, 0),
	F(228570000, P_GPLL0, 3.5, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	F(360000000, P_GPLL6, 3, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(465000000, P_GPLL2, 2, 0, 0),
	F(540000000, P_GPLL6, 2, 0, 0),
	{ }
};

static struct clk_rcg2 vcodec0_clk_src = {
	.cmd_rcgr = 0x4C000,
	.freq_tbl = ftbl_vcodec0_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_4,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_4,
		.name ="vcodec0_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_cpp_clk_src[] = {
	F(100000000, P_GPLL0_DIV2_MM, 4, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(320000000, P_GPLL0, 2.5, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(465000000, P_GPLL2, 2, 0, 0),
	{ }
};

static struct clk_rcg2 cpp_clk_src = {
	.cmd_rcgr = 0x58018,
	.freq_tbl = ftbl_cpp_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_2,
		.name ="cpp_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_jpeg0_clk_src[] = {
	F(66670000, P_GPLL0_DIV2, 6, 0, 0),
	F(133330000, P_GPLL0, 6, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	F(320000000, P_GPLL0, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 jpeg0_clk_src = {
	.cmd_rcgr = 0x57000,
	.freq_tbl = ftbl_jpeg0_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_1,
		.name ="jpeg0_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_mdp_clk_src[] = {
	F(50000000, P_GPLL0_DIV2, 8, 0, 0),
	F(80000000, P_GPLL0_DIV2, 5, 0, 0),
	F(160000000, P_GPLL0_DIV2, 2.5, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(320000000, P_GPLL0, 2.5, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	{ }
};

static struct clk_rcg2 mdp_clk_src = {
	.cmd_rcgr = 0x4D014,
	.freq_tbl = ftbl_mdp_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_1,
		.name ="mdp_clk_src",
		.ops = &clk_rcg2_ops,
	},
};

#if 0
static struct freq_tbl ftbl_pclk0_clk_src[] = {
	{
		.div_src_val = BVAL(10, 8, 0)
			| BVAL(4, 0, 0),
		.src_clk = &xo_clk_src.c,
		.freq_hz = 0,
	},
	{
		.div_src_val = BVAL(10, 8, 1)
			| BVAL(4, 0, 0),
		.src_clk = &ext_pclk0_clk_src.c,
		.freq_hz = 0,
	},
	{
		.div_src_val = BVAL(10, 8, 3)
			| BVAL(4, 0, 0),
		.src_clk = &ext_pclk1_clk_src.c,
		.freq_hz = 0,
	},
	{ }
};

static struct clk_rcg2 pclk0_clk_src = {
	.cmd_rcgr = 0x4D000,
	.freq_tbl = ftbl_pclk0_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = (const char*[]){
			"xo",
		},
		.name ="pclk0_clk_src",
		.ops = &clk_ops_pixel_multiparent,
	},
};

static struct freq_tbl ftbl_pclk1_clk_src[] = {
	{
		.div_src_val = BVAL(10, 8, 0)
			| BVAL(4, 0, 0),
		.src_clk = &xo_clk_src.c,
		.freq_hz = 0,
	},
	{
		.div_src_val = BVAL(10, 8, 1)
			| BVAL(4, 0, 0),
		.src_clk = &ext_pclk1_clk_src.c,
		.freq_hz = 0,
	},
	{
		.div_src_val = BVAL(10, 8, 3)
			| BVAL(4, 0, 0),
		.src_clk = &ext_pclk0_clk_src.c,
		.freq_hz = 0,
	},
	{ }
};

static struct clk_rcg2 pclk1_clk_src = {
	.cmd_rcgr = 0x4D0B8,
	.freq_tbl = ftbl_pclk1_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = (const char*[]){
			"xo",
		},
		.name ="pclk1_clk_src",
		.ops = &clk_ops_pixel_multiparent,
	},
};

#endif

static struct freq_tbl ftbl_usb30_master_clk_src[] = {
	F(80000000, P_GPLL0_DIV2_USB3, 5, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(133330000, P_GPLL0, 6, 0, 0),
	{ }
};

static struct clk_rcg2 usb30_master_clk_src = {
	.cmd_rcgr = 0x3F00C,
	.freq_tbl = ftbl_usb30_master_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_2,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_2,
		.name ="usb30_master_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_vfe1_clk_src[] = {
	F(50000000, P_GPLL0_DIV2_MM, 8, 0, 0),
	F(100000000, P_GPLL0_DIV2_MM, 4, 0, 0),
	F(133330000, P_GPLL0, 6, 0, 0),
	F(160000000, P_GPLL0, 5, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(465000000, P_GPLL2, 2, 0, 0),
	{ }
};

static struct clk_rcg2 vfe1_clk_src = {
	.cmd_rcgr = 0x58054,
	.freq_tbl = ftbl_vfe1_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_2,
		.name ="vfe1_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_apc0_droop_detector_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(576000000, P_GPLL4, 2, 0, 0),
	{ }
};

static struct clk_rcg2 apc0_droop_detector_clk_src = {
	.cmd_rcgr = 0x78008,
	.freq_tbl = ftbl_apc0_droop_detector_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="apc0_droop_detector_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_apc1_droop_detector_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(400000000, P_GPLL0, 2, 0, 0),
	F(576000000, P_GPLL4, 2, 0, 0),
	{ }
};

static struct clk_rcg2 apc1_droop_detector_clk_src = {
	.cmd_rcgr = 0x79008,
	.freq_tbl = ftbl_apc1_droop_detector_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="apc1_droop_detector_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_blsp_i2c_apps_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(25000000, P_GPLL0_DIV2, 16, 0, 0),
	F(50000000, P_GPLL0, 16, 0, 0),
	{ }
};

static struct clk_rcg2 blsp1_qup1_i2c_apps_clk_src = {
	.cmd_rcgr = 0x0200C,
	.freq_tbl = ftbl_blsp_i2c_apps_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="blsp1_qup1_i2c_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_blsp_spi_apps_clk_src[] = {
	F(960000, P_XO, 10, 1, 2),
	F(4800000, P_XO, 4, 0, 0),
	F(9600000, P_XO, 2, 0, 0),
	F(12500000, P_GPLL0_DIV2, 16, 1, 2),
	F(16000000, P_GPLL0, 10, 1, 5),
	F(19200000, P_XO, 1, 0, 0),
	F(25000000, P_GPLL0, 16, 1, 2),
	F(50000000, P_GPLL0, 16, 0, 0),
	{ }
};

static struct clk_rcg2 blsp1_qup1_spi_apps_clk_src = {
	.cmd_rcgr = 0x02024,
	.freq_tbl = ftbl_blsp_spi_apps_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="blsp1_qup1_spi_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup2_i2c_apps_clk_src = {
	.cmd_rcgr = 0x03000,
	.freq_tbl = ftbl_blsp_i2c_apps_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="blsp1_qup2_i2c_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup2_spi_apps_clk_src = {
	.cmd_rcgr = 0x03014,
	.freq_tbl = ftbl_blsp_spi_apps_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="blsp1_qup2_spi_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup3_i2c_apps_clk_src = {
	.cmd_rcgr = 0x04000,
	.freq_tbl = ftbl_blsp_i2c_apps_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="blsp1_qup3_i2c_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup3_spi_apps_clk_src = {
	.cmd_rcgr = 0x04024,
	.freq_tbl = ftbl_blsp_spi_apps_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="blsp1_qup3_spi_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup4_i2c_apps_clk_src = {
	.cmd_rcgr = 0x05000,
	.freq_tbl = ftbl_blsp_i2c_apps_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="blsp1_qup4_i2c_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_qup4_spi_apps_clk_src = {
	.cmd_rcgr = 0x05024,
	.freq_tbl = ftbl_blsp_spi_apps_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="blsp1_qup4_spi_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_blsp_uart_apps_clk_src[] = {
	F(3686400, P_GPLL0_DIV2, 1, 144, 15625),
	F(7372800, P_GPLL0_DIV2, 1, 288, 15625),
	F(14745600, P_GPLL0_DIV2, 1, 576, 15625),
	F(16000000, P_GPLL0_DIV2, 5, 1, 5),
	F(19200000, P_XO, 1, 0, 0),
	F(24000000, P_GPLL0, 1, 3, 100),
	F(25000000, P_GPLL0, 16, 1, 2),
	F(32000000, P_GPLL0, 1, 1, 25),
	F(40000000, P_GPLL0, 1, 1, 20),
	F(46400000, P_GPLL0, 1, 29, 500),
	F(48000000, P_GPLL0, 1, 3, 50),
	F(51200000, P_GPLL0, 1, 8, 125),
	F(56000000, P_GPLL0, 1, 7, 100),
	F(58982400, P_GPLL0, 1, 1152, 15625),
	F(60000000, P_GPLL0, 1, 3, 40),
	F(64000000, P_GPLL0, 1, 2, 25),
	{ }
};

static struct clk_rcg2 blsp1_uart1_apps_clk_src = {
	.cmd_rcgr = 0x02044,
	.freq_tbl = ftbl_blsp_uart_apps_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="blsp1_uart1_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp1_uart2_apps_clk_src = {
	.cmd_rcgr = 0x03034,
	.freq_tbl = ftbl_blsp_uart_apps_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="blsp1_uart2_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup1_i2c_apps_clk_src = {
	.cmd_rcgr = 0x0C00C,
	.freq_tbl = ftbl_blsp_i2c_apps_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="blsp2_qup1_i2c_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup1_spi_apps_clk_src = {
	.cmd_rcgr = 0x0C024,
	.freq_tbl = ftbl_blsp_spi_apps_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="blsp2_qup1_spi_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup2_i2c_apps_clk_src = {
	.cmd_rcgr = 0x0D000,
	.freq_tbl = ftbl_blsp_i2c_apps_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="blsp2_qup2_i2c_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup2_spi_apps_clk_src = {
	.cmd_rcgr = 0x0D014,
	.freq_tbl = ftbl_blsp_spi_apps_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="blsp2_qup2_spi_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup3_i2c_apps_clk_src = {
	.cmd_rcgr = 0x0F000,
	.freq_tbl = ftbl_blsp_i2c_apps_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="blsp2_qup3_i2c_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup3_spi_apps_clk_src = {
	.cmd_rcgr = 0x0F024,
	.freq_tbl = ftbl_blsp_spi_apps_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="blsp2_qup3_spi_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup4_i2c_apps_clk_src = {
	.cmd_rcgr = 0x18000,
	.freq_tbl = ftbl_blsp_i2c_apps_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="blsp2_qup4_i2c_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_qup4_spi_apps_clk_src = {
	.cmd_rcgr = 0x18024,
	.freq_tbl = ftbl_blsp_spi_apps_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="blsp2_qup4_spi_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_uart1_apps_clk_src = {
	.cmd_rcgr = 0x0C044,
	.freq_tbl = ftbl_blsp_uart_apps_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="blsp2_uart1_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 blsp2_uart2_apps_clk_src = {
	.cmd_rcgr = 0x0D034,
	.freq_tbl = ftbl_blsp_uart_apps_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="blsp2_uart2_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_cci_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(37500000, P_GPLL0_DIV2_CCI, 1, 3, 32),
	{ }
};

static struct clk_rcg2 cci_clk_src = {
	.cmd_rcgr = 0x51000,
	.freq_tbl = ftbl_cci_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_3,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_3,
		.name ="cci_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_csi0p_clk_src[] = {
	F(66670000, P_GPLL0_DIV2_MM, 6, 0, 0),
	F(133330000, P_GPLL0, 6, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	{ }
};

static struct clk_rcg2 csi0p_clk_src = {
	.cmd_rcgr = 0x58084,
	.freq_tbl = ftbl_csi0p_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_2,
		.name ="csi0p_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_csi1p_clk_src[] = {
	F(66670000, P_GPLL0_DIV2_MM, 6, 0, 0),
	F(133330000, P_GPLL0, 6, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	{ }
};

static struct clk_rcg2 csi1p_clk_src = {
	.cmd_rcgr = 0x58094,
	.freq_tbl = ftbl_csi1p_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_2,
		.name ="csi1p_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_csi2p_clk_src[] = {
	F(66670000, P_GPLL0_DIV2_MM, 6, 0, 0),
	F(133330000, P_GPLL0, 6, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	F(310000000, P_GPLL2, 3, 0, 0),
	{ }
};

static struct clk_rcg2 csi2p_clk_src = {
	.cmd_rcgr = 0x580A4,
	.freq_tbl = ftbl_csi2p_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_2,
		.name ="csi2p_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_camss_gp0_clk_src[] = {
	F(50000000, P_GPLL0_DIV2, 8, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	{ }
};

static struct clk_rcg2 camss_gp0_clk_src = {
	.cmd_rcgr = 0x54000,
	.freq_tbl = ftbl_camss_gp0_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_1,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_1,
		.name ="camss_gp0_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_camss_gp1_clk_src[] = {
	F(50000000, P_GPLL0_DIV2, 8, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	{ }
};

static struct clk_rcg2 camss_gp1_clk_src = {
	.cmd_rcgr = 0x55000,
	.freq_tbl = ftbl_camss_gp1_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_1,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_1,
		.name ="camss_gp1_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_mclk0_clk_src[] = {
	F(24000000, P_GPLL6_DIV2, 1, 2, 45),
	F(33330000, P_GPLL0_DIV2, 12, 0, 0),
	F(36610000, P_GPLL6, 1, 2, 59),
	F(66667000, P_GPLL0, 12, 0, 0),
	{ }
};

static struct clk_rcg2 mclk0_clk_src = {
	.cmd_rcgr = 0x52000,
	.freq_tbl = ftbl_mclk0_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_4,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_4,
		.name ="mclk0_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_mclk1_clk_src[] = {
	F(24000000, P_GPLL6_DIV2, 1, 2, 45),
	F(33330000, P_GPLL0_DIV2, 12, 0, 0),
	F(36610000, P_GPLL6, 1, 2, 59),
	F(66667000, P_GPLL0, 12, 0, 0),
	{ }
};

static struct clk_rcg2 mclk1_clk_src = {
	.cmd_rcgr = 0x53000,
	.freq_tbl = ftbl_mclk1_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_4,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_4,
		.name ="mclk1_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_mclk2_clk_src[] = {
	F(24000000, P_GPLL6_DIV2, 1, 2, 45),
	F(33330000, P_GPLL0_DIV2, 12, 0, 0),
	F(36610000, P_GPLL6, 1, 2, 59),
	F(66667000, P_GPLL0, 12, 0, 0),
	{ }
};

static struct clk_rcg2 mclk2_clk_src = {
	.cmd_rcgr = 0x5C000,
	.freq_tbl = ftbl_mclk2_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_4,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_4,
		.name ="mclk2_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_mclk3_clk_src[] = {
	F(24000000, P_GPLL6_DIV2, 1, 2, 45),
	F(33330000, P_GPLL0_DIV2, 12, 0, 0),
	F(36610000, P_GPLL6, 1, 2, 59),
	F(66667000, P_GPLL0, 12, 0, 0),
	{ }
};

static struct clk_rcg2 mclk3_clk_src = {
	.cmd_rcgr = 0x5E000,
	.freq_tbl = ftbl_mclk3_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_4,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_4,
		.name ="mclk3_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_csi0phytimer_clk_src[] = {
	F(100000000, P_GPLL0_DIV2, 4, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	{ }
};

static struct clk_rcg2 csi0phytimer_clk_src = {
	.cmd_rcgr = 0x4E000,
	.freq_tbl = ftbl_csi0phytimer_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_1,
		.name ="csi0phytimer_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_csi1phytimer_clk_src[] = {
	F(100000000, P_GPLL0_DIV2, 4, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	{ }
};

static struct clk_rcg2 csi1phytimer_clk_src = {
	.cmd_rcgr = 0x4F000,
	.freq_tbl = ftbl_csi1phytimer_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="csi1phytimer_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_csi2phytimer_clk_src[] = {
	F(100000000, P_GPLL0_DIV2, 4, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	F(266670000, P_GPLL0, 3, 0, 0),
	{ }
};

static struct clk_rcg2 csi2phytimer_clk_src = {
	.cmd_rcgr = 0x4F05C,
	.freq_tbl = ftbl_csi2phytimer_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="csi2phytimer_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_crypto_clk_src[] = {
	F(40000000, P_GPLL0_DIV2, 10, 0, 0),
	F(80000000, P_GPLL0, 10, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(160000000, P_GPLL0, 5, 0, 0),
	{ }
};

static struct clk_rcg2 crypto_clk_src = {
	.cmd_rcgr = 0x16004,
	.freq_tbl = ftbl_crypto_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="crypto_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_gp1_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gp1_clk_src = {
	.cmd_rcgr = 0x08004,
	.freq_tbl = ftbl_gp1_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="gp1_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_gp2_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gp2_clk_src = {
	.cmd_rcgr = 0x09004,
	.freq_tbl = ftbl_gp2_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="gp2_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_gp3_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gp3_clk_src = {
	.cmd_rcgr = 0x0A004,
	.freq_tbl = ftbl_gp3_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="gp3_clk_src",
		.ops = &clk_rcg2_ops,
	},
};

#if 0
static struct freq_tbl ftbl_byte0_clk_src[] = {
	{
		.div_src_val = BVAL(10, 8, 0)
			| BVAL(4, 0, 0),
		.src_clk = &xo_clk_src.c,
		.freq_hz = 0,
	},
	{
		.div_src_val = BVAL(10, 8, 1)
			| BVAL(4, 0, 0),
		.src_clk = &ext_byte0_clk_src.c,
		.freq_hz = 0,
	},
	{
		.div_src_val = BVAL(10, 8, 3)
			| BVAL(4, 0, 0),
		.src_clk = &ext_byte1_clk_src.c,
		.freq_hz = 0,
	},
	{ }
};

static struct clk_rcg2 byte0_clk_src = {
	.cmd_rcgr = 0x4D044,
	.freq_tbl = ftbl_byte0_clk_src,
	.hid_width = 5,
	.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = (const char*[]){
			"xo",
		},
		.name ="byte0_clk_src",
		.ops = &clk_ops_byte_multiparent,
	},
};

static struct freq_tbl ftbl_byte1_clk_src[] = {
	{
		.div_src_val = BVAL(10, 8, 0)
			| BVAL(4, 0, 0),
		.src_clk = &xo_clk_src.c,
		.freq_hz = 0,
	},
	{
		.div_src_val = BVAL(10, 8, 1)
			| BVAL(4, 0, 0),
		.src_clk = &ext_byte1_clk_src.c,
		.freq_hz = 0,
	},
	{
		.div_src_val = BVAL(10, 8, 3)
			| BVAL(4, 0, 0),
		.src_clk = &ext_byte0_clk_src.c,
		.freq_hz = 0,
	},
	{ }
};

static struct clk_rcg2 byte1_clk_src = {
	.cmd_rcgr = 0x4D0B0,
	.freq_tbl = ftbl_byte1_clk_src,
	.hid_width = 5,
	.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = (const char*[]){
			"xo",
		},
		.name ="byte1_clk_src",
		.ops = &clk_ops_byte_multiparent,
	},
};
#endif


static struct freq_tbl ftbl_esc0_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 esc0_clk_src = {
	.cmd_rcgr = 0x4D05C,
	.freq_tbl = ftbl_esc0_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="esc0_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_esc1_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 esc1_clk_src = {
	.cmd_rcgr = 0x4D0A8,
	.freq_tbl = ftbl_esc1_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="esc1_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_vsync_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 vsync_clk_src = {
	.cmd_rcgr = 0x4D02C,
	.freq_tbl = ftbl_vsync_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="vsync_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_pdm2_clk_src[] = {
	F(32000000, P_GPLL0_DIV2, 12.5, 0, 0),
	F(64000000, P_GPLL0, 12.5, 0, 0),
	{ }
};

static struct clk_rcg2 pdm2_clk_src = {
	.cmd_rcgr = 0x44010,
	.freq_tbl = ftbl_pdm2_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_1,
		.name ="pdm2_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_rbcpr_gfx_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0, 16, 0, 0),
	{ }
};

static struct clk_rcg2 rbcpr_gfx_clk_src = {
	.cmd_rcgr = 0x3A00C,
	.freq_tbl = ftbl_rbcpr_gfx_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="rbcpr_gfx_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_sdcc1_apps_clk_src[] = {
	F(144000, P_XO, 16, 3, 25),
	F(400000, P_XO, 12, 1, 4),
	F(20000000, P_GPLL0_DIV2, 5, 1, 4),
	F(25000000, P_GPLL0_DIV2, 16, 0, 0),
	F(50000000, P_GPLL0, 16, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(177770000, P_GPLL0, 4.5, 0, 0),
	F(192000000, P_GPLL4, 6, 0, 0),
	F(384000000, P_GPLL4, 3, 0, 0),
	{ }
};

static struct clk_rcg2 sdcc1_apps_clk_src = {
	.cmd_rcgr = 0x42004,
	.freq_tbl = ftbl_sdcc1_apps_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="sdcc1_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_sdcc1_ice_core_clk_src[] = {
	F(80000000, P_GPLL0_DIV2, 5, 0, 0),
	F(160000000, P_GPLL0, 5, 0, 0),
	F(270000000, P_GPLL6, 4, 0, 0),
	{ }
};

static struct clk_rcg2 sdcc1_ice_core_clk_src = {
	.cmd_rcgr = 0x5D000,
	.freq_tbl = ftbl_sdcc1_ice_core_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_4,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_4,
		.name ="sdcc1_ice_core_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_sdcc2_apps_clk_src[] = {
	F(144000, P_XO, 16, 3, 25),
	F(400000, P_XO, 12, 1, 4),
	F(20000000, P_GPLL0_DIV2, 5, 1, 4),
	F(25000000, P_GPLL0_DIV2, 16, 0, 0),
	F(50000000, P_GPLL0, 16, 0, 0),
	F(100000000, P_GPLL0, 8, 0, 0),
	F(177770000, P_GPLL0, 4.5, 0, 0),
	F(192000000, P_GPLL4, 6, 0, 0),
	F(200000000, P_GPLL0, 4, 0, 0),
	{ }
};

static struct clk_rcg2 sdcc2_apps_clk_src = {
	.cmd_rcgr = 0x43004,
	.freq_tbl = ftbl_sdcc2_apps_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="sdcc2_apps_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_usb30_mock_utmi_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
//	F(60000000, P_GPLL6_DIV2_MOCK, 9, 1, 1),
	{ }
};

static struct clk_rcg2 usb30_mock_utmi_clk_src = {
	.cmd_rcgr = 0x3F020,
	.freq_tbl = ftbl_usb30_mock_utmi_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_3,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_3,
		.name ="usb30_mock_utmi_clk_src",
		.ops = &clk_rcg2_ops,
	},
};


static struct freq_tbl ftbl_usb3_aux_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 usb3_aux_clk_src = {
	.cmd_rcgr = 0x3F05C,
	.freq_tbl = ftbl_usb3_aux_clk_src,
	.hid_width = 5,
	.mnd_width = 16,
	.parent_map = gcc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.num_parents = 1,
		.parent_names = gcc_parent_names_0,
		.name ="usb3_aux_clk_src",
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_apc0_droop_detector_gpll0_clk = {
	.halt_reg = 0x78004,
	.clkr = {
		.enable_reg = 0x78004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"apc0_droop_detector_clk_src",
			},
			.name ="gcc_apc0_droop_detector_gpll0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_apc1_droop_detector_gpll0_clk = {
	.halt_reg = 0x79004,
	.clkr = {
		.enable_reg = 0x79004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"apc1_droop_detector_clk_src",
			},
			.name ="gcc_apc1_droop_detector_gpll0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup1_i2c_apps_clk = {
	.halt_reg = 0x02008,
	.clkr = {
		.enable_reg = 0x02008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"blsp1_qup1_i2c_apps_clk_src",
			},
			.name ="gcc_blsp1_qup1_i2c_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup1_spi_apps_clk = {
	.halt_reg = 0x02004,
	.clkr = {
		.enable_reg = 0x02004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"blsp1_qup1_spi_apps_clk_src",
			},
			.name ="gcc_blsp1_qup1_spi_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup2_i2c_apps_clk = {
	.halt_reg = 0x03010,
	.clkr = {
		.enable_reg = 0x03010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"blsp1_qup2_i2c_apps_clk_src",
			},
			.name ="gcc_blsp1_qup2_i2c_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup2_spi_apps_clk = {
	.halt_reg = 0x0300C,
	.clkr = {
		.enable_reg = 0x0300C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"blsp1_qup2_spi_apps_clk_src",
			},
			.name ="gcc_blsp1_qup2_spi_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup3_i2c_apps_clk = {
	.halt_reg = 0x04020,
	.clkr = {
		.enable_reg = 0x04020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"blsp1_qup3_i2c_apps_clk_src",
			},
			.name ="gcc_blsp1_qup3_i2c_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup3_spi_apps_clk = {
	.halt_reg = 0x0401C,
	.clkr = {
		.enable_reg = 0x0401C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"blsp1_qup3_spi_apps_clk_src",
			},
			.name ="gcc_blsp1_qup3_spi_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup4_i2c_apps_clk = {
	.halt_reg = 0x05020,
	.clkr = {
		.enable_reg = 0x05020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"blsp1_qup4_i2c_apps_clk_src",
			},
			.name ="gcc_blsp1_qup4_i2c_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup4_spi_apps_clk = {
	.halt_reg = 0x0501C,
	.clkr = {
		.enable_reg = 0x0501C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"blsp1_qup4_spi_apps_clk_src",
			},
			.name ="gcc_blsp1_qup4_spi_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart1_apps_clk = {
	.halt_reg = 0x0203C,
	.clkr = {
		.enable_reg = 0x0203C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"blsp1_uart1_apps_clk_src",
			},
			.name ="gcc_blsp1_uart1_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart2_apps_clk = {
	.halt_reg = 0x0302C,
	.clkr = {
		.enable_reg = 0x0302C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"blsp1_uart2_apps_clk_src",
			},
			.name ="gcc_blsp1_uart2_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup1_i2c_apps_clk = {
	.halt_reg = 0x0C008,
	.clkr = {
		.enable_reg = 0x0C008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"blsp2_qup1_i2c_apps_clk_src",
			},
			.name ="gcc_blsp2_qup1_i2c_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup1_spi_apps_clk = {
	.halt_reg = 0x0C004,
	.clkr = {
		.enable_reg = 0x0C004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"blsp2_qup1_spi_apps_clk_src",
			},
			.name ="gcc_blsp2_qup1_spi_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup2_i2c_apps_clk = {
	.halt_reg = 0x0D010,
	.clkr = {
		.enable_reg = 0x0D010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"blsp2_qup2_i2c_apps_clk_src",
			},
			.name ="gcc_blsp2_qup2_i2c_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup2_spi_apps_clk = {
	.halt_reg = 0x0D00C,
	.clkr = {
		.enable_reg = 0x0D00C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"blsp2_qup2_spi_apps_clk_src",
			},
			.name ="gcc_blsp2_qup2_spi_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup3_i2c_apps_clk = {
	.halt_reg = 0x0F020,
	.clkr = {
		.enable_reg = 0x0F020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"blsp2_qup3_i2c_apps_clk_src",
			},
			.name ="gcc_blsp2_qup3_i2c_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup3_spi_apps_clk = {
	.halt_reg = 0x0F01C,
	.clkr = {
		.enable_reg = 0x0F01C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"blsp2_qup3_spi_apps_clk_src",
			},
			.name ="gcc_blsp2_qup3_spi_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup4_i2c_apps_clk = {
	.halt_reg = 0x18020,
	.clkr = {
		.enable_reg = 0x18020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"blsp2_qup4_i2c_apps_clk_src",
			},
			.name ="gcc_blsp2_qup4_i2c_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_qup4_spi_apps_clk = {
	.halt_reg = 0x1801C,
	.clkr = {
		.enable_reg = 0x1801C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"blsp2_qup4_spi_apps_clk_src",
			},
			.name ="gcc_blsp2_qup4_spi_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_uart1_apps_clk = {
	.halt_reg = 0x0C03C,
	.clkr = {
		.enable_reg = 0x0C03C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"blsp2_uart1_apps_clk_src",
			},
			.name ="gcc_blsp2_uart1_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_uart2_apps_clk = {
	.halt_reg = 0x0D02C,
	.clkr = {
		.enable_reg = 0x0D02C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"blsp2_uart2_apps_clk_src",
			},
			.name ="gcc_blsp2_uart2_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_bimc_gpu_clk = {
	.halt_reg = 0x59030,
	.clkr = {
		.enable_reg = 0x59030,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_bimc_gpu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_cci_ahb_clk = {
	.halt_reg = 0x5101C,
	.clkr = {
		.enable_reg = 0x5101C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"camss_top_ahb_clk_src",
			},
			.name ="gcc_camss_cci_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_cci_clk = {
	.halt_reg = 0x51018,
	.clkr = {
		.enable_reg = 0x51018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"cci_clk_src",
			},
			.name ="gcc_camss_cci_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_cpp_ahb_clk = {
	.halt_reg = 0x58040,
	.clkr = {
		.enable_reg = 0x58040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"camss_top_ahb_clk_src",
			},
			.name ="gcc_camss_cpp_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_cpp_axi_clk = {
	.halt_reg = 0x58064,
	.clkr = {
		.enable_reg = 0x58064,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_camss_cpp_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_cpp_clk = {
	.halt_reg = 0x5803C,
	.clkr = {
		.enable_reg = 0x5803C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"cpp_clk_src",
			},
			.name ="gcc_camss_cpp_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi0_ahb_clk = {
	.halt_reg = 0x4E040,
	.clkr = {
		.enable_reg = 0x4E040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"camss_top_ahb_clk_src",
			},
			.name ="gcc_camss_csi0_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi0_clk = {
	.halt_reg = 0x4E03C,
	.clkr = {
		.enable_reg = 0x4E03C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"csi0_clk_src",
			},
			.name ="gcc_camss_csi0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi0_csiphy_3p_clk = {
	.halt_reg = 0x58090,
	.clkr = {
		.enable_reg = 0x58090,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"csi0p_clk_src",
			},
			.name ="gcc_camss_csi0_csiphy_3p_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi0phy_clk = {
	.halt_reg = 0x4E048,
	.clkr = {
		.enable_reg = 0x4E048,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"csi0_clk_src",
			},
			.name ="gcc_camss_csi0phy_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi0pix_clk = {
	.halt_reg = 0x4E058,
	.clkr = {
		.enable_reg = 0x4E058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"csi0_clk_src",
			},
			.name ="gcc_camss_csi0pix_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi0rdi_clk = {
	.halt_reg = 0x4E050,
	.clkr = {
		.enable_reg = 0x4E050,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"csi0_clk_src",
			},
			.name ="gcc_camss_csi0rdi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi1_ahb_clk = {
	.halt_reg = 0x4F040,
	.clkr = {
		.enable_reg = 0x4F040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"camss_top_ahb_clk_src",
			},
			.name ="gcc_camss_csi1_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi1_clk = {
	.halt_reg = 0x4F03C,
	.clkr = {
		.enable_reg = 0x4F03C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"csi1_clk_src",
			},
			.name ="gcc_camss_csi1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi1_csiphy_3p_clk = {
	.halt_reg = 0x580A0,
	.clkr = {
		.enable_reg = 0x580A0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"csi1p_clk_src",
			},
			.name ="gcc_camss_csi1_csiphy_3p_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi1phy_clk = {
	.halt_reg = 0x4F048,
	.clkr = {
		.enable_reg = 0x4F048,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"csi1_clk_src",
			},
			.name ="gcc_camss_csi1phy_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi1pix_clk = {
	.halt_reg = 0x4F058,
	.clkr = {
		.enable_reg = 0x4F058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"csi1_clk_src",
			},
			.name ="gcc_camss_csi1pix_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi1rdi_clk = {
	.halt_reg = 0x4F050,
	.clkr = {
		.enable_reg = 0x4F050,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"csi1_clk_src",
			},
			.name ="gcc_camss_csi1rdi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi2_ahb_clk = {
	.halt_reg = 0x3C040,
	.clkr = {
		.enable_reg = 0x3C040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"camss_top_ahb_clk_src",
			},
			.name ="gcc_camss_csi2_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi2_clk = {
	.halt_reg = 0x3C03C,
	.clkr = {
		.enable_reg = 0x3C03C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"csi2_clk_src",
			},
			.name ="gcc_camss_csi2_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi2_csiphy_3p_clk = {
	.halt_reg = 0x580B0,
	.clkr = {
		.enable_reg = 0x580B0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"csi2p_clk_src",
			},
			.name ="gcc_camss_csi2_csiphy_3p_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi2phy_clk = {
	.halt_reg = 0x3C048,
	.clkr = {
		.enable_reg = 0x3C048,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"csi2_clk_src",
			},
			.name ="gcc_camss_csi2phy_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi2pix_clk = {
	.halt_reg = 0x3C058,
	.clkr = {
		.enable_reg = 0x3C058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"csi2_clk_src",
			},
			.name ="gcc_camss_csi2pix_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi2rdi_clk = {
	.halt_reg = 0x3C050,
	.clkr = {
		.enable_reg = 0x3C050,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"csi2_clk_src",
			},
			.name ="gcc_camss_csi2rdi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi_vfe0_clk = {
	.halt_reg = 0x58050,
	.clkr = {
		.enable_reg = 0x58050,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"vfe0_clk_src",
			},
			.name ="gcc_camss_csi_vfe0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi_vfe1_clk = {
	.halt_reg = 0x58074,
	.clkr = {
		.enable_reg = 0x58074,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"vfe1_clk_src",
			},
			.name ="gcc_camss_csi_vfe1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_gp0_clk = {
	.halt_reg = 0x54018,
	.clkr = {
		.enable_reg = 0x54018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"camss_gp0_clk_src",
			},
			.name ="gcc_camss_gp0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_gp1_clk = {
	.halt_reg = 0x55018,
	.clkr = {
		.enable_reg = 0x55018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"camss_gp1_clk_src",
			},
			.name ="gcc_camss_gp1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_ispif_ahb_clk = {
	.halt_reg = 0x50004,
	.clkr = {
		.enable_reg = 0x50004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"camss_top_ahb_clk_src",
			},
			.name ="gcc_camss_ispif_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_jpeg0_clk = {
	.halt_reg = 0x57020,
	.clkr = {
		.enable_reg = 0x57020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"jpeg0_clk_src",
			},
			.name ="gcc_camss_jpeg0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_jpeg_ahb_clk = {
	.halt_reg = 0x57024,
	.clkr = {
		.enable_reg = 0x57024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"camss_top_ahb_clk_src",
			},
			.name ="gcc_camss_jpeg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_jpeg_axi_clk = {
	.halt_reg = 0x57028,
	.clkr = {
		.enable_reg = 0x57028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_camss_jpeg_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_mclk0_clk = {
	.halt_reg = 0x52018,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"mclk0_clk_src",
			},
			.name ="gcc_camss_mclk0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_mclk1_clk = {
	.halt_reg = 0x53018,
	.clkr = {
		.enable_reg = 0x53018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"mclk1_clk_src",
			},
			.name ="gcc_camss_mclk1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_mclk2_clk = {
	.halt_reg = 0x5C018,
	.clkr = {
		.enable_reg = 0x5C018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"mclk2_clk_src",
			},
			.name ="gcc_camss_mclk2_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_mclk3_clk = {
	.halt_reg = 0x5E018,
	.clkr = {
		.enable_reg = 0x5E018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"mclk3_clk_src",
			},
			.name ="gcc_camss_mclk3_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_micro_ahb_clk = {
	.halt_reg = 0x5600C,
	.clkr = {
		.enable_reg = 0x5600C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"camss_top_ahb_clk_src",
			},
			.name ="gcc_camss_micro_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi0phytimer_clk = {
	.halt_reg = 0x4E01C,
	.clkr = {
		.enable_reg = 0x4E01C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"csi0phytimer_clk_src",
			},
			.name ="gcc_camss_csi0phytimer_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi1phytimer_clk = {
	.halt_reg = 0x4F01C,
	.clkr = {
		.enable_reg = 0x4F01C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"csi1phytimer_clk_src",
			},
			.name ="gcc_camss_csi1phytimer_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi2phytimer_clk = {
	.halt_reg = 0x4F068,
	.clkr = {
		.enable_reg = 0x4F068,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"csi2phytimer_clk_src",
			},
			.name ="gcc_camss_csi2phytimer_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_ahb_clk = {
	.halt_reg = 0x56004,
	.clkr = {
		.enable_reg = 0x56004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_camss_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_top_ahb_clk = {
	.halt_reg = 0x5A014,
	.clkr = {
		.enable_reg = 0x5A014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"camss_top_ahb_clk_src",
			},
			.name ="gcc_camss_top_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_vfe0_clk = {
	.halt_reg = 0x58038,
	.clkr = {
		.enable_reg = 0x58038,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"vfe0_clk_src",
			},
			.name ="gcc_camss_vfe0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_vfe_ahb_clk = {
	.halt_reg = 0x58044,
	.clkr = {
		.enable_reg = 0x58044,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"camss_top_ahb_clk_src",
			},
			.name ="gcc_camss_vfe_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_vfe_axi_clk = {
	.halt_reg = 0x58048,
	.clkr = {
		.enable_reg = 0x58048,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_camss_vfe_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_vfe1_ahb_clk = {
	.halt_reg = 0x58060,
	.clkr = {
		.enable_reg = 0x58060,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"camss_top_ahb_clk_src",
			},
			.name ="gcc_camss_vfe1_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_vfe1_axi_clk = {
	.halt_reg = 0x58068,
	.clkr = {
		.enable_reg = 0x58068,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_camss_vfe1_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_vfe1_clk = {
	.halt_reg = 0x5805C,
	.clkr = {
		.enable_reg = 0x5805C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"vfe1_clk_src",
			},
			.name ="gcc_camss_vfe1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_dcc_clk = {
	.halt_reg = 0x77004,
	.clkr = {
		.enable_reg = 0x77004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_dcc_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp1_clk = {
	.halt_reg = 0x08000,
	.clkr = {
		.enable_reg = 0x08000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"gp1_clk_src",
			},
			.name ="gcc_gp1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp2_clk = {
	.halt_reg = 0x09000,
	.clkr = {
		.enable_reg = 0x09000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"gp2_clk_src",
			},
			.name ="gcc_gp2_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp3_clk = {
	.halt_reg = 0x0A000,
	.clkr = {
		.enable_reg = 0x0A000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"gp3_clk_src",
			},
			.name ="gcc_gp3_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_ahb_clk = {
	.halt_reg = 0x4D07C,
	.clkr = {
		.enable_reg = 0x4D07C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_mdss_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_axi_clk = {
	.halt_reg = 0x4D080,
	.clkr = {
		.enable_reg = 0x4D080,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_mdss_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_byte0_clk = {
	.halt_reg = 0x4D094,
	.clkr = {
		.enable_reg = 0x4D094,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"byte0_clk_src",
			},
			.name ="gcc_mdss_byte0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_byte1_clk = {
	.halt_reg = 0x4D0A0,
	.clkr = {
		.enable_reg = 0x4D0A0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"byte1_clk_src",
			},
			.name ="gcc_mdss_byte1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_esc0_clk = {
	.halt_reg = 0x4D098,
	.clkr = {
		.enable_reg = 0x4D098,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"esc0_clk_src",
			},
			.name ="gcc_mdss_esc0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_esc1_clk = {
	.halt_reg = 0x4D09C,
	.clkr = {
		.enable_reg = 0x4D09C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"esc1_clk_src",
			},
			.name ="gcc_mdss_esc1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_mdp_clk = {
	.halt_reg = 0x4D088,
	.clkr = {
		.enable_reg = 0x4D088,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"mdp_clk_src",
			},
			.name ="gcc_mdss_mdp_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_pclk0_clk = {
	.halt_reg = 0x4D084,
	.clkr = {
		.enable_reg = 0x4D084,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"pclk0_clk_src",
			},
			.name ="gcc_mdss_pclk0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_pclk1_clk = {
	.halt_reg = 0x4D0A4,
	.clkr = {
		.enable_reg = 0x4D0A4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"pclk1_clk_src",
			},
			.name ="gcc_mdss_pclk1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdss_vsync_clk = {
	.halt_reg = 0x4D090,
	.clkr = {
		.enable_reg = 0x4D090,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"vsync_clk_src",
			},
			.name ="gcc_mdss_vsync_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mss_cfg_ahb_clk = {
	.halt_reg = 0x49000,
	.clkr = {
		.enable_reg = 0x49000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_mss_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mss_q6_bimc_axi_clk = {
	.halt_reg = 0x49004,
	.clkr = {
		.enable_reg = 0x49004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_mss_q6_bimc_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_bimc_gfx_clk = {
	.halt_reg = 0x59034,
	.clkr = {
		.enable_reg = 0x59034,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_bimc_gfx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_oxili_ahb_clk = {
	.halt_reg = 0x59028,
	.clkr = {
		.enable_reg = 0x59028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_oxili_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_oxili_aon_clk = {
	.halt_reg = 0x59044,
	.clkr = {
		.enable_reg = 0x59044,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"gfx3d_clk_src",
			},
			.name ="gcc_oxili_aon_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_oxili_gfx3d_clk = {
	.halt_reg = 0x59020,
	.clkr = {
		.enable_reg = 0x59020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"gfx3d_clk_src",
			},
			.name ="gcc_oxili_gfx3d_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_oxili_timer_clk = {
	.halt_reg = 0x59040,
	.clkr = {
		.enable_reg = 0x59040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo_clk_src",
			},
			.name ="gcc_oxili_timer_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcnoc_usb3_axi_clk = {
	.halt_reg = 0x3F038,
	.clkr = {
		.enable_reg = 0x3F038,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"usb30_master_clk_src",
			},
			.flags = CLK_SET_RATE_PARENT,
			.name ="gcc_pcnoc_usb3_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm2_clk = {
	.halt_reg = 0x4400C,
	.clkr = {
		.enable_reg = 0x4400C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"pdm2_clk_src",
			},
			.name ="gcc_pdm2_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_ahb_clk = {
	.halt_reg = 0x44004,
	.clkr = {
		.enable_reg = 0x44004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_pdm_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};


static struct clk_branch gcc_rbcpr_gfx_clk = {
	.halt_reg = 0x3A004,
	.clkr = {
		.enable_reg = 0x3A004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"rbcpr_gfx_clk_src",
			},
			.name ="gcc_rbcpr_gfx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ahb_clk = {
	.halt_reg = 0x4201C,
	.clkr = {
		.enable_reg = 0x4201C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_sdcc1_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_apps_clk = {
	.halt_reg = 0x42018,
	.clkr = {
		.enable_reg = 0x42018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"sdcc1_apps_clk_src",
			},
			.name ="gcc_sdcc1_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ice_core_clk = {
	.halt_reg = 0x5D014,
	.clkr = {
		.enable_reg = 0x5D014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"sdcc1_ice_core_clk_src",
			},
			.name ="gcc_sdcc1_ice_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_ahb_clk = {
	.halt_reg = 0x4301C,
	.clkr = {
		.enable_reg = 0x4301C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_sdcc2_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_apps_clk = {
	.halt_reg = 0x43018,
	.clkr = {
		.enable_reg = 0x43018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_sdcc2_apps_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_master_clk = {
	.halt_reg = 0x3F000,
	.clkr = {
		.enable_reg = 0x3F000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"usb30_master_clk_src",
			},
			.flags = CLK_SET_RATE_PARENT,
			.name ="gcc_usb30_master_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_mock_utmi_clk = {
	.halt_reg = 0x3F008,
	.clkr = {
		.enable_reg = 0x3F008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"usb30_mock_utmi_clk_src",
			},
			.flags = CLK_SET_RATE_PARENT,
			.name ="gcc_usb30_mock_utmi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_sleep_clk = {
	.halt_reg = 0x3F004,
	.clkr = {
		.enable_reg = 0x3F004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.flags = CLK_SET_RATE_PARENT,
			.name ="gcc_usb30_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_aux_clk = {
	.halt_reg = 0x3F044,
	.clkr = {
		.enable_reg = 0x3F044,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"usb3_aux_clk_src",
			},
			.flags = CLK_SET_RATE_PARENT,
			.name ="gcc_usb3_aux_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb_phy_cfg_ahb_clk = {
	.halt_reg = 0x3F080,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x3F080,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"pcnoc_clk",
			},
			.name ="gcc_usb_phy_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_venus0_ahb_clk = {
	.halt_reg = 0x4C020,
	.clkr = {
		.enable_reg = 0x4C020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_venus0_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_venus0_axi_clk = {
	.halt_reg = 0x4C024,
	.clkr = {
		.enable_reg = 0x4C024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_venus0_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_venus0_core0_vcodec0_clk = {
	.halt_reg = 0x4C02C,
	.clkr = {
		.enable_reg = 0x4C02C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"vcodec0_clk_src",
			},
			.name ="gcc_venus0_core0_vcodec0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_venus0_vcodec0_clk = {
	.halt_reg = 0x4C01C,
	.clkr = {
		.enable_reg = 0x4C01C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"vcodec0_clk_src",
			},
			.name ="gcc_venus0_vcodec0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qusb_ref_clk = {
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x41030,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"bb_clk1",
			},
			.name ="gcc_qusb_ref_clk",
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch gcc_usb_ss_ref_clk = {
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x3F07C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"bb_clk1",
			},
			.name ="gcc_usb_ss_ref_clk",
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch gcc_usb3_pipe_clk = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x3F040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"gcc_usb_pipe_clk_src",
			},
			.name ="gcc_usb3_pipe_clk",
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch gcc_apss_ahb_clk = {
	.halt_reg = 0x4601C,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(14),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"apss_ahb_clk_src",
			},
			.name ="gcc_apss_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_apss_axi_clk = {
	.halt_reg = 0x46020,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(13),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_apss_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_ahb_clk = {
	.halt_reg = 0x01008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(10),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_blsp1_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp2_ahb_clk = {
	.halt_reg = 0x0B008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(20),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_blsp2_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_boot_rom_ahb_clk = {
	.halt_reg = 0x1300C,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(7),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_boot_rom_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};


static struct clk_branch gcc_crypto_ahb_clk = {
	.halt_reg = 0x16024,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_crypto_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_crypto_axi_clk = {
	.halt_reg = 0x16020,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_crypto_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_crypto_clk = {
	.halt_reg = 0x1601C,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"crypto_clk_src",
			},
			.name ="gcc_crypto_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qdss_dap_clk = {
	.halt_reg = 0x29084,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_qdss_dap_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_prng_ahb_clk = {
	.halt_reg = 0x13004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x45004,
		.enable_mask = BIT(8),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_prng_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_apss_tcu_async_clk = {
	.halt_reg = 0x12018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4500C,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_apss_tcu_async_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cpp_tbu_clk = {
	.halt_reg = 0x12040,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4500C,
		.enable_mask = BIT(14),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_cpp_tbu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_jpeg_tbu_clk = {
	.halt_reg = 0x12034,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4500C,
		.enable_mask = BIT(10),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_jpeg_tbu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdp_tbu_clk = {
	.halt_reg = 0x1201C,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4500C,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_mdp_tbu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_smmu_cfg_clk = {
	.halt_reg = 0x12038,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4500C,
		.enable_mask = BIT(12),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_smmu_cfg_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_venus_tbu_clk = {
	.halt_reg = 0x12014,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4500C,
		.enable_mask = BIT(5),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_venus_tbu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_vfe1_tbu_clk = {
	.halt_reg = 0x12090,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4500C,
		.enable_mask = BIT(17),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_vfe1_tbu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_vfe_tbu_clk = {
	.halt_reg = 0x1203C,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x4500C,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.num_parents = 1,
			.parent_names = (const char*[]){
				"xo",
			},
			.name ="gcc_vfe_tbu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc usb30_gdsc = {
	.gdscr = 0x3f078,
	.pd = {
		.name = "usb30_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct clk_hw *gcc_msm8953_hws[] = {
	&xo.hw,
	&gpll0_early_div.hw,
	&gpll3_div2.hw,
	&gpll6_div2.hw,
};

static struct clk_regmap *gcc_msm8953_clocks[] = {
	[GPLL0_EARLY] = &gpll0_early.clkr,
	[GPLL0] = &gpll0.clkr,
	[GPLL2_CLK_SRC] = &gpll2_clk_src.clkr,
	[GPLL3_CLK_SRC] = &gpll3_clk_src.clkr,
	[GPLL4_CLK_SRC] = &gpll4_clk_src.clkr,
	[GPLL6_CLK_SRC] = &gpll6_clk_src.clkr,
	[GCC_APSS_AHB_CLK] = &gcc_apss_ahb_clk.clkr,
	[GCC_APSS_AXI_CLK] = &gcc_apss_axi_clk.clkr,
	//[GCC_BLSP1_AHB_CLK] = &gcc_blsp1_ahb_clk.clkr,
	[GCC_BLSP2_AHB_CLK] = &gcc_blsp2_ahb_clk.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_CRYPTO_AHB_CLK] = &gcc_crypto_ahb_clk.clkr,
	[GCC_CRYPTO_AXI_CLK] = &gcc_crypto_axi_clk.clkr,
	[GCC_CRYPTO_CLK] = &gcc_crypto_clk.clkr,
	[GCC_PRNG_AHB_CLK] = &gcc_prng_ahb_clk.clkr,
	[GCC_QDSS_DAP_CLK] = &gcc_qdss_dap_clk.clkr,
	[GCC_APSS_TCU_ASYNC_CLK] = &gcc_apss_tcu_async_clk.clkr,
	[GCC_CPP_TBU_CLK] = &gcc_cpp_tbu_clk.clkr,
	[GCC_JPEG_TBU_CLK] = &gcc_jpeg_tbu_clk.clkr,
	[GCC_MDP_TBU_CLK] = &gcc_mdp_tbu_clk.clkr,
	[GCC_SMMU_CFG_CLK] = &gcc_smmu_cfg_clk.clkr,
	[GCC_VENUS_TBU_CLK] = &gcc_venus_tbu_clk.clkr,
	[GCC_VFE1_TBU_CLK] = &gcc_vfe1_tbu_clk.clkr,
	[GCC_VFE_TBU_CLK] = &gcc_vfe_tbu_clk.clkr,
	[CAMSS_TOP_AHB_CLK_SRC] = &camss_top_ahb_clk_src.clkr,
	[CSI0_CLK_SRC] = &csi0_clk_src.clkr,
	[APSS_AHB_CLK_SRC] = &apss_ahb_clk_src.clkr,
	[CSI1_CLK_SRC] = &csi1_clk_src.clkr,
	[CSI2_CLK_SRC] = &csi2_clk_src.clkr,
	[VFE0_CLK_SRC] = &vfe0_clk_src.clkr,
	[VCODEC0_CLK_SRC] = &vcodec0_clk_src.clkr,
	[CPP_CLK_SRC] = &cpp_clk_src.clkr,
	[JPEG0_CLK_SRC] = &jpeg0_clk_src.clkr,
	[USB30_MASTER_CLK_SRC] = &usb30_master_clk_src.clkr,
	[VFE1_CLK_SRC] = &vfe1_clk_src.clkr,
	[APC0_DROOP_DETECTOR_CLK_SRC] = &apc0_droop_detector_clk_src.clkr,
	[APC1_DROOP_DETECTOR_CLK_SRC] = &apc1_droop_detector_clk_src.clkr,
/*
	[BLSP1_QUP1_I2C_APPS_CLK_SRC] = &blsp1_qup1_i2c_apps_clk_src.clkr,
	[BLSP1_QUP1_SPI_APPS_CLK_SRC] = &blsp1_qup1_spi_apps_clk_src.clkr,
	[BLSP1_QUP2_I2C_APPS_CLK_SRC] = &blsp1_qup2_i2c_apps_clk_src.clkr,
	[BLSP1_QUP2_SPI_APPS_CLK_SRC] = &blsp1_qup2_spi_apps_clk_src.clkr,
	[BLSP1_QUP3_I2C_APPS_CLK_SRC] = &blsp1_qup3_i2c_apps_clk_src.clkr,
	[BLSP1_QUP3_SPI_APPS_CLK_SRC] = &blsp1_qup3_spi_apps_clk_src.clkr,
	[BLSP1_QUP4_I2C_APPS_CLK_SRC] = &blsp1_qup4_i2c_apps_clk_src.clkr,
	[BLSP1_QUP4_SPI_APPS_CLK_SRC] = &blsp1_qup4_spi_apps_clk_src.clkr,
	[BLSP1_UART1_APPS_CLK_SRC] = &blsp1_uart1_apps_clk_src.clkr,
	[BLSP1_UART2_APPS_CLK_SRC] = &blsp1_uart2_apps_clk_src.clkr,
 */
	[BLSP2_QUP1_I2C_APPS_CLK_SRC] = &blsp2_qup1_i2c_apps_clk_src.clkr,
	[BLSP2_QUP1_SPI_APPS_CLK_SRC] = &blsp2_qup1_spi_apps_clk_src.clkr,
	[BLSP2_QUP2_I2C_APPS_CLK_SRC] = &blsp2_qup2_i2c_apps_clk_src.clkr,
	[BLSP2_QUP2_SPI_APPS_CLK_SRC] = &blsp2_qup2_spi_apps_clk_src.clkr,
	[BLSP2_QUP3_I2C_APPS_CLK_SRC] = &blsp2_qup3_i2c_apps_clk_src.clkr,
	[BLSP2_QUP3_SPI_APPS_CLK_SRC] = &blsp2_qup3_spi_apps_clk_src.clkr,
	[BLSP2_QUP4_I2C_APPS_CLK_SRC] = &blsp2_qup4_i2c_apps_clk_src.clkr,
	[BLSP2_QUP4_SPI_APPS_CLK_SRC] = &blsp2_qup4_spi_apps_clk_src.clkr,
	[BLSP2_UART1_APPS_CLK_SRC] = &blsp2_uart1_apps_clk_src.clkr,
	[BLSP2_UART2_APPS_CLK_SRC] = &blsp2_uart2_apps_clk_src.clkr,
	[CCI_CLK_SRC] = &cci_clk_src.clkr,
	[CSI0P_CLK_SRC] = &csi0p_clk_src.clkr,
	[CSI1P_CLK_SRC] = &csi1p_clk_src.clkr,
	[CSI2P_CLK_SRC] = &csi2p_clk_src.clkr,
	[CAMSS_GP0_CLK_SRC] = &camss_gp0_clk_src.clkr,
	[CAMSS_GP1_CLK_SRC] = &camss_gp1_clk_src.clkr,
	[MCLK0_CLK_SRC] = &mclk0_clk_src.clkr,
	[MCLK1_CLK_SRC] = &mclk1_clk_src.clkr,
	[MCLK2_CLK_SRC] = &mclk2_clk_src.clkr,
	[MCLK3_CLK_SRC] = &mclk3_clk_src.clkr,
	[CSI0PHYTIMER_CLK_SRC] = &csi0phytimer_clk_src.clkr,
	[CSI1PHYTIMER_CLK_SRC] = &csi1phytimer_clk_src.clkr,
	[CSI2PHYTIMER_CLK_SRC] = &csi2phytimer_clk_src.clkr,
	[CRYPTO_CLK_SRC] = &crypto_clk_src.clkr,
	[GP1_CLK_SRC] = &gp1_clk_src.clkr,
	[GP2_CLK_SRC] = &gp2_clk_src.clkr,
	[GP3_CLK_SRC] = &gp3_clk_src.clkr,
	[PDM2_CLK_SRC] = &pdm2_clk_src.clkr,
	[RBCPR_GFX_CLK_SRC] = &rbcpr_gfx_clk_src.clkr,
	[SDCC1_APPS_CLK_SRC] = &sdcc1_apps_clk_src.clkr,
	[SDCC1_ICE_CORE_CLK_SRC] = &sdcc1_ice_core_clk_src.clkr,
	[SDCC2_APPS_CLK_SRC] = &sdcc2_apps_clk_src.clkr,

	[USB30_MOCK_UTMI_CLK_SRC] = &usb30_mock_utmi_clk_src.clkr,
	[USB3_AUX_CLK_SRC] = &usb3_aux_clk_src.clkr,
	[GCC_APC0_DROOP_DETECTOR_GPLL0_CLK] = &gcc_apc0_droop_detector_gpll0_clk.clkr,
	[GCC_APC1_DROOP_DETECTOR_GPLL0_CLK] = &gcc_apc1_droop_detector_gpll0_clk.clkr,
	/*
	[GCC_BLSP1_QUP1_I2C_APPS_CLK] = &gcc_blsp1_qup1_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP1_SPI_APPS_CLK] = &gcc_blsp1_qup1_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP2_I2C_APPS_CLK] = &gcc_blsp1_qup2_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP2_SPI_APPS_CLK] = &gcc_blsp1_qup2_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP3_I2C_APPS_CLK] = &gcc_blsp1_qup3_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP3_SPI_APPS_CLK] = &gcc_blsp1_qup3_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP4_I2C_APPS_CLK] = &gcc_blsp1_qup4_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP4_SPI_APPS_CLK] = &gcc_blsp1_qup4_spi_apps_clk.clkr,
	[GCC_BLSP1_UART1_APPS_CLK] = &gcc_blsp1_uart1_apps_clk.clkr,
	[GCC_BLSP1_UART2_APPS_CLK] = &gcc_blsp1_uart2_apps_clk.clkr,
	*/
	[GCC_BLSP2_QUP1_I2C_APPS_CLK] = &gcc_blsp2_qup1_i2c_apps_clk.clkr,
	[GCC_BLSP2_QUP1_SPI_APPS_CLK] = &gcc_blsp2_qup1_spi_apps_clk.clkr,
	[GCC_BLSP2_QUP2_I2C_APPS_CLK] = &gcc_blsp2_qup2_i2c_apps_clk.clkr,
	[GCC_BLSP2_QUP2_SPI_APPS_CLK] = &gcc_blsp2_qup2_spi_apps_clk.clkr,
	[GCC_BLSP2_QUP3_I2C_APPS_CLK] = &gcc_blsp2_qup3_i2c_apps_clk.clkr,
	[GCC_BLSP2_QUP3_SPI_APPS_CLK] = &gcc_blsp2_qup3_spi_apps_clk.clkr,
	[GCC_BLSP2_QUP4_I2C_APPS_CLK] = &gcc_blsp2_qup4_i2c_apps_clk.clkr,
	[GCC_BLSP2_QUP4_SPI_APPS_CLK] = &gcc_blsp2_qup4_spi_apps_clk.clkr,
	[GCC_BLSP2_UART1_APPS_CLK] = &gcc_blsp2_uart1_apps_clk.clkr,
	[GCC_BLSP2_UART2_APPS_CLK] = &gcc_blsp2_uart2_apps_clk.clkr,
	[GCC_CAMSS_CCI_AHB_CLK] = &gcc_camss_cci_ahb_clk.clkr,
	[GCC_CAMSS_CCI_CLK] = &gcc_camss_cci_clk.clkr,
	[GCC_CAMSS_CPP_AHB_CLK] = &gcc_camss_cpp_ahb_clk.clkr,
	[GCC_CAMSS_CPP_AXI_CLK] = &gcc_camss_cpp_axi_clk.clkr,
	[GCC_CAMSS_CPP_CLK] = &gcc_camss_cpp_clk.clkr,
	[GCC_CAMSS_CSI0_AHB_CLK] = &gcc_camss_csi0_ahb_clk.clkr,
	[GCC_CAMSS_CSI0_CLK] = &gcc_camss_csi0_clk.clkr,
	[GCC_CAMSS_CSI0_CSIPHY_3P_CLK] = &gcc_camss_csi0_csiphy_3p_clk.clkr,
	[GCC_CAMSS_CSI0PHY_CLK] = &gcc_camss_csi0phy_clk.clkr,
	[GCC_CAMSS_CSI0PIX_CLK] = &gcc_camss_csi0pix_clk.clkr,
	[GCC_CAMSS_CSI0RDI_CLK] = &gcc_camss_csi0rdi_clk.clkr,
	[GCC_CAMSS_CSI1_AHB_CLK] = &gcc_camss_csi1_ahb_clk.clkr,
	[GCC_CAMSS_CSI1_CLK] = &gcc_camss_csi1_clk.clkr,
	[GCC_CAMSS_CSI1_CSIPHY_3P_CLK] = &gcc_camss_csi1_csiphy_3p_clk.clkr,
	[GCC_CAMSS_CSI1PHY_CLK] = &gcc_camss_csi1phy_clk.clkr,
	[GCC_CAMSS_CSI1PIX_CLK] = &gcc_camss_csi1pix_clk.clkr,
	[GCC_CAMSS_CSI1RDI_CLK] = &gcc_camss_csi1rdi_clk.clkr,
	[GCC_CAMSS_CSI2_AHB_CLK] = &gcc_camss_csi2_ahb_clk.clkr,
	[GCC_CAMSS_CSI2_CLK] = &gcc_camss_csi2_clk.clkr,
	[GCC_CAMSS_CSI2_CSIPHY_3P_CLK] = &gcc_camss_csi2_csiphy_3p_clk.clkr,
	[GCC_CAMSS_CSI2PHY_CLK] = &gcc_camss_csi2phy_clk.clkr,
	[GCC_CAMSS_CSI2PIX_CLK] = &gcc_camss_csi2pix_clk.clkr,
	[GCC_CAMSS_CSI2RDI_CLK] = &gcc_camss_csi2rdi_clk.clkr,
	[GCC_CAMSS_CSI_VFE0_CLK] = &gcc_camss_csi_vfe0_clk.clkr,
	[GCC_CAMSS_CSI_VFE1_CLK] = &gcc_camss_csi_vfe1_clk.clkr,
	[GCC_CAMSS_GP0_CLK] = &gcc_camss_gp0_clk.clkr,
	[GCC_CAMSS_GP1_CLK] = &gcc_camss_gp1_clk.clkr,
	[GCC_CAMSS_ISPIF_AHB_CLK] = &gcc_camss_ispif_ahb_clk.clkr,
	[GCC_CAMSS_JPEG0_CLK] = &gcc_camss_jpeg0_clk.clkr,
	[GCC_CAMSS_JPEG_AHB_CLK] = &gcc_camss_jpeg_ahb_clk.clkr,
	[GCC_CAMSS_JPEG_AXI_CLK] = &gcc_camss_jpeg_axi_clk.clkr,
	[GCC_CAMSS_MCLK0_CLK] = &gcc_camss_mclk0_clk.clkr,
	[GCC_CAMSS_MCLK1_CLK] = &gcc_camss_mclk1_clk.clkr,
	[GCC_CAMSS_MCLK2_CLK] = &gcc_camss_mclk2_clk.clkr,
	[GCC_CAMSS_MCLK3_CLK] = &gcc_camss_mclk3_clk.clkr,
	[GCC_CAMSS_MICRO_AHB_CLK] = &gcc_camss_micro_ahb_clk.clkr,
	[GCC_CAMSS_CSI0PHYTIMER_CLK] = &gcc_camss_csi0phytimer_clk.clkr,
	[GCC_CAMSS_CSI1PHYTIMER_CLK] = &gcc_camss_csi1phytimer_clk.clkr,
	[GCC_CAMSS_CSI2PHYTIMER_CLK] = &gcc_camss_csi2phytimer_clk.clkr,
	[GCC_CAMSS_AHB_CLK] = &gcc_camss_ahb_clk.clkr,
	[GCC_CAMSS_TOP_AHB_CLK] = &gcc_camss_top_ahb_clk.clkr,
	[GCC_CAMSS_VFE0_CLK] = &gcc_camss_vfe0_clk.clkr,
	[GCC_CAMSS_VFE_AHB_CLK] = &gcc_camss_vfe_ahb_clk.clkr,
	[GCC_CAMSS_VFE_AXI_CLK] = &gcc_camss_vfe_axi_clk.clkr,
	[GCC_CAMSS_VFE1_AHB_CLK] = &gcc_camss_vfe1_ahb_clk.clkr,
	[GCC_CAMSS_VFE1_AXI_CLK] = &gcc_camss_vfe1_axi_clk.clkr,
	[GCC_CAMSS_VFE1_CLK] = &gcc_camss_vfe1_clk.clkr,
	[GCC_DCC_CLK] = &gcc_dcc_clk.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_MSS_CFG_AHB_CLK] = &gcc_mss_cfg_ahb_clk.clkr,
	[GCC_MSS_Q6_BIMC_AXI_CLK] = &gcc_mss_q6_bimc_axi_clk.clkr,
	[GCC_PCNOC_USB3_AXI_CLK] = &gcc_pcnoc_usb3_axi_clk.clkr,
	[GCC_PDM2_CLK] = &gcc_pdm2_clk.clkr,
	[GCC_PDM_AHB_CLK] = &gcc_pdm_ahb_clk.clkr,
	[GCC_RBCPR_GFX_CLK] = &gcc_rbcpr_gfx_clk.clkr,
	[GCC_SDCC1_AHB_CLK] = &gcc_sdcc1_ahb_clk.clkr,
	[GCC_SDCC1_APPS_CLK] = &gcc_sdcc1_apps_clk.clkr,
	[GCC_SDCC1_ICE_CORE_CLK] = &gcc_sdcc1_ice_core_clk.clkr,
	[GCC_SDCC2_AHB_CLK] = &gcc_sdcc2_ahb_clk.clkr,
	[GCC_SDCC2_APPS_CLK] = &gcc_sdcc2_apps_clk.clkr,
	[GCC_USB30_MASTER_CLK] = &gcc_usb30_master_clk.clkr,
	[GCC_USB30_MOCK_UTMI_CLK] = &gcc_usb30_mock_utmi_clk.clkr,
	[GCC_USB30_SLEEP_CLK] = &gcc_usb30_sleep_clk.clkr,
	[GCC_USB3_AUX_CLK] = &gcc_usb3_aux_clk.clkr,
	[GCC_USB_PHY_CFG_AHB_CLK] = &gcc_usb_phy_cfg_ahb_clk.clkr,
	[GCC_VENUS0_AHB_CLK] = &gcc_venus0_ahb_clk.clkr,
	[GCC_VENUS0_AXI_CLK] = &gcc_venus0_axi_clk.clkr,
	[GCC_VENUS0_CORE0_VCODEC0_CLK] = &gcc_venus0_core0_vcodec0_clk.clkr,
	[GCC_VENUS0_VCODEC0_CLK] = &gcc_venus0_vcodec0_clk.clkr,
	[GCC_QUSB_REF_CLK] = &gcc_qusb_ref_clk.clkr,
	[GCC_USB_SS_REF_CLK] = &gcc_usb_ss_ref_clk.clkr,
	[GCC_USB3_PIPE_CLK] = &gcc_usb3_pipe_clk.clkr,
	[MDP_CLK_SRC] = &mdp_clk_src.clkr,
	[ESC0_CLK_SRC] = &esc0_clk_src.clkr,
	[ESC1_CLK_SRC] = &esc1_clk_src.clkr,
	[VSYNC_CLK_SRC] = &vsync_clk_src.clkr,
	[GCC_MDSS_AHB_CLK] = &gcc_mdss_ahb_clk.clkr,
	[GCC_MDSS_AXI_CLK] = &gcc_mdss_axi_clk.clkr,
	[GCC_MDSS_ESC0_CLK] = &gcc_mdss_esc0_clk.clkr,
	[GCC_MDSS_ESC1_CLK] = &gcc_mdss_esc1_clk.clkr,
	[GCC_MDSS_MDP_CLK] = &gcc_mdss_mdp_clk.clkr,
	[GCC_MDSS_VSYNC_CLK] = &gcc_mdss_vsync_clk.clkr,
};

static const struct qcom_reset_map gcc_msm8953_resets[] = {
	[GCC_QUSB2_PHY_BCR]	= { 0x4103C },
	[GCC_USB3_PHY_BCR]	= { 0x3F034 },
	[GCC_USB3PHY_PHY_BCR]	= { 0x3F03C },
	[GCC_USB_30_BCR]	= { 0x3F070 },
	[GCC_CAMSS_MICRO_BCR]	= { 0x56008 },
};

static const struct regmap_config gcc_msm8953_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x7fffc,
	.fast_io	= true,
};

static struct gdsc *gcc_msm8953_gdscs[] = {
	[USB30_GDSC] = &usb30_gdsc
};

static const struct qcom_cc_desc gcc_msm8953_desc = {
	.config = &gcc_msm8953_regmap_config,
	.clks = gcc_msm8953_clocks,
	.num_clks = ARRAY_SIZE(gcc_msm8953_clocks),
	.resets = gcc_msm8953_resets,
	.num_resets = ARRAY_SIZE(gcc_msm8953_resets),
	.gdscs = gcc_msm8953_gdscs,
	.num_gdscs = ARRAY_SIZE(gcc_msm8953_gdscs),
	.clk_hws = gcc_msm8953_hws,
	.num_clk_hws = ARRAY_SIZE(gcc_msm8953_hws),
};

static int gcc_msm8953_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &gcc_msm8953_desc);
}

static const struct of_device_id gcc_msm8953_match_table[] = {
	{ .compatible = "qcom,gcc-msm8953" },
	{},
};

static struct platform_driver gcc_msm8953_driver = {
	.probe = gcc_msm8953_probe,
	.driver = {
		.name = "gcc-msm8953",
		.of_match_table = gcc_msm8953_match_table,
		.owner = THIS_MODULE,
	},
};

static int __init msm_gcc_init(void)
{
	return platform_driver_register(&gcc_msm8953_driver);
}
arch_initcall(msm_gcc_init);
