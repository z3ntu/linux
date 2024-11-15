// SPDX-License-Identifier: GPL-2.0-only
/*
 * A V4L2 driver for Sony IMX800 cameras.
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
#define IMX800_REG_MODE_SELECT	0x0100
#define IMX800_MODE_STANDBY	0x00
#define IMX800_MODE_STREAMING	0x01

/* Lines per frame */
#define IMX800_REG_LPFR		0x0340

/* Chip ID */
#define IMX800_REG_ID		0x0016
#define IMX800_ID		0x800

/* Exposure control */
#define IMX800_REG_EXPOSURE_CIT	0x0202
#define IMX800_EXPOSURE_MIN	8
#define IMX800_EXPOSURE_OFFSET	22
#define IMX800_EXPOSURE_STEP	1
#define IMX800_EXPOSURE_DEFAULT	0x0648

/* Analog gain control */
#define IMX800_REG_AGAIN	0x0204
#define IMX800_AGAIN_MIN	0
#define IMX800_AGAIN_MAX	978
#define IMX800_AGAIN_STEP	1
#define IMX800_AGAIN_DEFAULT	0

/* Group hold register */
#define IMX800_REG_HOLD		0x0104

/* Input clock rate */
#define IMX800_INCLK_RATE	24000000

/* CSI2 HW configuration */
#define IMX800_LINK_FREQ	600000000
#define IMX800_NUM_DATA_LANES	3 // how many data lanes does 3+3+3 pin C-PHY have?

#define IMX800_REG_MIN		0x00
#define IMX800_REG_MAX		0xffff

/**
 * struct imx800_reg - imx800 sensor register
 * @address: Register address
 * @val: Register value
 */
struct imx800_reg {
	u16 address;
	u8 val;
};

/**
 * struct imx800_reg_list - imx800 sensor register list
 * @num_of_regs: Number of registers in the list
 * @regs: Pointer to register list
 */
struct imx800_reg_list {
	u32 num_of_regs;
	const struct imx800_reg *regs;
};

/**
 * struct imx800_mode - imx800 sensor mode structure
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
struct imx800_mode {
	u32 width;
	u32 height;
	u32 code;
	u32 hblank;
	u32 vblank;
	u32 vblank_min;
	u32 vblank_max;
	u64 pclk;
	u32 link_freq_idx;
	struct imx800_reg_list reg_list;
};

static const char * const imx800_supply_names[] = {
	"iovdd",	/* Digital I/O power */
	"avdd",		/* Analog power */
	"dvdd",		/* Digital core power */
};

