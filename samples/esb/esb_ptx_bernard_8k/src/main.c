/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <string.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/drivers/timer/nrf_grtc_timer.h>
#include <zephyr/logging/log.h>
#include <esb.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <dk_buttons_and_leds.h>
#if defined(CONFIG_CLOCK_CONTROL_NRF2)
#include <hal/nrf_lrcconf.h>
#endif
// #include <nrf_erratas.h>
#if NRF54L_ERRATA_20_PRESENT
#include <hal/nrf_power.h>
#endif /* NRF54L_ERRATA_20_PRESENT */
#if defined(NRF54LM20A_ENGA_XXAA)
#include <hal/nrf_clock.h>
#endif /* defined(NRF54LM20A_ENGA_XXAA) */

LOG_MODULE_REGISTER(esb_ptx, CONFIG_ESB_PTX_APP_LOG_LEVEL);


struct main_msg {
	uint32_t type;
	uint32_t cnt;
};

#define MAIN_MSG_TX             0
#define MAIN_MSG_TIMER          1
#define MAIN_MSG_TX_FAIL        2
#define MAIN_MSG_RX             3
#define MAIN_MSG_BTN1		4
#define MAIN_MSG_BTN2		5
#define MAIN_MSG_BTN3		6
#define MAIN_MSG_BTN4		7

#define APP_MOTION_CW		0
#define APP_MOTION_CCW		1
#define APP_ESB_TX		2

struct print_msg {
	uint32_t tx_cnt;
	uint32_t tx_fail_cnt;
	uint32_t rx_cnt;
	int64_t time_cnt;
};


static struct esb_payload rx_payload;
static struct esb_payload tx_payload = ESB_CREATE_PAYLOAD(0, 0x00);

K_MSGQ_DEFINE(main_msgq,
	      sizeof(struct main_msg),
	      8,
	      sizeof(uint32_t));

K_MSGQ_DEFINE(print_msgq,
	      sizeof(struct print_msg),
	      1,
	      sizeof(uint32_t));

static struct k_thread print_thread;

static K_THREAD_STACK_DEFINE(print_stack, 512);

static int32_t timer_chan;
static uint64_t timer_tick;

static int timer_target_set(void);
static void esb_tx_main(void);

struct motion_2d {
	int8_t dx;
	int8_t dy;
};

static const struct motion_2d circular_2d[] = {
	{-25, -2}, {-24, -8}, {-22, -12}, {-20, -15}, {-15, -20}, {-12, -22},
	{-8, -24}, {-2, -25}, {2, -25}, {8, -24}, {12, -22}, {15, -20},
	{20, -15}, {22, -12}, {24, -8}, {25, -2}, {25, 2}, {24, 8}, {22, 12},
	{20, 15}, {15, 20}, {12, 22}, {8, 24}, {2, 25}, {-2, 25}, {-8, 24},
	{-12, 22}, {-15, 20}, {-20, 15}, {-22, 12}, {-24, 8}, {-25, 2}
};

static uint8_t circular_idx;

struct esb_tx_rate {
	uint32_t delay;	/* in microseconds */
	const char *str;
};

static const struct esb_tx_rate tx_rates[] = {
	{0, "unlimited"}, {125, "8kHz"}, {142, "7kHz"}, {166, "6kHz"},
	{200, "5kHz"}, {250, "4kHz"}, {333, "3kHz"}, {500, "2kHz"},
	{1000, "1kHz"}, {2000, "500Hz"}, {5000, "200Hz"}, {10000, "100Hz"}
};

static uint8_t tx_rate_idx;

static atomic_t esb_ptx_delay;

static atomic_t tx_fifo_cnt = ATOMIC_INIT(0);

static atomic_t app_status = ATOMIC_INIT(0);

