// SPDX-License-Identifier: GPL-2.0+
/*
 * HID driver for Razer gaming accessories.
 * Copyright (c) 2018 Linus Walleij <linus.walleij@linaro.org>
 *
 * Based on know-how from the out-of-tree Razer accesories driver:
 * Copyright (c) Terry Cain <terry@terrys-home.co.uk>
 * Copyright (c) Luca Weiss <luca@z3ntu.xyz>
 * Copyright (c) Tim Theede <pez2001@voyagerproject.de>
 * Copyright (c) Adam Honse <calcprogrammer1@gmail.com>
 * Copyright (c) Steve Kondik <shade@chemlab.org>
 *
 * The Razer accessories share a common protocol accessed over the
 * USB HID mouse interface with HID_REQ_[SET|GET]_REPORT control
 * messages. The message is identical in both directions (to and
 * from the device) and consists of 0x5a bytes with the following
 * layout:
 *
 * Byte offset:     Content:
 * 0x00             Status (0x00 when sending)
 * 0x01             Transaction ID (usually 0xff or 0x3f)
 * 0x02             Remaining packets HI byte (big endian)
 * 0x03             Remaining packest LO byte (big endian)
 * 0x04             Protocol type (always 0x00)
 * 0x05             Data size (number of bytes used in the payload)
 * 0x06             Command class
 * 0x07             Command ID
 * 0x08 .. 0x57     Argument (payload)
 * 0x58             CRC sum (0x00 XOR bytes 0x02..0x57)
 * 0x59             Reserved/unused
 */
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/string.h>
#include <linux/random.h>
#include <linux/sysfs.h>
#include <linux/leds.h>
#include <linux/dmi.h>
#include <linux/mutex.h>
#include <linux/bitops.h>
#include <linux/bitmap.h>

#include "hid-ids.h"

/*
 * The upstream kernel driver version is bumped to 3.0.0 compared
 * to the old out-of-tree driver so that userspace programs have
 * a chance to deal with this.
 */
#define DRIVER_VERSION "3.0.0"

/*
 * Waiting constants for USB control messages
 * some survive at 600us but keep it safe at 900
 */
#define RAZER_WAIT_MIN_US	900
#define RAZER_WAIT_MAX_US	1000

#define RAZER_USB_REPORT_LEN	0x5A

#define RAZER_CMD_GET_LED_STATE		0x80
#define RAZER_CMD_GET_FW_VER		0x81
#define RAZER_CMD_GET_SERIAL		0x82
#define RAZER_CMD_GET_LED_EFFECT	0x82
#define RAZER_CMD_GET_BRIGHTNESS	0x83
#define RAZER_CMD_GET_VARIABLE		0x84
#define RAZER_CMD_GET_LAYOUT		0x86
/* Length 4 reads something that is 01 00 09 00 */
#define RAZER_CMD_UNKNOWN_87		0x87

#define RAZER_CMD_SET_LED_STATE		0x00
/* Length 5 writes 00 07 34 a0 e1 set RGB on LED 7 ? */
#define RAZER_CMD_UNKNOWN_01		0x01
#define RAZER_CMD_SET_LED_EFFECT	0x02
#define RAZER_CMD_SET_LED_EFFECT_ANANSI	0x04
#define RAZER_CMD_SET_BRIGHTNESS	0x03
#define RAZER_CMD_SET_VARIABLE		0x04
#define RAZER_CMD_SET_EFFECT		0x0a

/* Response types for commands on the control interface */
#define RAZER_CMD_BUSY	 	 0x01
#define RAZER_CMD_SUCCESSFUL	 0x02
#define RAZER_CMD_FAILURE	 0x03
#define RAZER_CMD_TIMEOUT	 0x04
#define RAZER_CMD_NOT_SUPPORTED	 0x05

enum razer_mode {
	RAZER_MODE_NORMAL,
	RAZER_MODE_FACTORY,
	RAZER_MODE_DRIVER,
	RAZER_MODE_UNKNOWN,
};

enum razer_led_type {
	RAZER_LED_SCROLL_WHEEL,
	RAZER_LED_BATTERY,
	RAZER_LED_LOGO,
	RAZER_LED_BACKLIGHT,
	RAZER_LED_BACKLIGHT_BLADE,
	RAZER_LED_BACKLIGHT_STULT,
	RAZER_LED_MACRO,
	RAZER_LED_MACRO_ANANSI,
	RAZER_LED_GAME,
	RAZER_LED_RED_PROFILE,
	RAZER_LED_GREEN_PROFILE,
	RAZER_LED_BLUE_PROFILE,
};

enum razer_macro_state {
	RAZER_MACRO_OFF,
	RAZER_MACRO_RECORD,
	RAZER_MACRO_STORE,
};

enum razer_matrix_effect {
	RAZER_EFFECT_NONE,
	RAZER_EFFECT_WAVE,
	RAZER_EFFECT_SPECTRUM,
	RAZER_EFFECT_REACTIVE,
	RAZER_EFFECT_STATIC,
	RAZER_EFFECT_BREATHING, /* Also known as "pulsate" */
	RAZER_EFFECT_STARLIGHT,
	RAZER_EFFECT_RIPPLE,
	RAZER_EFFECT_FIRE,
	/*
	 * FIXME: missing effects: audio meter, ambient awareness,
	 * wheel ... please find these.
	 */
};

struct razer;

/**
 * struct razer_led - Information for a Razer LED
 */
struct razer_led_info {
	/**
	 * @name: LED name
	 */
	const char *name;
	/**
	 * @color: LED color
	 */
	const char *color;
	/**
	 * @type: Razer LED type
	 */
	enum razer_led_type type;
};

/**
 * struct razer_raw_keymap - maps a raw Razer key event to a Linux key
 */
struct razer_raw_keymap {
	/**
	 * @name: Name of this key (mostly for debugging)
	 */
	const char *name;
	/**
	 * @is_fn: This is the magical fn key, it will not be translated
	 * to a Linux key, but keypresses will result in state change
	 */
	bool is_fn;
	/**
	 * @razerkey: The Razer key code reported in the raw event
	 */
	u8 razer_key;
	/**
	 * @linuxkey: The Linux key code to be reported upward
	 */
	unsigned int linux_key;
};

/**
 * struct razer_fn_keymap - maps a Razer key while holding fn
 * (mostly F1..F8) to a Linux key
 */
struct razer_fn_keymap {
	/**
	 * @name: Name of this key (mostly for debugging)
	 */
	const char *name;
	/**
	 * @from_key: The Linux key code on the keyboard pressed while
	 * holding fn
	 */
	unsigned int from_key;
	/**
	 * @to_key: The Linux key code to be reported upward
	 */
	unsigned int to_key;
	/**
	 * @is_macro: This is the macro key
	 */
	bool is_macro;
	/**
	 * @is_game: This is the game key
	 */
	bool is_game;
	/**
	 * @is_bl_down: This is the backlight down key
	 */
	bool is_bl_down;
	/**
	 * @is_bl_up: This the backlight up key
	 */
	bool is_bl_up;
	/**
	 * @is_profile: This is the profile toggle key (not present on all
	 * keyboards)
	 */
	bool is_profile;
	/**
	 * @is_razereffect: This is the Razer effect toggle key (not present
	 * on all keyboards)
	 */
	bool is_razereffect;
};

/**
 * struct razer_keyboard - Product information for a Razer keyboard
 */
struct razer_keyboard {
	/**
	 * @name: Razer product name
	 */
	const char *name;
	/**
	 * @req_res_index_2: The control message index to request device
	 * status on the control interface of the device uses index 2.
	 * This is an odd outlier: all devices except one use index 1.
	 */
	bool req_res_index_2;
	/**
	 * @is_blade: This is a laptop blade laptop keyboard, meaning it is
	 * embedded inside one of the Razer gaming laptops
	 */
	bool is_blade;
	/**
	 * @is_bw_stealth: This is the BlackWidow Stealth keyboard which has
	 * a slightly deviant key mapping for the special Razer keys
	 */
	bool is_bw_stealth;
	/**
	 * @volume_wheel: This keyboard has a volume wheel we need to
	 * translate from mouse wheel events to volume up/down events.
	 */
	bool volume_wheel;
	/**
	 * @extended_effects: This keyboard use extended effect settings in
	 * command class 0x0f rather than 0x03.
	 */
	bool extended_effects;
	/**
	 * @leds: array of LEDs on this device
	 */
	const struct razer_led_info *leds;
	/**
	 * @effects: keyboard backlight effects on this device
	 */
	const enum razer_matrix_effect *effects;
	/**
	 * @raw_keymap: map between raw Razer keyboard events and Linux keys
	 */
	const struct razer_raw_keymap *raw_keymap;
	/**
	 * @fn_keymap: map for keys pressed while holding down the fn key
	 */
	const struct razer_fn_keymap *fn_keymap;
};

/**
 * struct razer_led - Information for a Razer LED
 */
struct razer_led {
	/**
	 * @led_info: Razer LED info pointer
	 */
	const struct razer_led_info *info;
	/**
	 * @r: pointer to main state struct
	 */
	struct razer *r;
	/**
	 * @led: LED class device for the Razer LED
	 */
	struct led_classdev led;
};

/**
 * struct razer_control - State container for control interface
 *
 * This struct will only be attached to the control interface of the Razer
 * device.
 */
struct razer_control {
	/**
	 * @lock: locks a USB control message transaction
	 */
	struct mutex lock;
	/**
	 * @request_buf: buffer to store a report request
	 */
	u8 request_buf[RAZER_USB_REPORT_LEN];
	/**
	 * @response_buf: buffer to store the response from a requested
	 * report
	 */
	u8 response_buf[RAZER_USB_REPORT_LEN];
	/**
	 * @fw_major: major firmware version
	 */
	u8 fw_major;
	/**
	 * @fw_minor: minor firmware version
	 */
	u8 fw_minor;
	/**
	 * @serial: serial number, DMI serial number can be up to 50 chars
	 * and NULL, normal serial numbers are just 22 characters
	 */
	char serial[51];
	/**
	 * @mode: the current mode of the device
	 */
	enum razer_mode mode;
	/**
	 * @layout: the layout on this keyboard
	 */
	u8 layout;
	/**
	 * @backlight: handle to the backlight LED on this keyboard
	 */
	struct razer_led *backlight;
	/**
	 * @backlight_brightness: current backlight brightness
	 */
	enum led_brightness backlight_brightness;
	/**
	 * @game_mode: if the keyboard is in game mode
	 */
	bool game_mode;
	/**
	 * @game_led: handle to the LED that indicates GAME mode on this
	 * keyboard
	 */
	struct razer_led *gameled;
	/**
	 * @macro_state: the macro key state
	 */
	enum razer_macro_state macro_state;
	/**
	 * @macro_led: handle to the LED that indicates MACRO mode on this
	 * keyboard
	 */
	struct razer_led *macroled;
	/**
	 * @matrix_effect: currently selected matrix effect
	 */
	enum razer_matrix_effect matrix_effect;
	/**
	 * @input: input device for the special Razer keys
	 */
	struct input_dev *input;
};

