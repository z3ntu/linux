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
#include <uapi/linux/stat.h>
#include <linux/jiffies.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#include "eswin_eph861x_project_config.h"
#include "eswin_eph861x_tlv.h"
#include "eswin_eph861x_types.h"
#include "eswin_eph861x_eswin.h"
#include "eswin_eph861x_comms.h"

int eph_check_firmware_format(struct device *dev, const struct firmware *fw)
{
    unsigned int pos = 0;
    char c;

    while (pos < fw->size)
    {
        c = *(fw->data + pos);

        if (c < '0' || (c > '9' && c < 'A') || c > 'F')
        {
            return 0;
        }

        pos++;
    }

    dev_err(dev, "Aborting: firmware file must be in binary format\n");
    dev_err(dev, "xxd -r -p EPH861x_XXXXX_XXXX.enc > EPH861x_XXXXX_XXXX.bin\n");

    return -EINVAL;
}

#ifdef IC_UPDATE_DETECT
// return 0 means ic isn't need to update
// return 1 means need to update
int eph_check_ic_update(struct eph_data *ephdata, const struct firmware *fw)
{
    int ret = 0;
    struct eph_firmware_info fw_info;
    struct eph_device_info *dev_info;
    struct device *dev = &ephdata->commsdevice->dev;

    if (!fw)
        return -EINVAL;

    dev_info = &ephdata->ephdeviceinfo;

    memcpy(&fw_info, fw->data, sizeof(struct eph_firmware_info));

    if ((fw_info.product_id != dev_info->product_id) && (fw_info.variant_id !=
        dev_info->variant_id)) {
        dev_info(dev, "product version diff or requested fw no info in head");
        return ret;
    }

    // check crc
    if (fw_info.crc != eph_get_data_crc((u8 *)&fw_info, sizeof(struct eph_firmware_info) - 1)) {
        dev_err(dev, "requested fw info is incompete");
        return -EINVAL;
    }

    // check bootloader
    if (fw_info.bootloader_version != dev_info->bootloader_version) {
        dev_warn(dev, "firmware bootloader version diff");
    }

    // check app version
    if ((fw_info.application_version_major != dev_info->application_version_major) ||
        (fw_info.application_version_minor != dev_info->application_version_minor)) {
        dev_info(dev, "firmware application version diff, wait update");
        ret = 1;
    }
    dev_info(dev, "ic vers: %d %d, requested fw vers: %d %d\n", dev_info->application_version_major,
		    dev_info->application_version_minor, fw_info.application_version_major,
		    fw_info.application_version_minor);

    return ret;
}
#endif

int eph_update_file_name(struct device *dev,
                                char **out_file_name,
                                const char *in_file_name,
                                size_t in_str_len)
{
    char *file_name_tmp;

    /* Simple sanity check */
    if (in_str_len > 64)
    {
        dev_warn(dev, "File name too long\n");
        return -EINVAL;
    }

    file_name_tmp = (char *)krealloc(*out_file_name, in_str_len + 1, GFP_KERNEL);
    if (!file_name_tmp)
    {
        return -ENOMEM;
    }

    *out_file_name = file_name_tmp;
    memcpy(*out_file_name, in_file_name, in_str_len);

    /* Echo into the sysfs entry may append newline at the end of buf */
    if (in_file_name[in_str_len - 1] == '\n')
    {
        (*out_file_name)[in_str_len - 1] = '\0';
    }
    else
    {
        (*out_file_name)[in_str_len] = '\0';
    }

    return 0;
}




