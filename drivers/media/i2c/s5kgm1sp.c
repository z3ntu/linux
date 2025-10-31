// SPDX-License-Identifier: GPL-2.0-only
/*
 * A V4L2 driver for Samsung S5KGM1SP cameras.
 * Copyright (C) 2024 Luca Weiss <luca.weiss@fairphone.com>
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
#define S5KGM1SP_REG_CHIP_ID		CCI_REG16(0x0000)
#define S5KGM1SP_CHIP_ID			0x08d1

/* Streaming Mode */
#define S5KGM1SP_REG_MODE_SELECT		CCI_REG8(0x0100)
#define S5KGM1SP_MODE_STANDBY		0x00
#define S5KGM1SP_MODE_STREAMING		0x01

/* Lines per frame */
#define S5KGM1SP_REG_LPFR			CCI_REG16(0x0340)

/* Exposure control */
#define S5KGM1SP_REG_EXPOSURE		CCI_REG16(0x0202)
#define S5KGM1SP_EXPOSURE_MIN		8
#define S5KGM1SP_EXPOSURE_OFFSET		22
#define S5KGM1SP_EXPOSURE_STEP		1
#define S5KGM1SP_EXPOSURE_DEFAULT		0x0648

/* Analog gain control */
#define S5KGM1SP_REG_ANALOG_GAIN		CCI_REG16(0x0204)
#define S5KGM1SP_ANA_GAIN_MIN		0
#define S5KGM1SP_ANA_GAIN_MAX		978
#define S5KGM1SP_ANA_GAIN_STEP		1
#define S5KGM1SP_ANA_GAIN_DEFAULT		0

/* Group hold register */
#define S5KGM1SP_REG_HOLD		CCI_REG8(0x0104)

/* Input clock rate */
#define S5KGM1SP_INCLK_RATE	24000000

/* CSI2 HW configuration */
#define S5KGM1SP_LINK_FREQ	600000000
#define S5KGM1SP_NUM_DATA_LANES	4

#define S5KGM1SP_REG_MIN		0x00
#define S5KGM1SP_REG_MAX		0xffff

/**
 * struct s5kgm1sp_reg_list - s5kgm1sp sensor register list
 * @num_of_regs: Number of registers in the list
 * @regs: Pointer to register list
 */
struct s5kgm1sp_reg_list {
	u32 num_of_regs;
	const struct cci_reg_sequence *regs;
};

/**
 * struct s5kgm1sp_mode - s5kgm1sp sensor mode structure
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
struct s5kgm1sp_mode {
	u32 width;
	u32 height;
	u32 code;
	u32 hblank;
	u32 vblank;
	u32 vblank_min;
	u32 vblank_max;
	u64 pclk;
	u32 link_freq_idx;
	struct s5kgm1sp_reg_list reg_list;
};

static const char * const s5kgm1sp_supply_names[] = {
	"vdda",		/* 2.8V Analog Power */
	"vddio",	/* 1.8V Interface Power */
	"vddd",		/* 1.05V Digital Power */
};

