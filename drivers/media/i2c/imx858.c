// SPDX-License-Identifier: GPL-2.0-only
/*
 * A V4L2 driver for Sony IMX858 cameras.
 * Copyright (C) 2024 Luca Weiss <luca.weiss@fairphone.com>
 *
 * Based on Sony imx412 camera driver
 * Copyright (C) 2021 Intel Corporation
 */
#include <asm/unaligned.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

/* Streaming Mode */
#define IMX858_REG_MODE_SELECT	0x0100
#define IMX858_MODE_STANDBY	0x00
#define IMX858_MODE_STREAMING	0x01

/* Lines per frame */
#define IMX858_REG_LPFR		0x0340

/* Chip ID */
#define IMX858_REG_ID		0x0016
#define IMX858_ID		0x858

/* Exposure control */
#define IMX858_REG_EXPOSURE_CIT	0x0202
#define IMX858_EXPOSURE_MIN	8
#define IMX858_EXPOSURE_OFFSET	22
#define IMX858_EXPOSURE_STEP	1
#define IMX858_EXPOSURE_DEFAULT	0x0648

/* Analog gain control */
#define IMX858_REG_AGAIN	0x0204
#define IMX858_AGAIN_MIN	0
#define IMX858_AGAIN_MAX	978
#define IMX858_AGAIN_STEP	1
#define IMX858_AGAIN_DEFAULT	0

/* Group hold register */
#define IMX858_REG_HOLD		0x0104

/* Input clock rate */
#define IMX858_INCLK_RATE	24000000

/* CSI2 HW configuration */
#define IMX858_LINK_FREQ	600000000
#define IMX858_NUM_DATA_LANES	4

#define IMX858_REG_MIN		0x00
#define IMX858_REG_MAX		0xffff

/**
 * struct imx858_reg - imx858 sensor register
 * @address: Register address
 * @val: Register value
 */
struct imx858_reg {
	u16 address;
	u8 val;
};

/**
 * struct imx858_reg_list - imx858 sensor register list
 * @num_of_regs: Number of registers in the list
 * @regs: Pointer to register list
 */
struct imx858_reg_list {
	u32 num_of_regs;
	const struct imx858_reg *regs;
};

/**
 * struct imx858_mode - imx858 sensor mode structure
 * @width: Frame width
 * @height: Frame height
 * @code: Format code
 * @hblank: Horizontal blanking in lines
 * @vblank: Vertical blanking in lines
 * @vblank_min: Minimum vertical blanking in lines
 * @vblank_max: Maximum vertical blanking in lines
 * @pclk: Sensor pixel clock
 * @link_freq_idx: Link frequency index
 * @reg_list: Register list for sensor mode
 */
struct imx858_mode {
	u32 width;
	u32 height;
	u32 code;
	u32 hblank;
	u32 vblank;
	u32 vblank_min;
	u32 vblank_max;
	u64 pclk;
	u32 link_freq_idx;
	struct imx858_reg_list reg_list;
};

static const char * const imx858_supply_names[] = {
	"iovdd",	/* Digital I/O power */
	"avdd",		/* Analog power */
	"dvdd",		/* Digital core power */
};

/**
 * struct imx858 - imx858 sensor device structure
 * @dev: Pointer to generic device
 * @client: Pointer to i2c client
 * @sd: V4L2 sub-device
 * @pad: Media pad. Only one pad supported
 * @reset_gpio: Sensor reset gpio
 * @inclk: Sensor input clock
 * @supplies: Regulator supplies
 * @ctrl_handler: V4L2 control handler
 * @link_freq_ctrl: Pointer to link frequency control
 * @pclk_ctrl: Pointer to pixel clock control
 * @hblank_ctrl: Pointer to horizontal blanking control
 * @vblank_ctrl: Pointer to vertical blanking control
 * @exp_ctrl: Pointer to exposure control
 * @again_ctrl: Pointer to analog gain control
 * @vblank: Vertical blanking in lines
 * @cur_mode: Pointer to current selected sensor mode
 * @mutex: Mutex for serializing sensor controls
 */
struct imx858 {
	struct device *dev;
	struct i2c_client *client;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct gpio_desc *reset_gpio;
	struct clk *inclk;
	struct regulator_bulk_data supplies[ARRAY_SIZE(imx858_supply_names)];
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *link_freq_ctrl;
	struct v4l2_ctrl *pclk_ctrl;
	struct v4l2_ctrl *hblank_ctrl;
	struct v4l2_ctrl *vblank_ctrl;
	struct {
		struct v4l2_ctrl *exp_ctrl;
		struct v4l2_ctrl *again_ctrl;
	};
	u32 vblank;
	const struct imx858_mode *cur_mode;
	struct mutex mutex;
};

static const s64 link_freq[] = {
	IMX858_LINK_FREQ,
};

