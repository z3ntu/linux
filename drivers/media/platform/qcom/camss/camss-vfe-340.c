// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm MSM Camera Subsystem - VFE (Video Front End) Module 340 (TFE)
 *
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/bitfield.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>

#include "camss.h"
#include "camss-vfe.h"

// downstream:
//	cam_tfe0: qcom,tfe0@5c6e000 {
//	then 0x1014 for 0x14 here
//
// upstream:
// "vfe0" = <0x0 0x5c6f000 0x0 0x4000>,

/////////////////////////////
// milos:
// tfe0 = <0xac62000 0xD000>; // this matches tfe_0 in FLAT
// then the registers become 0xac63800 for hw_version
//
// and upstream "vfe0" <0x0 0x0ac63800 0x0 0xb800>; probably? need to confirm with FLAT // this would be weird, start addr is tfe_0.top part but that's a small subpart
//
//nvim -d drivers/cam_isp/isp_hw_mgr/isp_hw/tfe_hw/cam_tfe665.h drivers/cam_isp/isp_hw_mgr/isp_hw/tfe_hw/cam_tfe530.h
//
// CAMERA_SS BASE 0x0AC00000  SIZE=0x00200000
// tfe_0 MODULE OFFSET=CAMERA_SS+0x00062000 MAX=CAMERA_SS+0x0006EFFF
// top MODULE OFFSET=CAMERA_SS+0x00063800 MAX=CAMERA_SS+0x000639FF
/////////////////////////////

//#define BUS_REG_BASE 0x1800
#define BUS_REG_BASE 0x0

// 	.global_reset_cmd                       = 0x00001014,
#define TFE_GLOBAL_RESET_CMD				(BUS_REG_BASE + 0x014)
#define		TFE_GLOBAL_RESET_CMD_CORE	BIT(0)

//.reg_update_cmd                         = 0x0000102C,
#define TFE_REG_UPDATE_CMD				(BUS_REG_BASE + 0x02c)

// 	.top_irq_cmd                       = 0x00001030,
#define TFE_IRQ_CMD					(BUS_REG_BASE + 0x030)
#define		TFE_IRQ_CMD_CLEAR		BIT(0)
// 	.top_irq_mask = {
//		0x00001034,
//		0x00001038,
//		0x0000103C,
#define TFE_IRQ_MASK_0					(BUS_REG_BASE + 0x034)
//	.reset_irq_mask = {
//		0x00000001,
#define		TFE_IRQ_MASK_0_RST_DONE		BIT(0)
//	.bus_reg_irq_mask = {
//		0x00000002,
#define		TFE_IRQ_MASK_0_BUS_WR		BIT(1)
#define TFE_IRQ_MASK_1					(BUS_REG_BASE + 0x038)
#define TFE_IRQ_MASK_2					(BUS_REG_BASE + 0x03c)

//	.top_irq_clear = {
//		0x00001040,
//		0x00001044,
//		0x00001048,
#define TFE_IRQ_CLEAR_0					(BUS_REG_BASE + 0x040)

//	.top_irq_status = {
//		0x0000104C,
//		0x00001050,
//		0x00001054,
//	},
#define TFE_IRQ_STATUS_0				(BUS_REG_BASE + 0x04c)

#define BUS_REG(a)					(BUS_REG_BASE + 0x1800 + (a))