/**
 * struct s5kgm1sp - s5kgm1sp sensor device structure
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
struct s5kgm1sp {
	struct device *dev;
	struct i2c_client *client;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct gpio_desc *reset_gpio;
	struct clk *inclk;
	struct regulator_bulk_data supplies[ARRAY_SIZE(s5kgm1sp_supply_names)];
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
	const struct s5kgm1sp_mode *cur_mode;
	struct mutex mutex;
	struct regmap *regmap;
};

static const s64 link_freq[] = {
	S5KGM1SP_LINK_FREQ,
};

/* Sensor mode registers */
static const struct cci_reg_sequence mode_2000x1500_regs[] = {
	// common registers
	{ CCI_REG16(0x6028), 0x4000 },
	{ CCI_REG16(0x0000), 0x0009 },
	{ CCI_REG16(0x0000), 0x08D1 },
	{ CCI_REG16(0x6010), 0x0001 },
	{ CCI_REG16(0x6214), 0x7971 },
	{ CCI_REG16(0x6218), 0x7150 },
	{ CCI_REG16(0x0A02), 0x0074 },
	{ CCI_REG16(0x6028), 0x2000 },
	{ CCI_REG16(0x602A), 0x3F5C },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x6F12), 0x0549 },
	{ CCI_REG16(0x6F12), 0x0448 },
	{ CCI_REG16(0x6F12), 0x054A },
	{ CCI_REG16(0x6F12), 0xC1F8 },
	{ CCI_REG16(0x6F12), 0x5005 },
	{ CCI_REG16(0x6F12), 0x101A },
	{ CCI_REG16(0x6F12), 0xA1F8 },
	{ CCI_REG16(0x6F12), 0x5405 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0xCFB9 },
	{ CCI_REG16(0x6F12), 0x2000 },
	{ CCI_REG16(0x6F12), 0x4470 },
	{ CCI_REG16(0x6F12), 0x2000 },
	{ CCI_REG16(0x6F12), 0x2E30 },
	{ CCI_REG16(0x6F12), 0x2000 },
	{ CCI_REG16(0x6F12), 0x6E00 },
	{ CCI_REG16(0x6F12), 0x2DE9 },
	{ CCI_REG16(0x6F12), 0xFF5F },
	{ CCI_REG16(0x6F12), 0xF848 },
	{ CCI_REG16(0x6F12), 0x8B46 },
	{ CCI_REG16(0x6F12), 0x1746 },
	{ CCI_REG16(0x6F12), 0x0068 },
	{ CCI_REG16(0x6F12), 0x9A46 },
	{ CCI_REG16(0x6F12), 0x4FEA },
	{ CCI_REG16(0x6F12), 0x1049 },
	{ CCI_REG16(0x6F12), 0x80B2 },
	{ CCI_REG16(0x6F12), 0x8046 },
	{ CCI_REG16(0x6F12), 0x0146 },
	{ CCI_REG16(0x6F12), 0x0022 },
	{ CCI_REG16(0x6F12), 0x4846 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0x02FA },
	{ CCI_REG16(0x6F12), 0xF24D },
	{ CCI_REG16(0x6F12), 0x95F8 },
	{ CCI_REG16(0x6F12), 0x6D00 },
	{ CCI_REG16(0x6F12), 0x0228 },
	{ CCI_REG16(0x6F12), 0x35D0 },
	{ CCI_REG16(0x6F12), 0x0224 },
	{ CCI_REG16(0x6F12), 0xF04E },
	{ CCI_REG16(0x6F12), 0x5346 },
	{ CCI_REG16(0x6F12), 0xB6F8 },
	{ CCI_REG16(0x6F12), 0xB802 },
	{ CCI_REG16(0x6F12), 0xB0FB },
	{ CCI_REG16(0x6F12), 0xF4F0 },
	{ CCI_REG16(0x6F12), 0xA6F8 },
	{ CCI_REG16(0x6F12), 0xB802 },
	{ CCI_REG16(0x6F12), 0xD5F8 },
	{ CCI_REG16(0x6F12), 0x1411 },
	{ CCI_REG16(0x6F12), 0x06F5 },
	{ CCI_REG16(0x6F12), 0x2E76 },
	{ CCI_REG16(0x6F12), 0x6143 },
	{ CCI_REG16(0x6F12), 0xC5F8 },
	{ CCI_REG16(0x6F12), 0x1411 },
	{ CCI_REG16(0x6F12), 0xB5F8 },
	{ CCI_REG16(0x6F12), 0x8C11 },
	{ CCI_REG16(0x6F12), 0x411A },
	{ CCI_REG16(0x6F12), 0x89B2 },
	{ CCI_REG16(0x6F12), 0x25F8 },
	{ CCI_REG16(0x6F12), 0x981B },
	{ CCI_REG16(0x6F12), 0x35F8 },
	{ CCI_REG16(0x6F12), 0x142C },
	{ CCI_REG16(0x6F12), 0x6243 },
	{ CCI_REG16(0x6F12), 0x521E },
	{ CCI_REG16(0x6F12), 0x00FB },
	{ CCI_REG16(0x6F12), 0x0210 },
	{ CCI_REG16(0x6F12), 0xB5F8 },
	{ CCI_REG16(0x6F12), 0xF210 },
	{ CCI_REG16(0x6F12), 0x07FB },
	{ CCI_REG16(0x6F12), 0x04F2 },
	{ CCI_REG16(0x6F12), 0x0844 },
	{ CCI_REG16(0x6F12), 0xC5F8 },
	{ CCI_REG16(0x6F12), 0xF800 },
	{ CCI_REG16(0x6F12), 0x5946 },
	{ CCI_REG16(0x6F12), 0x0098 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0xDBF9 },
	{ CCI_REG16(0x6F12), 0x3088 },
	{ CCI_REG16(0x6F12), 0x4146 },
	{ CCI_REG16(0x6F12), 0x6043 },
	{ CCI_REG16(0x6F12), 0x3080 },
	{ CCI_REG16(0x6F12), 0xE86F },
	{ CCI_REG16(0x6F12), 0x0122 },
	{ CCI_REG16(0x6F12), 0xB0FB },
	{ CCI_REG16(0x6F12), 0xF4F0 },
	{ CCI_REG16(0x6F12), 0xE867 },
	{ CCI_REG16(0x6F12), 0x04B0 },
	{ CCI_REG16(0x6F12), 0x4846 },
	{ CCI_REG16(0x6F12), 0xBDE8 },
	{ CCI_REG16(0x6F12), 0xF05F },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0xC7B9 },
	{ CCI_REG16(0x6F12), 0x0124 },
	{ CCI_REG16(0x6F12), 0xC8E7 },
	{ CCI_REG16(0x6F12), 0x2DE9 },
	{ CCI_REG16(0x6F12), 0xF041 },
	{ CCI_REG16(0x6F12), 0x8046 },
	{ CCI_REG16(0x6F12), 0xD148 },
	{ CCI_REG16(0x6F12), 0x0022 },
	{ CCI_REG16(0x6F12), 0x4168 },
	{ CCI_REG16(0x6F12), 0x0D0C },
	{ CCI_REG16(0x6F12), 0x8EB2 },
	{ CCI_REG16(0x6F12), 0x3146 },
	{ CCI_REG16(0x6F12), 0x2846 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0xB9F9 },
	{ CCI_REG16(0x6F12), 0xD04C },
	{ CCI_REG16(0x6F12), 0xCE4F },
	{ CCI_REG16(0x6F12), 0x2078 },
	{ CCI_REG16(0x6F12), 0x97F8 },
	{ CCI_REG16(0x6F12), 0x8B12 },
	{ CCI_REG16(0x6F12), 0x10FB },
	{ CCI_REG16(0x6F12), 0x01F0 },
	{ CCI_REG16(0x6F12), 0x2070 },
	{ CCI_REG16(0x6F12), 0x4046 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0xB8F9 },
	{ CCI_REG16(0x6F12), 0x2078 },
	{ CCI_REG16(0x6F12), 0x97F8 },
	{ CCI_REG16(0x6F12), 0x8B12 },
	{ CCI_REG16(0x6F12), 0x0122 },
	{ CCI_REG16(0x6F12), 0xB0FB },
	{ CCI_REG16(0x6F12), 0xF1F0 },
	{ CCI_REG16(0x6F12), 0x2070 },
	{ CCI_REG16(0x6F12), 0x3146 },
	{ CCI_REG16(0x6F12), 0x2846 },
	{ CCI_REG16(0x6F12), 0xBDE8 },
	{ CCI_REG16(0x6F12), 0xF041 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0xA1B9 },
	{ CCI_REG16(0x6F12), 0x2DE9 },
	{ CCI_REG16(0x6F12), 0xFF47 },
	{ CCI_REG16(0x6F12), 0x8146 },
	{ CCI_REG16(0x6F12), 0xBF48 },
	{ CCI_REG16(0x6F12), 0x1746 },
	{ CCI_REG16(0x6F12), 0x8846 },
	{ CCI_REG16(0x6F12), 0x8068 },
	{ CCI_REG16(0x6F12), 0x1C46 },
	{ CCI_REG16(0x6F12), 0x85B2 },
	{ CCI_REG16(0x6F12), 0x060C },
	{ CCI_REG16(0x6F12), 0x0022 },
	{ CCI_REG16(0x6F12), 0x2946 },
	{ CCI_REG16(0x6F12), 0x3046 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0x92F9 },
	{ CCI_REG16(0x6F12), 0x2346 },
	{ CCI_REG16(0x6F12), 0x3A46 },
	{ CCI_REG16(0x6F12), 0x4146 },
	{ CCI_REG16(0x6F12), 0x4846 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0x9BF9 },
	{ CCI_REG16(0x6F12), 0xBA4A },
	{ CCI_REG16(0x6F12), 0x9088 },
	{ CCI_REG16(0x6F12), 0xF0B3 },
	{ CCI_REG16(0x6F12), 0xB748 },
	{ CCI_REG16(0x6F12), 0x90F8 },
	{ CCI_REG16(0x6F12), 0xBA10 },
	{ CCI_REG16(0x6F12), 0xD1B3 },
	{ CCI_REG16(0x6F12), 0xD0F8 },
	{ CCI_REG16(0x6F12), 0x2801 },
	{ CCI_REG16(0x6F12), 0x1168 },
	{ CCI_REG16(0x6F12), 0x8842 },
	{ CCI_REG16(0x6F12), 0x00D3 },
	{ CCI_REG16(0x6F12), 0x0846 },
	{ CCI_REG16(0x6F12), 0x010A },
	{ CCI_REG16(0x6F12), 0xB1FA },
	{ CCI_REG16(0x6F12), 0x81F0 },
	{ CCI_REG16(0x6F12), 0xC0F1 },
	{ CCI_REG16(0x6F12), 0x1700 },
	{ CCI_REG16(0x6F12), 0xC140 },
	{ CCI_REG16(0x6F12), 0x02EB },
	{ CCI_REG16(0x6F12), 0x4000 },
	{ CCI_REG16(0x6F12), 0xC9B2 },
	{ CCI_REG16(0x6F12), 0x0389 },
	{ CCI_REG16(0x6F12), 0xC288 },
	{ CCI_REG16(0x6F12), 0x9B1A },
	{ CCI_REG16(0x6F12), 0x4B43 },
	{ CCI_REG16(0x6F12), 0x8033 },
	{ CCI_REG16(0x6F12), 0x02EB },
	{ CCI_REG16(0x6F12), 0x2322 },
	{ CCI_REG16(0x6F12), 0x0092 },
	{ CCI_REG16(0x6F12), 0x438A },
	{ CCI_REG16(0x6F12), 0x028A },
	{ CCI_REG16(0x6F12), 0x9B1A },
	{ CCI_REG16(0x6F12), 0x4B43 },
	{ CCI_REG16(0x6F12), 0x8033 },
	{ CCI_REG16(0x6F12), 0x02EB },
	{ CCI_REG16(0x6F12), 0x2322 },
	{ CCI_REG16(0x6F12), 0x0192 },
	{ CCI_REG16(0x6F12), 0x838B },
	{ CCI_REG16(0x6F12), 0x428B },
	{ CCI_REG16(0x6F12), 0x9B1A },
	{ CCI_REG16(0x6F12), 0x4B43 },
	{ CCI_REG16(0x6F12), 0x8033 },
	{ CCI_REG16(0x6F12), 0x02EB },
	{ CCI_REG16(0x6F12), 0x2322 },
	{ CCI_REG16(0x6F12), 0x0292 },
	{ CCI_REG16(0x6F12), 0xC28C },
	{ CCI_REG16(0x6F12), 0x808C },
	{ CCI_REG16(0x6F12), 0x121A },
	{ CCI_REG16(0x6F12), 0x4A43 },
	{ CCI_REG16(0x6F12), 0x8032 },
	{ CCI_REG16(0x6F12), 0x00EB },
	{ CCI_REG16(0x6F12), 0x2220 },
	{ CCI_REG16(0x6F12), 0x0390 },
	{ CCI_REG16(0x6F12), 0x0022 },
	{ CCI_REG16(0x6F12), 0x6846 },
	{ CCI_REG16(0x6F12), 0x54F8 },
	{ CCI_REG16(0x6F12), 0x2210 },
	{ CCI_REG16(0x6F12), 0x50F8 },
	{ CCI_REG16(0x6F12), 0x2230 },
	{ CCI_REG16(0x6F12), 0x5943 },
	{ CCI_REG16(0x6F12), 0x090B },
	{ CCI_REG16(0x6F12), 0x44F8 },
	{ CCI_REG16(0x6F12), 0x2210 },
	{ CCI_REG16(0x6F12), 0x521C },
	{ CCI_REG16(0x6F12), 0x00E0 },
	{ CCI_REG16(0x6F12), 0x01E0 },
	{ CCI_REG16(0x6F12), 0x042A },
	{ CCI_REG16(0x6F12), 0xF2D3 },
	{ CCI_REG16(0x6F12), 0x04B0 },
	{ CCI_REG16(0x6F12), 0x2946 },
	{ CCI_REG16(0x6F12), 0x3046 },
	{ CCI_REG16(0x6F12), 0xBDE8 },
	{ CCI_REG16(0x6F12), 0xF047 },
	{ CCI_REG16(0x6F12), 0x0122 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0x3FB9 },
	{ CCI_REG16(0x6F12), 0x2DE9 },
	{ CCI_REG16(0x6F12), 0xF041 },
	{ CCI_REG16(0x6F12), 0x954C },
	{ CCI_REG16(0x6F12), 0x9349 },
	{ CCI_REG16(0x6F12), 0x0646 },
	{ CCI_REG16(0x6F12), 0x94F8 },
	{ CCI_REG16(0x6F12), 0x6970 },
	{ CCI_REG16(0x6F12), 0x8988 },
	{ CCI_REG16(0x6F12), 0x94F8 },
	{ CCI_REG16(0x6F12), 0x8120 },
	{ CCI_REG16(0x6F12), 0x0020 },
	{ CCI_REG16(0x6F12), 0xC1B1 },
	{ CCI_REG16(0x6F12), 0x2146 },
	{ CCI_REG16(0x6F12), 0xD1F8 },
	{ CCI_REG16(0x6F12), 0x9410 },
	{ CCI_REG16(0x6F12), 0x72B1 },
	{ CCI_REG16(0x6F12), 0x8FB1 },
	{ CCI_REG16(0x6F12), 0x0846 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0x3FF9 },
	{ CCI_REG16(0x6F12), 0x0546 },
	{ CCI_REG16(0x6F12), 0xE06F },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0x3BF9 },
	{ CCI_REG16(0x6F12), 0x8542 },
	{ CCI_REG16(0x6F12), 0x02D2 },
	{ CCI_REG16(0x6F12), 0xD4F8 },
	{ CCI_REG16(0x6F12), 0x9400 },
	{ CCI_REG16(0x6F12), 0x26E0 },
	{ CCI_REG16(0x6F12), 0xE06F },
	{ CCI_REG16(0x6F12), 0x24E0 },
	{ CCI_REG16(0x6F12), 0x002F },
	{ CCI_REG16(0x6F12), 0xFBD1 },
	{ CCI_REG16(0x6F12), 0x002A },
	{ CCI_REG16(0x6F12), 0x24D0 },
	{ CCI_REG16(0x6F12), 0x0846 },
	{ CCI_REG16(0x6F12), 0x1EE0 },
	{ CCI_REG16(0x6F12), 0x8149 },
	{ CCI_REG16(0x6F12), 0x0D8E },
	{ CCI_REG16(0x6F12), 0x496B },
	{ CCI_REG16(0x6F12), 0x4B42 },
	{ CCI_REG16(0x6F12), 0x77B1 },
	{ CCI_REG16(0x6F12), 0x8148 },
	{ CCI_REG16(0x6F12), 0x806F },
	{ CCI_REG16(0x6F12), 0x10E0 },
	{ CCI_REG16(0x6F12), 0x4242 },
	{ CCI_REG16(0x6F12), 0x00E0 },
	{ CCI_REG16(0x6F12), 0x0246 },
	{ CCI_REG16(0x6F12), 0x0029 },
	{ CCI_REG16(0x6F12), 0x0FDB },
	{ CCI_REG16(0x6F12), 0x8A42 },
	{ CCI_REG16(0x6F12), 0x0FDD },
	{ CCI_REG16(0x6F12), 0x3046 },
	{ CCI_REG16(0x6F12), 0xBDE8 },
	{ CCI_REG16(0x6F12), 0xF041 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0x1FB9 },
	{ CCI_REG16(0x6F12), 0x002A },
	{ CCI_REG16(0x6F12), 0x0CD0 },
	{ CCI_REG16(0x6F12), 0x7848 },
	{ CCI_REG16(0x6F12), 0xD0F8 },
	{ CCI_REG16(0x6F12), 0x8C00 },
	{ CCI_REG16(0x6F12), 0x25B1 },
	{ CCI_REG16(0x6F12), 0x0028 },
	{ CCI_REG16(0x6F12), 0xEDDA },
	{ CCI_REG16(0x6F12), 0xEAE7 },
	{ CCI_REG16(0x6F12), 0x1946 },
	{ CCI_REG16(0x6F12), 0xEDE7 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0x17F9 },
	{ CCI_REG16(0x6F12), 0xE060 },
	{ CCI_REG16(0x6F12), 0x0120 },
	{ CCI_REG16(0x6F12), 0xBDE8 },
	{ CCI_REG16(0x6F12), 0xF081 },
	{ CCI_REG16(0x6F12), 0x2DE9 },
	{ CCI_REG16(0x6F12), 0xF35F },
	{ CCI_REG16(0x6F12), 0xDFF8 },
	{ CCI_REG16(0x6F12), 0xB0A1 },
	{ CCI_REG16(0x6F12), 0x0C46 },
	{ CCI_REG16(0x6F12), 0xBAF8 },
	{ CCI_REG16(0x6F12), 0xBE04 },
	{ CCI_REG16(0x6F12), 0x08B1 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0x0EF9 },
	{ CCI_REG16(0x6F12), 0x6C4E },
	{ CCI_REG16(0x6F12), 0x3088 },
	{ CCI_REG16(0x6F12), 0x0128 },
	{ CCI_REG16(0x6F12), 0x06D1 },
	{ CCI_REG16(0x6F12), 0x002C },
	{ CCI_REG16(0x6F12), 0x04D1 },
	{ CCI_REG16(0x6F12), 0x684D },
	{ CCI_REG16(0x6F12), 0x2889 },
	{ CCI_REG16(0x6F12), 0x18B1 },
	{ CCI_REG16(0x6F12), 0x401E },
	{ CCI_REG16(0x6F12), 0x2881 },
	{ CCI_REG16(0x6F12), 0xBDE8 },
	{ CCI_REG16(0x6F12), 0xFC9F },
	{ CCI_REG16(0x6F12), 0xDFF8 },
	{ CCI_REG16(0x6F12), 0x9891 },
	{ CCI_REG16(0x6F12), 0xD9F8 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x6F12), 0xB0F8 },
	{ CCI_REG16(0x6F12), 0xD602 },
	{ CCI_REG16(0x6F12), 0x38B1 },
	{ CCI_REG16(0x6F12), 0x3089 },
	{ CCI_REG16(0x6F12), 0x401C },
	{ CCI_REG16(0x6F12), 0x80B2 },
	{ CCI_REG16(0x6F12), 0x3081 },
	{ CCI_REG16(0x6F12), 0xFF28 },
	{ CCI_REG16(0x6F12), 0x01D9 },
	{ CCI_REG16(0x6F12), 0xE889 },
	{ CCI_REG16(0x6F12), 0x3081 },
	{ CCI_REG16(0x6F12), 0x6048 },
	{ CCI_REG16(0x6F12), 0x4FF0 },
	{ CCI_REG16(0x6F12), 0x0008 },
	{ CCI_REG16(0x6F12), 0xC6F8 },
	{ CCI_REG16(0x6F12), 0x0C80 },
	{ CCI_REG16(0x6F12), 0xB0F8 },
	{ CCI_REG16(0x6F12), 0x5EB0 },
	{ CCI_REG16(0x6F12), 0x40F2 },
	{ CCI_REG16(0x6F12), 0xFF31 },
	{ CCI_REG16(0x6F12), 0x0B20 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0xEBF8 },
	{ CCI_REG16(0x6F12), 0xD9F8 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x6F12), 0x0027 },
	{ CCI_REG16(0x6F12), 0x3C46 },
	{ CCI_REG16(0x6F12), 0xB0F8 },
	{ CCI_REG16(0x6F12), 0xD412 },
	{ CCI_REG16(0x6F12), 0x21B1 },
	{ CCI_REG16(0x6F12), 0x0098 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0xD2F8 },
	{ CCI_REG16(0x6F12), 0x0746 },
	{ CCI_REG16(0x6F12), 0x0BE0 },
	{ CCI_REG16(0x6F12), 0xB0F8 },
	{ CCI_REG16(0x6F12), 0xD602 },
	{ CCI_REG16(0x6F12), 0x40B1 },
	{ CCI_REG16(0x6F12), 0x3089 },
	{ CCI_REG16(0x6F12), 0xE989 },
	{ CCI_REG16(0x6F12), 0x8842 },
	{ CCI_REG16(0x6F12), 0x04D3 },
	{ CCI_REG16(0x6F12), 0x0098 },
	{ CCI_REG16(0x6F12), 0xFFF7 },
	{ CCI_REG16(0x6F12), 0x6EFF },
	{ CCI_REG16(0x6F12), 0x0746 },
	{ CCI_REG16(0x6F12), 0x0124 },
	{ CCI_REG16(0x6F12), 0x3846 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0xD5F8 },
	{ CCI_REG16(0x6F12), 0xD9F8 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x6F12), 0xB0F8 },
	{ CCI_REG16(0x6F12), 0xD602 },
	{ CCI_REG16(0x6F12), 0x08B9 },
	{ CCI_REG16(0x6F12), 0xA6F8 },
	{ CCI_REG16(0x6F12), 0x0280 },
	{ CCI_REG16(0x6F12), 0xC7B3 },
	{ CCI_REG16(0x6F12), 0x4746 },
	{ CCI_REG16(0x6F12), 0xA6F8 },
	{ CCI_REG16(0x6F12), 0x0880 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0xCDF8 },
	{ CCI_REG16(0x6F12), 0xF068 },
	{ CCI_REG16(0x6F12), 0x3061 },
	{ CCI_REG16(0x6F12), 0x688D },
	{ CCI_REG16(0x6F12), 0x50B3 },
	{ CCI_REG16(0x6F12), 0xA88D },
	{ CCI_REG16(0x6F12), 0x50BB },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0xCAF8 },
	{ CCI_REG16(0x6F12), 0xA889 },
	{ CCI_REG16(0x6F12), 0x20B3 },
	{ CCI_REG16(0x6F12), 0x1CB3 },
	{ CCI_REG16(0x6F12), 0x706B },
	{ CCI_REG16(0x6F12), 0xAA88 },
	{ CCI_REG16(0x6F12), 0xDAF8 },
	{ CCI_REG16(0x6F12), 0x0815 },
	{ CCI_REG16(0x6F12), 0xCAB1 },
	{ CCI_REG16(0x6F12), 0x8842 },
	{ CCI_REG16(0x6F12), 0x0CDB },
	{ CCI_REG16(0x6F12), 0x90FB },
	{ CCI_REG16(0x6F12), 0xF1F3 },
	{ CCI_REG16(0x6F12), 0x90FB },
	{ CCI_REG16(0x6F12), 0xF1F2 },
	{ CCI_REG16(0x6F12), 0x01FB },
	{ CCI_REG16(0x6F12), 0x1303 },
	{ CCI_REG16(0x6F12), 0xB3EB },
	{ CCI_REG16(0x6F12), 0x610F },
	{ CCI_REG16(0x6F12), 0x00DD },
	{ CCI_REG16(0x6F12), 0x521C },
	{ CCI_REG16(0x6F12), 0x01FB },
	{ CCI_REG16(0x6F12), 0x1200 },
	{ CCI_REG16(0x6F12), 0x0BE0 },
	{ CCI_REG16(0x6F12), 0x91FB },
	{ CCI_REG16(0x6F12), 0xF0F3 },
	{ CCI_REG16(0x6F12), 0x91FB },
	{ CCI_REG16(0x6F12), 0xF0F2 },
	{ CCI_REG16(0x6F12), 0x00FB },
	{ CCI_REG16(0x6F12), 0x1313 },
	{ CCI_REG16(0x6F12), 0xB3EB },
	{ CCI_REG16(0x6F12), 0x600F },
	{ CCI_REG16(0x6F12), 0x00DD },
	{ CCI_REG16(0x6F12), 0x521C },
	{ CCI_REG16(0x6F12), 0x5043 },
	{ CCI_REG16(0x6F12), 0x401A },
	{ CCI_REG16(0x6F12), 0xF168 },
	{ CCI_REG16(0x6F12), 0x01EB },
	{ CCI_REG16(0x6F12), 0x4000 },
	{ CCI_REG16(0x6F12), 0xF060 },
	{ CCI_REG16(0x6F12), 0xA88D },
	{ CCI_REG16(0x6F12), 0x10B1 },
	{ CCI_REG16(0x6F12), 0xF089 },
	{ CCI_REG16(0x6F12), 0x3087 },
	{ CCI_REG16(0x6F12), 0xAF85 },
	{ CCI_REG16(0x6F12), 0x5846 },
	{ CCI_REG16(0x6F12), 0xBDE8 },
	{ CCI_REG16(0x6F12), 0xFC5F },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0x9EB8 },
	{ CCI_REG16(0x6F12), 0x70B5 },
	{ CCI_REG16(0x6F12), 0x2349 },
	{ CCI_REG16(0x6F12), 0x0446 },
	{ CCI_REG16(0x6F12), 0x0020 },
	{ CCI_REG16(0x6F12), 0xC1F8 },
	{ CCI_REG16(0x6F12), 0x3005 },
	{ CCI_REG16(0x6F12), 0x1E48 },
	{ CCI_REG16(0x6F12), 0x0022 },
	{ CCI_REG16(0x6F12), 0xC168 },
	{ CCI_REG16(0x6F12), 0x0D0C },
	{ CCI_REG16(0x6F12), 0x8EB2 },
	{ CCI_REG16(0x6F12), 0x3146 },
	{ CCI_REG16(0x6F12), 0x2846 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0x53F8 },
	{ CCI_REG16(0x6F12), 0x2046 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0x91F8 },
	{ CCI_REG16(0x6F12), 0x3146 },
	{ CCI_REG16(0x6F12), 0x2846 },
	{ CCI_REG16(0x6F12), 0xBDE8 },
	{ CCI_REG16(0x6F12), 0x7040 },
	{ CCI_REG16(0x6F12), 0x0122 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0x49B8 },
	{ CCI_REG16(0x6F12), 0x10B5 },
	{ CCI_REG16(0x6F12), 0x0022 },
	{ CCI_REG16(0x6F12), 0xAFF2 },
	{ CCI_REG16(0x6F12), 0x9731 },
	{ CCI_REG16(0x6F12), 0x1C48 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0x88F8 },
	{ CCI_REG16(0x6F12), 0x114C },
	{ CCI_REG16(0x6F12), 0x0122 },
	{ CCI_REG16(0x6F12), 0xAFF2 },
	{ CCI_REG16(0x6F12), 0x0D31 },
	{ CCI_REG16(0x6F12), 0x2060 },
	{ CCI_REG16(0x6F12), 0x1948 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0x80F8 },
	{ CCI_REG16(0x6F12), 0x0022 },
	{ CCI_REG16(0x6F12), 0xAFF2 },
	{ CCI_REG16(0x6F12), 0xD121 },
	{ CCI_REG16(0x6F12), 0x6060 },
	{ CCI_REG16(0x6F12), 0x1648 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0x79F8 },
	{ CCI_REG16(0x6F12), 0x0022 },
	{ CCI_REG16(0x6F12), 0xAFF2 },
	{ CCI_REG16(0x6F12), 0x1D21 },
	{ CCI_REG16(0x6F12), 0xA060 },
	{ CCI_REG16(0x6F12), 0x1448 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0x72F8 },
	{ CCI_REG16(0x6F12), 0x0022 },
	{ CCI_REG16(0x6F12), 0xAFF2 },
	{ CCI_REG16(0x6F12), 0x9511 },
	{ CCI_REG16(0x6F12), 0x1248 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0x6CF8 },
	{ CCI_REG16(0x6F12), 0x0022 },
	{ CCI_REG16(0x6F12), 0xAFF2 },
	{ CCI_REG16(0x6F12), 0x7B01 },
	{ CCI_REG16(0x6F12), 0x1048 },
	{ CCI_REG16(0x6F12), 0x00F0 },
	{ CCI_REG16(0x6F12), 0x66F8 },
	{ CCI_REG16(0x6F12), 0xE060 },
	{ CCI_REG16(0x6F12), 0x10BD },
	{ CCI_REG16(0x6F12), 0x2000 },
	{ CCI_REG16(0x6F12), 0x4460 },
	{ CCI_REG16(0x6F12), 0x2000 },
	{ CCI_REG16(0x6F12), 0x2C30 },
	{ CCI_REG16(0x6F12), 0x2000 },
	{ CCI_REG16(0x6F12), 0x2E30 },
	{ CCI_REG16(0x6F12), 0x2000 },
	{ CCI_REG16(0x6F12), 0x2580 },
	{ CCI_REG16(0x6F12), 0x2000 },
	{ CCI_REG16(0x6F12), 0x6000 },
	{ CCI_REG16(0x6F12), 0x2000 },
	{ CCI_REG16(0x6F12), 0x2BA0 },
	{ CCI_REG16(0x6F12), 0x2000 },
	{ CCI_REG16(0x6F12), 0x3600 },
	{ CCI_REG16(0x6F12), 0x2000 },
	{ CCI_REG16(0x6F12), 0x0890 },
	{ CCI_REG16(0x6F12), 0x4000 },
	{ CCI_REG16(0x6F12), 0x7000 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x6F12), 0x24A7 },
	{ CCI_REG16(0x6F12), 0x0001 },
	{ CCI_REG16(0x6F12), 0x1AF3 },
	{ CCI_REG16(0x6F12), 0x0001 },
	{ CCI_REG16(0x6F12), 0x09BD },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x6F12), 0x576B },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x6F12), 0x57ED },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x6F12), 0xBF8D },
	{ CCI_REG16(0x6F12), 0x4AF6 },
	{ CCI_REG16(0x6F12), 0x293C },
	{ CCI_REG16(0x6F12), 0xC0F2 },
	{ CCI_REG16(0x6F12), 0x000C },
	{ CCI_REG16(0x6F12), 0x6047 },
	{ CCI_REG16(0x6F12), 0x42F2 },
	{ CCI_REG16(0x6F12), 0xA74C },
	{ CCI_REG16(0x6F12), 0xC0F2 },
	{ CCI_REG16(0x6F12), 0x000C },
	{ CCI_REG16(0x6F12), 0x6047 },
	{ CCI_REG16(0x6F12), 0x41F6 },
	{ CCI_REG16(0x6F12), 0xF32C },
	{ CCI_REG16(0x6F12), 0xC0F2 },
	{ CCI_REG16(0x6F12), 0x010C },
	{ CCI_REG16(0x6F12), 0x6047 },
	{ CCI_REG16(0x6F12), 0x40F6 },
	{ CCI_REG16(0x6F12), 0xBD1C },
	{ CCI_REG16(0x6F12), 0xC0F2 },
	{ CCI_REG16(0x6F12), 0x010C },
	{ CCI_REG16(0x6F12), 0x6047 },
	{ CCI_REG16(0x6F12), 0x4AF6 },
	{ CCI_REG16(0x6F12), 0x532C },
	{ CCI_REG16(0x6F12), 0xC0F2 },
	{ CCI_REG16(0x6F12), 0x000C },
	{ CCI_REG16(0x6F12), 0x6047 },
	{ CCI_REG16(0x6F12), 0x45F2 },
	{ CCI_REG16(0x6F12), 0x377C },
	{ CCI_REG16(0x6F12), 0xC0F2 },
	{ CCI_REG16(0x6F12), 0x000C },
	{ CCI_REG16(0x6F12), 0x6047 },
	{ CCI_REG16(0x6F12), 0x45F2 },
	{ CCI_REG16(0x6F12), 0xD56C },
	{ CCI_REG16(0x6F12), 0xC0F2 },
	{ CCI_REG16(0x6F12), 0x000C },
	{ CCI_REG16(0x6F12), 0x6047 },
	{ CCI_REG16(0x6F12), 0x45F2 },
	{ CCI_REG16(0x6F12), 0xC91C },
	{ CCI_REG16(0x6F12), 0xC0F2 },
	{ CCI_REG16(0x6F12), 0x000C },
	{ CCI_REG16(0x6F12), 0x6047 },
	{ CCI_REG16(0x6F12), 0x40F2 },
	{ CCI_REG16(0x6F12), 0xAB2C },
	{ CCI_REG16(0x6F12), 0xC0F2 },
	{ CCI_REG16(0x6F12), 0x000C },
	{ CCI_REG16(0x6F12), 0x6047 },
	{ CCI_REG16(0x6F12), 0x44F6 },
	{ CCI_REG16(0x6F12), 0x897C },
	{ CCI_REG16(0x6F12), 0xC0F2 },
	{ CCI_REG16(0x6F12), 0x000C },
	{ CCI_REG16(0x6F12), 0x6047 },
	{ CCI_REG16(0x6F12), 0x45F2 },
	{ CCI_REG16(0x6F12), 0xA56C },
	{ CCI_REG16(0x6F12), 0xC0F2 },
	{ CCI_REG16(0x6F12), 0x000C },
	{ CCI_REG16(0x6F12), 0x6047 },
	{ CCI_REG16(0x6F12), 0x45F2 },
	{ CCI_REG16(0x6F12), 0xEF6C },
	{ CCI_REG16(0x6F12), 0xC0F2 },
	{ CCI_REG16(0x6F12), 0x000C },
	{ CCI_REG16(0x6F12), 0x6047 },
	{ CCI_REG16(0x6F12), 0x40F2 },
	{ CCI_REG16(0x6F12), 0x6D7C },
	{ CCI_REG16(0x6F12), 0xC0F2 },
	{ CCI_REG16(0x6F12), 0x000C },
	{ CCI_REG16(0x6F12), 0x6047 },
	{ CCI_REG16(0x6F12), 0x4BF6 },
	{ CCI_REG16(0x6F12), 0x8D7C },
	{ CCI_REG16(0x6F12), 0xC0F2 },
	{ CCI_REG16(0x6F12), 0x000C },
	{ CCI_REG16(0x6F12), 0x6047 },
	{ CCI_REG16(0x6F12), 0x4BF2 },
	{ CCI_REG16(0x6F12), 0xAB4C },
	{ CCI_REG16(0x6F12), 0xC0F2 },
	{ CCI_REG16(0x6F12), 0x000C },
	{ CCI_REG16(0x6F12), 0x6047 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x6F12), 0x08D1 },
	{ CCI_REG16(0x6F12), 0x008B },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x6F12), 0x0067 },

	// Res 1
	{ CCI_REG16(0x6028), 0x4000 },
	{ CCI_REG16(0x6214), 0x7971 },
	{ CCI_REG16(0x6218), 0x7150 },
	{ CCI_REG16(0x0344), 0x0008 },
	{ CCI_REG16(0x0346), 0x0008 },
	{ CCI_REG16(0x0348), 0x0FA7 },
	{ CCI_REG16(0x034A), 0x0BBF },
	{ CCI_REG16(0x034C), 0x07D0 },
	{ CCI_REG16(0x034E), 0x05DC },
	{ CCI_REG16(0x0350), 0x0000 },
	{ CCI_REG16(0x0352), 0x0000 },
	{ CCI_REG16(0x0340), 0x0C7C },
	{ CCI_REG16(0x0342), 0x13A0 },
	{ CCI_REG16(0x0900), 0x0112 },
	{ CCI_REG16(0x0380), 0x0001 },
	{ CCI_REG16(0x0382), 0x0001 },
	{ CCI_REG16(0x0384), 0x0001 },
	{ CCI_REG16(0x0386), 0x0003 },
	{ CCI_REG16(0x0404), 0x2000 },
	{ CCI_REG16(0x0402), 0x1010 },
	{ CCI_REG16(0x0136), 0x1800 },
	{ CCI_REG16(0x0304), 0x0006 },
	{ CCI_REG16(0x030C), 0x0000 },
	{ CCI_REG16(0x0306), 0x00F1 },
	{ CCI_REG16(0x0302), 0x0001 },
	{ CCI_REG16(0x0300), 0x0008 },
	{ CCI_REG16(0x030E), 0x0003 },
	{ CCI_REG16(0x0312), 0x0001 },
	{ CCI_REG16(0x0310), 0x0090 },
	{ CCI_REG16(0x6028), 0x2000 },
	{ CCI_REG16(0x602A), 0x1492 },
	{ CCI_REG16(0x6F12), 0x0078 },
	{ CCI_REG16(0x602A), 0x0E4E },
	{ CCI_REG16(0x6F12), 0x0066 },
	{ CCI_REG16(0x6028), 0x4000 },
	{ CCI_REG16(0x0118), 0x0004 },
	{ CCI_REG16(0x021E), 0x0000 },
	{ CCI_REG16(0x6028), 0x2000 },
	{ CCI_REG16(0x602A), 0x2126 },
	{ CCI_REG16(0x6F12), 0x0100 },
	{ CCI_REG16(0x602A), 0x1168 },
	{ CCI_REG16(0x6F12), 0x0020 },
	{ CCI_REG16(0x602A), 0x2DB6 },
	{ CCI_REG16(0x6F12), 0x0001 },
	{ CCI_REG16(0x602A), 0x1668 },
	{ CCI_REG16(0x6F12), 0xFF00 },
	{ CCI_REG16(0x602A), 0x166A },
	{ CCI_REG16(0x6F12), 0xFF00 },
	{ CCI_REG16(0x602A), 0x118A },
	{ CCI_REG16(0x6F12), 0x0802 },
	{ CCI_REG16(0x602A), 0x151E },
	{ CCI_REG16(0x6F12), 0x0002 },
	{ CCI_REG16(0x602A), 0x217E },
	{ CCI_REG16(0x6F12), 0x0001 },
	{ CCI_REG16(0x602A), 0x1520 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x2522 },
	{ CCI_REG16(0x6F12), 0x1004 },
	{ CCI_REG16(0x602A), 0x2524 },
	{ CCI_REG16(0x6F12), 0x0200 },
	{ CCI_REG16(0x602A), 0x2568 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x2588 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x258C },
	{ CCI_REG16(0x6F12), 0x1111 },
	{ CCI_REG16(0x602A), 0x25A6 },
	{ CCI_REG16(0x6F12), 0x0001 },
	{ CCI_REG16(0x602A), 0x252C },
	{ CCI_REG16(0x6F12), 0x7801 },
	{ CCI_REG16(0x602A), 0x252E },
	{ CCI_REG16(0x6F12), 0x7805 },
	{ CCI_REG16(0x602A), 0x25A8 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x25AC },
	{ CCI_REG16(0x6F12), 0x1111 },
	{ CCI_REG16(0x602A), 0x25B0 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x25B4 },
	{ CCI_REG16(0x6F12), 0x1111 },
	{ CCI_REG16(0x602A), 0x15A4 },
	{ CCI_REG16(0x6F12), 0x0641 },
	{ CCI_REG16(0x602A), 0x15A6 },
	{ CCI_REG16(0x6F12), 0x0145 },
	{ CCI_REG16(0x602A), 0x15A8 },
	{ CCI_REG16(0x6F12), 0x0149 },
	{ CCI_REG16(0x602A), 0x15AA },
	{ CCI_REG16(0x6F12), 0x064D },
	{ CCI_REG16(0x602A), 0x15AC },
	{ CCI_REG16(0x6F12), 0x0651 },
	{ CCI_REG16(0x602A), 0x15AE },
	{ CCI_REG16(0x6F12), 0x0155 },
	{ CCI_REG16(0x602A), 0x15B0 },
	{ CCI_REG16(0x6F12), 0x0159 },
	{ CCI_REG16(0x602A), 0x15B2 },
	{ CCI_REG16(0x6F12), 0x065D },
	{ CCI_REG16(0x602A), 0x15B4 },
	{ CCI_REG16(0x6F12), 0x0661 },
	{ CCI_REG16(0x602A), 0x15B6 },
	{ CCI_REG16(0x6F12), 0x0165 },
	{ CCI_REG16(0x602A), 0x15B8 },
	{ CCI_REG16(0x6F12), 0x0169 },
	{ CCI_REG16(0x602A), 0x15BA },
	{ CCI_REG16(0x6F12), 0x066D },
	{ CCI_REG16(0x602A), 0x15BC },
	{ CCI_REG16(0x6F12), 0x0671 },
	{ CCI_REG16(0x602A), 0x15BE },
	{ CCI_REG16(0x6F12), 0x0175 },
	{ CCI_REG16(0x602A), 0x15C0 },
	{ CCI_REG16(0x6F12), 0x0179 },
	{ CCI_REG16(0x602A), 0x15C2 },
	{ CCI_REG16(0x6F12), 0x067D },
	{ CCI_REG16(0x602A), 0x15C4 },
	{ CCI_REG16(0x6F12), 0x0641 },
	{ CCI_REG16(0x602A), 0x15C6 },
	{ CCI_REG16(0x6F12), 0x0145 },
	{ CCI_REG16(0x602A), 0x15C8 },
	{ CCI_REG16(0x6F12), 0x0149 },
	{ CCI_REG16(0x602A), 0x15CA },
	{ CCI_REG16(0x6F12), 0x064D },
	{ CCI_REG16(0x602A), 0x15CC },
	{ CCI_REG16(0x6F12), 0x0651 },
	{ CCI_REG16(0x602A), 0x15CE },
	{ CCI_REG16(0x6F12), 0x0155 },
	{ CCI_REG16(0x602A), 0x15D0 },
	{ CCI_REG16(0x6F12), 0x0159 },
	{ CCI_REG16(0x602A), 0x15D2 },
	{ CCI_REG16(0x6F12), 0x065D },
	{ CCI_REG16(0x602A), 0x15D4 },
	{ CCI_REG16(0x6F12), 0x0661 },
	{ CCI_REG16(0x602A), 0x15D6 },
	{ CCI_REG16(0x6F12), 0x0165 },
	{ CCI_REG16(0x602A), 0x15D8 },
	{ CCI_REG16(0x6F12), 0x0169 },
	{ CCI_REG16(0x602A), 0x15DA },
	{ CCI_REG16(0x6F12), 0x066D },
	{ CCI_REG16(0x602A), 0x15DC },
	{ CCI_REG16(0x6F12), 0x0671 },
	{ CCI_REG16(0x602A), 0x15DE },
	{ CCI_REG16(0x6F12), 0x0175 },
	{ CCI_REG16(0x602A), 0x15E0 },
	{ CCI_REG16(0x6F12), 0x0179 },
	{ CCI_REG16(0x602A), 0x15E2 },
	{ CCI_REG16(0x6F12), 0x067D },
	{ CCI_REG16(0x602A), 0x1A50 },
	{ CCI_REG16(0x6F12), 0x0004 },
	{ CCI_REG16(0x602A), 0x1A54 },
	{ CCI_REG16(0x6F12), 0x0100 },
	{ CCI_REG16(0x6028), 0x4000 },
	{ CCI_REG16(0x0D00), 0x0101 },
	{ CCI_REG16(0x0D02), 0x0001 },
	{ CCI_REG16(0x0114), 0x0300 },
	{ CCI_REG16(0xF486), 0x0000 },
	{ CCI_REG16(0xF488), 0x0000 },
	{ CCI_REG16(0xF48A), 0x0000 },
	{ CCI_REG16(0xF48C), 0x0000 },
	{ CCI_REG16(0xF48E), 0x0000 },
	{ CCI_REG16(0xF490), 0x0000 },
	{ CCI_REG16(0xF492), 0x0000 },
	{ CCI_REG16(0xF494), 0x0000 },
	{ CCI_REG16(0xF496), 0x0000 },
	{ CCI_REG16(0xF498), 0x0000 },
	{ CCI_REG16(0xF49A), 0x0000 },
	{ CCI_REG16(0xF49C), 0x0000 },
	{ CCI_REG16(0xF49E), 0x0000 },
	{ CCI_REG16(0xF4A0), 0x0000 },
	{ CCI_REG16(0xF4A2), 0x0000 },
	{ CCI_REG16(0xF4A4), 0x0000 },
	{ CCI_REG16(0xF4A6), 0x0000 },
	{ CCI_REG16(0xF4A8), 0x0000 },
	{ CCI_REG16(0xF4AA), 0x0000 },
	{ CCI_REG16(0xF4AC), 0x0000 },
	{ CCI_REG16(0xF4AE), 0x0000 },
	{ CCI_REG16(0xF4B0), 0x0000 },
	{ CCI_REG16(0xF4B2), 0x0000 },
	{ CCI_REG16(0xF4B4), 0x0000 },
	{ CCI_REG16(0xF4B6), 0x0000 },
	{ CCI_REG16(0xF4B8), 0x0000 },
	{ CCI_REG16(0xF4BA), 0x0000 },
	{ CCI_REG16(0xF4BC), 0x0000 },
	{ CCI_REG16(0xF4BE), 0x0000 },
	{ CCI_REG16(0xF4C0), 0x0000 },
	{ CCI_REG16(0xF4C2), 0x0000 },
	{ CCI_REG16(0xF4C4), 0x0000 },
	{ CCI_REG16(0x0202), 0x0010 },
	{ CCI_REG16(0x0226), 0x0010 },
	{ CCI_REG16(0x0204), 0x0020 },
	{ CCI_REG16(0x0B06), 0x0101 },
	{ CCI_REG16(0x6028), 0x2000 },
	{ CCI_REG16(0x602A), 0x107A },
	{ CCI_REG16(0x6F12), 0x1D00 },
	{ CCI_REG16(0x602A), 0x1074 },
	{ CCI_REG16(0x6F12), 0x1D00 },
	{ CCI_REG16(0x602A), 0x0E7C },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x1120 },
	{ CCI_REG16(0x6F12), 0x0200 },
	{ CCI_REG16(0x602A), 0x1122 },
	{ CCI_REG16(0x6F12), 0x0028 },
	{ CCI_REG16(0x602A), 0x1128 },
	{ CCI_REG16(0x6F12), 0x0604 },
	{ CCI_REG16(0x602A), 0x1AC0 },
	{ CCI_REG16(0x6F12), 0x0200 },
	{ CCI_REG16(0x602A), 0x1AC2 },
	{ CCI_REG16(0x6F12), 0x0002 },
	{ CCI_REG16(0x602A), 0x1494 },
	{ CCI_REG16(0x6F12), 0x3D68 },
	{ CCI_REG16(0x602A), 0x1498 },
	{ CCI_REG16(0x6F12), 0xF10D },
	{ CCI_REG16(0x602A), 0x1488 },
	{ CCI_REG16(0x6F12), 0x0F04 },
	{ CCI_REG16(0x602A), 0x148A },
	{ CCI_REG16(0x6F12), 0x0F0B },
	{ CCI_REG16(0x602A), 0x150E },
	{ CCI_REG16(0x6F12), 0x00C2 },
	{ CCI_REG16(0x602A), 0x1510 },
	{ CCI_REG16(0x6F12), 0xC0AF },
	{ CCI_REG16(0x602A), 0x1512 },
	{ CCI_REG16(0x6F12), 0x0080 },
	{ CCI_REG16(0x602A), 0x1486 },
	{ CCI_REG16(0x6F12), 0x1430 },
	{ CCI_REG16(0x602A), 0x1490 },
	{ CCI_REG16(0x6F12), 0x4D09 },
	{ CCI_REG16(0x602A), 0x149E },
	{ CCI_REG16(0x6F12), 0x01C4 },
	{ CCI_REG16(0x602A), 0x11CC },
	{ CCI_REG16(0x6F12), 0x0008 },
	{ CCI_REG16(0x602A), 0x11CE },
	{ CCI_REG16(0x6F12), 0x000B },
	{ CCI_REG16(0x602A), 0x11D0 },
	{ CCI_REG16(0x6F12), 0x0003 },
	{ CCI_REG16(0x602A), 0x11DA },
	{ CCI_REG16(0x6F12), 0x0012 },
	{ CCI_REG16(0x602A), 0x11E6 },
	{ CCI_REG16(0x6F12), 0x002A },
	{ CCI_REG16(0x602A), 0x125E },
	{ CCI_REG16(0x6F12), 0x0048 },
	{ CCI_REG16(0x602A), 0x11F4 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x11F8 },
	{ CCI_REG16(0x6F12), 0x0016 },
	{ CCI_REG16(0x6028), 0x4000 },
	{ CCI_REG16(0xF444), 0x05BF },
	{ CCI_REG16(0xF44A), 0x0008 },
	{ CCI_REG16(0xF44E), 0x0012 },
	{ CCI_REG16(0xF46E), 0x74C0 },
	{ CCI_REG16(0xF470), 0x2809 },
	{ CCI_REG16(0x6028), 0x2000 },
	{ CCI_REG16(0x602A), 0x1CAA },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x1CAC },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x1CAE },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x1CB0 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x1CB2 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x1CB4 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x1CB6 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x1CB8 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x1CBA },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x1CBC },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x1CBE },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x1CC0 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x1CC2 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x1CC4 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x1CC6 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x1CC8 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x6000 },
	{ CCI_REG16(0x6F12), 0x000F },
	{ CCI_REG16(0x602A), 0x6002 },
	{ CCI_REG16(0x6F12), 0xFFFF },
	{ CCI_REG16(0x602A), 0x6004 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x6006 },
	{ CCI_REG16(0x6F12), 0x1000 },
	{ CCI_REG16(0x602A), 0x6008 },
	{ CCI_REG16(0x6F12), 0x1000 },
	{ CCI_REG16(0x602A), 0x600A },
	{ CCI_REG16(0x6F12), 0x1000 },
	{ CCI_REG16(0x602A), 0x600C },
	{ CCI_REG16(0x6F12), 0x1000 },
	{ CCI_REG16(0x602A), 0x600E },
	{ CCI_REG16(0x6F12), 0x1000 },
	{ CCI_REG16(0x602A), 0x6010 },
	{ CCI_REG16(0x6F12), 0x1000 },
	{ CCI_REG16(0x602A), 0x6012 },
	{ CCI_REG16(0x6F12), 0x1000 },
	{ CCI_REG16(0x602A), 0x6014 },
	{ CCI_REG16(0x6F12), 0x1000 },
	{ CCI_REG16(0x602A), 0x6016 },
	{ CCI_REG16(0x6F12), 0x1000 },
	{ CCI_REG16(0x602A), 0x6018 },
	{ CCI_REG16(0x6F12), 0x1000 },
	{ CCI_REG16(0x602A), 0x601A },
	{ CCI_REG16(0x6F12), 0x1000 },
	{ CCI_REG16(0x602A), 0x601C },
	{ CCI_REG16(0x6F12), 0x1000 },
	{ CCI_REG16(0x602A), 0x601E },
	{ CCI_REG16(0x6F12), 0x1000 },
	{ CCI_REG16(0x602A), 0x6020 },
	{ CCI_REG16(0x6F12), 0x1000 },
	{ CCI_REG16(0x602A), 0x6022 },
	{ CCI_REG16(0x6F12), 0x1000 },
	{ CCI_REG16(0x602A), 0x6024 },
	{ CCI_REG16(0x6F12), 0x1000 },
	{ CCI_REG16(0x602A), 0x6026 },
	{ CCI_REG16(0x6F12), 0x1000 },
	{ CCI_REG16(0x602A), 0x6028 },
	{ CCI_REG16(0x6F12), 0x1000 },
	{ CCI_REG16(0x602A), 0x602A },
	{ CCI_REG16(0x6F12), 0x1000 },
	{ CCI_REG16(0x602A), 0x602C },
	{ CCI_REG16(0x6F12), 0x1000 },
	{ CCI_REG16(0x602A), 0x1144 },
	{ CCI_REG16(0x6F12), 0x0100 },
	{ CCI_REG16(0x602A), 0x1146 },
	{ CCI_REG16(0x6F12), 0x1B00 },
	{ CCI_REG16(0x602A), 0x1080 },
	{ CCI_REG16(0x6F12), 0x0100 },
	{ CCI_REG16(0x602A), 0x1084 },
	{ CCI_REG16(0x6F12), 0x00C0 },
	{ CCI_REG16(0x602A), 0x108A },
	{ CCI_REG16(0x6F12), 0x00C0 },
	{ CCI_REG16(0x602A), 0x1090 },
	{ CCI_REG16(0x6F12), 0x0001 },
	{ CCI_REG16(0x602A), 0x1092 },
	{ CCI_REG16(0x6F12), 0x0000 },
	{ CCI_REG16(0x602A), 0x1094 },
	{ CCI_REG16(0x6F12), 0xA32E },
};

