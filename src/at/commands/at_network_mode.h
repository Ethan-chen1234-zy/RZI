/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file
 * @brief Shared AT+NWM working-mode state.
 */
#ifndef RZI_AT_NETWORK_MODE_H
#define RZI_AT_NETWORK_MODE_H

#include <rzi/network_mode.h>

/** Notify a package after AT+NWM has been written. Return 0 or a negative error. */
typedef int (*rzi_at_network_mode_changed_t)(enum rzi_network_mode mode);

/** Return the current working mode. */
enum rzi_network_mode rzi_at_get_network_mode(void);

/** Subscribe to working-mode changes. @p listener must have static lifetime. */
int rzi_at_add_network_mode_listener(rzi_at_network_mode_changed_t listener);

/** Register AT+NWM and load any persisted working mode. */
int rzi_at_network_mode_register(void);

/**
 * Store the build default and update the AT working mode after success.
 * Used when P2P radio initialization fails, so the next boot still brings AT up.
 */
int rzi_at_restore_default_network_mode(void);

#endif /* RZI_AT_NETWORK_MODE_H */
