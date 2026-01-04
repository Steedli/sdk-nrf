/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/drivers/timer/nrf_grtc_timer.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>
#include <esb.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <dk_buttons_and_leds.h>
#if defined(CONFIG_CLOCK_CONTROL_NRF2)
#include <hal/nrf_lrcconf.h>
#endif
#include <nrf_erratas.h>
#if NRF54L_ERRATA_20_PRESENT
#include <hal/nrf_power.h>
#endif /* NRF54L_ERRATA_20_PRESENT */
#if defined(NRF54LM20A_ENGA_XXAA)
#include <hal/nrf_clock.h>
#endif /* defined(NRF54LM20A_ENGA_XXAA) */
#include "app_usbd.h"

LOG_MODULE_REGISTER(esb_prx, CONFIG_ESB_PRX_APP_LOG_LEVEL);


/* Print cycle time, in microseconds */
#define PRINT_CYCLE     1000000

#define APP_TX_PRG			0
#define APP_USB_FWD			1

struct main_msg {
	uint32_t type;
};

#define MAIN_MSG_RX     		0
#define MAIN_MSG_TX     		1
#define MAIN_MSG_USBD_IN_REP_DONE	2
#define MAIN_MSG_TIMER  		3
#define MAIN_MSG_BTN1			4
#define MAIN_MSG_BTN2			5
#define MAIN_MSG_BTN3			6
#define MAIN_MSG_BTN4			7

struct print_msg {
	uint32_t type;
	uint32_t cnt;
};

#define PRINT_MSG_USB_DONE     		0
#define PRINT_MSG_ESB_RX     		1

static uint32_t rx_cnt;
static uint32_t rx_pos_in;
static uint32_t rx_pos_out;
static struct esb_payload rx_payload[CONFIG_ESB_RX_FIFO_SIZE];
#if CONFIG_ESB_PRX_TX_LENGTH > 0
static struct esb_payload tx_payload = ESB_CREATE_PAYLOAD(0, 0x00);

static void fill_tx_payload_data(struct esb_payload *payload)
{
	static uint8_t tx_data_next;
	uint8_t i;

	for (i = 0; i < CONFIG_ESB_PRX_TX_LENGTH; i++) {
		payload->data[i] = tx_data_next + i;
	}

	tx_data_next += CONFIG_ESB_PRX_TX_LENGTH;
}

static void fill_tx_fifo(void)
{
	int err;

	while (1) {
		err = esb_write_payload(&tx_payload);
		if (err) {
			break;
		}
	}
}
#endif

UDC_STATIC_BUF_DEFINE(app_report, APP_USBD_DATA_SIZE);

static atomic_t app_status = ATOMIC_INIT(BIT(APP_USB_FWD));

typedef void (*app_esb_rx_handler_t)(void);

static void app_forward_rx_payload_to_usb(void);

static app_esb_rx_handler_t app_rx_payload_proc = app_forward_rx_payload_to_usb;

K_MSGQ_DEFINE(main_msgq,
	      sizeof(struct main_msg),
	      20,
	      sizeof(uint32_t));

K_MSGQ_DEFINE(print_msgq,
	      sizeof(struct print_msg),
	      2,
	      sizeof(uint32_t));

static struct k_thread print_thread;

static K_THREAD_STACK_DEFINE(print_stack, 512);

static int32_t timer_chan;
static uint64_t timer_tick;

static int timer_target_set(void);

static uint32_t fill_rx_payload(void)
{
	uint32_t cnt = 0;

	while (rx_cnt < ARRAY_SIZE(rx_payload)) {
		if (!esb_read_rx_payload(&rx_payload[rx_pos_in])) {
			rx_cnt++;
			rx_pos_in = (rx_pos_in + 1 < ARRAY_SIZE(rx_payload)) ?
				    rx_pos_in + 1 : 0;
			cnt++;
		} else {
			break;
		}
	}

	return cnt;
}

static void event_handler(struct esb_evt const *event)
{
	struct main_msg msg;
	int err;

	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
		msg.type = MAIN_MSG_TX;
		err = k_msgq_put(&main_msgq, &msg, K_NO_WAIT);
		if (err) {
			LOG_ERR("Cannot put TX count to message queue");
		}
		break;
	case ESB_EVENT_RX_RECEIVED:
		msg.type = MAIN_MSG_RX;
		err = k_msgq_put(&main_msgq, &msg, K_NO_WAIT);
		if (err) {
			LOG_ERR("Cannot put RX count to message queue");
		}
		break;
	default:
		break;
	}
}

