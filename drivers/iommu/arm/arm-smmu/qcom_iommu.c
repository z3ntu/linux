// SPDX-License-Identifier: GPL-2.0-only
/*
 * IOMMU API for QCOM secure IOMMUs.  Somewhat based on arm-smmu.c
 *
 * Copyright (C) 2013 ARM Limited
 * Copyright (C) 2017 Red Hat
 */

#include <linux/atomic.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-iommu.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-hi-lo.h>
#include <linux/io-pgtable.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/kconfig.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_iommu.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/qcom_scm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "arm-smmu.h"

#define SMMU_INTR_SEL_NS     0x2000

enum qcom_iommu_clk {
	CLK_IFACE,
	CLK_BUS,
	CLK_TBU,
	CLK_ALT,
	CLK_NUM,
};

struct qcom_iommu_ctx;

struct qcom_iommu_dev {
	/* IOMMU core code handle */
	struct iommu_device	 iommu;
	struct device		*dev;
	struct clk_bulk_data clks[CLK_NUM];
	void __iomem		*local_base;
	u32			 sec_id;
	u8			 num_ctxs;
	struct qcom_iommu_ctx	*ctxs[];   /* indexed by asid-1 */
};

struct qcom_iommu_ctx {
	struct device		*dev;
	void __iomem		*base;
	bool			 secure_init;
	u8			 asid;      /* asid and ctx bank # are 1:1 */
	struct iommu_domain	*domain;
};

struct qcom_iommu_domain {
	struct io_pgtable_ops	*pgtbl_ops;
	spinlock_t		 pgtbl_lock;
	struct mutex		 init_mutex; /* Protects iommu pointer */
	struct iommu_domain	 domain;
	struct qcom_iommu_dev	*iommu;
	struct iommu_fwspec	*fwspec;
};

static struct qcom_iommu_domain *to_qcom_iommu_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct qcom_iommu_domain, domain);
}

static const struct iommu_ops qcom_iommu_ops;

static struct qcom_iommu_dev * to_iommu(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	if (!fwspec || fwspec->ops != &qcom_iommu_ops)
		return NULL;

	return dev_iommu_priv_get(dev);
}

static struct qcom_iommu_ctx * to_ctx(struct qcom_iommu_domain *d, unsigned asid)
{
	struct qcom_iommu_dev *qcom_iommu = d->iommu;
	if (!qcom_iommu)
		return NULL;
	return qcom_iommu->ctxs[asid - 1];
}

static inline void
iommu_writel(struct qcom_iommu_ctx *ctx, unsigned reg, u32 val)
{
	writel_relaxed(val, ctx->base + reg);
}

static inline void
iommu_writeq(struct qcom_iommu_ctx *ctx, unsigned reg, u64 val)
{
	writeq_relaxed(val, ctx->base + reg);
}

static inline u32
iommu_readl(struct qcom_iommu_ctx *ctx, unsigned reg)
{
	return readl_relaxed(ctx->base + reg);
}

static inline u64
iommu_readq(struct qcom_iommu_ctx *ctx, unsigned reg)
{
	return readq_relaxed(ctx->base + reg);
}

static void qcom_iommu_tlb_sync(void *cookie)
{
	struct qcom_iommu_domain *qcom_domain = cookie;
	struct iommu_fwspec *fwspec = qcom_domain->fwspec;
	unsigned i;

	for (i = 0; i < fwspec->num_ids; i++) {
		struct qcom_iommu_ctx *ctx = to_ctx(qcom_domain, fwspec->ids[i]);
		unsigned int val, ret;

		iommu_writel(ctx, ARM_SMMU_CB_TLBSYNC, 0);

		ret = readl_poll_timeout(ctx->base + ARM_SMMU_CB_TLBSTATUS, val,
					 (val & 0x1) == 0, 0, 5000000);
		if (ret)
			dev_err(ctx->dev, "timeout waiting for TLB SYNC\n");
	}
}

static void qcom_iommu_tlb_inv_context(void *cookie)
{
	struct qcom_iommu_domain *qcom_domain = cookie;
	struct iommu_fwspec *fwspec = qcom_domain->fwspec;
	unsigned i;

	for (i = 0; i < fwspec->num_ids; i++) {
		struct qcom_iommu_ctx *ctx = to_ctx(qcom_domain, fwspec->ids[i]);
		iommu_writel(ctx, ARM_SMMU_CB_S1_TLBIASID, ctx->asid);
	}

	qcom_iommu_tlb_sync(cookie);
}

static void qcom_iommu_tlb_inv_range_nosync(unsigned long iova, size_t size,
					    size_t granule, bool leaf, void *cookie)
{
	struct qcom_iommu_domain *qcom_domain = cookie;
	struct iommu_fwspec *fwspec = qcom_domain->fwspec;
	unsigned i, reg;

	reg = leaf ? ARM_SMMU_CB_S1_TLBIVAL : ARM_SMMU_CB_S1_TLBIVA;

	for (i = 0; i < fwspec->num_ids; i++) {
		struct qcom_iommu_ctx *ctx = to_ctx(qcom_domain, fwspec->ids[i]);
		size_t s = size;

		iova = (iova >> 12) << 12;
		iova |= ctx->asid;
		do {
			iommu_writel(ctx, reg, iova);
			iova += granule;
		} while (s -= granule);
	}
}

static void qcom_iommu_tlb_flush_walk(unsigned long iova, size_t size,
				      size_t granule, void *cookie)
{
	qcom_iommu_tlb_inv_range_nosync(iova, size, granule, false, cookie);
	qcom_iommu_tlb_sync(cookie);
}

static void qcom_iommu_tlb_flush_leaf(unsigned long iova, size_t size,
				      size_t granule, void *cookie)
{
	qcom_iommu_tlb_inv_range_nosync(iova, size, granule, true, cookie);
	qcom_iommu_tlb_sync(cookie);
}