//	.bus_irq_mask = {
//		0x00001A18,
//		0x00001A1C,
//	},
#define TFE_BUS_IRQ_MASK_0				BUS_REG(0x18)
//	.comp_rup_done_mask = 0xF,
#define		TFE_BUS_IRQ_MASK_RUP_DONE_MASK	GENMASK(3, 0)
#define		TFE_BUS_IRQ_MASK_RUP_DONE(sc)	FIELD_PREP(TFE_BUS_IRQ_MASK_RUP_DONE_MASK, BIT(sc))
//	.comp_buf_done_mask = 0xFF00,
#define		TFE_BUS_IRQ_MASK_BUF_DONE_MASK	GENMASK(19, 8)
#define		TFE_BUS_IRQ_MASK_BUF_DONE(sg)	FIELD_PREP(TFE_BUS_IRQ_MASK_BUF_DONE_MASK, BIT(sg))
//		.cons_violation_shift = 28,
#define		TFE_BUS_IRQ_MASK_0_CONS_VIOL	BIT(28)
//		.violation_shift  = 30,
#define		TFE_BUS_IRQ_MASK_0_VIOL		BIT(30)
//		.image_size_violation = 31,
#define		TFE_BUS_IRQ_MASK_0_IMG_VIOL	BIT(31)

#define TFE_BUS_IRQ_MASK_1				BUS_REG(0x1c)
//	.bus_irq_clear = {
//		0x00001A20,
//		0x00001A24,
//	},
#define TFE_BUS_IRQ_CLEAR_0				BUS_REG(0x20)
//	.bus_irq_status = {
//		0x00001A28,
//		0x00001A2C,
//	},
#define TFE_BUS_IRQ_STATUS_0				BUS_REG(0x28)
//	.bus_irq_cmd = 0x00001A30,
#define TFE_BUS_IRQ_CMD					BUS_REG(0x30)
#define		TFE_BUS_IRQ_CMD_CLEAR		BIT(0)

//	.bus_overflow_clear_cmd = 0x1A60,
#define TFE_BUS_STATUS_CLEAR				BUS_REG(0x60)
//	.bus_violation_reg = 0x00001A64,
#define TFE_BUS_VIOLATION_STATUS			BUS_REG(0x64)
//	.bus_overflow_reg = 0x00001A68,
#define TFE_BUS_OVERFLOW_STATUS				BUS_REG(0x68)
//	.bus_image_size_vilation_reg = 0x1A70,
#define TFE_BUS_IMAGE_SZ_VIOLATION_STATUS		BUS_REG(0x70)

//			.cfg                   = 0x00001C00,
//			(etc)
#define TFE_BUS_CLIENT_CFG(c)				BUS_REG(0x200 + (c) * 0x100)
//		rsrc_data->en_cfg = (0x1 << rsrc_data->common_data->mode_cfg_shift) | 0x1;
#define		TFE_BUS_CLIENT_CFG_EN		BIT(0)
//	.mode_cfg_shift = 16,
#define		TFE_BUS_CLIENT_CFG_MODE_FRAME	BIT(16)
//			.image_addr            = 0x00001C04,
//			.image_addr            = 0x00001D04,
//			.image_addr            = 0x00001E04,
//			.image_addr            = 0x00001F04,
//			.image_addr            = 0x00002004,
//			.image_addr            = 0x00002104,
//			.image_addr            = 0x00002204,
//			.image_addr            = 0x00002304,
//			.image_addr            = 0x00002404,
//			.image_addr            = 0x00002504,
#define TFE_BUS_IMAGE_ADDR(c)				BUS_REG(0x204 + (c) * 0x100)
//			.frame_incr            = 0x00001C08,
#define TFE_BUS_FRAME_INCR(c)				BUS_REG(0x208 + (c) * 0x100)
//			.image_cfg_0           = 0x00001C0C,
#define TFE_BUS_IMAGE_CFG_0(c)				BUS_REG(0x20c + (c) * 0x100)
#define		TFE_BUS_IMAGE_CFG_0_DEFAULT	0xffff
//			.image_cfg_1           = 0x00001C10,
#define TFE_BUS_IMAGE_CFG_1(c)				BUS_REG(0x210 + (c) * 0x100)
//			.image_cfg_2           = 0x00001C14,
#define TFE_BUS_IMAGE_CFG_2(c)				BUS_REG(0x214 + (c) * 0x100)
#define		TFE_BUS_IMAGE_CFG_2_DEFAULT	0xffff
//			.packer_cfg            = 0x00001C18,
#define TFE_BUS_PACKER_CFG(c)				BUS_REG(0x218 + (c) * 0x100)
#define		TFE_BUS_PACKER_CFG_FMT_PLAIN64	0xa
//			.irq_subsample_period  = 0x00001C30,
#define TFE_BUS_IRQ_SUBSAMPLE_CFG_0(c)			BUS_REG(0x230 + (c) * 0x100)
//			.irq_subsample_pattern = 0x00001C34,
#define TFE_BUS_IRQ_SUBSAMPLE_CFG_1(c)			BUS_REG(0x234 + (c) * 0x100)
//			.framedrop_period      = 0x00001C38,
#define TFE_BUS_FRAMEDROP_CFG_0(c)			BUS_REG(0x238 + (c) * 0x100)
//			.framedrop_pattern     = 0x00001C3C,
#define TFE_BUS_FRAMEDROP_CFG_1(c)			BUS_REG(0x23c + (c) * 0x100)