/* Supported sensor mode configurations */
static const struct s5kgm1sp_mode supported_mode = {
	.width = 2000,
	.height = 1500,
	.hblank = 456, // FIXME
	.vblank = 506, // FIXME
	.vblank_min = 506, // FIXME
	.vblank_max = 32420, // FIXME
	.pclk = 619200000, // outputPixelClock?
	.link_freq_idx = 0,
	.code = MEDIA_BUS_FMT_SGRBG10_1X10,
	.reg_list = {
		.num_of_regs = ARRAY_SIZE(mode_2000x1500_regs),
		.regs = mode_2000x1500_regs,
	},
};

/**
 * to_s5kgm1sp() - s5kgm1sp V4L2 sub-device to s5kgm1sp device.
 * @subdev: pointer to s5kgm1sp V4L2 sub-device
 *
 * Return: pointer to s5kgm1sp device
 */
static inline struct s5kgm1sp *to_s5kgm1sp(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct s5kgm1sp, sd);
}

/**
 * s5kgm1sp_update_controls() - Update control ranges based on streaming mode
 * @s5kgm1sp: pointer to s5kgm1sp device
 * @mode: pointer to s5kgm1sp_mode sensor mode
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kgm1sp_update_controls(struct s5kgm1sp *s5kgm1sp,
				  const struct s5kgm1sp_mode *mode)
{
	int ret;

	ret = __v4l2_ctrl_s_ctrl(s5kgm1sp->link_freq_ctrl, mode->link_freq_idx);
	if (ret)
		return ret;

	ret = __v4l2_ctrl_s_ctrl(s5kgm1sp->hblank_ctrl, mode->hblank);
	if (ret)
		return ret;

	return __v4l2_ctrl_modify_range(s5kgm1sp->vblank_ctrl, mode->vblank_min,
					mode->vblank_max, 1, mode->vblank);
}

/**
 * s5kgm1sp_update_exp_gain() - Set updated exposure and gain
 * @s5kgm1sp: pointer to s5kgm1sp device
 * @exposure: updated exposure value
 * @gain: updated analog gain value
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kgm1sp_update_exp_gain(struct s5kgm1sp *s5kgm1sp, u32 exposure, u32 gain)
{
	u32 lpfr;
	int ret;

	lpfr = s5kgm1sp->vblank + s5kgm1sp->cur_mode->height;

	dev_dbg(s5kgm1sp->dev, "Set exp %u, analog gain %u, lpfr %u\n",
		exposure, gain, lpfr);

	cci_write(s5kgm1sp->regmap, S5KGM1SP_REG_HOLD, 1, &ret);
	if (ret)
		return ret;

	cci_write(s5kgm1sp->regmap, S5KGM1SP_REG_LPFR, lpfr, &ret);
	if (ret)
		goto error_release_group_hold;

	cci_write(s5kgm1sp->regmap, S5KGM1SP_REG_EXPOSURE, exposure, &ret);
	if (ret)
		goto error_release_group_hold;

	cci_write(s5kgm1sp->regmap, S5KGM1SP_REG_ANALOG_GAIN, gain, &ret);

error_release_group_hold:
	cci_write(s5kgm1sp->regmap, S5KGM1SP_REG_HOLD, 0, NULL);

	return ret;
}

/**
 * s5kgm1sp_set_ctrl() - Set subdevice control
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
static int s5kgm1sp_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct s5kgm1sp *s5kgm1sp =
		container_of(ctrl->handler, struct s5kgm1sp, ctrl_handler);
	u32 analog_gain;
	u32 exposure;
	int ret;

	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		s5kgm1sp->vblank = s5kgm1sp->vblank_ctrl->val;

		dev_dbg(s5kgm1sp->dev, "Received vblank %u, new lpfr %u\n",
			s5kgm1sp->vblank,
			s5kgm1sp->vblank + s5kgm1sp->cur_mode->height);

		ret = __v4l2_ctrl_modify_range(s5kgm1sp->exp_ctrl,
					       S5KGM1SP_EXPOSURE_MIN,
					       s5kgm1sp->vblank +
					       s5kgm1sp->cur_mode->height -
					       S5KGM1SP_EXPOSURE_OFFSET,
					       1, S5KGM1SP_EXPOSURE_DEFAULT);
		break;
	case V4L2_CID_EXPOSURE:
		/* Set controls only if sensor is in power on state */
		if (!pm_runtime_get_if_in_use(s5kgm1sp->dev))
			return 0;

		exposure = ctrl->val;
		analog_gain = s5kgm1sp->again_ctrl->val;

		dev_dbg(s5kgm1sp->dev, "Received exp %u, analog gain %u\n",
			exposure, analog_gain);

		ret = s5kgm1sp_update_exp_gain(s5kgm1sp, exposure, analog_gain);

		pm_runtime_put(s5kgm1sp->dev);

		break;
	default:
		dev_err(s5kgm1sp->dev, "Invalid control %d\n", ctrl->id);
		ret = -EINVAL;
	}

	return ret;
}

