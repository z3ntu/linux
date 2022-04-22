// SPDX-License-Identifier: GPL-2.0

/* Copyright (C) 2021 Linaro Ltd. */

#include <linux/log2.h>

#include "../gsi.h"
#include "../ipa_data.h"
#include "../ipa_endpoint.h"
#include "../ipa_mem.h"

/** enum ipa_resource_type - IPA resource types for an SoC having IPA v4.7 */
enum ipa_resource_type {
	/* Source resource types; first must have value 0 */
	IPA_RESOURCE_TYPE_SRC_PKT_CONTEXTS		= 0,
	IPA_RESOURCE_TYPE_SRC_DESCRIPTOR_LISTS,
	IPA_RESOURCE_TYPE_SRC_DESCRIPTOR_BUFF,
	IPA_RESOURCE_TYPE_SRC_HPS_DMARS,
	IPA_RESOURCE_TYPE_SRC_ACK_ENTRIES,

	/* Destination resource types; first must have value 0 */
	IPA_RESOURCE_TYPE_DST_DATA_SECTORS		= 0,
	IPA_RESOURCE_TYPE_DST_DPS_DMARS,
};

/* Resource groups used for an SoC having IPA v4.7 */
enum ipa_rsrc_group_id {
	// DBG ipa3_rsrc_src_grp_config
	/* Source resource group identifiers */
	IPA_RSRC_GROUP_SRC_UL_DL		= 0,
	IPA_RSRC_GROUP_SRC_COUNT,	/* Last in set; not a source group */

	// DBG ipa3_rsrc_dst_grp_config
	/* Destination resource group identifiers */
	IPA_RSRC_GROUP_DST_UL_DL_DPL		= 0,
	IPA_RSRC_GROUP_DST_COUNT,	/* Last; not a destination group */
};

// DBG ipa3_qmb_outstanding
/* QSB configuration data for an SoC having IPA v4.7 */
static const struct ipa_qsb_data ipa_qsb_data[] = {
	[IPA_QSB_MASTER_DDR] = {
		.max_writes		= 12,
		.max_reads		= 13,
		.max_reads_beats	= 120,
		// 	[IPA_HW_v4_0][IPA_QSB_MAX_READS] = {
		// ipareg_construct_qsb_max_reads_v4_0, ipareg_parse_dummy,
		// 0x00000078, 0, 0, 0, 0},
		// 0x78 = 120

	},
};

// struct ipa_ep_configuration {
// 	bool valid;
// 	int group_num;
// 	bool support_flt;
// 	int sequencer_type;
// 	u8 qmb_master_sel;
// 	struct ipa_gsi_ep_config ipa_gsi_ep_info {
//		int ipa_ep_num;
//		int ipa_gsi_chan_num;
//		int ipa_if_tlv;
//		int ipa_if_aos;
//		int ee;
//		enum gsi_prefetch_mode prefetch_mode;
//		uint8_t prefetch_threshold;
//	};
// };
// FIXME!!!
/* Endpoint configuration data for an SoC having IPA v4.7 */
static const struct ipa_gsi_endpoint_data ipa_gsi_endpoint_data[] = {
	[IPA_ENDPOINT_AP_COMMAND_TX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 5,
		.endpoint_id	= 7,
		.toward_ipa	= true,
		.channel = {
			.tre_count	= 256,
			.event_count	= 256,
			.tlv_count	= 20,
		},
		.endpoint = {
			.config = {
				.resource_group	= IPA_RSRC_GROUP_SRC_UL_DL,
				.dma_mode	= true,
				.dma_endpoint	= IPA_ENDPOINT_AP_LAN_RX,
				.tx = {
					.seq_type = IPA_SEQ_DMA,
				},
			},
		},
	},
	[IPA_ENDPOINT_AP_LAN_RX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 14,
		.endpoint_id	= 9,
		.toward_ipa	= false,
		.channel = {
			.tre_count	= 256,
			.event_count	= 256,
			.tlv_count	= 9,
		},
		.endpoint = {
			.config = {
				.resource_group	= IPA_RSRC_GROUP_DST_UL_DL_DPL,
				.aggregation	= true,
				.status_enable	= true,
				.rx = {
					.buffer_size	= 8192,
					.pad_align	= ilog2(sizeof(u32)),
				},
			},
		},
	},
	[IPA_ENDPOINT_AP_MODEM_TX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 2,
		.endpoint_id	= 2,
		.toward_ipa	= true,
		.channel = {
			.tre_count	= 512,
			.event_count	= 512,
			.tlv_count	= 16,
		},
		.endpoint = {
			.filter_support	= true,
			.config = {
				.resource_group	= IPA_RSRC_GROUP_SRC_UL_DL,
				.qmap		= true,
				.status_enable	= true,
				.tx = {
					.seq_type = IPA_SEQ_2_PASS_SKIP_LAST_UC,
					.status_endpoint =
						IPA_ENDPOINT_MODEM_AP_RX,
				},
			},
		},
	},
	[IPA_ENDPOINT_AP_MODEM_RX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 7,
		.endpoint_id	= 16,
		.toward_ipa	= false,
		.channel = {
			.tre_count	= 256,
			.event_count	= 256,
			.tlv_count	= 9,
		},
		.endpoint = {
			.config = {
				.resource_group	= IPA_RSRC_GROUP_DST_UL_DL_DPL,
				.qmap		= true,
				.aggregation	= true,
				.rx = {
					.buffer_size	= 8192,
					.aggr_close_eof	= true,
				},
			},
		},
	},
	[IPA_ENDPOINT_MODEM_AP_TX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 0,
		.endpoint_id	= 5,
		.toward_ipa	= true,
		.endpoint = {
			.filter_support	= true,
		},
	},
	[IPA_ENDPOINT_MODEM_AP_RX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 7,
		.endpoint_id	= 14,
		.toward_ipa	= false,
	},
	[IPA_ENDPOINT_MODEM_DL_NLO_TX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 2,
		.endpoint_id	= 8,
		.toward_ipa	= true,
		.endpoint = {
			.filter_support	= true,
		},
	},
};