/**
 * struct razer - State containter for a Razer device
 *
 * This state container is attached to each interface of the Razer device, the control
 * interface will contain additional information as well.
 */
struct razer {
	/**
	 * @dev: pointer to the parent device
	 */
	struct device *dev;
	/**
	 * @hdev: pointer to the HID input device
	 */
	struct hid_device *hdev;
	/**
	 * @product: Razer product variant information
	 */
	const struct razer_keyboard *product;
	/**
	 * @uif: the interface used on the USB bus
	 */
	struct usb_interface *uif;
	/**
	 * @udev: USB device for this device
	 */
	struct usb_device *udev;
	/**
	 * @control: the razer control interface state, this will be NULL
	 * if this interface is not the control interface
	 */
	struct razer_control *control;
	/**
	 * @fn_pressed: Magical fn key is pressed or not
	 */
	bool fn_pressed;
	/**
	 * @active_keys: Bitmap of Razer special keys active right now
	 * we assume sizeof(unsigned long) will be enough for all
	 * custom keys.
	 */
	unsigned long active_keys;
};

/**
 * razer_get_control() - Get the control interface for a Razer device
 *
 * We keep a pointer to the control interface in the driver data of the
 * USB device, and that is how we get at it, unless we're instantiated
 * on the control interface itself.
 */
struct razer_control *razer_get_control(struct razer *r)
{
	if (r->control)
		return r->control;
	return dev_get_drvdata(&r->udev->dev);
}

static void razer_usb_marshal_request(struct razer *r, u8 cmd_class, u8 cmd_id, u8 cmd_size)
{
	struct razer_control *rc = razer_get_control(r);
	u8 crc = 0;
	unsigned int i;

	/* Marshal (serialize) the request */

	rc->request_buf[0] = 0x00; /* Status */
	/*
	 * It appears that the OpenRazer project is spending lots of time
	 * trying to hammer this transaction ID to 0x3f on some products, and
	 * in some logs the transaction ID 0x1f appears. In my (non-exhaustive)
	 * tests I have not found that the devices care one bit about the
	 * transaction ID, but if you think that your device is not working
	 * because the transaction ID is not the same as in the log, go ahead
	 * and patch this and see if it helps, who knows.
	 */
	rc->request_buf[1] = 0xff; /* Transaction ID */
	/*
	 * I suspect that the only time the remaining packets make any sense
	 * is in firmware updates
	 */
	rc->request_buf[2] = 0x00; /* Remaining packets HI */
	rc->request_buf[3] = 0x00; /* Remaining packets LO */
	rc->request_buf[4] = 0x00; /* Protocol type (always 0) */
	rc->request_buf[5] = cmd_size; /* Data size */
	rc->request_buf[6] = cmd_class; /* Command class */
	rc->request_buf[7] = cmd_id; /* Command ID */

	/* Bytes 8 .. 88 are arguments */

	/*
	 * Second to last byte of the request or response is a simple
	 * checksum: just XOR all bytes at index 2..88 up with overflow
	 * and you are done
	 */
	for(i = 2; i < (RAZER_USB_REPORT_LEN - 2); i++)
		crc ^= rc->request_buf[i];

	rc->request_buf[88] = crc; /* CRC */
	rc->request_buf[89] = 0x00; /* Reserved */
}

static int razer_usb_check_response(struct razer *r)
{
	struct razer_control *rc = razer_get_control(r);
	u8 status = rc->response_buf[0];
	/* Notice big endian format */
	u16 req_rp = rc->request_buf[2] << 8 | rc->request_buf[3];
	u16 res_rp = rc->response_buf[2] << 8 | rc->response_buf[3];
	u8 req_cc = rc->request_buf[6];
	u8 res_cc = rc->response_buf[6];
	u8 req_id = rc->request_buf[7];
	u8 res_id = rc->response_buf[7];

	/* First sanity check */
	if ((req_rp != res_rp) ||
	    (req_cc != res_cc) ||
	    (req_id != res_id)) {
		dev_err(r->dev, "request does not match response\n");
		return -EIO;
	}

	switch (status) {
	case RAZER_CMD_BUSY:
		dev_err(r->dev, "command 0x%02x device is busy\n", req_id);
		return -EIO;
	case RAZER_CMD_FAILURE:
		dev_err(r->dev, "command 0x%02x failed\n", req_id);
		return -EIO;
	case RAZER_CMD_TIMEOUT:
		dev_err(r->dev, "command 0x%02x timed out\n", req_id);
		return -EIO;
	case RAZER_CMD_NOT_SUPPORTED:
		dev_err(r->dev, "command 0x%02x not supported\n", req_id);
		return -EIO;
	default:
		dev_dbg(r->dev, "command 0x%02x successful\n", req_id);
	}

	return 0;
}

static int razer_usb_request_response(struct razer *r)
{
	struct razer_control *rc = razer_get_control(r);
	int req_res_index;
	int ret;

	/*
	 * All Razer products except an odd one requests a report index 1.
	 * One product requests on index 2, I wonder what is on index 1
	 * on that product.
	 */
	if (r->product->req_res_index_2)
		req_res_index = 2;
	else
		req_res_index = 1;

	/* Send the request to the device */
	ret = usb_control_msg(r->udev,
			      usb_rcvctrlpipe(r->udev, 0),
			      HID_REQ_SET_REPORT,
			      USB_TYPE_CLASS | USB_RECIP_INTERFACE |
			      USB_DIR_OUT,
			      0x300, /* value */
			      req_res_index, /* index */
			      rc->request_buf, /* data */
			      RAZER_USB_REPORT_LEN,
			      USB_CTRL_SET_TIMEOUT);

	if (ret != RAZER_USB_REPORT_LEN) {
		dev_err(r->dev, "failed request, sent bytes: %d\n", ret);
		return -EINVAL;
	}

	/* Wait after each USB message so as not to stress the interface */
	usleep_range(RAZER_WAIT_MIN_US, RAZER_WAIT_MAX_US);

	/* Ask for the response */
	ret = usb_control_msg(r->udev,
			      usb_rcvctrlpipe(r->udev, 0),
			      HID_REQ_GET_REPORT,
			      USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
			      0x300, /* value */
			      req_res_index, /* index */
			      rc->response_buf, /* data */
			      RAZER_USB_REPORT_LEN,
			      USB_CTRL_SET_TIMEOUT);

	/* Wait after each USB message so as not to stress the interface */
	usleep_range(RAZER_WAIT_MIN_US, RAZER_WAIT_MAX_US);

	/* Apparently this happens on some devices */
	if (ret != RAZER_USB_REPORT_LEN) {
		dev_err(r->dev, "short report, report length: 0x%02x\n", ret);
		return -EIO;
	}

	/* Response now contains the RAZER_USB_REPORT_LEN bytes */
	return razer_usb_check_response(r);
}

/**
 * razer_send_command() - Sends a command to the keyboard and get the response
 *
 * This will lock the transaction pipe, marshal a request, get the response and
 * check the result of a command with up to 80 arguments.
 */
static int razer_send_command(struct razer *r, u8 cmd_class, u8 cmd_id,
			      u8 num_args, u8 *buf)
{
	struct razer_control *rc = razer_get_control(r);
	int ret;

	if (num_args > 80) {
		dev_err(r->dev, "too many arguments\n");
		return -EIO;
	}

	mutex_lock(&rc->lock);

	/* Zero buffer, copy over arguments */
	memset(rc->request_buf, 0, sizeof(rc->request_buf));
	memcpy(&rc->request_buf[8], buf, num_args);

	razer_usb_marshal_request(r, cmd_class, cmd_id, num_args);

	ret = razer_usb_request_response(r);
	if (ret) {
		mutex_unlock(&rc->lock);
		return ret;
	}

	memcpy(buf, &rc->response_buf[8], num_args);

	mutex_unlock(&rc->lock);
	return 0;
}

/**
 * razer_set_device_mode() - set what mode the device will operate in
 *
 * Factory mode (0x02) will make M1-5 and FN emit normal keystrokes.
 */
static int razer_set_device_mode(struct razer *r, enum razer_mode mode)
{
	struct razer_control *rc = razer_get_control(r);
	u8 arg[2];
	int ret;

	/* Blade laptops are in the mode they are */
	if (r->product->is_blade)
		return 0;

	switch (mode) {
	case RAZER_MODE_NORMAL:
		arg[0] = 0x00;
		break;
	case RAZER_MODE_FACTORY:
		arg[0] = 0x02;
		break;
	case RAZER_MODE_DRIVER:
		arg[0] = 0x03;
		break;
	default:
		dev_err(r->dev, "illegal mode\n");
		return -EINVAL;
	}

	arg[1] = 0x00;
	ret = razer_send_command(r, 0, RAZER_CMD_SET_VARIABLE, 2, arg);
	if (ret) {
		dev_err(r->dev, "set mode request failed\n");
		return ret;
	}

	rc->mode = mode;
	return 0;
}

bool razer_led_is_backlight(struct razer_led *rled)
{
	const struct razer_led_info *rinfo = rled->info;

	return rinfo->type == RAZER_LED_BACKLIGHT ||
		rinfo->type == RAZER_LED_BACKLIGHT_BLADE ||
		rinfo->type == RAZER_LED_BACKLIGHT_STULT;
}

