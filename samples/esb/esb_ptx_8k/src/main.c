/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>
#include <nrf.h>
// #include <nrfx.h>
#include <esb.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
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

LOG_MODULE_REGISTER(esb_ptx, CONFIG_ESB_PTX_APP_LOG_LEVEL);

static bool ready = true;
static struct esb_payload rx_payload;
static struct esb_payload tx_payload = ESB_CREATE_PAYLOAD(0,
	0x01, 0x00, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08);

#define _RADIO_SHORTS_COMMON                                                   \
	(RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk |         \
	 RADIO_SHORTS_ADDRESS_RSSISTART_Msk |                                  \
	 RADIO_SHORTS_DISABLED_RSSISTOP_Msk)

void event_handler(struct esb_evt const *event)
{
	ready = true;

	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
		// LOG_DBG("TX SUCCESS EVENT");
		break;
	case ESB_EVENT_TX_FAILED:
		// LOG_DBG("TX FAILED EVENT");
		break;
	case ESB_EVENT_RX_RECEIVED:
		while (esb_read_rx_payload(&rx_payload) == 0) {
			LOG_DBG("Packet received, len %d : "
				"0x%02x, 0x%02x, 0x%02x, 0x%02x, "
				"0x%02x, 0x%02x, 0x%02x, 0x%02x",
				rx_payload.length, rx_payload.data[0],
				rx_payload.data[1], rx_payload.data[2],
				rx_payload.data[3], rx_payload.data[4],
				rx_payload.data[5], rx_payload.data[6],
				rx_payload.data[7]);
		}
		break;
	}
}

#if defined(CONFIG_CLOCK_CONTROL_NRF)
int clocks_start(void)
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

int esb_initialize(void)
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
	config.retransmit_delay = 438;
	config.retransmit_count = 0;
	config.bitrate = ESB_BITRATE_4MBPS;
	config.event_handler = event_handler;
	config.mode = ESB_MODE_PTX;
	config.selective_auto_ack = true;
	config.crc = ESB_CRC_8BIT;

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

	esb_set_tx_power(0);

	return 0;
}

static void leds_update(uint8_t value)
{
	uint32_t leds_mask =
		(!(value % 8 > 0 && value % 8 <= 4) ? DK_LED1_MSK : 0) |
		(!(value % 8 > 1 && value % 8 <= 5) ? DK_LED2_MSK : 0) |
		(!(value % 8 > 2 && value % 8 <= 6) ? DK_LED3_MSK : 0) |
		(!(value % 8 > 3) ? DK_LED4_MSK : 0);

	dk_set_leds(leds_mask);
}

#define APP_TIMER00           NRF_TIMER00//NRF_TIMER00_S
#define APP_TIMER20           NRF_TIMER20
#define APP_TIMER21           NRF_TIMER21
#define TIME_TO_WAIT_1MS      1
#define TIME_TO_WAIT_125US    125

#include <nrfx_timer.h>
#include <hal/nrf_timer.h>
#include <hal/nrf_dppi.h>
#include <hal/nrf_ppib.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_gpiote.h>
#define DEBUG_IO_ENABLE 				    1	
#if(DEBUG_IO_ENABLE)
#define G24_RF_IO_ENABLE                    1//需要关闭，调试2.4G rf时序的

#define DEBUG_IO1_PORT 						1							
#define DEBUG_IO1_PIN						10
#define DEBUG_IO1_PORT_PIN					((DEBUG_IO1_PORT<<5)|DEBUG_IO1_PIN)

#define DEBUG_IO2_PORT 						1						
#define DEBUG_IO2_PIN						11
#define DEBUG_IO2_PORT_PIN					((DEBUG_IO2_PORT<<5)|DEBUG_IO2_PIN)

#define DEBUG_IO3_PORT 						1						
#define DEBUG_IO3_PIN						12
#define DEBUG_IO3_PORT_PIN					((DEBUG_IO3_PORT<<5)|DEBUG_IO3_PIN)

#define DEBUG_IO4_PORT 						1						
#define DEBUG_IO4_PIN						13
#define DEBUG_IO4_PORT_PIN					((DEBUG_IO4_PORT<<5)|DEBUG_IO4_PIN)

#define DEBUG_IO5_PORT 						1						
#define DEBUG_IO5_PIN						14
#define DEBUG_IO5_PORT_PIN					((DEBUG_IO5_PORT<<5)|DEBUG_IO5_PIN)
#else
#define G24_RF_IO_ENABLE                    0//DEBUG_IO_ENABLE没有使能，不能使用
#endif