/* Sensor mode registers */
static const struct imx858_reg mode_2048x1536_regs[] = {
	// common registers
	{0x0136, 0x18},
	{0x0137, 0x00},
	{0x3304, 0x00},
	{0x33f0, 0x01},
	{0x33f1, 0x05},
	{0x0111, 0x02},
	{0x1200, 0x02},
	{0x1201, 0x02},
	{0x130b, 0x00},
	{0x1340, 0x00},
	{0x3bc0, 0xbf},
	{0x3bc4, 0xbf},
	{0x3bc8, 0xbf},
	{0x3bcc, 0xbf},
	{0x558f, 0x00},
	{0x5e2e, 0x00},
	{0x5e2f, 0x32},
	{0x5e32, 0x08},
	{0x5e33, 0xcd},
	{0x5e64, 0x00},
	{0x5e65, 0x32},
	{0x5e68, 0x0b},
	{0x5e69, 0x97},
	{0x61e8, 0x50},
	{0x61e9, 0x00},
	{0x61ea, 0x50},
	{0x61eb, 0x00},
	{0x7220, 0xff},
	{0x7221, 0xff},
	{0x7222, 0xff},
	{0x7223, 0xff},
	{0x7755, 0x09},
	{0x775b, 0x01},
	{0x7a28, 0x2d},
	{0x7a29, 0x30},
	{0x7a2a, 0x30},
	{0x7a2b, 0x0e},
	{0x7a2c, 0x10},
	{0x7a2d, 0x10},
	{0x7a2e, 0x0e},
	{0x7a2f, 0x0f},
	{0x7a30, 0x0f},
	{0x7a31, 0x10},
	{0x7a32, 0x10},
	{0x7a33, 0x10},
	{0x7a34, 0x0e},
	{0x7a35, 0x12},
	{0x7a36, 0x15},
	{0x7a3a, 0x2d},
	{0x7a3b, 0x30},
	{0x7a3c, 0x31},
	{0x7a3d, 0x2b},
	{0x7a3e, 0x2d},
	{0x7a3f, 0x2e},
	{0x7a40, 0x2e},
	{0x7a41, 0x2f},
	{0x7a42, 0x2f},
	{0x7a43, 0x2e},
	{0x7a44, 0x2f},
	{0x7a45, 0x2e},
	{0x7a46, 0x2f},
	{0x7a47, 0x31},
	{0x7a48, 0x34},
	{0x7a4c, 0x2f},
	{0x7a4d, 0x31},
	{0x7a4e, 0x31},
	{0x7a4f, 0x2d},
	{0x7a50, 0x2f},
	{0x7a51, 0x31},
	{0x7a52, 0x2f},
	{0x7a53, 0x31},
	{0x7a54, 0x31},
	{0x7a55, 0x2f},
	{0x7a56, 0x30},
	{0x7a57, 0x30},
	{0x7a58, 0x30},
	{0x7a59, 0x31},
	{0x7a5a, 0x36},
	{0x7a5b, 0x31},
	{0x7a5c, 0x33},
	{0x7a5e, 0x2f},
	{0x7a5f, 0x33},
	{0x7a60, 0x32},
	{0x7a61, 0x2d},
	{0x7a62, 0x30},
	{0x7a63, 0x31},
	{0x7a64, 0x30},
	{0x7a65, 0x30},
	{0x7a66, 0x30},
	{0x7a67, 0x30},
	{0x7a68, 0x31},
	{0x7a69, 0x31},
	{0x7a6a, 0x30},
	{0x7a6b, 0x30},
	{0x7a6c, 0x37},
	{0x7a6d, 0x32},
	{0x7a6e, 0x33},
	{0x7a70, 0x2f},
	{0x7a71, 0x30},
	{0x7a72, 0x31},
	{0x7a73, 0x31},
	{0x7a74, 0x32},
	{0x7a75, 0x32},
	{0x7a76, 0x31},
	{0x7a77, 0x31},
	{0x7a78, 0x32},
	{0x7a79, 0x32},
	{0x7a7a, 0x31},
	{0x7a7b, 0x33},
	{0x7a7c, 0x33},
	{0x7a7d, 0x34},
	{0x7a7f, 0x2f},
	{0x7a80, 0x31},
	{0x7a81, 0x32},
	{0x7a82, 0x31},
	{0x7a83, 0x31},
	{0x7a84, 0x31},
	{0x7a85, 0x31},
	{0x7a86, 0x32},
	{0x7a87, 0x31},
	{0x7a88, 0x31},
	{0x7a89, 0x32},
	{0x7a8a, 0x34},
	{0x7a8b, 0x33},
	{0x7a8c, 0x35},
	{0x7a90, 0x02},
	{0x7a92, 0x01},
	{0x7a95, 0x01},
	{0x7a98, 0x03},
	{0x7aa2, 0x02},
	{0x7aa5, 0x05},
	{0x7aaa, 0x08},
	{0x7aab, 0x02},
	{0x7ab4, 0x18},
	{0x7ab7, 0x06},
	{0x7abc, 0x03},
	{0x7abd, 0x02},
	{0x7ace, 0x06},
	{0x7acf, 0x07},
	{0x7aec, 0x01},
	{0x7b27, 0x09},
	{0x7b28, 0x08},
	{0x7b39, 0x06},
	{0x7b3a, 0x07},
	{0x7b48, 0x07},
	{0x7b49, 0x09},
	{0x7b57, 0x05},
	{0x7b58, 0x06},
	{0x7c18, 0x2d},
	{0x7c1e, 0x2d},
	{0x7c22, 0x23},
	{0x7c23, 0x1e},
	{0x7d5d, 0x19},
	{0x7d5e, 0x19},
	{0x7d5f, 0x19},
	{0x7d60, 0x19},
	{0x7d61, 0x19},
	{0x7d62, 0x19},
	{0x7d64, 0x19},
	{0x7d65, 0x19},
	{0x7d66, 0x19},
	{0x7d67, 0x19},
	{0x7d68, 0x19},
	{0x7d69, 0x19},
	{0x7d6b, 0x19},
	{0x7d6c, 0x19},
	{0x7d6d, 0x19},
	{0x7d6e, 0x19},
	{0x7d6f, 0x19},
	{0x7d70, 0x19},
	{0x7d72, 0x19},
	{0x7d73, 0x19},
	{0x7d74, 0x19},
	{0x7d75, 0x19},
	{0x7d76, 0x19},
	{0x7d77, 0x19},
	{0x7d79, 0x19},
	{0x7d7a, 0x19},
	{0x7d7b, 0x19},
	{0x7d7c, 0x19},
	{0x7d7d, 0x19},
	{0x7d7f, 0x19},
	{0x7d80, 0x19},
	{0x7d81, 0x19},
	{0x7d82, 0x19},
	{0x7d83, 0x19},
	{0x90b4, 0x0b},
	{0x90b5, 0x2c},
	{0x90b8, 0x0c},
	{0x90b9, 0x3c},
	{0x90e7, 0x01},
	{0x920c, 0x90},
	{0x920e, 0x53},
	{0x920f, 0x0c},
	{0x9210, 0xa0},
	{0x9212, 0xdd},
	{0x9213, 0xda},
	{0x9214, 0xa0},
	{0x9216, 0xeb},
	{0x9217, 0x96},
	{0x9218, 0xa0},
	{0x921a, 0xdd},
	{0x921b, 0xd7},
	{0x9674, 0x21},
	{0x9675, 0x5c},
	{0x96af, 0x01},
	{0x9739, 0x00},
	{0x973a, 0x13},
	{0x973b, 0x04},
	{0x973d, 0x00},
	{0x973e, 0x1c},
	{0x973f, 0xf4},
	{0x9741, 0x00},
	{0x9742, 0x32},
	{0x9743, 0x48},
	{0xa2c3, 0x18},
	{0xa2f5, 0x04},
	{0xa722, 0x00},
	{0xad01, 0x0a},
	{0xad02, 0x0a},
	{0xad0e, 0x02},
	{0xdda9, 0x4e},

	// res3 2048*1536@60fps (4:3) 4x4
	{0x0112, 0x0a},
	{0x0113, 0x0a},
	{0x0114, 0x03},
	{0x3239, 0x00},
	{0x0342, 0x0f},
	{0x0343, 0xb8},
	{0x3850, 0x03},
	{0x3851, 0xf0},
	{0x0340, 0x06},
	{0x0341, 0x32},
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x1f},
	{0x0349, 0xff},
	{0x034a, 0x17},
	{0x034b, 0xff},
	{0x0900, 0x01},
	{0x0901, 0x44},
	{0x0902, 0x00},
	{0x3005, 0x02},
	{0x3006, 0x02},
	{0x3140, 0x0a},
	{0x3144, 0x00},
	{0x3148, 0x04},
	{0x31c0, 0x43},
	{0x31c1, 0x43},
	{0x3205, 0x00},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040a, 0x00},
	{0x040b, 0x00},
	{0x040c, 0x08},
	{0x040d, 0x00},
	{0x040e, 0x06},
	{0x040f, 0x00},
	{0x034c, 0x08},
	{0x034d, 0x00},
	{0x034e, 0x06},
	{0x034f, 0x00},
	{0x0301, 0x05},
	{0x0303, 0x04},
	{0x0305, 0x04},
	{0x0306, 0x01},
	{0x0307, 0x40},
	{0x030b, 0x02},
	{0x030d, 0x02},
	{0x030e, 0x00},
	{0x030f, 0xc2},
	{0x3104, 0x01},
	{0x324c, 0x01},
	{0x3803, 0x01},
	{0x3804, 0x01},
	{0x3805, 0x01},
	{0x3806, 0x01},
	{0x38a0, 0x01},
	{0x38a1, 0x5e},
	{0x38a2, 0x00},
	{0x38a3, 0x00},
	{0x38a4, 0x00},
	{0x38a5, 0x00},
	{0x38a8, 0x01},
	{0x38a9, 0x5e},
	{0x38aa, 0x00},
	{0x38ab, 0x00},
	{0x38ac, 0x00},
	{0x38ad, 0x00},
	{0x38d0, 0x00},
	{0x38d1, 0xd2},
	{0x38d2, 0x00},
	{0x38d3, 0xd2},
	{0x38e0, 0x00},
	{0x38e1, 0x00},
	{0x38e2, 0x00},
	{0x38e3, 0x00},
	{0x38e4, 0x00},
	{0x38e5, 0x00},
	{0x38e6, 0x00},
	{0x38e7, 0x00},
	{0x3b00, 0x00},
	{0x3b01, 0x00},
	{0x3b04, 0x00},
	{0x3b05, 0x00},
	{0x3b06, 0x00},
	{0x3b07, 0x00},
	{0x3b0a, 0x00},
	{0x3b0b, 0x00},
	{0x0202, 0x03},
	{0x0203, 0xe8},
	{0x0204, 0x01},
	{0x0205, 0x34},
	{0x020e, 0x01},
	{0x020f, 0x00},
	{0x3103, 0x00},
	{0x3422, 0x01},
	{0x3423, 0xfc},
	{0x3190, 0x00},
	{0x0224, 0x01},
	{0x0225, 0xf4},
	{0x0216, 0x00},
	{0x0217, 0x00},
	{0x0218, 0x01},
	{0x0219, 0x00},
	{0x0e00, 0x00},
	{0x30a4, 0x00},
	{0x30a6, 0x00},
	{0x30c6, 0x01},
	{0x30c8, 0x01},
	{0x30f2, 0x01},
	{0x30f3, 0x01},
	{0x30a5, 0x30},
	{0x30a7, 0x30},
	{0x30c7, 0x30},
	{0x30c9, 0x30},
	{0x30a2, 0x00},
	{0x30c4, 0x01},
	{0x30f1, 0x01},
	{0x30a3, 0x30},
	{0x30c5, 0x30},
};