/**
 * struct imx800 - imx800 sensor device structure
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
struct imx800 {
	struct device *dev;
	struct i2c_client *client;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct gpio_desc *reset_gpio;
	struct clk *inclk;
	struct regulator_bulk_data supplies[ARRAY_SIZE(imx800_supply_names)];
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
	const struct imx800_mode *cur_mode;
	struct mutex mutex;
};

static const s64 link_freq[] = {
	IMX800_LINK_FREQ,
};

/* Sensor mode registers */
static const struct imx800_reg mode_4096x3072_regs[] = {
	// common registers
	{ 0x0136, 0x18 },
	{ 0x0137, 0x00 },
	{ 0x3304, 0x00 },
	{ 0x33f0, 0x01 },
	{ 0x33f1, 0x06 },
	{ 0x0111, 0x03 },
	{ 0x39d1, 0x00 },
	{ 0x7bba, 0x01 },
	{ 0x7d6e, 0x01 },
	{ 0x9230, 0xbd },
	{ 0x9231, 0x07 },
	{ 0x9232, 0xbd },
	{ 0x9233, 0x01 },
	{ 0x9234, 0xbd },
	{ 0x9235, 0x02 },
	{ 0x979f, 0x00 },
	{ 0x97ab, 0x01 },
	{ 0x97c1, 0x04 },
	{ 0x97c2, 0x02 },
	{ 0x98cc, 0x0a },
	{ 0x98ee, 0x3c },
	{ 0x98f1, 0x3c },
	{ 0xcb89, 0x2d },
	{ 0xcb8f, 0x2d },
	{ 0xcbab, 0x23 },
	{ 0xcbad, 0x23 },
	{ 0xcbb1, 0x23 },
	{ 0xcbb3, 0x23 },
	{ 0xcc71, 0x2d },
	{ 0xcc77, 0x2d },
	{ 0xcc93, 0x23 },
	{ 0xcc95, 0x23 },
	{ 0xcc99, 0x23 },
	{ 0xcc9b, 0x23 },
	{ 0xcd71, 0x51 },
	{ 0xcd77, 0x51 },
	{ 0xcd7d, 0x51 },
	{ 0xcd83, 0x51 },
	{ 0xd4d5, 0x19 },
	{ 0xd4d6, 0x19 },
	{ 0xd4d7, 0x19 },
	{ 0xd4d8, 0x19 },
	{ 0xd4d9, 0x19 },
	{ 0xd4ee, 0x1a },
	{ 0xd4ef, 0x1a },
	{ 0xd4f0, 0x1a },
	{ 0xd4f1, 0x1a },
	{ 0xd4f2, 0x1a },
	{ 0xd566, 0x1a },
	{ 0xd567, 0x1a },
	{ 0xd568, 0x1a },
	{ 0xd569, 0x1a },
	{ 0xd56a, 0x1a },
	{ 0xd589, 0x19 },
	{ 0xd58a, 0x19 },
	{ 0xd58b, 0x19 },
	{ 0xd58c, 0x19 },
	{ 0xd58d, 0x19 },
	{ 0xd855, 0xff },
	{ 0xd857, 0xff },
	{ 0xd859, 0xff },
	{ 0xe15e, 0x0a },
	{ 0xe15f, 0x05 },
	{ 0xe161, 0x0a },
	{ 0xe162, 0x05 },
	{ 0xe164, 0x0a },
	{ 0xe165, 0x05 },
	{ 0xe16d, 0x05 },
	{ 0xe16e, 0x05 },
	{ 0xe170, 0x05 },
	{ 0xe171, 0x05 },
	{ 0xe173, 0x05 },
	{ 0xe174, 0x05 },
	{ 0xe17c, 0x0a },
	{ 0xe17d, 0x05 },
	{ 0xe17f, 0x0a },
	{ 0xe180, 0x05 },
	{ 0xe182, 0x0a },
	{ 0xe183, 0x05 },
	{ 0xe19a, 0x74 },
	{ 0xe19b, 0x74 },
	{ 0xe19d, 0x74 },
	{ 0xe19e, 0x74 },
	{ 0xe1a0, 0x74 },
	{ 0xe1a1, 0x74 },
	{ 0xe1a9, 0x0a },
	{ 0xe1aa, 0x05 },
	{ 0xe1ac, 0x0a },
	{ 0xe1ad, 0x05 },
	{ 0xe1af, 0x0a },
	{ 0xe1b0, 0x05 },
	{ 0xe1b8, 0x05 },
	{ 0xe1b9, 0x05 },
	{ 0xe1bb, 0x05 },
	{ 0xe1bc, 0x05 },
	{ 0xe1be, 0x05 },
	{ 0xe1bf, 0x05 },
	{ 0xe1c7, 0x0a },
	{ 0xe1c8, 0x05 },
	{ 0xe1ca, 0x0a },
	{ 0xe1cb, 0x05 },
	{ 0xe1cd, 0x0a },
	{ 0xe1ce, 0x05 },
	{ 0xe1e5, 0x74 },
	{ 0xe1e6, 0x74 },
	{ 0xe1e8, 0x74 },
	{ 0xe1e9, 0x74 },
	{ 0xe1eb, 0x74 },
	{ 0xe1ec, 0x74 },
	{ 0xe57c, 0x0a },
	{ 0xe57d, 0x05 },
	{ 0xe57f, 0x0a },
	{ 0xe580, 0x05 },
	{ 0xe582, 0x0a },
	{ 0xe583, 0x05 },
	{ 0xe58b, 0x05 },
	{ 0xe58c, 0x05 },
	{ 0xe58e, 0x05 },
	{ 0xe58f, 0x05 },
	{ 0xe591, 0x05 },
	{ 0xe592, 0x05 },
	{ 0xe59a, 0x0a },
	{ 0xe59b, 0x05 },
	{ 0xe59d, 0x0a },
	{ 0xe59e, 0x05 },
	{ 0xe5a0, 0x0a },
	{ 0xe5a1, 0x05 },
	{ 0xe5b8, 0x0a },
	{ 0xe5b9, 0x05 },
	{ 0xe5bb, 0x0a },
	{ 0xe5bc, 0x05 },
	{ 0xe5be, 0x0a },
	{ 0xe5bf, 0x05 },
	{ 0xe5c7, 0x05 },
	{ 0xe5c8, 0x05 },
	{ 0xe5ca, 0x05 },
	{ 0xe5cb, 0x05 },
	{ 0xe5cd, 0x05 },
	{ 0xe5ce, 0x05 },
	{ 0xe5d6, 0x0a },
	{ 0xe5d7, 0x05 },
	{ 0xe5d9, 0x0a },
	{ 0xe5da, 0x05 },
	{ 0xe5dc, 0x0a },
	{ 0xe5dd, 0x05 },
	{ 0xe622, 0x74 },
	{ 0xe623, 0x74 },
	{ 0xe625, 0x74 },
	{ 0xe626, 0x74 },
	{ 0xe628, 0x74 },
	{ 0xe629, 0x74 },
	{ 0xe631, 0x74 },
	{ 0xe632, 0x74 },
	{ 0xe634, 0x74 },
	{ 0xe635, 0x74 },
	{ 0xe637, 0x74 },
	{ 0xe638, 0x74 },
	{ 0xf01e, 0x02 },
	{ 0xf01f, 0xbc },
	{ 0xf020, 0x02 },
	{ 0xf021, 0xbc },
	{ 0xf022, 0x02 },
	{ 0xf023, 0xbc },
	{ 0xf112, 0x00 },
	{ 0xf501, 0x01 },
	{ 0x0101, 0x03 },

	// res1 4096*3072@30fps (4:3) 2x2
	{ 0x0112, 0x0a },
	{ 0x0113, 0x0a },
	{ 0x0114, 0x02 },
	{ 0x321c, 0x00 },
	{ 0x0342, 0x26 },
	{ 0x0343, 0x30 },
	{ 0x0340, 0x1d },
	{ 0x0341, 0x3c },
	{ 0x0344, 0x01 },
	{ 0x0345, 0x20 },
	{ 0x0346, 0x00 },
	{ 0x0347, 0x00 },
	{ 0x0348, 0x21 },
	{ 0x0349, 0x1f },
	{ 0x034a, 0x17 },
	{ 0x034b, 0xff },
	{ 0x0900, 0x01 },
	{ 0x0901, 0x22 },
	{ 0x0902, 0x08 },
	{ 0x3005, 0x02 },
	{ 0x31a8, 0x04 },
	{ 0x31a9, 0x01 },
	{ 0x31d0, 0x41 },
	{ 0x31d1, 0x41 },
	{ 0x320b, 0x01 },
	{ 0x350d, 0x00 },
	{ 0x0408, 0x00 },
	{ 0x0409, 0x00 },
	{ 0x040a, 0x00 },
	{ 0x040b, 0x00 },
	{ 0x040c, 0x10 },
	{ 0x040d, 0x00 },
	{ 0x040e, 0x0c },
	{ 0x040f, 0x00 },
	{ 0x034c, 0x10 },
	{ 0x034d, 0x00 },
	{ 0x034e, 0x0c },
	{ 0x034f, 0x00 },
	{ 0x0301, 0x08 },
	{ 0x0303, 0x02 },
	{ 0x0305, 0x04 },
	{ 0x0306, 0x01 },
	{ 0x0307, 0x6e },
	{ 0x030b, 0x02 },
	{ 0x030d, 0x04 },
	{ 0x030e, 0x02 },
	{ 0x030f, 0x92 },
	{ 0x3205, 0x00 },
	{ 0x3206, 0x00 },
	{ 0x3213, 0x01 },
	{ 0x3818, 0x3c },
	{ 0x3819, 0x03 },
	{ 0x381a, 0xa9 },
	{ 0x381b, 0x01 },
	{ 0x381c, 0x02 },
	{ 0x381d, 0x04 },
	{ 0x381e, 0x01 },
	{ 0x381f, 0x01 },
	{ 0x3890, 0x00 },
	{ 0x3891, 0x00 },
	{ 0x3894, 0x00 },
	{ 0x3895, 0x00 },
	{ 0x3896, 0x00 },
	{ 0x3897, 0x00 },
	{ 0x389a, 0x00 },
	{ 0x389b, 0x00 },
	{ 0x389c, 0x00 },
	{ 0x389d, 0x00 },
	{ 0x389e, 0x00 },
	{ 0x389f, 0x00 },
	{ 0x38a0, 0x00 },
	{ 0x38a1, 0x00 },
	{ 0x38a2, 0x00 },
	{ 0x38a3, 0x00 },
	{ 0x38a4, 0x00 },
	{ 0x38a5, 0x00 },
	{ 0x38a6, 0x00 },
	{ 0x38a7, 0x00 },
	{ 0x38b8, 0x00 },
	{ 0x38b9, 0x00 },
	{ 0x38ba, 0x00 },
	{ 0x38bb, 0x00 },
	{ 0x38d0, 0x00 },
	{ 0x38d1, 0x00 },
	{ 0x38d2, 0x00 },
	{ 0x38d3, 0x00 },
	{ 0x38d6, 0x00 },
	{ 0x38d7, 0x00 },
	{ 0x38d8, 0x00 },
	{ 0x38d9, 0x00 },
	{ 0x38da, 0x00 },
	{ 0x38db, 0x00 },
	{ 0x38dc, 0x00 },
	{ 0x38dd, 0x00 },
	{ 0x38e8, 0x00 },
	{ 0x38e9, 0x00 },
	{ 0x0202, 0x1d },
	{ 0x0203, 0x0c },
	{ 0x0224, 0x01 },
	{ 0x0225, 0xf4 },
	{ 0x3162, 0x01 },
	{ 0x3163, 0xf4 },
	{ 0x3168, 0x01 },
	{ 0x3169, 0xf4 },
	{ 0x0204, 0x13 },
	{ 0x0205, 0x34 },
	{ 0x020e, 0x01 },
	{ 0x020f, 0x00 },
	{ 0x0216, 0x13 },
	{ 0x0217, 0x34 },
	{ 0x0218, 0x01 },
	{ 0x0219, 0x00 },
	{ 0x3164, 0x13 },
	{ 0x3165, 0x34 },
	{ 0x3166, 0x01 },
	{ 0x3167, 0x00 },
	{ 0x316a, 0x13 },
	{ 0x316b, 0x34 },
	{ 0x316c, 0x01 },
	{ 0x316d, 0x00 },
	{ 0x3104, 0x01 },
	{ 0x3103, 0x00 },
	{ 0x3474, 0x04 },
	{ 0x3475, 0x40 },
	{ 0x3170, 0x00 },
	{ 0x3171, 0x00 },
	{ 0x317e, 0x0a },
	{ 0x317f, 0x0a },
	{ 0x3180, 0x0a },
	{ 0x3181, 0x0a },
	{ 0x3182, 0x0a },
	{ 0x3183, 0x0a },
	{ 0x39d0, 0x00 },
};