__ramfunc 
void timer20_irq_handler(void)
{
	// if (nrf_timer_int_enable_check(APP_TIMER20, NRF_TIMER_INT_COMPARE0_MASK)) 
	if(APP_TIMER20->INTENSET & NRF_TIMER_INT_COMPARE0_MASK)
    {
		nrf_timer_event_clear(APP_TIMER20, NRF_TIMER_EVENT_COMPARE0);
        // nrf_timer_task_trigger(APP_TIMER20, NRF_TIMER_TASK_CLEAR);//shortcut自动清掉了
		nrf_gpio_pin_toggle(DEBUG_IO4_PORT_PIN);
		if (ready) {
		nrf_gpio_pin_toggle(DEBUG_IO5_PORT_PIN);			
			ready = false;

// 			nrf_gpio_pin_write(DEBUG_IO5_PORT_PIN,1);
// nrf_gpio_pin_write(DEBUG_IO5_PORT_PIN,0);
			esb_flush_tx();
			// leds_update(tx_payload.data[1]);
			// NRFX_DELAY_US(20);
			esb_write_payload(&tx_payload);
			tx_payload.data[1]++;
		}
	}
}

void app_timer20_init(void)
{
    nrf_timer_mode_set(APP_TIMER20,NRF_TIMER_MODE_TIMER);
    nrf_timer_bit_width_set(APP_TIMER20,NRF_TIMER_BIT_WIDTH_32);

    uint32_t prescaler = nrf_timer_prescaler_get(APP_TIMER20);
    printk("timer20_prescaler=%d\n", prescaler);
    uint32_t freq_base_hz = NRF_TIMER_BASE_FREQUENCY_GET(APP_TIMER20);
    printk("time20_hz=%d\n", freq_base_hz);
    uint32_t freq_base_khz = freq_base_hz/1000000;//US
    uint32_t ticks = (((uint32_t)TIME_TO_WAIT_125US * freq_base_khz) >> prescaler);
    printk("time20_ticks=%d\n", ticks);

    nrf_timer_int_enable(APP_TIMER20,TIMER_INTENSET_COMPARE0_Msk);
    nrf_timer_task_trigger(APP_TIMER20, NRF_TIMER_TASK_CLEAR);
	nrf_timer_shorts_set(APP_TIMER20, NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK);
    nrf_timer_cc_set(APP_TIMER20,NRF_TIMER_CC_CHANNEL0,ticks);

	printk("IRQ_PRIO_LOWEST=%ld\n",IRQ_PRIO_LOWEST);
	IRQ_DIRECT_CONNECT(TIMER20_IRQn, 3, timer20_irq_handler, 0);
	// IRQ_CONNECT(TIMER20_IRQn, 3, nrfx_isr, timer20_irq_handler, 0);
	irq_enable(TIMER20_IRQn);

    
    nrf_timer_task_trigger(APP_TIMER20, NRF_TIMER_TASK_START);
    // nrf_timer_task_trigger(APP_TIMER20, NRF_TIMER_TASK_CAPTURE0);
}


