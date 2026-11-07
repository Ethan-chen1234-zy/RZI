/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file
 * @brief LoRaWAN region, class, and MAC-parameter AT commands.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include "at_lorawan_priv.h"

#define RZI_ATCMD_BAND        "BAND"
#define RZI_ATCMD_CLASS       "CLASS"
#define RZI_ATCMD_ADR         "ADR"
#define RZI_ATCMD_DCS         "DCS"
#define RZI_ATCMD_DR          "DR"
#define RZI_ATCMD_TXP         "TXP"
#define RZI_ATCMD_PNM         "PNM"
#define RZI_ATCMD_RX1DL       "RX1DL"
#define RZI_ATCMD_RX2DL       "RX2DL"
#define RZI_ATCMD_RX2DR       "RX2DR"
#define RZI_ATCMD_RX2FQ       "RX2FQ"
#define RZI_ATCMD_JN1DL       "JN1DL"
#define RZI_ATCMD_JN2DL       "JN2DL"
#define RZI_ATCMD_LBT         "LBT"
#define RZI_ATCMD_LBTRSSI     "LBTRSSI"
#define RZI_ATCMD_LBTSCANTIME "LBTSCANTIME"
#define RZI_ATCMD_LINKCHECK   "LINKCHECK"
#define RZI_ATCMD_TIMEREQ     "TIMEREQ"

static int class_supported(enum rzi_lorawan_class device_class)
{
	uint32_t capabilities = rzi_lorawan_get_capabilities();

	switch (device_class) {
	case RZI_LORAWAN_CLASS_A:
		return (capabilities & RZI_LORAWAN_CAP_CLASS_A) != 0U ? 0 : -RZI_ERR_NOT_SUPPORTED;
	case RZI_LORAWAN_CLASS_B:
		return (capabilities & RZI_LORAWAN_CAP_CLASS_B) != 0U ? 0 : -RZI_ERR_NOT_SUPPORTED;
	case RZI_LORAWAN_CLASS_C:
		return (capabilities & RZI_LORAWAN_CAP_CLASS_C) != 0U ? 0 : -RZI_ERR_NOT_SUPPORTED;
	default:
		return -RZI_ERR_INVALID;
	}
}

static char class_letter(enum rzi_lorawan_class device_class)
{
	switch (device_class) {
	case RZI_LORAWAN_CLASS_B:
		return 'B';
	case RZI_LORAWAN_CLASS_C:
		return 'C';
	default:
		return 'A';
	}
}

static int handle_bool(const struct rzi_at_request *request, const char *name, int (*get)(bool *),
		       int (*set)(bool))
{
	bool enabled;
	int rc;

	if (request->operation == RZI_AT_OP_READ) {
		rc = get(&enabled);
		return rc != 0 ? rc
			       : rzi_at_respond_value(RZI_AT_VALUE("%s", "%d"), name,
						      enabled ? 1 : 0);
	}
	rc = rzi_at_lorawan_parse_bool(request->argument, &enabled);
	if (rc != 0) {
		return rc;
	}
	rc = set(enabled);
	return rc != 0 ? rc : rzi_at_respond_status(RZI_AT_STATUS_OK);
}

static int handle_u32(const struct rzi_at_request *request, const char *name,
		      int (*get)(uint32_t *), int (*set)(uint32_t), uint32_t scale)
{
	uint32_t value;
	long parsed;
	int rc;

	if (request->operation == RZI_AT_OP_READ) {
		rc = get(&value);
		return rc != 0 ? rc
			       : rzi_at_respond_value(RZI_AT_VALUE("%s", "%u"), name,
						      value / scale);
	}
	if (set == NULL) {
		return -RZI_ERR_NOT_SUPPORTED;
	}
	rc = rzi_at_lorawan_parse_long(request->argument, &parsed);
	if (rc != 0 || parsed <= 0) {
		return -RZI_ERR_INVALID;
	}
	rc = set((uint32_t)parsed * scale);
	return rc != 0 ? rc : rzi_at_respond_status(RZI_AT_STATUS_OK);
}

