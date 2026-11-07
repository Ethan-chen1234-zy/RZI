/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file
 * @brief Eager initialization of shared RZI infrastructure.
 */

#include <rzi/init.h>

#include <zephyr/kernel.h>

#include <rzi/err.h>

#if defined(CONFIG_RZI_STORAGE)
#include <rzi/storage/storage.h>
#endif

int rzi_init(void)
{
	int rc = 0;

	if (k_is_in_isr()) {
		return -RZI_ERR_ISR;
	}

#if defined(CONFIG_RZI_STORAGE)
	rc = rzi_storage_init();
#endif
	return rc;
}
