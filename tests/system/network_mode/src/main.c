/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/irq_offload.h>
#include <zephyr/ztest.h>

#include <rzi/network_mode.h>
#if defined(CONFIG_RZI_STORAGE)
#include <rzi/storage/storage.h>
#endif

static void reset_storage(void *fixture)
{
	ARG_UNUSED(fixture);
#if defined(CONFIG_RZI_STORAGE)
	/* Test fixture cleanup; production callers use only the typed get/set API. */
	int rc = rzi_storage_delete("rzi", "network_mode");

	zassert_true(rc == 0 || rc == -RZI_ERR_NOT_FOUND);
#endif
}

ZTEST(rzi_network_mode, test_invalid_arguments)
{
	zassert_equal(rzi_get_network_mode(NULL), -RZI_ERR_INVALID);
	zassert_equal(rzi_set_network_mode((enum rzi_network_mode) - 1), -RZI_ERR_INVALID);
	zassert_equal(rzi_set_network_mode((enum rzi_network_mode)3), -RZI_ERR_INVALID);
}

static int isr_get_result;
static int isr_set_result;

static void access_from_isr(const void *unused)
{
	enum rzi_network_mode mode;

	ARG_UNUSED(unused);
	isr_get_result = rzi_get_network_mode(&mode);
	isr_set_result = rzi_set_network_mode(RZI_NETWORK_MODE_P2P_LORA);
}

ZTEST(rzi_network_mode, test_isr_access_is_rejected)
{
	irq_offload(access_from_isr, NULL);
	zassert_equal(isr_get_result, -RZI_ERR_ISR);
	zassert_equal(isr_set_result, -RZI_ERR_ISR);
}

#if !defined(CONFIG_RZI_STORAGE)
ZTEST(rzi_network_mode, test_storage_disabled)
{
	enum rzi_network_mode mode = RZI_NETWORK_MODE_P2P_FSK;

	zassert_equal(rzi_get_network_mode(&mode), -RZI_ERR_NOT_SUPPORTED);
	zassert_equal(mode, RZI_NETWORK_MODE_P2P_FSK);
	zassert_equal(rzi_set_network_mode(RZI_NETWORK_MODE_LORAWAN), -RZI_ERR_NOT_SUPPORTED);
}
#else
ZTEST(rzi_network_mode, test_missing_record_preserves_output)
{
	enum rzi_network_mode mode = RZI_NETWORK_MODE_P2P_FSK;

	zassert_equal(rzi_get_network_mode(&mode), -RZI_ERR_NOT_FOUND);
	zassert_equal(mode, RZI_NETWORK_MODE_P2P_FSK);
}
#endif

#if defined(CONFIG_SETTINGS_NVS)
ZTEST(rzi_network_mode, test_each_mode_round_trips)
{
	const enum rzi_network_mode modes[] = {
		RZI_NETWORK_MODE_P2P_LORA,
		RZI_NETWORK_MODE_LORAWAN,
		RZI_NETWORK_MODE_P2P_FSK,
	};
	enum rzi_network_mode stored;

	for (size_t i = 0; i < ARRAY_SIZE(modes); ++i) {
		zassert_ok(rzi_set_network_mode(modes[i]));
		zassert_equal(rzi_set_network_mode((enum rzi_network_mode)3), -RZI_ERR_INVALID);
		zassert_ok(rzi_get_network_mode(&stored));
		zassert_equal(stored, modes[i]);
	}
}

ZTEST(rzi_network_mode, test_invalid_record_preserves_output)
{
	uint8_t invalid = 3U;
	enum rzi_network_mode mode = RZI_NETWORK_MODE_P2P_FSK;

	zassert_ok(rzi_storage_write_record("rzi", "network_mode", 1U, &invalid, sizeof(invalid)));
	zassert_equal(rzi_get_network_mode(&mode), -RZI_ERR_INVALID);
	zassert_equal(mode, RZI_NETWORK_MODE_P2P_FSK);
}
#endif

ZTEST_SUITE(rzi_network_mode, NULL, NULL, reset_storage, NULL, NULL);