// DBG ipa3_rsrc_src_grp_config
/* Source resource configuration data for an SoC having IPA v4.7 */
static const struct ipa_resource ipa_resource_src[] = {
	[IPA_RESOURCE_TYPE_SRC_PKT_CONTEXTS] = {
		.limits[IPA_RSRC_GROUP_SRC_UL_DL] = {
			.min = 8,	.max = 8,
		},
	},
	[IPA_RESOURCE_TYPE_SRC_DESCRIPTOR_LISTS] = {
		.limits[IPA_RSRC_GROUP_SRC_UL_DL] = {
			.min = 8,	.max = 8,
		},
	},
	[IPA_RESOURCE_TYPE_SRC_DESCRIPTOR_BUFF] = {
		.limits[IPA_RSRC_GROUP_SRC_UL_DL] = {
			.min = 18,	.max = 18,
		},
	},
	[IPA_RESOURCE_TYPE_SRC_HPS_DMARS] = {
		.limits[IPA_RSRC_GROUP_SRC_UL_DL] = {
			.min = 2,	.max = 2,
		},
	},
	[IPA_RESOURCE_TYPE_SRC_ACK_ENTRIES] = {
		.limits[IPA_RSRC_GROUP_SRC_UL_DL] = {
			.min = 15,	.max = 15,
		},
	},
};

// DBG ipa3_rsrc_dst_grp_config
/* Destination resource configuration data for an SoC having IPA v4.7 */
static const struct ipa_resource ipa_resource_dst[] = {
	[IPA_RESOURCE_TYPE_DST_DATA_SECTORS] = {
		.limits[IPA_RSRC_GROUP_DST_UL_DL_DPL] = {
			.min = 7,	.max = 7,
		},
	},
	[IPA_RESOURCE_TYPE_DST_DPS_DMARS] = {
		.limits[IPA_RSRC_GROUP_DST_UL_DL_DPL] = {
			.min = 2,	.max = 2,
		},
	},
};

/* Resource configuration data for an SoC having IPA v4.7 */
static const struct ipa_resource_data ipa_resource_data = {
	.rsrc_group_src_count	= IPA_RSRC_GROUP_SRC_COUNT,
	.rsrc_group_dst_count	= IPA_RSRC_GROUP_DST_COUNT,
	.resource_src_count	= ARRAY_SIZE(ipa_resource_src),
	.resource_src		= ipa_resource_src,
	.resource_dst_count	= ARRAY_SIZE(ipa_resource_dst),
	.resource_dst		= ipa_resource_dst,
};

// DBG ipa_4_7_mem_part
// DBG canary_count = _ipa_init_sram_v3 ipa3_sram_set_canary
//	offset = canary_count = 1
//	offset and offset - 4 = canary_count = 2
//	offset - 12 = canary_count = 4
/* IPA-resident memory region data for an SoC having IPA v4.7 */
static const struct ipa_mem ipa_mem_local_data[] = {
	{
		.id		= IPA_MEM_UC_SHARED,
		.offset		= 0x0000,
		.size		= 0x0080,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_UC_INFO,
		.offset		= 0x0080,
		.size		= 0x0200,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_V4_FILTER_HASHED,
		.offset		= 0x0288,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V4_FILTER,
		.offset		= 0x0308,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V6_FILTER_HASHED,
		.offset		= 0x0388,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V6_FILTER,
		.offset		= 0x0408,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V4_ROUTE_HASHED,
		.offset		= 0x0488,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V4_ROUTE,
		.offset		= 0x0508,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V6_ROUTE_HASHED,
		.offset		= 0x0588,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V6_ROUTE,
		.offset		= 0x0608,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_MODEM_HEADER,
		.offset		= 0x0688,
		.size		= 0x0240,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_AP_HEADER,
		.offset		= 0x08c8,
		.size		= 0x0200,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_MODEM_PROC_CTX,
		.offset		= 0x0ad0,
		.size		= 0x0ac0,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_AP_PROC_CTX,
		.offset		= 0x1590,
		.size		= 0x0200,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_NAT_TABLE,
		.offset		= 0x17a0,
		.size		= 0x0d00,
		.canary_count	= 4,
	},
	///
	{
		.id		= IPA_MEM_PDN_CONFIG,
		.offset		= 0x24a8,
		.size		= 0x0050,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_STATS_QUOTA_MODEM,
		.offset		= 0x2500,
		.size		= 0x0030,
		.canary_count	= 4,
	},
	{
		.id		= IPA_MEM_STATS_QUOTA_AP,
		.offset		= 0x2530,
		.size		= 0x0048,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_STATS_TETHERING,
		.offset		= 0x2578,
		.size		= 0x0238,
		.canary_count	= 0,
	},
	//{ // FIXME??? .stats_fnr_ofst
	//	.id		= IPA_MEM_STATS_FILTER_ROUTE,
	//	.offset		= 0x27b0,
	//	.size		= 0x0000,
	//	.canary_count	= 0,
	//},
	{
		.id		= IPA_MEM_STATS_DROP,
		.offset		= 0x2fb0,
		.size		= 0x0020,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_MODEM,
		.offset		= 0x27d8,
		.size		= 0x0800,
		.canary_count	= 2,
	},
	//{ FIXME ? .apps_v6_rt_nhash_size
	//	.id		= IPA_MEM_UC_EVENT_RING,
	//	.offset		= 0x3000,
	//	.size		= 0x0000,
	//	.canary_count	= 1,
	//},
};