#ifdef CONFIG_OF // Open Firmware (Device Tree)
const struct eph_platform_data *eph_platform_data_get_from_device_tree(struct comms_device *commsdevice)
{
    struct eph_platform_data *ephplatform;
    struct device_node *devnode = commsdevice->dev.of_node;
    int ret_val;

    dev_dbg(&commsdevice->dev, "%s using device tree > \n", __func__);

    if (!devnode)
    {
        return (struct eph_platform_data *)ERR_PTR(-ENOENT); /* no device tree device */
    }

    ephplatform = (struct eph_platform_data *)devm_kzalloc(&commsdevice->dev, sizeof(struct eph_platform_data), GFP_KERNEL);
    if (!ephplatform)
    {
        return (struct eph_platform_data *)ERR_PTR(-ENOMEM);
    }

    ephplatform->gpio_reset = of_get_named_gpio_flags(devnode, "eswin,reset-gpio", 0, NULL);
    ephplatform->gpio_chg_irq = of_get_named_gpio_flags(devnode, "eswin,irq-gpio", 0, NULL);

    /* returns pointer to already allocated memory containing the string */
    ret_val = of_property_read_string(devnode, "eswin,regulator_dvdd", &ephplatform->regulator_dvdd);
    if (ret_val)
    {
        dev_err(&commsdevice->dev,  "Couldn't read eswin,regulator_dvdd: %d\n", ret_val);
    }

    ret_val = of_property_read_string(devnode, "eswin,regulator_avdd", &ephplatform->regulator_avdd);
    if (ret_val)
    {
        dev_err(&commsdevice->dev,  "Couldn't read eswin,regulator_avdd: %d\n", ret_val);
    }

    ret_val = of_property_read_string(devnode, "eswin,device_settings_name", &ephplatform->device_settings_name);
    if (ret_val)
    {
        dev_err(&commsdevice->dev,  "Couldn't read eswin,device_settings_name: %d\n", ret_val);
    }

    ret_val = of_property_read_string(devnode, "eswin,fw_name", &ephplatform->fw_name);
    if (ret_val)
    {
        dev_err(&commsdevice->dev,  "Couldn't read eswin,fw_name: %d\n", ret_val);
    }

    of_property_read_string(devnode, "eswin,input_name", &ephplatform->input_name);

    of_property_read_u32(devnode, "eswin,suspend-mode", &ephplatform->suspend_mode);

    of_property_read_u32(devnode, "eswin,panel-invert-x", &ephplatform->panel_invert_x);

    of_property_read_u32(devnode, "eswin,panel-invert-y", &ephplatform->panel_invert_y);

    ret_val = of_property_read_u32(devnode, "eswin,panel-max-x", &ephplatform->panel_max_x);
    if (ret_val) {
        dev_err(&commsdevice->dev,  "Couldn't read eswin,panel_max_x: %d\n", ret_val);
        return (struct eph_platform_data *)ERR_PTR(-EINVAL);
    }
    ret_val = of_property_read_u32(devnode, "eswin,panel-max-y", &ephplatform->panel_max_y);
    if (ret_val) {
        dev_err(&commsdevice->dev,  "Couldn't read eswin,panel_max_y: %d\n", ret_val);
        return (struct eph_platform_data *)ERR_PTR(-EINVAL);
    }

    dev_info(&commsdevice->dev, "panel X%uY%u\n", ephplatform->panel_max_x,
            ephplatform->panel_max_y);

    dev_dbg(&commsdevice->dev, "%s using device tree <\n", __func__);

    return ephplatform;
}
#else // CONFIG_OF
const struct eph_platform_data *eph_platform_data_get_from_device_tree(struct comms_device *commsdevice)
{
    return (struct eph_platform_data *)ERR_PTR(-ENOENT);
}
#endif // CONFIG_OF


struct eph_platform_data *eph_platform_data_get_default(struct comms_device *commsdevice)
{
    struct eph_platform_data *ephplatform = (struct eph_platform_data *)devm_kzalloc(&commsdevice->dev, sizeof(struct eph_platform_data), GFP_KERNEL);
    if (!ephplatform)
    {
        return (struct eph_platform_data *)ERR_PTR(-ENOMEM);
    }

    /* Set default parameters */