#if defined(CONFIG_CLOCK_CONTROL_NRF)
static int clocks_start(void)
{
	int err;
	int res;
	struct onoff_manager *clk_mgr;
	struct onoff_client clk_cli;

	clk_mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	if (!clk_mgr) {
		LOG_ERR("Unable to get the Clock manager");
		return -ENXIO;
	}

	sys_notify_init_spinwait(&clk_cli.notify);

	err = onoff_request(clk_mgr, &clk_cli);
	if (err < 0) {
		LOG_ERR("Clock request failed: %d", err);
		return err;
	}

	do {
		err = sys_notify_fetch_result(&clk_cli.notify, &res);
		if (!err && res) {
			LOG_ERR("Clock could not be started: %d", res);
			return res;
		}
	} while (err);

#if NRF54L_ERRATA_20_PRESENT
	if (nrf54l_errata_20()) {
		nrf_power_task_trigger(NRF_POWER, NRF_POWER_TASK_CONSTLAT);
	}
#endif /* NRF54L_ERRATA_20_PRESENT */

#if defined(NRF54LM20A_ENGA_XXAA)
	/* MLTPAN-39 */
	nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_PLLSTART);
#endif

	LOG_DBG("HF clock started");
	return 0;
}

#elif defined(CONFIG_CLOCK_CONTROL_NRF2)

int clocks_start(void)
{
	int err;
	int res;
	const struct device *radio_clk_dev =
		DEVICE_DT_GET_OR_NULL(DT_CLOCKS_CTLR(DT_NODELABEL(radio)));
	struct onoff_client radio_cli;

	/** Keep radio domain powered all the time to reduce latency. */
	nrf_lrcconf_poweron_force_set(NRF_LRCCONF010, NRF_LRCCONF_POWER_DOMAIN_1, true);

	sys_notify_init_spinwait(&radio_cli.notify);

	err = nrf_clock_control_request(radio_clk_dev, NULL, &radio_cli);

	do {
		err = sys_notify_fetch_result(&radio_cli.notify, &res);
		if (!err && res) {
			LOG_ERR("Clock could not be started: %d", res);
			return res;
		}
	} while (err == -EAGAIN);

	nrf_lrcconf_clock_always_run_force_set(NRF_LRCCONF000, 0, true);
	nrf_lrcconf_task_trigger(NRF_LRCCONF000, NRF_LRCCONF_TASK_CLKSTART_0);

	LOG_DBG("HF clock started");

	return 0;
}

#else
BUILD_ASSERT(false, "No Clock Control driver");
#endif /* defined(CONFIG_CLOCK_CONTROL_NRF2) */

static int esb_initialize(void)
{
	int err;
	/* These are arbitrary default addresses. In end user products
	 * different addresses should be used for each set of devices.
	 */
	uint8_t base_addr_0[4] = {0xE7, 0xE7, 0xE7, 0xE7};
	uint8_t base_addr_1[4] = {0xC2, 0xC2, 0xC2, 0xC2};
	uint8_t addr_prefix[8] = {0xE7, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8};

	struct esb_config config = ESB_DEFAULT_CONFIG;

	config.protocol = ESB_PROTOCOL_ESB_DPL;
	config.bitrate = ESB_BITRATE_4MBPS;
	config.mode = ESB_MODE_PRX;
	config.event_handler = event_handler;
	config.selective_auto_ack = true;
	if (IS_ENABLED(CONFIG_ESB_FAST_SWITCHING)) {
		config.use_fast_ramp_up = true;
	}

	err = esb_init(&config);
	if (err) {
		return err;
	}

	err = esb_set_base_address_0(base_addr_0);
	if (err) {
		return err;
	}

	err = esb_set_base_address_1(base_addr_1);
	if (err) {
		return err;
	}

	err = esb_set_prefixes(addr_prefix, ARRAY_SIZE(addr_prefix));
	if (err) {
		return err;
	}

	err = esb_set_rf_channel(40);
	if (err) {
		return err;
	}

	return 0;
}

static void timer_compare_handler(int32_t chan_id,
				  uint64_t expire_time,
				  void *user_data)
{
	timer_target_set();

	struct main_msg msg;
	int err;

	msg.type = MAIN_MSG_TIMER;
	err = k_msgq_put(&main_msgq, &msg, K_NO_WAIT);
	if (err) {
		LOG_ERR("Cannot put TIMER count to message queue");
	}
}