int razer_led_blink_set(struct razer *r,
			const struct razer_led_info *rinfo,
			bool on)
{
	u8 arg[4];
	int ret;

	switch (rinfo->type) {
	case RAZER_LED_MACRO:
		arg[0] = 1; /* VARSTORE? */
		arg[1] = 0x07; /* Macro LED */
		arg[2] = on ? 1 : 0; /* effects 0..5 */
		ret = razer_send_command(r, 3, RAZER_CMD_SET_LED_EFFECT,
					 3, arg);
		break;
	case RAZER_LED_MACRO_ANANSI:
		arg[0] = 0;
		arg[1] = 0x07; /* Macro LED */
		arg[2] = on ? 1 : 0; /* effects 0..5 */
		ret = razer_send_command(r, 3, RAZER_CMD_SET_LED_EFFECT,
					 3, arg);
		if (ret)
			return ret;
		/* The Anansi needs extra persuation */
		arg[0] = 0;
		arg[1] = 0x07; /* Macro LED */
		arg[2] = 0x05;
		arg[3] = 0x05;
		ret = razer_send_command(r, 3, RAZER_CMD_SET_LED_EFFECT_ANANSI,
					 4, arg);
		break;
	default:
		ret = -EINVAL;
		dev_err(r->dev, "can't %s blinking on led %s\n",
			on ? "enable" : "disable", rinfo->name);
		break;
	}

	return ret;
}

int razer_led_blink(struct led_classdev *cled,
		    unsigned long *delay_on,
		    unsigned long *delay_off)
{
	struct razer_led *rled = container_of(cled, struct razer_led, led);
	const struct razer_led_info *rinfo = rled->info;
	struct razer *r = rled->r;

	/* This call should always turn the blinking ON */
	return razer_led_blink_set(r, rinfo, true);
}

static int razer_led_set(struct led_classdev *cled,
			 enum led_brightness br)
{
	struct razer_led *rled = container_of(cled, struct razer_led, led);
	const struct razer_led_info *rinfo = rled->info;
	struct razer *r = rled->r;
	u8 arg[3];
	int ret;

	/*
	 * Argument format for 3 arguments:
	 * 0: 1 = VARSTORAGE (variable storage)
	 * 1: LED ID
	 * 2: brightness
	 */
	switch (rinfo->type) {
	case RAZER_LED_BACKLIGHT:
		arg[0] = 1;
		arg[1] = 0x05; /* 0x00 also work a lot of the time */
		arg[2] = br;
		if (r->product->extended_effects)
			ret = razer_send_command(r, 0x0f,
						 RAZER_CMD_SET_VARIABLE,
						 3, arg);
		else
			ret = razer_send_command(r, 3,
						 RAZER_CMD_SET_BRIGHTNESS,
						 3, arg);
		if (ret)
			break;
		return 0;
	case RAZER_LED_BACKLIGHT_BLADE:
		/* The blades only have two arguments: LED ID and brightness */
		arg[0] = 1;
		arg[1] = br;
		ret = razer_send_command(r, 0x0e, RAZER_CMD_SET_VARIABLE,
					 2, arg);
		if (ret)
			break;
		return 0;
	case RAZER_LED_BACKLIGHT_STULT:
	case RAZER_LED_LOGO:
		arg[0] = 1;
		arg[1] = 0x04;
		arg[2] = br;
		ret = razer_send_command(r, 3, RAZER_CMD_SET_BRIGHTNESS,
					 3, arg);
		if (ret)
			break;
		return 0;
	case RAZER_LED_SCROLL_WHEEL:
		arg[0] = 1;
		arg[1] = 0x01;
		arg[2] = br;
		ret = razer_send_command(r, 3, RAZER_CMD_SET_LED_STATE,
					 3, arg);
		if (ret)
			break;
		return 0;
	case RAZER_LED_BATTERY:
		arg[0] = 1;
		arg[1] = 0x03;
		arg[2] = br;
		ret = razer_send_command(r, 3, RAZER_CMD_SET_LED_STATE,
					 3, arg);
		if (ret)
			break;
		return 0;
	case RAZER_LED_GAME:
		arg[0] = 1;
		arg[1] = 0x08;
		arg[2] = br;
		ret = razer_send_command(r, 3, RAZER_CMD_SET_LED_STATE,
					 3, arg);
		if (ret)
			break;
		return 0;
	case RAZER_LED_MACRO:
	case RAZER_LED_MACRO_ANANSI:
		/* First turn off the blinking if set to off */
		if (br == LED_OFF) {
			ret = razer_led_blink_set(r, rinfo, false);
			if (ret)
				return ret;
		}

		arg[0] = 1;
		arg[1] = 0x07;
		arg[2] = br;
		ret = razer_send_command(r, 3, RAZER_CMD_SET_LED_STATE,
					 3, arg);
		if (ret)
			break;
		return 0;
	case RAZER_LED_RED_PROFILE:
		arg[0] = 1;
		arg[1] = 0x0c;
		arg[2] = br;
		ret = razer_send_command(r, 3, RAZER_CMD_SET_LED_STATE,
					 3, arg);
		if (ret)
			break;
		return 0;
	case RAZER_LED_GREEN_PROFILE:
		arg[0] = 1;
		arg[1] = 0x0d;
		arg[2] = br;
		ret = razer_send_command(r, 3, RAZER_CMD_SET_LED_STATE,
					 3, arg);
		if (ret)
			break;
		return 0;
	case RAZER_LED_BLUE_PROFILE:
		arg[0] = 1;
		arg[1] = 0x0e;
		arg[2] = br;
		ret = razer_send_command(r, 3, RAZER_CMD_SET_LED_STATE,
					 3, arg);
		if (ret)
			break;
		return 0;
	default:
		dev_err(r->dev, "unknown LED type\n");
		ret = -EINVAL;
		break;
	}

	dev_err(r->dev, "LED %s brightness set failed\n", rinfo->name);
	return ret;
}

static enum led_brightness razer_led_get(struct led_classdev *cled)
{
	struct razer_led *rled = container_of(cled, struct razer_led, led);
	const struct razer_led_info *rinfo = rled->info;
	struct razer *r = rled->r;
	u8 arg[3];
	int ret;

	arg[2] = 0;
	switch (rinfo->type) {
	case RAZER_LED_BACKLIGHT:
		arg[0] = 1;
		arg[1] = 0x05;
		if (r->product->extended_effects)
			ret = razer_send_command(r, 0x0f,
						 RAZER_CMD_GET_VARIABLE,
						 3, arg);
		else
			ret = razer_send_command(r, 3,
						 RAZER_CMD_GET_BRIGHTNESS,
						 3, arg);
		if (ret)
			break;
		return arg[2];
	case RAZER_LED_BACKLIGHT_BLADE:
		arg[0] = 1;
		arg[1] = 0;
		/* The blade laptops use only two arguments */
		ret = razer_send_command(r, 0x0e, RAZER_CMD_GET_VARIABLE,
					 2, arg);
		if (ret)
			break;
		return arg[1];
	case RAZER_LED_BACKLIGHT_STULT:
	case RAZER_LED_LOGO:
		arg[0] = 1;
		arg[1] = 0x04;
		ret = razer_send_command(r, 3, RAZER_CMD_GET_BRIGHTNESS,
					 3, arg);
		if (ret)
			break;
		return arg[2];
	case RAZER_LED_SCROLL_WHEEL:
		arg[0] = 1;
		arg[1] = 0x01;
		ret = razer_send_command(r, 3, RAZER_CMD_GET_LED_STATE,
					 3, arg);
		if (ret)
			break;
		return arg[2];
	case RAZER_LED_BATTERY:
		arg[0] = 1;
		arg[1] = 0x03;
		ret = razer_send_command(r, 3, RAZER_CMD_GET_LED_STATE,
					 3, arg);
		if (ret)
			break;
		return arg[2];
	case RAZER_LED_GAME:
		arg[0] = 1;
		arg[1] = 0x08;
		ret = razer_send_command(r, 3, RAZER_CMD_GET_LED_STATE,
					 3, arg);
		if (ret)
			break;
		return arg[2];
	case RAZER_LED_MACRO:
	case RAZER_LED_MACRO_ANANSI:
		arg[0] = 1;
		arg[1] = 0x07;
		ret = razer_send_command(r, 3, RAZER_CMD_GET_LED_STATE,
					 3, arg);
		if (ret)
			break;
		return arg[2];
	case RAZER_LED_RED_PROFILE:
		arg[0] = 1;
		arg[1] = 0x0c;
		ret = razer_send_command(r, 3, RAZER_CMD_GET_LED_STATE,
					 3, arg);
		if (ret)
			break;
		return arg[2];
	case RAZER_LED_GREEN_PROFILE:
		arg[0] = 1;
		arg[1] = 0x0d;
		ret = razer_send_command(r, 3, RAZER_CMD_GET_LED_STATE,
					 3, arg);
		if (ret)
			break;
		return arg[2];
	case RAZER_LED_BLUE_PROFILE:
		arg[0] = 1;
		arg[1] = 0x0e;
		ret = razer_send_command(r, 3, RAZER_CMD_GET_LED_STATE,
					 3, arg);
		if (ret)
			break;
		return arg[2];
	default:
		dev_err(r->dev, "unknown LED type\n");
		break;
	}

	dev_err(r->dev, "LED %s brightness request failed\n", rinfo->name);
	return 0;
}

static int razer_select_matrix_effect_extended(struct razer *r,
					       enum razer_matrix_effect effect)
{
	u8 arg[9];
	int ret;

	memset(arg, 0, sizeof(arg));
	arg[0] = 1; /* Variable storage */
	arg[1] = 0x05; /* Backlight LED ID */

