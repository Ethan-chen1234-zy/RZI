/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */

#ifndef TEST_SMTC_SW_PLATFORM_HELPER_H
#define TEST_SMTC_SW_PLATFORM_HELPER_H

#include <zephyr/kernel.h>

extern struct k_mutex rac_api_mutex;
void test_platform_init(void);
void smtc_rac_init(void);
#define SMTC_SW_PLATFORM_INIT() test_platform_init()

#endif