static void qcom_iommu_tlb_add_page(struct iommu_iotlb_gather *gather,
				    unsigned long iova, size_t granule,
				    void *cookie)
{
	qcom_iommu_tlb_inv_range_nosync(iova, granule, granule, true, cookie);
}

static const struct iommu_flush_ops qcom_flush_ops = {
	.tlb_flush_all	= qcom_iommu_tlb_inv_context,
	.tlb_flush_walk = qcom_iommu_tlb_flush_walk,
	.tlb_flush_leaf = qcom_iommu_tlb_flush_leaf,
	.tlb_add_page	= qcom_iommu_tlb_add_page,
};

static irqreturn_t qcom_iommu_fault(int irq, void *dev)
{
	struct qcom_iommu_ctx *ctx = dev;
	u32 fsr, fsynr;
	u64 iova;

	fsr = iommu_readl(ctx, ARM_SMMU_CB_FSR);

	if (!(fsr & ARM_SMMU_FSR_FAULT))
		return IRQ_NONE;

	fsynr = iommu_readl(ctx, ARM_SMMU_CB_FSYNR0);
	iova = iommu_readq(ctx, ARM_SMMU_CB_FAR);

	if (!report_iommu_fault(ctx->domain, ctx->dev, iova, 0)) {
		dev_err_ratelimited(ctx->dev,
				    "Unhandled context fault: fsr=0x%x, "
				    "iova=0x%016llx, fsynr=0x%x, cb=%d\n",
				    fsr, iova, fsynr, ctx->asid);
	}

	iommu_writel(ctx, ARM_SMMU_CB_FSR, fsr);
	iommu_writel(ctx, ARM_SMMU_CB_RESUME, ARM_SMMU_RESUME_TERMINATE);

	return IRQ_HANDLED;
}

static irqreturn_t qcom_iommu_fault2(int irq, void *dev)
{
	struct qcom_iommu_dev *qcom_iommu = dev;
	u32 val;

	val = readl(qcom_iommu->local_base + ARM_SMMU_GR0_sGFSR);

	printk(KERN_ERR "%s() %X\n", __func__, val);

	return IRQ_HANDLED;
}


static void qcom_iommu_halt(struct qcom_iommu_dev *qcom_iommu)
{
	u32 val;
	int ret;

	val = readl(qcom_iommu->local_base + 0x2000);
	val |= BIT(2);
	writel(val, qcom_iommu->local_base + 0x2000);

	ret = readl_poll_timeout(qcom_iommu->local_base + 0x2000, val, val & BIT(3), 1, 5000000);
	if (ret)
		dev_err(qcom_iommu->dev, "failed to halt bus\n");
}

static void qcom_iommu_unhalt(struct qcom_iommu_dev *qcom_iommu)
{
	u32 val;

	val = readl(qcom_iommu->local_base + 0x2000);
	val &= ~BIT(2);
	writel(val, qcom_iommu->local_base + 0x2000);
}

static int qcom_iommu_non_secure_init(struct qcom_iommu_dev *qcom_iommu);

static int qcom_iommu_init_domain(struct iommu_domain *domain,
				  struct qcom_iommu_dev *qcom_iommu,
				  struct device *dev)
{
	struct qcom_iommu_domain *qcom_domain = to_qcom_iommu_domain(domain);
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	unsigned long oas;
	enum io_pgtable_fmt fmt;
	struct io_pgtable_ops *pgtbl_ops;
	struct io_pgtable_cfg pgtbl_cfg;
	int i, ret = 0;
	u32 reg;

	dev_err(qcom_iommu->dev, "%s()\n", __func__);

	mutex_lock(&qcom_domain->init_mutex);
	if (qcom_domain->iommu)
		goto out_unlock;

	if (IS_ENABLED(CONFIG_IOMMU_IO_PGTABLE_ARMV7S) &&
	    !IS_ENABLED(CONFIG_64BIT)) {
		fmt = ARM_V7S;
		oas = 32;
	} else {
		fmt = ARM_32_LPAE_S1;
		oas = 40;
	}

	pgtbl_cfg = (struct io_pgtable_cfg) {
		.pgsize_bitmap	= qcom_iommu_ops.pgsize_bitmap,
		.ias		= 32,
		.oas		= oas,
		.tlb		= &qcom_flush_ops,
		.iommu_dev	= qcom_iommu->dev,
	};

	qcom_domain->iommu = qcom_iommu;
	qcom_domain->fwspec = fwspec;

	pgtbl_ops = alloc_io_pgtable_ops(fmt, &pgtbl_cfg, qcom_domain);
	if (!pgtbl_ops) {
		dev_err(qcom_iommu->dev, "failed to allocate pagetable ops\n");
		ret = -ENOMEM;
		goto out_clear_iommu;
	}

	/* Update the domain's page sizes to reflect the page table format */
	domain->pgsize_bitmap = pgtbl_cfg.pgsize_bitmap;
	domain->geometry.aperture_end = (1ULL << pgtbl_cfg.ias) - 1;
	domain->geometry.force_aperture = true;

	if (!qcom_iommu->sec_id) {
		qcom_iommu_halt(qcom_iommu);
		qcom_iommu_non_secure_init(qcom_iommu);
		qcom_iommu_unhalt(qcom_iommu);
	}

//	qcom_iommu_halt(qcom_iommu);