#if(DEBUG_IO_ENABLE && G24_RF_IO_ENABLE)
#include <hal/nrf_dppi.h>
#include <hal/nrf_ppib.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_gpiote.h>
#define NRF_GPIOTE_TEST  NRF_GPIOTE20
void app_g24_rf_io_init(void)
{
    // // 1. 初始化 GPIOTE
	// ret = nrf_gpiote_init();
    
    // 2. 配置 GPIO 输出
    // nrf_gpio_cfg_output(RADIO_INDICATOR_PIN);
    // nrf_gpio_pin_clear(RADIO_INDICATOR_PIN);
    
    // 3. 配置 GPIOTE 输出任务
	nrf_gpiote_task_configure(NRF_GPIOTE_TEST, 0, DEBUG_IO1_PORT_PIN,
							NRF_GPIOTE_POLARITY_TOGGLE,
							NRF_GPIOTE_INITIAL_VALUE_LOW);
	nrf_gpiote_task_configure(NRF_GPIOTE_TEST, 1, DEBUG_IO2_PORT_PIN,
							NRF_GPIOTE_POLARITY_TOGGLE,
							NRF_GPIOTE_INITIAL_VALUE_LOW);
	nrf_gpiote_task_configure(NRF_GPIOTE_TEST, 2, DEBUG_IO3_PORT_PIN,
							NRF_GPIOTE_POLARITY_TOGGLE,
							NRF_GPIOTE_INITIAL_VALUE_LOW);
    
    // 4. 启用 GPIOTE 输出任务
    nrf_gpiote_task_enable(NRF_GPIOTE_TEST, 0);
	nrf_gpiote_task_enable(NRF_GPIOTE_TEST, 1);
	nrf_gpiote_task_enable(NRF_GPIOTE_TEST, 2);

	/* Publish radio event */
	nrf_radio_publish_set(NRF_RADIO, NRF_RADIO_EVENT_TXREADY, 8);
	nrf_radio_publish_set(NRF_RADIO, NRF_RADIO_EVENT_END, 9);
	nrf_radio_publish_set(NRF_RADIO, NRF_RADIO_EVENT_RXREADY, 10);

	/* Enable same DPPI in Global domain */
	nrf_dppi_channels_enable(NRF_DPPIC10, BIT(8));
	nrf_dppi_channels_enable(NRF_DPPIC10, BIT(9));
	nrf_dppi_channels_enable(NRF_DPPIC10, BIT(10));

	/* Setup PPIB send subscribe */
	nrf_ppib_subscribe_set(NRF_PPIB11,NRF_PPIB_TASK_SEND_6,8);
	nrf_ppib_subscribe_set(NRF_PPIB11,NRF_PPIB_TASK_SEND_7,9);
	nrf_ppib_subscribe_set(NRF_PPIB11,NRF_PPIB_TASK_SEND_8,10);

	 /* Setup PPIB receive publish */
    nrf_ppib_publish_set(NRF_PPIB21, NRF_PPIB_EVENT_RECEIVE_6, 10);
	nrf_ppib_publish_set(NRF_PPIB21, NRF_PPIB_EVENT_RECEIVE_7, 11);
	nrf_ppib_publish_set(NRF_PPIB21, NRF_PPIB_EVENT_RECEIVE_8, 12);

	nrf_gpiote_subscribe_set(NRF_GPIOTE_TEST, NRF_GPIOTE_TASK_OUT_0, 10);
	nrf_gpiote_subscribe_set(NRF_GPIOTE_TEST, NRF_GPIOTE_TASK_OUT_1, 11);
	nrf_gpiote_subscribe_set(NRF_GPIOTE_TEST, NRF_GPIOTE_TASK_OUT_2, 12);

	nrf_dppi_channels_enable(NRF_DPPIC20, BIT(10));
	nrf_dppi_channels_enable(NRF_DPPIC20, BIT(11));
	nrf_dppi_channels_enable(NRF_DPPIC20, BIT(12));

	printk("G24_RF_IO_ENABLE!\n");
}
#endif

void app_gpio_initial(void)  // 
{	
#if(DEBUG_IO_ENABLE)
	nrf_gpio_cfg_output(DEBUG_IO1_PORT_PIN);
	nrf_gpio_pin_write(DEBUG_IO1_PORT_PIN,0);

	nrf_gpio_cfg_output(DEBUG_IO2_PORT_PIN);
	nrf_gpio_pin_write(DEBUG_IO2_PORT_PIN,0);

	nrf_gpio_cfg_output(DEBUG_IO3_PORT_PIN);
	nrf_gpio_pin_write(DEBUG_IO3_PORT_PIN,0);

	nrf_gpio_cfg_output(DEBUG_IO4_PORT_PIN);
	nrf_gpio_pin_write(DEBUG_IO4_PORT_PIN,0);

	nrf_gpio_cfg_output(DEBUG_IO5_PORT_PIN);
	nrf_gpio_pin_write(DEBUG_IO5_PORT_PIN,0);
app_g24_rf_io_init();
#endif 
}

int main(void)
{
	int err;

	LOG_INF("Enhanced ShockBurst ptx sample");

	err = clocks_start();
	if (err) {
		return 0;
	}

	// err = dk_leds_init();
	// if (err) {
	// 	LOG_ERR("LEDs initialization failed, err %d", err);
	// 	return 0;
	// }

	err = esb_initialize();
	if (err) {
		LOG_ERR("ESB initialization failed, err %d", err);
		return 0;
	}

	LOG_INF("Initialization complete");
	LOG_INF("Sending test packet");

	tx_payload.noack = false;
	app_gpio_initial();
	app_timer20_init();

	while (1) {
		// if (ready) {
		// 	ready = false;
		// 	esb_flush_tx();
		// leds_update(tx_payload.data[1]);

		// 	err = esb_write_payload(&tx_payload);
		// 	if (err) {
		// 		LOG_ERR("Payload write failed, err %d", err);
		// 	}
		// 	tx_payload.data[1]++;
		// }
		// k_sleep(K_MSEC(100));
		k_cpu_idle();
	}
}
