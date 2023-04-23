/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SSC_SNS_SMGR_H__
#define __SSC_SNS_SMGR_H__

#include <linux/iio/common/qcom_smgr.h>
#include <linux/soc/qcom/qmi.h>
#include <linux/types.h>

#define SNS_SMGR_QMI_SVC_ID			0x0100
#define SNS_SMGR_QMI_SVC_V1			1
#define SNS_SMGR_QMI_INS_ID			50

#define SNS_SMGR_ALL_SENSOR_INFO_MSG_ID		0x05
#define SNS_SMGR_SINGLE_SENSOR_INFO_MSG_ID	0x06
#define SNS_SMGR_BUFFERING_MSG_ID		0x21
#define SNS_SMGR_BUFFERING_REPORT_MSG_ID	0x22

#define SNS_SMGR_ALL_SENSOR_INFO_REQ_MAX_LEN		0x0
#define SNS_SMGR_ALL_SENSOR_INFO_RESP_MAX_LEN		0x3e // might be more
#define SNS_SMGR_SINGLE_SENSOR_INFO_REQ_MAX_LEN		0x4
#define SNS_SMGR_SINGLE_SENSOR_INFO_RESP_MAX_LEN	0x110 // 0x101, + 0xa for good measure. might be more
#define SNS_SMGR_BUFFERING_REQ_MAX_LEN			0x30 // probably less
#define SNS_SMGR_BUFFERING_RESP_MAX_LEN			0x1e // probably less

/* TODO: find actual maximums */
#define SNS_SMGR_ALL_SENSOR_INFO_MAX_LEN	0xf
#define SNS_SMGR_SENSOR_TYPE_MAX_LEN		0xf
#define SNS_SMGR_DATA4_MAX_LEN			0xf
#define SNS_SMGR_SAMPLES_MAX_LEN		0xff

enum sns_smgr_buffering_action {
	SNS_SMGR_BUFFERING_ACTION_ADD	 = 1,
	SNS_SMGR_BUFFERING_ACTION_DELETE = 2,
};

struct sns_smgr_all_sensor_info {
	u8 id;
	u8 type_len;
	char type[SNS_SMGR_SENSOR_TYPE_MAX_LEN];
};

struct sns_smgr_all_sensor_info_resp {
	u16 result;
	u8 item_len;
	struct sns_smgr_all_sensor_info items[SNS_SMGR_ALL_SENSOR_INFO_MAX_LEN];
};

struct sns_smgr_single_sensor_info_req {
	u8 sensor_id;
};

struct sns_smgr_single_sensor_info_data_type {
	u8 sensor_id;
	u8 data_type;
	u8 name_len;
	char name[0xff];
	u8 vendor_len;
	char vendor[0xff];
	u32 val1;
	u16 max_sample_rate;
	u16 val3;
	u16 val4;
	u16 val5;
	u16 val6;
	u32 val7;
};

struct sns_smgr_single_sensor_info_data4 {
	u8 item_len;
	u16 items[SNS_SMGR_DATA4_MAX_LEN];
};

struct sns_smgr_single_sensor_info_resp {
	u16 result;
	u8 data_type_len;
	struct sns_smgr_single_sensor_info_data_type data_types[SNS_SMGR_DATA_TYPE_COUNT];
	u8 data1_len;
	u32 data1[SNS_SMGR_DATA_TYPE_COUNT];
	u32 data2;
	u8 data3_len;
	u64 data3[SNS_SMGR_DATA_TYPE_COUNT];
	u8 data4_len;
	struct sns_smgr_single_sensor_info_data4 data4[SNS_SMGR_DATA_TYPE_COUNT];
	u8 data5_len;
	u32 data5[SNS_SMGR_DATA_TYPE_COUNT];
};

struct sns_smgr_buffering_req_item {
	u8 sensor_id;
	u8 data_type;
	u16 decimation;
	u16 sampling_rate;
	u16 calibration;
};

struct sns_smgr_buffering_req_notify_suspend {
	u16 proc_type;
	u16 send_indications_during_suspend;
};

struct sns_smgr_buffering_req {
	u8 report_id;
	u8 action;
	u32 report_rate;
	u8 item_len;
	struct sns_smgr_buffering_req_item items[SNS_SMGR_DATA_TYPE_COUNT];
	u8 notify_suspend_valid;
	struct sns_smgr_buffering_req_notify_suspend notify_suspend;
};

struct sns_smgr_buffering_resp {
	u16 result;
	u8 report_id;
	u8 ack_nak;
};

struct sns_smgr_buffering_report_metadata {
	u32 val1;
	u8 sample_count;
	u32 timestamp;
	u32 val2;
};

struct sns_smgr_buffering_report_sample {
	u32 values[3];
	u8 val1;
	u8 val2;
	u16 val3;
};

struct sns_smgr_buffering_report_ind {
	u8 report_id;
	struct sns_smgr_buffering_report_metadata metadata;
	u8 samples_len;
	struct sns_smgr_buffering_report_sample samples[SNS_SMGR_SAMPLES_MAX_LEN];
	u8 val2;
};

extern const struct qmi_elem_info sns_smgr_all_sensor_info_resp_ei[];
extern const struct qmi_elem_info sns_smgr_single_sensor_info_req_ei[];
extern const struct qmi_elem_info sns_smgr_single_sensor_info_resp_ei[];
extern const struct qmi_elem_info sns_smgr_buffering_req_ei[];
extern const struct qmi_elem_info sns_smgr_buffering_resp_ei[];
extern const struct qmi_elem_info sns_smgr_buffering_report_ind_ei[];

extern enum smgr_sensor_type sns_smgr_sensor_type_from_str(const char *str);

#endif /* __SSC_SNS_SMGR_H__ */
