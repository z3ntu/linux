// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct nt35596_tianma {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpio;
	bool prepared;
};

static inline struct nt35596_tianma *to_nt35596_tianma(struct drm_panel *panel)
{
	return container_of(panel, struct nt35596_tianma, panel);
}

static void nt35596_tianma_reset(struct nt35596_tianma *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
}

static int nt35596_tianma_on(struct nt35596_tianma *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq(dsi, 0xff, 0x04);
	usleep_range(1000, 2000);
	mipi_dsi_generic_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0x08, 0x05);
	mipi_dsi_generic_write_seq(dsi, 0xff, 0x00);
	usleep_range(1000, 2000);
	mipi_dsi_generic_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0x35, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0x36, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0x51, 0xff);
	mipi_dsi_generic_write_seq(dsi, 0x53, 0x2c);
	mipi_dsi_generic_write_seq(dsi, 0x55, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xd3, 0x06);
	mipi_dsi_generic_write_seq(dsi, 0xd4, 0x0e);
	mipi_dsi_generic_write_seq(dsi, 0xff, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0x72, 0x21);
	mipi_dsi_generic_write_seq(dsi, 0x6d, 0x33);
	mipi_dsi_generic_write_seq(dsi, 0xff, 0x05);
	mipi_dsi_generic_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xe7, 0x80);
	mipi_dsi_generic_write_seq(dsi, 0xff, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0x11, 0x00);
	msleep(120);
	mipi_dsi_generic_write_seq(dsi, 0x29, 0x00);
	msleep(20);

	return 0;
}

static int nt35596_tianma_off(struct nt35596_tianma *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq(dsi, 0xff, 0x00);

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	msleep(20);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(60);

	return 0;
}

static int nt35596_tianma_prepare(struct drm_panel *panel)
{
	struct nt35596_tianma *ctx = to_nt35596_tianma(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	nt35596_tianma_reset(ctx);

	ret = nt35596_tianma_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
		return ret;
	}

	ctx->prepared = true;
	return 0;
}

static int nt35596_tianma_unprepare(struct drm_panel *panel)
{
	struct nt35596_tianma *ctx = to_nt35596_tianma(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = nt35596_tianma_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	ctx->prepared = false;
	return 0;
}

static const struct drm_display_mode nt35596_tianma_mode = {
	.clock = (1080 + 96 + 16 + 64) * (1920 + 14 + 2 + 4) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 96,
	.hsync_end = 1080 + 96 + 16,
	.htotal = 1080 + 96 + 16 + 64,
	.vdisplay = 1920,
	.vsync_start = 1920 + 14,
	.vsync_end = 1920 + 14 + 2,
	.vtotal = 1920 + 14 + 2 + 4,
	.width_mm = 69,
	.height_mm = 122,
};

static int nt35596_tianma_get_modes(struct drm_panel *panel,
				    struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &nt35596_tianma_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs nt35596_tianma_panel_funcs = {
	.prepare = nt35596_tianma_prepare,
	.unprepare = nt35596_tianma_unprepare,
	.get_modes = nt35596_tianma_get_modes,
};

static int nt35596_tianma_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct nt35596_tianma *ctx;
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
			  MIPI_DSI_MODE_VIDEO_HSE |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &nt35596_tianma_panel_funcs,
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

static void nt35596_tianma_remove(struct mipi_dsi_device *dsi)
{
	struct nt35596_tianma *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id nt35596_tianma_of_match[] = {
	{ .compatible = "xiaomi,tianma-nt35596" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nt35596_tianma_of_match);

static struct mipi_dsi_driver nt35596_tianma_driver = {
	.probe = nt35596_tianma_probe,
	.remove = nt35596_tianma_remove,
	.driver = {
		.name = "panel-nt35596-tianma",
		.of_match_table = nt35596_tianma_of_match,
	},
};
module_mipi_dsi_driver(nt35596_tianma_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for nt35596 tianma fhd video mode dsi panel");
MODULE_LICENSE("GPL");