/*
 * TODO: differentiate the port id based on requested type of RDI, BHIST etc
 *
 * TFE write master IDs (clients)
 *
 * BAYER		0
 * IDEAL_RAW		1
 * STATS_TINTLESS_BG	2
 * STATS_BHIST		3
 * STATS_AWB_BG		4
 * STATS_AEC_BG		5
 * STATS_BAF		6
 * RDI0			7
 * RDI1			8
 * RDI2			9
 * PDAF			10
 * DS4			11
 * DS16			12
 * AI_Y			13
 * AI_C			14
 * STATS_RS		15
 * PDAF0_STAT_LCR	16
 * PDAF1_PD_PREPROCESSED	17
 * PDAF2_PD_PARSED	18
 */
#define RDI_WM(n)		(7 + (n))
#define TFE_WM_NUM		19

// This is RUP GROUP
enum tfe_iface {
	TFE_IFACE_PIX,
	TFE_IFACE_RDI0,
	TFE_IFACE_RDI1,
	TFE_IFACE_RDI2,
	TFE_IFACE_NUM // CAM_TFE_BUS_RUP_GRP_MAX ?
};

// This is COMP GROUP
enum tfe_subgroups {
	TFE_SUBGROUP_BAYER, // 0
	TFE_SUBGROUP_IDEAL_RAW, // 1
	TFE_SUBGROUP_HDR, // 2 -- should be called STATS? (STATS BE TINTLESS & STATS BHIST)
	TFE_SUBGROUP_BG, // 3 (STATS AWB BG & STATS AEC BG)
	//	(/* BUS Client 6 Stats BAF */)
	//		.comp_group            = CAM_TFE_BUS_COMP_GRP_4,
	//		.client_name           = "STATS BAF",
	TFE_SUBGROUP_BAF, // 4 (STATS_BAF)
	TFE_SUBGROUP_RDI0, // 5
	TFE_SUBGROUP_RDI1, // 6
	TFE_SUBGROUP_RDI2, // 7
	TFE_SUBGROUP_PDAF, // 8
	TFE_SUBGROUP_AI, // 9
	TFE_SUBGROUP_STATS_RS, // 10
	// TFE_SUBGROUP_STAT_LCR, // 11 TODO: Add once VFE line 4 is available (CAM_TFE_BUS_RUP_GRP_4)
	TFE_SUBGROUP_NUM
	// .num_comp_grp             = 12,
};

static enum tfe_iface tfe_line_iface_map[VFE_LINE_NUM_MAX] = {
	[VFE_LINE_RDI0] = TFE_IFACE_RDI0,
	[VFE_LINE_RDI1] = TFE_IFACE_RDI1,
	[VFE_LINE_RDI2] = TFE_IFACE_RDI2,
	[VFE_LINE_PIX] = TFE_IFACE_PIX,
};