/* V4l2 subdevice control ops*/
static const struct v4l2_ctrl_ops s5kgm1sp_ctrl_ops = {
	.s_ctrl = s5kgm1sp_set_ctrl,
};

/**
 * s5kgm1sp_enum_mbus_code() - Enumerate V4L2 sub-device mbus codes
 * @sd: pointer to s5kgm1sp V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @code: V4L2 sub-device code enumeration need to be filled
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kgm1sp_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = supported_mode.code;

	return 0;
}

/**
 * s5kgm1sp_enum_frame_size() - Enumerate V4L2 sub-device frame sizes
 * @sd: pointer to s5kgm1sp V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fsize: V4L2 sub-device size enumeration need to be filled
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kgm1sp_enum_frame_size(struct v4l2_subdev *sd,
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
 * s5kgm1sp_fill_pad_format() - Fill subdevice pad format
 *                            from selected sensor mode
 * @s5kgm1sp: pointer to s5kgm1sp device
 * @mode: pointer to s5kgm1sp_mode sensor mode
 * @fmt: V4L2 sub-device format need to be filled
 */
static void s5kgm1sp_fill_pad_format(struct s5kgm1sp *s5kgm1sp,
				   const struct s5kgm1sp_mode *mode,
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
 * s5kgm1sp_get_pad_format() - Get subdevice pad format
 * @sd: pointer to s5kgm1sp V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fmt: V4L2 sub-device format need to be set
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kgm1sp_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct s5kgm1sp *s5kgm1sp = to_s5kgm1sp(sd);

	mutex_lock(&s5kgm1sp->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *framefmt;

		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		fmt->format = *framefmt;
	} else {
		s5kgm1sp_fill_pad_format(s5kgm1sp, s5kgm1sp->cur_mode, fmt);
	}

	mutex_unlock(&s5kgm1sp->mutex);

	return 0;
}

