// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct ili7807 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpio;
	bool prepared;
};

static inline struct ili7807 *to_ili7807(struct drm_panel *panel)
{
	return container_of(panel, struct ili7807, panel);
}

static void ili7807_reset(struct ili7807 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int ili7807_on(struct ili7807 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x78, 0x07, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0x03, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x04, 0x03);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x34);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x78, 0x07, 0x00);

	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0) {
		dev_err(dev, "Failed to set tear on: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_display_brightness(dsi, 0xff0f);
	if (ret < 0) {
		dev_err(dev, "Failed to set display brightness: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x2c);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x11, 0x00);
	msleep(120);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x78, 0x07, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0xb2, 0x22);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_READ_PPS_START, 0x07);
	mipi_dsi_dcs_write_seq(dsi, 0xa3, 0x1e);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x78, 0x07, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x65, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x66, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x6d, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x78, 0x07, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x29, 0x00);
	msleep(20);

	return 0;
}

static int ili7807_off(struct ili7807 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;

	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x78, 0x07, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x28, 0x00);
	msleep(20);
	mipi_dsi_dcs_write_seq(dsi, 0x10, 0x00);
	msleep(120);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x78, 0x07, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x58, 0x01);

	return 0;
}

static int ili7807_prepare(struct drm_panel *panel)
{
	struct ili7807 *ctx = to_ili7807(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	ili7807_reset(ctx);

	ret = ili7807_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
		return ret;
	}

	ctx->prepared = true;
	return 0;
}

static int ili7807_unprepare(struct drm_panel *panel)
{
	struct ili7807 *ctx = to_ili7807(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = ili7807_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	ctx->prepared = false;
	return 0;
}

static const struct drm_display_mode ili7807_mode = {
	.clock = (1080 + 84 + 24 + 80) * (1920 + 22 + 8 + 16) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 84,
	.hsync_end = 1080 + 84 + 24,
	.htotal = 1080 + 84 + 24 + 80,
	.vdisplay = 1920,
	.vsync_start = 1920 + 22,
	.vsync_end = 1920 + 22 + 8,
	.vtotal = 1920 + 22 + 8 + 16,
	.width_mm = 69,
	.height_mm = 122,
};

static int ili7807_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &ili7807_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs ili7807_panel_funcs = {
	.prepare = ili7807_prepare,
	.unprepare = ili7807_unprepare,
	.get_modes = ili7807_get_modes,
};

static int ili7807_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ili7807 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supplies[0].supply = "vsn";
	ctx->supplies[1].supply = "vsp";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &ili7807_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void ili7807_remove(struct mipi_dsi_device *dsi)
{
	struct ili7807 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id ili7807_of_match[] = {
	{ .compatible = "mdss,ili7807-fhd" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ili7807_of_match);

static struct mipi_dsi_driver ili7807_driver = {
	.probe = ili7807_probe,
	.remove = ili7807_remove,
	.driver = {
		.name = "panel-ili7807",
		.of_match_table = ili7807_of_match,
	},
};
module_mipi_dsi_driver(ili7807_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for ili7807 fhd video mode dsi panel");
MODULE_LICENSE("GPL");
