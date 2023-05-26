// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct mem_lg_novatek_35521 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	bool prepared;
};

static inline
struct mem_lg_novatek_35521 *to_mem_lg_novatek_35521(struct drm_panel *panel)
{
	return container_of(panel, struct mem_lg_novatek_35521, panel);
}

static void mem_lg_novatek_35521_reset(struct mem_lg_novatek_35521 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(15000, 16000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(150);
}

static int mem_lg_novatek_35521_on(struct mem_lg_novatek_35521 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq(dsi, 0xff, 0xaa, 0x55, 0xa5, 0x80);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xb1, 0x68, 0x21);
	mipi_dsi_dcs_write_seq(dsi, 0xb5, 0xc8);
	mipi_dsi_dcs_write_seq(dsi, 0xb6, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0xbb, 0x74, 0x44);
	mipi_dsi_dcs_write_seq(dsi, 0xbd, 0x02, 0x68, 0x20, 0x20, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0xf7, 0x47);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x17);
	mipi_dsi_dcs_write_seq(dsi, 0xf4, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0xd9, 0x00, 0x01, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xb3, 0x1f, 0x1f);
	mipi_dsi_dcs_write_seq(dsi, 0xb4, 0x28, 0x28);
	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0x35, 0x35);
	mipi_dsi_dcs_write_seq(dsi, 0xba, 0x25, 0x25);
	mipi_dsi_dcs_write_seq(dsi, 0xbc, 0x93, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xbd, 0xa3, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xca, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0xee, 0x03);
	mipi_dsi_dcs_write_seq(dsi, 0xb0,
			       0x00, 0x00, 0x00, 0x16, 0x00, 0x34, 0x00, 0x50,
			       0x00, 0x64, 0x00, 0x87, 0x00, 0xa6, 0x00, 0xd5);
	mipi_dsi_dcs_write_seq(dsi, 0xb1,
			       0x00, 0xfb, 0x01, 0x39, 0x01, 0x69, 0x01, 0xb7,
			       0x01, 0xf5, 0x01, 0xf7, 0x02, 0x30, 0x02, 0x74);
	mipi_dsi_dcs_write_seq(dsi, 0xb2,
			       0x02, 0x97, 0x02, 0xcd, 0x02, 0xf2, 0x03, 0x14,
			       0x03, 0x25, 0x03, 0x37, 0x03, 0x44, 0x03, 0x5f);
	mipi_dsi_dcs_write_seq(dsi, 0xb3, 0x03, 0x6f, 0x03, 0x9f);
	mipi_dsi_dcs_write_seq(dsi, 0xb4,
			       0x00, 0x00, 0x00, 0x16, 0x00, 0x34, 0x00, 0x50,
			       0x00, 0x64, 0x00, 0x87, 0x00, 0xa6, 0x00, 0xd5);
	mipi_dsi_dcs_write_seq(dsi, 0xb5,
			       0x00, 0xfb, 0x01, 0x39, 0x01, 0x69, 0x01, 0xb7,
			       0x01, 0xf5, 0x01, 0xf7, 0x02, 0x30, 0x02, 0x74);
	mipi_dsi_dcs_write_seq(dsi, 0xb6,
			       0x02, 0x97, 0x02, 0xcd, 0x02, 0xf2, 0x03, 0x14,
			       0x03, 0x25, 0x03, 0x37, 0x03, 0x44, 0x03, 0x5f);
	mipi_dsi_dcs_write_seq(dsi, 0xb7, 0x03, 0x6f, 0x03, 0x9f);
	mipi_dsi_dcs_write_seq(dsi, 0xb8,
			       0x00, 0x00, 0x00, 0x0b, 0x00, 0x24, 0x00, 0x3c,
			       0x00, 0x4b, 0x00, 0x71, 0x00, 0x8b, 0x00, 0xbd);
	mipi_dsi_dcs_write_seq(dsi, 0xb9,
			       0x00, 0xe5, 0x01, 0x27, 0x01, 0x5a, 0x01, 0xab,
			       0x01, 0xec, 0x01, 0xee, 0x02, 0x2a, 0x02, 0x6f);
	mipi_dsi_dcs_write_seq(dsi, 0xba,
			       0x02, 0x94, 0x02, 0xcd, 0x02, 0xf6, 0x03, 0x1f,
			       0x03, 0x36, 0x03, 0x52, 0x03, 0x69, 0x03, 0x8f);
	mipi_dsi_dcs_write_seq(dsi, 0xbb, 0x03, 0x9f, 0x03, 0xff);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x03);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x22, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xb1, 0x22, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xb2, 0x05, 0x00, 0xb0, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xb3, 0x05, 0x00, 0xb0, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xb4, 0x05, 0x00, 0xb0, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xb5, 0x05, 0x00, 0xb0, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xba, 0x53, 0x00, 0xb0, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xbb, 0x53, 0x00, 0xb0, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xbc, 0x53, 0x00, 0xb0, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xbd, 0x53, 0x00, 0xb0, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xc0, 0x00, 0x60, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xc1, 0x00, 0x00, 0x60, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xc2, 0x00, 0x00, 0x34, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xc3, 0x00, 0x00, 0x34, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xc4, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0xc5, 0xc0);
	mipi_dsi_dcs_write_seq(dsi, 0xc6, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xc7, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x17, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xb1, 0x17, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xb2, 0x17, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xb3, 0x17, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xb4, 0x17, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xb5, 0x17, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xb6, 0x17, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xb7, 0x17, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xb8, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xba, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xbb, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, 0xbc, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0xbd, 0x03, 0x03, 0x00, 0x03, 0x03);
	mipi_dsi_dcs_write_seq(dsi, 0xc0, 0x0b);
	mipi_dsi_dcs_write_seq(dsi, 0xc1, 0x09);
	mipi_dsi_dcs_write_seq(dsi, 0xc2, 0xa6);
	mipi_dsi_dcs_write_seq(dsi, 0xc3, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xc4, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xc5, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0xc6, 0x22);
	mipi_dsi_dcs_write_seq(dsi, 0xc7, 0x03);
	mipi_dsi_dcs_write_seq(dsi, 0xc8, 0x07, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0xc9, 0x03, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0xca, 0x01, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0xcb, 0x01, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0xcc, 0x00, 0x00, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0xcd, 0x00, 0x00, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0xce, 0x00, 0x00, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0xcf, 0x00, 0x00, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0xd0, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xd1, 0x00, 0x05, 0x01, 0x07, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0xd2, 0x10, 0x05, 0x05, 0x03, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0xd3, 0x20, 0x00, 0x43, 0x07, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0xd4, 0x30, 0x00, 0x43, 0x07, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0xe5, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xe6, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xe7, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xe8, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xe9, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xea, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xeb, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xec, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xed, 0x33);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x34, 0x34);
	mipi_dsi_dcs_write_seq(dsi, 0xb1, 0x34, 0x34);
	mipi_dsi_dcs_write_seq(dsi, 0xb2, 0x2d, 0x2e);
	mipi_dsi_dcs_write_seq(dsi, 0xb3, 0x34, 0x34);
	mipi_dsi_dcs_write_seq(dsi, 0xb4, 0x29, 0x2a);
	mipi_dsi_dcs_write_seq(dsi, 0xb5, 0x13, 0x11);
	mipi_dsi_dcs_write_seq(dsi, 0xb6, 0x19, 0x17);
	mipi_dsi_dcs_write_seq(dsi, 0xb7, 0x01, 0x03);
	mipi_dsi_dcs_write_seq(dsi, 0xb8, 0x34, 0x31);
	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0x31, 0x31);
	mipi_dsi_dcs_write_seq(dsi, 0xba, 0x31, 0x31);
	mipi_dsi_dcs_write_seq(dsi, 0xbb, 0x31, 0x34);
	mipi_dsi_dcs_write_seq(dsi, 0xbc, 0x02, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xbd, 0x16, 0x18);
	mipi_dsi_dcs_write_seq(dsi, 0xbe, 0x10, 0x12);
	mipi_dsi_dcs_write_seq(dsi, 0xbf, 0x2a, 0x29);
	mipi_dsi_dcs_write_seq(dsi, 0xc0, 0x34, 0x34);
	mipi_dsi_dcs_write_seq(dsi, 0xc1, 0x2e, 0x2d);
	mipi_dsi_dcs_write_seq(dsi, 0xc2, 0x34, 0x34);
	mipi_dsi_dcs_write_seq(dsi, 0xc3, 0x34, 0x34);
	mipi_dsi_dcs_write_seq(dsi, 0xc4, 0x34, 0x34);
	mipi_dsi_dcs_write_seq(dsi, 0xc5, 0x34, 0x34);
	mipi_dsi_dcs_write_seq(dsi, 0xc6, 0x2e, 0x2d);
	mipi_dsi_dcs_write_seq(dsi, 0xc7, 0x34, 0x34);
	mipi_dsi_dcs_write_seq(dsi, 0xc8, 0x29, 0x2a);
	mipi_dsi_dcs_write_seq(dsi, 0xc9, 0x16, 0x18);
	mipi_dsi_dcs_write_seq(dsi, 0xca, 0x10, 0x12);
	mipi_dsi_dcs_write_seq(dsi, 0xcb, 0x02, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xcc, 0x34, 0x31);
	mipi_dsi_dcs_write_seq(dsi, 0xcd, 0x31, 0x31);
	mipi_dsi_dcs_write_seq(dsi, 0xce, 0x31, 0x31);
	mipi_dsi_dcs_write_seq(dsi, 0xcf, 0x31, 0x34);
	mipi_dsi_dcs_write_seq(dsi, 0xd0, 0x01, 0x03);
	mipi_dsi_dcs_write_seq(dsi, 0xd1, 0x13, 0x11);
	mipi_dsi_dcs_write_seq(dsi, 0xd2, 0x19, 0x17);
	mipi_dsi_dcs_write_seq(dsi, 0xd3, 0x2a, 0x29);
	mipi_dsi_dcs_write_seq(dsi, 0xd4, 0x34, 0x34);
	mipi_dsi_dcs_write_seq(dsi, 0xd5, 0x2d, 0x2e);
	mipi_dsi_dcs_write_seq(dsi, 0xd6, 0x34, 0x34);
	mipi_dsi_dcs_write_seq(dsi, 0xd7, 0x34, 0x34);
	mipi_dsi_dcs_write_seq(dsi, 0xd8, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xd9, 0x00, 0x00, 0x00, 0x00, 0x00);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(140);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0xff, 0xaa, 0x55, 0xa5, 0x80);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xd1,
			       0x00, 0x07, 0x0b, 0x11, 0x18, 0x20, 0x27, 0x27,
			       0x25, 0x21, 0x1c, 0x14, 0x0c, 0x06, 0x02, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xcc,
			       0x41, 0x36, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xd7,
			       0x30, 0x30, 0x30, 0x28, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xd8,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x28, 0x30, 0x30);
	mipi_dsi_dcs_write_seq(dsi, 0xd3, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xd6, 0x44, 0x44);
	mipi_dsi_dcs_write_seq(dsi, 0xd9, 0x00, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xe7,
			       0xff, 0xfa, 0xf8, 0xf5, 0xee, 0xe1, 0xd5, 0xcd,
			       0xb9, 0xb4);
	mipi_dsi_dcs_write_seq(dsi, 0xf5,
			       0x02, 0x1d, 0x1b, 0x1b, 0x14, 0x14, 0x12, 0x0f,
			       0x12, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0xf5, 0x70);
	mipi_dsi_dcs_write_seq(dsi, 0xe8,
			       0xff, 0xfa, 0xf5, 0xeb, 0xe1, 0xc8, 0xaa, 0x96,
			       0x73, 0x66);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x0c);
	mipi_dsi_dcs_write_seq(dsi, 0xf5, 0x0c);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_POWER_SAVE, 0x82);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS, 0x22);

	return 0;
}

