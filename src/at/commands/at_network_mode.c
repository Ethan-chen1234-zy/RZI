/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file
 * @brief Shared AT+NWM working-mode state and command.
 */

#include <errno.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#include "../at_priv.h"
#include "at_network_mode.h"

#define RZI_ATCMD_NWM "NWM"

#define LISTENER_MAX 2U
#define DEFAULT_NETWORK_MODE                                                                       \
	(IS_ENABLED(CONFIG_RZI_AT_COMMAND_LORAWAN) ? RZI_NETWORK_MODE_LORAWAN                      \
						   : RZI_NETWORK_MODE_P2P_LORA)

static enum rzi_network_mode network_mode = DEFAULT_NETWORK_MODE;
static rzi_at_network_mode_changed_t listeners[LISTENER_MAX];
static size_t listener_count;

static int parse_long(const char *argument, long *value)
{
	char *end = NULL;

	if (argument == NULL || value == NULL) {
		return -RZI_ERR_INVALID;
	}
	*value = strtol(argument, &end, 10);
	if (end == argument || *end != '\0') {
		return -RZI_ERR_INVALID;
	}
	return 0;
}

static int notify_listeners(enum rzi_network_mode mode)
{
	int rc = 0;

	for (size_t i = 0; i < listener_count; ++i) {
		int one = listeners[i](mode);

		if (one != 0 && rc == 0) {
			rc = one;
		}
	}
	return rc;
}

#if defined(CONFIG_RZI_AT_NVM)
static int load_mode(void)
{
	enum rzi_network_mode stored;
	int rc = rzi_get_network_mode(&stored);

	if (rc == -RZI_ERR_NOT_FOUND) {
		return 0;
	}
	if (rc != 0) {
		return rc;
	}
	network_mode = stored;
	return 0;
}

static int save_mode(enum rzi_network_mode mode)
{
	return rzi_set_network_mode(mode);
}
#else
static int load_mode(void)
{
	return 0;
}

static int save_mode(enum rzi_network_mode mode)
{
	ARG_UNUSED(mode);
	return 0;
}
#endif

enum rzi_network_mode rzi_at_get_network_mode(void)
{
	return network_mode;
}

int rzi_at_add_network_mode_listener(rzi_at_network_mode_changed_t listener)
{
	if (listener == NULL) {
		return -RZI_ERR_INVALID;
	}
	if (listener_count >= ARRAY_SIZE(listeners)) {
		return -RZI_ERR_NO_RESOURCE;
	}
	listeners[listener_count++] = listener;
	return 0;
}

static int handle_nwm(const struct rzi_at_request *request, void *user_data)
{
	long parsed;
	int rc;

	ARG_UNUSED(user_data);
	if (request->operation == RZI_AT_OP_READ) {
		return rzi_at_respond_value(RZI_AT_VALUE(RZI_ATCMD_NWM, "%u"), network_mode);
	}
	rc = parse_long(request->argument, &parsed);
	if (rc != 0 || parsed < 0 || parsed > (long)RZI_NETWORK_MODE_P2P_FSK) {
		return -RZI_ERR_INVALID;
	}
	if ((enum rzi_network_mode)parsed != RZI_NETWORK_MODE_LORAWAN &&
	    !IS_ENABLED(CONFIG_RZI_AT_COMMAND_LORA)) {
		return -RZI_ERR_NOT_SUPPORTED;
	}
	if ((enum rzi_network_mode)parsed == network_mode) {
		return rzi_at_respond_status(RZI_AT_STATUS_OK);
	}
	/*
	 * Start or stop the radio before persisting. A fault during USP init
	 * must not leave P2P mode in flash, or every later boot faults too.
	 */
	{
		enum rzi_network_mode previous = network_mode;

		network_mode = (enum rzi_network_mode)parsed;
		rc = notify_listeners(network_mode);
		if (rc == 0) {
			rc = save_mode(network_mode);
		}
		if (rc != 0) {
			network_mode = previous;
			(void)notify_listeners(network_mode);
			return rc;
		}
	}
	rc = rzi_at_respond_status(RZI_AT_STATUS_OK);
	if (IS_ENABLED(CONFIG_RZI_AT_COMMAND_LORAWAN) && !IS_ENABLED(CONFIG_ZTEST)) {
		sys_reboot(SYS_REBOOT_COLD);
	}
	return rc;
}

static const struct rzi_at_command nwm_command = {
	.name = RZI_ATCMD_NWM,
	.help = "get or set the network working mode (0 = P2P_LORA, 1 = LoRaWAN, 2 = P2P_FSK)",
	.allowed_operations = RZI_AT_ALLOW_READ | RZI_AT_ALLOW_WRITE,
	.handler = handle_nwm,
};

#if defined(CONFIG_RZI_AT_NVM)
static int extension_factory_reset(void)
{
	return save_mode(DEFAULT_NETWORK_MODE);
}

static const struct rzi_at_extension extension = {
	.factory_reset = extension_factory_reset,
};
#endif

int rzi_at_restore_default_network_mode(void)
{
	int rc = save_mode(DEFAULT_NETWORK_MODE);

	if (rc == 0) {
		network_mode = DEFAULT_NETWORK_MODE;
	}
	return rc;
}

int rzi_at_network_mode_register(void)
{
	int rc = load_mode();

	if (rc != 0) {
		return rc;
	}
	rc = rzi_at_register(&nwm_command, 1);
#if defined(CONFIG_RZI_AT_NVM)
	if (rc == 0) {
		rc = rzi_at_registry_add_extension(&extension);
	}
#endif
	return rc;
}
