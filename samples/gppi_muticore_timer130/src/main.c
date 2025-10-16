/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#include <helpers/nrfx_gppi.h>
#include <nrfx_dppi.h>
#include <hal/nrf_ipct.h>
#include <nrfx_timer.h>
#include <nrfx_gpiote.h>
#if defined(CONFIG_SOC_NRF54H20_CPUAPP)
LOG_MODULE_REGISTER(App);

int main(void)
{
	k_sleep(K_FOREVER);

	return 0;
}
#elif defined(CONFIG_SOC_NRF54H20_CPURAD)
LOG_MODULE_REGISTER(Rad);
/** @brief Symbol specifying GPIOTE instance to be used. */
#define GPIOTE_INST_IDX 130

/** @brief Symbol specifying timer instance to be used. */
#define TIMER_INST_IDX 137

/** @brief Symbol specifying time in milliseconds to wait for handler execution. */
#define TIME_TO_WAIT_MS 1000UL

nrfx_gpiote_t const gpiote_inst = NRFX_GPIOTE_INSTANCE(GPIOTE_INST_IDX);
#define IRPT_EN_AS_PIN NRF_GPIO_PIN_MAP(9, 2)
uint8_t irpt_en_on_button_timer_c0;
uint8_t irpt_gpiote_channel;
int gpiote_config(void)
{
	nrfx_err_t status;
	status = nrfx_gpiote_init(&gpiote_inst, 0);
	if (status != NRFX_SUCCESS) {
		LOG_ERR("nrfx_gpiote_init error: 0x%08X", status);
		return 0;
	}

	status = nrfx_gpiote_channel_alloc(&gpiote_inst, &irpt_gpiote_channel);
    NRFX_ASSERT(status == NRFX_SUCCESS);
	static const nrfx_gpiote_output_config_t output_config =
	{
		.drive = NRF_GPIO_PIN_S0S1,
		.input_connect = NRF_GPIO_PIN_INPUT_DISCONNECT,
		.pull = NRF_GPIO_PIN_NOPULL,
	};
	
	const nrfx_gpiote_task_config_t task_config =
	{
		.task_ch = irpt_gpiote_channel,
		.polarity = NRF_GPIOTE_POLARITY_TOGGLE,
		.init_val = GPIOTE_CONFIG_OUTINIT_Low,
	};
	status = nrfx_gpiote_output_configure(&gpiote_inst, IRPT_EN_AS_PIN, &output_config, &task_config);
	NRFX_ASSERT(status == NRFX_SUCCESS);
	nrfx_gpiote_out_task_enable(&gpiote_inst, IRPT_EN_AS_PIN);

	LOG_INF("irpt gpiote out task, channel: %d", irpt_gpiote_channel);
	return 0;
}


static void timer_handler(nrf_timer_event_t event_type, void * p_context)
{
    if(event_type == NRF_TIMER_EVENT_COMPARE0)
    {
        char * p_msg = p_context;
        LOG_INF("Timer finished. Context passed to the handler: >%s<", p_msg);
    }
}
nrfx_timer_t timer_inst = NRFX_TIMER_INSTANCE(TIMER_INST_IDX);

void timer_config(void)
{
    nrfx_err_t status;
    (void)status;

#if defined(__ZEPHYR__)
    IRQ_CONNECT(NRFX_IRQ_NUMBER_GET(NRF_TIMER_INST_GET(TIMER_INST_IDX)), IRQ_PRIO_LOWEST,
                NRFX_TIMER_INST_HANDLER_GET(TIMER_INST_IDX), 0, 0);
#endif

 //   NRFX_EXAMPLE_LOG_INIT();

    LOG_INF("Starting nrfx_timer basic example:");
 //   NRFX_EXAMPLE_LOG_PROCESS();

    uint32_t base_frequency = NRF_TIMER_BASE_FREQUENCY_GET(timer_inst.p_reg);
    nrfx_timer_config_t config = NRFX_TIMER_DEFAULT_CONFIG(base_frequency);
    config.bit_width = NRF_TIMER_BIT_WIDTH_32;
    config.p_context = "Some context";

    status = nrfx_timer_init(&timer_inst, &config, timer_handler);
    NRFX_ASSERT(status == NRFX_SUCCESS);

    nrfx_timer_clear(&timer_inst);

    /* Creating variable desired_ticks to store the output of nrfx_timer_ms_to_ticks function */
    uint32_t desired_ticks = nrfx_timer_ms_to_ticks(&timer_inst, TIME_TO_WAIT_MS);
    LOG_INF("Time to wait: %lu ms", TIME_TO_WAIT_MS);

    /*
     * Setting the timer channel NRF_TIMER_CC_CHANNEL0 in the extended compare mode to stop the timer and
     * trigger an interrupt if internal counter register is equal to desired_ticks.
     */
    nrfx_timer_extended_compare(&timer_inst, NRF_TIMER_CC_CHANNEL0, desired_ticks,
                                NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, true);

    nrfx_timer_enable(&timer_inst);
    LOG_INF("Timer status: %s", nrfx_timer_is_enabled(&timer_inst) ? "enabled" : "disabled");
}
void dppic_init(void)
{
	nrfx_err_t status;
	status = nrfx_gppi_channel_alloc(&irpt_en_on_button_timer_c0);
	nrfx_gppi_channel_endpoints_setup(irpt_en_on_button_timer_c0,
        nrfx_timer_compare_event_address_get(&timer_inst, NRF_TIMER_CC_CHANNEL0),
        nrfx_gpiote_out_task_address_get(&gpiote_inst, IRPT_EN_AS_PIN));
	nrfx_gppi_channels_enable(BIT(irpt_en_on_button_timer_c0));
}

int main(void)
{
	gpiote_config();
	timer_config();
	dppic_init();
    return 0;
}
#endif
