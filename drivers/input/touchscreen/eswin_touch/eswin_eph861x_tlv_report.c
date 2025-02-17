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

#include <linux/mutex.h>
#include <linux/kernel.h>
#include <uapi/linux/input-event-codes.h>


#include <linux/module.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/input/mt.h>

#include "eswin_eph861x_project_config.h"
#include "eswin_eph861x_tlv.h"
#include "eswin_eph861x_types.h"
#include "eswin_eph861x_comms.h"
#include "eswin_eph861x_tlv_report.h"

#define EPH_MT_PRESSURE_CONTACTING     1
#define EPH_MT_PRESSURE_HOVER          0


u8 sysfs_report_buf[PAGE_SIZE]={0};
u8 sysfs_packetised_eng_buf[PAGE_SIZE]={0};

static void eph_recv_touch_report(struct eph_data* ephdata, u8* message);




const char *get_touch_type_str(u8 touch_type)
{
    switch (touch_type)
    {
        case CONTACT_TYPE:
            return "Contact touch type";
        case RELEASE_TYPE:
            return "Release touch type";
        case HOVER_TYPE:
            return "Hover touch type";
        case STYLUS_POSITION_TYPE:
            return "Pen touch type";
        case GESTURE_TYPE:
            return "Gesture touch type";
        case STYLUS_RELEASE_TYPE:
            return "Pen touch release type";
        default:
            break;
    }
    return "???";
}

/* gesture report format
 * Byte |          Field           |   Value
 * 0    |  Type(4-7)  Length(0-3)  |   5, 7
 * 1    |        Gesture Type      |    x
 * 2    |       X location Low     |    x
 * 3    |       X location High    |    x
 * 4    |       Y location Low     |    x
 * 5    |       Y location High    |    x
 * 6    |          Size Low        |    x
 * 7    |          Size High       |    x
 */
static void eph_gesture_event_process(struct eph_data *ephdata, u8 *message)
{
    struct device *dev = &ephdata->commsdevice->dev;
    struct input_dev *input_dev = ephdata->inputdev;

    u8 gesture_type = message[1];

    printk("eswin gesture report type %d\n", gesture_type);
    switch (gesture_type) {
    case GESTURE_TAP:
    {
        input_report_key(input_dev, KEY_WAKEUP, 1);
        input_sync(input_dev);
        input_report_key(input_dev, KEY_WAKEUP, 0);
        input_sync(input_dev);
        break;
    }
    case GESTURE_DOUBLE_TAP:
    {
        input_report_key(input_dev, KEY_WAKEUP, 1);
        input_sync(input_dev);
        input_report_key(input_dev, KEY_WAKEUP, 0);
        input_sync(input_dev);
        break;
    }
    case GESTURE_SWIPE_LEFT:
        /* TODO: Implement according to requirement */
        break;
    case GESTURE_SWIPE_RIGHT:
        /* TODO: Implement according to requirement */
        break;
    case GESTURE_SWIPE_UP:
        /* TODO: Implement according to requirement */
        break;
    case GESTURE_SWIPE_DOWN:
        /* TODO: Implement according to requirement */
        break;
    case GESTURE_PINCH:
        /* TODO: Implement according to requirement */
        break;
    case GESTURE_STRETCH:
        /* TODO: Implement according to requirement */
        break;
    case GESTURE_UNUSED:
    default:
        dev_err(dev, "unexpected gesture type %x\n", gesture_type);
        break;
    }
}

void eph_recv_event_report_contianer(struct eph_data *ephdata, u8 *message)
{

    struct device *dev = &ephdata->commsdevice->dev;
    struct tlv_header tlvheader;
    u16 message_offset = TLV_HEADER_SIZE;
    u8 event_type;
    u8 event_length;

    tlvheader = eph_get_tl_header_info(ephdata, message);
    while (message_offset < tlvheader.length)
    {
        
        /* Get the event report type and length */
        event_type = (message[message_offset] & EVENT_REPORT_TYPE_MASK) >> EVENT_REPORT_TYPE_OFFSET;
        event_length = (message[message_offset] & EVENT_REPORT_LENGTH_MASK);
#if 0
        dev_info(dev,
             "report - offset: %u event_type: %u event_length: %u ",
             message_offset, event_type, event_length);
#endif
        switch (event_type)
        {

            case CONTACT_TYPE:
            case RELEASE_TYPE:
            case HOVER_TYPE:
            case STYLUS_POSITION_TYPE:
            case STYLUS_RELEASE_TYPE:
            {
                eph_recv_touch_report(ephdata, &message[message_offset]);
                break;
            }
            case GESTURE_TYPE:
                eph_gesture_event_process(ephdata, &message[message_offset]);
                break;
            default:
            {
                dev_err(dev, "Unsupported event report %d\n", event_type);
                return;
            }

        }
        message_offset = message_offset + (event_length + 1);

    }
    /* Mark the end of the multi-touch transfer */
    input_sync(ephdata->inputdev);
    
    return;

}