	for (i = 0; i < fwspec->num_ids; i++) {
		struct qcom_iommu_ctx *ctx = to_ctx(qcom_domain, fwspec->ids[i]);

		if (!ctx->secure_init) {
			ctx->secure_init = true;

			dev_err(ctx->dev, "%s() restore_sec(%d)\n", __func__, qcom_iommu->sec_id);

			if (qcom_iommu->sec_id) {
				ret = qcom_scm_restore_sec_cfg(qcom_iommu->sec_id, i + 1);
				if (ret) {
					dev_err(qcom_iommu->dev, "secure init failed: %d\n", ret);
					return -ENODEV;
				}
			}

#if 0
			printk(KERN_ERR "%s(%s)\n", __func__, ctx->dev->of_node->name);
			if (!strcmp(ctx->dev->of_node->name, "kgsl-ctx")) {
				printk(KERN_ERR "%s() setting bfb\n", __func__);
				writel(3, qcom_iommu->local_base + 0x204c);
				printk(KERN_ERR "*\n");
				writel(0, qcom_iommu->local_base + 0x2050);
				printk(KERN_ERR "*\n");
				//writel(4, qcom_iommu->local_base + 0x2514);
				printk(KERN_ERR "*\n");
				writel(0x10, qcom_iommu->local_base + 0x2540);
				printk(KERN_ERR "*\n");
				writel(0, qcom_iommu->local_base + 0x256c);
				printk(KERN_ERR "*\n");
				//writel(0, qcom_iommu->local_base + 0x20ac);
				//printk(KERN_ERR "*\n");
				//writel(0, qcom_iommu->local_base + 0x215c);
				//printk(KERN_ERR "*\n");
				//writel(0x20, qcom_iommu->local_base + 0x220c);
				//printk(KERN_ERR "*\n");
				//writel(0, qcom_iommu->local_base + 0x2314);
				//printk(KERN_ERR "*\n");
				//writel(1, qcom_iommu->local_base + 0x2394);
				//printk(KERN_ERR "*\n");
				//writel(0x81, qcom_iommu->local_base + 0x2414);
				//printk(KERN_ERR "*\n");
				//writel(0, qcom_iommu->local_base + 0x2008);
				//printk(KERN_ERR "*\n");
			}

			printk(KERN_ERR "%s(%s)\n", __func__, ctx->dev->of_node->name);
			if (!strcmp(ctx->dev->of_node->name, "kgsl-ctx")) {
				printk(KERN_ERR "%s() setting bfb\n", __func__);
				writel(3, qcom_iommu->local_base + 0x204c);
				printk(KERN_ERR "*\n");
				writel(0, qcom_iommu->local_base + 0x2050);
				printk(KERN_ERR "*\n");
				writel(4, qcom_iommu->local_base + 0x2514);
				printk(KERN_ERR "*\n");
				writel(0x10, qcom_iommu->local_base + 0x2540);
				printk(KERN_ERR "*\n");
				writel(0, qcom_iommu->local_base + 0x256c);
				printk(KERN_ERR "*\n");
				writel(0, qcom_iommu->local_base + 0x20ac);
				printk(KERN_ERR "*\n");
				writel(1, qcom_iommu->local_base + 0x215c);
				printk(KERN_ERR "*\n");
				writel(0x21, qcom_iommu->local_base + 0x220c);
				printk(KERN_ERR "*\n");
				writel(0, qcom_iommu->local_base + 0x2314);
				printk(KERN_ERR "*\n");
				writel(1, qcom_iommu->local_base + 0x2394);
				printk(KERN_ERR "*\n");
				writel(0x81, qcom_iommu->local_base + 0x2414);
				printk(KERN_ERR "*\n");
				writel(0, qcom_iommu->local_base + 0x2008);
				printk(KERN_ERR "*\n");
			}
#endif
		}

		printk(KERN_ERR "%s() reset\n", __func__);

/*
		printk("ARM_SMMU_CB_ACTLR %X\n", iommu_readl(ctx, ARM_SMMU_CB_ACTLR));
		printk("ARM_SMMU_CB_TTBCR2 %X\n", iommu_readl(ctx, ARM_SMMU_CB_TTBCR2));
		printk("ARM_SMMU_CB_TTBCR %X\n", iommu_readl(ctx, ARM_SMMU_CB_TTBCR));
		//printk("ARM_SMMU_CB_ACTLR %X\n", iommu_readl(ctx, ARM_SMMU_CB_ACTLR));
		printk("ARM_SMMU_CB_CONTEXTIDR %X\n", iommu_readl(ctx, ARM_SMMU_CB_CONTEXTIDR));

		printk("ARM_SMMU_GR1_CBAR(0) %X\n", iommu_readl(ctx, ARM_SMMU_GR1_CBAR(0)));
		printk("ARM_SMMU_GR1_CBAR(1) %X\n", iommu_readl(ctx, ARM_SMMU_GR1_CBAR(1)));
		printk("ARM_SMMU_GR1_CBAR(2) %X\n", iommu_readl(ctx, ARM_SMMU_GR1_CBAR(2)));
*/

		/* Reset context */
		iommu_writel(ctx, ARM_SMMU_CB_ACTLR, 0);
		iommu_writel(ctx, ARM_SMMU_CB_FAR, 0);
		iommu_writel(ctx, ARM_SMMU_CB_FSRRESTORE, 0);
		iommu_writel(ctx, ARM_SMMU_CB_S1_MAIR1, 0);
		iommu_writel(ctx, ARM_SMMU_CB_PAR, 0);
		iommu_writel(ctx, ARM_SMMU_CB_S1_MAIR0, 0);
		iommu_writel(ctx, ARM_SMMU_CB_SCTLR, 0);
		iommu_writel(ctx, ARM_SMMU_CB_S1_TLBIALL, 0);

		printk(KERN_ERR "%s() actlr\n", __func__);

		iommu_writel(ctx, ARM_SMMU_CB_ACTLR,
                     ARM_SMMU_CB_ACTLR_BPRCOSH |
                     ARM_SMMU_CB_ACTLR_BPRCISH |
                     ARM_SMMU_CB_ACTLR_BPRCNSH);


		printk(KERN_ERR "%s() ttbr\n", __func__);

#ifndef CONFIG_IOMMU_IO_PGTABLE_ARMV7S
		/* TTBRs */
		iommu_writeq(ctx, ARM_SMMU_CB_TTBR0,
				pgtbl_cfg.arm_lpae_s1_cfg.ttbr |
				FIELD_PREP(ARM_SMMU_TTBRn_ASID, ctx->asid));
		iommu_writeq(ctx, ARM_SMMU_CB_TTBR1, 0);

		/* TCR */
		iommu_writel(ctx, ARM_SMMU_CB_TCR2,
				arm_smmu_lpae_tcr2(&pgtbl_cfg));
		iommu_writel(ctx, ARM_SMMU_CB_TCR,
			     arm_smmu_lpae_tcr(&pgtbl_cfg) | ARM_SMMU_TCR_EAE);

		/* MAIRs (stage-1 only) */
		iommu_writel(ctx, ARM_SMMU_CB_S1_MAIR0,
				pgtbl_cfg.arm_lpae_s1_cfg.mair);
		iommu_writel(ctx, ARM_SMMU_CB_S1_MAIR1,
				pgtbl_cfg.arm_lpae_s1_cfg.mair >> 32);
#else
		/* TTBRs */
		iommu_writel(ctx, ARM_SMMU_CB_TTBR0,
			     pgtbl_cfg.arm_v7s_cfg.ttbr);

		printk(KERN_ERR "%s() ttbcr %X\n", __func__, pgtbl_cfg.arm_v7s_cfg.tcr);

		printk(KERN_ERR "%s() mair\n", __func__);

		iommu_writel(ctx, ARM_SMMU_CB_S1_MAIR0, pgtbl_cfg.arm_v7s_cfg.prrr);
		iommu_writel(ctx, ARM_SMMU_CB_S1_MAIR1, pgtbl_cfg.arm_v7s_cfg.nmrr);
#endif

		if (!qcom_iommu->sec_id) {
			printk(KERN_ERR "%s() cbar\n", __func__);

			/* Stage 1 Context with Stage 2 bypass */
			reg = FIELD_PREP(ARM_SMMU_CBAR_TYPE, CBAR_TYPE_S1_TRANS_S2_BYPASS);

			/* Route page faults to the non-secure interrupt */
			reg |= FIELD_PREP(ARM_SMMU_CBAR_IRPTNDX, 1);

			/* Set VMID to non-secure HLOS */
			reg |= FIELD_PREP(ARM_SMMU_CBAR_VMID, 3);

			/* Bypass is treated as inner-shareable */
			reg |= FIELD_PREP(ARM_SMMU_CBAR_S1_BPSHCFG, 2);

			/* Do not downgrade memory attributes */
			reg |= FIELD_PREP(ARM_SMMU_CBAR_S1_MEMATTR, 0x0a);

			writel(reg, qcom_iommu->local_base + 0x1000 + ARM_SMMU_GR1_CBAR(i));
		}

		printk(KERN_ERR "%s() contextidr\n", __func__);

		iommu_writel(ctx, ARM_SMMU_CB_CONTEXTIDR, ctx->asid);

		/* SCTLR */
		reg = ARM_SMMU_SCTLR_CFIE | ARM_SMMU_SCTLR_TRE |
		      ARM_SMMU_SCTLR_S1_ASIDPNE | ARM_SMMU_SCTLR_M;

#ifndef CONFIG_IOMMU_IO_PGTABLE_ARMV7S
		reg |= ARM_SMMU_SCTLR_CFRE | ARM_SMMU_SCTLR_AFE |
		       ARM_SMMU_SCTLR_CFCFG;
#endif

		if (IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
			reg |= ARM_SMMU_SCTLR_E;

		printk(KERN_ERR "%s() sctlr\n", __func__);

		iommu_writel(ctx, ARM_SMMU_CB_SCTLR, reg);

		ctx->domain = domain;
	}

//	qcom_iommu_unhalt(qcom_iommu);

	mutex_unlock(&qcom_domain->init_mutex);

	/* Publish page table ops for map/unmap */
	qcom_domain->pgtbl_ops = pgtbl_ops;

	printk(KERN_ERR "%s() done\n", __func__);

	return 0;

out_clear_iommu:
	qcom_domain->iommu = NULL;
out_unlock:
	mutex_unlock(&qcom_domain->init_mutex);
	return ret;
}

static struct iommu_domain *qcom_iommu_domain_alloc(unsigned type)
{
	struct qcom_iommu_domain *qcom_domain;