/* Supported sensor mode configurations */
static const struct imx800_mode supported_mode = {
	.width = 4096,
	.height = 3072,
	.hblank = 456, // FIXME
	.vblank = 506, // FIXME
	.vblank_min = 506, // FIXME
	.vblank_max = 32420, // FIXME
	.pclk = 619200000, // outputPixelClock?
	.link_freq_idx = 0,
	.code = MEDIA_BUS_FMT_SRGGB10_1X10,
	.reg_list = {
		.num_of_regs = ARRAY_SIZE(mode_4096x3072_regs),
		.regs = mode_4096x3072_regs,
	},
};

/**
 * to_imx800() - imx800 V4L2 sub-device to imx800 device.
 * @subdev: pointer to imx800 V4L2 sub-device
 *
 * Return: pointer to imx800 device
 */
static inline struct imx800 *to_imx800(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct imx800, sd);
}

/**
 * imx800_read_reg() - Read registers.
 * @imx800: pointer to imx800 device
 * @reg: register address
 * @len: length of bytes to read. Max supported bytes is 4
 * @val: pointer to register value to be filled.
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx800_read_reg(struct imx800 *imx800, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx800->sd);
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

	printk(KERN_ERR "%s:%d DBG reg=0x%x len=%d data_buf[0]=0x%x data_buf[1]=0x%x data_buf[2]=0x%x data_buf[3]=0x%x val=0x%x\n", __func__, __LINE__, reg, len, data_buf[0], data_buf[1], data_buf[2], data_buf[3], *val);

	return 0;
}

/**
 * imx800_write_reg() - Write register
 * @imx800: pointer to imx800 device
 * @reg: register address
 * @len: length of bytes. Max supported bytes is 4
 * @val: register value
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx800_write_reg(struct imx800 *imx800, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx800->sd);
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
 * imx800_write_regs() - Write a list of registers
 * @imx800: pointer to imx800 device
 * @regs: list of registers to be written
 * @len: length of registers array
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx800_write_regs(struct imx800 *imx800,
			     const struct imx800_reg *regs, u32 len)
{
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = imx800_write_reg(imx800, regs[i].address, 1, regs[i].val);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * imx800_update_controls() - Update control ranges based on streaming mode
 * @imx800: pointer to imx800 device
 * @mode: pointer to imx800_mode sensor mode
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx800_update_controls(struct imx800 *imx800,
				  const struct imx800_mode *mode)
{
	int ret;

	ret = __v4l2_ctrl_s_ctrl(imx800->link_freq_ctrl, mode->link_freq_idx);
	if (ret)
		return ret;

	ret = __v4l2_ctrl_s_ctrl(imx800->hblank_ctrl, mode->hblank);
	if (ret)
		return ret;

	return __v4l2_ctrl_modify_range(imx800->vblank_ctrl, mode->vblank_min,
					mode->vblank_max, 1, mode->vblank);
}

/**
 * imx800_update_exp_gain() - Set updated exposure and gain
 * @imx800: pointer to imx800 device
 * @exposure: updated exposure value
 * @gain: updated analog gain value
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx800_update_exp_gain(struct imx800 *imx800, u32 exposure, u32 gain)
{
	u32 lpfr;
	int ret;

	lpfr = imx800->vblank + imx800->cur_mode->height;

	dev_dbg(imx800->dev, "Set exp %u, analog gain %u, lpfr %u\n",
		exposure, gain, lpfr);

	ret = imx800_write_reg(imx800, IMX800_REG_HOLD, 1, 1);
	if (ret)
		return ret;

	ret = imx800_write_reg(imx800, IMX800_REG_LPFR, 2, lpfr);
	if (ret)
		goto error_release_group_hold;

	ret = imx800_write_reg(imx800, IMX800_REG_EXPOSURE_CIT, 2, exposure);
	if (ret)
		goto error_release_group_hold;

	ret = imx800_write_reg(imx800, IMX800_REG_AGAIN, 2, gain);

error_release_group_hold:
	imx800_write_reg(imx800, IMX800_REG_HOLD, 1, 0);

	return ret;
}

/**
 * imx800_set_ctrl() - Set subdevice control
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
static int imx800_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx800 *imx800 =
		container_of(ctrl->handler, struct imx800, ctrl_handler);
	u32 analog_gain;
	u32 exposure;
	int ret;

	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		imx800->vblank = imx800->vblank_ctrl->val;

		dev_dbg(imx800->dev, "Received vblank %u, new lpfr %u\n",
			imx800->vblank,
			imx800->vblank + imx800->cur_mode->height);

		ret = __v4l2_ctrl_modify_range(imx800->exp_ctrl,
					       IMX800_EXPOSURE_MIN,
					       imx800->vblank +
					       imx800->cur_mode->height -
					       IMX800_EXPOSURE_OFFSET,
					       1, IMX800_EXPOSURE_DEFAULT);
		break;
	case V4L2_CID_EXPOSURE:
		/* Set controls only if sensor is in power on state */
		if (!pm_runtime_get_if_in_use(imx800->dev))
			return 0;

		exposure = ctrl->val;
		analog_gain = imx800->again_ctrl->val;

		dev_dbg(imx800->dev, "Received exp %u, analog gain %u\n",
			exposure, analog_gain);

		ret = imx800_update_exp_gain(imx800, exposure, analog_gain);

		pm_runtime_put(imx800->dev);

		break;
	default:
		dev_err(imx800->dev, "Invalid control %d\n", ctrl->id);
		ret = -EINVAL;
	}

	return ret;
}