void eph_recv_off_event_report_contianer(struct eph_data *ephdata, u8 *message)
{

    struct device *dev = &ephdata->commsdevice->dev;

    struct tlv_header tlvheader;
    u8 finger_event_type;
    u8 gesture_event_type;

    tlvheader = eph_get_tl_header_info(ephdata, message);

    finger_event_type = message[9];
    gesture_event_type = message[11];

    dev_info(dev,
            "finger_event_type: %u gesture_event_type: %u",
            finger_event_type, gesture_event_type);  

    /*  0x25 reporting both fingerprint and gesture event, only process fingerprint event. 
        gesture event is implemented in 0x23 reporting */
    /* 0x25 Screen off report format
    * Byte  |	Field                       |   Value
    * 0     |   Type                        |   0x25
    * 1     |   Length (LSB)                |
    * 2     |   Length (MSB)                |    9
    * 3     |   CRC_Value                   |   ---
    * 4     |   Finger ID                   |   ---
    * 5     |   Finger Size                 |   ---
    * 6     |   FP Area Status              |   ---
    * 7     |   X Position                  |   ---
    * 8     |   Y Position                  |   ---
    * 9     |   Finger Region Event Type    |	---
    * 10	|   Interrupt Counter           |   ---
    * 11	|   Gesture Event Type          |   ---
    */
    switch (finger_event_type) 
    {
        case FINGER_REGION_FINGER_DOWN:
        {
            /* TODO: Implement finger print down action */
            dev_err(dev, "finger print type %x -- FINGER_REGION_FINGER_DOWN\n", finger_event_type);
            break;
        }
        case FINGER_REGION_FINGER_UP:
        {
            /* TODO: Implement finger print up action */
            dev_err(dev, "finger print type %x -- FINGER_REGION_FINGER_UP\n", finger_event_type);
            break;
        }
        case FINGER_REGION_HOVER_DOWN:
            /* TODO: Implement according to requirement */
            dev_err(dev, "finger print type %x -- FINGER_REGION_HOVER_DOWN\n", finger_event_type);
            break;
        case FINGER_REGION_HOVER_UP:
            /* TODO: Implement according to requirement */
            dev_err(dev, "finger print type %x -- FINGER_REGION_HOVER_UP\n", finger_event_type);
            break;
        case FINGER_REGION_RESERVED:
        default:
            dev_err(dev, "unexpected gesture type %x\n", finger_event_type);
            break;
    }

    input_sync(ephdata->inputdev);

    return;

}

static u16 stored_touches = 0;
static u16 prev_stored_touches = 0;

