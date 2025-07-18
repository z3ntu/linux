// SPDX-License-Identifier: GPL-2.0-only
/*
 * A V4L2 driver for Samsung S5KKD1 cameras.
 * Copyright (C) 2025 Luca Weiss <luca.weiss@fairphone.com>
 *
 * Based on Sony imx412 camera driver
 * Copyright (C) 2021 Intel Corporation
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

/* Chip ID */
#define S5KKD1_REG_CHIP_ID		CCI_REG16(0x0000)
#define S5KKD1_CHIP_ID			0x4841

/* Streaming Mode */
#define S5KKD1_REG_MODE_SELECT		CCI_REG16(0x0100)
#define S5KKD1_MODE_STANDBY		0x00
#define S5KKD1_MODE_STREAMING		0x0103

/* Lines per frame */
#define S5KKD1_REG_LPFR			CCI_REG16(0x0340)

/* Exposure control */
#define S5KKD1_REG_EXPOSURE		CCI_REG16(0x0202)
#define S5KKD1_EXPOSURE_MIN		8
#define S5KKD1_EXPOSURE_OFFSET		22
#define S5KKD1_EXPOSURE_STEP		1
#define S5KKD1_EXPOSURE_DEFAULT		0x0648

/* Analog gain control */
#define S5KKD1_REG_ANALOG_GAIN		CCI_REG16(0x0204)
#define S5KKD1_ANA_GAIN_MIN		0
#define S5KKD1_ANA_GAIN_MAX		978
#define S5KKD1_ANA_GAIN_STEP		1
#define S5KKD1_ANA_GAIN_DEFAULT		0

/* Group hold register */
#define S5KKD1_REG_HOLD		CCI_REG8(0x0104)

/* Input clock rate */
#define S5KKD1_INCLK_RATE	24000000

/* CSI2 HW configuration */
#define S5KKD1_LINK_FREQ	600000000
#define S5KKD1_NUM_DATA_LANES	4

#define S5KKD1_REG_MIN		0x00
#define S5KKD1_REG_MAX		0xffff

/**
 * struct s5kkd1_reg_list - s5kkd1 sensor register list
 * @num_of_regs: Number of registers in the list
 * @regs: Pointer to register list
 */
struct s5kkd1_reg_list {
	u32 num_of_regs;
	const struct cci_reg_sequence *regs;
};

/**
 * struct s5kkd1_mode - s5kkd1 sensor mode structure
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
struct s5kkd1_mode {
	u32 width;
	u32 height;
	u32 code;
	u32 hblank;
	u32 vblank;
	u32 vblank_min;
	u32 vblank_max;
	u64 pclk;
	u32 link_freq_idx;
	struct s5kkd1_reg_list reg_list;
};

static const char * const s5kkd1_supply_names[] = {
	"vddd",		/* 1.05V Digital Power */
	"vdda",		/* 2.8V Analog Power */
	"vddio",	/* 1.8V Interface Power */
};

