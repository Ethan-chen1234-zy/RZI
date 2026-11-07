/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file
 * @brief Network working-mode persistence API.
 */
#ifndef RZI_NETWORK_MODE_H
#define RZI_NETWORK_MODE_H

#include <zephyr/toolchain.h>

#include <rzi/err.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup rzi_network_mode RZI network-mode persistence
 *  @brief Persist the radio working mode independently of command adapters.
 *  @since 0.5
 *  @version 0.5.0
 *  @{
 */

/** Radio network working mode persisted for the next service startup. */
enum rzi_network_mode {
	/** Raw point-to-point LoRa modulation. */
	RZI_NETWORK_MODE_P2P_LORA,
	/** LoRaWAN network operation. */
	RZI_NETWORK_MODE_LORAWAN,
	/** Raw point-to-point FSK modulation. */
	RZI_NETWORK_MODE_P2P_FSK,
};

/**
 * @brief Read the persisted radio network working mode.
 *
 * This API owns the storage namespace, key, representation, and validation.
 * It does not start or stop a radio service.
 * Storage errors propagate unchanged.
 *
 * @param[out] mode Destination for the stored mode.
 *
 * @retval 0 Stored mode returned.
 * @retval -RZI_ERR_INVALID mode is NULL or the stored value is invalid.
 * @retval -RZI_ERR_NOT_FOUND No mode has been stored.
 * @retval -RZI_ERR_NOT_SUPPORTED Storage is not enabled.
 * @retval -RZI_ERR_ISR Called from an ISR.
 * @retval -RZI_ERR_IO Storage access failed.
 * @note Thread context only.
 * @since 0.5
 */
__must_check int rzi_get_network_mode(enum rzi_network_mode *mode);

/**
 * @brief Persist the radio network working mode.
 *
 * The caller remains responsible for coordinating active radio services.
 * Storage errors propagate unchanged.
 *
 * @param mode Mode to persist.
 *
 * @retval 0 Mode stored.
 * @retval -RZI_ERR_INVALID mode is outside enum rzi_network_mode.
 * @retval -RZI_ERR_NOT_SUPPORTED Storage is not enabled.
 * @retval -RZI_ERR_ISR Called from an ISR.
 * @retval -RZI_ERR_IO Storage access failed.
 * @note Thread context only.
 * @since 0.5
 */
__must_check int rzi_set_network_mode(enum rzi_network_mode mode);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* RZI_NETWORK_MODE_H */
