/*
 * Copyright (c) 2018 qianfan Zhao
 * Copyright (c) 2018, 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// #include <sample_usbd.h>

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/util.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_hid.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const uint8_t hid_report_desc_0[] = HID_MOUSE_REPORT_DESC(2);
static const uint8_t hid_report_desc_1[] = HID_KEYBOARD_REPORT_DESC();
static const uint8_t hid_report_desc_2[] = HID_KEYBOARD_REPORT_DESC();

typedef struct _usb_device_desc_t {
    uint8_t		*p_desc;
	uint16_t    size;
} usb_device_desc_t;

struct usb_device_t {
	const struct device *dev;
	bool enabled;
};

static const usb_device_desc_t hid_desc_report[] = {
    {(uint8_t *)hid_report_desc_0, sizeof(hid_report_desc_0)},	// mouse desc
	{(uint8_t *)hid_report_desc_1, sizeof(hid_report_desc_1)},	
	{(uint8_t *)hid_report_desc_2, sizeof(hid_report_desc_2)},	
};
uint8_t report_test[4] = { 0x00 };

#define USB_NEXT_USB_HID_DEVICE_INIT(node_id)	\
{												\
	.dev = DEVICE_DT_GET_OR_NULL(node_id),		\
	.enabled = true,							\
},

static struct usb_device_t usb_hid_device[] = {
	DT_FOREACH_STATUS_OKAY(zephyr_hid_device, USB_NEXT_USB_HID_DEVICE_INIT)
};

#define MOUSE_BTN_LEFT		0
#define MOUSE_BTN_RIGHT		1

enum mouse_report_idx {
	MOUSE_BTN_REPORT_IDX = 0,
	MOUSE_X_REPORT_IDX = 1,
	MOUSE_Y_REPORT_IDX = 2,
	MOUSE_WHEEL_REPORT_IDX = 3,
	MOUSE_REPORT_COUNT = 4,
};

K_MSGQ_DEFINE(mouse_msgq, MOUSE_REPORT_COUNT, 2, 1);
static K_SEM_DEFINE(ep_write_sem, 0, 1);
static bool usb_enabled;

//const struct device *hid_dev;
struct usbd_context *sample_usbd;
static uint32_t idle_duration;

/** @brief Peer states. */
enum usb_state {
	/** Cable is not attached. */
	USB_STATE_DISCONNECTED,
	/** Cable attached but no communication. */
	USB_STATE_POWERED,
	/** Cable attached and data is exchanged. */
	USB_STATE_ACTIVE,
	/** Cable attached but port is suspended. */
	USB_STATE_SUSPENDED,

	/** Number of states. */
	USB_STATE_COUNT
};
static enum usb_state state;

static ALWAYS_INLINE void rwup_if_suspended(void)
{
	if (IS_ENABLED(CONFIG_USB_DEVICE_REMOTE_WAKEUP)) {
		if (state == USB_STATE_SUSPENDED) {
			usb_wakeup_request();
			return;
		}
	}
}

static void input_cb(struct input_event *evt, void *user_data)
{
	static uint8_t tmp[MOUSE_REPORT_COUNT];

	ARG_UNUSED(user_data);

	switch (evt->code) {
	case INPUT_KEY_0:
		rwup_if_suspended();
		WRITE_BIT(tmp[MOUSE_BTN_REPORT_IDX], MOUSE_BTN_LEFT, evt->value);
		break;
	case INPUT_KEY_1:
		rwup_if_suspended();
		WRITE_BIT(tmp[MOUSE_BTN_REPORT_IDX], MOUSE_BTN_RIGHT, evt->value);
		break;
	case INPUT_KEY_2:
		if (evt->value) {
			tmp[MOUSE_X_REPORT_IDX] += 10U;
		}

		break;
	case INPUT_KEY_3:
		if (evt->value) {
			tmp[MOUSE_Y_REPORT_IDX] += 10U;
		}

		break;
	default:
		LOG_INF("Unrecognized input code %u value %d",
			evt->code, evt->value);
		return;
	}

	if (k_msgq_put(&mouse_msgq, tmp, K_NO_WAIT) != 0) {
		LOG_ERR("Failed to put new input event");
}

	tmp[MOUSE_X_REPORT_IDX] = 0U;
	tmp[MOUSE_Y_REPORT_IDX] = 0U;

}

INPUT_CALLBACK_DEFINE(NULL, input_cb, NULL);
static void iface_ready_next(const struct device *dev, const bool ready)
{
	LOG_INF("iface_ready_next");
}

static int get_report_next(const struct device *dev, const uint8_t type, const uint8_t id,
			   const uint16_t len, uint8_t *const buf)
{
	/* Omit the first byte - HID report ID. */
	buf[0] = id;
	memset(buf,0x12,len);
	LOG_INF("get_report : type = %d, id = %d, len = %d",type,id,len);

	for (uint16_t i = 0; i < len; i++) {
        LOG_INF("%02x ", buf[i]);
    }


	//return err ? err : len;
	return len;
}

