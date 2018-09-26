// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Amarula Solutions
 * Author: Jagan Teki <jagan@amarulasolutions.com>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_device.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct s070wv20 {
	struct drm_panel	panel;
	struct mipi_dsi_device	*dsi;

	struct backlight_device	*backlight;
	struct regulator	*vdd;
	struct gpio_desc	*reset;
};

static inline struct s070wv20 *panel_to_s070wv20(struct drm_panel *panel)
{
	return container_of(panel, struct s070wv20, panel);
}

struct s070wv20_init_cmd {
	size_t len;
	const char *data;
};

#define S070WV20_INIT_CMD(...) { \
	.len = sizeof((char[]){__VA_ARGS__}), \
	.data = (char[]){__VA_ARGS__} }

static const struct s070wv20_init_cmd s070wv20_init_cmds[] = {
	S070WV20_INIT_CMD(0x7A, 0xC1),
	S070WV20_INIT_CMD(0x20, 0x20),
	S070WV20_INIT_CMD(0x21, 0xE0),
	S070WV20_INIT_CMD(0x22, 0x13),
	S070WV20_INIT_CMD(0x23, 0x28),
	S070WV20_INIT_CMD(0x24, 0x30),
	S070WV20_INIT_CMD(0x25, 0x28),
	S070WV20_INIT_CMD(0x26, 0x00),
	S070WV20_INIT_CMD(0x27, 0x0D),
	S070WV20_INIT_CMD(0x28, 0x03),
	S070WV20_INIT_CMD(0x29, 0x1D),
	S070WV20_INIT_CMD(0x34, 0x80),
	S070WV20_INIT_CMD(0x36, 0x28),
	S070WV20_INIT_CMD(0xB5, 0xA0),
	S070WV20_INIT_CMD(0x5C, 0xFF),
	S070WV20_INIT_CMD(0x2A, 0x01),
	S070WV20_INIT_CMD(0x56, 0x92),
	S070WV20_INIT_CMD(0x6B, 0x71),
	S070WV20_INIT_CMD(0x69, 0x2B),
	S070WV20_INIT_CMD(0x10, 0x40),
	S070WV20_INIT_CMD(0x11, 0x98),
	S070WV20_INIT_CMD(0xB6, 0x20),
	S070WV20_INIT_CMD(0x51, 0x20),
	S070WV20_INIT_CMD(0x09, 0x10),
};