/* V4l2 subdevice control ops*/
static const struct v4l2_ctrl_ops imx800_ctrl_ops = {
	.s_ctrl = imx800_set_ctrl,
};

/**
 * imx800_enum_mbus_code() - Enumerate V4L2 sub-device mbus codes
 * @sd: pointer to imx800 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @code: V4L2 sub-device code enumeration need to be filled
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx800_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = supported_mode.code;

	return 0;
}

/**
 * imx800_enum_frame_size() - Enumerate V4L2 sub-device frame sizes
 * @sd: pointer to imx800 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fsize: V4L2 sub-device size enumeration need to be filled
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx800_enum_frame_size(struct v4l2_subdev *sd,
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
 * imx800_fill_pad_format() - Fill subdevice pad format
 *                            from selected sensor mode
 * @imx800: pointer to imx800 device
 * @mode: pointer to imx800_mode sensor mode
 * @fmt: V4L2 sub-device format need to be filled
 */
static void imx800_fill_pad_format(struct imx800 *imx800,
				   const struct imx800_mode *mode,
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
 * imx800_get_pad_format() - Get subdevice pad format
 * @sd: pointer to imx800 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fmt: V4L2 sub-device format need to be set
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx800_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx800 *imx800 = to_imx800(sd);

	mutex_lock(&imx800->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *framefmt;

		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		fmt->format = *framefmt;
	} else {
		imx800_fill_pad_format(imx800, imx800->cur_mode, fmt);
	}

	mutex_unlock(&imx800->mutex);

	return 0;
}

