/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file
 * @brief Shared Semtech USP platform layer used by the RZI USP backends.
 */
#ifndef RZI_USP_PLATFORM_H
#define RZI_USP_PLATFORM_H

#include <stdbool.h>

#include <smtc_modem_api.h>

struct device;

/**
 * Called in the USP thread with the USP lock held, once per LBM event.
 * @p event is NULL and @p error is a negative RZI_ERR_* value when the
 * event queue could not be read.
 */
typedef void (*rzi_usp_event_handler_t)(const smtc_modem_event_t *event, int error);

/**
 * Initialize the USP platform, the radio access controller and LoRa Basics
 * Modem and verify device/modem readiness before committing initialization.
 * Return 0 on success or a negative RZI_ERR_* value. Failed calls may be
 * retried; later calls after success return immediately. Thread context only.
 */
int rzi_usp_init(void);

/** Board readiness seam; weak default calls device_is_ready(). */
bool rzi_usp_radio_is_ready(const struct device *radio);

/** Initialize/resume the shared platform and retain one backend reference. */
int rzi_usp_acquire(void);

/** Release a reference; the last release sleeps the radio and parks the engine. */
int rzi_usp_release(void);

/** Serialize USP API calls with the USP engine thread. Recursive. */
void rzi_usp_lock(void);

/** Release rzi_usp_lock() and wake the engine to process queued work. */
void rzi_usp_unlock(void);

/**
 * Install the LBM event consumer. If LBM already reported RESET, the handler
 * receives a RESET event before this returns. Call with the lock held.
 */
void rzi_usp_set_event_handler(rzi_usp_event_handler_t handler);

/** Map an LBM return code to 0 or a negative RZI_ERR_* value. */
int rzi_usp_result(smtc_modem_return_code_t rc);

#endif /* RZI_USP_PLATFORM_H */