static int handle_band(const struct rzi_at_request *request, void *user_data)
{
	struct rzi_at_lorawan_context *context = &rzi_at_lorawan_context;

	ARG_UNUSED(user_data);
	if (request->operation == RZI_AT_OP_READ) {
		enum rzi_lorawan_region region;
		int rc = rzi_lorawan_get_region(&region);

		return rc != 0 ? rc
			       : rzi_at_respond_value(RZI_AT_VALUE(RZI_ATCMD_BAND, "%d"),
						      rzi_at_lorawan_region_to_band(region));
	}
	if (context->service_started) {
		return -RZI_ERR_BUSY;
	}

	char *end = NULL;
	long band = strtol(request->argument, &end, 10);
	enum rzi_lorawan_region region;
	int rc;

	if (end == request->argument || *end != '\0') {
		return -RZI_ERR_INVALID;
	}
	rc = rzi_at_lorawan_band_to_region((int)band, &region);
	if (rc != 0) {
		return rc;
	}

	/* The core needs the region before init for the channel-mask commands. */
	rc = rzi_lorawan_set_region(region);
	if (rc != 0) {
		return rc;
	}
	return rzi_at_respond_status(RZI_AT_STATUS_OK);
}

static int handle_class(const struct rzi_at_request *request, void *user_data)
{
	struct rzi_at_lorawan_context *context = &rzi_at_lorawan_context;
	enum rzi_lorawan_class device_class;
	int rc;

	ARG_UNUSED(user_data);
	if (request->operation == RZI_AT_OP_READ) {
		if (context->service_started) {
			rc = rzi_lorawan_get_class(&device_class);
			if (rc != 0) {
				return rc;
			}
			context->device_class = device_class;
		}
		return rzi_at_respond_value(RZI_AT_VALUE(RZI_ATCMD_CLASS, "%c"),
					    class_letter(context->device_class));
	}
	if (strcmp(request->argument, "A") == 0) {
		device_class = RZI_LORAWAN_CLASS_A;
	} else if (strcmp(request->argument, "B") == 0) {
		device_class = RZI_LORAWAN_CLASS_B;
	} else if (strcmp(request->argument, "C") == 0) {
		device_class = RZI_LORAWAN_CLASS_C;
	} else {
		return -RZI_ERR_INVALID;
	}
	rc = class_supported(device_class);
	if (rc != 0) {
		return rc;
	}
	if (context->service_started) {
		rc = rzi_lorawan_set_class(device_class);
		if (rc != 0) {
			return rc;
		}
	}
	context->device_class = device_class;
	return rzi_at_respond_status(RZI_AT_STATUS_OK);
}

static int handle_adr(const struct rzi_at_request *request, void *user_data)
{
	ARG_UNUSED(user_data);
	return handle_bool(request, RZI_ATCMD_ADR, rzi_lorawan_get_adr, rzi_lorawan_set_adr);
}

static int handle_dcs(const struct rzi_at_request *request, void *user_data)
{
	ARG_UNUSED(user_data);
	return handle_bool(request, RZI_ATCMD_DCS, rzi_lorawan_get_duty_cycle,
			   rzi_lorawan_set_duty_cycle);
}

static int handle_pnm(const struct rzi_at_request *request, void *user_data)
{
	ARG_UNUSED(user_data);
	return handle_bool(request, RZI_ATCMD_PNM, rzi_lorawan_get_public_network,
			   rzi_lorawan_set_public_network);
}

static int handle_lbt(const struct rzi_at_request *request, void *user_data)
{
	ARG_UNUSED(user_data);
	return handle_bool(request, RZI_ATCMD_LBT, rzi_lorawan_get_lbt, rzi_lorawan_set_lbt);
}

