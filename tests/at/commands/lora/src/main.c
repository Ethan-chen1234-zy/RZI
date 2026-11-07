/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#include <rzi/at/at.h>
#include <rzi/lora/lora.h>
#include <rzi/network_mode.h>

static atomic_t init_error;
static atomic_t stop_error;
static atomic_t stop_calls;
static enum rzi_lora_modulation active_modulation;

int __real_rzi_lora_init(enum rzi_lora_modulation modulation);
int __real_rzi_lora_stop(void);

int __wrap_rzi_lora_init(enum rzi_lora_modulation modulation)
{
	int rc = (int)atomic_set(&init_error, 0);

	if (rc == 0) {
		rc = __real_rzi_lora_init(modulation);
	}
	if (rc == 0) {
		active_modulation = modulation;
	}
	return rc;
}

int __wrap_rzi_lora_stop(void)
{
	int rc = (int)atomic_set(&stop_error, 0);

	atomic_inc(&stop_calls);
	return rc != 0 ? rc : __real_rzi_lora_stop();
}

#if defined(CONFIG_RZI_AT_NVM)
#include "at_priv.h"

static atomic_t write_error;
int __real_rzi_storage_write_record(const char *ns, const char *key, uint16_t version,
				    const void *value, size_t size);

int __wrap_rzi_storage_write_record(const char *ns, const char *key, uint16_t version,
				    const void *value, size_t size)
{
	if (strcmp(ns, "rzi") == 0 && strcmp(key, "network_mode") == 0) {
		int rc = (int)atomic_set(&write_error, 0);

		if (rc != 0) {
			return rc;
		}
	}
	return __real_rzi_storage_write_record(ns, key, version, value, size);
}
#endif

#if defined(CONFIG_RZI_LORAWAN)
#include "backend/lorawan_backend.h"

static int fake_init(enum rzi_lorawan_region region, bool bypass, rzi_lorawan_event_sink_t sink)
{
	const struct rzi_lorawan_backend_event event = {.type = RZI_LORAWAN_BACKEND_READY};

	ARG_UNUSED(region);
	ARG_UNUSED(bypass);
	sink(&event);
	return 0;
}

static int fake_join(const struct rzi_lorawan_join_config *config)
{
	ARG_UNUSED(config);
	return -RZI_ERR_NOT_SUPPORTED;
}

static int fake_leave(void)
{
	return 0;
}

static int fake_send(uint8_t port, const uint8_t *data, size_t size,
		     enum rzi_lorawan_message_type type)
{
	ARG_UNUSED(port);
	ARG_UNUSED(data);
	ARG_UNUSED(size);
	ARG_UNUSED(type);
	return -RZI_ERR_NOT_SUPPORTED;
}

static int fake_set_class(enum rzi_lorawan_class device_class)
{
	ARG_UNUSED(device_class);
	return 0;
}

static int fake_false(bool *value)
{
	*value = false;
	return 0;
}

const struct rzi_lorawan_backend_api rzi_lorawan_backend = {
	.capabilities = RZI_LORAWAN_CAP_OTAA | RZI_LORAWAN_CAP_CLASS_A,
	.base.init = fake_init,
	.base.join = fake_join,
	.base.leave = fake_leave,
	.base.send = fake_send,
	.base.set_class = fake_set_class,
	.base.is_joined = fake_false,
	.base.is_busy = fake_false,
};
#endif

K_SEM_DEFINE(output_ready, 0, 16);
K_MUTEX_DEFINE(output_lock);
static char output[256];
static size_t output_size;

static int io_write(const uint8_t *data, size_t size, void *user_data)
{
	ARG_UNUSED(user_data);
	k_mutex_lock(&output_lock, K_FOREVER);
	zassert_true(output_size + size < sizeof(output));
	memcpy(output + output_size, data, size);
	output_size += size;
	output[output_size] = '\0';
	k_mutex_unlock(&output_lock);
	k_sem_give(&output_ready);
	return 0;
}

static void send_command(const char *command, const char *expected)
{
	int64_t deadline = k_uptime_get() + 1000;

	k_mutex_lock(&output_lock, K_FOREVER);
	output_size = 0;
	output[0] = '\0';
	k_sem_reset(&output_ready);
	k_mutex_unlock(&output_lock);
	zassert_ok(rzi_at_receive((const uint8_t *)command, strlen(command)));
	for (;;) {
		bool found;
		int64_t remaining = deadline - k_uptime_get();

		zassert_true(remaining > 0);
		zassert_ok(k_sem_take(&output_ready, K_MSEC(remaining)));
		k_mutex_lock(&output_lock, K_FOREVER);
		found = strstr(output, expected) != NULL;
		k_mutex_unlock(&output_lock);
		if (found) {
			return;
		}
	}
}

