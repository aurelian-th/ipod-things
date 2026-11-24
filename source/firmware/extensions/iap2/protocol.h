// SPDX-License-Identifier: GPL-3.0-only
/*
 * Adapted from PodBox firmware/usbstack/usb_iap2.c
 * (commit 7a89c33d0ed49c93ba49a8198e822b8adb72d0a5).
 * PodBox is GPL-2.0-or-later; this version is GPL-3.0-only.
 * Modified by the RevolaOS authors, 2026.
 */

#ifndef IAP2_PROTOCOL_H
#define IAP2_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IAP2_SOP1 0xFF
#define IAP2_SOP2 0x5A
#define IAP2_HEADER_LEN 9
#define IAP2_PROBE_LEN 6

/* Flags used by the reference; the codec preserves other flag bits. */
#define IAP2_CTL_SYN 0x80
#define IAP2_CTL_ACK 0x40
#define IAP2_CTL_EAK 0x20
#define IAP2_CTL_RST 0x10

/* HID link control byte bits (IAPHIDReport) */
#define IAP2_LINK_CONTROL_CONTINUE (1 << 0)
#define IAP2_LINK_CONTROL_MORE_TO_FOLLOW (1 << 1)
#define IAP2_LINK_CONTROL_RESERVED_MASK (~0x03)

typedef enum {
    IAP2_STATUS_OK = 0,
    IAP2_STATUS_IN_PROGRESS,
    IAP2_STATUS_UNCLAIMED,
    IAP2_STATUS_INVALID_ARG,
    IAP2_STATUS_MALFORMED,
    IAP2_STATUS_MALFORMED_CHECKSUM,
    IAP2_STATUS_BUFFER_TOO_SMALL,
} iap2_status_t;

typedef enum {
    IAP2_PACKET_TYPE_NONE = 0,
    IAP2_PACKET_TYPE_PROBE,
    IAP2_PACKET_TYPE_LINK,
} iap2_packet_type_t;

extern const uint8_t iap2_probe_wire[IAP2_PROBE_LEN];

struct iap2_header {
    uint16_t length;
    uint8_t control;
    uint8_t seq;
    uint8_t ack;
    uint8_t session;
};

/*
 * Borrowed packet view.
 *
 * Memory ownership:
 * - payload points directly into the caller-provided buffer (from decode or reassembly).
 * - Lifetime is bounded by the underlying caller buffer; no pointer retention or hidden
 *   allocation is performed.
 * - For header-only link packets (length == 9), payload is NULL and payload_len is 0.
 */
struct iap2_packet_view {
    struct iap2_header header;
    const uint8_t *payload;
    size_t payload_len;
};

struct iap2_assembled_packet {
    iap2_packet_type_t type;
    const uint8_t *data;
    size_t len;
    struct iap2_packet_view view;
};

/* All input ranges must be readable for their supplied lengths. Encode input
 * and output must not overlap; HID reports must not overlap assembly storage.
 * Decoding requires exactly one packet. Output views are valid only on OK;
 * assembly views expire on the next process, reset or init call. Output
 * structures must not overlap the packet/report bytes or assembler state.
 * A validated caller supplies the expected report ID and total size for each
 * report. Probe detection accepts an unfragmented probe with zero padding.
 * Multi-packet reports and packets spanning separate HID transfers are not
 * supported by this strict single-packet assembler. */

/*
 * Bounded reassembler state.
 *
 * Storage is caller-provided via iap2_reassembler_init(). Reassembly operates on a
 * single-packet-per-assembly basis. Concatenated non-padding data in reports is
 * explicitly rejected rather than silently discarded.
 */
struct iap2_reassembler {
    uint8_t *buf;
    size_t capacity;
    size_t rx_len;
    size_t expected_packet_len;
    bool in_progress;
};

iap2_status_t iap2_detect_probe(const uint8_t *report,
                                size_t report_len,
                                uint8_t expected_report_id,
                                size_t expected_report_len);

iap2_status_t iap2_encode_packet(uint8_t *out_buf,
                                 size_t out_buf_size,
                                 uint8_t control,
                                 uint8_t seq,
                                 uint8_t ack,
                                 uint8_t session,
                                 const uint8_t *payload,
                                 size_t payload_len,
                                 size_t *out_len);

iap2_status_t iap2_decode_packet(const uint8_t *buf,
                                 size_t buf_len,
                                 struct iap2_packet_view *view);

void iap2_reassembler_init(struct iap2_reassembler *r,
                           uint8_t *storage,
                           size_t capacity);

void iap2_reassembler_reset(struct iap2_reassembler *r);

iap2_status_t iap2_reassembler_process_report(
    struct iap2_reassembler *r,
    const uint8_t *report,
    size_t report_len,
    uint8_t expected_report_id,
    size_t expected_report_len,
    struct iap2_assembled_packet *out_packet);

#endif /* IAP2_PROTOCOL_H */
