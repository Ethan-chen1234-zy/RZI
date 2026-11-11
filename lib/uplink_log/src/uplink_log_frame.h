/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file
 * @brief Internal 11-byte frame encoders.
 */
#ifndef UPLINK_LOG_FRAME_H
#define UPLINK_LOG_FRAME_H

#include "uplink_log.h"

#define UPLINK_LOG_RETRY_MAX   15U
#define UPLINK_LOG_ELAPSED_MAX 0xFFFFU

uint8_t uplink_log_frame_version_byte(void);

void uplink_log_frame_boot(uint8_t frame[UPLINK_LOG_FRAME_SIZE], uint8_t device_code,
			   const struct uplink_log_boot *boot);

void uplink_log_frame_periodic(uint8_t frame[UPLINK_LOG_FRAME_SIZE], uint8_t device_code,
			       uint8_t count, const struct uplink_log_periodic *periodic);

/**
 * Event and config frames share one layout: elapsed_sec, a byte holding the
 * retry count over a 4-bit field (domain or status), one code byte and four
 * data bytes.
 */
void uplink_log_frame_record(uint8_t frame[UPLINK_LOG_FRAME_SIZE], uint8_t device_code,
			     uint8_t type, uint32_t elapsed_sec, uint8_t retry, uint8_t nibble,
			     uint8_t code, const uint8_t data[4]);

void uplink_log_frame_custom(uint8_t frame[UPLINK_LOG_FRAME_SIZE], uint8_t device_code,
			     const uint8_t payload[UPLINK_LOG_PAYLOAD_SIZE]);

uint8_t uplink_log_frame_rssi(int16_t rssi_dbm);

uint16_t uplink_log_crc16(const uint8_t *data, size_t size);

#endif /* UPLINK_LOG_FRAME_H */
