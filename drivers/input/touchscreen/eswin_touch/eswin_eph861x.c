/*
 * ESWIN EPH861X series Touchscreen driver
 *
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

// uncomment to enable the dev_dbg prints to dmesg
#define DEBUG
// uncomment to test with input forced open
//#define INPUT_DEVICE_ALWAYS_OPEN
#include <linux/types.h>
#include <uapi/asm-generic/errno-base.h>
#include <linux/sysfs.h>

#include <linux/mutex.h>
#include <linux/kobject.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <uapi/linux/input-event-codes.h>
#include <uapi/linux/stat.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/hardirq.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/pinctrl/consumer.h>

#include <linux/soc/qcom/panel_event_notifier.h>
#include "eswin_eph861x_project_config.h"
#include "eswin_eph861x_types.h"
#include "eswin_eph861x_comms.h"
#include "eswin_eph861x_eswin.h"
#include "eswin_eph861x_bootloader.h"
#include "eswin_eph861x_tlv_command.h"
#include "eswin_eph861x_tlv_report.h"
#include "eswin_eph861x.h"

/* device settings file format version expected*/
#define EPH_DEVICE_SETTINGS_FORMAT       "<product>EPH8610</product>"

/* Touchscreen absolute values */
#define EPH_MAX_HEIGHT_WIDTH      255u

#if defined(CONFIG_DRM)
static struct drm_panel *active_panel;
static void eph_panel_notifier_callback(enum panel_event_notifier_tag tag,
		 struct panel_event_notification *event, void *client_data);

static void eph_register_for_panel_events(struct device_node *dp,
					struct eph_data *cd)
{
	void *cookie;

	cookie = panel_event_notifier_register(PANEL_EVENT_NOTIFICATION_PRIMARY,
			PANEL_EVENT_NOTIFIER_CLIENT_PRIMARY_TOUCH, active_panel,
			&eph_panel_notifier_callback, cd);
	if (!cookie) {
		pr_err("Failed to register for panel events\n");
		return;
	}

	ts_debug("registered for panel notifications panel: 0x%x\n",
			active_panel);

	cd->notifier_cookie = cookie;
}
#endif

static int eph_sysfs_mem_access_init(struct eph_data *ephdata);
static void eph_sysfs_mem_access_remove(struct eph_data *ephdata);
static int eph_configure_components(struct eph_data *ephdata, const struct firmware *device_settings);

static int eph_check_mem_access_params(struct eph_data *ephdata,
                                       loff_t off,
                                       size_t *count)
{
    if (off >= PAGE_SIZE)
    {
        return -EIO;
    }

    if (off + *count > PAGE_SIZE)
    {
        return -EIO;
    }

    return 0;
}

#if 0
static int eph_probe_bootloader(struct eph_data *ephdata)
{
    struct device *dev = &ephdata->commsdevice->dev;
    int ret_val;

    dev_dbg(dev, "%s >\n", __func__);

    ret_val = eph_comms_specific_bootloader_checks(ephdata);
    if (ret_val)
    {
        return ret_val;
    }

    /* Check bootloader status and version information */
    ret_val = eph_read_bootloader_information(ephdata);

    dev_info(dev, "%s, product_id = %x, variant_id = %x, bootloader_version = %x, error = %d\n",
            __func__,
            ephdata->ephdeviceinfo.product_id,
            ephdata->ephdeviceinfo.variant_id,
            ephdata->ephdeviceinfo.bootloader_version,
            ret_val);

    if (ret_val)
    {
        /* Force device into bootloader mode using chg line held low while toggle reset */
        ret_val = eph_chg_force_bootloader(ephdata);

        /* now forced into bootloader check whether we get a valid read */
        ret_val = eph_read_bootloader_information(ephdata);

        dev_info(dev, "%s, product_id = %x, variant_id = %x, bootloader_version = %x, error = %d\n",
                __func__,
                ephdata->ephdeviceinfo.product_id,
                ephdata->ephdeviceinfo.variant_id,
                ephdata->ephdeviceinfo.bootloader_version,
                ret_val);
        
        /* Release CHG line as no more bootloader reads required - Can return to app (reset to app not strictly required */
        (void)eph_bootloader_release_chg(ephdata);

        if (ret_val)
        {
            return ret_val;
        }
    }

    return 0;
}
#endif

static int eph_read_and_process_messages(struct eph_data *ephdata)
{
    struct device *dev = &ephdata->commsdevice->dev;
    int ret_val;

    //dev_dbg(&ephdata->commsdevice->dev, "%s >\n", __func__);

    mutex_lock(&ephdata->comms_mutex);
    /* Read report */
    ret_val = eph_read_report(ephdata, ephdata->report_buf);
    mutex_unlock(&ephdata->comms_mutex);

    if (0 > ret_val)
    {
        dev_err(dev, "Failed to read message (%d)\n", ret_val);
        return ret_val;
    }

    ret_val = eph_handle_report(ephdata, ephdata->report_buf);

    memset(ephdata->report_buf, 0, COMMS_BUF_SIZE);
    /* return 0 is success */
    return ret_val;
}

static irqreturn_t eph_interrupt(int irq, void *dev_id)
{
    struct eph_data *ephdata = (struct eph_data *)dev_id;
    struct device *dev = &ephdata->commsdevice->dev;

    if (dev == NULL)
        return IRQ_HANDLED;

    pm_stay_awake(dev);

    eph_read_and_process_messages(ephdata);

    /* will not unblock any other threads until message has been read */
    complete(&ephdata->chg_completion);

    pm_relax(dev);

    return IRQ_HANDLED;
}

static int eph_trigger_baseline(struct eph_data *ephdata)
{
    int ret_val;
    u8 cfg[8] =    {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x01};
    u16 length = (sizeof(cfg)/sizeof(cfg[0]));
    mutex_lock(&ephdata->comms_mutex);
    ret_val = eph_write_control_config(ephdata, length, &cfg[0]);
    mutex_unlock(&ephdata->comms_mutex);

    return ret_val;

}

static void eph_trigger_baseline_work(struct work_struct *work)
{
    int ret_val = 0;
    struct eph_data *ephdata = container_of(work, struct eph_data, force_baseline_work);
    struct backlight_device *bd = ephdata->bl;
    int brightness = 0;

    if (ephdata->last_brightness == 0) {
        do {
            if (bd->ops && bd->ops->get_brightness)
                brightness = bd->ops->get_brightness(bd);
            else
                brightness = bd->props.brightness;

            if (brightness)
                break;

            dev_info(&ephdata->commsdevice->dev, "eph wait 5 msec\n");
            msleep(5);

        } while (brightness == 0);

        ret_val = eph_trigger_baseline(ephdata);

        if (ret_val)
            dev_err(&ephdata->commsdevice->dev, "eph set baseline fail %d\n", ret_val);

        ephdata->last_brightness = brightness;

        dev_info(&ephdata->commsdevice->dev, "brightness %d\n", brightness);
    } else {
        dev_info(&ephdata->commsdevice->dev, "no need force baseline\n");
    }
}

static int eph_finger_print_enable(struct eph_data *ephdata, bool enable)
{
    int ret_val;
    u8 cfg[8] = {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x00};
    u16 length = (sizeof(cfg)/sizeof(cfg[0]));

    if (enable)
    {
        cfg[7] = 0x08;
    }

    mutex_lock(&ephdata->comms_mutex);
    ret_val = eph_write_control_config(ephdata, length, &cfg[0]);
    mutex_unlock(&ephdata->comms_mutex);

    return ret_val;
}

static int eph_trigger_backup_to_nvm(struct eph_data *ephdata)
{
    int ret_val;
    u8 cfg[8] =    {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02};
    u16 length = (sizeof(cfg)/sizeof(cfg[0]));
    mutex_lock(&ephdata->comms_mutex);
    ret_val = eph_write_control_config(ephdata, length, &cfg[0]);
    mutex_unlock(&ephdata->comms_mutex);

    return ret_val;
}

// enable: 0 mean disable
//         1 mean enable
static int eph_dev_enable_eng_mode(struct eph_data *ephdata, int enable)
{
    int ret_val;
    struct device *dev = &ephdata->commsdevice->dev;
    u8 cfg[] =    {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x00, 0x00, 0x16, 0x00, 0x0};
    u16 length = (sizeof(cfg)/sizeof(cfg[0]));

    if (enable == 0)
        cfg[7] = 0;
    else if (enable == 1)
        cfg[7] = 0x80;
    else {
        dev_err(dev, "%s unexpected param %d\n", __func__, enable);
        return -1;
    };

    mutex_lock(&ephdata->comms_mutex);
    ret_val = eph_write_control_config(ephdata, length, &cfg[0]);
    mutex_unlock(&ephdata->comms_mutex);

    return ret_val;
}

/* eph_update_device_settings - download device settings to chip
 * The file consists of repeating patterns of the following:
 *   <TYPE> - 1-byte type
 *   <LENGTH> - 2-byte length
 *   <DATA> - length-bytes of data
 */