/* Supported sensor mode configurations */
static const struct imx858_mode supported_mode = {
	.width = 2048,
	.height = 1536,
	.hblank = 456, // FIXME
	.vblank = 506, // FIXME
	.vblank_min = 506, // FIXME
	.vblank_max = 32420, // FIXME
	.pclk = 619200000, // outputPixelClock?
	.link_freq_idx = 0,
	.code = MEDIA_BUS_FMT_SRGGB10_1X10,
	.reg_list = {
		.num_of_regs = ARRAY_SIZE(mode_2048x1536_regs),
		.regs = mode_2048x1536_regs,
	},
};

/**
 * to_imx858() - imx858 V4L2 sub-device to imx858 device.
 * @subdev: pointer to imx858 V4L2 sub-device
 *
 * Return: pointer to imx858 device
 */
static inline struct imx858 *to_imx858(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct imx858, sd);
}

/**
 * imx858_read_reg() - Read registers.
 * @imx858: pointer to imx858 device
 * @reg: register address
 * @len: length of bytes to read. Max supported bytes is 4
 * @val: pointer to register value to be filled.
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx858_read_reg(struct imx858 *imx858, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx858->sd);
	struct i2c_msg msgs[2] = {0};
	u8 addr_buf[2] = {0};
	u8 data_buf[4] = {0};
	int ret;

	if (WARN_ON(len > 4))
		return -EINVAL;

	put_unaligned_be16(reg, addr_buf);

	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = ARRAY_SIZE(addr_buf);
	msgs[0].buf = addr_buf;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_buf[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = get_unaligned_be32(data_buf);

	return 0;
}

/**
 * imx858_write_reg() - Write register
 * @imx858: pointer to imx858 device
 * @reg: register address
 * @len: length of bytes. Max supported bytes is 4
 * @val: register value
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx858_write_reg(struct imx858 *imx858, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx858->sd);
	u8 buf[6] = {0};

	if (WARN_ON(len > 4))
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << (8 * (4 - len)), buf + 2);
	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

/**
 * imx858_write_regs() - Write a list of registers
 * @imx858: pointer to imx858 device
 * @regs: list of registers to be written
 * @len: length of registers array
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx858_write_regs(struct imx858 *imx858,
			     const struct imx858_reg *regs, u32 len)
{
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = imx858_write_reg(imx858, regs[i].address, 1, regs[i].val);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * imx858_update_controls() - Update control ranges based on streaming mode
 * @imx858: pointer to imx858 device
 * @mode: pointer to imx858_mode sensor mode
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx858_update_controls(struct imx858 *imx858,
				  const struct imx858_mode *mode)
{
	int ret;

	ret = __v4l2_ctrl_s_ctrl(imx858->link_freq_ctrl, mode->link_freq_idx);
	if (ret)
		return ret;

	ret = __v4l2_ctrl_s_ctrl(imx858->hblank_ctrl, mode->hblank);
	if (ret)
		return ret;

	return __v4l2_ctrl_modify_range(imx858->vblank_ctrl, mode->vblank_min,
					mode->vblank_max, 1, mode->vblank);
}

/**
 * imx858_update_exp_gain() - Set updated exposure and gain
 * @imx858: pointer to imx858 device
 * @exposure: updated exposure value
 * @gain: updated analog gain value
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx858_update_exp_gain(struct imx858 *imx858, u32 exposure, u32 gain)
{
	u32 lpfr;
	int ret;

	lpfr = imx858->vblank + imx858->cur_mode->height;

	dev_dbg(imx858->dev, "Set exp %u, analog gain %u, lpfr %u\n",
		exposure, gain, lpfr);

	ret = imx858_write_reg(imx858, IMX858_REG_HOLD, 1, 1);
	if (ret)
		return ret;

	ret = imx858_write_reg(imx858, IMX858_REG_LPFR, 2, lpfr);
	if (ret)
		goto error_release_group_hold;

	ret = imx858_write_reg(imx858, IMX858_REG_EXPOSURE_CIT, 2, exposure);
	if (ret)
		goto error_release_group_hold;

	ret = imx858_write_reg(imx858, IMX858_REG_AGAIN, 2, gain);

error_release_group_hold:
	imx858_write_reg(imx858, IMX858_REG_HOLD, 1, 0);

	return ret;
}

/**
 * imx858_set_ctrl() - Set subdevice control
 * @ctrl: pointer to v4l2_ctrl structure
 *
 * Supported controls:
 * - V4L2_CID_VBLANK
 * - cluster controls:
 *   - V4L2_CID_ANALOGUE_GAIN
 *   - V4L2_CID_EXPOSURE
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx858_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx858 *imx858 =
		container_of(ctrl->handler, struct imx858, ctrl_handler);
	u32 analog_gain;
	u32 exposure;
	int ret;

	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		imx858->vblank = imx858->vblank_ctrl->val;

		dev_dbg(imx858->dev, "Received vblank %u, new lpfr %u\n",
			imx858->vblank,
			imx858->vblank + imx858->cur_mode->height);

		ret = __v4l2_ctrl_modify_range(imx858->exp_ctrl,
					       IMX858_EXPOSURE_MIN,
					       imx858->vblank +
					       imx858->cur_mode->height -
					       IMX858_EXPOSURE_OFFSET,
					       1, IMX858_EXPOSURE_DEFAULT);
		break;
	case V4L2_CID_EXPOSURE:
		/* Set controls only if sensor is in power on state */
		if (!pm_runtime_get_if_in_use(imx858->dev))
			return 0;

		exposure = ctrl->val;
		analog_gain = imx858->again_ctrl->val;

		dev_dbg(imx858->dev, "Received exp %u, analog gain %u\n",
			exposure, analog_gain);

		ret = imx858_update_exp_gain(imx858, exposure, analog_gain);

		pm_runtime_put(imx858->dev);

		break;
	default:
		dev_err(imx858->dev, "Invalid control %d\n", ctrl->id);
		ret = -EINVAL;
	}

	return ret;
}