/**
 * struct s5kkd1 - s5kkd1 sensor device structure
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
struct s5kkd1 {
	struct device *dev;
	struct i2c_client *client;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct gpio_desc *reset_gpio;
	struct clk *inclk;
	struct regulator_bulk_data supplies[ARRAY_SIZE(s5kkd1_supply_names)];
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
	const struct s5kkd1_mode *cur_mode;
	struct mutex mutex;
	struct regmap *regmap;
};

static const s64 link_freq[] = {
	S5KKD1_LINK_FREQ,
};

/* Sensor mode registers */
static const struct cci_reg_sequence mode_3104x1848_regs[] = {
	// common registers
	{ CCI_REG16(0xfcfc), 0x4000 },
	{ CCI_REG16(0x0000), 0x0000 },
	{ CCI_REG16(0x0000), 0x4841 },
	{ CCI_REG16(0x6010), 0x0001 },
// WARNING: Delay 10000 us
	{ CCI_REG16(0x6214), 0xff7d },
	{ CCI_REG16(0x6218), 0x0000 },
	{ CCI_REG16(0x6226), 0x0001 },
// WARNING: Delay 10000 us
	{ CCI_REG16(0x0a02), 0x0078 },
	{ CCI_REG16(0xfcfc), 0x2400 },
	{ CCI_REG16(0x7b1c), 0x17a3 },
	{ CCI_REG16(0x7b1e), 0x01fc },
	{ CCI_REG16(0x7b20), 0xe702 },
	{ CCI_REG16(0x7b22), 0xe329 },
	{ CCI_REG16(0x7b24), 0xb787 },
	{ CCI_REG16(0x7b26), 0x0024 },
	{ CCI_REG16(0x7b28), 0x3777 },
	{ CCI_REG16(0x7b2a), 0x0024 },
	{ CCI_REG16(0x7b2c), 0x9387 },
	{ CCI_REG16(0x7b2e), 0xc7c7 },
	{ CCI_REG16(0x7b30), 0x2324 },
	{ CCI_REG16(0x7b32), 0xf79e },
	{ CCI_REG16(0x7b34), 0xb747 },
	{ CCI_REG16(0x7b36), 0x0024 },
	{ CCI_REG16(0x7b38), 0x3787 },
	{ CCI_REG16(0x7b3a), 0x0024 },
	{ CCI_REG16(0x7b3c), 0x9387 },
	{ CCI_REG16(0x7b3e), 0x87ff },
	{ CCI_REG16(0x7b40), 0x1307 },
	{ CCI_REG16(0x7b42), 0x47b9 },
	{ CCI_REG16(0x7b44), 0xd8c3 },
	{ CCI_REG16(0x7b46), 0x3787 },
	{ CCI_REG16(0x7b48), 0x0024 },
	{ CCI_REG16(0x7b4a), 0xb746 },
	{ CCI_REG16(0x7b4c), 0x0024 },
	{ CCI_REG16(0x7b4e), 0x1307 },
	{ CCI_REG16(0x7b50), 0x87be },
	{ CCI_REG16(0x7b52), 0x23aa },
	{ CCI_REG16(0x7b54), 0xe6f4 },
	{ CCI_REG16(0x7b56), 0xb785 },
	{ CCI_REG16(0x7b58), 0x0024 },
	{ CCI_REG16(0x7b5a), 0x3787 },
	{ CCI_REG16(0x7b5c), 0x0024 },
	{ CCI_REG16(0x7b5e), 0x37e5 },
	{ CCI_REG16(0x7b60), 0x0120 },
	{ CCI_REG16(0x7b62), 0x1307 },
	{ CCI_REG16(0x7b64), 0xe7bb },
	{ CCI_REG16(0x7b66), 0x0146 },
	{ CCI_REG16(0x7b68), 0x9385 },
	{ CCI_REG16(0x7b6a), 0x85c3 },
	{ CCI_REG16(0x7b6c), 0x1305 },
	{ CCI_REG16(0x7b6e), 0xc504 },
	{ CCI_REG16(0x7b70), 0x98cb },
	{ CCI_REG16(0x7b72), 0x9730 },
	{ CCI_REG16(0x7b74), 0x00fc },
	{ CCI_REG16(0x7b76), 0xe780 },
	{ CCI_REG16(0x7b78), 0x0036 },
	{ CCI_REG16(0x7b7a), 0xb787 },
	{ CCI_REG16(0x7b7c), 0x0024 },
	{ CCI_REG16(0x7b7e), 0x23a6 },
	{ CCI_REG16(0x7b80), 0xa7b8 },
	{ CCI_REG16(0x7b82), 0x17a3 },
	{ CCI_REG16(0x7b84), 0x01fc },
	{ CCI_REG16(0x7b86), 0x6700 },
	{ CCI_REG16(0x7b88), 0xc325 },
	{ CCI_REG16(0x7b8a), 0x0000 },
	{ CCI_REG16(0x7b8c), 0x0000 },
	{ CCI_REG16(0x7b8e), 0x0000 },
	{ CCI_REG16(0x7b90), 0x0000 },
	{ CCI_REG16(0x7b92), 0x0000 },
	{ CCI_REG16(0x7b94), 0x17a3 },
	{ CCI_REG16(0x7b96), 0x01fc },
	{ CCI_REG16(0x7b98), 0xe702 },
	{ CCI_REG16(0x7b9a), 0x6322 },
	{ CCI_REG16(0x7b9c), 0x97f0 },
	{ CCI_REG16(0x7b9e), 0x00fc },
	{ CCI_REG16(0x7ba0), 0xe780 },
	{ CCI_REG16(0x7ba2), 0xc031 },
	{ CCI_REG16(0x7ba4), 0xb777 },
	{ CCI_REG16(0x7ba6), 0x0024 },
	{ CCI_REG16(0x7ba8), 0x03c5 },
	{ CCI_REG16(0x7baa), 0x8710 },
	{ CCI_REG16(0x7bac), 0x9d45 },
	{ CCI_REG16(0x7bae), 0x9710 },
	{ CCI_REG16(0x7bb0), 0x01fc },
	{ CCI_REG16(0x7bb2), 0xe780 },
	{ CCI_REG16(0x7bb4), 0x2029 },
	{ CCI_REG16(0x7bb6), 0x17a3 },
	{ CCI_REG16(0x7bb8), 0x01fc },
	{ CCI_REG16(0x7bba), 0x6700 },
	{ CCI_REG16(0x7bbc), 0x8322 },
	{ CCI_REG16(0x7bbe), 0x17a3 },
	{ CCI_REG16(0x7bc0), 0x01fc },
	{ CCI_REG16(0x7bc2), 0xe702 },
	{ CCI_REG16(0x7bc4), 0xc31f },
	{ CCI_REG16(0x7bc6), 0x9700 },
	{ CCI_REG16(0x7bc8), 0x01fc },
	{ CCI_REG16(0x7bca), 0xe780 },
	{ CCI_REG16(0x7bcc), 0x6077 },
	{ CCI_REG16(0x7bce), 0x1965 },
	{ CCI_REG16(0x7bd0), 0x0146 },
	{ CCI_REG16(0x7bd2), 0x8965 },
	{ CCI_REG16(0x7bd4), 0x1305 },
	{ CCI_REG16(0x7bd6), 0x0522 },
	{ CCI_REG16(0x7bd8), 0x9780 },
	{ CCI_REG16(0x7bda), 0xfffb },
	{ CCI_REG16(0x7bdc), 0xe780 },
	{ CCI_REG16(0x7bde), 0x8077 },
	{ CCI_REG16(0x7be0), 0x17a3 },
	{ CCI_REG16(0x7be2), 0x01fc },
	{ CCI_REG16(0x7be4), 0x6700 },
	{ CCI_REG16(0x7be6), 0xe31f },
	{ CCI_REG16(0x7be8), 0x17a3 },
	{ CCI_REG16(0x7bea), 0x01fc },
	{ CCI_REG16(0x7bec), 0xe702 },
	{ CCI_REG16(0x7bee), 0x231d },
	{ CCI_REG16(0x7bf0), 0x3764 },
	{ CCI_REG16(0x7bf2), 0x0024 },
	{ CCI_REG16(0x7bf4), 0xb767 },
	{ CCI_REG16(0x7bf6), 0x0024 },
	{ CCI_REG16(0x7bf8), 0x1304 },
	{ CCI_REG16(0x7bfa), 0x04a6 },
	{ CCI_REG16(0x7bfc), 0x9387 },
	{ CCI_REG16(0x7bfe), 0x071f },
	{ CCI_REG16(0x7c00), 0x0329 },
	{ CCI_REG16(0x7c02), 0x440e },
	{ CCI_REG16(0x7c04), 0x8324 },
	{ CCI_REG16(0x7c06), 0x4425 },
	{ CCI_REG16(0x7c08), 0x03c7 },
	{ CCI_REG16(0x7c0a), 0xc724 },
	{ CCI_REG16(0x7c0c), 0x83c7 },
	{ CCI_REG16(0x7c0e), 0xd724 },
	{ CCI_REG16(0x7c10), 0x3317 },
	{ CCI_REG16(0x7c12), 0xe900 },
	{ CCI_REG16(0x7c14), 0xb397 },
	{ CCI_REG16(0x7c16), 0xf400 },
	{ CCI_REG16(0x7c18), 0x2322 },
	{ CCI_REG16(0x7c1a), 0xe40e },
	{ CCI_REG16(0x7c1c), 0x232a },
	{ CCI_REG16(0x7c1e), 0xf424 },
	{ CCI_REG16(0x7c20), 0x97b0 },
	{ CCI_REG16(0x7c22), 0xfffb },
	{ CCI_REG16(0x7c24), 0xe780 },
	{ CCI_REG16(0x7c26), 0xa016 },
	{ CCI_REG16(0x7c28), 0x2322 },
	{ CCI_REG16(0x7c2a), 0x240f },
	{ CCI_REG16(0x7c2c), 0x232a },
	{ CCI_REG16(0x7c2e), 0x9424 },
	{ CCI_REG16(0x7c30), 0x17a3 },
	{ CCI_REG16(0x7c32), 0x01fc },
	{ CCI_REG16(0x7c34), 0x6700 },
	{ CCI_REG16(0x7c36), 0xe31a },
	{ CCI_REG16(0x7c38), 0x17a3 },
	{ CCI_REG16(0x7c3a), 0x01fc },
	{ CCI_REG16(0x7c3c), 0xe702 },
	{ CCI_REG16(0x7c3e), 0x2318 },
	{ CCI_REG16(0x7c40), 0xb787 },
	{ CCI_REG16(0x7c42), 0x0024 },
	{ CCI_REG16(0x7c44), 0x03a4 },
	{ CCI_REG16(0x7c46), 0xc7b8 },
	{ CCI_REG16(0x7c48), 0x0146 },
	{ CCI_REG16(0x7c4a), 0x1145 },
	{ CCI_REG16(0x7c4c), 0xa285 },
	{ CCI_REG16(0x7c4e), 0x9780 },
	{ CCI_REG16(0x7c50), 0xfffb },
	{ CCI_REG16(0x7c52), 0xe780 },
	{ CCI_REG16(0x7c54), 0x407a },
	{ CCI_REG16(0x7c56), 0x9760 },
	{ CCI_REG16(0x7c58), 0x01fc },
	{ CCI_REG16(0x7c5a), 0xe780 },
	{ CCI_REG16(0x7c5c), 0x603f },
	{ CCI_REG16(0x7c5e), 0x0546 },
	{ CCI_REG16(0x7c60), 0xa285 },
	{ CCI_REG16(0x7c62), 0x1145 },
	{ CCI_REG16(0x7c64), 0x9780 },
	{ CCI_REG16(0x7c66), 0xfffb },
	{ CCI_REG16(0x7c68), 0xe780 },
	{ CCI_REG16(0x7c6a), 0xe078 },
	{ CCI_REG16(0x7c6c), 0xb767 },
	{ CCI_REG16(0x7c6e), 0x0024 },
	{ CCI_REG16(0x7c70), 0x239a },
	{ CCI_REG16(0x7c72), 0x0738 },
	{ CCI_REG16(0x7c74), 0x17a3 },
	{ CCI_REG16(0x7c76), 0x01fc },
	{ CCI_REG16(0x7c78), 0x6700 },
	{ CCI_REG16(0x7c7a), 0xa316 },
	{ CCI_REG16(0x35cc), 0x1c80 },
	{ CCI_REG16(0x35ce), 0x0024 },
	{ CCI_REG16(0xfcfc), 0x2400 },
	{ CCI_REG16(0x005a), 0x0000 },
	{ CCI_REG16(0x005c), 0xd60e },
	{ CCI_REG16(0x0060), 0xd606 },
	{ CCI_REG16(0x00ec), 0x0000 },
	{ CCI_REG16(0x011a), 0x0000 },
	{ CCI_REG16(0x0274), 0x0202 },
	{ CCI_REG16(0x0288), 0x0000 },
	{ CCI_REG16(0x028a), 0x0000 },
	{ CCI_REG16(0x0290), 0x0000 },
	{ CCI_REG16(0x0292), 0x0000 },
	{ CCI_REG16(0x029c), 0x0000 },
	{ CCI_REG16(0x029e), 0x0000 },
	{ CCI_REG16(0x02a4), 0x0000 },
	{ CCI_REG16(0x02a6), 0x0000 },
	{ CCI_REG16(0x02a8), 0x0000 },
	{ CCI_REG16(0x02aa), 0x0000 },
	{ CCI_REG16(0x02ac), 0x0000 },
	{ CCI_REG16(0x032a), 0x0000 },
	{ CCI_REG16(0x0634), 0x0002 },
	{ CCI_REG16(0x0636), 0x0002 },
	{ CCI_REG16(0x0638), 0x080a },
	{ CCI_REG16(0x063a), 0x080a },
	{ CCI_REG16(0x063c), 0x0002 },
	{ CCI_REG16(0x063e), 0x0002 },
	{ CCI_REG16(0x0640), 0x080a },
	{ CCI_REG16(0x0642), 0x080a },
	{ CCI_REG16(0x0b48), 0x0000 },
	{ CCI_REG16(0x10ba), 0x0100 },
	{ CCI_REG16(0x10f6), 0x0100 },
	{ CCI_REG16(0x1222), 0x0100 },
	{ CCI_REG16(0x1224), 0x9411 },
	{ CCI_REG16(0x1248), 0x6018 },
	{ CCI_REG16(0x1256), 0x0b00 },
	{ CCI_REG16(0x1260), 0x0100 },
	{ CCI_REG16(0x1292), 0x0101 },
	{ CCI_REG16(0x1294), 0x0001 },
	{ CCI_REG16(0x12a4), 0x0307 },
	{ CCI_REG16(0x1c2e), 0x0300 },
	{ CCI_REG16(0x1c66), 0x530a },
	{ CCI_REG16(0x1c8e), 0x3401 },
	{ CCI_REG16(0x1cbe), 0x0000 },
	{ CCI_REG16(0x1f02), 0xca44 },
	{ CCI_REG16(0x21b0), 0x0101 },
	{ CCI_REG16(0x21b2), 0x0101 },
	{ CCI_REG16(0x2204), 0x0000 },
	{ CCI_REG16(0x22be), 0x0101 },
	{ CCI_REG16(0x2322), 0x0000 },
	{ CCI_REG16(0x2f74), 0x0050 },
	{ CCI_REG16(0x2f7c), 0x0a00 },
	{ CCI_REG16(0x3020), 0x0800 },
	{ CCI_REG16(0x302c), 0x0000 },
	{ CCI_REG16(0x303c), 0x0000 },
	{ CCI_REG16(0x30ae), 0x0200 },
	{ CCI_REG16(0x3128), 0x0001 },
	{ CCI_REG16(0x3162), 0x1400 },
	{ CCI_REG16(0x3192), 0x0000 },
	{ CCI_REG16(0x5730), 0x1ea1 },
	{ CCI_REG16(0x5736), 0x0000 },
	{ CCI_REG16(0x58c0), 0x0c00 },
	{ CCI_REG16(0x58c4), 0x0000 },
	{ CCI_REG16(0x58c6), 0x0700 },
	{ CCI_REG16(0x58c8), 0x0c00 },
	{ CCI_REG16(0x58ce), 0x0700 },
	{ CCI_REG16(0xfcfc), 0x4000 },
	{ CCI_REG16(0x001e), 0x0105 },
	{ CCI_REG16(0x011a), 0x0001 },
	{ CCI_REG16(0x0b0a), 0x0101 },
	{ CCI_REG16(0xf41c), 0x0002 },
	{ CCI_REG16(0xf44a), 0x0006 },
	{ CCI_REG16(0xf44c), 0x0b0b },

	// Sensor Mode 3: 3104 x 1848@60fps
	{ CCI_REG16(0xfcfc), 0x4000 },
	{ CCI_REG16(0x0136), 0x1800 },
	{ CCI_REG16(0x013e), 0x0000 },
	{ CCI_REG16(0x0304), 0x0004 },
	{ CCI_REG16(0x0306), 0x008c },
	{ CCI_REG16(0x030c), 0x0000 },
	{ CCI_REG16(0x0302), 0x0001 },
	{ CCI_REG16(0x0300), 0x0006 },
	{ CCI_REG16(0x030e), 0x0004 },
	{ CCI_REG16(0x0310), 0x0097 },
	{ CCI_REG16(0x0322), 0x0000 },
	{ CCI_REG16(0x0312), 0x0000 },
	{ CCI_REG16(0x030a), 0x0001 },
	{ CCI_REG16(0x0308), 0x0008 },
	{ CCI_REG16(0x02ee), 0x0000 },
	{ CCI_REG16(0xfcfc), 0x2400 },
	{ CCI_REG16(0x1c6a), 0x9600 },
	{ CCI_REG16(0xfcfc), 0x4000 },
	{ CCI_REG16(0x0344), 0x00b0 },
	{ CCI_REG16(0x0348), 0x18ff },
	{ CCI_REG16(0x0346), 0x0268 },
	{ CCI_REG16(0x034a), 0x10e7 },
	{ CCI_REG16(0x0350), 0x0004 },
	{ CCI_REG16(0x0352), 0x0004 },
	{ CCI_REG16(0x034c), 0x0c20 },
	{ CCI_REG16(0x034e), 0x0738 },
	{ CCI_REG16(0x0900), 0x0122 },
	{ CCI_REG16(0x0404), 0x1000 },
	{ CCI_REG16(0x0936), 0x0000 },
	{ CCI_REG16(0x0c40), 0x0000 },
	{ CCI_REG16(0x0086), 0x1000 },
	{ CCI_REG16(0x0380), 0x0002 },
	{ CCI_REG16(0x0382), 0x0002 },
	{ CCI_REG16(0x0384), 0x0002 },
	{ CCI_REG16(0x0386), 0x0002 },
	{ CCI_REG16(0x0342), 0x0e38 },
	{ CCI_REG16(0x0340), 0x09fe },
	{ CCI_REG16(0x0702), 0x0000 },
	{ CCI_REG16(0x0112), 0x0a0a },
	{ CCI_REG16(0x0114), 0x0301 },
	{ CCI_REG16(0x0116), 0x2b00 },
	{ CCI_REG16(0x0118), 0x0000 },
	{ CCI_REG16(0x011c), 0x0100 },
	{ CCI_REG16(0x080e), 0x1200 },
	{ CCI_REG16(0x0816), 0x0000 },
	{ CCI_REG16(0x0d00), 0x0101 },
	{ CCI_REG16(0x0d02), 0x0101 },
	{ CCI_REG16(0x0d04), 0x0102 },
	{ CCI_REG16(0xfcfc), 0x2400 },
	{ CCI_REG16(0x2f9a), 0x0a00 },
	{ CCI_REG16(0xfcfc), 0x4000 },
	{ CCI_REG16(0x0b06), 0x0101 },
	{ CCI_REG16(0x0b08), 0x0000 },
	{ CCI_REG16(0x0fea), 0x0b00 },
	{ CCI_REG16(0xfcfc), 0x2400 },
	{ CCI_REG16(0x01ea), 0xc017 },
	{ CCI_REG16(0x01ec), 0x0018 },
	{ CCI_REG16(0x0050), 0x0100 },
	{ CCI_REG16(0x0070), 0x0002 },
	{ CCI_REG16(0x0080), 0x0600 },
	{ CCI_REG16(0x0084), 0x0000 },
	{ CCI_REG16(0x008a), 0x0000 },
	{ CCI_REG16(0x0098), 0x0a00 },
	{ CCI_REG16(0x009a), 0x0c00 },
	{ CCI_REG16(0x009c), 0x0a00 },
	{ CCI_REG16(0x009e), 0x0c00 },
	{ CCI_REG16(0x00b4), 0x4001 },
	{ CCI_REG16(0x00b8), 0x4001 },
	{ CCI_REG16(0x00e6), 0x0a00 },
	{ CCI_REG16(0x0130), 0x0002 },
	{ CCI_REG16(0x0132), 0x0804 },
	{ CCI_REG16(0x0134), 0x0100 },
	{ CCI_REG16(0x0138), 0x0001 },
	{ CCI_REG16(0x01a6), 0x7701 },
	{ CCI_REG16(0x028c), 0x0000 },
	{ CCI_REG16(0x028e), 0x0000 },
	{ CCI_REG16(0x0294), 0x0000 },
	{ CCI_REG16(0x0296), 0x0000 },
	{ CCI_REG16(0x0298), 0x0000 },
	{ CCI_REG16(0x029a), 0x0000 },
	{ CCI_REG16(0x02a0), 0x0000 },
	{ CCI_REG16(0x02a2), 0x0000 },
	{ CCI_REG16(0x0308), 0x0000 },
	{ CCI_REG16(0x030a), 0x0000 },
	{ CCI_REG16(0x030c), 0x0000 },
	{ CCI_REG16(0x030e), 0x0000 },
	{ CCI_REG16(0x0310), 0x0000 },
	{ CCI_REG16(0x0312), 0x0000 },
	{ CCI_REG16(0x0314), 0x0000 },
	{ CCI_REG16(0x0316), 0x0000 },
	{ CCI_REG16(0x0318), 0x0000 },
	{ CCI_REG16(0x031a), 0x0000 },
	{ CCI_REG16(0x031c), 0x0000 },
	{ CCI_REG16(0x031e), 0x0000 },
	{ CCI_REG16(0x0320), 0x0000 },
	{ CCI_REG16(0x0322), 0x0000 },
	{ CCI_REG16(0x0324), 0x0000 },
	{ CCI_REG16(0x0326), 0x0000 },
	{ CCI_REG16(0x0328), 0x0000 },
	{ CCI_REG16(0x032c), 0x0000 },
	{ CCI_REG16(0x0632), 0x0110 },
	{ CCI_REG16(0x0906), 0x1000 },
	{ CCI_REG16(0x097e), 0x1000 },
	{ CCI_REG16(0x09f6), 0x1000 },
	{ CCI_REG16(0x0a6e), 0x1000 },
	{ CCI_REG16(0x0a78), 0x3000 },
	{ CCI_REG16(0x0a7a), 0x3000 },
	{ CCI_REG16(0x0ae6), 0x1000 },
	{ CCI_REG16(0x0af0), 0x3000 },
	{ CCI_REG16(0x0af2), 0x3000 },
	{ CCI_REG16(0x0b32), 0x0000 },
	{ CCI_REG16(0x0b4a), 0x0100 },
	{ CCI_REG16(0x1088), 0x0020 },
	{ CCI_REG16(0x109e), 0x1000 },
	{ CCI_REG16(0x10da), 0x0100 },
	{ CCI_REG16(0x1226), 0x0200 },
	{ CCI_REG16(0x1230), 0x0100 },
	{ CCI_REG16(0x1288), 0x0000 },
	{ CCI_REG16(0x128a), 0x0400 },
	{ CCI_REG16(0x128c), 0x0200 },
	{ CCI_REG16(0x128e), 0x0a00 },
	{ CCI_REG16(0x1290), 0x0900 },
	{ CCI_REG16(0x12a0), 0x0101 },
	{ CCI_REG16(0x12a2), 0x0100 },
	{ CCI_REG16(0x12a6), 0x0000 },
	{ CCI_REG16(0x12a8), 0x0000 },
	{ CCI_REG16(0x12aa), 0x0000 },
	{ CCI_REG16(0x12c4), 0x0402 },
	{ CCI_REG16(0x12dc), 0x0000 },
	{ CCI_REG16(0x13b6), 0x1800 },
	{ CCI_REG16(0x13bc), 0x1800 },
	{ CCI_REG16(0x1c6e), 0x0000 },
	{ CCI_REG16(0x1c9a), 0x8207 },
	{ CCI_REG16(0x1ca4), 0x8a00 },
	{ CCI_REG16(0x1ca6), 0x0206 },
	{ CCI_REG16(0x1cb0), 0xbf00 },
	{ CCI_REG16(0x1cba), 0x0101 },
	{ CCI_REG16(0x1f42), 0x0100 },
	{ CCI_REG16(0x1f5e), 0x0000 },
	{ CCI_REG16(0x2144), 0x1a00 },
	{ CCI_REG16(0x2156), 0x0000 },
	{ CCI_REG16(0x2176), 0x0000 },
	{ CCI_REG16(0x2180), 0x0000 },
	{ CCI_REG16(0x2182), 0x0000 },
	{ CCI_REG16(0x2190), 0xffff },
	{ CCI_REG16(0x21b4), 0x0101 },
	{ CCI_REG16(0x21b6), 0x0100 },
	{ CCI_REG16(0x21b8), 0x0000 },
	{ CCI_REG16(0x21ba), 0x7f00 },
	{ CCI_REG16(0x21bc), 0x0008 },
	{ CCI_REG16(0x21be), 0x0000 },
	{ CCI_REG16(0x21c0), 0x0000 },
	{ CCI_REG16(0x21c4), 0x8000 },
	{ CCI_REG16(0x21c6), 0x0108 },
	{ CCI_REG16(0x21c8), 0x0000 },
	{ CCI_REG16(0x21ca), 0x0000 },
	{ CCI_REG16(0x21ce), 0x5500 },
	{ CCI_REG16(0x21d0), 0x6600 },
	{ CCI_REG16(0x21d2), 0x10d6 },
	{ CCI_REG16(0x21fe), 0x0800 },
	{ CCI_REG16(0x2200), 0x0a00 },
	{ CCI_REG16(0x2202), 0x6cf4 },
	{ CCI_REG16(0x2206), 0x4100 },
	{ CCI_REG16(0x2208), 0x3ef6 },
	{ CCI_REG16(0x222e), 0x0000 },
	{ CCI_REG16(0x2230), 0x0000 },
	{ CCI_REG16(0x2232), 0x0000 },
	{ CCI_REG16(0x225e), 0x0000 },
	{ CCI_REG16(0x2260), 0x0000 },
	{ CCI_REG16(0x2262), 0x0000 },
	{ CCI_REG16(0x22c2), 0x0101 },
	{ CCI_REG16(0x22c8), 0x4000 },
	{ CCI_REG16(0x22ca), 0x7f00 },
	{ CCI_REG16(0x22cc), 0x8000 },
	{ CCI_REG16(0x22ce), 0x0001 },
	{ CCI_REG16(0x22d0), 0x0002 },
	{ CCI_REG16(0x22d2), 0x0004 },
	{ CCI_REG16(0x22d4), 0x0010 },
	{ CCI_REG16(0x230e), 0x54f4 },
	{ CCI_REG16(0x2310), 0x1611 },
	{ CCI_REG16(0x2312), 0x1607 },
	{ CCI_REG16(0x2314), 0x1611 },
	{ CCI_REG16(0x2316), 0x1613 },
	{ CCI_REG16(0x2318), 0x1609 },
	{ CCI_REG16(0x231a), 0x1613 },
	{ CCI_REG16(0x231c), 0x1619 },
	{ CCI_REG16(0x231e), 0x161f },
	{ CCI_REG16(0x2320), 0x6af4 },
	{ CCI_REG16(0x2324), 0x4000 },
	{ CCI_REG16(0x2326), 0x4000 },
	{ CCI_REG16(0x2328), 0x0000 },
	{ CCI_REG16(0x232a), 0x4000 },
	{ CCI_REG16(0x232c), 0x4000 },
	{ CCI_REG16(0x232e), 0x4000 },
	{ CCI_REG16(0x2330), 0x4000 },
	{ CCI_REG16(0x2332), 0x0ad6 },
	{ CCI_REG16(0x2334), 0x0c0c },
	{ CCI_REG16(0x2336), 0x0c0c },
	{ CCI_REG16(0x2338), 0x0c0c },
	{ CCI_REG16(0x233a), 0x0101 },
	{ CCI_REG16(0x233c), 0x0101 },
	{ CCI_REG16(0x233e), 0x0a0a },
	{ CCI_REG16(0x2340), 0x0a0a },
	{ CCI_REG16(0x2342), 0x0a0a },
	{ CCI_REG16(0x2344), 0x02f4 },
	{ CCI_REG16(0x2346), 0x4000 },
	{ CCI_REG16(0x2348), 0x4000 },
	{ CCI_REG16(0x234a), 0x4000 },
	{ CCI_REG16(0x234c), 0x4c00 },
	{ CCI_REG16(0x234e), 0x4c00 },
	{ CCI_REG16(0x2350), 0x4000 },
	{ CCI_REG16(0x2352), 0x4000 },
	{ CCI_REG16(0x2354), 0x4000 },
	{ CCI_REG16(0x2f70), 0xe803 },
	{ CCI_REG16(0x2f8e), 0x0101 },
	{ CCI_REG16(0x2f94), 0x0100 },
	{ CCI_REG16(0x2fa6), 0x0200 },
	{ CCI_REG16(0x2fde), 0x0000 },
	{ CCI_REG16(0x2fe0), 0x0000 },
	{ CCI_REG16(0x3018), 0x0000 },
	{ CCI_REG16(0x3098), 0x0400 },
	{ CCI_REG16(0x30a0), 0xf002 },
	{ CCI_REG16(0x30c6), 0x0000 },
	{ CCI_REG16(0x30d4), 0x0000 },
	{ CCI_REG16(0x313a), 0x0100 },
	{ CCI_REG16(0x314a), 0x0010 },
	{ CCI_REG16(0x3164), 0x0000 },
	{ CCI_REG16(0x31ce), 0x2c01 },
	{ CCI_REG16(0x3e40), 0x0100 },
	{ CCI_REG16(0x3e50), 0x0000 },
	{ CCI_REG16(0x3e54), 0x2d01 },
	{ CCI_REG16(0x5732), 0x0100 },
	{ CCI_REG16(0x5734), 0x0000 },
	{ CCI_REG16(0x58cc), 0x0100 },
	{ CCI_REG16(0x7108), 0x0000 },
	{ CCI_REG16(0xfcfc), 0x4000 },
	{ CCI_REG16(0x6226), 0x0000 },
};