/**
 * s5kgm1sp_set_pad_format() - Set subdevice pad format
 * @sd: pointer to s5kgm1sp V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fmt: V4L2 sub-device format need to be set
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kgm1sp_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct s5kgm1sp *s5kgm1sp = to_s5kgm1sp(sd);
	const struct s5kgm1sp_mode *mode;
	int ret = 0;

	mutex_lock(&s5kgm1sp->mutex);

	mode = &supported_mode;
	s5kgm1sp_fill_pad_format(s5kgm1sp, mode, fmt);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *framefmt;

		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		*framefmt = fmt->format;
	} else {
		ret = s5kgm1sp_update_controls(s5kgm1sp, mode);
		if (!ret)
			s5kgm1sp->cur_mode = mode;
	}

	mutex_unlock(&s5kgm1sp->mutex);

	return ret;
}

/**
 * s5kgm1sp_init_state() - Initialize sub-device state
 * @sd: pointer to s5kgm1sp V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kgm1sp_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	struct s5kgm1sp *s5kgm1sp = to_s5kgm1sp(sd);
	struct v4l2_subdev_format fmt = { 0 };

	fmt.which = sd_state ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	s5kgm1sp_fill_pad_format(s5kgm1sp, &supported_mode, &fmt);

	return s5kgm1sp_set_pad_format(sd, sd_state, &fmt);
}

/**
 * s5kgm1sp_start_streaming() - Start sensor stream
 * @s5kgm1sp: pointer to s5kgm1sp device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kgm1sp_start_streaming(struct s5kgm1sp *s5kgm1sp)
{
	const struct s5kgm1sp_reg_list *reg_list;
	int ret;

	/* Write sensor mode registers */
	reg_list = &s5kgm1sp->cur_mode->reg_list;
	ret = cci_multi_reg_write(s5kgm1sp->regmap, reg_list->regs,
				  reg_list->num_of_regs, NULL);
	if (ret) {
		dev_err(s5kgm1sp->dev, "fail to write initial registers\n");
		return ret;
	}

	/* Setup handler will write actual exposure and gain */
	ret =  __v4l2_ctrl_handler_setup(s5kgm1sp->sd.ctrl_handler);
	if (ret) {
		dev_err(s5kgm1sp->dev, "fail to setup handler\n");
		return ret;
	}

	/* Delay is required before streaming*/
	usleep_range(7400, 8000);

	/* Start streaming */
	cci_write(s5kgm1sp->regmap, S5KGM1SP_REG_MODE_SELECT, S5KGM1SP_MODE_STREAMING, &ret);
	if (ret) {
		dev_err(s5kgm1sp->dev, "fail to start streaming\n");
		return ret;
	}

	return 0;
}

