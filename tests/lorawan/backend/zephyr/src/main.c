/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/lorawan/emul.h>
#include <zephyr/lorawan/lorawan.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#include <rzi/lorawan/lorawan.h>

#include "backend/lorawan_backend.h"

K_SEM_DEFINE(callback_sem, 0, 8);

struct callback_stats {
	atomic_t ready;
	atomic_t joined;
	atomic_t join_status;
	atomic_t uplink;
	atomic_t downlink;
	atomic_t payload_valid;
};

static uint8_t last_uplink_port;
static uint8_t last_uplink_len;
static uint8_t last_uplink[RZI_LORAWAN_MAX_PAYLOAD];

struct wrapped_join {
	bool valid;
	enum lorawan_act_type mode;
	uint32_t dev_addr;
	uint8_t nwk_skey[16];
	uint8_t app_skey[16];
};

static struct wrapped_join captured_join;

int __real_lorawan_join(const struct lorawan_join_config *join_cfg);

int __wrap_lorawan_join(const struct lorawan_join_config *join_cfg)
{
	captured_join.valid = join_cfg != NULL;
	if (join_cfg != NULL) {
		captured_join.mode = join_cfg->mode;
		if (join_cfg->mode == LORAWAN_ACT_ABP && join_cfg->abp.nwk_skey != NULL &&
		    join_cfg->abp.app_skey != NULL) {
			captured_join.dev_addr = join_cfg->abp.dev_addr;
			memcpy(captured_join.nwk_skey, join_cfg->abp.nwk_skey,
			       sizeof(captured_join.nwk_skey));
			memcpy(captured_join.app_skey, join_cfg->abp.app_skey,
			       sizeof(captured_join.app_skey));
		}
	}
	return __real_lorawan_join(join_cfg);
}

static void on_emul_uplink(uint8_t port, uint8_t len, const uint8_t *data)
{
	last_uplink_port = port;
	last_uplink_len = len;
	if (data != NULL && len > 0U) {
		memcpy(last_uplink, data, len);
	}
}

static void on_event(const struct rzi_lorawan_event *event, void *user_data)
{
	struct callback_stats *stats = user_data;

	switch (event->type) {
	case RZI_LORAWAN_EVENT_READY:
		atomic_inc(&stats->ready);
		k_sem_give(&callback_sem);
		break;
	case RZI_LORAWAN_EVENT_JOINED:
		atomic_set(&stats->join_status, 0);
		atomic_inc(&stats->joined);
		k_sem_give(&callback_sem);
		break;
	case RZI_LORAWAN_EVENT_JOIN_FAILED:
		atomic_set(&stats->join_status, event->error);
		k_sem_give(&callback_sem);
		break;
	case RZI_LORAWAN_EVENT_TXDONE:
		zassert_equal(event->tx.status, RZI_LORAWAN_EVENT_TXDONE_SENT);
		zassert_ok(event->tx.error);
		atomic_inc(&stats->uplink);
		k_sem_give(&callback_sem);
		break;
	case RZI_LORAWAN_EVENT_DOWNDATA:
		if (event->downlink.port == 3 && event->downlink.size == 2 &&
		    event->downlink.data[0] == 0xab && event->downlink.data[1] == 0xcd) {
			atomic_set(&stats->payload_valid, 1);
		}
		atomic_inc(&stats->downlink);
		k_sem_give(&callback_sem);
		break;
	default:
		break;
	}
}

static void wait_for_callbacks(unsigned int count)
{
	for (unsigned int i = 0; i < count; ++i) {
		zassert_ok(k_sem_take(&callback_sem, K_SECONDS(2)));
	}
}

