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

struct td4310plus_e7 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpio;
	bool prepared;
};

static inline struct td4310plus_e7 *to_td4310plus_e7(struct drm_panel *panel)
{
	return container_of(panel, struct td4310plus_e7, panel);
}

static void td4310plus_e7_reset(struct td4310plus_e7 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(30);
}

static int td4310plus_e7_on(struct td4310plus_e7 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq(dsi, 0x11, 0x00);
	msleep(120);
	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x04);
	mipi_dsi_generic_write_seq(dsi, 0xd6, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xc7,
				   0x00, 0x1a, 0x29, 0x3c, 0x4b, 0x57, 0x6f,
				   0x7f, 0x8c, 0x97, 0x49, 0x55, 0x63, 0x77,
				   0x80, 0x8c, 0x9b, 0xa6, 0xb2, 0x00, 0x1a,
				   0x29, 0x3c, 0x4b, 0x57, 0x6f, 0x7f, 0x8c,
				   0x97, 0x49, 0x55, 0x63, 0x77, 0x80, 0x8c,
				   0x9b, 0xa6, 0xb2);
	mipi_dsi_generic_write_seq(dsi, 0xc8,
				   0x03, 0x00, 0x01, 0x01, 0x02, 0xfe, 0x00,
				   0x00, 0xfe, 0xff, 0x02, 0xf3, 0x00, 0x00,
				   0x01, 0xfd, 0x01, 0xee, 0x00, 0x00, 0xff,
				   0x01, 0x01, 0xf6, 0x00, 0x00, 0x01, 0xfe,
				   0x03, 0xec, 0x00, 0x00, 0x01, 0xfc, 0xfe,
				   0xfe, 0x00, 0x00, 0x01, 0x01, 0x02, 0xfe,
				   0x00, 0x00, 0xff, 0xff, 0x02, 0xe9, 0x00,
				   0x00, 0x01, 0xfe, 0x01, 0xcd, 0x00);

	ret = mipi_dsi_dcs_set_display_brightness(dsi, 0x00ff);
	if (ret < 0) {
		dev_err(dev, "Failed to set display brightness: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_POWER_SAVE, 0x00);

	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0) {
		dev_err(dev, "Failed to set tear on: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0x29, 0x00);
	msleep(20);

	return 0;
}

static int td4310plus_e7_off(struct td4310plus_e7 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq(dsi, 0x28, 0x00);
	msleep(20);
	mipi_dsi_dcs_write_seq(dsi, 0x10, 0x00);
	msleep(120);

	return 0;
}

static int td4310plus_e7_prepare(struct drm_panel *panel)
{
	struct td4310plus_e7 *ctx = to_td4310plus_e7(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	td4310plus_e7_reset(ctx);

	ret = td4310plus_e7_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
		return ret;
	}

	ctx->prepared = true;
	return 0;
}

static int td4310plus_e7_unprepare(struct drm_panel *panel)
{
	struct td4310plus_e7 *ctx = to_td4310plus_e7(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = td4310plus_e7_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	ctx->prepared = false;
	return 0;
}

static const struct drm_display_mode td4310plus_e7_mode = {
	.clock = (1080 + 108 + 12 + 60) * (2160 + 6 + 4 + 33) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 108,
	.hsync_end = 1080 + 108 + 12,
	.htotal = 1080 + 108 + 12 + 60,
	.vdisplay = 2160,
	.vsync_start = 2160 + 6,
	.vsync_end = 2160 + 6 + 4,
	.vtotal = 2160 + 6 + 4 + 33,
	.width_mm = 69,
	.height_mm = 122,
};

static int td4310plus_e7_get_modes(struct drm_panel *panel,
				   struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &td4310plus_e7_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs td4310plus_e7_panel_funcs = {
	.prepare = td4310plus_e7_prepare,
	.unprepare = td4310plus_e7_unprepare,
	.get_modes = td4310plus_e7_get_modes,
};

static int td4310plus_e7_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct td4310plus_e7 *ctx;
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
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &td4310plus_e7_panel_funcs,
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

static void td4310plus_e7_remove(struct mipi_dsi_device *dsi)
{
	struct td4310plus_e7 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id td4310plus_e7_of_match[] = {
	{ .compatible = "xiaomi,td4310-fhdplus-e7" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, td4310plus_e7_of_match);

static struct mipi_dsi_driver td4310plus_e7_driver = {
	.probe = td4310plus_e7_probe,
	.remove = td4310plus_e7_remove,
	.driver = {
		.name = "panel-td4310plus-e7",
		.of_match_table = td4310plus_e7_of_match,
	},
};
module_mipi_dsi_driver(td4310plus_e7_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for td4310 fhdplus e7 video mode dsi panel");
MODULE_LICENSE("GPL");