static int handle_dr(const struct rzi_at_request *request, void *user_data)
{
	enum rzi_lorawan_data_rate data_rate;
	long parsed;
	int rc;

	ARG_UNUSED(user_data);
	if (request->operation == RZI_AT_OP_READ) {
		rc = rzi_lorawan_get_data_rate(&data_rate);
		return rc != 0 ? rc
			       : rzi_at_respond_value(RZI_AT_VALUE(RZI_ATCMD_DR, "%u"),
						      (unsigned int)data_rate);
	}
	rc = rzi_at_lorawan_parse_long(request->argument, &parsed);
	if (rc != 0 || parsed < 0 || parsed > 15) {
		return -RZI_ERR_INVALID;
	}
	rc = rzi_lorawan_set_data_rate((enum rzi_lorawan_data_rate)parsed);
	return rc != 0 ? rc : rzi_at_respond_status(RZI_AT_STATUS_OK);
}

static int handle_txp(const struct rzi_at_request *request, void *user_data)
{
	uint8_t tx_power;
	long parsed;
	int rc;

	ARG_UNUSED(user_data);
	if (request->operation == RZI_AT_OP_READ) {
		rc = rzi_lorawan_get_tx_power(&tx_power);
		return rc != 0 ? rc
			       : rzi_at_respond_value(RZI_AT_VALUE(RZI_ATCMD_TXP, "%u"), tx_power);
	}
	rc = rzi_at_lorawan_parse_long(request->argument, &parsed);
	if (rc != 0 || parsed < 0 || parsed > 15) {
		return -RZI_ERR_INVALID;
	}
	rc = rzi_lorawan_set_tx_power((uint8_t)parsed);
	return rc != 0 ? rc : rzi_at_respond_status(RZI_AT_STATUS_OK);
}

static int handle_rx1dl(const struct rzi_at_request *request, void *user_data)
{
	ARG_UNUSED(user_data);
	return handle_u32(request, RZI_ATCMD_RX1DL, rzi_lorawan_get_rx1_delay,
			  rzi_lorawan_set_rx1_delay, 1000U);
}

static int handle_rx2dl(const struct rzi_at_request *request, void *user_data)
{
	ARG_UNUSED(user_data);
	return handle_u32(request, RZI_ATCMD_RX2DL, rzi_lorawan_get_rx2_delay,
			  rzi_lorawan_set_rx2_delay, 1000U);
}

static int handle_jn1dl(const struct rzi_at_request *request, void *user_data)
{
	ARG_UNUSED(user_data);
	return handle_u32(request, RZI_ATCMD_JN1DL, rzi_lorawan_get_join_accept_delay1,
			  rzi_lorawan_set_join_accept_delay1, 1000U);
}

static int handle_jn2dl(const struct rzi_at_request *request, void *user_data)
{
	ARG_UNUSED(user_data);
	return handle_u32(request, RZI_ATCMD_JN2DL, rzi_lorawan_get_join_accept_delay2,
			  rzi_lorawan_set_join_accept_delay2, 1000U);
}

static int handle_rx2dr(const struct rzi_at_request *request, void *user_data)
{
	enum rzi_lorawan_data_rate data_rate;
	long parsed;
	int rc;

	ARG_UNUSED(user_data);
	if (request->operation == RZI_AT_OP_READ) {
		rc = rzi_lorawan_get_rx2_data_rate(&data_rate);
		return rc != 0 ? rc
			       : rzi_at_respond_value(RZI_AT_VALUE(RZI_ATCMD_RX2DR, "%u"),
						      (unsigned int)data_rate);
	}
	rc = rzi_at_lorawan_parse_long(request->argument, &parsed);
	if (rc != 0 || parsed < 0 || parsed > 15) {
		return -RZI_ERR_INVALID;
	}
	rc = rzi_lorawan_set_rx2_data_rate((enum rzi_lorawan_data_rate)parsed);
	return rc != 0 ? rc : rzi_at_respond_status(RZI_AT_STATUS_OK);
}

static int handle_rx2fq(const struct rzi_at_request *request, void *user_data)
{
	ARG_UNUSED(user_data);
	return handle_u32(request, RZI_ATCMD_RX2FQ, rzi_lorawan_get_rx2_frequency,
			  rzi_lorawan_set_rx2_frequency, 1U);
}

