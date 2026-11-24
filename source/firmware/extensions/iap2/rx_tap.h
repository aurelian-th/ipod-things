// SPDX-License-Identifier: GPL-3.0-only
#ifndef IAP2_RX_TAP_H
#define IAP2_RX_TAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "link.h"

#define IAP2_HID_MAX_REPORT_ROWS   16U
#define IAP2_HID_HS_MAX_REPORT_LEN 1024U
#define IAP2_HID_FS_MAX_REPORT_LEN 64U
#define IAP2_RX_TAP_REASM_CAPACITY IAP2_LINK_MAX_RX_PACKET

/*
 * Apple iPod HID report table row (8 bytes, little-endian in Apple DRAM).
 * Terminated by body_size == 0.
 * body_size excludes the 1-byte report ID (report[0]) and includes
 * link_control (report[1]) + payload.
 */
struct iap2_hid_report_row {
    uint16_t report_id;
    uint16_t reserved;
    uint32_t body_size;
};
_Static_assert(sizeof(struct iap2_hid_report_row) == 8, "8-byte report row");

typedef enum {
    IAP2_TAP_PASSTHROUGH = 0,
    IAP2_TAP_CLAIMED_ACK,
    IAP2_TAP_DROP_ERROR,
} iap2_tap_action_t;

struct iap2_rx_stats {
    uint32_t rx_total_reports;
    uint32_t rx_iap1_passthrough;
    uint32_t rx_bad_report;
    uint32_t rx_iap2_probes;
    uint32_t rx_iap2_packets;
    uint32_t rx_link_handled;
    uint32_t rx_link_payloads;
    uint32_t rx_bad_checksum;
    uint32_t rx_malformed;
    uint32_t tx_queued_events;
};

struct iap2_link_summary {
    iap2_link_state_t link_state;
    uint32_t pending_tx_mask;
    uint32_t queued_slots;
    uint32_t link_rx_accepted;
    uint8_t my_seq;
    uint8_t peer_seq;
    uint8_t last_ack;
    uint8_t last_session;
    uint16_t last_payload_len;
};

struct iap2_rx_tap {
    struct iap2_reassembler reasm;
    struct iap2_link link;
    struct iap2_rx_stats stats;
    struct iap2_link_summary summary;
    void *callback_context;
    void (*on_payload)(void *context, uint8_t session,
                       const uint8_t *payload, size_t len, uint32_t now_ms);
    void (*on_reset)(void *context);
    uint32_t now_ms;
    uint8_t reasm_buf[IAP2_RX_TAP_REASM_CAPACITY];
};

iap2_status_t iap2_rx_tap_init(struct iap2_rx_tap *tap);

void iap2_rx_tap_reset(struct iap2_rx_tap *tap);

void iap2_rx_tap_disconnect(struct iap2_rx_tap *tap);

void iap2_link_summary_clear_session(struct iap2_link_summary *summary);

void iap2_rx_tap_sync_summary(const struct iap2_link *link,
                              struct iap2_link_summary *summary);

void iap2_rx_tap_snapshot(const struct iap2_rx_tap *tap,
                          struct iap2_rx_stats *out_stats,
                          struct iap2_link_summary *out_summary);

bool iap2_hid_lookup_report_len(const struct iap2_hid_report_row *table,
                                size_t max_rows,
                                size_t max_report_len,
                                uint16_t report_id,
                                size_t *out_expected_len);

iap2_tap_action_t iap2_rx_tap_process_report(
    struct iap2_rx_tap *tap,
    const struct iap2_hid_report_row *out_table,
    size_t max_rows,
    size_t max_report_len,
    const uint8_t *report,
    size_t report_len);

#endif /* IAP2_RX_TAP_H */