/* V4l2 subdevice control ops*/
static const struct v4l2_ctrl_ops imx858_ctrl_ops = {
	.s_ctrl = imx858_set_ctrl,
};

/**
 * imx858_enum_mbus_code() - Enumerate V4L2 sub-device mbus codes
 * @sd: pointer to imx858 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @code: V4L2 sub-device code enumeration need to be filled
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx858_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = supported_mode.code;

	return 0;
}

/**
 * imx858_enum_frame_size() - Enumerate V4L2 sub-device frame sizes
 * @sd: pointer to imx858 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fsize: V4L2 sub-device size enumeration need to be filled
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx858_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fsize)
{
	if (fsize->index > 0)
		return -EINVAL;

	if (fsize->code != supported_mode.code)
		return -EINVAL;

	fsize->min_width = supported_mode.width;
	fsize->max_width = fsize->min_width;
	fsize->min_height = supported_mode.height;
	fsize->max_height = fsize->min_height;

	return 0;
}

/**
 * imx858_fill_pad_format() - Fill subdevice pad format
 *                            from selected sensor mode
 * @imx858: pointer to imx858 device
 * @mode: pointer to imx858_mode sensor mode
 * @fmt: V4L2 sub-device format need to be filled
 */
static void imx858_fill_pad_format(struct imx858 *imx858,
				   const struct imx858_mode *mode,
				   struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.code = mode->code;
	fmt->format.field = V4L2_FIELD_NONE;
	fmt->format.colorspace = V4L2_COLORSPACE_RAW;
	fmt->format.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->format.quantization = V4L2_QUANTIZATION_DEFAULT;
	fmt->format.xfer_func = V4L2_XFER_FUNC_NONE;
}