/**
 * imx800_set_pad_format() - Set subdevice pad format
 * @sd: pointer to imx800 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fmt: V4L2 sub-device format need to be set
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx800_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx800 *imx800 = to_imx800(sd);
	const struct imx800_mode *mode;
	int ret = 0;

	mutex_lock(&imx800->mutex);

	mode = &supported_mode;
	imx800_fill_pad_format(imx800, mode, fmt);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *framefmt;

		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		*framefmt = fmt->format;
	} else {
		ret = imx800_update_controls(imx800, mode);
		if (!ret)
			imx800->cur_mode = mode;
	}

	mutex_unlock(&imx800->mutex);

	return ret;
}

/**
 * imx800_init_state() - Initialize sub-device state
 * @sd: pointer to imx800 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx800_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	struct imx800 *imx800 = to_imx800(sd);
	struct v4l2_subdev_format fmt = { 0 };

	fmt.which = sd_state ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	imx800_fill_pad_format(imx800, &supported_mode, &fmt);

	return imx800_set_pad_format(sd, sd_state, &fmt);
}

/**
 * imx800_start_streaming() - Start sensor stream
 * @imx800: pointer to imx800 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx800_start_streaming(struct imx800 *imx800)
{
	const struct imx800_reg_list *reg_list;
	int ret;

	/* Write sensor mode registers */
	reg_list = &imx800->cur_mode->reg_list;
	ret = imx800_write_regs(imx800, reg_list->regs,
				reg_list->num_of_regs);
	if (ret) {
		dev_err(imx800->dev, "fail to write initial registers\n");
		return ret;
	}

	/* Setup handler will write actual exposure and gain */
	ret =  __v4l2_ctrl_handler_setup(imx800->sd.ctrl_handler);
	if (ret) {
		dev_err(imx800->dev, "fail to setup handler\n");
		return ret;
	}

	/* Delay is required before streaming*/
	usleep_range(7400, 8000);

	/* Start streaming */
	ret = imx800_write_reg(imx800, IMX800_REG_MODE_SELECT,
			       1, IMX800_MODE_STREAMING);
	if (ret) {
		dev_err(imx800->dev, "fail to start streaming\n");
		return ret;
	}

	return 0;
}

