/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

#include <zephyr/ztest.h>

#include <rzi/err.h>
#include <rzi/init.h>
#if defined(CONFIG_RZI_STORAGE)
#include <rzi/storage/storage.h>
#endif

ZTEST(rzi_system_init, test_init_is_idempotent)
{
	zassert_ok(rzi_init());
	zassert_ok(rzi_init());
}

#if defined(CONFIG_RZI_STORAGE)
ZTEST(rzi_system_init, test_init_prepares_storage)
{
	uint8_t value = 0U;

	zassert_ok(rzi_init());
	zassert_equal(rzi_storage_read("rzi", "missing", &value, sizeof(value)),
		      -RZI_ERR_NOT_FOUND);
}
#endif

ZTEST_SUITE(rzi_system_init, NULL, NULL, NULL, NULL, NULL);