static int timer_target_set(void)
{
	int err;

	timer_tick += PRINT_CYCLE; // TODO: convert microseconds to timer tick

	err = z_nrf_grtc_timer_set(timer_chan,
				   timer_tick,
				   timer_compare_handler,
				   NULL);

	return err;
}

static void print_main(void *p1, void *p2, void *p3)
{
	struct print_msg msg;

	/* Process message queue */
	while (!k_msgq_get(&print_msgq, &msg, K_FOREVER)) {
		switch (msg.type) {
		case PRINT_MSG_USB_DONE:
			LOG_INF("# USB report = %u.", msg.cnt);
			break;
		case PRINT_MSG_ESB_RX:
			LOG_INF("Received %u packets.", msg.cnt);
			break;
		default:
			LOG_INF("Unknown count: %u.", msg.cnt);
			break;
		}
	}
}

static void usbd_in_report_done_cb(void)
{
	int err;
	struct main_msg msg;

	atomic_clear_bit(&app_status, APP_TX_PRG);

	msg.type = MAIN_MSG_USBD_IN_REP_DONE;

	err = k_msgq_put(&main_msgq, &msg, K_NO_WAIT);
	if (err) {
		LOG_WRN("Cannot put USB device event to message queue: %d", err);
	}
}

static void app_forward_rx_payload_to_usb(void)
{
	if (!atomic_test_and_set_bit(&app_status, APP_TX_PRG)) {
		int err = -ENODATA;

		while (rx_cnt) {
			bool rcv_done;
			bool loop_break;

			struct esb_payload *rcv = &rx_payload[rx_pos_out];

			if (rcv->length == APP_USBD_DATA_SIZE) {
				memcpy(app_report, rcv->data, APP_USBD_DATA_SIZE);
				/* USB connection is assumed */
				err = app_usbd_submit_report(app_report, APP_USBD_DATA_SIZE);
				rcv_done = !err;
				loop_break = true;
			} else {
				err = -EMSGSIZE;
				rcv_done = true;
				loop_break = false;
			}

			if (rcv_done) {
				rx_cnt--;
				rx_pos_out = (rx_pos_out + 1 < ARRAY_SIZE(rx_payload)) ?
					      rx_pos_out + 1 : 0;
			}

			if (loop_break) {
				break;
			}
		}

		if (err) {
			atomic_clear_bit(&app_status, APP_TX_PRG);
		}
	}
}

static void app_skip_rx_payload(void)
{
	while (rx_cnt) {
		rx_cnt--;
		rx_pos_out = (rx_pos_out + 1 < ARRAY_SIZE(rx_payload)) ?
			      rx_pos_out + 1 : 0;
	}
}

static void button_changed(uint32_t button_state, uint32_t has_changed)
{
	uint32_t buttons = button_state & has_changed;
	int err = 0;
	struct main_msg msg;
	bool msgq_put = false;

	if (buttons & DK_BTN1_MSK) {
		msg.type = MAIN_MSG_BTN1;
		msgq_put = true;
	}

	if (buttons & DK_BTN2_MSK) {
		msg.type = MAIN_MSG_BTN2;
		msgq_put = true;
	}

	if (buttons & DK_BTN3_MSK) {
		msg.type = MAIN_MSG_BTN3;
		msgq_put = true;
	}

	if (buttons & DK_BTN4_MSK) {
		msg.type = MAIN_MSG_BTN4;
		msgq_put = true;
	}

	if (msgq_put) {
		err = k_msgq_put(&main_msgq, &msg, K_NO_WAIT);
		if (err) {
			LOG_WRN("Cannot put button event to message queue: %d", err);
		}
	}
}

static int main_button_handler(uint32_t msg_type)
{
	int err;

	switch (msg_type) {
	case MAIN_MSG_BTN1:
		/* toggle USB HID reporting */
		atomic_xor(&app_status, BIT(APP_USB_FWD));
		app_rx_payload_proc = atomic_test_bit(&app_status, APP_USB_FWD) ?
					app_forward_rx_payload_to_usb :
					app_skip_rx_payload;
		err = 0;
		break;
	case MAIN_MSG_BTN2:
	case MAIN_MSG_BTN3:
	case MAIN_MSG_BTN4:
	default:
		err = -ESRCH;
		break;
	}

	return err;
}