static int eph_update_device_settings(struct eph_data *ephdata, const struct firmware *device_settings_image)
{
    struct device *dev = &ephdata->commsdevice->dev;
    struct eph_device_settings device_settings;
    int ret_val = 0;
    u8* cfg_ptr;
    u16 component;
    u16 offset;
    u16 msg_count = 0u;
    u8 retry = 0u;
    u16 pos = 0u;
    struct tlv_header tlvheader;

    dev_dbg(dev, "%s >\n", __func__);

    /* Allocate space for zero terminated copy of the device settings file */
    device_settings.raw = (u8 *)kzalloc(device_settings_image->size + 1, GFP_KERNEL);
    if (!device_settings.raw)
    {
        dev_err(dev, "ESWIN Couldnt allocate memory for device settings file %d.\n", -ENOMEM);
        return -ENOMEM;
    }

    // enable eng mode
    ret_val = eph_dev_enable_eng_mode(ephdata, 1);
    dev_dbg(dev, "eng enable %d\n", ret_val);

    /* Copy from the firmware image into local buffer */
    memcpy(device_settings.raw, device_settings_image->data, device_settings_image->size);
    /* Pad last entry as a zero. We are about to loop through all config and control tlvs */
    /* The 0 will not match an expected T and the configuration loading will terminate */
    device_settings.raw[device_settings_image->size] = 0;
    device_settings.raw_size = device_settings_image->size;
    cfg_ptr = &device_settings.raw[0];

    //TODO add format checking - Currently no version information in settings file.
    while (pos < device_settings_image->size)
    {
        tlvheader = eph_get_tl_header_info(ephdata, cfg_ptr);
        /* if item next in the data stream is not configuration or control break out */
        if((TLV_CONFIG_DATA_WRITE == tlvheader.type) || (TLV_CONTROL_DATA_WRITE == tlvheader.type))
        {
            if (pos > device_settings_image->size)
            {
                /* Have reached the end of the file but did not find the terminating character */
                ret_val = -ENOMEM;
                dev_err(dev, "ESWIN went out of bounds of device settings file %d.\n", ret_val);
                /* Ensure memory released before returning */
                goto fail_release;
            }
            component = *(cfg_ptr + TLV_HEADER_SIZE + TLV_WRITE_COMPONENT_FIELD) | (*(cfg_ptr + TLV_HEADER_SIZE + TLV_WRITE_COMPONENT_FIELD + 1) << 8);
            offset = *(cfg_ptr + TLV_HEADER_SIZE + TLV_WRITE_OFFSET_FIELD) | (*(cfg_ptr + TLV_HEADER_SIZE + TLV_WRITE_OFFSET_FIELD + 1) << 8);
            dev_dbg(dev, "TYPE: %d LEGNTH: %d COMPONENT_ID: %d OFFSET: %d\n", tlvheader.type, tlvheader.length, component, offset);
            msg_count++;
            dev_dbg(dev, "Configuration/control write: BLOCK NUMBER: %d.\n", msg_count);

            mutex_lock(&ephdata->comms_mutex);
            ret_val = eph_write_control_config(ephdata, tlvheader.length, cfg_ptr);
            mutex_unlock(&ephdata->comms_mutex);

            retry = 0u;
            while (ret_val)
            {
                dev_info(&ephdata->commsdevice->dev,
                         "Retry control config write, It failed for some reason. msg_count: %u",
                         msg_count);

                mutex_lock(&ephdata->comms_mutex);
                ret_val = eph_write_control_config(ephdata, tlvheader.length, cfg_ptr);
                mutex_unlock(&ephdata->comms_mutex);

                retry++;
                if (10 < retry)
                {
                    ret_val = -EIO;
                    dev_err(dev, "ESWIN failed to send settings to TIC %d.\n", ret_val);
                    /* Ensure memory released before returning */
                    goto fail_release;

                }
            }

            cfg_ptr = cfg_ptr + tlvheader.length;
            pos = pos + tlvheader.length;

            if(ret_val)
            {
                dev_info(dev, "Configuration/Contol write failure on: BLOCK NUMBER: %d.\n", msg_count);
                /* Ensure memory released before returning */
                goto fail_release;
            }

        }
        else
        {
            break;
        }
    }

    // disable eng mode
    ret_val = eph_dev_enable_eng_mode(ephdata, 0);
    dev_dbg(dev, "eng enable %d\n", ret_val);
    
    ret_val = eph_trigger_backup_to_nvm(ephdata);
    if(ret_val)
    {
        dev_err(dev, "Failed to trigger_backup_to_nvm %d.\n", ret_val);
    }
    ret_val = eph_trigger_baseline(ephdata);
    if(ret_val)
    {
        dev_err(dev, "Failed to trigger_baseline %d.\n", ret_val);
    }

    dev_info(dev, "Config successfully updated\n");

fail_release:
    kfree(device_settings.raw);

    return ret_val;
}

static int eph_acquire_irq(struct eph_data *ephdata)
{
    int ret_val;
    char *commsdevice_name;

    dev_dbg(&ephdata->commsdevice->dev, "%s >\n", __func__);

    if (!ephdata->chg_irq)
    {
        ephdata->chg_irq = gpio_to_irq(ephdata->ephplatform->gpio_chg_irq);

        commsdevice_name = eph_comms_devicename_get(ephdata);

        /* configure a thread that is triggered on the interrupt */
        /* This specific IRQ remains disabled while handler/thread is active */
        /* Thread happens outside of interrupt handler and so frees up the general interupt hardware */
        ret_val = request_threaded_irq(ephdata->chg_irq,
                                       NULL,
                                       eph_interrupt,
                                       IRQF_TRIGGER_LOW | IRQF_ONESHOT,
                                       commsdevice_name,
                                       ephdata);
        if (ret_val)
        {
            dev_err(&ephdata->commsdevice->dev, "request_threaded_irq (%d)\n", ret_val);
            return ret_val;
        }

        /* Presence of ephdata->chg_irq means IRQ initialised */
        dev_info(&ephdata->commsdevice->dev, "zxz gpio_to_irq %lu -> %d\n", ephdata->ephplatform->gpio_chg_irq, ephdata->chg_irq);
    }
    else
    {
        enable_irq(ephdata->chg_irq);
    }


    dev_info(&ephdata->commsdevice->dev, "%s <\n", __func__);

    return 0;
}


static int eph_input_open(struct input_dev *inputdev);
static void eph_input_close(struct input_dev *inputdev);

static void eph_unregister_input_device(struct eph_data *ephdata)
{
    dev_dbg(&ephdata->commsdevice->dev, "%s >\n", __func__);

    if (ephdata->inputdev)
    {
#ifdef INPUT_DEVICE_ALWAYS_OPEN
        eph_input_close(ephdata->inputdev);
#endif //INPUT_DEVICE_ALWAYS_OPEN

        input_unregister_device(ephdata->inputdev);
    }
}


static int eph_probe_regulators(struct eph_data *ephdata)
{
    struct device *dev = &ephdata->commsdevice->dev;
    int ret_val;

    dev_dbg(dev, "%s >\n", __func__);

    /* Must have reset GPIO to use regulator support */
    if (!gpio_is_valid(ephdata->ephplatform->gpio_reset))
    {
        ret_val = -EINVAL;
        goto fail;
    }
    if (!gpio_is_valid(ephdata->ephplatform->gpio_chg_irq))
    {
        ret_val = -EINVAL;
        goto fail;
    }

    ephdata->reg_vdd = regulator_get(dev, ephdata->ephplatform->regulator_dvdd);
	// ephdata->reg_vdd = regulator_get(dev, "vdd");
    if (IS_ERR(ephdata->reg_vdd))
    {
        ret_val = PTR_ERR(ephdata->reg_vdd);
        dev_err(dev, "Error %d getting vdd regulator\n", ret_val);
        goto fail;
    }

    ephdata->reg_avdd = regulator_get(dev, ephdata->ephplatform->regulator_avdd);
    // ephdata->reg_avdd = regulator_get(dev, "avdd");
    if (IS_ERR(ephdata->reg_avdd))
    {
        ret_val = PTR_ERR(ephdata->reg_avdd);
        dev_err(dev, "Error %d getting avdd regulator\n", ret_val);
        goto fail_release;
    }

    eph_regulator_enable(ephdata);
    dev_dbg(dev, "Initialised regulators end\n");
    return 0;

fail_release:
    regulator_put(ephdata->reg_vdd);
    regulator_put(ephdata->reg_avdd);

fail:
    ephdata->reg_vdd = NULL;
    ephdata->reg_avdd = NULL;
    gpio_free(ephdata->ephplatform->gpio_reset);
    gpio_free(ephdata->ephplatform->gpio_chg_irq);
    return ret_val;
}


static int eph_input_device_initialize(struct eph_data *ephdata)
{
    const struct eph_platform_data *ephplatform = ephdata->ephplatform;
    struct device *dev = &ephdata->commsdevice->dev;
    int ret_val;
    unsigned int mt_flags = 0;
    u16 max_resoultion;

    dev_dbg(dev, "%s >\n", __func__);


    dev_info(dev, "Touchscreen resolution X%uY%u\n", ephplatform->panel_max_x, ephplatform->panel_max_y);


    dev_info(dev, "ESWIN device not allocated, allocate device %d\n", 0);
    /* allocate memory for new managed input device 
     * (no need to free as is freed when last reference to the device is dropped) 
     * use input_unregister_device() and memory will be freed (after last reference) */
    ephdata->inputdev = devm_input_allocate_device(dev);
    if (!ephdata->inputdev)
    {
        dev_err(dev, "allocate device failed %d\n", -ENOMEM);
        return -ENOMEM;
    }

    if (ephplatform->input_name)
    {
        ephdata->inputdev->name = ephplatform->input_name;
    }
    else
    {
        ephdata->inputdev->name = "ESWIN EPH861X Touchscreen";
    }

    ephdata->inputdev->phys = ephdata->phys;
    ephdata->inputdev->id.bustype = EPH_COMMS_BUS_TYPE;
    ephdata->inputdev->dev.parent = dev;

#ifndef INPUT_DEVICE_ALWAYS_OPEN
    ephdata->inputdev->open = eph_input_open;
    ephdata->inputdev->close = eph_input_close;
#endif //INPUT_DEVICE_ALWAYS_OPEN


    /* this will confuse android into thinking we support a feature we dont - IDEALLY should no be set */
    /* Cannot remove EV_KEY from get event so appears to be required for pixel at least */
    input_set_capability(ephdata->inputdev, EV_KEY/*event type*/, BTN_TOUCH/*event code*/);


    input_set_capability(ephdata->inputdev, EV_ABS, ABS_MT_POSITION_X);
    input_set_capability(ephdata->inputdev, EV_ABS, ABS_MT_POSITION_Y);


    /* direct device, e.g. touchscreen */
    mt_flags |= INPUT_MT_DIRECT;

    /* multi touch */
    ret_val = input_mt_init_slots(ephdata->inputdev, CONFIG_SUPPORTED_TOUCHES, mt_flags);
    if (ret_val)
    {
        dev_err(dev, "Error %d initialising slots\n", ret_val);
        return ret_val;
    }

    /* reports co-ordinates of the tool */
    /* Height appears to always be Y */
    input_set_abs_params(ephdata->inputdev, ABS_MT_POSITION_X, 0, ephplatform->panel_max_x, 0, 0);
    input_set_abs_params(ephdata->inputdev, ABS_MT_POSITION_Y, 0, ephplatform->panel_max_y, 0, 0);

    max_resoultion = (ephplatform->panel_max_y > ephplatform->panel_max_x) ? ephplatform->panel_max_y :
        ephplatform->panel_max_x;

    /* The minor and major has no particular orientation just the longest/shortest axis */
    input_set_abs_params(ephdata->inputdev, ABS_MT_TOUCH_MAJOR, 0, (EPH_MAX_HEIGHT_WIDTH*max_resoultion), 0, 0);
    input_set_abs_params(ephdata->inputdev, ABS_MT_TOUCH_MINOR, 0, (EPH_MAX_HEIGHT_WIDTH*max_resoultion), 0, 0);
    input_set_abs_params(ephdata->inputdev, ABS_MT_PRESSURE, 0, 255, 0, 0);


    input_set_drvdata(ephdata->inputdev, ephdata);

    dev_dbg(dev, "input_register_device\n");

    input_set_capability(ephdata->inputdev, EV_KEY, KEY_WAKEUP);

    ret_val = input_register_device(ephdata->inputdev);
    if (ret_val)
    {
        dev_err(dev, "Error %d registering input device\n", ret_val);
        return ret_val;
    }

#ifdef INPUT_DEVICE_ALWAYS_OPEN
    eph_input_open(ephdata->inputdev);
#endif //INPUT_DEVICE_ALWAYS_OPEN

    dev_info(dev, "%s <\n", __func__);

    return 0;
}