static int handle_lbt_rssi(const struct rzi_at_request *request, void *user_data)
{
	int16_t rssi;
	long parsed;
	int rc;

	ARG_UNUSED(user_data);
	if (request->operation == RZI_AT_OP_READ) {
		rc = rzi_lorawan_get_lbt_rssi(&rssi);
		return rc != 0 ? rc
			       : rzi_at_respond_value(RZI_AT_VALUE(RZI_ATCMD_LBTRSSI, "%d"), rssi);
	}
	rc = rzi_at_lorawan_parse_long(request->argument, &parsed);
	if (rc != 0) {
		return rc;
	}
	rc = rzi_lorawan_set_lbt_rssi((int16_t)parsed);
	return rc != 0 ? rc : rzi_at_respond_status(RZI_AT_STATUS_OK);
}

static int handle_lbt_scan(const struct rzi_at_request *request, void *user_data)
{
	ARG_UNUSED(user_data);
	return handle_u32(request, RZI_ATCMD_LBTSCANTIME, rzi_lorawan_get_lbt_scan_time,
			  rzi_lorawan_set_lbt_scan_time, 1U);
}

static int handle_timereq(const struct rzi_at_request *request, void *user_data)
{
	bool enabled;
	int rc;

	ARG_UNUSED(user_data);
	if (request->operation == RZI_AT_OP_READ) {
		rc = rzi_lorawan_get_device_time_enabled(&enabled);
		return rc != 0 ? rc
			       : rzi_at_respond_value(RZI_AT_VALUE(RZI_ATCMD_TIMEREQ, "%d"),
						      enabled ? 1 : 0);
	}
	rc = rzi_at_lorawan_parse_bool(request->argument, &enabled);
	if (rc != 0) {
		return rc;
	}
	rc = rzi_lorawan_request_device_time(enabled);
	return rc != 0 ? rc : rzi_at_respond_status(RZI_AT_STATUS_OK);
}

static int handle_linkcheck(const struct rzi_at_request *request, void *user_data)
{
	enum rzi_lorawan_link_check_mode mode;
	long parsed;
	int rc;

	ARG_UNUSED(user_data);
	if (request->operation == RZI_AT_OP_READ) {
		rc = rzi_lorawan_get_link_check_mode(&mode);
		return rc != 0 ? rc
			       : rzi_at_respond_value(RZI_AT_VALUE(RZI_ATCMD_LINKCHECK, "%u"),
						      (unsigned int)mode);
	}
	rc = rzi_at_lorawan_parse_long(request->argument, &parsed);
	if (rc != 0 || parsed < 0 || parsed > 2) {
		return -RZI_ERR_INVALID;
	}
	rc = rzi_lorawan_request_link_check((enum rzi_lorawan_link_check_mode)parsed);
	return rc != 0 ? rc : rzi_at_respond_status(RZI_AT_STATUS_OK);
}

