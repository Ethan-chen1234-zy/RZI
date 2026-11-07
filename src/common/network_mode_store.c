/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file
 * @brief Persisted RZI network working mode.
 */

#include <rzi/network_mode.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#if defined(CONFIG_RZI_STORAGE)
#include <rzi/storage/storage.h>
#endif

#define NETWORK_MODE_NAMESPACE  "rzi"
#define NETWORK_MODE_KEY        "network_mode"
#define NETWORK_MODE_RECORD_VER 1U

static bool is_network_mode_valid(enum rzi_network_mode mode)
{
	return (unsigned int)mode <= RZI_NETWORK_MODE_P2P_FSK;
}

int rzi_get_network_mode(enum rzi_network_mode *mode)
{
#if defined(CONFIG_RZI_STORAGE)
	uint8_t stored;
	size_t size;
	uint16_t version;
	int rc;
#endif

	if (k_is_in_isr()) {
		return -RZI_ERR_ISR;
	}
	if (mode == NULL) {
		return -RZI_ERR_INVALID;
	}
#if defined(CONFIG_RZI_STORAGE)
	rc = rzi_storage_read_record(NETWORK_MODE_NAMESPACE, NETWORK_MODE_KEY, &version, &stored,
				     sizeof(stored), &size);
	if (rc != 0) {
		return rc;
	}
	if (version != NETWORK_MODE_RECORD_VER || size != sizeof(stored) ||
	    !is_network_mode_valid((enum rzi_network_mode)stored)) {
		return -RZI_ERR_INVALID;
	}
	*mode = (enum rzi_network_mode)stored;
	return 0;
#else
	return -RZI_ERR_NOT_SUPPORTED;
#endif
}

int rzi_set_network_mode(enum rzi_network_mode mode)
{
	if (k_is_in_isr()) {
		return -RZI_ERR_ISR;
	}
	if (!is_network_mode_valid(mode)) {
		return -RZI_ERR_INVALID;
	}
#if defined(CONFIG_RZI_STORAGE)
	uint8_t stored = (uint8_t)mode;

	return rzi_storage_write_record(NETWORK_MODE_NAMESPACE, NETWORK_MODE_KEY,
					NETWORK_MODE_RECORD_VER, &stored, sizeof(stored));
#else
	return -RZI_ERR_NOT_SUPPORTED;
#endif
}