#if 0
static void eph_request_fw_device_settings_nowait_cb(const struct firmware *device_settings, void *ctx)
{
    (void)eph_configure_components((struct eph_data *)ctx, device_settings);
    (void)eph_input_device_initialize((struct eph_data *)ctx);
    release_firmware(device_settings);
}
#endif

static int eph_initialize(struct eph_data *ephdata)
{
    struct comms_device *commsdevice = ephdata->commsdevice;
    int comms_attempts = 0;
    int ret_val;
    int device_info_read_retry = 0;

    dev_dbg(&commsdevice->dev, "%s >\n", __func__);

    while (2 > comms_attempts)
    {

        mutex_lock(&ephdata->comms_mutex);
        ret_val = eph_read_device_information(ephdata);
        while(ret_val && (device_info_read_retry <= COMMS_READ_RETRY_NUM))
        {
            /* retry we didnt get a device information response */
            ret_val = eph_read_device_information(ephdata);
            device_info_read_retry++;

        }
        mutex_unlock(&ephdata->comms_mutex);

        dev_info(&ephdata->commsdevice->dev,
                 "Product ID: %u Variant ID: %u Application_version_major %u Application_version_minor: %u Bootloader_version: %u Protocol_version: %u CRC:%u\n",
                 ephdata->ephdeviceinfo.product_id, ephdata->ephdeviceinfo.variant_id,
                 ephdata->ephdeviceinfo.application_version_major, ephdata->ephdeviceinfo.application_version_minor,
                 ephdata->ephdeviceinfo.bootloader_version, ephdata->ephdeviceinfo.protocol_version, ephdata->ephdeviceinfo.crc);

        if ((0 == ret_val) && ((ephdata->ephdeviceinfo.application_version_major != 0) || (ephdata->ephdeviceinfo.application_version_minor != 0)))
        {

            /* sucessfully read from device info and confirmed in application mode */
            ephdata->in_bootloader = false;
            break;
        }

#if 0
        /* Failed to read the device information - try bootloader */
        ret_val = eph_chg_force_bootloader(ephdata);

        /* Check bootloader state */
        ret_val = eph_probe_bootloader(ephdata);

        /* release chg as bootloader actions complete  */
        (void)eph_bootloader_release_chg(ephdata);

        if (ret_val)
        {
            /* Chip is not in appmode or bootloader mode */
            return ret_val;
        }
        comms_attempts++;
        /* OK, we are in bootloader, see if we can recover */
        if (comms_attempts > 1)
        {
            dev_err(&commsdevice->dev, "Could not recover from bootloader mode\n");
            /*
             * We can reflash from this state, so do not
             * abort initialization.
             */
            ephdata->in_bootloader = true;
            return 0;
        }
#else
        comms_attempts++;
#endif

        /* Attempt to exit bootloader into app mode */
        eph_reset_device(ephdata);
        msleep(EPH_FW_RESET_TIME);
    }

    ret_val = eph_acquire_irq(ephdata);
    if (ret_val)
    {
        return ret_val;
    }

    ret_val = eph_sysfs_mem_access_init(ephdata);
    if (ret_val)
    {
        return ret_val;
    }

#if 0
    dev_dbg(&commsdevice->dev, "device_settings_name: %s\n", ephdata->device_settings_name);
    if (ephdata->device_settings_name)
    {
        ret_val = request_firmware_nowait(THIS_MODULE,
                                          true,
                                          ephdata->device_settings_name,
                                          &ephdata->commsdevice->dev,
                                          GFP_KERNEL,
                                          ephdata,
                                          eph_request_fw_device_settings_nowait_cb);
        if (ret_val)
        {
            dev_err(&commsdevice->dev, "Failed to invoke firmware load: %d\n", ret_val);
            return ret_val;
        }
    }
    else
    {
        ret_val = eph_configure_components(ephdata, NULL);
        if (ret_val)
        {
            /* Do not return on configuration loading failure as will still want to initialise device */
            dev_err(&commsdevice->dev, "Failed to eph_configure_components %d\n", ret_val);
        }

        ret_val = eph_input_device_initialize(ephdata);
        if (ret_val)
        {
            return ret_val;
        }
    }
#endif

    dev_info(&commsdevice->dev, "%s <\n", __func__);

    return ret_val;
}


static int eph_configure_components(struct eph_data *ephdata, const struct firmware *device_settings)
{
    struct device *dev = &ephdata->commsdevice->dev;
    int ret_val = 0;

    dev_dbg(dev, "%s %s >\n", __func__, device_settings ? "device_settings":"-");

    if (device_settings)
    {
        ret_val = eph_update_device_settings(ephdata, device_settings);
        if (ret_val)
        {
            dev_warn(dev, "Error %d updating device_settings\n", ret_val);
        }
    }
    

    return ret_val;
}

/* Firmware Version is reported as Major.Minor.bootloaderversion */
static ssize_t eph_devattr_fw_version_show(struct device *dev,
                                           struct device_attribute *attr,
                                           char *buf)
{
    struct eph_data *ephdata = (struct eph_data*)dev_get_drvdata(dev);
    return scnprintf(buf, PAGE_SIZE, "%u.%u.%u\n",
                     ephdata->ephdeviceinfo.application_version_major, ephdata->ephdeviceinfo.application_version_minor,
                     ephdata->ephdeviceinfo.bootloader_version);
}

/* Hardware Version is reported as DevicefamilyID.DevicevariantID */
static ssize_t eph_devattr_hw_version_show(struct device *dev,
                                           struct device_attribute *attr,
                                           char *buf)
{
    struct eph_data *ephdata = (struct eph_data*)dev_get_drvdata(dev);
    return scnprintf(buf, PAGE_SIZE, "%u.%u\n",
                     ephdata->ephdeviceinfo.product_id, ephdata->ephdeviceinfo.variant_id);
}


static int eph_enter_bootloader(struct eph_data *ephdata)
{
    int ret_val;

    dev_dbg(&ephdata->commsdevice->dev, "%s >\n", __func__);

    if (!ephdata->in_bootloader)
    {
        if (ephdata->suspended)
        {
            if (ephdata->ephplatform->suspend_mode == EPH_SUSPEND_REGULATOR)
            {
                eph_regulator_enable(ephdata);
            }
            ephdata->suspended = false;
        }
    }

    /* force bootloader regardless whether we are in bootloader already - Always in defined TIC state */
    /* Force device into bootloader mode using chg line held low while toggle reset */
    ret_val = eph_chg_force_bootloader(ephdata);

    if (ret_val)
    {
        /* failed to enter bootloader correctly - restore CHG */
        (void)eph_bootloader_release_chg(ephdata);
        return ret_val;
    }

    ret_val = eph_comms_specific_bootloader_checks(ephdata);
    if (ret_val)
    {
        /* failed to enter bootloader correctly - restore CHG */
        (void)eph_bootloader_release_chg(ephdata);
        return ret_val;
    }

    if (!ephdata->in_bootloader)
    {
        ephdata->in_bootloader = true;
        /* Need in_bootloader to be true otherwise the regulators get disabled */
        eph_sysfs_mem_access_remove(ephdata);
        eph_unregister_input_device(ephdata);
    }

    dev_info(&ephdata->commsdevice->dev, "Entered bootloader\n");

    return 0;
}

static int eph_load_fw(struct device *dev)
{
    struct eph_data *ephdata = (struct eph_data*)dev_get_drvdata(dev);
    int ret_val;
    int reload_cnt = 5;
#if (ESWIN_EPH861X_SPI)
    u32 default_spi_speed = ephdata->commsdevice->max_speed_hz;
#endif

    dev_dbg(&ephdata->commsdevice->dev, "%s >\n", __func__);

    ephdata->ephflash = (struct eph_flash*)devm_kzalloc(dev, sizeof(struct eph_flash), GFP_KERNEL);
    if (!ephdata->ephflash)
    {
        return -ENOMEM;
    }

    ephdata->ephflash->ephdata = ephdata;

    dev_dbg(&ephdata->commsdevice->dev, "%s request_firmware name %s \n", __func__, ephdata->fw_name);
 
    /* Finds fw under the requested name */
    ret_val = request_firmware(&ephdata->ephflash->fw, ephdata->fw_name, dev);
    if (ret_val)
    {
        dev_err(dev, "request_firmware %d %s\n", ret_val, ephdata->fw_name);
        goto free;
    }

    /* Check for incorrect enc file */
    ret_val = eph_check_firmware_format(dev, ephdata->ephflash->fw);
    if (ret_val)
    {
        goto release_firmware;
    }

#ifdef IC_UPDATE_DETECT
    if (!eph_check_ic_update(ephdata, ephdata->ephflash->fw)) {
        dev_info(dev, "not get new vers firmware\n");
        goto release_firmware;
    }
#endif

    disable_irq(ephdata->chg_irq);
#if (ESWIN_EPH861X_SPI)
    // 2M Hz more stable when loading fw
    ephdata->commsdevice->max_speed_hz = 2000000;
    spi_setup(ephdata->commsdevice);
#endif

reload:

    ret_val = eph_enter_bootloader(ephdata);
    if (ret_val)
    {
        goto exit_bootenv;
    }

    ret_val = eph_send_frames(ephdata);
    if (ret_val)
    {
        reload_cnt--;
        if (reload_cnt > 0)
            goto reload;
        goto exit_bootenv;
    }

exit_bootenv:
#if (ESWIN_EPH861X_SPI)
    ephdata->commsdevice->max_speed_hz = default_spi_speed;
    spi_setup(ephdata->commsdevice);
#endif
    enable_irq(ephdata->chg_irq);
release_firmware:
    release_firmware(ephdata->ephflash->fw);
free:
    devm_kfree(dev, ephdata->ephflash);
    return ret_val;
}