	switch (effect) {
	case RAZER_EFFECT_NONE:
		arg[2] = 0; /* Effect ID */
		ret = razer_send_command(r, 0x0f, RAZER_CMD_SET_LED_EFFECT,
					 6, arg);
		break;
	case RAZER_EFFECT_STATIC:
		arg[2] = 1; /* Effect ID */
		arg[5] = 1; /* Unknown */
		arg[6] = 0xff; /* R */
		arg[7] = 0xff; /* G */
		arg[8] = 0xff; /* B */
		ret = razer_send_command(r, 0x0f, RAZER_CMD_SET_LED_EFFECT,
					 9, arg);
		break;
	case RAZER_EFFECT_BREATHING:
		arg[2] = 2; /* Effect ID */
		/*
		 * This sets up random color breathing, to set one color, send
		 * arg[3] = 1, arg[5] = 1 and RGB in arg[6..8], for two colors
		 * arg[3] = 2, arh[5] = 2 and RGB in arh[6..8] and arg[9..11]
		 * send 9 or 12 bytes in those cases.
		 */
		ret = razer_send_command(r, 0x0f, RAZER_CMD_SET_LED_EFFECT,
					 6, arg);
		break;
	case RAZER_EFFECT_SPECTRUM:
		arg[2] = 3; /* Effect ID */
		ret = razer_send_command(r, 0x0f, RAZER_CMD_SET_LED_EFFECT,
					 6, arg);
		break;
	case RAZER_EFFECT_WAVE:
		arg[2] = 4; /* Effect ID */
		arg[3] = 1; /* Wave direction 1 = right, 2 = left */
		arg[4] = 0x28; /* Unknown */
		ret = razer_send_command(r, 0x0f, RAZER_CMD_SET_LED_EFFECT,
					 6, arg);
		break;
	case RAZER_EFFECT_REACTIVE:
		arg[2] = 5; /* Effect ID */
		arg[4] = 2; /* Afterglow delay 1..4 */
		arg[5] = 1; /* Unknown */
		arg[6] = 0xff; /* R */
		arg[7] = 0xff; /* G */
		arg[8] = 0xff; /* B */
		ret = razer_send_command(r, 0x0f, RAZER_CMD_SET_LED_EFFECT,
					 9, arg);
		break;
	case RAZER_EFFECT_RIPPLE:
		arg[2] = 6; /* Effect ID */
		arg[4] = 0; /* ? */
		arg[5] = 0; /* ? */
		ret = razer_send_command(r, 0x0f, RAZER_CMD_SET_LED_EFFECT,
					 6, arg);
		break;
	case RAZER_EFFECT_STARLIGHT:
		arg[2] = 7; /* Effect ID */
		arg[4] = 2; /* Speed 1..3 */
		/* Starlight Type 0 = random, 1 = 1 color, 2 = 2 colors */
		arg[5] = 0;
		ret = razer_send_command(r, 0x0f, RAZER_CMD_SET_LED_EFFECT,
					 6, arg);
		break;
	case RAZER_EFFECT_FIRE:
		arg[2] = 9; /* Effect ID */
		arg[4] = 0; /* ? */
		arg[5] = 0; /* ? */
		ret = razer_send_command(r, 0x0f, RAZER_CMD_SET_LED_EFFECT,
					 6, arg);
		break;
	default:
		dev_err(r->dev, "unknown effect %d", effect);
		ret = -EINVAL;
	}

	return ret;
}

static int razer_select_matrix_effect_legacy(struct razer *r,
					     enum razer_matrix_effect effect)
{
	u8 arg[9];
	int ret;

	memset(arg, 0, sizeof(arg));

	switch (effect) {
	case RAZER_EFFECT_NONE:
		arg[0] = 0; /* Effect ID */
		ret = razer_send_command(r, 3, RAZER_CMD_SET_EFFECT, 1, arg);
		break;
	case RAZER_EFFECT_STATIC:
		arg[0] = 6; /* Effect ID */
		arg[1] = 0xff; /* R */
		arg[2] = 0xff; /* G */
		arg[3] = 0xff; /* B */
		ret = razer_send_command(r, 3, RAZER_CMD_SET_EFFECT, 4, arg);
		break;
	case RAZER_EFFECT_BREATHING:
		arg[0] = 3; /* Effect ID */
		/* Breathing type:
		 * 1 = single, breath a single color
		 * 2 = dual, breath between two colors
		 * 3 = random, breath between random colors
		 */
		arg[1] = 3;
		ret = razer_send_command(r, 3, RAZER_CMD_SET_EFFECT, 8, arg);
		break;
	case RAZER_EFFECT_SPECTRUM:
		arg[0] = 4; /* Effect ID */
		ret = razer_send_command(r, 3, RAZER_CMD_SET_EFFECT, 1, arg);
		break;
	case RAZER_EFFECT_WAVE:
		arg[0] = 1; /* Effect ID */
		arg[1] = 1; /* Wave direction */
		ret = razer_send_command(r, 3, RAZER_CMD_SET_EFFECT, 2, arg);
		break;
	case RAZER_EFFECT_REACTIVE:
		arg[0] = 2; /* Effect ID */
		arg[1] = 2; /* Afterglow delay 1..4 */
		arg[2] = 0xff; /* R */
		arg[3] = 0xff; /* G */
		arg[4] = 0xff; /* B */
		ret = razer_send_command(r, 3, RAZER_CMD_SET_EFFECT, 5, arg);
		break;
	case RAZER_EFFECT_STARLIGHT:
		/* FIXME: very untested */
		arg[0] = 0x19; /* Effect ID */
		arg[1] = 3; /* Random colors */
		arg[2] = 2; /* Speed: 1..3 */
		ret = razer_send_command(r, 3, RAZER_CMD_SET_EFFECT, 3, arg);
		break;
	default:
		dev_err(r->dev, "unsupported effect %d", effect);
		ret = -EINVAL;
	}

	return ret;
}

static int razer_select_matrix_effect(struct razer *r,
				      enum razer_matrix_effect effect)
{
	struct razer_control *rc = razer_get_control(r);
	int ret;

	dev_dbg(r->dev, "select matrix effect %d\n", effect);

	if (r->product->extended_effects)
		ret = razer_select_matrix_effect_extended(r, effect);
	else
		ret = razer_select_matrix_effect_legacy(r, effect);

	if (!ret)
		rc->matrix_effect = effect;

	return ret;
}

static const char* razer_matrix_effects[] = {
	[RAZER_EFFECT_NONE] = "none",
	[RAZER_EFFECT_WAVE] = "wave",
	[RAZER_EFFECT_SPECTRUM] = "spectrum",
	[RAZER_EFFECT_REACTIVE] = "reactive",
	[RAZER_EFFECT_STATIC] = "static",
	[RAZER_EFFECT_BREATHING] = "breathing",
	[RAZER_EFFECT_STARLIGHT] = "starlight",
	[RAZER_EFFECT_RIPPLE] = "ripple",
	[RAZER_EFFECT_FIRE] = "fire",
};

static ssize_t razer_matrix_effect_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct razer *r = dev_get_drvdata(dev->parent);
	struct razer_control *rc = r->control;
	int len = 0;
	int i;

	if (!r->product->effects)
		return sprintf(buf, "[none]\n");

	for (i = 0; r->product->effects[i] != U8_MAX; i++) {
		int effect = r->product->effects[i];
		if (effect == rc->matrix_effect)
			len += sprintf(buf + len, "[%s] ",
				       razer_matrix_effects[effect]);
		else
			len += sprintf(buf + len, "%s ",
				       razer_matrix_effects[effect]);
	}
	len += sprintf(buf + len, "\n");
	return len;
}

static ssize_t razer_matrix_effect_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct razer *r = dev_get_drvdata(dev->parent);
	int effect;
	int ret;
	int i;

	if (!r->product->effects)
		return count;

	for (i = 0; r->product->effects[i] != U8_MAX; i++) {
		effect = r->product->effects[i];
		if (sysfs_streq(buf, razer_matrix_effects[effect]))
			break;
	}
	if (r->product->effects[i] == U8_MAX)
		return count;

	ret = razer_select_matrix_effect(r, effect);
	if (ret)
		return ret;

	return count;
}

DEVICE_ATTR(matrix_effect, 0660, razer_matrix_effect_show,
	    razer_matrix_effect_store);

static struct attribute *razer_led_attrs[] = {
	&dev_attr_matrix_effect.attr,
	NULL,
};

static const struct attribute_group razer_led_group = {
	.attrs = razer_led_attrs,
};

static const struct attribute_group *razer_led_groups[] = {
	&razer_led_group,
	NULL,
};

static int razer_add_leds(struct razer *r)
{
	struct razer_control *rc = razer_get_control(r);
	const struct razer_led_info *rinfo;
	int ret;
	int i = 0;

	rinfo = &r->product->leds[i];
	while (rinfo->name) {
		struct razer_led *rled;

		rled = devm_kzalloc(r->dev, sizeof(*rled), GFP_KERNEL);
		if (!rled)
			return -ENOMEM;

		rled->info = rinfo;
		rled->r = r;

		rled->led.name = devm_kasprintf(r->dev, GFP_KERNEL,
						"razer:%s:%s",
						rinfo->color,
						rinfo->name);
		if (!rled->led.name)
			return -ENOMEM;
		rled->led.brightness_set_blocking = razer_led_set;
		rled->led.brightness_get = razer_led_get;
		rled->led.blink_set_blocking = razer_led_blink;

		/* Backlight LEDs have 255 brightness levels */
		if (razer_led_is_backlight(rled))
			rled->led.max_brightness = LED_FULL;
		else
			rled->led.max_brightness = 1;

		dev_info(r->dev, "adding LED %s\n", rled->led.name);
		ret = devm_led_classdev_register(r->dev, &rled->led);
		if (ret) {
			dev_err(r->dev, "error registering LED %d\n", i);
			break;
		}

		if (razer_led_is_backlight(rled)) {
			/* Set some default brightness */
			led_set_brightness(&rled->led, LED_HALF);
			/* Add a special sysfs file for backlight effects */
			ret = sysfs_create_groups(&rled->led.dev->kobj,
						  razer_led_groups);
			if (ret) {
				dev_err(r->dev,
					"error creating sysfs for LED %d\n", i);
				break;
			}
			rc->backlight = rled;
			rc->backlight_brightness = LED_HALF;
		}

		/*
		 * These LEDs are needed to respond to keys, take them
		 * away from sysfs and control them in this driver.
		 */
		if (rinfo->type == RAZER_LED_GAME) {
			led_set_brightness(&rled->led, LED_OFF);
			rc->game_mode = false;
			rc->gameled = rled;
		}
		if (rinfo->type == RAZER_LED_MACRO) {
			led_set_brightness(&rled->led, LED_OFF);
			rc->macro_state = RAZER_MACRO_OFF;
			rc->macroled = rled;
		}

		i++;
		rinfo = &r->product->leds[i];
	}

	return ret;
}

static ssize_t razer_version_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", DRIVER_VERSION);
}

static ssize_t razer_firmware_version_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct razer *r = dev_get_drvdata(dev);
	struct razer_control *rc = r->control;

	return sprintf(buf, "v%d.%d\n", rc->fw_major, rc->fw_minor);
}

static ssize_t razer_serial_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct razer *r = dev_get_drvdata(dev);
	struct razer_control *rc = r->control;

	return sprintf(buf, "%s\n", rc->serial);
}

static ssize_t razer_device_type_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct razer *r = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", r->product->name);
}