	if (type != IOMMU_DOMAIN_UNMANAGED && type != IOMMU_DOMAIN_DMA)
		return NULL;
	/*
	 * Allocate the domain and initialise some of its data structures.
	 * We can't really do anything meaningful until we've added a
	 * master.
	 */
	qcom_domain = kzalloc(sizeof(*qcom_domain), GFP_KERNEL);
	if (!qcom_domain)
		return NULL;

	if (type == IOMMU_DOMAIN_DMA &&
	    iommu_get_dma_cookie(&qcom_domain->domain)) {
		kfree(qcom_domain);
		return NULL;
	}

	mutex_init(&qcom_domain->init_mutex);
	spin_lock_init(&qcom_domain->pgtbl_lock);

	return &qcom_domain->domain;
}

static void qcom_iommu_domain_free(struct iommu_domain *domain)
{
	struct qcom_iommu_domain *qcom_domain = to_qcom_iommu_domain(domain);

	iommu_put_dma_cookie(domain);

	if (qcom_domain->iommu) {
		/*
		 * NOTE: unmap can be called after client device is powered
		 * off, for example, with GPUs or anything involving dma-buf.
		 * So we cannot rely on the device_link.  Make sure the IOMMU
		 * is on to avoid unclocked accesses in the TLB inv path:
		 */
		pm_runtime_get_sync(qcom_domain->iommu->dev);
		free_io_pgtable_ops(qcom_domain->pgtbl_ops);
		pm_runtime_put_sync(qcom_domain->iommu->dev);
	}

	kfree(qcom_domain);
}

static int qcom_iommu_attach_dev(struct iommu_domain *domain, struct device *dev)
{
	struct qcom_iommu_dev *qcom_iommu = to_iommu(dev);
	struct qcom_iommu_domain *qcom_domain = to_qcom_iommu_domain(domain);
	int ret;

	if (!qcom_iommu) {
		dev_err(dev, "cannot attach to IOMMU, is it on the same bus?\n");
		return -ENXIO;
	}

	/* Ensure that the domain is finalized */
	pm_runtime_get_sync(qcom_iommu->dev);
	ret = qcom_iommu_init_domain(domain, qcom_iommu, dev);
//	pm_runtime_put_sync(qcom_iommu->dev);
	if (ret < 0)
		return ret;

	/*
	 * Sanity check the domain. We don't support domains across
	 * different IOMMUs.
	 */
	if (qcom_domain->iommu != qcom_iommu) {
		dev_err(dev, "cannot attach to IOMMU %s while already "
			"attached to domain on IOMMU %s\n",
			dev_name(qcom_domain->iommu->dev),
			dev_name(qcom_iommu->dev));
		return -EINVAL;
	}

	return 0;
}

static void qcom_iommu_detach_dev(struct iommu_domain *domain, struct device *dev)
{
	struct qcom_iommu_domain *qcom_domain = to_qcom_iommu_domain(domain);
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct qcom_iommu_dev *qcom_iommu = to_iommu(dev);
	unsigned i;

	if (WARN_ON(!qcom_domain->iommu))
		return;

	pm_runtime_get_sync(qcom_iommu->dev);
	for (i = 0; i < fwspec->num_ids; i++) {
		struct qcom_iommu_ctx *ctx = to_ctx(qcom_domain, fwspec->ids[i]);

		/* Disable the context bank: */
		iommu_writel(ctx, ARM_SMMU_CB_SCTLR, 0);

		ctx->domain = NULL;
	}
	pm_runtime_put_sync(qcom_iommu->dev);
}

static int qcom_iommu_map(struct iommu_domain *domain, unsigned long iova,
			  phys_addr_t paddr, size_t size, int prot, gfp_t gfp)
{
	int ret;
	unsigned long flags;
	struct qcom_iommu_domain *qcom_domain = to_qcom_iommu_domain(domain);
	struct io_pgtable_ops *ops = qcom_domain->pgtbl_ops;

	if (!ops)
		return -ENODEV;

	spin_lock_irqsave(&qcom_domain->pgtbl_lock, flags);
	ret = ops->map(ops, iova, paddr, size, prot, GFP_ATOMIC);
	spin_unlock_irqrestore(&qcom_domain->pgtbl_lock, flags);
	return ret;
}

static size_t qcom_iommu_unmap(struct iommu_domain *domain, unsigned long iova,
			       size_t size, struct iommu_iotlb_gather *gather)
{
	size_t ret;
	unsigned long flags;
	struct qcom_iommu_domain *qcom_domain = to_qcom_iommu_domain(domain);
	struct io_pgtable_ops *ops = qcom_domain->pgtbl_ops;

	if (!ops)
		return 0;

	printk(KERN_ERR "%s()\n", __func__);

	/* NOTE: unmap can be called after client device is powered off,
	 * for example, with GPUs or anything involving dma-buf.  So we
	 * cannot rely on the device_link.  Make sure the IOMMU is on to
	 * avoid unclocked accesses in the TLB inv path:
	 */
	pm_runtime_get_sync(qcom_domain->iommu->dev);
	spin_lock_irqsave(&qcom_domain->pgtbl_lock, flags);
	ret = ops->unmap(ops, iova, size, gather);
	spin_unlock_irqrestore(&qcom_domain->pgtbl_lock, flags);
	pm_runtime_put_sync(qcom_domain->iommu->dev);

	return ret;
}

static void qcom_iommu_flush_iotlb_all(struct iommu_domain *domain)
{
	struct qcom_iommu_domain *qcom_domain = to_qcom_iommu_domain(domain);
	struct io_pgtable *pgtable = container_of(qcom_domain->pgtbl_ops,
						  struct io_pgtable, ops);
	if (!qcom_domain->pgtbl_ops)
		return;
	printk(KERN_ERR "%s()\n", __func__);

	pm_runtime_get_sync(qcom_domain->iommu->dev);
	qcom_iommu_tlb_sync(pgtable->cookie);
	pm_runtime_put_sync(qcom_domain->iommu->dev);
}

static void qcom_iommu_iotlb_sync(struct iommu_domain *domain,
				  struct iommu_iotlb_gather *gather)
{
	qcom_iommu_flush_iotlb_all(domain);
}

static phys_addr_t qcom_iommu_iova_to_phys(struct iommu_domain *domain,
					   dma_addr_t iova)
{
	phys_addr_t ret;
	unsigned long flags;
	struct qcom_iommu_domain *qcom_domain = to_qcom_iommu_domain(domain);
	struct io_pgtable_ops *ops = qcom_domain->pgtbl_ops;

	if (!ops)
		return 0;
	printk(KERN_ERR "%s()\n", __func__);

	spin_lock_irqsave(&qcom_domain->pgtbl_lock, flags);
	ret = ops->iova_to_phys(ops, iova);
	spin_unlock_irqrestore(&qcom_domain->pgtbl_lock, flags);

	return ret;
}

static bool qcom_iommu_capable(enum iommu_cap cap)
{
	switch (cap) {
	case IOMMU_CAP_CACHE_COHERENCY:
		/*
		 * Return true here as the SMMU can always send out coherent
		 * requests.
		 */
		return true;
	case IOMMU_CAP_NOEXEC:
		return true;
	default:
		return false;
	}
}

static struct iommu_device *qcom_iommu_probe_device(struct device *dev)
{
	struct qcom_iommu_dev *qcom_iommu = to_iommu(dev);
	struct device_link *link;