    dev_dbg(&commsdevice->dev, "%s <\n", __func__);

    return ephplatform;
}

const struct eph_platform_data * eph_platform_data_get(struct comms_device *commsdevice)
{
    const struct eph_platform_data *ephplatform;

    dev_dbg(&commsdevice->dev, "%s >\n", __func__);

    ephplatform = (struct eph_platform_data *)dev_get_platdata(&commsdevice->dev);
    if (ephplatform)
    {
        return ephplatform;
    }

    ephplatform = eph_platform_data_get_from_device_tree(commsdevice);
    if (!IS_ERR(ephplatform) || PTR_ERR(ephplatform) != -ENOENT)
    {
        return ephplatform;
    }

    ephplatform = eph_platform_data_get_default(commsdevice);
    if (!IS_ERR(ephplatform))
    {
        return ephplatform;
    }

    dev_err(&commsdevice->dev, "No platform data specified\n");
    return (struct eph_platform_data *)ERR_PTR(-EINVAL);
}

int eph_gpio_setup(struct eph_data *ephdata)
{
    int ret_val;
    dev_dbg(&ephdata->commsdevice->dev, "%s >\n", __func__);

    ret_val = gpio_request(ephdata->ephplatform->gpio_chg_irq, "irq-gpio");
    if (ret_val)
    {
        dev_err(&ephdata->commsdevice->dev, "gpio_request %lu (%d)", ephdata->ephplatform->gpio_chg_irq, ret_val);
        return ret_val;
    }
    ret_val = gpio_direction_input(ephdata->ephplatform->gpio_chg_irq);
    if (ret_val)
    {
        dev_err(&ephdata->commsdevice->dev, "gpio_direction_input %lu (%d)", ephdata->ephplatform->gpio_chg_irq, ret_val);
        return ret_val;
    }
    dev_dbg(&ephdata->commsdevice->dev, "gpio_chg_irq %lu IN %d\n", ephdata->ephplatform->gpio_chg_irq, (u8)gpio_get_value(ephdata->ephplatform->gpio_chg_irq));

    ret_val = gpio_request(ephdata->ephplatform->gpio_reset, "reset-gpio");
    if (ret_val)
    {
        dev_err(&ephdata->commsdevice->dev, "gpio_request %lu (%d)", ephdata->ephplatform->gpio_reset, ret_val);
        return ret_val;
    }
    /* Initialise so that we are holding the device in reset until power has been applied */
    ret_val = gpio_direction_output(ephdata->ephplatform->gpio_reset, GPIO_RESET_YES_LOW);
    if (ret_val)
    {
        dev_err(&ephdata->commsdevice->dev, "gpio_direction_output %lu (%d)", ephdata->ephplatform->gpio_reset, ret_val);
        return ret_val;
    }
    dev_dbg(&ephdata->commsdevice->dev, "gpio_reset %lu OUT %d\n", ephdata->ephplatform->gpio_reset, GPIO_RESET_YES_LOW);

    dev_dbg(&ephdata->commsdevice->dev, "%s <\n", __func__);

    return 0;
}

int eph_wait_for_completion(struct eph_data *ephdata,
                                   struct completion *comp,
                                   unsigned int timeout_ms,
                                   const char *dbg_str)
{
    struct device *dev = &ephdata->commsdevice->dev;
    unsigned long timeout = msecs_to_jiffies(timeout_ms);
    long ret_val;

    dev_dbg(&ephdata->commsdevice->dev, "%s %s >\n", __func__, dbg_str);

    ret_val = wait_for_completion_interruptible_timeout(comp, timeout);
    if (ret_val < 0)
    {
        return ret_val;
    }
    if (ret_val == 0)
    {
        dev_err(dev, "wait_for_completion timeout %s\n", dbg_str);
        return -ETIMEDOUT;
    }
    return 0;
}