DEVICE_ATTR(version, 0440, razer_version_show, NULL);
DEVICE_ATTR(firmware_version, 0440, razer_firmware_version_show, NULL);
DEVICE_ATTR(device_serial, 0440, razer_serial_show, NULL);
DEVICE_ATTR(device_type, 0440, razer_device_type_show, NULL);

static struct attribute *razer_default_attrs[] = {
	&dev_attr_version.attr,
	&dev_attr_firmware_version.attr,
	&dev_attr_device_serial.attr,
	&dev_attr_device_type.attr,
	&dev_attr_matrix_effect.attr,
	NULL,
};

static const struct attribute_group razer_default_group = {
	.attrs = razer_default_attrs,
};

static const struct attribute_group *razer_default_groups[] = {
	&razer_default_group,
	NULL,
};

static int razer_add_sysfs(struct razer *r)
{
	return sysfs_create_groups(&r->dev->kobj, razer_default_groups);
}

static void razer_macro_mode_off(struct razer *r)
{
	struct razer_control *rc = razer_get_control(r);

	dev_dbg(r->dev, "razer macro done\n");
	/* Userspace has stored the macro or cancelled macro recording */
	rc->macro_state = RAZER_MACRO_OFF;
	led_set_brightness(&rc->macroled->led, LED_OFF);
}

static void razer_toggle_macro(struct razer *r)
{
	struct razer_control *rc = razer_get_control(r);
	unsigned long dummy = 50;

	if (!rc || !rc->macroled)
		return;

	switch (rc->macro_state) {
	case RAZER_MACRO_OFF:
		dev_dbg(r->dev, "razer macro record\n");
		rc->macro_state = RAZER_MACRO_RECORD;
		led_set_brightness(&rc->macroled->led, LED_ON);
		break;
	case RAZER_MACRO_RECORD:
		dev_dbg(r->dev, "razer macro store\n");
		/*
		 * When exiting the recording we should flash the LED
		 * so the user knows it is time to select a key to
		 * store the macro in.
		 */
		rc->macro_state = RAZER_MACRO_STORE;
		led_blink_set(&rc->macroled->led, &dummy, &dummy);
		break;
	case RAZER_MACRO_STORE:
		/*
		 * Usually we should end recording with pressing a key
		 * to store the macro but just clicking this works too.
		 */
		razer_macro_mode_off(r);
		break;
	}
}

static void razer_toggle_game(struct razer *r)
{
	struct razer_control *rc = razer_get_control(r);

	if (!rc || !rc->gameled)
		return;

	rc->game_mode = !rc->game_mode;

	dev_dbg(r->dev, "razer game mode %s\n",
		rc->game_mode ? "ON" : "OFF");

	if (rc->game_mode)
		led_set_brightness(&rc->gameled->led, LED_ON);
	else
		led_set_brightness(&rc->gameled->led, LED_OFF);
}

static void razer_backlight_up(struct razer *r)
{
	struct razer_control *rc = razer_get_control(r);
	int brightness = rc->backlight_brightness;

	dev_dbg(r->dev, "razer brightness up\n");
	brightness += 20;
	if (brightness > LED_FULL)
		brightness = LED_FULL;
	rc->backlight_brightness = brightness;
	led_set_brightness(&rc->backlight->led, brightness);
}

static void razer_backlight_down(struct razer *r)
{
	struct razer_control *rc = razer_get_control(r);
	int brightness = rc->backlight_brightness;

	dev_dbg(r->dev, "razer brightness down\n");
	brightness -= 20;
	if (brightness < LED_OFF)
		brightness = LED_OFF;
	rc->backlight_brightness = brightness;
	led_set_brightness(&rc->backlight->led, brightness);
}

static int razer_keyboard_event(struct hid_device *hdev,
				struct hid_field *field,
				struct hid_usage *usage,
				__s32 value)
{
	struct razer *r = hid_get_drvdata(hdev);
	const struct razer_keyboard *rk = r->product;
	struct razer_control *rc = razer_get_control(r);
	const struct razer_fn_keymap *rmap;

	/*
	 * No event translation needed on the Blade laptops (as far as
	 * we know) or non keyboard interfaces.
	 */
	if (r->product->is_blade ||
	    r->uif->cur_altsetting->desc.bInterfaceProtocol !=
	    USB_INTERFACE_PROTOCOL_KEYBOARD)
		return 0;

	dev_dbg(r->dev, "keycode: %u value %d\n", usage->code, value);

	/* ESC aborts macro recording, also any key in store mode */
	if ((rc->macro_state != RAZER_MACRO_OFF && usage->code == KEY_ESC) ||
	    (!r->fn_pressed && value && rc->macro_state == RAZER_MACRO_STORE))
		razer_macro_mode_off(r);

	/* Only handle special keys when the magic fn key is pressed */
	if (!r->fn_pressed)
		return 0;

	/*
	 * The Razer keyboard have some special keys overlaid on the keys
	 * F9 thru F12 for macro recording, game mode etc. These are
	 * accessed by first pushing down and holding the magic fn key.
	 * Here we either just steal the events or translate the key to
	 * the Linux equivalent, or both.
	 */
	rmap = rk->fn_keymap;
	while (rmap->name) {
		if (usage->code == rmap->from_key)
			break;
		rmap++;
	}
	if (!rmap->name) {
		/* No mapping found, only report on pressing */
		if (value)
			dev_info(r->dev, "Unknown Razer fn key %u\n",
				 usage->code);
		return 1;
	}

	/* Internal processing only on pushing down keys */
	if (value) {
		if (rmap->is_macro)
			razer_toggle_macro(r);

		if (rmap->is_game)
			razer_toggle_game(r);

		if (rmap->is_bl_down)
			razer_backlight_down(r);

		if (rmap->is_bl_up)
			razer_backlight_up(r);

		if (rmap->is_razereffect)
			dev_info(r->dev, "toggle razer effect key\n");

		if (rmap->is_profile)
			dev_info(r->dev, "toggle profile key\n");
	}

	if (rmap->to_key) {
		dev_info(r->dev, "key %u translated to %u\n",
			 usage->code, rmap->to_key);
		input_event(field->hidinput->input, usage->type,
			    rmap->to_key, value);
	}

	/* Just discard anything else when holding down fn */
	return 1;
}

void razer_check_raw_keycode(struct razer *r,
			     struct razer_control *rc,
			     u8 raw_keycode,
			     unsigned long *currently_pressed)
{
	const struct razer_keyboard *rk = r->product;
	const struct razer_raw_keymap *rmap;
	int i = 0;

	rmap = rk->raw_keymap;
	while (rmap->name) {
		if (raw_keycode == rmap->razer_key)
			break;
		i++;
		rmap++;
	}
	if (!rmap->name) {
		dev_info(r->dev, "unknown Razer key 0x%02x\n", raw_keycode);
		return;
	}

	/*
	 * FIXME: would be nice to light up a LED under KEY_MUTE
	 * if possible BlackWidow Elite has a LED under this key.
	 */
	if (!test_bit(i, &r->active_keys)) {
		set_bit(i, &r->active_keys);
		dev_dbg(r->dev, "%s DOWN\n", rmap->name);
		if (rmap->is_fn)
			r->fn_pressed = true;
		else
			input_report_key(rc->input, rmap->linux_key, 1);
	}
	/*
	 * Indicate that this key is currently pressed, even if
	 * we e.g. push a key, hold it down and then push another
	 * key at the same time: in that case we get an event
	 * when the second key is pressed, including the first
	 * key, but it is already reported above, so all we
	 * note is that it is still pressed by setting the
	 * corresponding bit in "currently_pressed".
	 */
	set_bit(i, currently_pressed);
}

int razer_keyboard_scan_game_keys(struct razer *r, struct razer_control *rc,
				  u8 *data, int size)
{
	const struct razer_keyboard *rk = r->product;
	const struct razer_raw_keymap *rmap;
	unsigned long currently_pressed = 0;
	int i;

	/* Do not scan index 0: this just contains 0x04 (game accessory) */
	for (i = size-1; i != 0; i --) {
		/* Skip all zeroes */
		if (data[i] == 0x00)
			continue;

		razer_check_raw_keycode(r, rc, data[i], &currently_pressed);
	}

	/*
	 * We get events with the absence of a keycode (if no keys pressed
	 * just an array of zeroes) in response to a key being released.
	 *
	 * To report key release, we keep track of all the keys that are
	 * currently pressed in a bitmap in r->active_keys and when the
	 * bit is set for a key and an event comes in with this key absent,
	 * we report the key as released.
	 */
	i = 0;
	rmap = rk->raw_keymap;
	while (rmap->name) {
		if (test_bit(i, &r->active_keys) &&
		    !test_bit(i, &currently_pressed)) {
			clear_bit(i, &r->active_keys);
			dev_dbg(r->dev, "%s UP\n", rmap->name);
			if (rmap->is_fn)
				r->fn_pressed = false;
			else
				input_report_key(rc->input, rmap->linux_key, 0);
		}
		i++;
		rmap++;
	}

	input_sync(rc->input);
	/* This will consume the event */
	return 1;
}

/**
 * Raw event processing
 *
 * This is needed when the keyboard is switched into RAZER_MODE_DRIVER,
 * as that changes the characteristics of the keyboard to be more of a
 * game controller, which means it starts to emit new and interesting
 * events instead of pretending to be a normal keyboard.
 *
 * The raw event is 16 bytes, and if the first byte [0] is 0x04 (which
 * means "game controller") we have a stack of key events after this. If
 * just one key is pressed, this will be in the second byte [1], but if
 * more than one key is pressed, those will just stack up in [1], [2] ...
 */