/* https://www.kernel.org/doc/Documentation/input/multi-touch-protocol.txt */
static void eph_recv_touch_report(struct eph_data *ephdata, u8 *message)
{
    struct device *dev = &ephdata->commsdevice->dev;
    const struct eph_platform_data *ephplatform = ephdata->ephplatform;
    u8 touch_id_slot;
    u8 touch_type;
    u16 position_x;
    u16 position_y;
    u8 width;
    u8 height;
    int touch_pressure = 0;
    int touch_tool_type = 0;
    u8 touch_major_axis = 0;
    u8 touch_minor_axis = 0;
    bool is_active = false;

    prev_stored_touches = stored_touches;
    /* Get the touch report touch type */
    touch_type = (message[0] & EVENT_REPORT_TYPE_MASK) >> EVENT_REPORT_TYPE_OFFSET;
    touch_id_slot = message[1];

    position_x = message[2] | ((u16)message[3] << 8); // little endian 16
    position_y = message[4] | ((u16)message[5] << 8); // little endian 16

    // TODO review the units of this (mm or pixels) and whether they need to switch with touch orientation
    width = message[6];
    height = message[7];

        switch (touch_type)
        {

            case CONTACT_TYPE:
            {
                touch_tool_type = MT_TOOL_FINGER;
                touch_pressure = EPH_MT_PRESSURE_CONTACTING;
                is_active = true;

                touch_major_axis = height;
                touch_minor_axis = width;
                break;
            }

            case RELEASE_TYPE:
            {
                touch_tool_type = MT_TOOL_FINGER;
                touch_pressure = EPH_MT_PRESSURE_HOVER;
                is_active = false;

                touch_major_axis = height;
                touch_minor_axis = width;
                break;
            }

           case HOVER_TYPE:
           {
                touch_tool_type = MT_TOOL_FINGER;
                touch_pressure = EPH_MT_PRESSURE_HOVER;
                is_active = true;
                break;
            }

            case STYLUS_POSITION_TYPE:
            {
                is_active = true;
                touch_tool_type = MT_TOOL_PEN;
                /* For now initialise pressure being set - this would want to change when data supported */
                touch_pressure = EPH_MT_PRESSURE_CONTACTING;

                touch_major_axis = height;
                touch_minor_axis = width;

                break;
            }
            case STYLUS_RELEASE_TYPE:
            {
                touch_tool_type = MT_TOOL_PEN;
                touch_pressure = EPH_MT_PRESSURE_HOVER;
                is_active = false;

                touch_major_axis = height;
                touch_minor_axis = width;
                break;
            }

            default:
            {
                dev_err(dev, "Unexpected touch_type %d\n", touch_type);
                return;
            }
        }

    /* Generate a slot event for updates to this slot */
    input_mt_slot(ephdata->inputdev, touch_id_slot);


    if (RELEASE_TYPE == touch_type)
    {

        /* close out slot */
        /* touch tool type on a release will always show MT_TOOL_FINGER - Optional information - Doesnt matter the touch_tool_type for a release */
        input_mt_report_slot_state(ephdata->inputdev, touch_tool_type, is_active);
        input_report_abs(ephdata->inputdev, ABS_MT_PRESSURE, touch_pressure);
#if 0
        dev_dbg(dev,
                "[%u] %s,  RELEASE_TYPE,  ABS_MT_PRESSURE:%d touch_tool_type:%d is_active:%d BTN_TOUCH:%d\n",
                touch_id_slot,
                get_touch_type_str(touch_type), touch_pressure, touch_tool_type, is_active, 0);
#endif
        stored_touches &= (~(1 << touch_id_slot));
    }
    else
    {
#if 0
        dev_dbg(dev,
                "[%u] %s POSTITION:(%u, %u) ABS_MT_TOUCH_MAJOR:%d ABS_MT_TOUCH_MINOR:%d ABS_MT_PRESSURE:%d touch_tool_type:%d is_active:%d\n",
                touch_id_slot,
                get_touch_type_str(touch_type),
                position_x, position_y, touch_major_axis, touch_minor_axis, touch_pressure, touch_tool_type, is_active);
#endif
        /* Sets ABS_MT_TRACKING_ID if active or -1 if not */
        input_mt_report_slot_state(ephdata->inputdev, touch_tool_type, is_active);

        if (ephplatform->panel_invert_x)
            input_report_abs(ephdata->inputdev, ABS_MT_POSITION_X, (ephplatform->panel_max_x - position_x));
        else
            input_report_abs(ephdata->inputdev, ABS_MT_POSITION_X, (position_x));

        if (ephplatform->panel_invert_y)
            input_report_abs(ephdata->inputdev, ABS_MT_POSITION_Y, (ephplatform->panel_max_y - position_y));
        else
            input_report_abs(ephdata->inputdev, ABS_MT_POSITION_Y, position_y);

        input_report_abs(ephdata->inputdev, ABS_MT_TOUCH_MAJOR, touch_major_axis);
        input_report_abs(ephdata->inputdev, ABS_MT_TOUCH_MINOR, touch_minor_axis);
        input_report_abs(ephdata->inputdev, ABS_MT_PRESSURE, touch_pressure);

        stored_touches |= 1 << touch_id_slot;

    }

    if ((stored_touches) && (!prev_stored_touches))
    {
        /* first touch */
        //TODO android documentation specifieS this should not be needed but host reports do not register otherwise
        input_report_key(ephdata->inputdev, BTN_TOUCH, 1);
    }
    else if ((!stored_touches) && (prev_stored_touches))
    {
        /* last touch */
        input_report_key(ephdata->inputdev, BTN_TOUCH, 0u);
    }
    else
    {
        /* no update required */
    }

    return;

}



