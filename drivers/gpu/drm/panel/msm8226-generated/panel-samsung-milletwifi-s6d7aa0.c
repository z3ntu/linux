// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct sdc {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	bool prepared;
};

static inline struct sdc *to_sdc(struct drm_panel *panel)
{
	return container_of(panel, struct sdc, panel);
}

static int sdc_on(struct sdc *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x5a, 0x5a);
	usleep_range(1000, 2000);
	mipi_dsi_dcs_write_seq(dsi, 0xf1, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq(dsi, 0xfc, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq(dsi, 0xd0, 0x00, 0x10);
	usleep_range(1000, 2000);
	mipi_dsi_dcs_write_seq(dsi, 0xb6, 0x10);
	usleep_range(1000, 2000);
	mipi_dsi_dcs_write_seq(dsi, 0xc3, 0x40, 0x00, 0x28);
	usleep_range(1000, 2000);
	mipi_dsi_dcs_write_seq(dsi, 0xbc, 0x00, 0x4e, 0xa2);
	usleep_range(1000, 2000);
	mipi_dsi_dcs_write_seq(dsi, 0xfd, 0x16, 0x10, 0x11, 0x23);
	usleep_range(1000, 2000);
	mipi_dsi_dcs_write_seq(dsi, 0xfe, 0x00, 0x02, 0x03, 0x21, 0x00, 0x70);
	usleep_range(1000, 2000);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x26);
	usleep_range(1000, 2000);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_ADDRESS_MODE, 0x04);
	usleep_range(1000, 2000);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);

	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_ADDRESS_MODE, 0x00);
	usleep_range(1000, 2000);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0xa5, 0xa5);
	usleep_range(1000, 2000);
	mipi_dsi_dcs_write_seq(dsi, 0xf1, 0xa5, 0xa5);
	usleep_range(1000, 2000);
	mipi_dsi_dcs_write_seq(dsi, 0xfc, 0x5a, 0x5a);
	usleep_range(1000, 2000);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	usleep_range(1000, 2000);

	return 0;
}

static int sdc_off(struct sdc *ctx)
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
	msleep(64);

	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x5a, 0x5a);
	usleep_range(1000, 2000);
	mipi_dsi_dcs_write_seq(dsi, 0xc3, 0x40, 0x00, 0x20);
	usleep_range(1000, 2000);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	usleep_range(10000, 11000);

	return 0;
}

static int sdc_prepare(struct drm_panel *panel)
{
	struct sdc *ctx = to_sdc(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	ret = sdc_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		return ret;
	}

	ctx->prepared = true;
	return 0;
}

static int sdc_unprepare(struct drm_panel *panel)
{
	struct sdc *ctx = to_sdc(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = sdc_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);


	ctx->prepared = false;
	return 0;
}

static const struct drm_display_mode sdc_mode = {
	.clock = (800 + 16 + 4 + 140) * (1280 + 8 + 4 + 4) * 60 / 1000,
	.hdisplay = 800,
	.hsync_start = 800 + 16,
	.hsync_end = 800 + 16 + 4,
	.htotal = 800 + 16 + 4 + 140,
	.vdisplay = 1280,
	.vsync_start = 1280 + 8,
	.vsync_end = 1280 + 8 + 4,
	.vtotal = 1280 + 8 + 4 + 4,
	.width_mm = 108,
	.height_mm = 172,
};

static int sdc_get_modes(struct drm_panel *panel,
			 struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &sdc_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs sdc_panel_funcs = {
	.prepare = sdc_prepare,
	.unprepare = sdc_unprepare,
	.get_modes = sdc_get_modes,
};

static int sdc_bl_update_status(struct backlight_device *bl)
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
static int sdc_bl_get_brightness(struct backlight_device *bl)
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

static const struct backlight_ops sdc_bl_ops = {
	.update_status = sdc_bl_update_status,
	.get_brightness = sdc_bl_get_brightness,
};

static struct backlight_device *
sdc_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 255,
		.max_brightness = 255,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &sdc_bl_ops, &props);
}

static int sdc_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct sdc *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			  MIPI_DSI_MODE_NO_EOT_PACKET;

	drm_panel_init(&ctx->panel, dev, &sdc_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = sdc_create_backlight(dsi);
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

static void sdc_remove(struct mipi_dsi_device *dsi)
{
	struct sdc *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id sdc_of_match[] = {
	{ .compatible = "samsung,milletwifi-panel-s6d7aa0" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sdc_of_match);

static struct mipi_dsi_driver sdc_driver = {
	.probe = sdc_probe,
	.remove = sdc_remove,
	.driver = {
		.name = "panel-sdc",
		.of_match_table = sdc_of_match,
	},
};
module_mipi_dsi_driver(sdc_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for SDC WXGA video mode dsi SEC_S6D7AA0 panel");
MODULE_LICENSE("GPL");