ZTEST(rzi_lorawan_backend_zephyr, test_async_contract_over_blocking_api)
{
	struct callback_stats stats = {0};
	const struct rzi_lorawan_callbacks callbacks = {
		.on_event = on_event,
		.user_data = &stats,
	};
	static const uint8_t dev_eui[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
	static const uint8_t join_eui[8] = {0};
	static const uint8_t app_key[16] = {0x11};
	const uint8_t payload[] = {0x12, 0x34};
	const uint8_t downlink[] = {0xab, 0xcd};
	bool joined = false;
	uint32_t caps;

	lorawan_emul_register_uplink_callback(on_emul_uplink);
	zassert_ok(rzi_lorawan_register_callbacks(&callbacks));
	zassert_ok(rzi_lorawan_set_join_backoff_bypass(true));

	caps = rzi_lorawan_get_capabilities();
	zassert_true((caps & RZI_LORAWAN_CAP_CLASS_C) != 0U);
	zassert_true((caps & RZI_LORAWAN_CAP_CLASS_B) == 0U);
	zassert_true((caps & RZI_LORAWAN_CAP_MULTICAST) == 0U);

	{
		uint32_t remaining_s = 123;

		zassert_equal(rzi_lorawan_get_join_backoff(&remaining_s), -RZI_ERR_NOT_READY);
		zassert_equal(remaining_s, 123U);
	}
	zassert_ok(rzi_lorawan_init(RZI_LORAWAN_REGION_EU_868));
	wait_for_callbacks(1);
	zassert_equal(atomic_get(&stats.ready), 1);
	{
		uint32_t remaining_s = 123;

		zassert_equal(rzi_lorawan_get_join_backoff(NULL), -RZI_ERR_INVALID);
		zassert_equal(rzi_lorawan_get_join_backoff(&remaining_s), -RZI_ERR_NOT_SUPPORTED);
		zassert_equal(remaining_s, 123U);
	}

	{
		uint32_t dev_addr = 1U;
		uint8_t stored[16];
		uint8_t key[16];
		uint8_t zeros[16] = {0};

		memset(stored, 0xab, sizeof(stored));
		zassert_ok(rzi_lorawan_set_activation(RZI_LORAWAN_ACTIVATION_ABP));
		zassert_ok(rzi_lorawan_set_dev_addr(0x01020304U));
		zassert_ok(rzi_lorawan_set_nwk_skey(stored, sizeof(stored)));
		zassert_ok(rzi_lorawan_set_app_skey(stored, sizeof(stored)));
		zassert_ok(rzi_lorawan_get_dev_addr(&dev_addr));
		zassert_equal(dev_addr, 0x01020304U);
		zassert_ok(rzi_lorawan_get_nwk_skey(key, sizeof(key)));
		zassert_mem_equal(key, stored, sizeof(stored));
		zassert_ok(rzi_lorawan_get_app_skey(key, sizeof(key)));
		zassert_mem_equal(key, stored, sizeof(stored));

		zassert_ok(rzi_lorawan_set_activation(RZI_LORAWAN_ACTIVATION_OTAA));
		zassert_ok(rzi_lorawan_get_dev_addr(&dev_addr));
		zassert_equal(dev_addr, 0U);
		zassert_ok(rzi_lorawan_get_nwk_skey(key, sizeof(key)));
		zassert_mem_equal(key, zeros, sizeof(zeros));
		zassert_ok(rzi_lorawan_get_app_skey(key, sizeof(key)));
		zassert_mem_equal(key, zeros, sizeof(zeros));
		zassert_ok(rzi_lorawan_set_activation(RZI_LORAWAN_ACTIVATION_ABP));
	}

	zassert_ok(rzi_lorawan_set_dev_eui(dev_eui, sizeof(dev_eui)));
	zassert_ok(rzi_lorawan_set_app_eui(join_eui, sizeof(join_eui)));
	zassert_ok(rzi_lorawan_set_app_key(app_key, sizeof(app_key)));
	zassert_ok(rzi_lorawan_set_dev_nonce(7));
	zassert_ok(rzi_lorawan_join(1, 0, RZI_LORAWAN_JOIN_INTERVAL_DEFAULT, 0));
	wait_for_callbacks(1);
	zassert_equal(atomic_get(&stats.joined), 1);
	zassert_equal(atomic_get(&stats.join_status), 0);
	zassert_ok(rzi_lorawan_is_joined(&joined));
	zassert_true(joined);
	zassert_true(captured_join.valid);
	zassert_equal(captured_join.mode, LORAWAN_ACT_ABP);
	zassert_equal(captured_join.dev_addr, 0x01020304U);
	{
		uint8_t expected[16];
		uint32_t dev_addr = 1U;

		memset(expected, 0xab, sizeof(expected));
		zassert_mem_equal(captured_join.nwk_skey, expected, sizeof(expected));
		zassert_mem_equal(captured_join.app_skey, expected, sizeof(expected));
		/* The public getter returns the stored ABP address. */
		zassert_ok(rzi_lorawan_get_dev_addr(&dev_addr));
		zassert_equal(dev_addr, 0x01020304U);
	}

	zassert_ok(rzi_lorawan_send(10, payload, sizeof(payload), RZI_LORAWAN_MSG_UNCONFIRMED));
	wait_for_callbacks(1);
	zassert_equal(atomic_get(&stats.uplink), 1);
	zassert_equal(last_uplink_port, 10);
	zassert_equal(last_uplink_len, sizeof(payload));
	zassert_mem_equal(last_uplink, payload, sizeof(payload));

	lorawan_emul_send_downlink(3, false, -80, 6, sizeof(downlink), downlink);
	wait_for_callbacks(1);
	zassert_equal(atomic_get(&stats.downlink), 1);
	zassert_true(atomic_get(&stats.payload_valid));

	zassert_ok(rzi_lorawan_set_class(RZI_LORAWAN_CLASS_A));
	zassert_ok(rzi_lorawan_set_class(RZI_LORAWAN_CLASS_C));
	zassert_equal(rzi_lorawan_set_class(RZI_LORAWAN_CLASS_B), -RZI_ERR_NOT_SUPPORTED);
	zassert_equal(rzi_lorawan_leave(), -RZI_ERR_NOT_SUPPORTED);
	zassert_ok(rzi_lorawan_unregister_callbacks(&callbacks));
}

ZTEST_SUITE(rzi_lorawan_backend_zephyr, NULL, NULL, NULL, NULL, NULL);
