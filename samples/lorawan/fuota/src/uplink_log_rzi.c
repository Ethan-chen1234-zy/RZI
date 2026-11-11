/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/atomic.h>
#include <rzi/err.h>
#include <rzi/lorawan/lorawan.h>

#include <uplink_log_port.h>

#include "uplink_log_rzi.h"

LOG_MODULE_REGISTER(uplink_log_rzi, LOG_LEVEL_INF);

/*
 * The log state lives in raw flash inside storage_partition, written as an
 * append-only sequence of fixed slots and erased only when the region is full.
 */
#define STORAGE_SIZE DT_REG_SIZE(DT_NODELABEL(storage_partition))
#if DT_HAS_CHOSEN(lora_basics_modem_context_partition)
/* LBM keeps its context in its own partition. */
#define LOG_REGION_OFFSET 0
#define LOG_REGION_SIZE   STORAGE_SIZE
#else
/* LBM writes its context to the first 8 KiB of storage_partition. */
BUILD_ASSERT(!IS_ENABLED(CONFIG_LORA_BASICS_MODEM_STORE_AND_FORWARD),
	     "LBM store-and-forward uses the rest of storage_partition");
#define LOG_REGION_SIZE   4096
#define LOG_REGION_OFFSET (STORAGE_SIZE - LOG_REGION_SIZE)
BUILD_ASSERT(LOG_REGION_OFFSET >= 8192, "storage_partition too small for LBM and the uplink log");
#endif

#define SLOT_SIZE   64U
#define SLOT_COUNT  (LOG_REGION_SIZE / SLOT_SIZE)
#define SLOT_MAGIC  0xA5U
#define SLOT_HEADER 3U
BUILD_ASSERT(SLOT_HEADER + UPLINK_LOG_PERSIST_MAX_SIZE <= SLOT_SIZE);
BUILD_ASSERT(UPLINK_LOG_PERSIST_MAX_SIZE <= UINT8_MAX);

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

static const struct flash_area *log_area;
/* First erased slot; slots before it hold data or foreign bytes. */
static uint32_t next_slot;

static off_t slot_offset(uint32_t slot)
{
	return LOG_REGION_OFFSET + (off_t)slot * SLOT_SIZE;
}

static bool slot_valid(const uint8_t *header)
{
	return header[0] == SLOT_MAGIC && header[1] == (uint8_t)~header[2] &&
	       header[1] <= UPLINK_LOG_PERSIST_MAX_SIZE;
}

static int area_open(void)
{
	uint8_t erased;
	uint8_t header[SLOT_HEADER];
	int rc;

	rc = flash_area_open(PARTITION_ID(storage_partition), &log_area);
	if (rc != 0) {
		return rc;
	}
	if (SLOT_SIZE % flash_area_align(log_area) != 0U) {
		flash_area_close(log_area);
		log_area = NULL;
		return -ENOTSUP;
	}
	erased = flash_area_erased_val(log_area);
	for (next_slot = 0U; next_slot < SLOT_COUNT; next_slot++) {
		rc = flash_area_read(log_area, slot_offset(next_slot), header, sizeof(header));
		if (rc != 0) {
			flash_area_close(log_area);
			log_area = NULL;
			return rc;
		}
		if (header[0] == erased && header[1] == erased && header[2] == erased) {
			break;
		}
	}
	return 0;
}

static int port_persist_load(void *ctx, uint8_t *buf, size_t capacity, size_t *size)
{
	uint8_t header[SLOT_HEADER];

	ARG_UNUSED(ctx);
	if (log_area == NULL) {
		return -ENODEV;
	}
	for (uint32_t slot = next_slot; slot-- > 0U;) {
		off_t offset = slot_offset(slot);

		if (flash_area_read(log_area, offset, header, sizeof(header)) != 0 ||
		    !slot_valid(header)) {
			continue;
		}
		if (header[1] > capacity) {
			return -ENOSPC;
		}
		*size = header[1];
		return flash_area_read(log_area, offset + SLOT_HEADER, buf, header[1]);
	}
	return -ENOENT;
}

static int port_persist_save(void *ctx, const uint8_t *buf, size_t size)
{
	uint8_t slot[SLOT_SIZE];
	int rc;

	ARG_UNUSED(ctx);
	if (log_area == NULL) {
		return -ENODEV;
	}
	if (next_slot >= SLOT_COUNT) {
		rc = flash_area_erase(log_area, LOG_REGION_OFFSET, LOG_REGION_SIZE);
		if (rc != 0) {
			LOG_WRN("Log state erase failed: %d", rc);
			return rc;
		}
		next_slot = 0U;
	}
	memset(slot, flash_area_erased_val(log_area), sizeof(slot));
	slot[0] = SLOT_MAGIC;
	slot[1] = (uint8_t)size;
	slot[2] = (uint8_t)~size;
	memcpy(&slot[SLOT_HEADER], buf, size);
	rc = flash_area_write(log_area, slot_offset(next_slot), slot, sizeof(slot));
	/* A failed write may leave the slot partly programmed; never reuse it. */
	next_slot++;
	if (rc != 0) {
		LOG_WRN("Log state not saved: %d", rc);
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
	.persist_load = port_persist_load,
	.persist_save = port_persist_save,
	.lock = port_lock,
	.unlock = port_unlock,
	.wakeup = port_wakeup,
};

int uplink_log_rzi_init(const struct uplink_log_options *options)
{
	int rc = area_open();

	if (rc != 0) {
		LOG_WRN("Log storage unavailable, state is not persisted: %d", rc);
	}
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