/* gesture_mode: BIT0 enable/disable
 *               BIT1 tap
 *               BIT2 doubel tap
 *               BIT4 swipe
 */
static int eph_gesture_mode_set(struct eph_data *ephdata, u8 gesture_mode)
{
    int ret_val;

    u8 type = TLV_CONFIG_DATA_WRITE;
    u8 prepayload_len = 4;
    // gesture extraction 450
    u8 comp_id_low = (u8)450;
    u8 comp_id_high = (u8)(450 > 8);
    // param config flags
    u8 offset = 30;
    // config length
    u8 data_len = 1;
    volatile u8 data[] = { gesture_mode & 0xf };
    volatile u8 tlv[] = {type, data_len + prepayload_len, 0x00, comp_id_low, comp_id_high, offset, 0x00, data[0]};
    u16 length = (sizeof(tlv)/sizeof(tlv[0]));

    if (gesture_mode & 0xf0) {
        dev_err(&ephdata->commsdevice->dev, "Invaild gesture mode");
        return -EINVAL;
    }

    mutex_lock(&ephdata->comms_mutex);
    ret_val = eph_write_control_config(ephdata, length, (u8 *)&tlv[0]);
    mutex_unlock(&ephdata->comms_mutex);

    if (likely(!ret_val))
        ephdata->gesture_mode = gesture_mode;
    else
        dev_err(&ephdata->commsdevice->dev, "Set/Clr gesture mode fail\n");

    return ret_val;
}

static ssize_t eph_devattr_update_fw_store(struct device *dev,
                                           struct device_attribute *attr,
                                           const char *buf,
                                           size_t count)
{
    struct eph_data *ephdata = (struct eph_data*)dev_get_drvdata(dev);
    int ret_val;

    /* Pass in file name and length of name and if accepted put into fw_name */
    ret_val = eph_update_file_name(dev, &ephdata->fw_name, buf, count);
    if (ret_val)
    {
        return ret_val;
    }

    ret_val = eph_load_fw(dev);
    if (ret_val)
    {
        dev_err(dev, "The firmware update failed(%d)\n", ret_val);
        count = ret_val;
    }
    else
    {
        dev_info(dev, "The firmware update succeeded\n");

        /* TODO re-check - initial bootloader appears to require an extended delay before moving to communicating with App */
        msleep(EPH_RESET_TIME*2);
        ephdata->suspended = false;

        ret_val = eph_initialize(ephdata);
        if (ret_val)
        {
            return ret_val;
        }
    }

    return count;
}

static ssize_t eph_devattr_update_device_settings_store(struct device *dev,
                                            struct device_attribute *attr,
                                            const char *buf,
                                            size_t count)
{
    struct eph_data *ephdata = (struct eph_data*)dev_get_drvdata(dev);
    const struct eph_platform_data *ephplatform = ephdata->ephplatform;
    const struct firmware *device_settings;
    int ret_val;

    ret_val = eph_update_file_name(dev, &ephdata->device_settings_name, buf, count);
    if (ret_val)
    {
        return ret_val;
    }

    /* find the device settings file under the following name */
    ret_val = request_firmware(&device_settings, ephdata->device_settings_name, dev);
    if (ret_val)
    {
        dev_err(dev, "request_firmware %d %s\n", ret_val, ephdata->device_settings_name);
        goto out;
    }

    ephdata->updating_device_settings = true;


    if (ephdata->suspended)
    {
        dev_info(dev, "ESWIN device was in suspend %d\n", ret_val);
        if (ephplatform->suspend_mode == EPH_SUSPEND_REGULATOR)
        {
            enable_irq(ephdata->chg_irq);
            eph_regulator_enable(ephdata);
        }
        else if (ephplatform->suspend_mode == EPH_SUSPEND_DEEP_SLEEP)
        {
            /* do nothing as TIC does not currently support sleep */
        }

        ephdata->suspended = false;
    }

    ret_val = eph_configure_components(ephdata, device_settings);
    if (!ret_val)
    {
        /* no error so return count */
        ret_val = count;
    }

    release_firmware(device_settings);
out:
    ephdata->updating_device_settings = false;
    return ret_val;
}


static ssize_t eph_devattr_comms_read(struct device *dev,
                                           struct device_attribute *attr,
                                           char *buf)
{
    struct eph_data *ephdata;
    int ret_val;
    size_t count = TLV_HEADER_SIZE;

    ephdata = (struct eph_data*)dev_get_drvdata(dev);


    /* Wait for any pending IRQ handler to complete - Gives priority for interrupt handler */
    synchronize_irq(ephdata->chg_irq);

    mutex_lock(&ephdata->comms_mutex);
    ret_val = eph_comms_two_stage_read(ephdata, (u8*)buf);
    mutex_unlock(&ephdata->comms_mutex);


    if(ret_val)
    {
        /* Return NULL message if there is no message or an error occured */
        /* clear 3 bytes to mimic null read */
        memset(buf, 0, TLV_HEADER_SIZE); 
    }
    else
    {
        /* decode and return the legth of message */
        count = (size_t)((buf[TLV_LENGTH_FIELD] | ((u16)buf[TLV_LENGTH_FIELD + 1u] << 8u)) + TLV_HEADER_SIZE);
    }

    dev_info(&ephdata->commsdevice->dev, "ESWIN sysfs read the following %d, %d , %d<\n", buf[0], buf[1], buf[2]);

    return ret_val == 0 ? count : ret_val;
}


static ssize_t eph_devattr_comms_write(struct device *dev,
                                           struct device_attribute *attr,
                                           const char *buf,
                                           size_t count)
{
    
    struct eph_data *ephdata = (struct eph_data*)dev_get_drvdata(dev);
    ssize_t ret_val;

    ret_val = eph_check_mem_access_params(ephdata, 0, &count);
    if (0 < ret_val)
    {
        return ret_val;
    }

    if (0 < count)
    {

        /* Wait for any pending IRQ handler to complete - Gives priority for interrupt handler */
        synchronize_irq(ephdata->chg_irq);

        mutex_lock(&ephdata->comms_mutex);
        ret_val = eph_comms_write(ephdata, count, (u8 *)buf);
        mutex_unlock(&ephdata->comms_mutex);

        dev_info(&ephdata->commsdevice->dev, "ESWIN sysfs: %s. Write the following %d, %d , %d<\n", __func__, buf[0], buf[1], buf[2]);        
    }
    

    return ret_val == 0 ? count : ret_val;


  }


static ssize_t eph_devattr_device_report_read(struct device *dev,
                                           struct device_attribute *attr,
                                           char *buf)
{
    struct eph_data *ephdata;
    struct tlv_header tlvheader;
    ephdata = (struct eph_data*)dev_get_drvdata(dev);



    mutex_lock(&ephdata->sysfs_report_buffer_lock);
    tlvheader = eph_get_tl_header_info(ephdata, sysfs_report_buf);


    if (TLV_HEADER_SIZE>tlvheader.length)
    {
        /* if length was 0 for any reason generate NULL message - Ensures null message copied to the buffer */
        tlvheader.length = TLV_HEADER_SIZE;
        memset(&sysfs_report_buf[0], 0, tlvheader.length); 
    }

    /* copy message into page buffer */
    memcpy(buf,&sysfs_report_buf[0],tlvheader.length);

    /* Flush buffer once consumed - to prevent same message being read twice */
    /* relies on message being smaller than a page and the tooling processing the header in that single read */
    memset(&sysfs_report_buf[0], 0, tlvheader.length); 

    /* Only generate a message if valid message is being returned. */
    if (0!=tlvheader.type)
    {
        dev_dbg(&ephdata->commsdevice->dev, "ESWIN sysfs read buffer. Type: %d, length: %d", tlvheader.type, tlvheader.length);
    }
    mutex_unlock(&ephdata->sysfs_report_buffer_lock);


    return (size_t)tlvheader.length;

  }


static ssize_t eph_devattr_reset_device(struct device *dev,
                                           struct device_attribute *attr,
                                           char *buf)
{
    struct eph_data *ephdata;
    int ret_val;
    ephdata = (struct eph_data*)dev_get_drvdata(dev);

    dev_dbg(&ephdata->commsdevice->dev, "%s start gpio_reset \n > \n", __func__);
    eph_reset_device(ephdata);

    dev_dbg(&ephdata->commsdevice->dev, "%s start trigger_baseline \n > \n", __func__);
    ret_val = eph_trigger_baseline(ephdata);
    eph_clear_all_host_touch_slots(ephdata);

    dev_dbg(&ephdata->commsdevice->dev, "%s < \n", __func__);
    return ret_val;

  }

static ssize_t eph_devattr_gesture_wakeup_read(struct device *dev,
                                           struct device_attribute *attr,
                                           char *buf)
{
    struct eph_data *ephdata;
    ephdata = (struct eph_data*)dev_get_drvdata(dev);

    if (!ephdata)
        return -EIO;

    return sprintf(buf, "%s mode[%x]\n", ephdata->gesture_wakeup_enable ? "enable" : "disable",
            ephdata->gesture_mode);
}

