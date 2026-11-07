/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_SMTC_RAC_API_H
#define TEST_SMTC_RAC_API_H

#include <stdbool.h>

typedef enum {
	RAL_STATUS_OK = 0,
	RAL_STATUS_ERROR,
} ral_status_t;

typedef struct {
	int unused;
} ral_t;

typedef struct {
	ral_t ral;
} smtc_rac_radio_t;

void smtc_rac_init(void);
smtc_rac_radio_t *smtc_rac_get_radio(void);
ral_status_t ral_set_sleep(ral_t *ral, bool retain_config);

#endif /* TEST_SMTC_RAC_API_H */