/**
 * imx858_get_pad_format() - Get subdevice pad format
 * @sd: pointer to imx858 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fmt: V4L2 sub-device format need to be set
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx858_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx858 *imx858 = to_imx858(sd);

	mutex_lock(&imx858->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *framefmt;

		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		fmt->format = *framefmt;
	} else {
		imx858_fill_pad_format(imx858, imx858->cur_mode, fmt);
	}

	mutex_unlock(&imx858->mutex);

	return 0;
}

/**
 * imx858_set_pad_format() - Set subdevice pad format
 * @sd: pointer to imx858 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fmt: V4L2 sub-device format need to be set
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx858_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx858 *imx858 = to_imx858(sd);
	const struct imx858_mode *mode;
	int ret = 0;

	mutex_lock(&imx858->mutex);

	mode = &supported_mode;
	imx858_fill_pad_format(imx858, mode, fmt);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *framefmt;

		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		*framefmt = fmt->format;
	} else {
		ret = imx858_update_controls(imx858, mode);
		if (!ret)
			imx858->cur_mode = mode;
	}

	mutex_unlock(&imx858->mutex);

	return ret;
}

/**
 * imx858_init_state() - Initialize sub-device state
 * @sd: pointer to imx858 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx858_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	struct imx858 *imx858 = to_imx858(sd);
	struct v4l2_subdev_format fmt = { 0 };

	fmt.which = sd_state ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	imx858_fill_pad_format(imx858, &supported_mode, &fmt);

	return imx858_set_pad_format(sd, sd_state, &fmt);
}

/**
 * imx858_start_streaming() - Start sensor stream
 * @imx858: pointer to imx858 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx858_start_streaming(struct imx858 *imx858)
{
	const struct imx858_reg_list *reg_list;
	int ret;

	/* Write sensor mode registers */
	reg_list = &imx858->cur_mode->reg_list;
	ret = imx858_write_regs(imx858, reg_list->regs,
				reg_list->num_of_regs);
	if (ret) {
		dev_err(imx858->dev, "fail to write initial registers\n");
		return ret;
	}

	/* Setup handler will write actual exposure and gain */
	ret =  __v4l2_ctrl_handler_setup(imx858->sd.ctrl_handler);
	if (ret) {
		dev_err(imx858->dev, "fail to setup handler\n");
		return ret;
	}

	/* Delay is required before streaming*/
	usleep_range(7400, 8000);

	/* Start streaming */
	ret = imx858_write_reg(imx858, IMX858_REG_MODE_SELECT,
			       1, IMX858_MODE_STREAMING);
	if (ret) {
		dev_err(imx858->dev, "fail to start streaming\n");
		return ret;
	}

	return 0;
}