static void event_handler(struct esb_evt const *event)
{
	struct main_msg msg;
	int err;

	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
		atomic_dec(&tx_fifo_cnt);
		msg.type = MAIN_MSG_TX;
		msg.cnt = 1;
		err = k_msgq_put(&main_msgq, &msg, K_NO_WAIT);
		if (err) {
			LOG_ERR("Cannot put TX done to message queue");
		}
		break;
	case ESB_EVENT_TX_FAILED:
		esb_start_tx();
		msg.type = MAIN_MSG_TX_FAIL;
		msg.cnt = 1;
		err = k_msgq_put(&main_msgq, &msg, K_NO_WAIT);
		if (err) {
			LOG_ERR("Cannot put TX fail to message queue");
		}
		break;
	case ESB_EVENT_RX_RECEIVED:
		msg.type = MAIN_MSG_RX;
		msg.cnt = 0;
		while (esb_read_rx_payload(&rx_payload) == 0) {
			msg.cnt++;
		}
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
	config.retransmit_delay = 450;
	config.retransmit_count = 400;
	config.bitrate = ESB_BITRATE_4MBPS;
	config.event_handler = event_handler;
	config.mode = ESB_MODE_PTX;
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

static void fill_tx_payload_data(struct esb_payload *payload)
{
	payload->data[0] = 0;
	payload->data[1] = circular_2d[circular_idx].dx;
	payload->data[2] = circular_2d[circular_idx].dy;
	payload->data[3] = 0;
	payload->length = 4;
}

static void fill_tx_fifo(bool is_timer)
{
	int err;
	static bool tx_data_set;
	bool is_timed_tx = (atomic_get(&esb_ptx_delay) > 0);

	err = atomic_test_bit(&app_status, APP_ESB_TX) ? 0 : -EACCES;

	if (!err && !(is_timed_tx ^ is_timer)) {
		uint32_t max = is_timed_tx ? 1 : CONFIG_ESB_TX_FIFO_SIZE;
		uint32_t cnt;

		for (cnt = atomic_get(&tx_fifo_cnt); cnt < max; cnt++) {
			if (!tx_data_set) {
				fill_tx_payload_data(&tx_payload);

				if (atomic_test_bit(&app_status, APP_MOTION_CW)) {
					circular_idx = (circular_idx + 1 <
							    ARRAY_SIZE(circular_2d)) ?
							circular_idx + 1 : 0;
				} else {
					circular_idx = (circular_idx > 0) ?
							circular_idx - 1 :
							ARRAY_SIZE(circular_2d) - 1;
				}
			}

			atomic_inc(&tx_fifo_cnt);

			err = esb_write_payload(&tx_payload);
			tx_data_set = (!err) ? false : true;

			if (err) {
				atomic_dec(&tx_fifo_cnt);
				break;
			}
		}
	}
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
	uint32_t delay = atomic_get(&esb_ptx_delay);

	if (delay > 0) {
		timer_tick += delay; // TODO: convert microseconds to timer tick

		err = z_nrf_grtc_timer_set(timer_chan,
					   timer_tick,
					   timer_compare_handler,
					   NULL);
	} else {
		err = -ECANCELED;
	}

	return err;
}

static void print_main(void *p1, void *p2, void *p3)
{
	struct print_msg msg;

	/* Process message queue */
	while (!k_msgq_get(&print_msgq, &msg, K_FOREVER)) {
		LOG_INF("Sent %u packets. Failed %u packets. Received %u packets.",
			msg.tx_cnt, msg.tx_fail_cnt, msg.rx_cnt);
		LOG_INF("Elapsed %lld milliseconds.", msg.time_cnt);
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

static void tx_timer_start(void)
{
	timer_tick = z_nrf_grtc_timer_read();
	timer_target_set();
}

static void tx_timer_stop(void)
{
	z_nrf_grtc_timer_abort(timer_chan);
}

static void esb_tx_start(void)
{
	atomic_set_bit(&app_status, APP_ESB_TX);

	if (atomic_get(&esb_ptx_delay)) {
		tx_timer_start();
	}
}

static void esb_tx_stop(void)
{
	atomic_clear_bit(&app_status, APP_ESB_TX);

	tx_timer_stop();
}

static void update_tx_rate_idx(uint8_t idx, bool timer_change)
{
	tx_rate_idx = idx;

	atomic_set(&esb_ptx_delay, tx_rates[idx].delay);

	if (timer_change && atomic_test_bit(&app_status, APP_ESB_TX)) {
		if (idx) {
			tx_timer_start();
		} else {
			tx_timer_stop();
			fill_tx_fifo(false);
		}
	}
}

static int init_tx_rate_idx(void)
{
	int err;
	const char *str_default = "8kHz";
	uint8_t idx;

	/* find the tx rate with 8kHz string */
	for (idx = 0; idx < ARRAY_SIZE(tx_rates); idx++) {
		if (!strcmp(str_default, tx_rates[idx].str)) {
			break;
		}
	}
	err = (idx < ARRAY_SIZE(tx_rates)) ? 0 : -ENOENT;

	if (!err) {
		update_tx_rate_idx(idx, false);
	}

	if (!err) {
		/* check that the tx rates table can work with the code */
		if (ARRAY_SIZE(tx_rates) < 2) {
			err = -EFAULT;
		}
		for (idx = 0; idx < ARRAY_SIZE(tx_rates); idx++) {
			if ((!idx && tx_rates[idx].delay) ||
			    (idx && !tx_rates[idx].delay)) {
				err = -EFAULT;
			}
		}
	}

	return err;
}

static void print_tx_rate_idx(void)
{
	if (!atomic_test_bit(&app_status, APP_ESB_TX)) {
		LOG_INF("ESB TX rate is %s.", tx_rates[tx_rate_idx].str);
	}
}

static int main_button_handler(uint32_t msg_type)
{
	int err = 0;

	switch (msg_type) {
	case MAIN_MSG_BTN1:
		if (tx_rate_idx + 1 < ARRAY_SIZE(tx_rates)) {
			update_tx_rate_idx(tx_rate_idx + 1, !tx_rate_idx);
		}
		print_tx_rate_idx();
		break;
	case MAIN_MSG_BTN2:
		if (tx_rate_idx > 0) {
			update_tx_rate_idx(tx_rate_idx - 1, (tx_rate_idx == 1));
		}
		print_tx_rate_idx();
		break;
	case MAIN_MSG_BTN3:
		if (!atomic_test_and_clear_bit(&app_status, APP_MOTION_CCW)) {
			atomic_set_bit(&app_status, APP_MOTION_CW);
			esb_tx_start();
		} else {
			esb_tx_stop();
			err = -ECANCELED;
		}
		break;
	case MAIN_MSG_BTN4:
		if (!atomic_test_and_clear_bit(&app_status, APP_MOTION_CW)) {
			atomic_set_bit(&app_status, APP_MOTION_CCW);
			esb_tx_start();
		} else {
			esb_tx_stop();
			err = -ECANCELED;
		}
		break;
	default:
		break;
	}

	return err;
}

int main(void)
{
	int err;

	LOG_INF("Enhanced ShockBurst ptx sample");

	err = clocks_start();
	if (err) {
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

	err = init_tx_rate_idx();
	if (err) {
		LOG_ERR("Default TX rate initialization failed, err %d", err);
		return 0;
	}

	err = dk_buttons_init(button_changed);
	if (err) {
		LOG_ERR("Cannot initialize buttons, err %d", err);
		return 0;
	}

	LOG_INF("Initialization complete");

	tx_payload.noack = false;

	while (1) {
		struct main_msg msg;

		if (!atomic_test_bit(&app_status, APP_ESB_TX)) {
			if (!k_msgq_get(&main_msgq, &msg, K_FOREVER)) {
				switch(msg.type) {
				case MAIN_MSG_BTN1:
				case MAIN_MSG_BTN2:
				case MAIN_MSG_BTN3:
				case MAIN_MSG_BTN4:
					main_button_handler(msg.type);
					break;
				default:
					break;
				}
			} else {
				break;
			}
		} else {
			esb_tx_main();
		}
	}

	LOG_WRN("main loop exited");

	/* return to idle thread */
	return 0;
}

static void esb_tx_main(void)
{
	int err = 0;
	uint32_t tx_event_cnt = 0;
	uint32_t tx_fail_cnt = 0;
	uint32_t rx_fifo_cnt = 0;

	struct main_msg msg;

	while (atomic_test_bit(&app_status, APP_ESB_TX)) {
		int64_t time_cnt;

		time_cnt = k_uptime_get();

		if (!atomic_get(&esb_ptx_delay)) {
			fill_tx_fifo(false);
		}

		do {
			k_msgq_get(&main_msgq, &msg, K_FOREVER);
			do {
				switch(msg.type) {
				case MAIN_MSG_TX_FAIL:
					tx_fail_cnt += msg.cnt;
				case MAIN_MSG_TX:
					tx_event_cnt += msg.cnt;
					fill_tx_fifo(false);
					break;
				case MAIN_MSG_TIMER:
					fill_tx_fifo(true);
					break;
				case MAIN_MSG_RX:
					rx_fifo_cnt += msg.cnt;
					break;
				case MAIN_MSG_BTN1:
				case MAIN_MSG_BTN2:
				case MAIN_MSG_BTN3:
				case MAIN_MSG_BTN4:
					err = main_button_handler(msg.type);
					break;
				default:
					break;
				}
			} while (!k_msgq_get(&main_msgq, &msg, K_NO_WAIT));
		} while (tx_event_cnt < CONFIG_ESB_PTX_BATCH_SIZE && err != -ECANCELED);

		time_cnt = k_uptime_delta(&time_cnt);

		struct print_msg prt_msg;

		prt_msg.tx_cnt = tx_event_cnt;
		tx_event_cnt = 0;
		prt_msg.tx_fail_cnt = tx_fail_cnt;
		tx_fail_cnt = 0;
		prt_msg.rx_cnt = rx_fifo_cnt;
		rx_fifo_cnt = 0;
		prt_msg.time_cnt = time_cnt;

		err = k_msgq_put(&print_msgq, &prt_msg, K_NO_WAIT);
		if (err) {
			LOG_ERR("Cannot put PTX statistics to message queue");
		}
	}
}
