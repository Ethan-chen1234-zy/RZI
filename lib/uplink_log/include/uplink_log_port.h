/* SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless */
/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file
 * @brief LoRa uplink log library: platform port contract.
 *
 * Only the platform adaptation includes this header. It registers the
 * platform capabilities with uplink_log_init(), drives uplink_log_process()
 * and reports transmit results back to the library.
 *
 * The library does not use flash. Cross-reboot storage, if any, belongs to
 * the application.
 */
#ifndef UPLINK_LOG_PORT_H
#define UPLINK_LOG_PORT_H

#include "uplink_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Outcome of a frame previously accepted by uplink_log_port.send. */
enum uplink_log_tx_result {
	/** A confirmed frame was acknowledged by the network. */
	UPLINK_LOG_TX_ACKED,
	/** The frame was transmitted; for a confirmed frame no ACK arrived. */
	UPLINK_LOG_TX_SENT,
	/** The frame was not transmitted. */
	UPLINK_LOG_TX_FAILED,
};

/**
 * Platform capabilities. The table is copied by uplink_log_init().
 *
 * Every hook receives ctx. Hooks are called with the library lock held and
 * must not call back into the library.
 */
struct uplink_log_port {
	void *ctx;

	/** Required. Monotonic seconds since this boot. */
	uint32_t (*uptime_sec)(void *ctx);

	/**
	 * Required. Start transmitting one frame on FPort UPLINK_LOG_FPORT.
	 * Must copy the frame and return without waiting for completion.
	 * Return 0 when accepted; the platform then reports exactly one result
	 * through uplink_log_on_tx_result(). Any other value means "not accepted,
	 * try again later".
	 */
	int (*send)(void *ctx, const uint8_t *frame, size_t size, bool confirmed);

	/** Optional. Needed when the library is called from more than one thread. */
	void (*lock)(void *ctx);
	void (*unlock)(void *ctx);

	/** Optional. Ask the platform to call uplink_log_process() soon. */
	void (*wakeup)(void *ctx);
};

/**
 * @brief Register the platform and reset all library state for this boot.
 *
 * @retval UPLINK_LOG_OK Ready.
 * @retval UPLINK_LOG_ERR_INVALID A required hook or option is missing or invalid.
 */
int uplink_log_init(const struct uplink_log_port *port, const struct uplink_log_options *options);

/**
 * @brief Tell the library whether the network link can carry uplinks.
 *
 * Nothing is sent while the link is down. Going down fails the frame in
 * flight. The first periodic report falls due one interval after link up.
 */
int uplink_log_set_link(bool up);

/**
 * @brief Report the outcome of the last frame accepted by port.send.
 *
 * Does not transmit; it requests processing through port.wakeup.
 */
int uplink_log_on_tx_result(enum uplink_log_tx_result result);

/**
 * @brief Run timers and transmit the next due frame, if any.
 *
 * @return Seconds until the next call is needed, 0 for "call again now", or
 *         UPLINK_LOG_NO_DEADLINE when only a wakeup can create new work.
 */
uint32_t uplink_log_process(void);

#ifdef __cplusplus
}
#endif

#endif /* UPLINK_LOG_PORT_H */