void eph_regulator_enable(struct eph_data *ephdata)
{
    int ret_val;

    dev_err(&ephdata->commsdevice->dev, "zcy %s >\n", __func__);

    if (!ephdata->reg_vdd || !ephdata->reg_avdd)
    {
        dev_err(&ephdata->commsdevice->dev, "zcy %s out 1\n", __func__);
        return;
    }

    gpio_set_value(ephdata->ephplatform->gpio_reset, GPIO_RESET_YES_LOW);

    ret_val = regulator_enable(ephdata->reg_vdd);
    if (ret_val)
    {
        dev_err(&ephdata->commsdevice->dev, "zcy %s out 2\n", __func__);
        return;
    }

    ret_val = regulator_enable(ephdata->reg_avdd);
    if (ret_val)
    {
        dev_err(&ephdata->commsdevice->dev, "zcy %s out 3\n", __func__);
        return;
    }

    /* According to power sequencing specification, RESET line must be kept 
     * low until some time after regulators come up to voltage */
    msleep(EPH_REGULATOR_DELAY);
    gpio_set_value(ephdata->ephplatform->gpio_reset, GPIO_RESET_NO_HIGH);
    //TODO this additional delay should not be needed
    /* Delay to prevent poor signals after power up. This will allow device time to "settle" before baseline */
    msleep(EPH_POWERON_DELAY);
retry_wait:
    reinit_completion(&ephdata->chg_completion);
    ephdata->in_bootloader = true;
    ret_val = eph_wait_for_completion(ephdata, &ephdata->chg_completion, EPH_POWERON_DELAY, "CHG");
    if (ret_val == -EINTR)
    {
        goto retry_wait;
    }
    ephdata->in_bootloader = false;
    dev_err(&ephdata->commsdevice->dev, "zcy %s out 4\n", __func__);
 }

void eph_regulator_disable(struct eph_data *ephdata)
{
    dev_dbg(&ephdata->commsdevice->dev, "%s >\n", __func__);

    if (!ephdata->reg_vdd || !ephdata->reg_avdd)
    {
        return;
    }

    regulator_disable(ephdata->reg_vdd);
    regulator_disable(ephdata->reg_avdd);
}

void eph_recovery_device(struct eph_data *ephdata)
{
    int ret_val;

    disable_irq(ephdata->chg_irq);
    gpio_set_value(ephdata->ephplatform->gpio_reset, GPIO_RESET_YES_LOW);
    if (ephdata->reg_vdd && regulator_is_enabled(ephdata->reg_vdd))
        regulator_disable(ephdata->reg_vdd);
    if (ephdata->reg_avdd && regulator_is_enabled(ephdata->reg_avdd))
        regulator_disable(ephdata->reg_avdd);

    msleep(500); // at least 100ms for ic discharge

    if (ephdata->reg_vdd)
        ret_val = regulator_enable(ephdata->reg_vdd);
    if (ephdata->reg_avdd)
        ret_val = regulator_enable(ephdata->reg_avdd);

    msleep(20); // ic spec at least 10ms

    gpio_set_value(ephdata->ephplatform->gpio_reset, GPIO_RESET_NO_HIGH);

    // wait ic stable, ic spec should no comms action before ic has
    // first data ready
    // sleep over 200ms or wait INT be low
    msleep(200);

    enable_irq(ephdata->chg_irq);

    dev_dbg(&ephdata->commsdevice->dev, "%s\n", __func__);

    return;
}

void eph_reset_device(struct eph_data *ephdata)
{

    dev_dbg(&ephdata->commsdevice->dev, "%s gpio is %ld >\n", __func__, ephdata->ephplatform->gpio_reset);
    gpio_set_value(ephdata->ephplatform->gpio_reset, GPIO_RESET_YES_LOW);
    msleep(1);
    gpio_set_value(ephdata->ephplatform->gpio_reset, GPIO_RESET_NO_HIGH);
    msleep(EPH_POWERON_DELAY);

    return;

  }