/* Supported sensor mode configurations */
static const struct s5kkd1_mode supported_mode = {
	.width = 3104,
	.height = 1848,
	.hblank = 456, // FIXME
	.vblank = 506, // FIXME
	.vblank_min = 506, // FIXME
	.vblank_max = 32420, // FIXME
	.pclk = 619200000, // outputPixelClock?
	.link_freq_idx = 0,
	.code = MEDIA_BUS_FMT_SRGGB10_1X10,
	.reg_list = {
		.num_of_regs = ARRAY_SIZE(mode_3104x1848_regs),
		.regs = mode_3104x1848_regs,
	},
};

/**
 * to_s5kkd1() - s5kkd1 V4L2 sub-device to s5kkd1 device.
 * @subdev: pointer to s5kkd1 V4L2 sub-device
 *
 * Return: pointer to s5kkd1 device
 */
static inline struct s5kkd1 *to_s5kkd1(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct s5kkd1, sd);
}

/**
 * s5kkd1_update_controls() - Update control ranges based on streaming mode
 * @s5kkd1: pointer to s5kkd1 device
 * @mode: pointer to s5kkd1_mode sensor mode
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kkd1_update_controls(struct s5kkd1 *s5kkd1,
				  const struct s5kkd1_mode *mode)
{
	int ret;

	ret = __v4l2_ctrl_s_ctrl(s5kkd1->link_freq_ctrl, mode->link_freq_idx);
	if (ret)
		return ret;

	ret = __v4l2_ctrl_s_ctrl(s5kkd1->hblank_ctrl, mode->hblank);
	if (ret)
		return ret;

	return __v4l2_ctrl_modify_range(s5kkd1->vblank_ctrl, mode->vblank_min,
					mode->vblank_max, 1, mode->vblank);
}

/**
 * s5kkd1_update_exp_gain() - Set updated exposure and gain
 * @s5kkd1: pointer to s5kkd1 device
 * @exposure: updated exposure value
 * @gain: updated analog gain value
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kkd1_update_exp_gain(struct s5kkd1 *s5kkd1, u32 exposure, u32 gain)
{
	u32 lpfr;
	int ret;

	lpfr = s5kkd1->vblank + s5kkd1->cur_mode->height;

	dev_dbg(s5kkd1->dev, "Set exp %u, analog gain %u, lpfr %u\n",
		exposure, gain, lpfr);

	cci_write(s5kkd1->regmap, S5KKD1_REG_HOLD, 1, &ret);
	if (ret)
		return ret;

	cci_write(s5kkd1->regmap, S5KKD1_REG_LPFR, lpfr, &ret);
	if (ret)
		goto error_release_group_hold;

	cci_write(s5kkd1->regmap, S5KKD1_REG_EXPOSURE, exposure, &ret);
	if (ret)
		goto error_release_group_hold;

	cci_write(s5kkd1->regmap, S5KKD1_REG_ANALOG_GAIN, gain, &ret);

error_release_group_hold:
	cci_write(s5kkd1->regmap, S5KKD1_REG_HOLD, 0, NULL);

	return ret;
}

/**
 * s5kkd1_set_ctrl() - Set subdevice control
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
static int s5kkd1_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct s5kkd1 *s5kkd1 =
		container_of(ctrl->handler, struct s5kkd1, ctrl_handler);
	u32 analog_gain;
	u32 exposure;
	int ret;

	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		s5kkd1->vblank = s5kkd1->vblank_ctrl->val;

		dev_dbg(s5kkd1->dev, "Received vblank %u, new lpfr %u\n",
			s5kkd1->vblank,
			s5kkd1->vblank + s5kkd1->cur_mode->height);

		ret = __v4l2_ctrl_modify_range(s5kkd1->exp_ctrl,
					       S5KKD1_EXPOSURE_MIN,
					       s5kkd1->vblank +
					       s5kkd1->cur_mode->height -
					       S5KKD1_EXPOSURE_OFFSET,
					       1, S5KKD1_EXPOSURE_DEFAULT);
		break;
	case V4L2_CID_EXPOSURE:
		/* Set controls only if sensor is in power on state */
		if (!pm_runtime_get_if_in_use(s5kkd1->dev))
			return 0;

		exposure = ctrl->val;
		analog_gain = s5kkd1->again_ctrl->val;

		dev_dbg(s5kkd1->dev, "Received exp %u, analog gain %u\n",
			exposure, analog_gain);

		ret = s5kkd1_update_exp_gain(s5kkd1, exposure, analog_gain);

		pm_runtime_put(s5kkd1->dev);

		break;
	default:
		dev_err(s5kkd1->dev, "Invalid control %d\n", ctrl->id);
		ret = -EINVAL;
	}

	return ret;
}