static int s070wv20_prepare(struct drm_panel *panel)
{
	struct s070wv20 *ctx = panel_to_s070wv20(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	unsigned int i;
	int ret;

	ret = regulator_enable(ctx->vdd);
	if (ret)
		return ret;

	msleep(50);

	gpiod_set_value(ctx->reset, 1);
	msleep(50);

	gpiod_set_value(ctx->reset, 0);
	msleep(50);

	gpiod_set_value(ctx->reset, 1);
	msleep(20);

	for (i = 0; i < ARRAY_SIZE(s070wv20_init_cmds); i++) {
		const struct s070wv20_init_cmd *cmd = &s070wv20_init_cmds[i];

		ret = mipi_dsi_generic_write(dsi, cmd->data, cmd->len);
		if (ret < 0)
			return ret;

		msleep(10);
	}

	return 0;
}

static int s070wv20_enable(struct drm_panel *panel)
{
	struct s070wv20 *ctx = panel_to_s070wv20(panel);

	msleep(120);

	mipi_dsi_dcs_set_display_on(ctx->dsi);
	backlight_enable(ctx->backlight);

	return 0;
}

static int s070wv20_disable(struct drm_panel *panel)
{
	struct s070wv20 *ctx = panel_to_s070wv20(panel);

	backlight_disable(ctx->backlight);
	return mipi_dsi_dcs_set_display_on(ctx->dsi);
}

static int s070wv20_unprepare(struct drm_panel *panel)
{
	struct s070wv20 *ctx = panel_to_s070wv20(panel);
	int ret;

	ret = mipi_dsi_dcs_set_display_off(ctx->dsi);
	if (ret < 0)
		dev_err(panel->dev, "failed to set display off: %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(ctx->dsi);
	if (ret < 0)
		dev_err(panel->dev, "failed to enter sleep mode: %d\n", ret);

	msleep(100);

	gpiod_set_value(ctx->reset, 0);

	gpiod_set_value(ctx->reset, 1);

	gpiod_set_value(ctx->reset, 0);

	regulator_disable(ctx->vdd);

	return 0;
}

static const struct drm_display_mode s070wv20_default_mode = {
	.clock = 55000,
	.vrefresh = 60,

	.hdisplay = 800,
	.hsync_start = 800 + 40,
	.hsync_end = 800 + 40 + 48,
	.htotal = 800 + 40 + 48 + 40,

	.vdisplay = 480,
	.vsync_start = 480 + 13,
	.vsync_end = 480 + 13 + 3,
	.vtotal = 480 + 13 + 3 + 29,

	.width_mm	= 86,
	.height_mm	= 154,
};

static int s070wv20_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct s070wv20 *ctx = panel_to_s070wv20(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(panel->drm, &s070wv20_default_mode);
	if (!mode) {
		dev_err(&ctx->dsi->dev, "failed to add mode %ux%ux@%u\n",
			s070wv20_default_mode.hdisplay,
			s070wv20_default_mode.vdisplay,
			s070wv20_default_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	panel->connector->display_info.width_mm = mode->width_mm;
	panel->connector->display_info.height_mm = mode->height_mm;

	return 1;
}

static const struct drm_panel_funcs s070wv20_funcs = {
	.disable = s070wv20_disable,
	.unprepare = s070wv20_unprepare,
	.prepare = s070wv20_prepare,
	.enable = s070wv20_enable,
	.get_modes = s070wv20_get_modes,
};

static int s070wv20_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct device_node *np;
	struct s070wv20 *ctx;
	int ret;

	ctx = devm_kzalloc(&dsi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = &dsi->dev;
	ctx->panel.funcs = &s070wv20_funcs;

	ctx->vdd = devm_regulator_get(&dsi->dev, "vdd");
	if (IS_ERR(ctx->vdd)) {
		dev_err(&dsi->dev, "Couldn't get vdd regulator\n");
		return PTR_ERR(ctx->vdd);
	}

	ctx->reset = devm_gpiod_get(&dsi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset)) {
		dev_err(&dsi->dev, "Couldn't get our reset GPIO\n");
		return PTR_ERR(ctx->reset);
	}

	np = of_parse_phandle(dsi->dev.of_node, "backlight", 0);
	if (np) {
		ctx->backlight = of_find_backlight_by_node(np);
		of_node_put(np);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	ret = drm_panel_add(&ctx->panel);
	if (ret < 0)
		return ret;

	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->lanes = 4;

	return mipi_dsi_attach(dsi);
}

static int s070wv20_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct s070wv20 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	if (ctx->backlight)
		put_device(&ctx->backlight->dev);

	return 0;
}

static const struct of_device_id s070wv20_of_match[] = {
	{ .compatible = "bananapi,s070wv20-ct16-icn6211", },
	{ }
};
MODULE_DEVICE_TABLE(of, s070wv20_of_match);

static struct mipi_dsi_driver s070wv20_driver = {
	.probe = s070wv20_dsi_probe,
	.remove = s070wv20_dsi_remove,
	.driver = {
		.name = "bananapi-s070wv20-ct16-icn6211",
		.of_match_table = s070wv20_of_match,
	},
};
module_mipi_dsi_driver(s070wv20_driver);

MODULE_AUTHOR("Jagan Teki <jagan@amarulasolutions.com>");
MODULE_DESCRIPTION("Bananapi S070WV20-CT16 ICN6211 MIPI-DSI to RGB");
MODULE_LICENSE("GPL v2");