static int razer_keyboard_raw_event(struct hid_device *hdev,
				    struct hid_report *report,
				    u8 *data, int size)
{
	struct razer *r = hid_get_drvdata(hdev);
	struct razer_control *rc = razer_get_control(r);
	int ret;

	/* No event translation needed on the Blade laptops */
	if (r->product->is_blade)
		return 0;

	/* Only process raw events in driver mode */
	if (!rc || rc->mode != RAZER_MODE_DRIVER)
		return 0;

#if 0
	int i;

	dev_info(r->dev, "raw mouse event: ");
	for (i = 0; i < size; i++)
		pr_cont("%02x ", data[i]);
	pr_cont("\n");
#endif

	/*
	 * Special keys from Razer keyboards appear on the keyboard
	 * interfaces, and the first byte is always 0x04.
	 */
	if ((r->uif->cur_altsetting->desc.bInterfaceProtocol ==
	     USB_INTERFACE_PROTOCOL_KEYBOARD) && data[0] == 0x04) {
		ret = razer_keyboard_scan_game_keys(r, rc, data, size);
		if (ret)
			/* Eat this event */
			return -ENODATA;
	}

	/*
	 * The BlackWidow Elite has a volume wheel that will appear
	 * as a mouse scroll wheel in driver mode. Translate the
	 * mouse scroll events to volume up/down events as is the default
	 * in the vendor driver. The events look like this:
	 *
	 * 00 00 00 01 00 00 00 00 up
	 * 00 00 00 ff 00 00 00 00 down
	 */
	if (r->product->volume_wheel &&
	    (r->uif->cur_altsetting->desc.bInterfaceProtocol ==
	     USB_INTERFACE_PROTOCOL_MOUSE)) {
		if (data[3] == 0x01) {
			input_report_key(rc->input, KEY_VOLUMEUP, 1);
			input_report_key(rc->input, KEY_VOLUMEUP, 0);
			input_sync(rc->input);
			return -ENODATA;
		}
		if (data[3] == 0xff) {
			input_report_key(rc->input, KEY_VOLUMEDOWN, 1);
			input_report_key(rc->input, KEY_VOLUMEDOWN, 0);
			input_sync(rc->input);
			return -ENODATA;
		}
	}

	return 0;
}

static int razer_probe_control_interface(struct razer *r,
					 const struct razer_keyboard *rk)
{
	const char* razer_modes[] = {
		[RAZER_MODE_NORMAL] = "normal",
		[RAZER_MODE_FACTORY] = "factory",
		[RAZER_MODE_DRIVER] = "driver",
		[RAZER_MODE_UNKNOWN] = "unknown",
	};
	struct razer_control *rc;
	u8 resp[2];
	int ret;

	rc = devm_kzalloc(r->dev, sizeof(*rc), GFP_KERNEL);
	if (!rc)
		return -ENOMEM;

	r->control = rc;
	mutex_init(&rc->lock);
	rc->matrix_effect = RAZER_EFFECT_NONE;
	/* This is how other interfaces will access the control interface */
	dev_set_drvdata(&r->udev->dev, rc);

	/* Allocate a side-channel input device for special keys */
	rc->input = devm_input_allocate_device(r->dev);
	if (!rc->input)
		return -ENOMEM;
	input_set_drvdata(rc->input, r);
	rc->input->dev.parent = &r->hdev->dev;
        rc->input->phys = r->hdev->phys;
        rc->input->uniq = r->hdev->uniq;
        rc->input->id.bustype = r->hdev->bus;
        rc->input->id.vendor = r->hdev->vendor;
        rc->input->id.product = r->hdev->product;
        rc->input->id.version = r->hdev->version;
	rc->input->name = devm_kasprintf(r->dev, GFP_KERNEL,
					 "%s-keys", r->hdev->name);
	if (!rc->input->name)
		return -ENOMEM;
	/* Those are the special keys we can report */
	input_set_capability(rc->input, EV_KEY, KEY_MUTE);
	input_set_capability(rc->input, EV_KEY, KEY_PLAYPAUSE);
	input_set_capability(rc->input, EV_KEY, KEY_NEXTSONG);
	input_set_capability(rc->input, EV_KEY, KEY_PREVIOUSSONG);
	if (r->product->volume_wheel) {
		input_set_capability(rc->input, EV_KEY, KEY_VOLUMEUP);
		input_set_capability(rc->input, EV_KEY, KEY_VOLUMEDOWN);
	}
	ret = input_register_device(rc->input);
	if (ret < 0)
		return ret;

	/* Look up firmware version on the control interface */
	memset(resp, 0, sizeof(resp));
	ret = razer_send_command(r, 0, RAZER_CMD_GET_FW_VER, 2, resp);
	if (ret) {
		dev_err(r->dev, "firmware version request failed\n");
		return ret;
	}
	rc->fw_major = resp[0];
	rc->fw_minor = resp[1];

	if (r->product->is_blade) {
		strscpy(rc->serial, dmi_get_system_info(DMI_PRODUCT_SERIAL),
			sizeof(rc->serial));
	} else {
		u8 serbuf[0x1a];

		memset(serbuf, 0, sizeof(serbuf));
		ret = razer_send_command(r, 0, RAZER_CMD_GET_SERIAL,
					 sizeof(serbuf), serbuf);
		if (ret) {
			dev_err(r->dev, "serial number request failed\n");
			return ret;
		}
		strscpy(rc->serial, serbuf, sizeof(rc->serial));
	}
	/* Device-unique so toss this into the entropy pool */
	add_device_randomness(rc->serial, strlen(rc->serial));

	memset(resp, 0, sizeof(resp));
	ret = razer_send_command(r, 0, RAZER_CMD_GET_VARIABLE, 2, resp);
	if (ret) {
		dev_err(r->dev, "get mode request failed\n");
		return ret;
	}
	switch (resp[0]) {
	case 0x00:
		rc->mode = RAZER_MODE_NORMAL;
		break;
	case 0x02:
		rc->mode = RAZER_MODE_FACTORY;
		break;
	case 0x03:
		rc->mode = RAZER_MODE_DRIVER;
		break;
	default:
		rc->mode = RAZER_MODE_UNKNOWN;
		break;
	}

	memset(resp, 0, sizeof(resp));
	ret = razer_send_command(r, 0, RAZER_CMD_GET_LAYOUT, 2, resp);
	if (ret) {
		dev_err(r->dev, "get layout request failed\n");
		return ret;
	}
	rc->layout = resp[0];

	ret = razer_add_sysfs(r);
	if (ret)
		return ret;

	ret = razer_add_leds(r);
	if (ret)
		return ret;

	/* Enter driver mode and take control */
	ret = razer_set_device_mode(r, RAZER_MODE_DRIVER);
	if (ret) {
		dev_err(r->dev, "failed to enter driver mode\n");
		return ret;
	}

	dev_info(r->dev,
		 "HID device %s connected, FW v%d.%d, serial: %s, mode %s, layout %d\n",
		 rk->name, rc->fw_major, rc->fw_minor, rc->serial,
		 razer_modes[rc->mode], rc->layout);

	return 0;
}

static int razer_keyboard_probe(struct hid_device *hdev,
				const struct hid_device_id *id)
{
	struct device *dev = &hdev->dev;
	const struct razer_keyboard *rk =
		(const struct razer_keyboard *)id->driver_data;
	struct usb_interface *uif;
	struct usb_device *udev;
	struct razer *r;
	int ret;

	uif = to_usb_interface(dev->parent);
	udev = interface_to_usbdev(uif);

	r = devm_kzalloc(dev, sizeof(*r), GFP_KERNEL);
	if (!r)
		return -ENOMEM;

	r->dev = dev;
	r->hdev = hdev;
	r->product = rk;
	r->uif = uif;
	r->udev = udev;
	hid_set_drvdata(hdev, r);

	/*
	 * The mouse interface is always the control interface. We only
	 * want to create device sysfs files etc for the control interface.
	 */
	if (uif->cur_altsetting->desc.bInterfaceProtocol ==
	    USB_INTERFACE_PROTOCOL_MOUSE) {
		ret = razer_probe_control_interface(r, rk);
		if (ret)
			return ret;
	}

	ret = hid_parse(hdev);
	if (ret) {
		dev_err(dev, "HID device Razer parse failed\n");
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		dev_err(dev, "HID device Razer hardware failed to start\n");
		return ret;
	}

	return 0;
}

static void razer_keyboard_remove(struct hid_device *hdev)
{
	struct razer *r = hid_get_drvdata(hdev);

	hid_hw_stop(hdev);

	/* Remove extenstions on the control interface */
	if (r->control) {
		int ret;

		/* Return device to normal mode */
		ret = razer_set_device_mode(r, RAZER_MODE_NORMAL);
		if (ret)
			dev_err(r->dev, "failed to enter normal mode\n");

		sysfs_remove_groups(&r->dev->kobj, razer_default_groups);
		dev_set_drvdata(&r->udev->dev, NULL);

		dev_info(r->dev, "HID device Razer %s disconnected\n",
			 r->product->name);
	}
}

/*
 * This is the Razer Keyboard device database. This database is ordered in
 * USB device ID order. Please keep that order when adding new devices.
 */

/* Blade laptop LED set: special backlight and logo */
static const struct razer_led_info razer_blade_leds[] = {
	{
		.name = "kbd_backlight",
		.color = "rgb",
		.type = RAZER_LED_BACKLIGHT_BLADE,
	},
	{
		.name = "logo",
		.color = "rgb",
		.type = RAZER_LED_LOGO,
	},
	{ },
};

/* Keyboards with backlight, macro and game leds */
static const struct razer_led_info razer_anansi_leds[] = {
	{
		.name = "kbd_backlight",
		.color = "rgb",
		.type = RAZER_LED_BACKLIGHT,
	},
	{
		.name = "macro",
		.color = "red",
		.type = RAZER_LED_MACRO_ANANSI,
	},
	{
		.name = "game",
		.color = "white",
		.type = RAZER_LED_GAME,
	},
	{ },
};

/* Keyboards with backlight, macro and game leds */
static const struct razer_led_info razer_bl_macro_game[] = {
	{
		.name = "kbd_backlight",
		.color = "rgb",
		.type = RAZER_LED_BACKLIGHT,
	},
	{
		.name = "macro",
		.color = "red",
		.type = RAZER_LED_MACRO,
	},
	{
		.name = "game",
		.color = "white",
		.type = RAZER_LED_GAME,
	},
	{ },
};

/* Keyboards such as BlackWidow Stealth and Ultimate */
static const struct razer_led_info razer_stult_leds[] = {
	{
		.name = "kbd_backlight",
		.color = "rgb",
		.type = RAZER_LED_BACKLIGHT_STULT,
	},
	{
		.name = "macro",
		.color = "red",
		.type = RAZER_LED_MACRO,
	},
	{
		.name = "game",
		.color = "white",
		.type = RAZER_LED_GAME,
	},
	{ },
};

/* Keyboards with standard backlight, red, green and blue profile LEDs */
static const struct razer_led_info razer_bl_red_green_blue[] = {
	{
		.name = "kbd_backlight",
		.color = "rgb",
		.type = RAZER_LED_BACKLIGHT,
	},
	{
		.name = "profile",
		.color = "red",
		.type = RAZER_LED_RED_PROFILE,
	},
	{
		.name = "profile",
		.color = "green",
		.type = RAZER_LED_GREEN_PROFILE,
	},
	{
		.name = "profile",
		.color = "blue",
		.type = RAZER_LED_BLUE_PROFILE,
	},
	{ },
};