/* V4l2 subdevice control ops*/
static const struct v4l2_ctrl_ops s5kkd1_ctrl_ops = {
	.s_ctrl = s5kkd1_set_ctrl,
};

/**
 * s5kkd1_enum_mbus_code() - Enumerate V4L2 sub-device mbus codes
 * @sd: pointer to s5kkd1 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @code: V4L2 sub-device code enumeration need to be filled
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kkd1_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = supported_mode.code;

	return 0;
}

/**
 * s5kkd1_enum_frame_size() - Enumerate V4L2 sub-device frame sizes
 * @sd: pointer to s5kkd1 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fsize: V4L2 sub-device size enumeration need to be filled
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kkd1_enum_frame_size(struct v4l2_subdev *sd,
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
 * s5kkd1_fill_pad_format() - Fill subdevice pad format
 *                            from selected sensor mode
 * @s5kkd1: pointer to s5kkd1 device
 * @mode: pointer to s5kkd1_mode sensor mode
 * @fmt: V4L2 sub-device format need to be filled
 */
static void s5kkd1_fill_pad_format(struct s5kkd1 *s5kkd1,
				   const struct s5kkd1_mode *mode,
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
 * s5kkd1_get_pad_format() - Get subdevice pad format
 * @sd: pointer to s5kkd1 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fmt: V4L2 sub-device format need to be set
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kkd1_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct s5kkd1 *s5kkd1 = to_s5kkd1(sd);

	mutex_lock(&s5kkd1->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *framefmt;

		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		fmt->format = *framefmt;
	} else {
		s5kkd1_fill_pad_format(s5kkd1, s5kkd1->cur_mode, fmt);
	}

	mutex_unlock(&s5kkd1->mutex);

	return 0;
}

