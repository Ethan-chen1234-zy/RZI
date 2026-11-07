/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#include <rzi/lorawan/lorawan.h>

extern int fake_stop_calls;
extern int fake_stop_error;
extern int fake_deinit_calls;
extern int fake_deinit_error;
extern bool fake_start_error_event;

struct callback_stats {
	atomic_t ready;
	atomic_t state_changes;
	atomic_t last_state;
	atomic_t try_stop;
	atomic_t stop_result;
};

static void on_event(const struct rzi_lorawan_event *event, void *user_data)
{
	struct callback_stats *stats = user_data;

	if (event->type == RZI_LORAWAN_EVENT_READY) {
		atomic_inc(&stats->ready);
		if (atomic_cas(&stats->try_stop, 1, 0)) {
			atomic_set(&stats->stop_result, rzi_lorawan_stop());
		}
	} else if (event->type == RZI_LORAWAN_EVENT_STATE_CHANGED) {
		atomic_set(&stats->last_state, event->state);
		atomic_inc(&stats->state_changes);
	}
}

static void wait_for_state(enum rzi_lorawan_state expected)
{
	int64_t deadline = k_uptime_get() + 1000;
	enum rzi_lorawan_state state = RZI_LORAWAN_STATE_STOPPED;

	do {
		zassert_ok(rzi_lorawan_get_state(&state));
		if (state == expected) {
			return;
		}
		k_sleep(K_MSEC(1));
	} while (k_uptime_get() < deadline);

	zassert_equal(state, expected);
}

static void wait_for_count(const atomic_t *value, atomic_val_t expected)
{
	int64_t deadline = k_uptime_get() + 1000;

	while (atomic_get(value) < expected && k_uptime_get() < deadline) {
		k_sleep(K_MSEC(1));
	}
	zassert_equal(atomic_get(value), expected);
}

ZTEST(rzi_lorawan_lifecycle, test_stop_restart_and_deinit_contract)
{
	struct callback_stats stats = {0};
	const struct rzi_lorawan_callbacks callbacks = {
		.on_event = on_event,
		.user_data = &stats,
	};
	atomic_val_t callbacks_before_restart;

	zassert_ok(rzi_lorawan_set_join_backoff_bypass(true));
	zassert_ok(rzi_lorawan_register_callbacks(&callbacks));
	atomic_set(&stats.try_stop, 1);
	zassert_ok(rzi_lorawan_init(RZI_LORAWAN_REGION_US_915));
	wait_for_state(RZI_LORAWAN_STATE_READY);
	wait_for_count(&stats.ready, 1);
	zassert_equal(atomic_get(&stats.stop_result), -RZI_ERR_DENIED);

	fake_stop_error = -RZI_ERR_BUSY;
	zassert_equal(rzi_lorawan_stop(), -RZI_ERR_BUSY);
	zassert_equal(fake_stop_calls, 1);
	wait_for_state(RZI_LORAWAN_STATE_READY);

	fake_stop_error = 0;
	zassert_ok(rzi_lorawan_stop());
	zassert_equal(fake_stop_calls, 2);
	wait_for_state(RZI_LORAWAN_STATE_STOPPED);
	zassert_equal(atomic_get(&stats.last_state), RZI_LORAWAN_STATE_STOPPED);
	zassert_ok(rzi_lorawan_stop());
	zassert_equal(fake_stop_calls, 2);

	zassert_ok(rzi_lorawan_init(RZI_LORAWAN_REGION_US_915));
	wait_for_state(RZI_LORAWAN_STATE_READY);
	fake_deinit_error = -RZI_ERR_IO;
	zassert_equal(rzi_lorawan_deinit(), -RZI_ERR_IO);
	zassert_equal(fake_stop_calls, 3);
	zassert_equal(fake_deinit_calls, 1);
	wait_for_state(RZI_LORAWAN_STATE_STOPPED);

	fake_deinit_error = 0;
	zassert_ok(rzi_lorawan_deinit());
	zassert_equal(fake_stop_calls, 3);
	zassert_equal(fake_deinit_calls, 2);
	callbacks_before_restart = atomic_get(&stats.state_changes);

	zassert_ok(rzi_lorawan_init(RZI_LORAWAN_REGION_US_915));
	wait_for_state(RZI_LORAWAN_STATE_READY);
	k_sleep(K_MSEC(10));
	zassert_equal(atomic_get(&stats.state_changes), callbacks_before_restart);
	zassert_ok(rzi_lorawan_deinit());

	zassert_ok(rzi_lorawan_register_callbacks(&callbacks));
	fake_start_error_event = true;
	zassert_ok(rzi_lorawan_init(RZI_LORAWAN_REGION_US_915));
	wait_for_state(RZI_LORAWAN_STATE_STOPPED);
	zassert_equal(rzi_lorawan_init(RZI_LORAWAN_REGION_US_915), -RZI_ERR_ALREADY);
	zassert_equal(rzi_lorawan_set_region(RZI_LORAWAN_REGION_EU_868), -RZI_ERR_INVALID_STATE);
	fake_start_error_event = false;
	zassert_ok(rzi_lorawan_stop());
	zassert_ok(rzi_lorawan_deinit());
}

ZTEST_SUITE(rzi_lorawan_lifecycle, NULL, NULL, NULL, NULL, NULL);