static ssize_t eph_devattr_gesture_wakeup_store(struct device *dev,
                                           struct device_attribute *attr,
                                           const char *buf,
                                           size_t count)
{
    struct eph_data *ephdata;
    int ret_val = -1;
    int input = 0;
    u8 gesture_mode = 0;
    u8 set_mode = 0;
    ephdata = (struct eph_data*)dev_get_drvdata(dev);

    if (!ephdata)
        return -EIO;

    if (kstrtoint(buf, 10, &input))
        return -EINVAL;

    switch (input) {
    case 1:
        /* enable tap */
        if (ephdata->gesture_mode & BIT(1)) {
            dev_info(&ephdata->commsdevice->dev, "tap already set\n");
            goto exit;
        }
        gesture_mode = BIT(1);
        break;
    case 2:
        /* enable double tap */
        if (ephdata->gesture_mode & BIT(2)) {
            dev_info(&ephdata->commsdevice->dev, "double tap already set\n");
            goto exit;
        }
        gesture_mode = BIT(2);
        break;
    case 3:
        /* enable swipe */
        if (ephdata->gesture_mode & BIT(3)) {
            dev_info(&ephdata->commsdevice->dev, "swipe already set\n");
            goto exit;
        }
        gesture_mode = BIT(3);
        break;
    case -1:
        /* disable tap */
        if ((ephdata->gesture_mode & BIT(1)) == 0) {
            dev_info(&ephdata->commsdevice->dev, "tap not set\n");
            goto exit;
        }
        gesture_mode |= ~BIT(1);
        break;
    case -2:
        /* disable double tap */
        if ((ephdata->gesture_mode & BIT(2)) == 0) {
            dev_info(&ephdata->commsdevice->dev, "double tap not set\n");
            goto exit;
        }
        gesture_mode |= ~BIT(2);
        break;
    case -3:
        /* disable swipe */
        if ((ephdata->gesture_mode & BIT(3)) == 0) {
            dev_info(&ephdata->commsdevice->dev, "swipe not set\n");
            goto exit;
        }
        gesture_mode |= ~BIT(3);
        break;
    case 0:
    default:
        dev_err(&ephdata->commsdevice->dev, "Invaild Para set\n");
        return -EINVAL;
    }

    set_mode = (input > 0) ? (ephdata->gesture_mode | gesture_mode) :
            (ephdata->gesture_mode & gesture_mode);

    ret_val = eph_gesture_mode_set(ephdata, set_mode);

    if (ret_val) {
        dev_err(&ephdata->commsdevice->dev, "mode set/clr fail %x", ret_val);
        goto exit;
    }

    if (ephdata->gesture_mode & 0xE) {
        ephdata->gesture_wakeup_enable = true;
        dev_info(&ephdata->commsdevice->dev, "mode %x enabled\n", ephdata->gesture_mode);
    } else {
        ephdata->gesture_wakeup_enable = false;
        dev_info(&ephdata->commsdevice->dev, "gesture disabled\n");
    }

exit:
    return count;
}

static ssize_t eph_devattr_finger_print_enable(struct device *dev,
                                           struct device_attribute *attr,
                                           const char *buf,
                                           size_t count)
{
    struct eph_data *ephdata = (struct eph_data*)dev_get_drvdata(dev);
    ssize_t ret_val = 0;
    int input = 0;

    if (!ephdata)
    {
        return -EIO;
    }

    if (0 < count)
    {
        if (kstrtoint(buf, 10, &input))
        {
            return -EINVAL;
        }
        else
        {
            if (0 == input)
            {
                dev_dbg(&ephdata->commsdevice->dev, "%s disable FOD \n > \n", __func__);
                ret_val = eph_finger_print_enable(ephdata, false);
            }
            else
            {
                dev_dbg(&ephdata->commsdevice->dev, "%s enable FOD \n > \n", __func__);
                ret_val = eph_finger_print_enable(ephdata, true);
            }
        }
    }
    return ret_val == 0 ? count : ret_val;
}

/* .attr.name, .attr.mode, .show, .store  */
static DEVICE_ATTR(update_fw, S_IWUSR, NULL, eph_devattr_update_fw_store);
/* S_IRUGO - read-only attributes  */
static DEVICE_ATTR(fw_version, S_IRUGO, eph_devattr_fw_version_show, NULL);
static DEVICE_ATTR(hw_version, S_IRUGO, eph_devattr_hw_version_show, NULL);
/* S_IWUSR - write access to root only */
static DEVICE_ATTR(update_device_settings, S_IWUSR, NULL, eph_devattr_update_device_settings_store);
static DEVICE_ATTR(write_device_message, S_IWUSR, NULL, eph_devattr_comms_write);
static DEVICE_ATTR(read_device_message, S_IRUGO, eph_devattr_comms_read, NULL);
static DEVICE_ATTR(read_device_report, S_IRUGO, eph_devattr_device_report_read, NULL);
static DEVICE_ATTR(reset_device, S_IRUGO, eph_devattr_reset_device, NULL);
static DEVICE_ATTR(gesture_wakeup, (S_IWUSR|S_IRUGO), eph_devattr_gesture_wakeup_read, eph_devattr_gesture_wakeup_store);
static DEVICE_ATTR(finger_print_enable, S_IWUSR, NULL, eph_devattr_finger_print_enable);

static struct attribute *eph_fw_attrs[] =
{
    /* update_fw */
    &dev_attr_update_fw.attr,
    NULL
};

static const struct attribute_group eph_fw_attr_group =
{
    .attrs = eph_fw_attrs,
};



static struct attribute *eph_attrs[] =
{
    /* fw_version */
    &dev_attr_fw_version.attr,
    /* hw_version */    
    &dev_attr_hw_version.attr,
    /* update_device_settings */
    &dev_attr_update_device_settings.attr,
    /* write message to device */    
    &dev_attr_write_device_message.attr,
    /* read message from device */    
    &dev_attr_read_device_message.attr,
    /* read buffered report from device */    
    &dev_attr_read_device_report.attr,

    &dev_attr_reset_device.attr,

	&dev_attr_gesture_wakeup.attr,
    /* FOD enable switch */
    &dev_attr_finger_print_enable.attr,

    NULL
};

static const struct attribute_group eph_attr_group =
{
    .attrs = eph_attrs,
};

static int eph_sysfs_mem_access_init(struct eph_data *ephdata)
{
    struct comms_device *commsdevice = ephdata->commsdevice;
    int ret_val;

    dev_dbg(&ephdata->commsdevice->dev, "%s >\n", __func__);

    ret_val = sysfs_create_group(&commsdevice->dev.kobj, &eph_attr_group);
    if (ret_val)
    {
        dev_err(&commsdevice->dev, "Failure %d creating sysfs group\n", ret_val);
        sysfs_remove_group(&commsdevice->dev.kobj, &eph_attr_group);
        return ret_val;
    }

    return ret_val;
}

static void eph_sysfs_mem_access_remove(struct eph_data *ephdata)
{
    struct comms_device *commsdevice = ephdata->commsdevice;

    dev_dbg(&ephdata->commsdevice->dev, "%s >\n", __func__);

    sysfs_remove_group(&commsdevice->dev.kobj, &eph_attr_group);
}


static int eph_start(struct eph_data *ephdata)
{
    struct comms_device *commsdevice = ephdata->commsdevice;

    dev_info(&commsdevice->dev, "%s, suspend_mode %d >\n", __func__, ephdata->ephplatform->suspend_mode);

    if (!ephdata->suspended || ephdata->in_bootloader)
    {
        dev_info(&commsdevice->dev, "%s, suspended %d, in_bootloader = %d <\n", __func__, ephdata->suspended, ephdata->in_bootloader);
        return 0;
    }

    switch (ephdata->ephplatform->suspend_mode)
    {
        case EPH_SUSPEND_REGULATOR:
            enable_irq(ephdata->chg_irq);
            #if (ESWIN_EPH861X_I2C)
            /* TODO Pixel4XL specific - Temporary comment out diabling/enable of regulator due to demo handset specific issues */
            eph_regulator_enable(ephdata);
            #endif
            break;

        case EPH_SUSPEND_DEEP_SLEEP:
        default:
            /* do nothing for the moment as no tic sleep */

            break;
    }

    ephdata->suspended = false;

    dev_info(&commsdevice->dev, "%s <\n",__func__);

    return 0;
}

static int eph_stop(struct eph_data *ephdata)
{

    struct comms_device *commsdevice = ephdata->commsdevice;

    dev_info(&commsdevice->dev, "%s, suspend mode %d >\n", __func__, ephdata->ephplatform->suspend_mode);

    if (ephdata->suspended || ephdata->in_bootloader || ephdata->updating_device_settings)
    {
        dev_info(&commsdevice->dev, "%s, suspended %d, in_bootloader %d, updating_device_settings %d <\n",__func__, ephdata->suspended, ephdata->in_bootloader, ephdata->updating_device_settings);
        return 0;
    }

    switch (ephdata->ephplatform->suspend_mode)
    {
        case EPH_SUSPEND_REGULATOR:
            disable_irq(ephdata->chg_irq);
            #if (ESWIN_EPH861X_I2C)
            /* TODO Pixel4XL specific - Temporary comment out diabling of regulator due to demo handset specific issues */
            eph_regulator_disable(ephdata);
            #endif
            eph_clear_all_host_touch_slots(ephdata);
            break;

        case EPH_SUSPEND_DEEP_SLEEP:
        default:

            /* For now does nothing as sleep not implemented in TIC */

            /* Clear all the touch slots on UI */
            eph_clear_all_host_touch_slots(ephdata);

    }

    ephdata->suspended = true;

    dev_info(&commsdevice->dev, "%s <\n",__func__);

    return 0;
}

static int eph_input_open(struct input_dev *inputdev)
{
    struct eph_data *ephdata = (struct eph_data *)input_get_drvdata(inputdev);
    int ret_val;

    dev_dbg(&ephdata->commsdevice->dev, "%s >\n", __func__);

    ret_val = eph_start(ephdata);

    if (ret_val)
    {
        dev_err(&ephdata->commsdevice->dev, "%s failed rc=%d\n", __func__, ret_val);
    }

    return ret_val;
}