/**
 * imx858_stop_streaming() - Stop sensor stream
 * @imx858: pointer to imx858 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx858_stop_streaming(struct imx858 *imx858)
{
	return imx858_write_reg(imx858, IMX858_REG_MODE_SELECT,
				1, IMX858_MODE_STANDBY);
}

/**
 * imx858_set_stream() - Enable sensor streaming
 * @sd: pointer to imx858 subdevice
 * @enable: set to enable sensor streaming
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx858_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx858 *imx858 = to_imx858(sd);
	int ret;

	mutex_lock(&imx858->mutex);

	if (enable) {
		ret = pm_runtime_resume_and_get(imx858->dev);
		if (ret)
			goto error_unlock;

		ret = imx858_start_streaming(imx858);
		if (ret)
			goto error_power_off;
	} else {
		imx858_stop_streaming(imx858);
		pm_runtime_put(imx858->dev);
	}

	mutex_unlock(&imx858->mutex);

	return 0;

error_power_off:
	pm_runtime_put(imx858->dev);
error_unlock:
	mutex_unlock(&imx858->mutex);

	return ret;
}

/**
 * imx858_detect() - Detect imx858 sensor
 * @imx858: pointer to imx858 device
 *
 * Return: 0 if successful, -EIO if sensor id does not match
 */
static int imx858_detect(struct imx858 *imx858)
{
	int ret;
	u32 val;

	ret = imx858_read_reg(imx858, IMX858_REG_ID, 2, &val);
	if (ret)
		return ret;

	if (val != IMX858_ID) {
		dev_err(imx858->dev, "chip id mismatch: %x!=%x\n",
			IMX858_ID, val);
		return -ENXIO;
	}

	return 0;
}