/* Stealth ultimate models, Tartarus, Orbweaver has a limited set of effects */
static const enum razer_matrix_effect razer_basic_effects[] = {
	RAZER_EFFECT_NONE,
	RAZER_EFFECT_STATIC,
	RAZER_EFFECT_BREATHING,
	U8_MAX,
};

/* Orbweaver and Tartarus chroma adds the spectrum effect */
static const enum razer_matrix_effect razer_chroma_effects[] = {
	RAZER_EFFECT_NONE,
	RAZER_EFFECT_STATIC,
	RAZER_EFFECT_BREATHING,
	RAZER_EFFECT_SPECTRUM,
	U8_MAX,
};

/* Anansi has none, static and spectrum, notably no breath/pulsate */
static const enum razer_matrix_effect razer_anansi_effects[] = {
	RAZER_EFFECT_NONE,
	RAZER_EFFECT_STATIC,
	RAZER_EFFECT_SPECTRUM,
	U8_MAX,
};

/* BlackWidow Chroma, no starlight (same as Blade stealth) */
static const enum razer_matrix_effect razer_bw_chroma_effects[] = {
	RAZER_EFFECT_NONE,
	RAZER_EFFECT_STATIC,
	RAZER_EFFECT_WAVE,
	RAZER_EFFECT_SPECTRUM,
	RAZER_EFFECT_REACTIVE,
	RAZER_EFFECT_BREATHING,
	U8_MAX,
};

/* Deathstalker Chroma effects, no reactice effect */
static const enum razer_matrix_effect razer_ds_chroma_effects[] = {
	RAZER_EFFECT_NONE,
	RAZER_EFFECT_STATIC,
	RAZER_EFFECT_WAVE,
	RAZER_EFFECT_SPECTRUM,
	RAZER_EFFECT_BREATHING,
	U8_MAX,
};

/* Blade stealth effects are notably missing Starlight */
static const enum razer_matrix_effect razer_blade_stealth_effects[] = {
	RAZER_EFFECT_NONE,
	RAZER_EFFECT_STATIC,
	RAZER_EFFECT_WAVE,
	RAZER_EFFECT_SPECTRUM,
	RAZER_EFFECT_REACTIVE,
	RAZER_EFFECT_BREATHING,
	U8_MAX,
};

/* Blade QHD, pro versions and 2018 version */
static const enum razer_matrix_effect razer_blade_pro_effects[] = {
	RAZER_EFFECT_NONE,
	RAZER_EFFECT_STATIC,
	RAZER_EFFECT_SPECTRUM,
	RAZER_EFFECT_WAVE,
	RAZER_EFFECT_REACTIVE,
	RAZER_EFFECT_BREATHING,
	RAZER_EFFECT_STARLIGHT,
	U8_MAX,
};

/* BlackWidow Ultimate 2016 and X effects */
static const enum razer_matrix_effect razer_ult16_effects[] = {
	RAZER_EFFECT_NONE,
	RAZER_EFFECT_STATIC,
	RAZER_EFFECT_WAVE,
	RAZER_EFFECT_REACTIVE,
	RAZER_EFFECT_BREATHING,
	RAZER_EFFECT_STARLIGHT,
	U8_MAX,
};

/* Ornata, Ornata chroma and Cynosa */
static const enum razer_matrix_effect razer_ornata_family_effects[] = {
	RAZER_EFFECT_NONE,
	RAZER_EFFECT_STATIC,
	RAZER_EFFECT_SPECTRUM,
	RAZER_EFFECT_WAVE,
	RAZER_EFFECT_REACTIVE,
	RAZER_EFFECT_BREATHING,
	RAZER_EFFECT_STARLIGHT,
	RAZER_EFFECT_RIPPLE,
	U8_MAX,
};

/* The BlackWidow Elite has a whole slew of built-in effects */
static const enum razer_matrix_effect razer_bw_elite_effects[] = {
	RAZER_EFFECT_NONE,
	RAZER_EFFECT_STATIC,
	RAZER_EFFECT_SPECTRUM,
	RAZER_EFFECT_WAVE,
	RAZER_EFFECT_REACTIVE,
	RAZER_EFFECT_BREATHING,
	RAZER_EFFECT_STARLIGHT,
	RAZER_EFFECT_RIPPLE,
	RAZER_EFFECT_FIRE,
	U8_MAX,
};

static const struct razer_raw_keymap razer_default_raw_keymap[] = {
	{
		.name = "fn KEY",
		.razer_key = 0x01,
		.is_fn = true,
	},
	{ },
};

static const struct razer_raw_keymap razer_bw_elite_raw_keymap[] = {
	{
		.name = "fn KEY",
		.razer_key = 0x01,
		.is_fn = true,
	},
	{
		.name = "MUTE/UNMUTE",
		.razer_key = 0x52,
		.linux_key = KEY_MUTE,
	},
	{
		.name = "NEXT SONG",
		.razer_key = 0x53,
		.linux_key = KEY_NEXTSONG,
	},
	{
		.name = "PREVIOUS SONG",
		.razer_key = 0x54,
		.linux_key = KEY_PREVIOUSSONG,
	},
	{
		.name = "PLAY/PAUSE",
		.razer_key = 0x55,
		.linux_key = KEY_PLAYPAUSE,
	},
	{ },
};

/* Keys that generate a special key when pressing fn */
static const struct razer_fn_keymap razer_default_fn_keymap[] = {
	{
		.name = "MUTE/UNMUTE",
		.from_key = KEY_F1,
		.to_key = KEY_MUTE,
	},
	{
		.name = "VOLUMEDOWN",
		.from_key = KEY_F2,
		.to_key = KEY_VOLUMEDOWN,
	},
	{
		.name = "VOLUMEUP",
		.from_key = KEY_F3,
		.to_key = KEY_VOLUMEUP,
	},
	{
		.name = "PREVIOUS SONG",
		.from_key = KEY_F5,
		.to_key = KEY_PREVIOUSSONG,
	},
	{
		.name = "PLAY/PAUSE",
		.from_key = KEY_F6,
		.to_key = KEY_PLAYPAUSE,
	},
	{
		.name = "NEXT SONG",
		.from_key = KEY_F7,
		.to_key = KEY_NEXTSONG,
	},
	{
		.name = "MACRO",
		.from_key = KEY_F9,
		.to_key = KEY_MACRO,
		.is_macro = true,
	},
	{
		.name = "GAME",
		.from_key = KEY_F10,
		.is_game = true,
	},
	{
		.name = "KEYBOARD BACKLIGHT DOWN",
		.from_key = KEY_F11,
		.is_bl_down = true,
	},
	{
		.name = "KEYBOARD BACKLIGHT UP",
		.from_key = KEY_F12,
		.is_bl_up = true,
	},
	{
		.name = "SLEEP",
		.from_key = KEY_PAUSE,
		.to_key = KEY_SLEEP,
	},
	{
		.name = "PROFILE",
		.from_key = KEY_COMPOSE,
		.is_profile = true,
	},
	{ },
};

/* Special key assignments on the BlackWidow Stealth */
static const struct razer_fn_keymap razer_bw_stealth_fn_keymap[] = {
	{
		.name = "MUTE/UNMUTE",
		.from_key = KEY_F1,
		.to_key = KEY_MUTE,
	},
	{
		.name = "VOLUMEDOWN",
		.from_key = KEY_F2,
		.to_key = KEY_VOLUMEDOWN,
	},
	{
		.name = "VOLUMEUP",
		.from_key = KEY_F3,
		.to_key = KEY_VOLUMEUP,
	},
	{
		.name = "PLAY/PAUSE",
		.from_key = KEY_F5,
		.to_key = KEY_PLAYPAUSE,
	},
	{
		.name = "STOP CD",
		.from_key = KEY_F6,
		.to_key = KEY_STOPCD,
	},
	{
		.name = "PREVIOUS SONG",
		.from_key = KEY_F7,
		.to_key = KEY_PREVIOUSSONG,
	},
	{
		.name = "NEXT SONG",
		.from_key = KEY_F8,
		.to_key = KEY_NEXTSONG,
	},
	{
		.name = "GAME",
		.from_key = KEY_F11,
		.is_macro = true,
	},
	{
		.name = "RAZER EFFECT",
		.from_key = KEY_F12,
		.is_razereffect = true,
	},
	{
		.name = "MACRO",
		.from_key = KEY_RIGHTALT,
		.is_macro = true,
	},
	{
		.name = "SLEEP",
		.from_key = KEY_PAUSE,
		.to_key = KEY_SLEEP,
	},
	{ },
};

/*
 * The BlackWidow Elite has a few separate keys that are accessed as raw
 * instead of overlaying function keys.
 */
static const struct razer_fn_keymap razer_bw_elite_fn_keymap[] = {
	{
		.name = "MACRO",
		.from_key = KEY_F9,
		.to_key = KEY_MACRO,
		.is_macro = true,
	},
	{
		.name = "GAME",
		.from_key = KEY_F10,
		.is_game = true,
	},
	{
		.name = "KEYBOARD BACKLIGHT DOWN",
		.from_key = KEY_F11,
		.is_bl_down = true,
	},
	{
		.name = "KEYBOARD BACKLIGHT UP",
		.from_key = KEY_F12,
		.is_bl_up = true,
	},
	{
		.name = "SLEEP",
		.from_key = KEY_PAUSE,
		.to_key = KEY_SLEEP,
	},
	{
		.name = "PROFILE",
		.from_key = KEY_COMPOSE,
		.is_profile = true,
	},
	{ },
};