/**
 * s5kkd1_set_pad_format() - Set subdevice pad format
 * @sd: pointer to s5kkd1 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fmt: V4L2 sub-device format need to be set
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kkd1_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct s5kkd1 *s5kkd1 = to_s5kkd1(sd);
	const struct s5kkd1_mode *mode;
	int ret = 0;

	mutex_lock(&s5kkd1->mutex);

	mode = &supported_mode;
	s5kkd1_fill_pad_format(s5kkd1, mode, fmt);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *framefmt;

		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		*framefmt = fmt->format;
	} else {
		ret = s5kkd1_update_controls(s5kkd1, mode);
		if (!ret)
			s5kkd1->cur_mode = mode;
	}

	mutex_unlock(&s5kkd1->mutex);

	return ret;
}

/**
 * s5kkd1_init_state() - Initialize sub-device state
 * @sd: pointer to s5kkd1 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kkd1_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	struct s5kkd1 *s5kkd1 = to_s5kkd1(sd);
	struct v4l2_subdev_format fmt = { 0 };

	fmt.which = sd_state ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	s5kkd1_fill_pad_format(s5kkd1, &supported_mode, &fmt);

	return s5kkd1_set_pad_format(sd, sd_state, &fmt);
}

/**
 * s5kkd1_start_streaming() - Start sensor stream
 * @s5kkd1: pointer to s5kkd1 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kkd1_start_streaming(struct s5kkd1 *s5kkd1)
{
	const struct s5kkd1_reg_list *reg_list;
	int ret;

	/* Write sensor mode registers */
	reg_list = &s5kkd1->cur_mode->reg_list;
	ret = cci_multi_reg_write(s5kkd1->regmap, reg_list->regs,
				  reg_list->num_of_regs, NULL);
	if (ret) {
		dev_err(s5kkd1->dev, "fail to write initial registers\n");
		return ret;
	}

	/* Setup handler will write actual exposure and gain */
	ret =  __v4l2_ctrl_handler_setup(s5kkd1->sd.ctrl_handler);
	if (ret) {
		dev_err(s5kkd1->dev, "fail to setup handler\n");
		return ret;
	}

	/* Delay is required before streaming*/
	usleep_range(7400, 8000);

	/* Start streaming */
	cci_write(s5kkd1->regmap, S5KKD1_REG_MODE_SELECT, S5KKD1_MODE_STREAMING, &ret);
	if (ret) {
		dev_err(s5kkd1->dev, "fail to start streaming\n");
		return ret;
	}

	return 0;
}