static const struct rzi_at_command commands[] = {
	{
		.name = RZI_ATCMD_BAND,
		.help = "get or set the active region (0 = EU433, 1 = CN470, 2 = RU864, "
			"3 = IN865, 4 = EU868, 5 = US915, 6 = AU915, 7 = KR920, "
			"8 = AS923-1, 9 = AS923-2, 10 = AS923-3, 11 = AS923-4, "
			"12 = LA915)",
		.allowed_operations = RZI_AT_ALLOW_READ | RZI_AT_ALLOW_WRITE,
		.handler = handle_band,
	},
	{
		.name = RZI_ATCMD_CLASS,
		.help = "get or set the device class (A = class A, B = class B, C = class C)",
		.allowed_operations = RZI_AT_ALLOW_READ | RZI_AT_ALLOW_WRITE,
		.handler = handle_class,
	},
	{
		.name = RZI_ATCMD_ADR,
		.help = "get or set the adaptive data rate setting (0 = off, 1 = on)",
		.allowed_operations = RZI_AT_ALLOW_READ | RZI_AT_ALLOW_WRITE,
		.handler = handle_adr,
	},
	{
		.name = RZI_ATCMD_DCS,
		.help = "get or set the ETSI duty cycle setting (0 = disabled, 1 = enabled)",
		.allowed_operations = RZI_AT_ALLOW_READ | RZI_AT_ALLOW_WRITE,
		.handler = handle_dcs,
	},
	{
		.name = RZI_ATCMD_DR,
		.help = "get or set the data rate",
		.allowed_operations = RZI_AT_ALLOW_READ | RZI_AT_ALLOW_WRITE,
		.handler = handle_dr,
	},
	{
		.name = RZI_ATCMD_TXP,
		.help = "get or set the transmitting power",
		.allowed_operations = RZI_AT_ALLOW_READ | RZI_AT_ALLOW_WRITE,
		.handler = handle_txp,
	},
	{
		.name = RZI_ATCMD_PNM,
		.help = "get or set the public network mode (0 = off, 1 = on)",
		.allowed_operations = RZI_AT_ALLOW_READ | RZI_AT_ALLOW_WRITE,
		.handler = handle_pnm,
	},
	{
		.name = RZI_ATCMD_RX1DL,
		.help = "get or set the delay between the end of TX and RX window 1 in seconds",
		.allowed_operations = RZI_AT_ALLOW_READ | RZI_AT_ALLOW_WRITE,
		.handler = handle_rx1dl,
	},
	{
		.name = RZI_ATCMD_RX2DL,
		.help = "get or set the delay between the end of TX and RX window 2 in seconds",
		.allowed_operations = RZI_AT_ALLOW_READ | RZI_AT_ALLOW_WRITE,
		.handler = handle_rx2dl,
	},
	{
		.name = RZI_ATCMD_RX2DR,
		.help = "get or set the RX2 window data rate",
		.allowed_operations = RZI_AT_ALLOW_READ | RZI_AT_ALLOW_WRITE,
		.handler = handle_rx2dr,
	},
	{
		.name = RZI_ATCMD_RX2FQ,
		.help = "get or set the RX2 window frequency (Hz)",
		.allowed_operations = RZI_AT_ALLOW_READ | RZI_AT_ALLOW_WRITE,
		.handler = handle_rx2fq,
	},
	{
		.name = RZI_ATCMD_JN1DL,
		.help = "get or set the join accept delay for Rx window 1 in seconds",
		.allowed_operations = RZI_AT_ALLOW_READ | RZI_AT_ALLOW_WRITE,
		.handler = handle_jn1dl,
	},
	{
		.name = RZI_ATCMD_JN2DL,
		.help = "get or set the join accept delay for Rx window 2 in seconds",
		.allowed_operations = RZI_AT_ALLOW_READ | RZI_AT_ALLOW_WRITE,
		.handler = handle_jn2dl,
	},
	{
		.name = RZI_ATCMD_LBT,
		.help = "get or set LoRaWAN LBT (0 = disabled, 1 = enabled)",
		.allowed_operations = RZI_AT_ALLOW_READ | RZI_AT_ALLOW_WRITE,
		.handler = handle_lbt,
	},
	{
		.name = RZI_ATCMD_LBTRSSI,
		.help = "get or set the LoRaWAN LBT RSSI threshold",
		.allowed_operations = RZI_AT_ALLOW_READ | RZI_AT_ALLOW_WRITE,
		.handler = handle_lbt_rssi,
	},
	{
		.name = RZI_ATCMD_LBTSCANTIME,
		.help = "get or set the LoRaWAN LBT scan time in milliseconds",
		.allowed_operations = RZI_AT_ALLOW_READ | RZI_AT_ALLOW_WRITE,
		.handler = handle_lbt_scan,
	},
	{
		.name = RZI_ATCMD_LINKCHECK,
		.help = "get or set the link check setting (0 = disabled, 1 = once, 2 = everytime)",
		.allowed_operations = RZI_AT_ALLOW_READ | RZI_AT_ALLOW_WRITE,
		.handler = handle_linkcheck,
	},
	{
		.name = RZI_ATCMD_TIMEREQ,
		.help = "request the current date and time (0 = disabled, 1 = enabled)",
		.allowed_operations = RZI_AT_ALLOW_READ | RZI_AT_ALLOW_WRITE,
		.handler = handle_timereq,
	},
};

const struct rzi_at_lorawan_command_group rzi_at_lorawan_network_management_group = {
	.commands = commands,
	.count = ARRAY_SIZE(commands),
};
