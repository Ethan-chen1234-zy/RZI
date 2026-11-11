/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <rzi/err.h>
#include <rzi/lorawan/lorawan.h>

#include <uplink_log_port.h>

#include "uplink_log_rzi.h"

LOG_MODULE_REGISTER(uplink_log_rzi, LOG_LEVEL_INF);

/* Zero means "no result"; otherwise enum uplink_log_tx_result + 1. */
#define NO_TX_RESULT 0

static K_MUTEX_DEFINE(lib_lock);
static K_SEM_DEFINE(wake, 0, 1);

/* Set once rzi_lorawan_send() accepts a log frame: the next TXDONE is ours. */
static atomic_t log_tx_outstanding;
static atomic_t tx_result = ATOMIC_INIT(NO_TX_RESULT);
static atomic_t tx_fail_count;
static atomic_t last_rssi = ATOMIC_INIT(-180);
static atomic_t last_snr;

static uint32_t port_uptime_sec(void *ctx)
{
	ARG_UNUSED(ctx);
	return k_uptime_seconds();
}

static int port_send(void *ctx, const uint8_t *frame, size_t size, bool confirmed)
{
	int rc;

	ARG_UNUSED(ctx);
	rc = rzi_lorawan_send(UPLINK_LOG_FPORT, frame, size,
			      confirmed ? RZI_LORAWAN_MSG_CONFIRMED : RZI_LORAWAN_MSG_UNCONFIRMED);
	if (rc == 0) {
		atomic_set(&log_tx_outstanding, 1);
		LOG_INF("Log uplink type 0x%02x, %s", frame[2],
			confirmed ? "confirmed" : "unconfirmed");
	} else if (rc != -RZI_ERR_BUSY) {
		LOG_DBG("Log uplink deferred: %s", rzi_err_str(rc));
	}
	return rc;
}

static void port_lock(void *ctx)
{
	ARG_UNUSED(ctx);
	k_mutex_lock(&lib_lock, K_FOREVER);
}

static void port_unlock(void *ctx)
{
	ARG_UNUSED(ctx);
	k_mutex_unlock(&lib_lock);
}

static void port_wakeup(void *ctx)
{
	ARG_UNUSED(ctx);
	k_sem_give(&wake);
}

static const struct uplink_log_port port = {
	.uptime_sec = port_uptime_sec,
	.send = port_send,
	.lock = port_lock,
	.unlock = port_unlock,
	.wakeup = port_wakeup,
};

int uplink_log_rzi_init(const struct uplink_log_options *options)
{
	return uplink_log_init(&port, options);
}

static enum uplink_log_tx_result map_tx_result(const struct rzi_lorawan_tx_result *tx)
{
	if (tx->error != 0) {
		return UPLINK_LOG_TX_FAILED;
	}
	switch (tx->status) {
	case RZI_LORAWAN_EVENT_TXDONE_CONFIRMED:
		return UPLINK_LOG_TX_ACKED;
	case RZI_LORAWAN_EVENT_TXDONE_SENT:
		return UPLINK_LOG_TX_SENT;
	default:
		return UPLINK_LOG_TX_FAILED;
	}
}

static void set_link(bool up)
{
	int rc = uplink_log_set_link(up);

	if (rc != UPLINK_LOG_OK) {
		LOG_WRN("Log link update failed: %d", rc);
	}
}

void uplink_log_rzi_on_lorawan_event(const struct rzi_lorawan_event *event)
{
	switch (event->type) {
	case RZI_LORAWAN_EVENT_JOINED:
		set_link(true);
		break;
	case RZI_LORAWAN_EVENT_JOIN_FAILED:
		set_link(false);
		break;
	case RZI_LORAWAN_EVENT_STATE_CHANGED:
		if (event->state != RZI_LORAWAN_STATE_JOINED) {
			set_link(false);
		}
		break;
	case RZI_LORAWAN_EVENT_TXDONE:
		if (event->tx.status == RZI_LORAWAN_EVENT_TXDONE_NOT_SENT &&
		    atomic_get(&tx_fail_count) < UINT8_MAX) {
			atomic_inc(&tx_fail_count);
		}
		if (atomic_cas(&log_tx_outstanding, 1, 0)) {
			atomic_set(&tx_result, (atomic_val_t)map_tx_result(&event->tx) + 1);
			k_sem_give(&wake);
		}
		break;
	case RZI_LORAWAN_EVENT_DOWNDATA:
		atomic_set(&last_rssi, event->downlink.rssi_dbm);
		atomic_set(&last_snr, event->downlink.snr_quarter_db / 4);
		break;
	default:
		break;
	}
}

void uplink_log_rzi_poll(uint32_t max_wait_ms)
{
	atomic_val_t result = atomic_set(&tx_result, NO_TX_RESULT);
	uint64_t wait_ms = max_wait_ms;
	uint32_t next;

	if (result != NO_TX_RESULT) {
		(void)uplink_log_on_tx_result((enum uplink_log_tx_result)(result - 1));
	}
	next = uplink_log_process();
	if (next != UPLINK_LOG_NO_DEADLINE) {
		wait_ms = MIN(wait_ms, (uint64_t)next * MSEC_PER_SEC);
	}
	(void)k_sem_take(&wake, K_MSEC(wait_ms));
}

void uplink_log_rzi_wakeup(void)
{
	k_sem_give(&wake);
}

int16_t uplink_log_rzi_last_rssi(void)
{
	return (int16_t)atomic_get(&last_rssi);
}

int8_t uplink_log_rzi_last_snr(void)
{
	return (int8_t)atomic_get(&last_snr);
}

uint8_t uplink_log_rzi_tx_fail_count(void)
{
	return (uint8_t)atomic_get(&tx_fail_count);
}