	if (!qcom_iommu)
		return ERR_PTR(-ENODEV);

	/*
	 * Establish the link between iommu and master, so that the
	 * iommu gets runtime enabled/disabled as per the master's
	 * needs.
	 */
	link = device_link_add(dev, qcom_iommu->dev, DL_FLAG_PM_RUNTIME);
	if (!link) {
		dev_err(qcom_iommu->dev, "Unable to create device link between %s and %s\n",
			dev_name(qcom_iommu->dev), dev_name(dev));
		return ERR_PTR(-ENODEV);
	}

	return &qcom_iommu->iommu;
}

static void qcom_iommu_release_device(struct device *dev)
{
	struct qcom_iommu_dev *qcom_iommu = to_iommu(dev);

	if (!qcom_iommu)
		return;

	iommu_fwspec_free(dev);
}

static int qcom_iommu_of_xlate(struct device *dev, struct of_phandle_args *args)
{
	struct qcom_iommu_dev *qcom_iommu;
	struct platform_device *iommu_pdev;
	unsigned asid = args->args[0];

	if (args->args_count != 1) {
		dev_err(dev, "incorrect number of iommu params found for %s "
			"(found %d, expected 1)\n",
			args->np->full_name, args->args_count);
		return -EINVAL;
	}

	iommu_pdev = of_find_device_by_node(args->np);
	if (WARN_ON(!iommu_pdev))
		return -EINVAL;

	qcom_iommu = platform_get_drvdata(iommu_pdev);

	/* make sure the asid specified in dt is valid, so we don't have
	 * to sanity check this elsewhere, since 'asid - 1' is used to
	 * index into qcom_iommu->ctxs:
	 */
	if (WARN_ON(asid < 1) ||
	    WARN_ON(asid > qcom_iommu->num_ctxs)) {
		put_device(&iommu_pdev->dev);
		return -EINVAL;
	}

	if (!dev_iommu_priv_get(dev)) {
		dev_iommu_priv_set(dev, qcom_iommu);
	} else {
		/* make sure devices iommus dt node isn't referring to
		 * multiple different iommu devices.  Multiple context
		 * banks are ok, but multiple devices are not:
		 */
		if (WARN_ON(qcom_iommu != dev_iommu_priv_get(dev))) {
			put_device(&iommu_pdev->dev);
			return -EINVAL;
		}
	}

	return iommu_fwspec_add_ids(dev, &asid, 1);
}

static const struct iommu_ops qcom_iommu_ops = {
	.capable	= qcom_iommu_capable,
	.domain_alloc	= qcom_iommu_domain_alloc,
	.domain_free	= qcom_iommu_domain_free,
	.attach_dev	= qcom_iommu_attach_dev,
	.detach_dev	= qcom_iommu_detach_dev,
	.map		= qcom_iommu_map,
	.unmap		= qcom_iommu_unmap,
	.flush_iotlb_all = qcom_iommu_flush_iotlb_all,
	.iotlb_sync	= qcom_iommu_iotlb_sync,
	.iova_to_phys	= qcom_iommu_iova_to_phys,
	.probe_device	= qcom_iommu_probe_device,
	.release_device	= qcom_iommu_release_device,
	.device_group	= generic_device_group,
	.of_xlate	= qcom_iommu_of_xlate,
	.pgsize_bitmap	= SZ_4K | SZ_64K | SZ_1M | SZ_16M,
};

static int qcom_iommu_sec_ptbl_init(struct device *dev)
{
	size_t psize = 0;
	unsigned int spare = 0;
	void *cpu_addr;
	dma_addr_t paddr;
	unsigned long attrs;
	static bool allocated = false;
	int ret;

	if (allocated)
		return 0;

	ret = qcom_scm_iommu_secure_ptbl_size(spare, &psize);
	if (ret) {
		dev_err(dev, "failed to get iommu secure pgtable size (%d)\n",
			ret);
		return ret;
	}

	dev_info(dev, "iommu sec: pgtable size: %zu\n", psize);

	attrs = DMA_ATTR_NO_KERNEL_MAPPING;

	cpu_addr = dma_alloc_attrs(dev, psize, &paddr, GFP_KERNEL, attrs);
	if (!cpu_addr) {
		dev_err(dev, "failed to allocate %zu bytes for pgtable\n",
			psize);
		return -ENOMEM;
	}

	ret = qcom_scm_iommu_secure_ptbl_init(paddr, psize, spare);
	if (ret) {
		dev_err(dev, "failed to init iommu pgtable (%d)\n", ret);
		goto free_mem;
	}

	allocated = true;
	return 0;

free_mem:
	dma_free_attrs(dev, psize, cpu_addr, paddr, attrs);
	return ret;
}

static int get_asid(const struct device_node *np)
{
	u32 reg;

	/* read the "reg" property directly to get the relative address
	 * of the context bank, and calculate the asid from that:
	 */
	if (of_property_read_u32_index(np, "reg", 0, &reg))
		return -ENODEV;

	return reg / 0x1000;      /* context banks are 0x1000 apart */
}

static int qcom_iommu_ctx_probe(struct platform_device *pdev)
{
	struct qcom_iommu_ctx *ctx;
	struct device *dev = &pdev->dev;
	struct qcom_iommu_dev *qcom_iommu = dev_get_drvdata(dev->parent);
	struct resource *res;
	int ret, irq;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = dev;
	platform_set_drvdata(pdev, ctx);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dev_err(dev, "%s() res: %pR\n", __func__, res);
	ctx->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ctx->base))
		return PTR_ERR(ctx->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENODEV;

	/* clear IRQs before registering fault handler, just in case the
	 * boot-loader left us a surprise:
	 */
//	iommu_writel(ctx, ARM_SMMU_CB_FSR, iommu_readl(ctx, ARM_SMMU_CB_FSR));

	ret = devm_request_irq(dev, irq,
			       qcom_iommu_fault,
			       IRQF_SHARED,
			       "qcom-iommu-fault",
			       ctx);
	if (ret) {
		dev_err(dev, "failed to request IRQ %u\n", irq);
		return ret;
	}

	ret = get_asid(dev->of_node);
	if (ret < 0) {
		dev_err(dev, "missing reg property\n");
		return ret;
	}

	ctx->asid = ret;

	dev_dbg(dev, "found asid %u\n", ctx->asid);

	qcom_iommu->ctxs[ctx->asid - 1] = ctx;

	return 0;
}