static void eph_input_close(struct input_dev *inputdev)
{
    struct eph_data *ephdata = (struct eph_data *)input_get_drvdata(inputdev);
    int ret_val;

    dev_dbg(&ephdata->commsdevice->dev, "%s >\n", __func__);

    ret_val = eph_stop(ephdata);

    if (ret_val)
    {
        dev_err(&ephdata->commsdevice->dev, "%s failed rc=%d\n", __func__, ret_val);
    }
}

#if defined(CONFIG_DRM) //|| defined(CONFIG_BOARD_CLOUDRIPPER)
static int eph_dev_enter_lp_mode(struct eph_data *ephdata);
static int eph_dev_enter_normal_mode(struct eph_data *ephdata);
#endif

#if defined(CONFIG_DRM)
static void eph_panel_notifier_callback(enum panel_event_notifier_tag tag,
		 struct panel_event_notification *notification, void *client_data)
{
	struct eph_data *ephdata = client_data;

	if (!notification) {
		pr_err("Invalid notification\n");
		return;
	}

	ts_debug("Notification type:%d, early_trigger:%d",
			notification->notif_type,
			notification->notif_data.early_trigger);
	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_UNBLANK:
		if (!notification->notif_data.early_trigger)
			eph_dev_enter_normal_mode(ephdata);
	    // heartbeat work is needed
            //schedule_delayed_work(&ephdata->heartbeat_work, msecs_to_jiffies(5000));
            //schedule_work(&ephdata->force_baseline_work);
		break;

	case DRM_PANEL_EVENT_BLANK:
        // disable heartbeat
		if (notification->notif_data.early_trigger)
			eph_dev_enter_lp_mode(ephdata);
		break;

	case DRM_PANEL_EVENT_BLANK_LP:
		ts_debug("received lp event\n");
		break;

	case DRM_PANEL_EVENT_FPS_CHANGE:
		ts_debug("Received fps change old fps:%d new fps:%d\n",
				notification->notif_data.old_fps,
				notification->notif_data.new_fps);
		break;

	default:
		ts_debug("notification serviced :%d\n",
				notification->notif_type);
		break;
	}
}

/*static int eph_notifier_callback(struct notifier_block *nb, unsigned long event,
        void *data)
{
    struct eph_data *ephdata = container_of(nb, struct eph_data, notifier);
    struct msm_drm_notifier *evdata = data;
    struct comms_device *commsdevice;
    struct backlight_device *bd = ephdata->bl;
    unsigned int blank;
    int brightness = 0;

    if (!evdata || evdata->id != 0)
        return 0;

    commsdevice = ephdata->commsdevice;

    dev_dbg(&commsdevice->dev, "%s >>>\n", __func__);
    dev_dbg(&commsdevice->dev, "event %x\n", event);

    if (event != MSM_DRM_EVENT_BLANK)
        return 0;

    if (evdata->data && event == MSM_DRM_EVENT_BLANK && ephdata) {
        blank = *(int *)(evdata->data);

        switch (blank) {
        case MSM_DRM_BLANK_POWERDOWN:
            eph_dev_enter_lp_mode(ephdata);
            if (bd->ops && bd->ops->get_brightness)
                brightness = bd->ops->get_brightness(bd);
            else
                brightness = bd->props.brightness;
            ephdata->last_brightness = brightness;
            dev_info(&commsdevice->dev, "brightness %d\n", brightness);

            cancel_delayed_work(&ephdata->heartbeat_work);
            flush_delayed_work(&ephdata->heartbeat_work);
            break;
        case MSM_DRM_BLANK_UNBLANK:
            eph_dev_enter_normal_mode(ephdata);
	    // heartbeat work is needed
            schedule_delayed_work(&ephdata->heartbeat_work, msecs_to_jiffies(5000));
            schedule_work(&ephdata->force_baseline_work);
            break;
        default:
            break;
        }
    }

    return NOTIFY_OK;
}*/

/*#elif defined(CONFIG_BOARD_CLOUDRIPPER)
struct drm_connector *eph_get_bridge_connector(struct drm_bridge *bridge)
{
    struct drm_connector *connector;
    struct drm_connector_list_iter conn_iter;

    drm_connector_list_iter_begin(bridge->dev, &conn_iter);
    drm_for_each_connector_iter(connector, &conn_iter) {
        if (connector->encoder == bridge->encoder)
            break;
    }
    drm_connector_list_iter_end(&conn_iter);
    return connector;
}

static bool eph_bridge_is_lp_mode(struct drm_connector *connector)
{
    if (connector && connector->state) {
        struct exynos_drm_connector_state *s =
            to_exynos_connector_state(connector->state);
        return s->exynos_mode.is_lp_mode;
    }
    return false;
}

static void eph_panel_bridge_enable(struct drm_bridge *bridge)
{
    struct eph_data *ephdata =
                container_of(bridge, struct eph_data, panel_bridge);
    struct device *dev = &ephdata->commsdevice->dev;

    dev_info(dev, "%s\n", __func__);
    ephdata->is_panel_lp_mode = eph_bridge_is_lp_mode(ephdata->connector);
    if (!ephdata->is_panel_lp_mode) {
        eph_dev_enter_normal_mode(ephdata);
	// heartbeat work is needed
        schedule_delayed_work(&ephdata->heartbeat_work, msecs_to_jiffies(5000));
        schedule_work(&ephdata->force_baseline_work);
    }
}

static void eph_panel_bridge_disable(struct drm_bridge *bridge)
{
    struct eph_data *ephdata =
            container_of(bridge, struct eph_data, panel_bridge);
    struct device *dev = &ephdata->commsdevice->dev;

    if (bridge->encoder && bridge->encoder->crtc) {
        const struct drm_crtc_state *crtc_state = bridge->encoder->crtc->state;

        if (drm_atomic_crtc_effectively_active(crtc_state))
            return;
    }

    dev_info(dev, "%s\n", __func__);

    eph_dev_enter_lp_mode(ephdata);
    ephdata->last_brightness = backlight_get_brightness(ephdata->bl);

    cancel_delayed_work(&ephdata->heartbeat_work);
    flush_delayed_work(&ephdata->heartbeat_work);
}

static void eph_panel_bridge_mode_set(struct drm_bridge *bridge,
                        const struct drm_display_mode *mode,
                        const struct drm_display_mode *adjusted_mode)
{
    struct eph_data *ephdata = container_of(bridge, struct eph_data, panel_bridge);
    struct device *dev = &ephdata->commsdevice->dev;

    dev_info(dev, "%s\n", __func__);

    if (!ephdata->connector || !ephdata->connector->state) {
        ephdata->connector = eph_get_bridge_connector(bridge);
        dev_err(dev, "%s: Get bridge connector.\n", __func__);
    }

}

static const struct drm_bridge_funcs panel_bridge_funcs = {
    .enable = eph_panel_bridge_enable,
    .disable = eph_panel_bridge_disable,
    .mode_set = eph_panel_bridge_mode_set,
};

static int eph_register_panel_bridge(struct eph_data *ephdata)
{
    struct device *dev = &ephdata->commsdevice->dev;
#ifdef CONFIG_OF
    ephdata->panel_bridge.of_node = dev->of_node;
#endif
    ephdata->panel_bridge.funcs = &panel_bridge_funcs;
    drm_bridge_add(&ephdata->panel_bridge);

    dev_info(dev, "%s\n", __func__);

    return 0;
}

static void eph_unregister_panel_bridge(struct eph_data *ephdata)
{
    struct drm_bridge *node;
    struct drm_bridge *bridge = &ephdata->panel_bridge;

    drm_bridge_remove(bridge);

    if (!bridge->dev) // not attached
        return;

    drm_modeset_lock(&bridge->dev->mode_config.connection_mutex, NULL);
    list_for_each_entry(node, &bridge->encoder->bridge_chain, chain_node)
        if (node == bridge) {
            if (bridge->funcs->detach)
                bridge->funcs->detach(bridge);
            list_del(&bridge->chain_node);
            break;
        }
    drm_modeset_unlock(&bridge->dev->mode_config.connection_mutex);
    bridge->dev = NULL;
}*/
#endif //USE_DRM_BRIDGE

static int eph_pinctrl_configure(struct eph_data *ephdata, bool enable)
{
    struct pinctrl_state *state;

    if (IS_ERR_OR_NULL(ephdata->pinctrl)) {
        dev_warn(&ephdata->commsdevice->dev, "Invalid pinctrl\n");
        return -EINVAL;
    }

    dev_dbg(&ephdata->commsdevice->dev, "%s enable %d >>>\n", __func__, enable);

    if (enable) {
        state = pinctrl_lookup_state(ephdata->pinctrl, "ts_active");
        if (IS_ERR(state))
            dev_err(&ephdata->commsdevice->dev, "Could not get ts_active pinstate!\n");
    } else {
        state = pinctrl_lookup_state(ephdata->pinctrl, "ts_suspend");
        if (IS_ERR(state))
            dev_err(&ephdata->commsdevice->dev, "Could not get ts_suspend pinstate!\n");
    }
    if (!IS_ERR_OR_NULL(state))
        return pinctrl_select_state(ephdata->pinctrl, state);

    return 0;
}

static int send_heartbeat(struct eph_data *ephdata)
{
    int ret;
    int retry = 1000;
relock:
    mutex_lock(&ephdata->comms_mutex);
    if (eph_read_chg(ephdata) == 0) {
    	// read the data if irq flag is tiggered by falling
	    if ((irq_get_trigger_type(ephdata->chg_irq) == IRQF_TRIGGER_FALLING) &&
	     (retry >= 0)) {
            if (eph_read_report(ephdata, ephdata->report_buf)) {
                ret = eph_handle_report(ephdata, ephdata->report_buf);
                memset(ephdata->report_buf, 0, COMMS_BUF_SIZE);
                dev_info(&ephdata->commsdevice->dev, "Process unexpected msg %d\n", ret);
	        }
            retry--;
	    }
    	mutex_unlock(&ephdata->comms_mutex);
	    usleep_range(900, 1000);
	    goto relock;
    }
    ret = eph_read_device_information(ephdata);
    mutex_unlock(&ephdata->comms_mutex);
    return ret;
}