// mapping from .tfe_out_hw_info
static enum vfe_line_id tfe_subgroup_line_map[TFE_SUBGROUP_NUM] = {
	//		.composite_group  = CAM_TFE_BUS_COMP_GRP_0,
	//		.rup_group_id     = CAM_TFE_BUS_RUP_GRP_0,
	[TFE_SUBGROUP_BAYER] = VFE_LINE_PIX,
	//		.composite_group  = CAM_TFE_BUS_COMP_GRP_1,
	//		.rup_group_id     = CAM_TFE_BUS_RUP_GRP_0,
	[TFE_SUBGROUP_IDEAL_RAW] = VFE_LINE_PIX,
	//		.composite_group  = CAM_TFE_BUS_COMP_GRP_2,
	//		.rup_group_id     = CAM_TFE_BUS_RUP_GRP_0,
	[TFE_SUBGROUP_HDR] = VFE_LINE_PIX,
	//		.composite_group  = CAM_TFE_BUS_COMP_GRP_3,
	//		.rup_group_id     = CAM_TFE_BUS_RUP_GRP_0,
	[TFE_SUBGROUP_BG] = VFE_LINE_PIX,
	//		.composite_group  = CAM_TFE_BUS_COMP_GRP_4, -- TFE_SUBGROUP_BAF
	//		.rup_group_id     = CAM_TFE_BUS_RUP_GRP_0, -- TFE_IFACE_PIX
	[TFE_SUBGROUP_BAF] = VFE_LINE_PIX,
	//		.composite_group  = CAM_TFE_BUS_COMP_GRP_5,
	//		.rup_group_id     = CAM_TFE_BUS_RUP_GRP_1,
	[TFE_SUBGROUP_RDI0] = VFE_LINE_RDI0,
	//		.composite_group  = CAM_TFE_BUS_COMP_GRP_6,
	//		.rup_group_id     = CAM_TFE_BUS_RUP_GRP_2,
	[TFE_SUBGROUP_RDI1] = VFE_LINE_RDI1,
	// 8+0
	[TFE_SUBGROUP_PDAF] = VFE_LINE_PIX,
	// 9+0
	[TFE_SUBGROUP_AI] = VFE_LINE_PIX,
	// 10+0
	[TFE_SUBGROUP_STATS_RS] = VFE_LINE_PIX,
	// 11+4
	//[TFE_SUBGROUP_STAT_LCR] = FIXME what's 4 CAM_TFE_BUS_RUP_GRP_4?
	// https://lore.kernel.org/linux-arm-msm/0b212187-168b-4757-b5a5-afe3ff0922e7@oss.qualcomm.com/
};

static inline enum tfe_iface  __line_to_iface(enum vfe_line_id line_id)
{
	if (line_id <= VFE_LINE_NONE || line_id >= VFE_LINE_NUM_MAX) {
		pr_warn("VFE: Invalid line %d\n", line_id);
		return TFE_IFACE_RDI0;
	}

	return tfe_line_iface_map[line_id];
}

static inline enum vfe_line_id __iface_to_line(unsigned int iface)
{
	int i;

	for (i = 0; i < VFE_LINE_NUM_MAX; i++) {
		if (tfe_line_iface_map[i] == iface)
			return i;
	}

	return VFE_LINE_NONE;
}

static inline enum vfe_line_id __subgroup_to_line(enum tfe_subgroups sg)
{
	if (sg >= TFE_SUBGROUP_NUM)
		return VFE_LINE_NONE;

	return tfe_subgroup_line_map[sg];
}

static void vfe_global_reset(struct vfe_device *vfe)
{
	writel(TFE_IRQ_MASK_0_RST_DONE, vfe->base + TFE_IRQ_MASK_0);
	writel(TFE_GLOBAL_RESET_CMD_CORE, vfe->base + TFE_GLOBAL_RESET_CMD);
}