// DBG
//ipa_smmu_ap: ipa_smmu_ap {
//	compatible = "qcom,ipa-smmu-ap-cb";
//	iommus = <&apps_smmu 0x0440 0x0>;
//	qcom,iommu-dma-addr-pool = <0x20000000 0x40000000>;
//	/* modem tables in IMEM */
//	qcom,additional-mapping = <0x146a8000 0x146a8000 0x2000>;
//	qcom,iommu-dma = "fastmap";
//	qcom,ipa-q6-smem-size = <26624>;
//	qcom,geometry-mapping = <0x0 0xF0000000>;
//};
//smem_id = SMEM_IPA_FILTER_TABLE, smem_size = qcom,ipa-q6-smem-size
/* Memory configuration data for an SoC having IPA v4.7 */
static const struct ipa_mem_data ipa_mem_data = {
	.local_count	= ARRAY_SIZE(ipa_mem_local_data),
	.local		= ipa_mem_local_data,
	/* lagoon = 0x146a8000, lito = 0x146a9000 */
	.imem_addr	= 0x146a8000,
	.imem_size	= 0x00002000,
	.smem_id	= 497,
	//.smem_size	= 0x00006800, // FIXME actually 0x9000, not 0x6800  -- ipa 1e40000.ipa: SMEM item 497 has size 36864, expected 26624
	.smem_size	= 0x00009000,
};

// DBG
// /* SVS2 */
// <MSM_BUS_MASTER_IPA MSM_BUS_SLAVE_LLCC 150000 500000>,
// <MSM_BUS_MASTER_LLCC MSM_BUS_SLAVE_EBI_CH0 150000 700000>,
// <MSM_BUS_MASTER_IPA MSM_BUS_SLAVE_OCIMEM 75000 700000>,
// <MSM_BUS_MASTER_AMPSS_M0 MSM_BUS_SLAVE_IPA_CFG 0 55000>,
// <MSM_BUS_MASTER_IPA_CORE MSM_BUS_SLAVE_IPA_CORE 0 100>,

/* Interconnect rates are in 1000 byte/second units */
static const struct ipa_interconnect_data ipa_interconnect_data[] = {
	{
		.name			= "memory",
		.peak_bandwidth		= 500000,	/* 500 MBps */
		.average_bandwidth	= 150000,	/* 150 MBps */
	},
	// FIXME /* Average rate is unused for the next two interconnects */
	{
		.name			= "imem",
		.peak_bandwidth		= 700000,	/* 700 MBps */
		.average_bandwidth	= 75000,	/* 75 MBps (unused?) */
	},
	{
		.name			= "config",
		.peak_bandwidth		= 55000,	/* 55 MBps */
		.average_bandwidth	= 0,		/* unused */
	},
};

/* Clock and interconnect configuration data for an SoC having IPA v4.7 */
static const struct ipa_power_data ipa_power_data = {
	/* XXX Downstream code says 150 MHz (DT SVS2), 60 MHz (code) */
	.core_clock_rate	= 100 * 1000 * 1000,	/* Hz (150?  60?) */
	.interconnect_count	= ARRAY_SIZE(ipa_interconnect_data),
	.interconnect_data	= ipa_interconnect_data,
};

/* Configuration data for an SoC having IPA v4.7 */
const struct ipa_data ipa_data_v4_7 = {
	.version	= IPA_VERSION_4_7,
	.qsb_count	= ARRAY_SIZE(ipa_qsb_data),
	.qsb_data	= ipa_qsb_data,
	.endpoint_count	= ARRAY_SIZE(ipa_gsi_endpoint_data),
	.endpoint_data	= ipa_gsi_endpoint_data,
	.resource_data	= &ipa_resource_data,
	.mem_data	= &ipa_mem_data,
	.power_data	= &ipa_power_data,
};