static int qcom_iommu_ctx_remove(struct platform_device *pdev)
{
	struct qcom_iommu_dev *qcom_iommu = dev_get_drvdata(pdev->dev.parent);
	struct qcom_iommu_ctx *ctx = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	qcom_iommu->ctxs[ctx->asid - 1] = NULL;

	return 0;
}

static const struct of_device_id ctx_of_match[] = {
	{ .compatible = "qcom,msm-iommu-v1-ns" },
	{ .compatible = "qcom,msm-iommu-v1-sec" },
	{ /* sentinel */ }
};

static struct platform_driver qcom_iommu_ctx_driver = {
	.driver	= {
		.name		= "qcom-iommu-ctx",
		.of_match_table	= ctx_of_match,
	},
	.probe	= qcom_iommu_ctx_probe,
	.remove = qcom_iommu_ctx_remove,
};

static bool qcom_iommu_has_secure_context(struct qcom_iommu_dev *qcom_iommu)
{
	struct device_node *child;

	for_each_child_of_node(qcom_iommu->dev->of_node, child)
		if (of_device_is_compatible(child, "qcom,msm-iommu-v1-sec"))
			return true;

	return false;
}

static int qcom_iommu_non_secure_init(struct qcom_iommu_dev *qcom_iommu)
{
	int smrs;
	u32 reg;
	int i;

	printk("ARM_SMMU_GR0_S2CR %X\n", readl(qcom_iommu->local_base + ARM_SMMU_GR0_S2CR(0)));
	printk("ARM_SMMU_GR0_SMR %X\n", readl(qcom_iommu->local_base + ARM_SMMU_GR0_SMR(0)));
	printk("ARM_SMMU_GR0_sCR0 %X\n", readl(qcom_iommu->local_base + ARM_SMMU_GR0_sCR0));
	printk("ARM_SMMU_GR0_ID0 %X\n", readl(qcom_iommu->local_base + ARM_SMMU_GR0_ID0));
	printk("ARM_SMMU_GR0_ID1 %X\n", readl(qcom_iommu->local_base + ARM_SMMU_GR0_ID1));
	printk("ARM_SMMU_GR0_sGFSR %X\n", readl(qcom_iommu->local_base + ARM_SMMU_GR0_sGFSR));

	writel(0, qcom_iommu->local_base + ARM_SMMU_GR0_sACR);
	writel(0, qcom_iommu->local_base + ARM_SMMU_GR0_CR2);
	writel(0, qcom_iommu->local_base + ARM_SMMU_GR0_GFAR);
	writel(0, qcom_iommu->local_base + ARM_SMMU_GR0_GFSRRESTORE);
	writel(0, qcom_iommu->local_base + ARM_SMMU_GR0_TLBIALLNSNH);
	writel_relaxed(0xffffffff, qcom_iommu->local_base + SMMU_INTR_SEL_NS);

	reg = readl(qcom_iommu->local_base + ARM_SMMU_GR0_ID0);
	smrs = reg & ARM_SMMU_ID0_NUMSMRG;
	for (i = 0; i < 3; i++) {
		reg = readl(qcom_iommu->local_base + ARM_SMMU_GR0_SMR(i));
		writel(ARM_SMMU_SMR_VALID | i, qcom_iommu->local_base + ARM_SMMU_GR0_SMR(i));

		writel(0 << 16 | 0x0a << 12 | i, qcom_iommu->local_base + ARM_SMMU_GR0_S2CR(i));
	}
	for (; i < smrs; i++)
		writel(0, qcom_iommu->local_base + ARM_SMMU_GR0_SMR(i));

	/* Enable fault reporting */
	reg = (ARM_SMMU_sCR0_GFRE | ARM_SMMU_sCR0_GFIE |
	       ARM_SMMU_sCR0_GCFGFRE | ARM_SMMU_sCR0_GCFGFIE);

	/* Enable client access, handling unmatched streams as appropriate */
//	reg &= ~ARM_SMMU_sCR0_CLIENTPD;
	reg |= ARM_SMMU_sCR0_USFCFG;

	/* Disable forced broadcasting */
//	reg &= ~ARM_SMMU_sCR0_FB;

	/* Don't upgrade barriers */
//	reg &= ~(ARM_SMMU_sCR0_BSU);

	/* ??? */
	reg |= ARM_SMMU_sCR0_SMCFCFG;

	reg |= ARM_SMMU_sCR0_STALLD;

//	reg = ARM_SMMU_sCR0_SMCFCFG | ARM_SMMU_sCR0_USFCFG | ARM_SMMU_sCR0_STALLD | ARM_SMMU_sCR0_GCFGFIE | ARM_SMMU_sCR0_GCFGFRE | ARM_SMMU_sCR0_GFIE | ARM_SMMU_sCR0_GFRE | 0;

	writel(reg, qcom_iommu->local_base + ARM_SMMU_GR0_sCR0);

	return 0;
}