/**
 * imx800_stop_streaming() - Stop sensor stream
 * @imx800: pointer to imx800 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx800_stop_streaming(struct imx800 *imx800)
{
	return imx800_write_reg(imx800, IMX800_REG_MODE_SELECT,
				1, IMX800_MODE_STANDBY);
}

/**
 * imx800_set_stream() - Enable sensor streaming
 * @sd: pointer to imx800 subdevice
 * @enable: set to enable sensor streaming
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx800_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx800 *imx800 = to_imx800(sd);
	int ret;

	mutex_lock(&imx800->mutex);

	if (enable) {
		ret = pm_runtime_resume_and_get(imx800->dev);
		if (ret)
			goto error_unlock;

		ret = imx800_start_streaming(imx800);
		if (ret)
			goto error_power_off;
	} else {
		imx800_stop_streaming(imx800);
		pm_runtime_put(imx800->dev);
	}

	mutex_unlock(&imx800->mutex);

	return 0;

error_power_off:
	pm_runtime_put(imx800->dev);
error_unlock:
	mutex_unlock(&imx800->mutex);

	return ret;
}

/**
 * imx800_detect() - Detect imx800 sensor
 * @imx800: pointer to imx800 device
 *
 * Return: 0 if successful, -EIO if sensor id does not match
 */
static int imx800_detect(struct imx800 *imx800)
{
	int ret;
	u32 val;

	ret = imx800_read_reg(imx800, IMX800_REG_ID, 2, &val);
	if (ret)
		return ret;

	if (val != IMX800_ID) {
		dev_err(imx800->dev, "chip id mismatch: %x!=%x\n",
			IMX800_ID, val);
		return -ENXIO;
	}

	return 0;
}