/**
 * imx858_parse_hw_config() - Parse HW configuration and check if supported
 * @imx858: pointer to imx858 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx858_parse_hw_config(struct imx858 *imx858)
{
	struct fwnode_handle *fwnode = dev_fwnode(imx858->dev);
	struct v4l2_fwnode_endpoint bus_cfg = {};
	struct fwnode_handle *ep;
	unsigned long rate;
	unsigned int i;
	int ret;

	if (!fwnode)
		return -ENXIO;

	/* Request optional reset pin */
	imx858->reset_gpio = devm_gpiod_get_optional(imx858->dev, "reset",
						     GPIOD_OUT_LOW);
	if (IS_ERR(imx858->reset_gpio)) {
		dev_err(imx858->dev, "failed to get reset gpio %ld\n",
			PTR_ERR(imx858->reset_gpio));
		return PTR_ERR(imx858->reset_gpio);
	}

	/* Get sensor input clock */
	imx858->inclk = devm_clk_get(imx858->dev, NULL);
	if (IS_ERR(imx858->inclk)) {
		dev_err(imx858->dev, "could not get inclk\n");
		return PTR_ERR(imx858->inclk);
	}

	rate = clk_get_rate(imx858->inclk);
	if (rate != IMX858_INCLK_RATE) {
		dev_err(imx858->dev, "inclk frequency mismatch\n");
		return -EINVAL;
	}

	/* Get optional DT defined regulators */
	for (i = 0; i < ARRAY_SIZE(imx858_supply_names); i++)
		imx858->supplies[i].supply = imx858_supply_names[i];

	ret = devm_regulator_bulk_get(imx858->dev,
				      ARRAY_SIZE(imx858_supply_names),
				      imx858->supplies);
	if (ret)
		return ret;

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -ENXIO;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	if (bus_cfg.bus_type != V4L2_MBUS_CSI2_DPHY) {
		dev_err(imx858->dev, "selected bus-type is not supported\n");
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != IMX858_NUM_DATA_LANES) {
		dev_err(imx858->dev,
			"number of CSI2 data lanes %d is not supported\n",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	if (!bus_cfg.nr_of_link_frequencies) {
		dev_err(imx858->dev, "no link frequencies defined\n");
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	for (i = 0; i < bus_cfg.nr_of_link_frequencies; i++)
		if (bus_cfg.link_frequencies[i] == IMX858_LINK_FREQ)
			goto done_endpoint_free;

	ret = -EINVAL;

done_endpoint_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

/* V4l2 subdevice ops */
static const struct v4l2_subdev_video_ops imx858_video_ops = {
	.s_stream = imx858_set_stream,
};

static const struct v4l2_subdev_pad_ops imx858_pad_ops = {
	.enum_mbus_code = imx858_enum_mbus_code,
	.enum_frame_size = imx858_enum_frame_size,
	.get_fmt = imx858_get_pad_format,
	.set_fmt = imx858_set_pad_format,
};

static const struct v4l2_subdev_ops imx858_subdev_ops = {
	.video = &imx858_video_ops,
	.pad = &imx858_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx858_internal_ops = {
	.init_state = imx858_init_state,
};

/**
 * imx858_power_on() - Sensor power on sequence
 * @dev: pointer to i2c device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx858_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx858 *imx858 = to_imx858(sd);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(imx858_supply_names),
				    imx858->supplies);
	if (ret < 0) {
		dev_err(dev, "failed to enable regulators\n");
		return ret;
	}

	gpiod_set_value_cansleep(imx858->reset_gpio, 0);

	ret = clk_prepare_enable(imx858->inclk);
	if (ret) {
		dev_err(imx858->dev, "fail to enable inclk\n");
		goto error_reset;
	}

	usleep_range(1000, 1200);

	return 0;

error_reset:
	gpiod_set_value_cansleep(imx858->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(imx858_supply_names),
			       imx858->supplies);

	return ret;
}

/**
 * imx858_power_off() - Sensor power off sequence
 * @dev: pointer to i2c device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx858_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx858 *imx858 = to_imx858(sd);

	clk_disable_unprepare(imx858->inclk);

	gpiod_set_value_cansleep(imx858->reset_gpio, 1);

	regulator_bulk_disable(ARRAY_SIZE(imx858_supply_names),
			       imx858->supplies);

	return 0;
}

/**
 * imx858_init_controls() - Initialize sensor subdevice controls
 * @imx858: pointer to imx858 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx858_init_controls(struct imx858 *imx858)
{
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr = &imx858->ctrl_handler;
	const struct imx858_mode *mode = imx858->cur_mode;
	u32 lpfr;
	int ret;

	/* set properties from fwnode (e.g. rotation, orientation) */
	ret = v4l2_fwnode_device_parse(imx858->dev, &props);
	if (ret)
		return ret;

	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 8);
	if (ret)
		return ret;

	/* Serialize controls with sensor device */
	ctrl_hdlr->lock = &imx858->mutex;

	/* Initialize exposure and gain */
	lpfr = mode->vblank + mode->height;
	imx858->exp_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					     &imx858_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX858_EXPOSURE_MIN,
					     lpfr - IMX858_EXPOSURE_OFFSET,
					     IMX858_EXPOSURE_STEP,
					     IMX858_EXPOSURE_DEFAULT);

	imx858->again_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					       &imx858_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN,
					       IMX858_AGAIN_MIN,
					       IMX858_AGAIN_MAX,
					       IMX858_AGAIN_STEP,
					       IMX858_AGAIN_DEFAULT);

	v4l2_ctrl_cluster(2, &imx858->exp_ctrl);

	imx858->vblank_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
						&imx858_ctrl_ops,
						V4L2_CID_VBLANK,
						mode->vblank_min,
						mode->vblank_max,
						1, mode->vblank);

	/* Read only controls */
	imx858->pclk_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					      &imx858_ctrl_ops,
					      V4L2_CID_PIXEL_RATE,
					      mode->pclk, mode->pclk,
					      1, mode->pclk);

	imx858->link_freq_ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr,
							&imx858_ctrl_ops,
							V4L2_CID_LINK_FREQ,
							ARRAY_SIZE(link_freq) -
							1,
							mode->link_freq_idx,
							link_freq);
	if (imx858->link_freq_ctrl)
		imx858->link_freq_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx858->hblank_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
						&imx858_ctrl_ops,
						V4L2_CID_HBLANK,
						IMX858_REG_MIN,
						IMX858_REG_MAX,
						1, mode->hblank);
	if (imx858->hblank_ctrl)
		imx858->hblank_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx858_ctrl_ops, &props);

	if (ctrl_hdlr->error) {
		dev_err(imx858->dev, "control init failed: %d\n",
			ctrl_hdlr->error);
		v4l2_ctrl_handler_free(ctrl_hdlr);
		return ctrl_hdlr->error;
	}

	imx858->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