/**
 * s5kkd1_stop_streaming() - Stop sensor stream
 * @s5kkd1: pointer to s5kkd1 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kkd1_stop_streaming(struct s5kkd1 *s5kkd1)
{
	return cci_write(s5kkd1->regmap, S5KKD1_REG_MODE_SELECT,
			 S5KKD1_MODE_STANDBY, NULL);
}

/**
 * s5kkd1_set_stream() - Enable sensor streaming
 * @sd: pointer to s5kkd1 subdevice
 * @enable: set to enable sensor streaming
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kkd1_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct s5kkd1 *s5kkd1 = to_s5kkd1(sd);
	int ret;

	mutex_lock(&s5kkd1->mutex);

	if (enable) {
		ret = pm_runtime_resume_and_get(s5kkd1->dev);
		if (ret)
			goto error_unlock;

		ret = s5kkd1_start_streaming(s5kkd1);
		if (ret)
			goto error_power_off;
	} else {
		s5kkd1_stop_streaming(s5kkd1);
		pm_runtime_put(s5kkd1->dev);
	}

	mutex_unlock(&s5kkd1->mutex);

	return 0;

error_power_off:
	pm_runtime_put(s5kkd1->dev);
error_unlock:
	mutex_unlock(&s5kkd1->mutex);

	return ret;
}

/**
 * s5kkd1_detect() - Detect s5kkd1 sensor
 * @s5kkd1: pointer to s5kkd1 device
 *
 * Return: 0 if successful, -EIO if sensor id does not match
 */
