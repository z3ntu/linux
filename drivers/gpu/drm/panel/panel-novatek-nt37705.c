// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2025 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct nt37705_boe_amoled {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data nt37705_boe_amoled_supplies[] = {
	{ .supply = "vddio" },
	{ .supply = "dvdd" },
	{ .supply = "vci" },
};

static inline
struct nt37705_boe_amoled *to_nt37705_boe_amoled(struct drm_panel *panel)
{
	return container_of(panel, struct nt37705_boe_amoled, panel);
}

static void nt37705_boe_amoled_reset(struct nt37705_boe_amoled *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int nt37705_boe_amoled_on(struct nt37705_boe_amoled *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0,
					 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x1b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xba, 0x18);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x1c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xba,
					 0x01, 0x01, 0x01, 0x01, 0x01, 0x00,
					 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x2c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xba,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x3c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xba,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x4c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xba,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x5c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xba,
					 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
					 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
					 0x01, 0x01, 0x01, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x6c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xba,
					 0x01, 0x01, 0x02, 0x03, 0x04, 0x05,
					 0x01, 0x77, 0x01, 0x01, 0x01, 0x0b,
					 0x01, 0x1d, 0x01, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x7c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xba,
					 0x01, 0x01, 0x02, 0x03, 0x04, 0x05,
					 0x01, 0x77, 0x01, 0x01, 0x01, 0x0b,
					 0x01, 0x1d, 0x01, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x8c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xba,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x9c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xba,
					 0x11, 0x11, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0xa4);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xba,
					 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0xa8);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xba,
					 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
					 0x22, 0x22);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0xb0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xba,
					 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
					 0x22, 0x22);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0,
					 0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x05);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc5,
					 0x15, 0x15, 0x15, 0xdd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0,
					 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x0e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb5, 0x32);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0,
					 0x55, 0xaa, 0x52, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff,
					 0xaa, 0x55, 0xa5, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x19);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf2, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x1a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf4, 0x55);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x11);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf8, 0x01, 0x7f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x2d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf8, 0x01, 0x20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff,
					 0xaa, 0x55, 0xa5, 0x81);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x05);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfe, 0x3c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf9, 0x04);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x1e);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfb, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x0f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf5, 0x20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x0d);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfb, 0x80);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff,
					 0xaa, 0x55, 0xa5, 0x83);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x12);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfe, 0x41);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x13);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xfd, 0x21);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff,
					 0xaa, 0x55, 0xa5, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x53, 0x20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2a,
					 0x00, 0x00, 0x04, 0x5b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2b,
					 0x00, 0x00, 0x09, 0xb3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x26, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x51, 0x0d, 0xbb);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6f, 0x04);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x51, 0x0f, 0xfe);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x81, 0x01, 0x19);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x90, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2f, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x5a, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x2f, 0x30);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x6d, 0x01);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 22);

	return dsi_ctx.accum_err;
}

static int nt37705_boe_amoled_off(struct nt37705_boe_amoled *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 100);

	return dsi_ctx.accum_err;
}

static int nt37705_boe_amoled_prepare(struct drm_panel *panel)
{
	struct nt37705_boe_amoled *ctx = to_nt37705_boe_amoled(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(nt37705_boe_amoled_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	nt37705_boe_amoled_reset(ctx);

	ret = nt37705_boe_amoled_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(nt37705_boe_amoled_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int nt37705_boe_amoled_unprepare(struct drm_panel *panel)
{
	struct nt37705_boe_amoled *ctx = to_nt37705_boe_amoled(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = nt37705_boe_amoled_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(nt37705_boe_amoled_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode nt37705_boe_amoled_mode = {
	.clock = (1116 + 100 + 30 + 100) * (2484 + 70 + 48 + 70) * 60 / 1000,
	.hdisplay = 1116,
	.hsync_start = 1116 + 100,
	.hsync_end = 1116 + 100 + 30,
	.htotal = 1116 + 100 + 30 + 100,
	.vdisplay = 2484,
	.vsync_start = 2484 + 70,
	.vsync_end = 2484 + 70 + 48,
	.vtotal = 2484 + 70 + 48 + 70,
	.width_mm = 66,
	.height_mm = 146,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int nt37705_boe_amoled_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &nt37705_boe_amoled_mode);
}

static const struct drm_panel_funcs nt37705_boe_amoled_panel_funcs = {
	.prepare = nt37705_boe_amoled_prepare,
	.unprepare = nt37705_boe_amoled_unprepare,
	.get_modes = nt37705_boe_amoled_get_modes,
};

static int nt37705_boe_amoled_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

// TODO: Check if /sys/class/backlight/.../actual_brightness actually returns
// correct values. If not, remove this function.
static int nt37705_boe_amoled_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness;
}

static const struct backlight_ops nt37705_boe_amoled_bl_ops = {
	.update_status = nt37705_boe_amoled_bl_update_status,
	.get_brightness = nt37705_boe_amoled_bl_get_brightness,
};

static struct backlight_device *
nt37705_boe_amoled_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 4095,
		.max_brightness = 4095,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &nt37705_boe_amoled_bl_ops, &props);
}

static int nt37705_boe_amoled_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct nt37705_boe_amoled *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(nt37705_boe_amoled_supplies),
					    nt37705_boe_amoled_supplies,
					    &ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &nt37705_boe_amoled_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = nt37705_boe_amoled_create_backlight(dsi);
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

static void nt37705_boe_amoled_remove(struct mipi_dsi_device *dsi)
{
	struct nt37705_boe_amoled *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id nt37705_boe_amoled_of_match[] = {
	{ .compatible = "boe,bj631jhm-t71-d900" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nt37705_boe_amoled_of_match);

static struct mipi_dsi_driver nt37705_boe_amoled_driver = {
	.probe = nt37705_boe_amoled_probe,
	.remove = nt37705_boe_amoled_remove,
	.driver = {
		.name = "panel-nt37705-boe-amoled",
		.of_match_table = nt37705_boe_amoled_of_match,
	},
};
module_mipi_dsi_driver(nt37705_boe_amoled_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for nt37705 amoled command mode dsi panel");
MODULE_LICENSE("GPL");