/**
 * s5kgm1sp_stop_streaming() - Stop sensor stream
 * @s5kgm1sp: pointer to s5kgm1sp device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kgm1sp_stop_streaming(struct s5kgm1sp *s5kgm1sp)
{
	return cci_write(s5kgm1sp->regmap, S5KGM1SP_REG_MODE_SELECT,
			 S5KGM1SP_MODE_STANDBY, NULL);
}

/**
 * s5kgm1sp_set_stream() - Enable sensor streaming
 * @sd: pointer to s5kgm1sp subdevice
 * @enable: set to enable sensor streaming
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kgm1sp_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct s5kgm1sp *s5kgm1sp = to_s5kgm1sp(sd);
	int ret;

	mutex_lock(&s5kgm1sp->mutex);

	if (enable) {
		ret = pm_runtime_resume_and_get(s5kgm1sp->dev);
		if (ret)
			goto error_unlock;

		ret = s5kgm1sp_start_streaming(s5kgm1sp);
		if (ret)
			goto error_power_off;
	} else {
		s5kgm1sp_stop_streaming(s5kgm1sp);
		pm_runtime_put(s5kgm1sp->dev);
	}

	mutex_unlock(&s5kgm1sp->mutex);

	return 0;

error_power_off:
	pm_runtime_put(s5kgm1sp->dev);
error_unlock:
	mutex_unlock(&s5kgm1sp->mutex);

	return ret;
}

/**
 * s5kgm1sp_detect() - Detect s5kgm1sp sensor
 * @s5kgm1sp: pointer to s5kgm1sp device
 *
 * Return: 0 if successful, -EIO if sensor id does not match
 */
