/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#include <rzi/lora/lora.h>

static atomic_t tx_callbacks;

static void on_tx_done(int error, void *user_data)
{
	ARG_UNUSED(user_data);
	zassert_ok(error);
	atomic_inc(&tx_callbacks);
}

ZTEST(rzi_lora_lifecycle, test_stop_restart_and_deinit_contract)
{
	const struct rzi_lora_callbacks callbacks = {
		.tx_done = on_tx_done,
	};
	const uint8_t payload = 0x5a;

	zassert_ok(rzi_lora_register_callbacks(&callbacks));
	zassert_ok(rzi_lora_init(RZI_LORA_MOD_LORA));
	zassert_true(rzi_lora_is_active());
	zassert_ok(rzi_lora_send(&payload, sizeof(payload)));
	zassert_equal(atomic_get(&tx_callbacks), 1);

	zassert_ok(rzi_lora_stop());
	zassert_false(rzi_lora_is_active());
	zassert_ok(rzi_lora_stop());
	zassert_ok(rzi_lora_init(RZI_LORA_MOD_FSK));
	zassert_true(rzi_lora_is_active());
	zassert_ok(rzi_lora_send(&payload, sizeof(payload)));
	zassert_equal(atomic_get(&tx_callbacks), 2);

	zassert_ok(rzi_lora_deinit());
	zassert_false(rzi_lora_is_active());
	zassert_ok(rzi_lora_init(RZI_LORA_MOD_LORA));
	zassert_ok(rzi_lora_send(&payload, sizeof(payload)));
	zassert_equal(atomic_get(&tx_callbacks), 2);
	zassert_ok(rzi_lora_deinit());
}

ZTEST_SUITE(rzi_lora_lifecycle, NULL, NULL, NULL, NULL, NULL);
