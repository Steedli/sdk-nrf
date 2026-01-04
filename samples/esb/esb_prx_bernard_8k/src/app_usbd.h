/*
 * Copyright (c) 2025 Nordic Semiconductor
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __APP_USBD_H_
#define __APP_USBD_H_

#ifdef __cplusplus
extern "C" {
#endif


#define APP_USBD_BUTTONS_IDX		0
#define APP_USBD_DX_IDX			1
#define APP_USBD_DY_IDX			2
#define APP_USBD_DW_IDX			3
#define APP_USBD_DATA_SIZE		4


typedef void (*app_usbd_in_report_done_cb)(void);


int app_usbd_init(app_usbd_in_report_done_cb cb);

int app_usbd_enable(void);

int app_usbd_submit_report(const uint8_t *report, uint16_t size);


#ifdef __cplusplus
}
#endif

#endif /* __APP_USBD_H_ */
