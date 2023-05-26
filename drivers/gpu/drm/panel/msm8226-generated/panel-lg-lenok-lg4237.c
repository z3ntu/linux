// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct lgd_lg4237 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
};

static inline struct lgd_lg4237 *to_lgd_lg4237(struct drm_panel *panel)
{
	return container_of_const(panel, struct lgd_lg4237, panel);
}

static void lgd_lg4237_reset(struct lgd_lg4237 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(50);
}

static int lgd_lg4237_on(struct lgd_lg4237 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 128);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_ADDRESS_MODE, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0xac);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc2, 0x08, 0x80, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5c, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS,
				     0x0b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5f,
				     0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
				     0x80, 0x80, 0x80, 0x80, 0x80, 0x8d, 0x8d,
				     0x8d, 0x8d, 0x8d, 0x8d, 0x8d, 0x8d, 0x8d,
				     0x8d, 0x8d, 0x8d, 0x7f, 0x7f, 0x7f, 0x7f,
				     0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
				     0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
				     0xff, 0xff, 0xff, 0xff, 0xff, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe1,
				     0x32, 0x3c, 0x46, 0x48, 0x4e, 0x46, 0x40,
				     0x30, 0x00, 0xfb, 0xf1, 0xee, 0xe2, 0xf6,
				     0x08, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe2,
				     0x32, 0x3c, 0x46, 0x48, 0x4e, 0x46, 0x40,
				     0x30, 0x00, 0xfb, 0xf1, 0xee, 0xe2, 0xf6,
				     0x08, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe3,
				     0x32, 0x3c, 0x46, 0x48, 0x4e, 0x46, 0x40,
				     0x30, 0x00, 0xfb, 0xf1, 0xee, 0xe2, 0xf6,
				     0x08, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int lgd_lg4237_off(struct lgd_lg4237 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 112);

	return dsi_ctx.accum_err;
}

static int lgd_lg4237_prepare(struct drm_panel *panel)
{
	struct lgd_lg4237 *ctx = to_lgd_lg4237(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	lgd_lg4237_reset(ctx);

	ret = lgd_lg4237_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	return 0;
}

static int lgd_lg4237_unprepare(struct drm_panel *panel)
{
	struct lgd_lg4237 *ctx = to_lgd_lg4237(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = lgd_lg4237_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	return 0;
}

static const struct drm_display_mode lgd_lg4237_mode = {
	.clock = (320 + 164 + 8 + 140) * (320 + 6 + 1 + 1) * 60 / 1000,
	.hdisplay = 320,
	.hsync_start = 320 + 164,
	.hsync_end = 320 + 164 + 8,
	.htotal = 320 + 164 + 8 + 140,
	.vdisplay = 320,
	.vsync_start = 320 + 6,
	.vsync_end = 320 + 6 + 1,
	.vtotal = 320 + 6 + 1 + 1,
	.width_mm = 33,
	.height_mm = 33,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int lgd_lg4237_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &lgd_lg4237_mode);
}

static const struct drm_panel_funcs lgd_lg4237_panel_funcs = {
	.prepare = lgd_lg4237_prepare,
	.unprepare = lgd_lg4237_unprepare,
	.get_modes = lgd_lg4237_get_modes,
};

static int lgd_lg4237_bl_update_status(struct backlight_device *bl)
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
static int lgd_lg4237_bl_get_brightness(struct backlight_device *bl)
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

static const struct backlight_ops lgd_lg4237_bl_ops = {
	.update_status = lgd_lg4237_bl_update_status,
	.get_brightness = lgd_lg4237_bl_get_brightness,
};

static struct backlight_device *
lgd_lg4237_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 255,
		.max_brightness = 255,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &lgd_lg4237_bl_ops, &props);
}

static int lgd_lg4237_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct lgd_lg4237 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct lgd_lg4237, panel,
				   &lgd_lg4237_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 1;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_VIDEO_HSE |
			  MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = lgd_lg4237_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void lgd_lg4237_remove(struct mipi_dsi_device *dsi)
{
	struct lgd_lg4237 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id lgd_lg4237_of_match[] = {
	{ .compatible = "lg,lenok-panel-lg4237" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lgd_lg4237_of_match);

static struct mipi_dsi_driver lgd_lg4237_driver = {
	.probe = lgd_lg4237_probe,
	.remove = lgd_lg4237_remove,
	.driver = {
		.name = "panel-lgd-lg4237",
		.of_match_table = lgd_lg4237_of_match,
	},
};
module_mipi_dsi_driver(lgd_lg4237_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for LG4237 320P OLED command mode dsi panel");
MODULE_LICENSE("GPL");