static int s5kkd1_detect(struct s5kkd1 *s5kkd1)
{
	int ret;
	u64 val;

	ret = cci_read(s5kkd1->regmap, S5KKD1_REG_CHIP_ID, &val, NULL);
	if (ret)
		return ret;

	printk(KERN_ERR "DBG %s:%d val=0x%llx\n", __func__, __LINE__, val);

	if (val != S5KKD1_CHIP_ID) {
		dev_err(s5kkd1->dev, "chip id mismatch: %x!=%llx\n",
			S5KKD1_CHIP_ID, val);
		return -ENXIO;
	}

	return 0;
}

/**
 * s5kkd1_parse_hw_config() - Parse HW configuration and check if supported
 * @s5kkd1: pointer to s5kkd1 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kkd1_parse_hw_config(struct s5kkd1 *s5kkd1)
{
	struct fwnode_handle *fwnode = dev_fwnode(s5kkd1->dev);
	struct v4l2_fwnode_endpoint bus_cfg = {};
	struct fwnode_handle *ep;
	unsigned long rate;
	unsigned int i;
	int ret;

	if (!fwnode)
		return -ENXIO;

	/* Request optional reset pin */
	s5kkd1->reset_gpio = devm_gpiod_get_optional(s5kkd1->dev, "reset",
						     GPIOD_OUT_LOW);
	if (IS_ERR(s5kkd1->reset_gpio)) {
		dev_err(s5kkd1->dev, "failed to get reset gpio %ld\n",
			PTR_ERR(s5kkd1->reset_gpio));
		return PTR_ERR(s5kkd1->reset_gpio);
	}

	/* Get sensor input clock */
	s5kkd1->inclk = devm_clk_get(s5kkd1->dev, NULL);
	if (IS_ERR(s5kkd1->inclk)) {
		dev_err(s5kkd1->dev, "could not get inclk\n");
		return PTR_ERR(s5kkd1->inclk);
	}

	rate = clk_get_rate(s5kkd1->inclk);
	if (rate != S5KKD1_INCLK_RATE) {
		dev_err(s5kkd1->dev, "inclk frequency mismatch\n");
		return -EINVAL;
	}

	/* Get optional DT defined regulators */
	for (i = 0; i < ARRAY_SIZE(s5kkd1_supply_names); i++)
		s5kkd1->supplies[i].supply = s5kkd1_supply_names[i];

	ret = devm_regulator_bulk_get(s5kkd1->dev,
				      ARRAY_SIZE(s5kkd1_supply_names),
				      s5kkd1->supplies);
	if (ret)
		return ret;

	return 0; // FIXME

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -ENXIO;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	if (bus_cfg.bus_type != V4L2_MBUS_CSI2_DPHY) {
		dev_err(s5kkd1->dev, "selected bus-type is not supported\n");
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != S5KKD1_NUM_DATA_LANES) {
		dev_err(s5kkd1->dev,
			"number of CSI2 data lanes %d is not supported\n",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	if (!bus_cfg.nr_of_link_frequencies) {
		dev_err(s5kkd1->dev, "no link frequencies defined\n");
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	for (i = 0; i < bus_cfg.nr_of_link_frequencies; i++)
		if (bus_cfg.link_frequencies[i] == S5KKD1_LINK_FREQ)
			goto done_endpoint_free;

	ret = -EINVAL;

done_endpoint_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

/* V4l2 subdevice ops */
static const struct v4l2_subdev_video_ops s5kkd1_video_ops = {
	.s_stream = s5kkd1_set_stream,
};

static const struct v4l2_subdev_pad_ops s5kkd1_pad_ops = {
	.enum_mbus_code = s5kkd1_enum_mbus_code,
	.enum_frame_size = s5kkd1_enum_frame_size,
	.get_fmt = s5kkd1_get_pad_format,
	.set_fmt = s5kkd1_set_pad_format,
};

static const struct v4l2_subdev_ops s5kkd1_subdev_ops = {
	.video = &s5kkd1_video_ops,
	.pad = &s5kkd1_pad_ops,
};

static const struct v4l2_subdev_internal_ops s5kkd1_internal_ops = {
	.init_state = s5kkd1_init_state,
};

/**
 * s5kkd1_power_on() - Sensor power on sequence
 * @dev: pointer to i2c device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kkd1_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct s5kkd1 *s5kkd1 = to_s5kkd1(sd);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(s5kkd1_supply_names),
				    s5kkd1->supplies);
	if (ret < 0) {
		dev_err(dev, "failed to enable regulators\n");
		return ret;
	}

	gpiod_set_value_cansleep(s5kkd1->reset_gpio, 0);

	ret = clk_prepare_enable(s5kkd1->inclk);
	if (ret) {
		dev_err(s5kkd1->dev, "fail to enable inclk\n");
		goto error_reset;
	}

	usleep_range(1000, 1200);

	return 0;

error_reset:
	gpiod_set_value_cansleep(s5kkd1->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(s5kkd1_supply_names),
			       s5kkd1->supplies);

	return ret;
}

/**
 * s5kkd1_power_off() - Sensor power off sequence
 * @dev: pointer to i2c device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kkd1_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct s5kkd1 *s5kkd1 = to_s5kkd1(sd);

	clk_disable_unprepare(s5kkd1->inclk);

	gpiod_set_value_cansleep(s5kkd1->reset_gpio, 1);

	regulator_bulk_disable(ARRAY_SIZE(s5kkd1_supply_names),
			       s5kkd1->supplies);

	return 0;
}

/**
 * s5kkd1_init_controls() - Initialize sensor subdevice controls
 * @s5kkd1: pointer to s5kkd1 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kkd1_init_controls(struct s5kkd1 *s5kkd1)
{
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr = &s5kkd1->ctrl_handler;
	const struct s5kkd1_mode *mode = s5kkd1->cur_mode;
	u32 lpfr;
	int ret;

	/* set properties from fwnode (e.g. rotation, orientation) */
	ret = v4l2_fwnode_device_parse(s5kkd1->dev, &props);
	if (ret)
		return ret;

	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 8);
	if (ret)
		return ret;

	/* Serialize controls with sensor device */
	ctrl_hdlr->lock = &s5kkd1->mutex;

	/* Initialize exposure and gain */
	lpfr = mode->vblank + mode->height;
	s5kkd1->exp_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					     &s5kkd1_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     S5KKD1_EXPOSURE_MIN,
					     lpfr - S5KKD1_EXPOSURE_OFFSET,
					     S5KKD1_EXPOSURE_STEP,
					     S5KKD1_EXPOSURE_DEFAULT);

	s5kkd1->again_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					       &s5kkd1_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN,
					       S5KKD1_ANA_GAIN_MIN,
					       S5KKD1_ANA_GAIN_MAX,
					       S5KKD1_ANA_GAIN_STEP,
					       S5KKD1_ANA_GAIN_DEFAULT);

	v4l2_ctrl_cluster(2, &s5kkd1->exp_ctrl);

	s5kkd1->vblank_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
						&s5kkd1_ctrl_ops,
						V4L2_CID_VBLANK,
						mode->vblank_min,
						mode->vblank_max,
						1, mode->vblank);

	/* Read only controls */
	s5kkd1->pclk_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					      &s5kkd1_ctrl_ops,
					      V4L2_CID_PIXEL_RATE,
					      mode->pclk, mode->pclk,
					      1, mode->pclk);

	s5kkd1->link_freq_ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr,
							&s5kkd1_ctrl_ops,
							V4L2_CID_LINK_FREQ,
							ARRAY_SIZE(link_freq) -
							1,
							mode->link_freq_idx,
							link_freq);
	if (s5kkd1->link_freq_ctrl)
		s5kkd1->link_freq_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	s5kkd1->hblank_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
						&s5kkd1_ctrl_ops,
						V4L2_CID_HBLANK,
						S5KKD1_REG_MIN,
						S5KKD1_REG_MAX,
						1, mode->hblank);
	if (s5kkd1->hblank_ctrl)
		s5kkd1->hblank_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &s5kkd1_ctrl_ops, &props);

	if (ctrl_hdlr->error) {
		dev_err(s5kkd1->dev, "control init failed: %d\n",
			ctrl_hdlr->error);
		v4l2_ctrl_handler_free(ctrl_hdlr);
		return ctrl_hdlr->error;
	}

	s5kkd1->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