static int qcom_iommu_device_probe(struct platform_device *pdev)
{
	struct device_node *child;
	struct qcom_iommu_dev *qcom_iommu;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct clk *clk;
	int ret, max_asid = 0;
	int irq;

	/* find the max asid (which is 1:1 to ctx bank idx), so we know how
	 * many child ctx devices we have:
	 */
	for_each_child_of_node(dev->of_node, child)
		max_asid = max(max_asid, get_asid(child));

	qcom_iommu = devm_kzalloc(dev, struct_size(qcom_iommu, ctxs, max_asid),
				  GFP_KERNEL);
	if (!qcom_iommu)
		return -ENOMEM;
	qcom_iommu->num_ctxs = max_asid;
	qcom_iommu->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res) {
		qcom_iommu->local_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(qcom_iommu->local_base))
			return PTR_ERR(qcom_iommu->local_base);
	}

	clk = devm_clk_get(dev, "iface");
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to get iface clock\n");
		return PTR_ERR(clk);
	}
	qcom_iommu->clks[CLK_IFACE].clk = clk;

	clk = devm_clk_get(dev, "bus");
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to get bus clock\n");
		return PTR_ERR(clk);
	}
	qcom_iommu->clks[CLK_BUS].clk = clk;

	clk = devm_clk_get_optional(dev, "tbu");
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to get tbu clock\n");
		return PTR_ERR(clk);
	}
	qcom_iommu->clks[CLK_TBU].clk = clk;

	clk = devm_clk_get_optional(dev, "alt");
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to get alt clock\n");
		return PTR_ERR(clk);
	}
	qcom_iommu->clks[CLK_ALT].clk = clk;

	ret = of_property_read_u32(dev->of_node, "qcom,iommu-secure-id",
	                           &qcom_iommu->sec_id);
	if (ret && ret != -EINVAL) {
		dev_err(dev, "invalid qcom,iommu-secure-id property\n");
		return -ENODEV;
	}

	if (qcom_iommu_has_secure_context(qcom_iommu)) {
		ret = qcom_iommu_sec_ptbl_init(dev);
		if (ret) {
			dev_err(dev, "cannot init secure pg table(%d)\n", ret);
			return ret;
		}
	}

	platform_set_drvdata(pdev, qcom_iommu);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "failed to get irq\n");
		return -ENODEV;
	}

	ret = devm_request_irq(dev, irq,
			       qcom_iommu_fault2,
			       IRQF_SHARED,
			       "qcom-iommu2-fault",
			       qcom_iommu);
	if (ret) {
		dev_err(dev, "failed to request IRQ %u\n", irq);
		return ret;
	}

	pm_runtime_enable(dev);

	/* register context bank devices, which are child nodes: */
	ret = devm_of_platform_populate(dev);
	if (ret) {
		dev_err(dev, "Failed to populate iommu contexts\n");
		return ret;
	}

	ret = iommu_device_sysfs_add(&qcom_iommu->iommu, dev, NULL,
				     dev_name(dev));
	if (ret) {
		dev_err(dev, "Failed to register iommu in sysfs\n");
		return ret;
	}

	iommu_device_set_ops(&qcom_iommu->iommu, &qcom_iommu_ops);
	iommu_device_set_fwnode(&qcom_iommu->iommu, dev->fwnode);

	pm_runtime_get_sync(dev);

	if (qcom_iommu->sec_id) {
		dev_err(qcom_iommu->dev, "%s() restore_sec(%d)\n", __func__,
		        qcom_iommu->sec_id);
		ret = qcom_scm_restore_sec_cfg(qcom_iommu->sec_id, 0);
		if (ret) {
			dev_err(qcom_iommu->dev, "secure init failed: %d\n", ret);
			return -ENODEV;
		}
	} else {
		dev_err(&pdev->dev, "non-secure iommu initialization\n");

		ret = qcom_iommu_non_secure_init(qcom_iommu);
		if (ret) {
			dev_err(qcom_iommu->dev, "non-secure init failed\n");
			return ret;
		}
	}
	pm_runtime_put_sync(dev);

	ret = iommu_device_register(&qcom_iommu->iommu);
	if (ret) {
		dev_err(dev, "Failed to register iommu\n");
		return ret;
	}

	bus_set_iommu(&platform_bus_type, &qcom_iommu_ops);

	return 0;
}