/**
 * imx858_probe() - I2C client device binding
 * @client: pointer to i2c client device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx858_probe(struct i2c_client *client)
{
	struct imx858 *imx858;
	int ret;

	imx858 = devm_kzalloc(&client->dev, sizeof(*imx858), GFP_KERNEL);
	if (!imx858)
		return -ENOMEM;

	imx858->dev = &client->dev;

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&imx858->sd, client, &imx858_subdev_ops);
	imx858->sd.internal_ops = &imx858_internal_ops;

	ret = imx858_parse_hw_config(imx858);
	if (ret) {
		dev_err(imx858->dev, "HW configuration is not supported\n");
		return ret;
	}

	mutex_init(&imx858->mutex);

	ret = imx858_power_on(imx858->dev);
	if (ret) {
		dev_err(imx858->dev, "failed to power-on the sensor\n");
		goto error_mutex_destroy;
	}

	/* Check module identity */
	ret = imx858_detect(imx858);
	if (ret) {
		dev_err(imx858->dev, "failed to find sensor: %d\n", ret);
		goto error_power_off;
	}

	/* Set default mode to max resolution */
	imx858->cur_mode = &supported_mode;
	imx858->vblank = imx858->cur_mode->vblank;

	ret = imx858_init_controls(imx858);
	if (ret) {
		dev_err(imx858->dev, "failed to init controls: %d\n", ret);
		goto error_power_off;
	}

	/* Initialize subdev */
	imx858->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx858->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	imx858->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&imx858->sd.entity, 1, &imx858->pad);
	if (ret) {
		dev_err(imx858->dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&imx858->sd);
	if (ret < 0) {
		dev_err(imx858->dev,
			"failed to register async subdev: %d\n", ret);
		goto error_media_entity;
	}

	pm_runtime_set_active(imx858->dev);
	pm_runtime_enable(imx858->dev);
	pm_runtime_idle(imx858->dev);

	return 0;

error_media_entity:
	media_entity_cleanup(&imx858->sd.entity);
error_handler_free:
	v4l2_ctrl_handler_free(imx858->sd.ctrl_handler);
error_power_off:
	imx858_power_off(imx858->dev);
error_mutex_destroy:
	mutex_destroy(&imx858->mutex);

	return ret;
}

static void imx858_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx858 *imx858 = to_imx858(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx858_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	mutex_destroy(&imx858->mutex);
}

static const struct dev_pm_ops imx858_pm_ops = {
	SET_RUNTIME_PM_OPS(imx858_power_off, imx858_power_on, NULL)
};

static const struct of_device_id imx858_of_match[] = {
	{ .compatible = "sony,imx858" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx858_of_match);

static struct i2c_driver imx858_driver = {
	.probe = imx858_probe,
	.remove = imx858_remove,
	.driver = {
		.name = "imx858",
		.pm = &imx858_pm_ops,
		.of_match_table = imx858_of_match,
	},
};

module_i2c_driver(imx858_driver);

MODULE_DESCRIPTION("Sony IMX858 sensor driver");
MODULE_LICENSE("GPL");