static int set_report_next(const struct device *dev, const uint8_t type, const uint8_t id,
			   const uint16_t len, const uint8_t *const buf)
{
	LOG_INF("set_report : type = %d, id = %d, len = %d",type,id,len);
	
	for (uint16_t i = 0; i < len; i++) {
        LOG_INF("%02x ", buf[i]);
    }
	/* Omit the first byte - HID report ID. */
	//return set_report(dev, type, id, buf + 1, len - 1);
	return 0;
}


static void report_sent_cb_next(const struct device *dev)
{
	ARG_UNUSED(dev);
	k_sem_give(&ep_write_sem);
	LOG_INF("report_sent_cb_next");

}

static void protocol_change(const struct device *dev, uint8_t protocol)
{
	LOG_INF("Protocol changed to %s",
		protocol == 0U ? "Boot Protocol" : "Report Protocol");
}

static void sof_next(const struct device *dev)
{
//	LOG_INF("SOF");
}	
static void set_idle_next(const struct device *dev, const uint8_t id, const uint32_t duration)
{
	idle_duration = duration;
}

static uint32_t get_idle_next(const struct device *dev, const uint8_t id)
{
	return idle_duration;
}
struct hid_device_ops hid_ops[] = {
	{
		.iface_ready = iface_ready_next,
		.get_report = get_report_next,
		.set_report = set_report_next,
		.set_protocol = protocol_change,
		.set_idle = set_idle_next,
		.get_idle = get_idle_next,
		.input_report_done = report_sent_cb_next,
		.sof = sof_next,
	},
	{
		.get_report = get_report_next,
		.set_report = set_report_next,
		.set_protocol = protocol_change,
		.set_idle = set_idle_next,
		.get_idle = get_idle_next,
		.input_report_done = report_sent_cb_next,
	},
	{
		.get_report = get_report_next,
		.set_report = set_report_next,
		.set_protocol = protocol_change,
		.set_idle = set_idle_next,
		.get_idle = get_idle_next,
		.input_report_done = report_sent_cb_next,
	},
};
static void usb_init_next_status_cb(struct usbd_context *const usbd,
				    const struct usbd_msg *const msg)
{
	LOG_DBG("USBD msg: %s", usbd_msg_type_string(msg->type));

	__ASSERT_NO_MSG(usbd == sample_usbd);
	
	if (msg->type == USBD_MSG_CONFIGURATION) {
		LOG_INF("\tConfiguration value %d", msg->status);
	}

	if (usbd_can_detect_vbus(usbd)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			if (usbd_enable(usbd)) {
				LOG_ERR("Failed to enable device support");
			}
		}

		if (msg->type == USBD_MSG_VBUS_REMOVED) {
			if (usbd_disable(usbd)) {
				LOG_ERR("Failed to disable device support");
			}
		}
	}

}

static int usb_init_next(void)
{
	int ret;

	for (uint8_t i = 0; i < ARRAY_SIZE(usb_hid_device); i++) {
		if (usb_hid_device[i].dev == NULL) {
			LOG_ERR("Cannot get USB HID Device");
			return -ENXIO;
		}
		ret = hid_device_register(usb_hid_device[i].dev,
					hid_desc_report[i].p_desc, hid_desc_report[i].size,
					&hid_ops[i]);
		if (ret != 0) {
			LOG_ERR("Failed to register HID Device, %d", ret);
			return ret;
		}
	}

	sample_usbd = sample_usbd_init_device(usb_init_next_status_cb);
	if (sample_usbd == NULL) {
		LOG_ERR("Failed to initialize USB device");
		return -ENODEV;
	}

	if (!usbd_can_detect_vbus(sample_usbd)) {
		/* doc device enable start */
		ret = usbd_enable(sample_usbd);
		if (ret) {
			LOG_ERR("Failed to enable device support");
			return ret;
		}
		/* doc device enable end */
	}
	return 0;
}


int main(void)
{
	int ret;

	if (!gpio_is_ready_dt(&led0)) {
		LOG_ERR("LED device %s is not ready", led0.port->name);
		return 0;
	}
	ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure the LED pin, error: %d", ret);
		return 0;
	}

    usb_init_next();

	LOG_INF("HID USBD mouse sample is initialized...");
	while (true) {
		UDC_STATIC_BUF_DEFINE(report, MOUSE_REPORT_COUNT);

		k_msgq_get(&mouse_msgq, &report, K_FOREVER);
		ret = hid_device_submit_report(usb_hid_device[0].dev, MOUSE_REPORT_COUNT, report);

	if (ret) {
		LOG_ERR("HID write error, %d", ret);
		} else {
			k_sem_take(&ep_write_sem, K_FOREVER);
			/* Toggle LED on sent report */
			(void)gpio_pin_toggle(led0.port, led0.pin);
	}
	}
	return 0;
}