void eph_clear_all_host_touch_slots(struct eph_data *ephdata)
{
    int id;

    dev_dbg(&ephdata->commsdevice->dev, "%s >\n", __func__);

    if (!ephdata->inputdev)
    {
        return;
    }

    for (id = 0; id < CONFIG_SUPPORTED_TOUCHES; id++)
    {
        input_mt_slot(ephdata->inputdev, id);
        input_mt_report_slot_state(ephdata->inputdev, 0, false);
    }
    input_report_key(ephdata->inputdev, BTN_TOUCH, 0u);
    input_sync(ephdata->inputdev);
    stored_touches = 0;
    prev_stored_touches = 0;
}


static void eph_recv_device_state_report(struct eph_data *ephdata, u8 *message)
{
    struct device *dev = &ephdata->commsdevice->dev;
    u8 device_status_flags;
    u16 component_id;
    struct tlv_header tlvheader;

    /* Get the device status type */
    /* Should only get here if a valid report was received from the device */
    tlvheader = eph_get_tl_header_info(ephdata, message);
    device_status_flags = message[(TLV_HEADER_SIZE+TLV_DEVICE_STATUS_FLAGS_FIELD)];
    component_id = message[(TLV_HEADER_SIZE+TLV_DEVICE_STATUS_COMPONENT_FIELD)] | ((u16)message[(TLV_HEADER_SIZE+TLV_DEVICE_STATUS_COMPONENT_FIELD+1)] << 8);


    if (0 != (TLV_DEVICE_STATUS_RESET_MASK & device_status_flags))
    {
        complete(&ephdata->reset_completion);
        dev_dbg(dev,
                "Reset completed. STATUS_FLAG:%d COMPONENT_ID:%d\n", device_status_flags, component_id);
    }

    dev_dbg(dev,
            "TYPE:%d LENGTH:%d STATUS_FLAG:%d COMPONENT_ID:%d\n",
            tlvheader.type, tlvheader.length, device_status_flags, component_id);

}




bool eph_proc_report(struct eph_data *ephdata, u8 *message)
{
    u8 type = message[0];
    bool buffer_report = false;

    switch (type)
    {
        case TLV_SCREEN_OFF_REPORT_DATA:
        {
            if (ephdata->inputdev)
            {
                /* only report messages to host if we have registered as an input device */
                eph_recv_off_event_report_contianer(ephdata, message);
                buffer_report = true;
            }
            break;
        }
        case TLV_REPORT_DATA:
        {
            if (ephdata->inputdev)
            {
                /* only report messages to host if we have registered as an input device */
                eph_recv_event_report_contianer(ephdata, message);
                buffer_report = true;
            }
            break;
        }
        case TLV_DEVICE_STATUS_DATA:
        {
            eph_recv_device_state_report(ephdata, message);
            buffer_report = true;
            break;
        }
        case TLV_PACKETISED_DATA:
        case TLV_ENG_DEBUG_DATA:
        {
            buffer_report = true;
            break;
        }
        default:
        {
            /* No processing required for unsupported types. */
            break;
        }
    }

    return buffer_report;
}

int eph_buffer_report(struct eph_data *ephdata, u8 *message)
{
    struct tlv_header tlvheader;
    
    /* Should only get here if a valid report was received from the device */
    tlvheader = eph_get_tl_header_info(ephdata, message);

    if(PAGE_SIZE <= tlvheader.length)
    {
        return -ENOMEM;
    }

    /* Needs around 200us for eswin app to read message */
    udelay(200);
    mutex_lock(&ephdata->sysfs_report_buffer_lock);
    /* copy message into buffer */
    memcpy(&sysfs_report_buf[0],message,tlvheader.length);
    /* Clear report buffer lock as access within the interrupt is complete */
    mutex_unlock(&ephdata->sysfs_report_buffer_lock);

    //dev_dbg(&ephdata->commsdevice->dev, "eswin T:%d len:%d", tlvheader.type, tlvheader.length);


    return 0;
}


int eph_handle_report(struct eph_data *ephdata, u8 *message)
{
    bool buffer_report;
    int ret_val = 0;
    /* Process report , this is always return 0 after modify */
    buffer_report = eph_proc_report(ephdata, message);
    if(buffer_report)
    {
        /* Buffer report */
        ret_val = eph_buffer_report(ephdata, message);
    }
    return ret_val;

}