int main(void)
{
	int err;

	LOG_INF("Enhanced ShockBurst prx sample");

	err = clocks_start();
	if (err) {
		return 0;
	}

	err = app_usbd_init(usbd_in_report_done_cb);
	if (err) {
		LOG_ERR("Failed to initialize USB device, err %d", err);
		return 0;
	}

	err = esb_initialize();
	if (err) {
		LOG_ERR("ESB initialization failed, err %d", err);
		return 0;
	}

	k_thread_create(&print_thread,
			print_stack,
			K_THREAD_STACK_SIZEOF(print_stack),
			print_main,
			NULL,
			NULL,
			NULL,
			K_LOWEST_APPLICATION_THREAD_PRIO,
			0, 
			K_NO_WAIT);

	timer_chan = z_nrf_grtc_timer_chan_alloc();
	if (timer_chan < 0) {
		LOG_ERR("Cannot get a timer channel");
		return 0;
	}

	err = dk_buttons_init(button_changed);
	if (err) {
		LOG_ERR("Cannot initialize buttons, err %d", err);
		return 0;
	}

	LOG_INF("Initialization complete");

	LOG_INF("Print packet counts every %u.%02u seconds",
		PRINT_CYCLE / USEC_PER_SEC,
		(PRINT_CYCLE % USEC_PER_SEC) * 100 / USEC_PER_SEC);

	timer_tick = z_nrf_grtc_timer_read();
	timer_target_set();

	err = app_usbd_enable();
	if (err) {
		LOG_ERR("Failed to enable USB device, err %d", err);
		return 0;
	}

	LOG_INF("ACK payload length = %u bytes", CONFIG_ESB_PRX_TX_LENGTH);

#if CONFIG_ESB_PRX_TX_LENGTH > 0
	tx_payload.length = CONFIG_ESB_PRX_TX_LENGTH;
	fill_tx_payload_data(&tx_payload);

	fill_tx_fifo();
#endif

	LOG_INF("Setting up for packet receiption");

	err = esb_start_rx();
	if (err) {
		LOG_ERR("RX setup failed, err %d", err);
		return 0;
	}

	uint32_t usb_done_cnt = 0;
	uint32_t esb_rx_cnt = 0;

	struct main_msg msg;
	struct print_msg prt_msg;

	/* Process message queue */
	while (!k_msgq_get(&main_msgq, &msg, K_FOREVER)) {
		switch(msg.type) {
		case MAIN_MSG_RX:
			esb_rx_cnt += fill_rx_payload();
			app_rx_payload_proc();
			break;
		case MAIN_MSG_TX:
#if CONFIG_ESB_PRX_TX_LENGTH > 0
			fill_tx_fifo();
#endif
			break;
		case MAIN_MSG_USBD_IN_REP_DONE:
			usb_done_cnt++;
			app_rx_payload_proc();
			/*
			   When PTX sends radio packets faster than 8kHz, PRX
			   will have ESB RX FIFO full at times. PRX will not
			   return ACK packets by then, and ESB library will not
			   send RX_RECEIVED event. To tackle this condition, we
			   read ESB RX FIFO after HID report is submitted. It
			   will give room to the ESB library storing received
			   radio packets.
			 */
			esb_rx_cnt += fill_rx_payload();
			break;
		case MAIN_MSG_TIMER:
			if (atomic_test_bit(&app_status, APP_USB_FWD)) {
				prt_msg.type = PRINT_MSG_USB_DONE;
				prt_msg.cnt = usb_done_cnt;
			} else {
				prt_msg.type = PRINT_MSG_ESB_RX;
				prt_msg.cnt = esb_rx_cnt;
			}
			usb_done_cnt = 0;
			esb_rx_cnt = 0;
			err = k_msgq_put(&print_msgq, &prt_msg, K_NO_WAIT);
			if (err) {
				LOG_ERR("Cannot put USB statistics to message queue");
			}
			break;
		case MAIN_MSG_BTN1:
		case MAIN_MSG_BTN2:
		case MAIN_MSG_BTN3:
		case MAIN_MSG_BTN4:
			err = main_button_handler(msg.type);
			/* When USB connection is down, ESB RX FIFO may become full.
			   The sample is stuck when it does not get any messages
			   from USB and ESB. Insert a hack here to get things going.
			   No worries. If things don't work, the user will reset the
			   board.
			 */
			if (!err) {
				esb_rx_cnt += fill_rx_payload();
				app_rx_payload_proc();
			}
			break;
		default:
			break;
		}
	}

	LOG_WRN("main loop exited");

	/* return to idle thread */
	return 0;
}