static irqreturn_t vfe_isr(int irq, void *dev)
{
	struct vfe_device *vfe = dev;
	u32 status;
	int i;

	status = readl_relaxed(vfe->base + TFE_IRQ_STATUS_0);
	writel_relaxed(status, vfe->base + TFE_IRQ_CLEAR_0);
	writel_relaxed(TFE_IRQ_CMD_CLEAR, vfe->base + TFE_IRQ_CMD);

	if (status & TFE_IRQ_MASK_0_RST_DONE) {
		dev_dbg(vfe->camss->dev, "VFE%u: Reset done!", vfe->id);
		vfe_isr_reset_ack(vfe);
	}

	if (status & TFE_IRQ_MASK_0_BUS_WR) {
		u32 bus_status = readl_relaxed(vfe->base + TFE_BUS_IRQ_STATUS_0);

		writel_relaxed(bus_status, vfe->base + TFE_BUS_IRQ_CLEAR_0);
		writel_relaxed(TFE_BUS_IRQ_CMD_CLEAR, vfe->base + TFE_BUS_IRQ_CMD);

		for (i = 0; i < TFE_IFACE_NUM; i++) {
			if (bus_status & TFE_BUS_IRQ_MASK_RUP_DONE(i))
				vfe->res->hw_ops->reg_update_clear(vfe, __iface_to_line(i));
		}

		for (i = 0; i < TFE_SUBGROUP_NUM; i++) {
			if (bus_status & TFE_BUS_IRQ_MASK_BUF_DONE(i))
				vfe_buf_done(vfe, __subgroup_to_line(i));
		}

		if (bus_status & TFE_BUS_IRQ_MASK_0_CONS_VIOL)
			dev_err_ratelimited(vfe->camss->dev, "VFE%u: Bad config violation",
					    vfe->id);

		if (bus_status & TFE_BUS_IRQ_MASK_0_VIOL)
			dev_err_ratelimited(vfe->camss->dev, "VFE%u: Input data violation",
					    vfe->id);

		if (bus_status & TFE_BUS_IRQ_MASK_0_IMG_VIOL)
			dev_err_ratelimited(vfe->camss->dev, "VFE%u: Image size violation",
					    vfe->id);
	}

	status = readl_relaxed(vfe->base + TFE_BUS_OVERFLOW_STATUS);
	if (status) {
		writel_relaxed(status, vfe->base + TFE_BUS_STATUS_CLEAR);
		for (i = 0; i < TFE_WM_NUM; i++) {
			if (status & BIT(i))
				dev_err_ratelimited(vfe->camss->dev,
						    "VFE%u: bus overflow for wm %u\n",
						    vfe->id, i);
		}
	}

	return IRQ_HANDLED;
}

static int vfe_halt(struct vfe_device *vfe)
{
	/* rely on vfe_disable_output() to stop the VFE */
	return 0;
}

static void vfe_enable_irq(struct vfe_device *vfe)
{
	writel(TFE_IRQ_MASK_0_RST_DONE | TFE_IRQ_MASK_0_BUS_WR,
	       vfe->base + TFE_IRQ_MASK_0);
	writel(TFE_BUS_IRQ_MASK_RUP_DONE_MASK | TFE_BUS_IRQ_MASK_BUF_DONE_MASK |
	       TFE_BUS_IRQ_MASK_0_CONS_VIOL | TFE_BUS_IRQ_MASK_0_VIOL |
	       TFE_BUS_IRQ_MASK_0_IMG_VIOL, vfe->base + TFE_BUS_IRQ_MASK_0);
}

static void vfe_wm_update(struct vfe_device *vfe, u8 rdi, u32 addr,
			  struct vfe_line *line)
{
	u8 wm = RDI_WM(rdi);

	writel_relaxed(addr, vfe->base + TFE_BUS_IMAGE_ADDR(wm));
}