static int qcom_iommu_device_remove(struct platform_device *pdev)
{
	struct qcom_iommu_dev *qcom_iommu = platform_get_drvdata(pdev);

	bus_set_iommu(&platform_bus_type, NULL);

	pm_runtime_force_suspend(&pdev->dev);
	platform_set_drvdata(pdev, NULL);
	iommu_device_sysfs_remove(&qcom_iommu->iommu);
	iommu_device_unregister(&qcom_iommu->iommu);

	return 0;
}

static int __maybe_unused qcom_iommu_resume(struct device *dev)
{
	struct qcom_iommu_dev *qcom_iommu = dev_get_drvdata(dev);

	printk("qcom_iommu_resume\n");

	return clk_bulk_prepare_enable(CLK_NUM, qcom_iommu->clks);
}

static int __maybe_unused qcom_iommu_suspend(struct device *dev)
{
	struct qcom_iommu_dev *qcom_iommu = dev_get_drvdata(dev);

	printk("qcom_iommu_suspend\n");

	clk_bulk_disable_unprepare(CLK_NUM, qcom_iommu->clks);

	return 0;
}

static const struct dev_pm_ops qcom_iommu_pm_ops = {
	SET_RUNTIME_PM_OPS(qcom_iommu_suspend, qcom_iommu_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static const struct of_device_id qcom_iommu_of_match[] = {
	{ .compatible = "qcom,msm-iommu-v1" },
	{ /* sentinel */ }
};

static struct platform_driver qcom_iommu_driver = {
	.driver	= {
		.name		= "qcom-iommu",
		.of_match_table	= qcom_iommu_of_match,
		.pm		= &qcom_iommu_pm_ops,
	},
	.probe	= qcom_iommu_device_probe,
	.remove	= qcom_iommu_device_remove,
};

static int __init qcom_iommu_init(void)
{
	int ret;

	ret = platform_driver_register(&qcom_iommu_ctx_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&qcom_iommu_driver);
	if (ret)
		platform_driver_unregister(&qcom_iommu_ctx_driver);

	return ret;
}
device_initcall(qcom_iommu_init);