static int s5kgm1sp_detect(struct s5kgm1sp *s5kgm1sp)
{
	int ret;
	u64 val;

	ret = cci_read(s5kgm1sp->regmap, S5KGM1SP_REG_CHIP_ID, &val, NULL);
	if (ret)
		return ret;

	if (val != S5KGM1SP_CHIP_ID) {
		dev_err(s5kgm1sp->dev, "chip id mismatch: %x!=%llx\n",
			S5KGM1SP_CHIP_ID, val);
		return -ENXIO;
	}

	return 0;
}

/**
 * s5kgm1sp_parse_hw_config() - Parse HW configuration and check if supported
 * @s5kgm1sp: pointer to s5kgm1sp device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kgm1sp_parse_hw_config(struct s5kgm1sp *s5kgm1sp)
{
	struct fwnode_handle *fwnode = dev_fwnode(s5kgm1sp->dev);
	struct v4l2_fwnode_endpoint bus_cfg = {};
	struct fwnode_handle *ep;
	unsigned long rate;
	unsigned int i;
	int ret;

	if (!fwnode)
		return -ENXIO;

	/* Request optional reset pin */
	s5kgm1sp->reset_gpio = devm_gpiod_get_optional(s5kgm1sp->dev, "reset",
						     GPIOD_OUT_LOW);
	if (IS_ERR(s5kgm1sp->reset_gpio)) {
		dev_err(s5kgm1sp->dev, "failed to get reset gpio %ld\n",
			PTR_ERR(s5kgm1sp->reset_gpio));
		return PTR_ERR(s5kgm1sp->reset_gpio);
	}

	/* Get sensor input clock */
	s5kgm1sp->inclk = devm_clk_get(s5kgm1sp->dev, NULL);
	if (IS_ERR(s5kgm1sp->inclk)) {
		dev_err(s5kgm1sp->dev, "could not get inclk\n");
		return PTR_ERR(s5kgm1sp->inclk);
	}

	rate = clk_get_rate(s5kgm1sp->inclk);
	if (rate != S5KGM1SP_INCLK_RATE) {
		dev_err(s5kgm1sp->dev, "inclk frequency mismatch\n");
		return -EINVAL;
	}

	/* Get optional DT defined regulators */
	for (i = 0; i < ARRAY_SIZE(s5kgm1sp_supply_names); i++)
		s5kgm1sp->supplies[i].supply = s5kgm1sp_supply_names[i];

	ret = devm_regulator_bulk_get(s5kgm1sp->dev,
				      ARRAY_SIZE(s5kgm1sp_supply_names),
				      s5kgm1sp->supplies);
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
		dev_err(s5kgm1sp->dev, "selected bus-type is not supported\n");
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != S5KGM1SP_NUM_DATA_LANES) {
		dev_err(s5kgm1sp->dev,
			"number of CSI2 data lanes %d is not supported\n",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	if (!bus_cfg.nr_of_link_frequencies) {
		dev_err(s5kgm1sp->dev, "no link frequencies defined\n");
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	for (i = 0; i < bus_cfg.nr_of_link_frequencies; i++)
		if (bus_cfg.link_frequencies[i] == S5KGM1SP_LINK_FREQ)
			goto done_endpoint_free;

	ret = -EINVAL;

done_endpoint_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

/* V4l2 subdevice ops */
static const struct v4l2_subdev_video_ops s5kgm1sp_video_ops = {
	.s_stream = s5kgm1sp_set_stream,
};

static const struct v4l2_subdev_pad_ops s5kgm1sp_pad_ops = {
	.enum_mbus_code = s5kgm1sp_enum_mbus_code,
	.enum_frame_size = s5kgm1sp_enum_frame_size,
	.get_fmt = s5kgm1sp_get_pad_format,
	.set_fmt = s5kgm1sp_set_pad_format,
};

static const struct v4l2_subdev_ops s5kgm1sp_subdev_ops = {
	.video = &s5kgm1sp_video_ops,
	.pad = &s5kgm1sp_pad_ops,
};

static const struct v4l2_subdev_internal_ops s5kgm1sp_internal_ops = {
	.init_state = s5kgm1sp_init_state,
};

/**
 * s5kgm1sp_power_on() - Sensor power on sequence
 * @dev: pointer to i2c device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kgm1sp_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct s5kgm1sp *s5kgm1sp = to_s5kgm1sp(sd);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(s5kgm1sp_supply_names),
				    s5kgm1sp->supplies);
	if (ret < 0) {
		dev_err(dev, "failed to enable regulators\n");
		return ret;
	}

	gpiod_set_value_cansleep(s5kgm1sp->reset_gpio, 0);

	ret = clk_prepare_enable(s5kgm1sp->inclk);
	if (ret) {
		dev_err(s5kgm1sp->dev, "fail to enable inclk\n");
		goto error_reset;
	}

	usleep_range(1000, 1200);

	return 0;

error_reset:
	gpiod_set_value_cansleep(s5kgm1sp->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(s5kgm1sp_supply_names),
			       s5kgm1sp->supplies);

	return ret;
}

/**
 * s5kgm1sp_power_off() - Sensor power off sequence
 * @dev: pointer to i2c device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kgm1sp_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct s5kgm1sp *s5kgm1sp = to_s5kgm1sp(sd);

	clk_disable_unprepare(s5kgm1sp->inclk);

	gpiod_set_value_cansleep(s5kgm1sp->reset_gpio, 1);

	regulator_bulk_disable(ARRAY_SIZE(s5kgm1sp_supply_names),
			       s5kgm1sp->supplies);

	return 0;
}

/**
 * s5kgm1sp_init_controls() - Initialize sensor subdevice controls
 * @s5kgm1sp: pointer to s5kgm1sp device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kgm1sp_init_controls(struct s5kgm1sp *s5kgm1sp)
{
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr = &s5kgm1sp->ctrl_handler;
	const struct s5kgm1sp_mode *mode = s5kgm1sp->cur_mode;
	u32 lpfr;
	int ret;

	/* set properties from fwnode (e.g. rotation, orientation) */
	ret = v4l2_fwnode_device_parse(s5kgm1sp->dev, &props);
	if (ret)
		return ret;

	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 8);
	if (ret)
		return ret;

	/* Serialize controls with sensor device */
	ctrl_hdlr->lock = &s5kgm1sp->mutex;

	/* Initialize exposure and gain */
	lpfr = mode->vblank + mode->height;
	s5kgm1sp->exp_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					     &s5kgm1sp_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     S5KGM1SP_EXPOSURE_MIN,
					     lpfr - S5KGM1SP_EXPOSURE_OFFSET,
					     S5KGM1SP_EXPOSURE_STEP,
					     S5KGM1SP_EXPOSURE_DEFAULT);

	s5kgm1sp->again_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					       &s5kgm1sp_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN,
					       S5KGM1SP_ANA_GAIN_MIN,
					       S5KGM1SP_ANA_GAIN_MAX,
					       S5KGM1SP_ANA_GAIN_STEP,
					       S5KGM1SP_ANA_GAIN_DEFAULT);

	v4l2_ctrl_cluster(2, &s5kgm1sp->exp_ctrl);

	s5kgm1sp->vblank_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
						&s5kgm1sp_ctrl_ops,
						V4L2_CID_VBLANK,
						mode->vblank_min,
						mode->vblank_max,
						1, mode->vblank);

	/* Read only controls */
	s5kgm1sp->pclk_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					      &s5kgm1sp_ctrl_ops,
					      V4L2_CID_PIXEL_RATE,
					      mode->pclk, mode->pclk,
					      1, mode->pclk);

	s5kgm1sp->link_freq_ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr,
							&s5kgm1sp_ctrl_ops,
							V4L2_CID_LINK_FREQ,
							ARRAY_SIZE(link_freq) -
							1,
							mode->link_freq_idx,
							link_freq);
	if (s5kgm1sp->link_freq_ctrl)
		s5kgm1sp->link_freq_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	s5kgm1sp->hblank_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
						&s5kgm1sp_ctrl_ops,
						V4L2_CID_HBLANK,
						S5KGM1SP_REG_MIN,
						S5KGM1SP_REG_MAX,
						1, mode->hblank);
	if (s5kgm1sp->hblank_ctrl)
		s5kgm1sp->hblank_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &s5kgm1sp_ctrl_ops, &props);

	if (ctrl_hdlr->error) {
		dev_err(s5kgm1sp->dev, "control init failed: %d\n",
			ctrl_hdlr->error);
		v4l2_ctrl_handler_free(ctrl_hdlr);
		return ctrl_hdlr->error;
	}

	s5kgm1sp->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