/**
 * s5kkd1_probe() - I2C client device binding
 * @client: pointer to i2c client device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kkd1_probe(struct i2c_client *client)
{
	struct s5kkd1 *s5kkd1;
	int ret;

	s5kkd1 = devm_kzalloc(&client->dev, sizeof(*s5kkd1), GFP_KERNEL);
	if (!s5kkd1)
		return -ENOMEM;

	s5kkd1->dev = &client->dev;

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&s5kkd1->sd, client, &s5kkd1_subdev_ops);
	s5kkd1->sd.internal_ops = &s5kkd1_internal_ops;

	ret = s5kkd1_parse_hw_config(s5kkd1);
	if (ret) {
		dev_err(s5kkd1->dev, "HW configuration is not supported\n");
		return ret;
	}

	mutex_init(&s5kkd1->mutex);

	s5kkd1->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(s5kkd1->regmap))
		return dev_err_probe(s5kkd1->dev, PTR_ERR(s5kkd1->regmap),
				     "failed to initialize CCI\n");

	ret = s5kkd1_power_on(s5kkd1->dev);
	if (ret) {
		dev_err(s5kkd1->dev, "failed to power-on the sensor\n");
		goto error_mutex_destroy;
	}

	/* Check module identity */
	ret = s5kkd1_detect(s5kkd1);
	if (ret) {
		dev_err(s5kkd1->dev, "failed to find sensor: %d\n", ret);
		goto error_power_off;
	}

	/* Set default mode to max resolution */
	s5kkd1->cur_mode = &supported_mode;
	s5kkd1->vblank = s5kkd1->cur_mode->vblank;

	ret = s5kkd1_init_controls(s5kkd1);
	if (ret) {
		dev_err(s5kkd1->dev, "failed to init controls: %d\n", ret);
		goto error_power_off;
	}

	/* Initialize subdev */
	s5kkd1->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	s5kkd1->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	s5kkd1->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&s5kkd1->sd.entity, 1, &s5kkd1->pad);
	if (ret) {
		dev_err(s5kkd1->dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&s5kkd1->sd);
	if (ret < 0) {
		dev_err(s5kkd1->dev,
			"failed to register async subdev: %d\n", ret);
		goto error_media_entity;
	}

	pm_runtime_set_active(s5kkd1->dev);
	pm_runtime_enable(s5kkd1->dev);
	pm_runtime_idle(s5kkd1->dev);

	return 0;

error_media_entity:
	media_entity_cleanup(&s5kkd1->sd.entity);
error_handler_free:
	v4l2_ctrl_handler_free(s5kkd1->sd.ctrl_handler);
error_power_off:
	s5kkd1_power_off(s5kkd1->dev);
error_mutex_destroy:
	mutex_destroy(&s5kkd1->mutex);

	return ret;
}

static void s5kkd1_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5kkd1 *s5kkd1 = to_s5kkd1(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		s5kkd1_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	mutex_destroy(&s5kkd1->mutex);
}

static const struct dev_pm_ops s5kkd1_pm_ops = {
	SET_RUNTIME_PM_OPS(s5kkd1_power_off, s5kkd1_power_on, NULL)
};

static const struct of_device_id s5kkd1_of_match[] = {
	{ .compatible = "samsung,s5kkd1" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s5kkd1_of_match);

static struct i2c_driver s5kkd1_driver = {
	.probe = s5kkd1_probe,
	.remove = s5kkd1_remove,
	.driver = {
		.name = "s5kkd1",
		.pm = &s5kkd1_pm_ops,
		.of_match_table = s5kkd1_of_match,
	},
};

module_i2c_driver(s5kkd1_driver);

MODULE_DESCRIPTION("Samsung S5KKD1 sensor driver");
MODULE_LICENSE("GPL");
