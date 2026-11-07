/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <rzi/err.h>

#include <smtc_rac_api.h>

#include "usp_platform.h"

LOG_MODULE_REGISTER(usp, 0);
K_MUTEX_DEFINE(rac_api_mutex);

static bool radio_ready = true;
static bool modem_ready;
static int platform_calls;
static int modem_calls;
static smtc_modem_return_code_t status_result = SMTC_MODEM_RC_OK;
static ral_status_t sleep_result = RAL_STATUS_OK;
static smtc_rac_radio_t radio;

static void lbm_thread(void *unused1, void *unused2, void *unused3)
{
	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);
	ARG_UNUSED(unused3);

	for (;;) {
		k_sleep(K_FOREVER);
	}
}

K_THREAD_DEFINE(lbm_main_thread_id, 1024, lbm_thread, NULL, NULL, NULL, K_PRIO_PREEMPT(1), 0, 0);

bool rzi_usp_radio_is_ready(const struct device *dev)
{
	ARG_UNUSED(dev);
	return radio_ready;
}

void test_platform_init(void)
{
	platform_calls++;
}

void smtc_rac_init(void)
{
}

smtc_rac_radio_t *smtc_rac_get_radio(void)
{
	return &radio;
}

ral_status_t ral_set_sleep(ral_t *ral, bool retain_config)
{
	zassert_equal(ral, &radio.ral);
	zassert_true(retain_config);
	return sleep_result;
}

void smtc_modem_init(void (*callback)(void))
{
	ARG_UNUSED(callback);
	modem_calls++;
}

bool smtc_is_modem_initialized(void)
{
	return modem_ready;
}

smtc_modem_return_code_t smtc_modem_get_status(uint8_t stack_id, smtc_modem_status_mask_t *status)
{
	zassert_equal(stack_id, 0);
	*status = 0;
	return status_result;
}

smtc_modem_return_code_t smtc_modem_get_event(smtc_modem_event_t *event, uint8_t *pending)
{
	ARG_UNUSED(event);
	*pending = 0;
	return SMTC_MODEM_RC_NO_EVENT;
}

void smtc_modem_hal_wake_up(void)
{
}

ZTEST(rzi_usp_platform, test_initialization_failures_can_be_retried)
{
	radio_ready = false;
	zassert_equal(rzi_usp_init(), -RZI_ERR_NO_DEVICE);
	zassert_equal(platform_calls, 0);
	radio_ready = true;
	zassert_equal(rzi_usp_init(), -RZI_ERR_IO);
	zassert_equal(modem_calls, 1);
	modem_ready = true;
	status_result = SMTC_MODEM_RC_FAIL;
	zassert_equal(rzi_usp_init(), -RZI_ERR_IO);
	zassert_equal(modem_calls, 2);
	status_result = SMTC_MODEM_RC_OK;
	zassert_ok(rzi_usp_init());
	zassert_equal(modem_calls, 3);
	zassert_ok(rzi_usp_init());
	zassert_equal(modem_calls, 3);

	zassert_ok(rzi_usp_acquire());
	zassert_ok(rzi_usp_acquire());
	zassert_ok(rzi_usp_release());
	zassert_ok(rzi_usp_release());
	zassert_equal(rzi_usp_release(), -RZI_ERR_INVALID_STATE);

	zassert_ok(rzi_usp_acquire());
	sleep_result = RAL_STATUS_ERROR;
	zassert_equal(rzi_usp_release(), -RZI_ERR_IO);
	sleep_result = RAL_STATUS_OK;
	zassert_ok(rzi_usp_release());
}

static int isr_result;

static void init_from_isr(const void *unused)
{
	ARG_UNUSED(unused);
	isr_result = rzi_usp_init();
}

ZTEST(rzi_usp_platform, test_isr_initialization_is_rejected)
{
	irq_offload(init_from_isr, NULL);
	zassert_equal(isr_result, -RZI_ERR_ISR);
}

ZTEST_SUITE(rzi_usp_platform, NULL, NULL, NULL, NULL, NULL);
