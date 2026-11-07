/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */

#ifndef TEST_SMTC_MODEM_API_H
#define TEST_SMTC_MODEM_API_H

#include <stdint.h>

typedef enum {
	SMTC_MODEM_RC_OK,
	SMTC_MODEM_RC_NOT_INIT,
	SMTC_MODEM_RC_INVALID,
	SMTC_MODEM_RC_INVALID_STACK_ID,
	SMTC_MODEM_RC_BUSY,
	SMTC_MODEM_RC_NO_TIME,
	SMTC_MODEM_RC_NO_EVENT,
	SMTC_MODEM_RC_FAIL,
} smtc_modem_return_code_t;

typedef uint32_t smtc_modem_status_mask_t;
enum {
	SMTC_MODEM_EVENT_RESET
};
typedef struct {
	int event_type;
} smtc_modem_event_t;

smtc_modem_return_code_t smtc_modem_get_event(smtc_modem_event_t *event, uint8_t *pending);
smtc_modem_return_code_t smtc_modem_get_status(uint8_t stack_id, smtc_modem_status_mask_t *status);

#endif
