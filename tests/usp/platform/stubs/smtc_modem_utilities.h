/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */

#ifndef TEST_SMTC_MODEM_UTILITIES_H
#define TEST_SMTC_MODEM_UTILITIES_H

#include <stdbool.h>

void smtc_modem_init(void (*callback)(void));
bool smtc_is_modem_initialized(void);

#endif
