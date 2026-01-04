/*
 * Copyright (c) 2025 Nordic Semiconductor
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_hid.h>
#include <sample_usbd.h>
#include "app_usbd.h"


static int get_report_cb(const struct device *dev,
			 const uint8_t type, const uint8_t id, const uint16_t len,
			 uint8_t *const buf);
static void input_report_done_cb(const struct device *dev, const uint8_t *const report);
static void output_report_cb(const struct device *dev,
			     const uint16_t len,
			     const uint8_t *const buf);


static const uint8_t hid_report_desc[] = HID_MOUSE_REPORT_DESC(2);

static app_usbd_in_report_done_cb usr_in_report_done_cb;


static int set_report_cb(const struct device *dev,
			 const uint8_t type, const uint8_t id, const uint16_t len,
			 const uint8_t *const buf)
{
	if (type != HID_REPORT_TYPE_OUTPUT) {
		return -ENOTSUP;
	}

	return 0;
}

static void iface_ready_cb(const struct device *dev, const bool ready)
{
}

static uint32_t idle_duration;

static void set_idle_cb(const struct device *dev,
			const uint8_t id, const uint32_t duration)
{
	idle_duration = duration;
}

static uint32_t get_idle_cb(const struct device *dev, const uint8_t id)
{
	return idle_duration;
}

static uint32_t idle_duration;

static const struct hid_device_ops hid_ops = {
	.iface_ready = iface_ready_cb,
	.get_report = get_report_cb,
	.set_report = set_report_cb,
	.input_report_done = input_report_done_cb,
	.output_report = output_report_cb,
	.set_idle = set_idle_cb,
	.get_idle = get_idle_cb
};

static struct usbd_context *sample_usbd;

static const struct device *hid_device = DEVICE_DT_GET_ONE(zephyr_hid_device);


static void output_report_cb(const struct device *dev,
			     const uint16_t len,
			     const uint8_t *const buf)
{
}

static void msg_cb(struct usbd_context *const usbd_ctx,
		   const struct usbd_msg *const msg)
{
	if (usbd_can_detect_vbus(usbd_ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			usbd_enable(usbd_ctx);
		} else if (msg->type == USBD_MSG_VBUS_REMOVED) {
			usbd_disable(usbd_ctx);
		}
	}
}

int app_usbd_init(app_usbd_in_report_done_cb cb)
{
	int err;

	err = hid_device_register(hid_device,
				  hid_report_desc,
				  sizeof(hid_report_desc),
				  &hid_ops);

	if (!err) {
		sample_usbd = sample_usbd_init_device(msg_cb);
		err = (sample_usbd) ? 0 : -ENODEV;
	}

	if (!err) {
		usr_in_report_done_cb = cb;
	}

	return err;
}

int app_usbd_enable(void)
{
	int err;

	if (!usbd_can_detect_vbus(sample_usbd)) {
		err = usbd_enable(sample_usbd);
	} else {
		err = 0;
	}

	return err;
}

int app_usbd_submit_report(const uint8_t *report, uint16_t size)
{
	int err;

	if (unlikely(!report)) {
		err = -EINVAL;
	} else if (size != APP_USBD_DATA_SIZE) {
		err = -ENOTSUP;
	} else {
		err = 0;
	}

	if (!err) {
		err = hid_device_submit_report(hid_device,
					       size,
					       report);
	}

	return err;
}

static int get_report_cb(const struct device *dev,
			 const uint8_t type, const uint8_t id, const uint16_t len,
			 uint8_t *const buf)
{
	return 0;
}

static void input_report_done_cb(const struct device *dev,
				 const uint8_t *const report)
{
	if (usr_in_report_done_cb) {
		usr_in_report_done_cb();
	}
}
