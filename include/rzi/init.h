/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file
 * @brief Eager initialization of shared RZI infrastructure.
 */
#ifndef RZI_INIT_H
#define RZI_INIT_H

#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup rzi_init RZI shared infrastructure
 * @brief Parameter-free infrastructure shared by RZI services.
 * @since 0.1.0
 * @version 0.1.0
 * @{
 */

/**
 * @brief Eagerly initialize compiled-in shared RZI infrastructure.
 *
 * This initializes only parameter-free infrastructure that is safe to prepare
 * before individual services, currently storage when enabled. It does not
 * initialize LoRaWAN, raw LoRa, AT, or any service that requires application
 * configuration or owns an active hardware resource.
 *
 * Service APIs may initialize their own dependencies lazily, so calling this
 * function is optional. Applications can call it during boot to detect shared
 * infrastructure failures early.
 *
 * Repeated calls are idempotent. After a failed call, a later call retries
 * initialization.
 *
 * @retval 0 Shared infrastructure initialized.
 * @retval -RZI_ERR_ISR Called from an ISR.
 * @retval negative The first infrastructure initialization error.
 *
 * @note Thread context only.
 */
__must_check int rzi_init(void);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* RZI_INIT_H */
