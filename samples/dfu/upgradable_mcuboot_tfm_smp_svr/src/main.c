/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 * Copyright (c) 2020 Prevas A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#define LOG_LEVEL LOG_LEVEL_DBG
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(smp_sample);

#include "common.h"

static void log_bootloader_versions(void)
{
#ifdef CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION
	LOG_INF("APP image version: %s", CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION);
#else
	LOG_INF("APP image version: unavailable");
#endif
}

int main(void)
{
	start_smp_bluetooth_adverts();

	/* using __TIME__ ensure that a new binary will be built on every
	 * compile which is convenient when testing firmware upgrade.
	 */
	LOG_INF("build time: " __DATE__ " " __TIME__);
	log_bootloader_versions();

	/* The system work queue handles all incoming mcumgr requests.  Let the
	 * main thread idle while the mcumgr server runs.
	 */
	while (1) {
		k_sleep(K_MSEC(1000));
	}
	return 0;
}