static void heartbeat_work_handler(struct work_struct *work)
{
    int ret = 0;
    struct delayed_work *dwork = to_delayed_work(work);
    struct eph_data *ephdata = container_of(dwork, struct eph_data, heartbeat_work);
    struct device *dev = &ephdata->commsdevice->dev;

    dev_info(dev, "ic heartbeat work...\n");
    // check power
    if (!regulator_is_enabled(ephdata->reg_vdd)) {
        schedule_delayed_work(&ephdata->heartbeat_work, msecs_to_jiffies(2000));
        return;
    }
    if (!regulator_is_enabled(ephdata->reg_avdd))
        dev_warn(dev, "ic avdd is not enable\n");

    if (ephdata->suspended) {
        if (ephdata->in_bootloader) {
            dev_warn(dev, "sys suspend, ic in boot mode!\n");
            goto ic_reset;
        } else {
#if 0
            // ic in gesture mode, fod mode
            if (ephdata->gesture_wakeup_enable == true) {
                    ret = send_heartbeat(ephdata);
                    if (ret) {
                        dev_err(dev, "sys suspend, heartbeat fail %d\n", ret);
                        goto ic_recovery;
                    }


            } else {
            // ic in deep mode or spi in low power mode
                    dev_info(dev, "sys suspend, ic in deep mode or spi in lp mode");
            }
#else
        dev_info(dev, "sys suspend, ic in deep mode or spi in lp mode");
#endif
        }
    } else {
        if (ephdata->in_bootloader) {
            /* may loading firmware or setting */
            dev_warn(dev, "sys active, ic in boot mode!\n");
        } else {
            // ic in active or idle mode
            ret = send_heartbeat(ephdata);
            if (ret) {
                dev_err(dev, "sys normal, heartbeat fail %d\n", ret);
                goto ic_recovery;
            }
        }
    }

    schedule_delayed_work(&ephdata->heartbeat_work, msecs_to_jiffies(5000));
    return;

ic_recovery:
    eph_recovery_device(ephdata);
    schedule_delayed_work(&ephdata->heartbeat_work, msecs_to_jiffies(3000));
    return;
ic_reset:
    eph_reset_device(ephdata);
    schedule_delayed_work(&ephdata->heartbeat_work, msecs_to_jiffies(3000));
    return;
}

#if defined(CONFIG_DRM)
static int eph_check_dt(struct device_node *np)
{
	int i;
	int count;
	struct device_node *node;
	struct drm_panel *panel;

	count = of_count_phandle_with_args(np, "panel", NULL);
	if (count <= 0)
		return 0;

	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "panel", i);
		panel = of_drm_find_panel(node);
		of_node_put(node);
		if (!IS_ERR(panel)) {
			active_panel = panel;
			return 0;
		}
	}

	return PTR_ERR(panel);
}

static int eph_check_default_tp(struct device_node *dt, const char *prop)
{
	const char **active_tp = NULL;
	int count, tmp, score = 0;
	const char *active;
	int ret, i;

	count = of_property_count_strings(dt->parent, prop);
	if (count <= 0 || count > 3)
		return -ENODEV;

	active_tp = kcalloc(count, sizeof(char *),  GFP_KERNEL);
	if (!active_tp) {
		ts_err("FTS alloc failed\n");
		return -ENOMEM;
	}

	ret = of_property_read_string_array(dt->parent, prop,
			active_tp, count);
	if (ret < 0) {
		ts_err("fail to read %s %d\n", prop, ret);
		ret = -ENODEV;
		goto out;
	}

	for (i = 0; i < count; i++) {
		active = active_tp[i];
		if (active != NULL) {
			tmp = of_device_is_compatible(dt, active);
			if (tmp > 0)
				score++;
		}
	}

	if (score <= 0) {
		ts_err("not match this driver\n");
		ret = -ENODEV;
		goto out;
	}
	ret = 0;
out:
	kfree(active_tp);
	return ret;
}

#endif

#if (ESWIN_EPH861X_SPI)
static int eph_probe(struct comms_device *commsdevice)
#endif
#if (ESWIN_EPH861X_I2C)
static int eph_probe(struct comms_device *commsdevice, const struct comms_device_id *id)
#endif
{
    struct eph_data *ephdata;
    const struct eph_platform_data *ephplatform;
    int ret_val;
    int device_info_read_retry = 0;
    struct device *dev = &commsdevice->dev;

    struct device_node *node = commsdevice->dev.of_node;
    dev_dbg(dev, "%s >>>\n", __func__);
    pr_err("eph_probe----11--100000ms--\n");

    msleep(10000);

    ret_val = eph_comms_specific_checks(commsdevice);
    if (ret_val)
    {
        return -EINVAL;
    }
    pr_err("eph_probe----22----\n");
    ephplatform = eph_platform_data_get(commsdevice);
    if (IS_ERR(ephplatform))
    {
        return PTR_ERR(ephplatform);
    }
#if defined(CONFIG_DRM)
	ret_val = eph_check_dt(node);
	if (ret_val == -EPROBE_DEFER)
    {
        pr_err("eph_check dt err %d\n", ret_val);
		return ret_val;
    }

	if (ret_val) {
		if (!eph_check_default_tp(node, "qcom,touch-active"))
			ret_val = -EPROBE_DEFER;
		else
			ret_val = -ENODEV;

		return ret_val;
	}
#endif
    ephdata = (struct eph_data *)kzalloc(sizeof(struct eph_data), GFP_KERNEL);
    if (!ephdata)
    {
        return -ENOMEM;
    }

    ephdata->bl = backlight_device_get_by_type(BACKLIGHT_RAW);

    if (ephdata->bl) {
        dev_info(dev, "backlight brightness %x\n", ephdata->bl->props.brightness);
        dev_info(dev, "backlight max brightness %x\n", ephdata->bl->props.max_brightness);
    } else {
        dev_info(dev, "backlight not ready\n");
        ret_val = -EPROBE_DEFER;
        goto err_free_mem;
    }

    INIT_WORK(&ephdata->force_baseline_work, eph_trigger_baseline_work);
    INIT_DELAYED_WORK(&ephdata->heartbeat_work, heartbeat_work_handler);

    ephdata->commsdevice = commsdevice;
    ephdata->ephplatform = ephplatform;
    eph_comms_driver_data_set(commsdevice, ephdata);

    //ephdata->pinctrl = devm_pinctrl_get(&commsdevice->dev);
    ephdata->pinctrl = NULL;
    if (IS_ERR_OR_NULL(ephdata->pinctrl)) {
        dev_warn(dev, "Could not get pinctrl\n");
    } else {
        eph_pinctrl_configure(ephdata, true);
    }

    ret_val = eph_allocate_comms_memory(commsdevice, ephdata);


    if (ephdata->ephplatform->device_settings_name)
    {
        eph_update_file_name(&ephdata->commsdevice->dev,
                             &ephdata->device_settings_name,
                             ephdata->ephplatform->device_settings_name,
                             strlen(ephdata->ephplatform->device_settings_name));
    }

    if (ephdata->ephplatform->fw_name)
    {
        eph_update_file_name(&ephdata->commsdevice->dev,
                             &ephdata->fw_name,
                             ephdata->ephplatform->fw_name,
                             strlen(ephdata->ephplatform->fw_name));
    }

    dev_info(dev, "%s ephdata->fw_name: %s, ephdata->device_settings_name: %s \n", __func__, ephdata->fw_name, ephdata->device_settings_name);

    init_completion(&ephdata->chg_completion);
    init_completion(&ephdata->reset_completion);

    mutex_init(&ephdata->comms_mutex);
    mutex_init(&ephdata->sysfs_report_buffer_lock);

    device_init_wakeup(&commsdevice->dev, true);

    ret_val = eph_gpio_setup(ephdata);
    if (ret_val)
    {
        goto err_free_irq;
    }

    ret_val = eph_acquire_irq(ephdata);
    if (ret_val)
    {
        goto err_free_mem;
    }

    ret_val = eph_probe_regulators(ephdata);
    if (ret_val)
    {
        goto err_free_irq;
    }

    //wait ic boot done
    msleep(5000);

    /* Need to have IRQ disabled before calling eph_initialize() as it re-enables it */
    disable_irq(ephdata->chg_irq);

    ret_val = sysfs_create_group(&commsdevice->dev.kobj, &eph_fw_attr_group);
    if (ret_val)
    {
        dev_err(dev, "Failure %d creating fw sysfs group\n", ret_val);
        return ret_val;
    }
    // get dev info from ic
    mutex_lock(&ephdata->comms_mutex);
    do {
        /* retry we didnt get a device information response */
        ret_val = eph_read_device_information(ephdata);
        device_info_read_retry++;

    } while(ret_val && (device_info_read_retry <= COMMS_READ_RETRY_NUM));
    mutex_unlock(&ephdata->comms_mutex);
    if (ret_val) {
	dev_warn(dev, "read ic info failed %d", ret_val);
    } else {
        dev_info(dev, "zxz Product ID: %u Variant ID: %u Application_version_major %u Application_version_minor: %u Bootloader_version: %u Protocol_version: %u CRC:%u\n",
                 ephdata->ephdeviceinfo.product_id, ephdata->ephdeviceinfo.variant_id,
                 ephdata->ephdeviceinfo.application_version_major, ephdata->ephdeviceinfo.application_version_minor,
                 ephdata->ephdeviceinfo.bootloader_version, ephdata->ephdeviceinfo.protocol_version, ephdata->ephdeviceinfo.crc);
    }
#if 1
    ret_val = eph_load_fw(&commsdevice->dev);
    if (ret_val) {
        dev_err(dev, "The firmware update failed(%d)\n", ret_val);
        //goto err_free_irq;
    } else {
        dev_info(dev, "The firmware update done");
        //delay for ic stabel
        usleep_range(300000, 320000);
    }
#endif

    ret_val = eph_initialize(ephdata);
    if (ret_val)
    {
        goto err_free_irq;
    }
    ret_val = eph_input_device_initialize(ephdata);
    if (ret_val) {
        goto err_free_irq;
    }

#if defined(CONFIG_DRM)
    eph_register_for_panel_events(node, ephdata);
    // ephdata->notifier.notifier_call = eph_notifier_callback;
    // ret_val = msm_drm_register_client(&ephdata->notifier);
    // if (ret_val < 0)
    //     dev_err(dev, "Failure %d register msm drm client\n", ret_val);
    // else
    //     dev_info(dev, "success register norifier\n");
//#elif defined(CONFIG_BOARD_CLOUDRIPPER)
    // ret_val = eph_register_panel_bridge(ephdata);
    // if (ret_val < 0)
    //     dev_err(dev, "Failure %d panel bridge\n", ret_val);
    // else
    //     dev_info(dev, "success register panel bridge\n");
#endif

    /* default disable gesture */
    ephdata->gesture_mode = 0x0;
    ephdata->gesture_wakeup_enable = false;

    ephdata->lp = false;
    ephdata->irq_wake = false;

    //schedule_delayed_work(&ephdata->heartbeat_work, msecs_to_jiffies(5000));

    dev_info(dev, "%s <\n", __func__);
    return 0;

err_free_irq:
    if (ephdata->chg_irq)
    {
        free_irq(ephdata->chg_irq, ephdata);
    }

    gpio_free(ephdata->ephplatform->gpio_reset);
    gpio_free(ephdata->ephplatform->gpio_chg_irq);
    if(ephdata->reg_vdd)
    {
        regulator_put(ephdata->reg_vdd);
    }
    if(ephdata->reg_avdd)
    {
        regulator_put(ephdata->reg_avdd);
    }
err_free_mem:
    kfree(ephdata);
    dev_info(dev, "%s error\n", __func__);
    return ret_val;
}

