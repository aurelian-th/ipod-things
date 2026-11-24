// SPDX-License-Identifier: GPL-3.0-only
/*
 * Adapted from PodBox firmware/usbstack/usb_iap2.c
 * (commit 7a89c33d0ed49c93ba49a8198e822b8adb72d0a5).
 * PodBox is GPL-2.0-or-later; this version is GPL-3.0-only.
 * Modified by the RevolaOS authors, 2026.
 */

#ifndef IAP2_LINK_H
#define IAP2_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "protocol.h"

#define IAP2_LINK_MAX_SLOTS      4
#define IAP2_LINK_MAX_PACKET     1024
#define IAP2_LINK_SYN_ACK_MAX    48
#define IAP2_LINK_MAX_RX_PACKET  8192
#define IAP2_LINK_SYN_FIXED_LEN  10
#define IAP2_LINK_MIN_RX_PACKET  (IAP2_HEADER_LEN + IAP2_LINK_SYN_ACK_MAX + 1)

#define IAP2_LINK_PENDING_ECHO    (1U << 0)
#define IAP2_LINK_PENDING_SYN_ACK (1U << 1)
#define IAP2_LINK_PENDING_ACK     (1U << 2)
#define IAP2_LINK_PENDING_RST     (1U << 3)

typedef enum {
    IAP2_LINK_STATE_DOWN = 0,
    IAP2_LINK_STATE_SYN_RCVD,
    IAP2_LINK_STATE_UP,
} iap2_link_state_t;

/*
 * Borrowed receive result view.
 *
 * Memory ownership:
 * - payload points directly into caller-provided packet storage.
 * - Lifetime bounded by the caller's packet buffer; no caller pointers retained.
 * - packet_handled is set to true strictly when iap2_link_receive_packet()
 *   returns IAP2_STATUS_OK.
 */
struct iap2_link_rx_result {
    bool packet_handled;
    bool has_payload;
    uint8_t session;
    const uint8_t *payload;
    size_t payload_len;
};

/*
 * Static outbound packet slot.
 *
 * Sequence and retry tracking:
 * - tries is widened to uint16_t to accommodate 256 attempts (retrans = 255)
 *   without 8-bit unsigned overflow.
 * - in_flight indicates that a frame slice has been handed to the host transport.
 */
struct iap2_link_slot {
    uint8_t packet[IAP2_LINK_MAX_PACKET];
    size_t len;
    uint8_t seq;
    uint16_t tries;
    uint32_t sent_ms;
    bool queued;
    bool in_flight;
};

/*
 * iAP2 link layer state engine.
 *
 * Transport and buffer ownership contract:
 * - All frame buffers returned by iap2_link_peek_tx() are borrowed references
 *   into internal storage (slots[].packet, ctl_buf, or iap2_probe_wire).
 * - A borrowed buffer remains immutable and owned by the caller/DMA/bridge
 *   until iap2_link_finish_tx() is invoked, even across session resets, peer
 *   probes, RST packets, timeout exhaustion, or new SYN arrivals.
 * - When a session reset occurs while tx_in_flight is true, tx_stale is set.
 *   The in-flight slot/buffer is preserved, remaining slot capacity is fenced,
 *   and subsequent session ACKs cannot acknowledge or mutate the stale TX.
 *   Only the eventual iap2_link_finish_tx() clears the in-flight reference.
 * - The native bridge may optionally invoke iap2_link_mark_submitted() to
 *   confirm report submission before completion.
 */
struct iap2_link {
    iap2_link_state_t state;
    bool synced;
    bool link_up;
    bool syn_ack_sent;
    bool timeout_exhausted;

    uint8_t my_seq;
    uint8_t syn_seq;
    uint8_t peer_seq;
    uint8_t last_ack;
    uint8_t highest_sent_seq;

    uint8_t control_session;
    uint8_t file_session;

    uint16_t window;
    size_t peer_max;
    uint32_t rto_ms;
    uint32_t cack_ms;
    uint16_t max_sends;
    uint8_t max_cacks;
    uint32_t unacked;
    uint32_t unacked_since_ms;

    uint32_t tx_unacked;
    uint32_t tx_unacked_since_ms;

    uint32_t rx_accepted;
    uint32_t last_sent_rx_ack;
    uint32_t tx_rx_ack;

    uint8_t last_sent_peer_ack;
    uint8_t tx_peer_ack;
    uint32_t ack_req_seq;
    uint32_t tx_ack_req_seq;

    uint32_t syn_req_seq;
    uint32_t tx_syn_req_seq;

    bool has_deferred_ack;
    uint8_t deferred_ack;

    size_t rx_capacity;

    uint8_t syn_ack_payload[IAP2_LINK_SYN_ACK_MAX];
    size_t syn_ack_len;

    uint8_t last_syn_payload[IAP2_LINK_SYN_ACK_MAX];
    size_t last_syn_len;
    uint8_t last_syn_seq;

    struct iap2_link_slot slots[IAP2_LINK_MAX_SLOTS];
    unsigned head;
    unsigned count;

    uint32_t pending;
    uint32_t last_now_ms;

    bool tx_in_flight;
    bool tx_stale;
    bool tx_submitted;
    uint8_t tx_type;
    struct iap2_link_slot *tx_slot;
    const uint8_t *tx_frame;
    size_t tx_len;
    uint8_t ctl_buf[IAP2_HEADER_LEN + IAP2_LINK_SYN_ACK_MAX + 1];
};

/*
 * Initialize link instance with bounded caller RX capacity.
 * Fails with IAP2_STATUS_INVALID_ARG if rx_capacity is below IAP2_LINK_MIN_RX_PACKET
 * (58 bytes) or exceeds IAP2_LINK_MAX_RX_PACKET (8192 bytes).
 */
iap2_status_t iap2_link_init(struct iap2_link *link, size_t rx_capacity);

/*
 * Soft reset of link session. Preserves active in-flight TX under tx_stale fence.
 */
void iap2_link_reset(struct iap2_link *link);

/*
 * Disconnect link session and restore default parameters. Preserves active
 * in-flight TX under tx_stale fence until finish_tx is invoked.
 */
void iap2_link_disconnect(struct iap2_link *link);

iap2_status_t iap2_link_handle_probe(struct iap2_link *link);

iap2_status_t iap2_link_receive_packet(struct iap2_link *link,
                                       const uint8_t *packet,
                                       size_t packet_len,
                                       struct iap2_link_rx_result *out_result);

iap2_status_t iap2_link_tick(struct iap2_link *link, uint32_t now_ms);

iap2_status_t iap2_link_queue_payload(struct iap2_link *link,
                                      uint8_t session,
                                      const uint8_t *payload,
                                      size_t payload_len);

iap2_status_t iap2_link_peek_tx(struct iap2_link *link,
                                const uint8_t **out_frame,
                                size_t *out_len);

/*
 * Confirm that the final report of a borrowed frame has been submitted.
 * All calls to this link instance must be serialized by the caller.
 * Completion-first callers may use finish_tx(true) without this marker.
 */
iap2_status_t iap2_link_mark_submitted(struct iap2_link *link);

/*
 * Release borrowed TX frame and update transmission/retry state.
 */
iap2_status_t iap2_link_finish_tx(struct iap2_link *link, bool success);

bool iap2_link_is_up(const struct iap2_link *link);

#endif /* IAP2_LINK_H */