/**
 * imx800_parse_hw_config() - Parse HW configuration and check if supported
 * @imx800: pointer to imx800 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx800_parse_hw_config(struct imx800 *imx800)
{
	struct fwnode_handle *fwnode = dev_fwnode(imx800->dev);
	struct v4l2_fwnode_endpoint bus_cfg = {};
	struct fwnode_handle *ep;
	unsigned long rate;
	unsigned int i;
	int ret;

	if (!fwnode)
		return -ENXIO;

	/* Request optional reset pin */
	imx800->reset_gpio = devm_gpiod_get_optional(imx800->dev, "reset",
						     GPIOD_OUT_LOW);
	if (IS_ERR(imx800->reset_gpio)) {
		dev_err(imx800->dev, "failed to get reset gpio %ld\n",
			PTR_ERR(imx800->reset_gpio));
		return PTR_ERR(imx800->reset_gpio);
	}

	/* Get sensor input clock */
	imx800->inclk = devm_clk_get(imx800->dev, NULL);
	if (IS_ERR(imx800->inclk)) {
		dev_err(imx800->dev, "could not get inclk\n");
		return PTR_ERR(imx800->inclk);
	}

	rate = clk_get_rate(imx800->inclk);
	if (rate != IMX800_INCLK_RATE) {
		dev_err(imx800->dev, "inclk frequency mismatch\n");
		return -EINVAL;
	}

	/* Get optional DT defined regulators */
	for (i = 0; i < ARRAY_SIZE(imx800_supply_names); i++)
		imx800->supplies[i].supply = imx800_supply_names[i];

	ret = devm_regulator_bulk_get(imx800->dev,
				      ARRAY_SIZE(imx800_supply_names),
				      imx800->supplies);
	if (ret)
		return ret;

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -ENXIO;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	if (bus_cfg.bus_type != V4L2_MBUS_CSI2_CPHY) {
		dev_err(imx800->dev, "selected bus-type is not supported\n");
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != IMX800_NUM_DATA_LANES) {
		dev_err(imx800->dev,
			"number of CSI2 data lanes %d is not supported\n",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	if (!bus_cfg.nr_of_link_frequencies) {
		dev_err(imx800->dev, "no link frequencies defined\n");
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	for (i = 0; i < bus_cfg.nr_of_link_frequencies; i++)
		if (bus_cfg.link_frequencies[i] == IMX800_LINK_FREQ)
			goto done_endpoint_free;

	ret = -EINVAL;

done_endpoint_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

/* V4l2 subdevice ops */
static const struct v4l2_subdev_video_ops imx800_video_ops = {
	.s_stream = imx800_set_stream,
};

static const struct v4l2_subdev_pad_ops imx800_pad_ops = {
	.enum_mbus_code = imx800_enum_mbus_code,
	.enum_frame_size = imx800_enum_frame_size,
	.get_fmt = imx800_get_pad_format,
	.set_fmt = imx800_set_pad_format,
};

static const struct v4l2_subdev_ops imx800_subdev_ops = {
	.video = &imx800_video_ops,
	.pad = &imx800_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx800_internal_ops = {
	.init_state = imx800_init_state,
};

/**
 * imx800_power_on() - Sensor power on sequence
 * @dev: pointer to i2c device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx800_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx800 *imx800 = to_imx800(sd);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(imx800_supply_names),
				    imx800->supplies);
	if (ret < 0) {
		dev_err(dev, "failed to enable regulators\n");
		return ret;
	}

	gpiod_set_value_cansleep(imx800->reset_gpio, 0);

	ret = clk_prepare_enable(imx800->inclk);
	if (ret) {
		dev_err(imx800->dev, "fail to enable inclk\n");
		goto error_reset;
	}

	usleep_range(1000, 1200);

	return 0;

error_reset:
	gpiod_set_value_cansleep(imx800->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(imx800_supply_names),
			       imx800->supplies);

	return ret;
}

/**
 * imx800_power_off() - Sensor power off sequence
 * @dev: pointer to i2c device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx800_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx800 *imx800 = to_imx800(sd);

	clk_disable_unprepare(imx800->inclk);

	gpiod_set_value_cansleep(imx800->reset_gpio, 1);

	regulator_bulk_disable(ARRAY_SIZE(imx800_supply_names),
			       imx800->supplies);

	return 0;
}

/**
 * imx800_init_controls() - Initialize sensor subdevice controls
 * @imx800: pointer to imx800 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx800_init_controls(struct imx800 *imx800)
{
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr = &imx800->ctrl_handler;
	const struct imx800_mode *mode = imx800->cur_mode;
	u32 lpfr;
	int ret;

	/* set properties from fwnode (e.g. rotation, orientation) */
	ret = v4l2_fwnode_device_parse(imx800->dev, &props);
	if (ret)
		return ret;

	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 8);
	if (ret)
		return ret;

	/* Serialize controls with sensor device */
	ctrl_hdlr->lock = &imx800->mutex;

	/* Initialize exposure and gain */
	lpfr = mode->vblank + mode->height;
	imx800->exp_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					     &imx800_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX800_EXPOSURE_MIN,
					     lpfr - IMX800_EXPOSURE_OFFSET,
					     IMX800_EXPOSURE_STEP,
					     IMX800_EXPOSURE_DEFAULT);

	imx800->again_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					       &imx800_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN,
					       IMX800_AGAIN_MIN,
					       IMX800_AGAIN_MAX,
					       IMX800_AGAIN_STEP,
					       IMX800_AGAIN_DEFAULT);

	v4l2_ctrl_cluster(2, &imx800->exp_ctrl);

	imx800->vblank_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
						&imx800_ctrl_ops,
						V4L2_CID_VBLANK,
						mode->vblank_min,
						mode->vblank_max,
						1, mode->vblank);

	/* Read only controls */
	imx800->pclk_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					      &imx800_ctrl_ops,
					      V4L2_CID_PIXEL_RATE,
					      mode->pclk, mode->pclk,
					      1, mode->pclk);

	imx800->link_freq_ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr,
							&imx800_ctrl_ops,
							V4L2_CID_LINK_FREQ,
							ARRAY_SIZE(link_freq) -
							1,
							mode->link_freq_idx,
							link_freq);
	if (imx800->link_freq_ctrl)
		imx800->link_freq_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx800->hblank_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
						&imx800_ctrl_ops,
						V4L2_CID_HBLANK,
						IMX800_REG_MIN,
						IMX800_REG_MAX,
						1, mode->hblank);
	if (imx800->hblank_ctrl)
		imx800->hblank_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx800_ctrl_ops, &props);

	if (ctrl_hdlr->error) {
		dev_err(imx800->dev, "control init failed: %d\n",
			ctrl_hdlr->error);
		v4l2_ctrl_handler_free(ctrl_hdlr);
		return ctrl_hdlr->error;
	}

	imx800->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