static int mem_lg_novatek_35521_off(struct mem_lg_novatek_35521 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(130);

	return 0;
}

static int mem_lg_novatek_35521_prepare(struct drm_panel *panel)
{
	struct mem_lg_novatek_35521 *ctx = to_mem_lg_novatek_35521(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	mem_lg_novatek_35521_reset(ctx);

	ret = mem_lg_novatek_35521_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	ctx->prepared = true;
	return 0;
}

static int mem_lg_novatek_35521_unprepare(struct drm_panel *panel)
{
	struct mem_lg_novatek_35521 *ctx = to_mem_lg_novatek_35521(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = mem_lg_novatek_35521_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	ctx->prepared = false;
	return 0;
}

static const struct drm_display_mode mem_lg_novatek_35521_mode = {
	.clock = (720 + 45 + 1 + 46) * (1280 + 19 + 1 + 19) * 60 / 1000,
	.hdisplay = 720,
	.hsync_start = 720 + 45,
	.hsync_end = 720 + 45 + 1,
	.htotal = 720 + 45 + 1 + 46,
	.vdisplay = 1280,
	.vsync_start = 1280 + 19,
	.vsync_end = 1280 + 19 + 1,
	.vtotal = 1280 + 19 + 1 + 19,
	.width_mm = 55,
	.height_mm = 98,
};

static int mem_lg_novatek_35521_get_modes(struct drm_panel *panel,
					  struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &mem_lg_novatek_35521_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs mem_lg_novatek_35521_panel_funcs = {
	.prepare = mem_lg_novatek_35521_prepare,
	.unprepare = mem_lg_novatek_35521_unprepare,
	.get_modes = mem_lg_novatek_35521_get_modes,
};

static int mem_lg_novatek_35521_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

// TODO: Check if /sys/class/backlight/.../actual_brightness actually returns
// correct values. If not, remove this function.
static int mem_lg_novatek_35521_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness & 0xff;
}

static const struct backlight_ops mem_lg_novatek_35521_bl_ops = {
	.update_status = mem_lg_novatek_35521_bl_update_status,
	.get_brightness = mem_lg_novatek_35521_bl_get_brightness,
};

static struct backlight_device *
mem_lg_novatek_35521_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 255,
		.max_brightness = 255,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &mem_lg_novatek_35521_bl_ops, &props);
}

static int mem_lg_novatek_35521_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct mem_lg_novatek_35521 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 3;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_HSE |
			  MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &mem_lg_novatek_35521_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ctx->panel.backlight = mem_lg_novatek_35521_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void mem_lg_novatek_35521_remove(struct mipi_dsi_device *dsi)
{
	struct mem_lg_novatek_35521 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id mem_lg_novatek_35521_of_match[] = {
	{ .compatible = "htc,memul-panel-novatek35521" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mem_lg_novatek_35521_of_match);

static struct mipi_dsi_driver mem_lg_novatek_35521_driver = {
	.probe = mem_lg_novatek_35521_probe,
	.remove = mem_lg_novatek_35521_remove,
	.driver = {
		.name = "panel-mem-lg-novatek-35521",
		.of_match_table = mem_lg_novatek_35521_of_match,
	},
};
module_mipi_dsi_driver(mem_lg_novatek_35521_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for LG novatek 720p video mode dsi panel");
MODULE_LICENSE("GPL");
