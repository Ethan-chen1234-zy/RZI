/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file
 * @brief Shared Semtech USP platform layer used by the RZI USP backends.
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/lorawan_lbm/lorawan_hal_init.h>
#include <zephyr/usp/smtc_sw_platform_helper.h>
#include <smtc_modem_api.h>
#include <smtc_modem_utilities.h>
#include <smtc_rac_api.h>
#include <rzi/err.h>

#include "usp_platform.h"

/* usp_zephyr v1.2.1 registers the usp log module in smtc_sw_platform_helper.c. */
LOG_MODULE_DECLARE(usp, CONFIG_USP_LOG_LEVEL);
BUILD_ASSERT(CONFIG_USP_THREADS_MUTEXES, "RZI needs USP serialization");

/* Protected by rac_api_mutex. */
static bool initialized;
static bool reset_seen;
static rzi_usp_event_handler_t handler;
static K_MUTEX_DEFINE(init_lock);
static unsigned int users;
static bool engine_suspended;
extern const k_tid_t lbm_main_thread_id;

__weak bool rzi_usp_radio_is_ready(const struct device *radio)
{
	return device_is_ready(radio);
}

int rzi_usp_result(smtc_modem_return_code_t rc)
{
	switch (rc) {
	case SMTC_MODEM_RC_OK:
		return 0;
	case SMTC_MODEM_RC_NOT_INIT:
		return -RZI_ERR_NOT_READY;
	case SMTC_MODEM_RC_INVALID:
	case SMTC_MODEM_RC_INVALID_STACK_ID:
		return -RZI_ERR_INVALID;
	case SMTC_MODEM_RC_BUSY:
		return -RZI_ERR_BUSY;
	case SMTC_MODEM_RC_NO_TIME:
	case SMTC_MODEM_RC_NO_EVENT:
		return -RZI_ERR_NO_DATA;
	case SMTC_MODEM_RC_FAIL:
	default:
		return -RZI_ERR_IO;
	}
}

static void modem_event_callback(void)
{
	smtc_modem_event_t event;
	uint8_t pending = 0;

	do {
		smtc_modem_return_code_t rc = smtc_modem_get_event(&event, &pending);

		if (rc == SMTC_MODEM_RC_NO_EVENT) {
			break;
		}
		if (rc != SMTC_MODEM_RC_OK) {
			if (handler != NULL) {
				handler(NULL, rzi_usp_result(rc));
			}
			break;
		}
		if (event.event_type == SMTC_MODEM_EVENT_RESET) {
			reset_seen = true;
		}
		if (handler != NULL) {
			handler(&event, 0);
		}
	} while (pending > 0);
}

void rzi_usp_lock(void)
{
	k_mutex_lock(&rac_api_mutex, K_FOREVER);
}

void rzi_usp_unlock(void)
{
	k_mutex_unlock(&rac_api_mutex);
	smtc_modem_hal_wake_up();
}

int rzi_usp_init(void)
{
	const struct device *radio = DEVICE_DT_GET(DT_CHOSEN(zephyr_lorawan_transceiver));
	smtc_modem_status_mask_t status;
	int rc = 0;

	if (k_is_in_isr()) {
		return -RZI_ERR_ISR;
	}
	if (!rzi_usp_radio_is_ready(radio)) {
		return -RZI_ERR_NO_DEVICE;
	}

	k_mutex_lock(&init_lock, K_FOREVER);
	if (!initialized) {
		SMTC_SW_PLATFORM_INIT();
		rzi_usp_lock();
		reset_seen = false;
		smtc_rac_init();
		smtc_modem_init(modem_event_callback);
		if (!smtc_is_modem_initialized()) {
			rc = -RZI_ERR_IO;
		} else {
			rc = rzi_usp_result(smtc_modem_get_status(0, &status));
		}
		initialized = rc == 0;
		if (rc != 0) {
			reset_seen = false;
		}
		rzi_usp_unlock();
	}
	k_mutex_unlock(&init_lock);
	return rc;
}

int rzi_usp_acquire(void)
{
	int rc = rzi_usp_init();

	if (rc != 0) {
		return rc;
	}
	k_mutex_lock(&init_lock, K_FOREVER);
	if (engine_suspended) {
		k_thread_resume(lbm_main_thread_id);
		engine_suspended = false;
	}
	users++;
	k_mutex_unlock(&init_lock);
	return 0;
}

int rzi_usp_release(void)
{
	int rc = 0;

	if (k_is_in_isr()) {
		return -RZI_ERR_ISR;
	}
	if (k_current_get() == lbm_main_thread_id) {
		return -RZI_ERR_DENIED;
	}
	k_mutex_lock(&init_lock, K_FOREVER);
	if (users == 0U) {
		rc = -RZI_ERR_INVALID_STATE;
	} else if (users > 1U) {
		users--;
	} else {
		rzi_usp_lock();
		if (ral_set_sleep(&smtc_rac_get_radio()->ral, true) != RAL_STATUS_OK) {
			rc = -RZI_ERR_IO;
		} else {
			/* The mutex proves the engine is outside vendor code when parked. */
			k_thread_suspend(lbm_main_thread_id);
			engine_suspended = true;
			users = 0U;
		}
		rzi_usp_unlock();
	}
	k_mutex_unlock(&init_lock);
	return rc;
}

void rzi_usp_set_event_handler(rzi_usp_event_handler_t next)
{
	handler = next;
	if (handler != NULL && reset_seen) {
		const smtc_modem_event_t reset = {
			.event_type = SMTC_MODEM_EVENT_RESET,
		};

		handler(&reset, 0);
	}
}