/**
 * imx800_probe() - I2C client device binding
 * @client: pointer to i2c client device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx800_probe(struct i2c_client *client)
{
	struct imx800 *imx800;
	int ret;

	imx800 = devm_kzalloc(&client->dev, sizeof(*imx800), GFP_KERNEL);
	if (!imx800)
		return -ENOMEM;

	imx800->dev = &client->dev;

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&imx800->sd, client, &imx800_subdev_ops);
	imx800->sd.internal_ops = &imx800_internal_ops;

	ret = imx800_parse_hw_config(imx800);
	if (ret) {
		dev_err(imx800->dev, "HW configuration is not supported\n");
		return ret;
	}

	mutex_init(&imx800->mutex);

	ret = imx800_power_on(imx800->dev);
	if (ret) {
		dev_err(imx800->dev, "failed to power-on the sensor\n");
		goto error_mutex_destroy;
	}

	/* Check module identity */
	ret = imx800_detect(imx800);
	if (ret) {
		dev_err(imx800->dev, "failed to find sensor: %d\n", ret);
		goto error_power_off;
	}

	/* Set default mode to max resolution */
	imx800->cur_mode = &supported_mode;
	imx800->vblank = imx800->cur_mode->vblank;

	ret = imx800_init_controls(imx800);
	if (ret) {
		dev_err(imx800->dev, "failed to init controls: %d\n", ret);
		goto error_power_off;
	}

	/* Initialize subdev */
	imx800->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx800->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	imx800->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&imx800->sd.entity, 1, &imx800->pad);
	if (ret) {
		dev_err(imx800->dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&imx800->sd);
	if (ret < 0) {
		dev_err(imx800->dev,
			"failed to register async subdev: %d\n", ret);
		goto error_media_entity;
	}

	pm_runtime_set_active(imx800->dev);
	pm_runtime_enable(imx800->dev);
	pm_runtime_idle(imx800->dev);

	return 0;

error_media_entity:
	media_entity_cleanup(&imx800->sd.entity);
error_handler_free:
	v4l2_ctrl_handler_free(imx800->sd.ctrl_handler);
error_power_off:
	imx800_power_off(imx800->dev);
error_mutex_destroy:
	mutex_destroy(&imx800->mutex);

	return ret;
}

static void imx800_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx800 *imx800 = to_imx800(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx800_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	mutex_destroy(&imx800->mutex);
}

static const struct dev_pm_ops imx800_pm_ops = {
	SET_RUNTIME_PM_OPS(imx800_power_off, imx800_power_on, NULL)
};

static const struct of_device_id imx800_of_match[] = {
	{ .compatible = "sony,imx800" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx800_of_match);

static struct i2c_driver imx800_driver = {
	.probe = imx800_probe,
	.remove = imx800_remove,
	.driver = {
		.name = "imx800",
		.pm = &imx800_pm_ops,
		.of_match_table = imx800_of_match,
	},
};

module_i2c_driver(imx800_driver);

MODULE_DESCRIPTION("Sony IMX800 sensor driver");
MODULE_LICENSE("GPL");