#if KERNEL_VERSION(5, 17, 0) >= LINUX_VERSION_CODE
static int eph_remove(struct comms_device *commsdevice)
#else
static void eph_remove(struct comms_device *commsdevice)
#endif
{
    struct eph_data *ephdata = eph_comms_driver_data_get(commsdevice);
    dev_info(&commsdevice->dev, "%s >\n", __func__);

    cancel_delayed_work_sync(&ephdata->heartbeat_work);

    sysfs_remove_group(&commsdevice->dev.kobj, &eph_fw_attr_group);
    eph_sysfs_mem_access_remove(ephdata);

#if defined(CONFIG_DRM)
    if (ephdata->notifier_cookie)
			panel_event_notifier_unregister(ephdata->notifier_cookie);
// #elif defined(CONFIG_BOARD_CLOUDRIPPER)
//     eph_unregister_panel_bridge(ephdata);
#endif

    if (ephdata->chg_irq)
    {
        free_irq(ephdata->chg_irq, ephdata);
    }

    gpio_free(ephdata->ephplatform->gpio_reset);
    gpio_free(ephdata->ephplatform->gpio_chg_irq);

    if(ephdata->reg_avdd)
    {
        regulator_put(ephdata->reg_avdd);
    }
    if(ephdata->reg_vdd)
    {
        regulator_put(ephdata->reg_vdd);
    }
    eph_unregister_input_device(ephdata);

#if (ESWIN_EPH861X_SPI && ESWIN_EPH861X_SPI_USE_DMA)
    dma_pool_free(pool_rx, ephdata->comms_receive_buf, ephdata->comms_dma_handle_rx);
    dma_pool_free(pool_tx, ephdata->comms_send_buf, ephdata->comms_dma_handle_tx);

    dma_pool_destroy(pool_rx);
    dma_pool_destroy(pool_tx);
#elif (ESWIN_EPH861X_SPI)
    kfree(ephdata->comms_receive_buf);
    kfree(ephdata->comms_send_buf);
#endif

    kfree(ephdata->comms_send_crc_buf);
    kfree(ephdata->report_buf);
    kfree(ephdata);

#if KERNEL_VERSION(5, 17, 0) >= LINUX_VERSION_CODE 
    return 0;
#endif
}

#if defined(CONFIG_DRM) //|| defined(CONFIG_BOARD_CLOUDRIPPER)
int eph_enable_report_event(struct device *dev, int enable)
{
    int ret = 0;
    struct eph_data *ephdata = (struct eph_data*)dev_get_drvdata(dev);
    u8 cfg[] = { 0x8, 0x5, 0x0, 0xc, 0x0, 0x1, 0x0, 0x2 };
    u16 length = sizeof(cfg)/sizeof(cfg[0]);

    if (enable == 1)
    {
        cfg[7] = 0x3;
    }
    else
    {
        cfg[7] = 0x2;
    }

    mutex_lock(&ephdata->comms_mutex);
    ret = eph_write_control_config(ephdata, length, &cfg[0]);
    mutex_unlock(&ephdata->comms_mutex);

    dev_info(&ephdata->commsdevice->dev, "ic report %s, %d\n", enable ? "enable" : "disable", ret);
    return ret;
}

static int eph_deep_mode_enable(struct eph_data *ephdata, int enable)
{
    int ret_val;
    u8 cfg[] = {0x08, 0x05, 0x00, 0x00, 0x00, 0x16, 0x00, 0x02};
    u16 length = (sizeof(cfg)/sizeof(cfg[0]));

    if (enable == 1)
        cfg[7] = 0x02;
    else if (enable == 0)
        cfg[7] = 0x0;

    mutex_lock(&ephdata->comms_mutex);
    ret_val = eph_write_control_config(ephdata, length, &cfg[0]);
    mutex_unlock(&ephdata->comms_mutex);
    return ret_val;
}

static int eph_dev_enter_lp_mode(struct eph_data *ephdata)
{
    int ret_val = 0;
    struct device *dev = &ephdata->commsdevice->dev;

    if (ephdata->lp)
        return 0;

    if (!ephdata->lp) {
        if (ephdata->gesture_wakeup_enable) {
            if (eph_enable_report_event(dev, 0)) {
                dev_warn(dev, "disable ic report ev faild\n");
            }
            enable_irq_wake(ephdata->chg_irq);
            ephdata->irq_wake = true;
            ret_val = eph_gesture_mode_set(ephdata, ephdata->gesture_mode | BIT(0));
	    } else { 
            // set tic in deep mode
	        ret_val = eph_deep_mode_enable(ephdata, 1);
	    }

        ephdata->lp = true;
    }

    if (ret_val)
        dev_err(dev, "Failed to enter lp mode (%d)\n", ret_val);

    return ret_val;
}

static int eph_dev_enter_normal_mode(struct eph_data *ephdata)
{
    int ret_val = 0;
    struct device *dev = &ephdata->commsdevice->dev;

    if (!ephdata->lp)
        return 0;

    if (ephdata->lp) {
        if (ephdata->gesture_wakeup_enable) {
            disable_irq_wake(ephdata->chg_irq);
            ephdata->irq_wake = false;
            ret_val = eph_gesture_mode_set(ephdata, ephdata->gesture_mode & (~BIT(0)));
	        if (ret_val)
                dev_err(dev, "gesture mode set failed %d\n", ret_val);
            if (eph_enable_report_event(dev, 1)) {
                dev_warn(dev, "disable ic report ev faild\n");
            }
	    } else {
                // wake up tic
                ret_val = eph_deep_mode_enable(ephdata, 0);
                // wait tic wake
                msleep(200);
	    }

        ephdata->lp = false;
    }

    if (ret_val)
        dev_err(dev, "Failed to enter normal mode (%d)\n", ret_val);

    return ret_val;
}
#endif

static int __maybe_unused eph_suspend(struct device *dev)
{
    struct comms_device *commsdevice = eph_comms_device_get(dev);
    struct eph_data *ephdata = eph_comms_driver_data_get(commsdevice);

    dev_dbg(&ephdata->commsdevice->dev, "%s >\n", __func__);

    if (!ephdata->inputdev)
    {
        return 0;
    }

    if (ephdata->suspended == true)
        return 0;

    cancel_work_sync(&ephdata->force_baseline_work);

    mutex_lock(&ephdata->inputdev->mutex);

    if (ephdata->inputdev->users)
    {
        (void)eph_stop(ephdata);
    }

    mutex_unlock(&ephdata->inputdev->mutex);

    eph_pinctrl_configure(ephdata, false);

    return 0;
}

static int __maybe_unused eph_resume(struct device *dev)
{
    struct comms_device *commsdevice = eph_comms_device_get(dev);
    struct eph_data *ephdata = eph_comms_driver_data_get(commsdevice);

    dev_dbg(&ephdata->commsdevice->dev, "%s >\n", __func__);

    if (!ephdata->inputdev)
    {
        return 0;
    }

    if (ephdata->suspended == false)
        return 0;

    eph_pinctrl_configure(ephdata, true);

    mutex_lock(&ephdata->inputdev->mutex);

    eph_clear_all_host_touch_slots(ephdata);

    if (ephdata->inputdev->users)
    {
        (void)eph_start(ephdata);
    }

    mutex_unlock(&ephdata->inputdev->mutex);

    return 0;
}

static SIMPLE_DEV_PM_OPS(eph_pm_ops, eph_suspend, eph_resume);

#ifdef CONFIG_OF // Open Firmware (Device Tree)
static const struct of_device_id eph_of_match[] =
{
    { .compatible = "eswin,eph861x", },
    {},
};
MODULE_DEVICE_TABLE(of, eph_of_match);
#endif // CONFIG_OF

static const struct comms_device_id eph_id[] =
{
    { "eswin_eph861x", 0 },
    { }
};
MODULE_DEVICE_TABLE(comms_mode_type, eph_id);

static struct comms_driver eph_driver =
{
    .id_table   = eph_id,
    .probe      = eph_probe,
    .remove     = eph_remove,
    .driver = {
        .name   = "eswin_eph861x",
        .owner  = THIS_MODULE,
        .of_match_table = of_match_ptr(eph_of_match),
        .pm = &eph_pm_ops,
    },

};

module_comms_driver(eph_driver);

/* Module information */
MODULE_AUTHOR("chris.ollerenshaw@eswin.com>");
MODULE_DESCRIPTION("ESWIN EPH861 series Touchscreen driver");
MODULE_LICENSE("GPL");