static const struct razer_keyboard razer_orbweaver = {
	.name = "Razer Orbweaver",
	.leds = razer_bl_red_green_blue,
	.effects = razer_basic_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_orbweaver_chroma = {
	.name = "Razer Orbweaver Chroma",
	.leds = razer_bl_red_green_blue,
	.effects = razer_chroma_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_nostromo = {
	.name = "Razer Nostromo",
	.leds = razer_bl_red_green_blue,
	/* This keyboard has no effects */
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blackwidow_stealth = {
	.name = "Razer BlackWidow Stealth",
	.is_bw_stealth = true,
	.leds = razer_stult_leds,
	.effects = razer_basic_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blackwidow_stealth_edition = {
	.name = "Razer BlackWidow Stealth Edition",
	.leds = razer_stult_leds,
	.effects = razer_basic_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_bw_stealth_fn_keymap,
};

static const struct razer_keyboard razer_blackwidow_ultimate_2012 = {
	.name = "Razer BlackWidow Ultimate 2012",
	.leds = razer_stult_leds,
	.effects = razer_basic_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blackwidow_ultimate_2013 = {
	.name = "Razer BlackWidow Ultimate 2013",
	.leds = razer_stult_leds,
	.effects = razer_basic_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blackwidow_ultimate_2016 = {
	.name = "Razer BlackWidow Ultimate 2016",
	.leds = razer_bl_macro_game,
	.effects = razer_ult16_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blackwidow_x_ultimate = {
	.name = "Razer BlackWidow X Ultimate",
	.leds = razer_bl_macro_game,
	.effects = razer_ult16_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blade_stealth = {
	.name = "Razer Blade Stealth",
	.is_blade = true,
	.leds = razer_blade_leds,
	.effects = razer_blade_stealth_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blade_stealth_late_2016 = {
	.name = "Razer Blade Stealth (Late 2016)",
	.is_blade = true,
	.leds = razer_blade_leds,
	.effects = razer_blade_stealth_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blade_qhd = {
	.name = "Razer Blade (QHD)",
	.is_blade = true,
	.leds = razer_blade_leds,
	.effects = razer_blade_pro_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blade_pro_late_2016 = {
	.name = "Razer Blade Pro (Late 2016)",
	.is_blade = true,
	.leds = razer_blade_leds,
	.effects = razer_blade_pro_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blade_2018 = {
	.name = "Razer Blade 15 (2018)",
	.is_blade = true,
	.leds = razer_blade_leds,
	.effects = razer_blade_pro_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_tartarus = {
	.name = "Razer Tartarus",
	.leds = razer_bl_red_green_blue,
	.effects = razer_basic_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_tartarus_chroma = {
	.name = "Razer Tartarus Chroma",
	.leds = razer_bl_red_green_blue,
	.effects = razer_chroma_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_deathstalker_expert = {
	.name = "Razer Deathstalker Expert",
	.leds = razer_bl_macro_game,
	.effects = razer_basic_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blackwidow_chroma = {
	.name = "Razer BlackWidow Chroma",
	.leds = razer_bl_macro_game,
	.effects = razer_bw_chroma_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blackwidow_overwatch = {
	.name = "Razer BlackWidow Chroma (Overwatch)",
	.leds = razer_bl_macro_game,
	.effects = razer_bw_chroma_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_deathstalker_chroma = {
	.name = "Razer Deathstalker Chroma",
	.leds = razer_bl_macro_game,
	.effects = razer_ds_chroma_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blackwidow_chroma_te = {
	.name = "Razer BlackWidow Chroma Tournament Edition",
	.leds = razer_bl_macro_game,
	.effects = razer_bw_chroma_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blackwidow_x_chroma = {
	.name = "Razer BlackWidow X Chroma",
	.leds = razer_bl_macro_game,
	.effects = razer_bw_chroma_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blackwidow_x_chroma_te = {
	.name = "Razer BlackWidow X Chroma Tournament Edition",
	.leds = razer_bl_macro_game,
	.effects = razer_bw_chroma_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_ornata_chroma = {
	.name = "Razer Ornata Chroma",
	.extended_effects = true,
	.leds = razer_bl_macro_game,
	.effects = razer_ornata_family_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_cynosa_chroma = {
	.name = "Razer Cynosa Chroma",
	.extended_effects = true,
	.leds = razer_bl_macro_game,
	.effects = razer_ornata_family_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_ornata = {
	.name = "Razer Ornata",
	.extended_effects = true,
	.leds = razer_bl_macro_game,
	.effects = razer_ornata_family_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_anansi = {
	.name = "Razer Anansi",
	/* This device has peculiar report and request indices */
	.req_res_index_2 = true,
	/* The LEDs also need special treatment */
	.leds = razer_anansi_leds,
	/* And a special shortlist of effects */
	.effects = razer_anansi_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blackwidow_chroma_v2 = {
	.name = "Razer BlackWidow Chroma v2",
	.leds = razer_bl_macro_game,
	/* FIXME: uncertain about the effect list, please test */
	.effects = razer_ornata_family_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blade_late_2016 = {
	.name = "Razer Blade (Late 2016)",
	.is_blade = true,
	.leds = razer_blade_leds,
	.effects = razer_blade_pro_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blade_stealth_mid_2017 = {
	.name = "Razer Blade Stealth (Mid 2017)",
	.is_blade = true,
	.leds = razer_blade_leds,
	.effects = razer_blade_stealth_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blade_pro_2017 = {
	.name = "Razer Blade Pro (2017)",
	.is_blade = true,
	.leds = razer_blade_leds,
	.effects = razer_blade_pro_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blackwidow_elite = {
	.name = "Razer BlackWidow Elite",
	.volume_wheel = true,
	.leds = razer_bl_macro_game,
	.extended_effects = true,
	.effects = razer_bw_elite_effects,
	.raw_keymap = razer_bw_elite_raw_keymap,
	.fn_keymap = razer_bw_elite_fn_keymap,
};

static const struct razer_keyboard razer_blade_pro_2017_fullhd = {
	.name = "Razer Blade Pro FullHD (2017)",
	.is_blade = true,
	.leds = razer_blade_leds,
	.effects = razer_blade_pro_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct razer_keyboard razer_blade_stealth_late_2017 = {
	.name = "Razer Blade Stealth (Late 2017)",
	.is_blade = true,
	.leds = razer_blade_leds,
	.effects = razer_blade_stealth_effects,
	.raw_keymap = razer_default_raw_keymap,
	.fn_keymap = razer_default_fn_keymap,
};

static const struct hid_device_id razer_keyboard_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_ORBWEAVER),
	  .driver_data = (kernel_ulong_t)&razer_orbweaver },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_ORBWEAVER_CHROMA),
	  .driver_data = (kernel_ulong_t)&razer_orbweaver_chroma },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_NOSTROMO),
	  .driver_data = (kernel_ulong_t)&razer_nostromo },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLACKWIDOW_STEALTH),
	  .driver_data = (kernel_ulong_t)&razer_blackwidow_stealth },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLACKWIDOW_STEALTH_EDITION),
	  .driver_data = (kernel_ulong_t)&razer_blackwidow_stealth_edition },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLACKWIDOW_ULTIMATE_2012),
	  .driver_data = (kernel_ulong_t)&razer_blackwidow_ultimate_2012 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLACKWIDOW_ULTIMATE_2013),
	  .driver_data = (kernel_ulong_t)&razer_blackwidow_ultimate_2013 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLACKWIDOW_ULTIMATE_2016),
	  .driver_data = (kernel_ulong_t)&razer_blackwidow_ultimate_2016 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLACKWIDOW_X_ULTIMATE),
	  .driver_data = (kernel_ulong_t)&razer_blackwidow_x_ultimate },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLADE_STEALTH),
	  .driver_data = (kernel_ulong_t)&razer_blade_stealth },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLADE_STEALTH_LATE_2016),
	  .driver_data = (kernel_ulong_t)&razer_blade_stealth_late_2016 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLADE_QHD),
	  .driver_data = (kernel_ulong_t)&razer_blade_qhd },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLADE_PRO_LATE_2016),
	  .driver_data = (kernel_ulong_t)&razer_blade_pro_late_2016 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLADE_2018),
	  .driver_data = (kernel_ulong_t)&razer_blade_2018 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_TARTARUS),
	  .driver_data = (kernel_ulong_t)&razer_tartarus },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_TARTARUS_CHROMA),
	  .driver_data = (kernel_ulong_t)&razer_tartarus_chroma },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_DEATHSTALKER_EXPERT),
	  .driver_data = (kernel_ulong_t)&razer_deathstalker_expert },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLACKWIDOW_CHROMA),
	  .driver_data = (kernel_ulong_t)&razer_blackwidow_chroma },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLACKWIDOW_OVERWATCH),
	  .driver_data = (kernel_ulong_t)&razer_blackwidow_overwatch },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_DEATHSTALKER_CHROMA),
	  .driver_data = (kernel_ulong_t)&razer_deathstalker_chroma },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLACKWIDOW_CHROMA_TE),
	  .driver_data = (kernel_ulong_t)&razer_blackwidow_chroma_te },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLACKWIDOW_X_CHROMA),
	  .driver_data = (kernel_ulong_t)&razer_blackwidow_x_chroma },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLACKWIDOW_X_CHROMA_TE),
	  .driver_data = (kernel_ulong_t)&razer_blackwidow_x_chroma_te },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_ORNATA_CHROMA),
	  .driver_data = (kernel_ulong_t)&razer_ornata_chroma },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_CYNOSA_CHROMA),
	  .driver_data = (kernel_ulong_t)&razer_cynosa_chroma },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_ORNATA),
	  .driver_data = (kernel_ulong_t)&razer_ornata },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_ANANSI),
	  .driver_data = (kernel_ulong_t)&razer_anansi },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLACKWIDOW_CHROMA_V2),
	  .driver_data = (kernel_ulong_t)&razer_blackwidow_chroma_v2 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLADE_LATE_2016),
	  .driver_data = (kernel_ulong_t)&razer_blade_late_2016 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLADE_STEALTH_MID_2017),
	  .driver_data = (kernel_ulong_t)&razer_blade_stealth_mid_2017 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLADE_PRO_2017),
	  .driver_data = (kernel_ulong_t)&razer_blade_pro_2017 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLACKWIDOW_ELITE),
	  .driver_data = (kernel_ulong_t)&razer_blackwidow_elite },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLADE_PRO_2017_FULLHD),
	  .driver_data = (kernel_ulong_t)&razer_blade_pro_2017_fullhd },
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_BLADE_STEALTH_LATE_2017),
	  .driver_data = (kernel_ulong_t)&razer_blade_stealth_late_2017 },
	{ }
};
MODULE_DEVICE_TABLE(hid, razer_keyboard_devices);

static struct hid_driver razer_keyboard_driver = {
	.name = "razer-keyboard",
	.id_table = razer_keyboard_devices,
	.probe = razer_keyboard_probe,
	.remove = razer_keyboard_remove,
	.event = razer_keyboard_event,
	.raw_event = razer_keyboard_raw_event,
};
module_hid_driver(razer_keyboard_driver);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_LICENSE("GPL");
