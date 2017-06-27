/*
 * Copyright (C) ??? ???
 * Author: Luca Weiss <luca@z3ntu.xyz>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/backlight.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

struct otm_panel {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	struct backlight_device *backlight;
	struct regulator *supply;
	struct gpio_desc *reset_gpio;

	bool prepared;
	bool enabled;

	const struct drm_display_mode *mode;
};

static inline struct otm_panel *to_otm_panel(struct drm_panel *panel)
{
	return container_of(panel, struct otm_panel, base);
}

static int otm_panel_on(struct otm_panel *otm)
{
	struct mipi_dsi_device *dsi = otm->dsi;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_write(dsi, 0x00, (u8[]){ 0x00 }, 1);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_write(dsi, 0xff, (u8[]){ 0x19, 0x02, 0x01, 0x00 }, 4);
	if (ret < 0)
		return ret;
	msleep(1);

	ret = mipi_dsi_dcs_write(dsi, 0x00, (u8[]){ 0x80 }, 1);
	if (ret < 0)
		return ret;
	msleep(1);

	ret = mipi_dsi_dcs_write(dsi, 0xff, (u8[]){ 0x19, 0x02 }, 2);
	if (ret < 0)
		return ret;
	msleep(1);

	ret = mipi_dsi_dcs_write(dsi, 0x00, (u8[]){ 0xB0 }, 1);
	if (ret < 0)
		return ret;
	msleep(1);

	ret = mipi_dsi_dcs_write(dsi, 0xca, (u8[]){ 0xff, 0x02, 0x5f, 0x40 }, 4);
	if (ret < 0)
		return ret;
	msleep(1);

	ret = mipi_dsi_dcs_write(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY, (u8[]){ 0x2c }, 1);
	if (ret < 0)
		return ret;
	msleep(1);

	ret = mipi_dsi_dcs_write(dsi, MIPI_DCS_EXIT_SLEEP_MODE, (u8[]){ 0x00 }, 1);
	//ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0)
		return ret;
	msleep(50);

	ret = mipi_dsi_dcs_write(dsi, MIPI_DCS_SET_DISPLAY_ON, (u8[]){ 0x00 }, 1);
	//ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0)
		return ret;
	msleep(96);

	printk(KERN_ERR "OTM_PANEL_ON() SUCCESS\n");

	return 0;
}

static int otm_panel_off(struct otm_panel *otm)
{
	struct mipi_dsi_device *dsi = otm->dsi;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		return ret;
	msleep(50);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0)
		return ret;
	msleep(100);

	return 0;
}

static int otm_panel_disable(struct drm_panel *panel)
{
	struct otm_panel *otm = to_otm_panel(panel);

	if (!otm->enabled)
		return 0;

	DRM_DEBUG("disable\n");

	if (otm->backlight) {
		otm->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(otm->backlight);
	}

	otm->enabled = false;

	return 0;
}

static int otm_panel_unprepare(struct drm_panel *panel)
{
	struct otm_panel *otm = to_otm_panel(panel);
	int ret;

	if (!otm->prepared)
		return 0;

	DRM_DEBUG("unprepare\n");

	ret = otm_panel_off(otm);
	if (ret) {
		dev_err(panel->dev, "failed to set panel off: %d\n", ret);
		return ret;
	}

	regulator_disable(otm->supply);
	if (otm->reset_gpio)
		gpiod_set_value(otm->reset_gpio, 0);

	otm->prepared = false;

	return 0;
}

static int otm_panel_prepare(struct drm_panel *panel)
{
	struct otm_panel *otm = to_otm_panel(panel);
	int ret;

	if (otm->prepared)
		return 0;

	DRM_DEBUG("prepare\n");

	if (otm->reset_gpio) {
		gpiod_set_value(otm->reset_gpio, 0);
		msleep(5);
	}

	ret = regulator_enable(otm->supply);
	if (ret < 0)
		return ret;

	msleep(20);

	if (otm->reset_gpio) {
		gpiod_set_value(otm->reset_gpio, 1);
		msleep(10);
	}

	msleep(150);

	ret = otm_panel_on(otm);
	if (ret) {
		dev_err(panel->dev, "failed to set panel on: %d\n", ret);
		goto poweroff;
	}

	otm->prepared = true;

	return 0;

poweroff:
	regulator_disable(otm->supply);
	if (otm->reset_gpio)
		gpiod_set_value(otm->reset_gpio, 0);
	return ret;
}

static int otm_panel_enable(struct drm_panel *panel)
{
	struct otm_panel *otm = to_otm_panel(panel);

	if (otm->enabled)
		return 0;

	DRM_DEBUG("enable\n");

	if (otm->backlight) {
		otm->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(otm->backlight);
	}

	otm->enabled = true;

	return 0;
}

static const struct drm_display_mode default_mode = {
		.clock = 147290, // or 147291 - actually 147290.88
		.hdisplay = 1080,
		.hsync_start = 1080 + 144,
		.hsync_end = 1080 + 144 + 12,
		.htotal = 1080 + 144 + 12 + 32,
		.vdisplay = 1920,
		.vsync_start = 1920 + 9,
		.vsync_end = 1920 + 9 + 4,
		.vtotal = 1920 + 9 + 4 + 3,
		.vrefresh = 60,
};

static int otm_panel_get_modes(struct drm_panel *panel)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(panel->drm, &default_mode);
	if (!mode) {
		dev_err(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
				default_mode.hdisplay, default_mode.vdisplay,
				default_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	drm_mode_probed_add(panel->connector, mode);

	panel->connector->display_info.width_mm = 62;
	panel->connector->display_info.height_mm = 110;

	return 1;
}

static int dsi_dcs_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	int ret;
	u16 brightness = bl->props.brightness;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness & 0xff;
}

static int dsi_dcs_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness(dsi, bl->props.brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static const struct backlight_ops dsi_bl_ops = {
	.update_status = dsi_dcs_bl_update_status,
	.get_brightness = dsi_dcs_bl_get_brightness,
};

static struct backlight_device *
drm_panel_create_dsi_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct backlight_properties props;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.brightness = 255;
	props.max_brightness = 255;

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &dsi_bl_ops, &props);
}

static const struct drm_panel_funcs otm_panel_funcs = {
		.disable = otm_panel_disable,
		.unprepare = otm_panel_unprepare,
		.prepare = otm_panel_prepare,
		.enable = otm_panel_enable,
		.get_modes = otm_panel_get_modes,
};

static const struct of_device_id otm_of_match[] = {
		{ .compatible = "otm,otm1902b-1080p-cmd", },
		{ }
};
MODULE_DEVICE_TABLE(of, otm_of_match);

static int otm_panel_add(struct otm_panel *otm)
{
	struct device *dev= &otm->dsi->dev;
	int ret;

	otm->mode = &default_mode;

	otm->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(otm->supply))
		return PTR_ERR(otm->supply);

	otm->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(otm->reset_gpio)) {
		dev_err(dev, "cannot get reset-gpios %ld\n",
			PTR_ERR(otm->reset_gpio));
		otm->reset_gpio = NULL;
	} else {
		gpiod_direction_output(otm->reset_gpio, 0);
	}

	otm->backlight = drm_panel_create_dsi_backlight(otm->dsi);
	if (IS_ERR(otm->backlight)) {
		ret = PTR_ERR(otm->backlight);
		dev_err(dev, "failed to register backlight %d\n", ret);
		return ret;
	}

	drm_panel_init(&otm->base);
	otm->base.funcs = &otm_panel_funcs;
	otm->base.dev = &otm->dsi->dev;

	ret = drm_panel_add(&otm->base);
	if (ret < 0)
		goto put_backlight;

	return 0;

	put_backlight:
	if (otm->backlight)
		put_device(&otm->backlight->dev);

	return ret;
}

static void otm_panel_del(struct otm_panel *otm)
{
	if (otm->base.dev)
		drm_panel_remove(&otm->base);

	if (otm->backlight)
		put_device(&otm->backlight->dev);
}

static int otm_panel_probe(struct mipi_dsi_device *dsi)
{
	struct otm_panel *otm;
	int ret;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_HSE |
			MIPI_DSI_CLOCK_NON_CONTINUOUS |
			MIPI_DSI_MODE_EOT_PACKET;

	otm = devm_kzalloc(&dsi->dev, sizeof(*otm), GFP_KERNEL);
	if (!otm) {
		return -ENOMEM;
	}

	mipi_dsi_set_drvdata(dsi, otm);

	otm->dsi = dsi;

	ret = otm_panel_add(otm);
	if (ret < 0) {
		return ret;
	}

	return mipi_dsi_attach(dsi);
}

static int otm_panel_remove(struct mipi_dsi_device *dsi)
{
	struct otm_panel *otm = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = otm_panel_disable(&otm->base);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to disable panel: %d\n", ret);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", ret);

	drm_panel_detach(&otm->base);
	otm_panel_del(otm);

	return 0;
}

static void otm_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct otm_panel *otm = mipi_dsi_get_drvdata(dsi);

	otm_panel_disable(&otm->base);
}

static struct mipi_dsi_driver otm_panel_driver = {
	.driver = {
		.name = "panel-otm-otm1902b-1080p",
		.of_match_table = otm_of_match,
	},
	.probe = otm_panel_probe,
	.remove = otm_panel_remove,
	.shutdown = otm_panel_shutdown,
};
module_mipi_dsi_driver(otm_panel_driver);

MODULE_AUTHOR("Luca Weiss <luca@z3ntu.xyz>");
MODULE_DESCRIPTION("OTM1902b 1080p panel driver");
MODULE_LICENSE("GPL v2");



///////////////////////////////////////////////////////////////////////
/*

panel->connector->display_info.width_mm = 62;
panel->connector->display_info.height_mm = 110;

MIPI_DSI_MODE_VIDEO_HSE
MIPI_DSI_MODE_EOT_PACKET
MIPI_DSI_CLOCK_NON_CONTINUOUS


dsi->lanes = 4;
dsi->format = MIPI_DSI_FMT_RGB888;



hdisplay = 1080;
hsync_start = 1080 + 144;
hsync_end = 1080 + 144 + 12;
htotal = 1080 + 144 + 12 + 32;

vdisplay = 1920;
vsync_start = 1920 + 9;
vsync_end = 1920 + 9 + 4;
vtotal = 1920 + 9 + 4 + 3;

clock = (1080 + 144 + 12 + 32) * (1920 + 9 + 4 + 3) * 60 / 1000;


                            P
                            A
 D                          Y
 T  L        W      D       L
 Y  A     A  A      L       O
 P  S  V  C  I      E       A
 E  T  C  K  T      N       D
 29 01 00 00 00   00 02   00 00
 29 01 00 00 01   00 05   FF 19 02 01 00
 29 01 00 00 01   00 02   00 80
 29 01 00 00 01   00 03   FF 19 02
 15 01 00 00 01   00 02   00 B0
 29 01 00 00 01   00 05   CA FF 02 5F 40
 15 01 00 00 01   00 02   53 2C
 05 01 00 00 32   00 02   11 00
 05 01 00 00 60   00 02   29 00


 OFF:
 05 01 00 00 32   00 02   28 00
 05 01 00 00 64   00 02   10 00
 */