/**
 * s5kgm1sp_probe() - I2C client device binding
 * @client: pointer to i2c client device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int s5kgm1sp_probe(struct i2c_client *client)
{
	struct s5kgm1sp *s5kgm1sp;
	int ret;

	s5kgm1sp = devm_kzalloc(&client->dev, sizeof(*s5kgm1sp), GFP_KERNEL);
	if (!s5kgm1sp)
		return -ENOMEM;

	s5kgm1sp->dev = &client->dev;

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&s5kgm1sp->sd, client, &s5kgm1sp_subdev_ops);
	s5kgm1sp->sd.internal_ops = &s5kgm1sp_internal_ops;

	ret = s5kgm1sp_parse_hw_config(s5kgm1sp);
	if (ret) {
		dev_err(s5kgm1sp->dev, "HW configuration is not supported\n");
		return ret;
	}

	mutex_init(&s5kgm1sp->mutex);

	s5kgm1sp->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(s5kgm1sp->regmap))
		return dev_err_probe(s5kgm1sp->dev, PTR_ERR(s5kgm1sp->regmap),
				     "failed to initialize CCI\n");

	ret = s5kgm1sp_power_on(s5kgm1sp->dev);
	if (ret) {
		dev_err(s5kgm1sp->dev, "failed to power-on the sensor\n");
		goto error_mutex_destroy;
	}

	/* Check module identity */
	ret = s5kgm1sp_detect(s5kgm1sp);
	if (ret) {
		dev_err(s5kgm1sp->dev, "failed to find sensor: %d\n", ret);
		goto error_power_off;
	}

	/* Set default mode to max resolution */
	s5kgm1sp->cur_mode = &supported_mode;
	s5kgm1sp->vblank = s5kgm1sp->cur_mode->vblank;

	ret = s5kgm1sp_init_controls(s5kgm1sp);
	if (ret) {
		dev_err(s5kgm1sp->dev, "failed to init controls: %d\n", ret);
		goto error_power_off;
	}

	/* Initialize subdev */
	s5kgm1sp->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	s5kgm1sp->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	s5kgm1sp->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&s5kgm1sp->sd.entity, 1, &s5kgm1sp->pad);
	if (ret) {
		dev_err(s5kgm1sp->dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&s5kgm1sp->sd);
	if (ret < 0) {
		dev_err(s5kgm1sp->dev,
			"failed to register async subdev: %d\n", ret);
		goto error_media_entity;
	}

	pm_runtime_set_active(s5kgm1sp->dev);
	pm_runtime_enable(s5kgm1sp->dev);
	pm_runtime_idle(s5kgm1sp->dev);

	return 0;

error_media_entity:
	media_entity_cleanup(&s5kgm1sp->sd.entity);
error_handler_free:
	v4l2_ctrl_handler_free(s5kgm1sp->sd.ctrl_handler);
error_power_off:
	s5kgm1sp_power_off(s5kgm1sp->dev);
error_mutex_destroy:
	mutex_destroy(&s5kgm1sp->mutex);

	return ret;
}

static void s5kgm1sp_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5kgm1sp *s5kgm1sp = to_s5kgm1sp(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		s5kgm1sp_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	mutex_destroy(&s5kgm1sp->mutex);
}

static const struct dev_pm_ops s5kgm1sp_pm_ops = {
	SET_RUNTIME_PM_OPS(s5kgm1sp_power_off, s5kgm1sp_power_on, NULL)
};

static const struct of_device_id s5kgm1sp_of_match[] = {
	{ .compatible = "samsung,s5kgm1sp" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s5kgm1sp_of_match);

static struct i2c_driver s5kgm1sp_driver = {
	.probe = s5kgm1sp_probe,
	.remove = s5kgm1sp_remove,
	.driver = {
		.name = "s5kgm1sp",
		.pm = &s5kgm1sp_pm_ops,
		.of_match_table = s5kgm1sp_of_match,
	},
};

module_i2c_driver(s5kgm1sp_driver);

MODULE_DESCRIPTION("Samsung S5KGM1SP sensor driver");
MODULE_LICENSE("GPL");