static void *setup(void)
{
	const struct rzi_at_io io = {.write = io_write};

	zassert_ok(rzi_at_init(&io));
	return NULL;
}

static void prepare_lora(void *fixture)
{
	int64_t deadline = k_uptime_get() + 1000;

	ARG_UNUSED(fixture);
	atomic_clear(&init_error);
	atomic_clear(&stop_error);
	send_command("AT+NWM=0\r", "\r\nOK\r\n");
	while (!rzi_lora_is_active() && k_uptime_get() < deadline) {
		k_sleep(K_MSEC(1));
	}
	zassert_true(rzi_lora_is_active());
	zassert_equal(active_modulation, RZI_LORA_MOD_LORA);
}

static void assert_lora_restored(void)
{
	send_command("AT+NWM=?\r", "AT+NWM=0\r\nOK\r\n");
	zassert_true(rzi_lora_is_active());
	zassert_equal(active_modulation, RZI_LORA_MOD_LORA);
}

ZTEST(rzi_at_lora_command, test_switches_both_modulations)
{
	send_command("AT+NWM=2\r", "\r\nOK\r\n");
	zassert_true(rzi_lora_is_active());
	zassert_equal(active_modulation, RZI_LORA_MOD_FSK);
	send_command("AT+NWM=?\r", "AT+NWM=2\r\nOK\r\n");
	send_command("AT+NWM=0\r", "\r\nOK\r\n");
	assert_lora_restored();
}

ZTEST(rzi_at_lora_command, test_same_mode_does_not_restart_radio)
{
	atomic_val_t before = atomic_get(&stop_calls);

	send_command("AT+NWM=0\r", "\r\nOK\r\n");
	zassert_equal(atomic_get(&stop_calls), before);
	assert_lora_restored();
}

ZTEST(rzi_at_lora_command, test_stop_failure_keeps_previous_mode)
{
	atomic_set(&stop_error, -RZI_ERR_IO);
	send_command("AT+NWM=2\r", "\r\nAT_ERROR\r\n");
	assert_lora_restored();
}

ZTEST(rzi_at_lora_command, test_init_failure_restores_previous_mode)
{
	atomic_set(&init_error, -RZI_ERR_IO);
	send_command("AT+NWM=2\r", "\r\nAT_ERROR\r\n");
	assert_lora_restored();
}

#if defined(CONFIG_RZI_AT_COMMAND_LORAWAN)
ZTEST(rzi_at_lora_command, test_lorawan_mode_stops_raw_radio)
{
	send_command("AT+NWM=1\r", "\r\nOK\r\n");
	zassert_false(rzi_lora_is_active());
	send_command("AT+NWM=0\r", "\r\nOK\r\n");
	assert_lora_restored();
}
#endif

#if defined(CONFIG_RZI_AT_NVM)
ZTEST(rzi_at_lora_command, test_factory_reset_sets_default_network_mode)
{
	enum rzi_network_mode stored;

	zassert_ok(rzi_at_registry_factory_reset());
	zassert_ok(rzi_get_network_mode(&stored));
	zassert_equal(stored, RZI_NETWORK_MODE_LORAWAN);
	/* The production reset flow reboots; restore the fixture for later tests. */
	zassert_ok(rzi_set_network_mode(RZI_NETWORK_MODE_P2P_LORA));
}

ZTEST(rzi_at_lora_command, test_factory_reset_reports_mode_write_failure)
{
	enum rzi_network_mode stored;

	atomic_set(&write_error, -RZI_ERR_IO);
	zassert_equal(rzi_at_registry_factory_reset(), -RZI_ERR_IO);
	zassert_ok(rzi_get_network_mode(&stored));
	zassert_equal(stored, RZI_NETWORK_MODE_P2P_LORA);
	assert_lora_restored();
}

ZTEST(rzi_at_lora_command, test_write_failure_restores_radio_and_saved_mode)
{
	enum rzi_network_mode stored;

	atomic_set(&write_error, -RZI_ERR_IO);
	send_command("AT+NWM=2\r", "\r\nAT_ERROR\r\n");
	assert_lora_restored();
	zassert_ok(rzi_get_network_mode(&stored));
	zassert_equal(stored, RZI_NETWORK_MODE_P2P_LORA);
}
#endif

ZTEST_SUITE(rzi_at_lora_command, NULL, setup, prepare_lora, NULL, NULL);