static void vfe_wm_start(struct vfe_device *vfe, u8 rdi, struct vfe_line *line)
{
	struct v4l2_pix_format_mplane *pix = &line->video_out.active_fmt.fmt.pix_mp;
	u32 stride = pix->plane_fmt[0].bytesperline;
	u8 wm = RDI_WM(rdi);

	/* Configuration for plain RDI frames */
	writel_relaxed(TFE_BUS_IMAGE_CFG_0_DEFAULT, vfe->base + TFE_BUS_IMAGE_CFG_0(wm));
	writel_relaxed(0u, vfe->base + TFE_BUS_IMAGE_CFG_1(wm));
	writel_relaxed(TFE_BUS_IMAGE_CFG_2_DEFAULT, vfe->base + TFE_BUS_IMAGE_CFG_2(wm));
	writel_relaxed(stride * pix->height, vfe->base + TFE_BUS_FRAME_INCR(wm));
	writel_relaxed(TFE_BUS_PACKER_CFG_FMT_PLAIN64, vfe->base + TFE_BUS_PACKER_CFG(wm));

	/* No dropped frames, one irq per frame */
	writel_relaxed(0, vfe->base + TFE_BUS_FRAMEDROP_CFG_0(wm));
	writel_relaxed(1, vfe->base + TFE_BUS_FRAMEDROP_CFG_1(wm));
	writel_relaxed(0, vfe->base + TFE_BUS_IRQ_SUBSAMPLE_CFG_0(wm));
	writel_relaxed(1, vfe->base + TFE_BUS_IRQ_SUBSAMPLE_CFG_1(wm));

	vfe_enable_irq(vfe);

	writel(TFE_BUS_CLIENT_CFG_EN | TFE_BUS_CLIENT_CFG_MODE_FRAME,
	       vfe->base + TFE_BUS_CLIENT_CFG(wm));

	dev_dbg(vfe->camss->dev, "VFE%u: Started RDI%u width %u height %u stride %u\n",
		vfe->id, rdi, pix->width, pix->height, stride);
}

static void vfe_wm_stop(struct vfe_device *vfe, u8 rdi)
{
	u8 wm = RDI_WM(rdi);

	writel(0, vfe->base + TFE_BUS_CLIENT_CFG(wm));

	dev_dbg(vfe->camss->dev, "VFE%u: Stopped RDI%u\n", vfe->id, rdi);
}

static const struct camss_video_ops vfe_video_ops_520 = {
	.queue_buffer = vfe_queue_buffer_v2,
	.flush_buffers = vfe_flush_buffers,
};

static void vfe_subdev_init(struct device *dev, struct vfe_device *vfe)
{
	vfe->video_ops = vfe_video_ops_520;
}

static void vfe_reg_update(struct vfe_device *vfe, enum vfe_line_id line_id)
{
	vfe->reg_update |= BIT(__line_to_iface(line_id));
	writel_relaxed(vfe->reg_update, vfe->base + TFE_REG_UPDATE_CMD);
}

static void vfe_reg_update_clear(struct vfe_device *vfe, enum vfe_line_id line_id)
{
	vfe->reg_update &= ~BIT(__line_to_iface(line_id));
}

const struct vfe_hw_ops vfe_ops_340 = {
	.global_reset = vfe_global_reset,
	.hw_version = vfe_hw_version,
	.isr = vfe_isr,
	.pm_domain_off = vfe_pm_domain_off,
	.pm_domain_on = vfe_pm_domain_on,
	.subdev_init = vfe_subdev_init,
	.vfe_disable = vfe_disable,
	.vfe_enable = vfe_enable_v2,
	.vfe_halt = vfe_halt,
	.vfe_wm_start = vfe_wm_start,
	.vfe_wm_stop = vfe_wm_stop,
	.vfe_buf_done = vfe_buf_done,
	.vfe_wm_update = vfe_wm_update,
	.reg_update = vfe_reg_update,
	.reg_update_clear = vfe_reg_update_clear,
};
