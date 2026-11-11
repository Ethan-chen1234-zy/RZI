/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file
 * @brief This sample's port of the uplink log library onto RZI and Zephyr.
 */
#ifndef UPLINK_LOG_RZI_H
#define UPLINK_LOG_RZI_H

#include <stdint.h>
#include <rzi/lorawan/lorawan.h>

#include <uplink_log.h>

/** Register the RZI port with the library. Call once from the main thread. */
int uplink_log_rzi_init(const struct uplink_log_options *options);

/**
 * Forward every LoRaWAN event. Runs in the RZI dispatcher thread; it only
 * records state and wakes the thread blocked in uplink_log_rzi_poll().
 */
void uplink_log_rzi_on_lorawan_event(const struct rzi_lorawan_event *event);

/**
 * Deliver the last transmit result, run the library, then sleep until the
 * library needs another pass, a wakeup arrives or max_wait_ms elapses.
 */
void uplink_log_rzi_poll(uint32_t max_wait_ms);

/** Wake the thread blocked in uplink_log_rzi_poll(). */
void uplink_log_rzi_wakeup(void);

/** RSSI in dBm of the last downlink, or -180 before any downlink. */
int16_t uplink_log_rzi_last_rssi(void);

/** SNR in dB of the last downlink. */
int8_t uplink_log_rzi_last_snr(void);

/** Uplinks reported as not sent since boot, saturating at 255. */
uint8_t uplink_log_rzi_tx_fail_count(void);

#endif /* UPLINK_LOG_RZI_H */
