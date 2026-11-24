// SPDX-License-Identifier: GPL-3.0-only
/*
 * Protocol wire and reassembler unit tests.
 *
 * Adapted from PodBox firmware/usbstack/usb_iap2.c
 * (commit 7a89c33d0ed49c93ba49a8198e822b8adb72d0a5).
 * PodBox is GPL-2.0-or-later; this version is GPL-3.0-only.
 * Modified by the RevolaOS authors, 2026.
 */

#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NDEBUG
#error "Protocol tests require assertions"
#endif

static void test_probe_detection(void)
{
    /* Expected probe: report_id=0x01, link_ctrl=0x00, 6 probe bytes */
    uint8_t valid_report[8] = {0x01, 0x00, 0xFF, 0x55, 0x02, 0x00, 0xEE, 0x10};
    assert(iap2_detect_probe(valid_report, sizeof(valid_report), 0x01, sizeof(valid_report)) == IAP2_STATUS_OK);

    /* Allowed 0x00 padding in longer report */
    uint8_t padded_report[16] = {0x01, 0x00, 0xFF, 0x55, 0x02, 0x00, 0xEE, 0x10, 0, 0, 0, 0, 0, 0, 0, 0};
    assert(iap2_detect_probe(padded_report, sizeof(padded_report), 0x01, sizeof(padded_report)) == IAP2_STATUS_OK);

    /* Concatenated non-padding tail rejected */
    uint8_t non_pad_report[16] = {0x01, 0x00, 0xFF, 0x55, 0x02, 0x00, 0xEE, 0x10, 0, 0, 0xAA, 0, 0, 0, 0, 0};
    assert(iap2_detect_probe(non_pad_report, sizeof(non_pad_report), 0x01, sizeof(non_pad_report)) == IAP2_STATUS_MALFORMED);

    /* Continuation bit set is unclaimed as a probe */
    valid_report[1] = IAP2_LINK_CONTROL_CONTINUE;
    assert(iap2_detect_probe(valid_report, sizeof(valid_report), 0x01, sizeof(valid_report)) == IAP2_STATUS_UNCLAIMED);

    /* MoreToFollow set on probe is malformed */
    valid_report[1] = IAP2_LINK_CONTROL_MORE_TO_FOLLOW;
    assert(iap2_detect_probe(valid_report, sizeof(valid_report), 0x01, sizeof(valid_report)) == IAP2_STATUS_MALFORMED);

    /* Reserved link control bits rejected */
    valid_report[1] = 0x04;
    assert(iap2_detect_probe(valid_report, sizeof(valid_report), 0x01, sizeof(valid_report)) == IAP2_STATUS_MALFORMED);
    valid_report[1] = 0x80;
    assert(iap2_detect_probe(valid_report, sizeof(valid_report), 0x01, sizeof(valid_report)) == IAP2_STATUS_MALFORMED);
    valid_report[1] = 0x00;

    /* Report ID mismatch is unclaimed */
    assert(iap2_detect_probe(valid_report, sizeof(valid_report), 0x02, sizeof(valid_report)) == IAP2_STATUS_UNCLAIMED);

    /* Report length mismatch is malformed */
    assert(iap2_detect_probe(valid_report, 7, 0x01, sizeof(valid_report)) == IAP2_STATUS_MALFORMED);

    /* Unknown / non-probe iAP1 packet remains unclaimed */
    uint8_t iap1_pkt[8] = {0x01, 0x00, 0xFF, 0x55, 0x04, 0x00, 0x01, 0x02};
    assert(iap2_detect_probe(iap1_pkt, sizeof(iap1_pkt), 0x01, sizeof(iap1_pkt)) == IAP2_STATUS_UNCLAIMED);
}

static void test_wire_codec_explicit_vectors(void)
{
    /*
     * Explicit Vector 1: Empty link packet (header-only, length 9).
     * Control: ACK (0x40), Seq: 0x01, Ack: 0x02, Session: 0x00.
     * Header bytes: FF 5A 00 09 40 01 02 00 -> Sum = 0x1A5.
     * Checksum = -0xA5 = 0x5B.
     */
    const uint8_t vec1[9] = {0xFF, 0x5A, 0x00, 0x09, 0x40, 0x01, 0x02, 0x00, 0x5B};
    struct iap2_packet_view view1;
    assert(iap2_decode_packet(vec1, sizeof(vec1), &view1) == IAP2_STATUS_OK);
    assert(view1.header.length == 9);
    assert(view1.header.control == 0x40);
    assert(view1.header.seq == 0x01);
    assert(view1.header.ack == 0x02);
    assert(view1.header.session == 0x00);
    assert(view1.payload == NULL);
    assert(view1.payload_len == 0);

    uint8_t enc1[16];
    size_t enc1_len = 0;
    assert(iap2_encode_packet(enc1, sizeof(enc1), 0x40, 0x01, 0x02, 0x00, NULL, 0, &enc1_len) == IAP2_STATUS_OK);
    assert(enc1_len == sizeof(vec1));
    assert(memcmp(enc1, vec1, sizeof(vec1)) == 0);

    /*
     * Explicit Vector 2: Packet with payload {1, 2, 3, 4} (total length 14).
     * Control: SYN (0x80), Seq: 0x05, Ack: 0x00, Session: 0x01.
     * Header bytes: FF 5A 00 0E 80 05 00 01 -> Sum = 0x1ED.
     * Checksum = -0xED = 0x13.
     * Payload bytes: 01 02 03 04 -> Sum = 0x0A.
     * Checksum = -0x0A = 0xF6.
     */
    const uint8_t vec2[14] = {
        0xFF, 0x5A, 0x00, 0x0E, 0x80, 0x05, 0x00, 0x01, 0x13,
        0x01, 0x02, 0x03, 0x04, 0xF6
    };
    struct iap2_packet_view view2;
    assert(iap2_decode_packet(vec2, sizeof(vec2), &view2) == IAP2_STATUS_OK);
    assert(view2.header.length == 14);
    assert(view2.header.control == 0x80);
    assert(view2.header.seq == 0x05);
    assert(view2.header.ack == 0x00);
    assert(view2.header.session == 0x01);
    assert(view2.payload == vec2 + 9);
    assert(view2.payload_len == 4);
    assert(memcmp(view2.payload, "\x01\x02\x03\x04", 4) == 0);

    uint8_t enc2[32];
    size_t enc2_len = 0;
    const uint8_t payload2[4] = {1, 2, 3, 4};
    assert(iap2_encode_packet(enc2, sizeof(enc2), 0x80, 0x05, 0x00, 0x01, payload2, 4, &enc2_len) == IAP2_STATUS_OK);
    assert(enc2_len == sizeof(vec2));
    assert(memcmp(enc2, vec2, sizeof(vec2)) == 0);

    /*
     * Explicit Vector 3: Minimum payload packet (length 11, 1 byte payload).
     * Control: RST (0x10), Seq: 0x00, Ack: 0x00, Session: 0x00, Payload: 0xAA.
     * Header bytes: FF 5A 00 0B 10 00 00 00 -> Sum = 0x174 -> Checksum = 0x8C.
     * Payload: AA -> Checksum = 0x56.
     */
    const uint8_t vec3[11] = {
        0xFF, 0x5A, 0x00, 0x0B, 0x10, 0x00, 0x00, 0x00, 0x8C,
        0xAA, 0x56
    };
    struct iap2_packet_view view3;
    assert(iap2_decode_packet(vec3, sizeof(vec3), &view3) == IAP2_STATUS_OK);
    assert(view3.header.length == 11);
    assert(view3.header.control == 0x10);
    assert(view3.payload_len == 1);
    assert(view3.payload[0] == 0xAA);
}

static void test_checksum_corruption(void)
{
    uint8_t pkt[14] = {
        0xFF, 0x5A, 0x00, 0x0E, 0x80, 0x05, 0x00, 0x01, 0x13,
        0x01, 0x02, 0x03, 0x04, 0xF6
    };
    struct iap2_packet_view view;

    /* Corrupt header checksum */
    pkt[8] ^= 0xFF;
    assert(iap2_decode_packet(pkt, sizeof(pkt), &view) == IAP2_STATUS_MALFORMED_CHECKSUM);
    pkt[8] ^= 0xFF;

    /* Corrupt header data field */
    pkt[5] ^= 0x01;
    assert(iap2_decode_packet(pkt, sizeof(pkt), &view) == IAP2_STATUS_MALFORMED_CHECKSUM);
    pkt[5] ^= 0x01;

    /* Corrupt payload checksum */
    pkt[13] ^= 0x55;
    assert(iap2_decode_packet(pkt, sizeof(pkt), &view) == IAP2_STATUS_MALFORMED_CHECKSUM);
    pkt[13] ^= 0x55;

    /* Corrupt payload content */
    pkt[10] ^= 0x01;
    assert(iap2_decode_packet(pkt, sizeof(pkt), &view) == IAP2_STATUS_MALFORMED_CHECKSUM);
}

static void test_boundary_lengths_and_controls(void)
{
    struct iap2_packet_view view;

    /* Underflow: lengths < 9 */
    uint8_t short_wire[9] = {0xFF, 0x5A, 0x00, 0x08, 0, 0, 0, 0, 0};
    assert(iap2_decode_packet(short_wire, sizeof(short_wire), &view) == IAP2_STATUS_MALFORMED);

    /* Disallowed boundary: length 10 (0-byte payload with 1 checksum) */
    uint8_t len10_wire[10] = {0xFF, 0x5A, 0x00, 0x0A, 0, 0, 0, 0, 0, 0};
    assert(iap2_decode_packet(len10_wire, sizeof(len10_wire), &view) == IAP2_STATUS_MALFORMED);

    /* The framing codec preserves flags for the session layer. */
    uint8_t ctl_wire[9] = {0xFF, 0x5A, 0x00, 0x09, 0x41, 0x01, 0x02, 0x00, 0x5A};
    assert(iap2_decode_packet(ctl_wire, sizeof(ctl_wire), &view) == IAP2_STATUS_OK);

    /* Truncated buffer */
    const uint8_t vec2[14] = {
        0xFF, 0x5A, 0x00, 0x0E, 0x80, 0x05, 0x00, 0x01, 0x13,
        0x01, 0x02, 0x03, 0x04, 0xF6
    };
    assert(iap2_decode_packet(vec2, 13, &view) == IAP2_STATUS_MALFORMED);

    /* Non-SOP wire */
    uint8_t bad_sop[9] = {0x00, 0x5A, 0x00, 0x09, 0x40, 0x01, 0x02, 0x00, 0x5B};
    assert(iap2_decode_packet(bad_sop, sizeof(bad_sop), &view) == IAP2_STATUS_UNCLAIMED);
}

static void test_reassembler_framing_and_splits(void)
{
    uint8_t rx_storage[128];
    struct iap2_reassembler r;
    iap2_reassembler_init(&r, rx_storage, sizeof(rx_storage));

    struct iap2_assembled_packet out;

    /* 1. Probe through reassembler */
    uint8_t probe_rep[8] = {0x01, 0x00, 0xFF, 0x55, 0x02, 0x00, 0xEE, 0x10};
    assert(iap2_reassembler_process_report(&r, probe_rep, 8, 0x01, 8, &out) == IAP2_STATUS_OK);
    assert(out.type == IAP2_PACKET_TYPE_PROBE);
    assert(out.len == 6);
    assert(memcmp(out.data, iap2_probe_wire, 6) == 0);

    /* 2. Single-report link packet with trailing 0x00 padding */
    uint8_t single_rep[16] = {
        0x01, 0x00,
        0xFF, 0x5A, 0x00, 0x09, 0x40, 0x01, 0x02, 0x00, 0x5B,
        0x00, 0x00, 0x00, 0x00, 0x00
    };
    assert(iap2_reassembler_process_report(&r, single_rep, 16, 0x01, 16, &out) == IAP2_STATUS_OK);
    assert(out.type == IAP2_PACKET_TYPE_LINK);
    assert(out.len == 9);
    assert(out.view.header.length == 9);
    assert(out.view.header.control == 0x40);

    /* 3. Concatenated non-padding tail in report rejected */
    uint8_t concat_rep[16] = {
        0x01, 0x00,
        0xFF, 0x5A, 0x00, 0x09, 0x40, 0x01, 0x02, 0x00, 0x5B,
        0xFF, 0x5A, 0x00, 0x09, 0x40
    };
    assert(iap2_reassembler_process_report(&r, concat_rep, 16, 0x01, 16, &out) == IAP2_STATUS_MALFORMED);
    assert(!r.in_progress);

    /* 4. Split across 2 reports: 14-byte packet split 6 bytes + 8 bytes */
    const uint8_t vec2[14] = {
        0xFF, 0x5A, 0x00, 0x0E, 0x80, 0x05, 0x00, 0x01, 0x13,
        0x01, 0x02, 0x03, 0x04, 0xF6
    };
    uint8_t rep_part1[8] = {0x01, IAP2_LINK_CONTROL_MORE_TO_FOLLOW, 0, 0, 0, 0, 0, 0};
    memcpy(rep_part1 + 2, vec2, 6);
    assert(iap2_reassembler_process_report(&r, rep_part1, 8, 0x01, 8, &out) == IAP2_STATUS_IN_PROGRESS);
    assert(r.in_progress);

    uint8_t rep_part2[10] = {0x01, IAP2_LINK_CONTROL_CONTINUE, 0, 0, 0, 0, 0, 0, 0, 0};
    memcpy(rep_part2 + 2, vec2 + 6, 8);
    assert(iap2_reassembler_process_report(&r, rep_part2, 10, 0x01, 10, &out) == IAP2_STATUS_OK);
    assert(out.type == IAP2_PACKET_TYPE_LINK);
    assert(out.len == 14);
    assert(out.view.header.control == 0x80);
    assert(out.view.payload_len == 4);
    assert(memcmp(out.view.payload, "\x01\x02\x03\x04", 4) == 0);

    /* 5. Split across 3 reports with single-byte length progression */
    uint8_t frag1[5] = {0x01, IAP2_LINK_CONTROL_MORE_TO_FOLLOW, vec2[0], vec2[1], vec2[2]};
    assert(iap2_reassembler_process_report(&r, frag1, 5, 0x01, 5, &out) == IAP2_STATUS_IN_PROGRESS);
    assert(r.expected_packet_len == 0);

    uint8_t frag2[4] = {0x01, IAP2_LINK_CONTROL_CONTINUE | IAP2_LINK_CONTROL_MORE_TO_FOLLOW, vec2[3], vec2[4]};
    assert(iap2_reassembler_process_report(&r, frag2, 4, 0x01, 4, &out) == IAP2_STATUS_IN_PROGRESS);
    assert(r.expected_packet_len == 14);

    uint8_t frag3[12] = {0x01, IAP2_LINK_CONTROL_CONTINUE};
    memcpy(frag3 + 2, vec2 + 5, 9);
    frag3[11] = 0x00; /* trailing 0 padding */
    assert(iap2_reassembler_process_report(&r, frag3, 12, 0x01, 12, &out) == IAP2_STATUS_OK);
    assert(out.len == 14);
}

static void test_reassembler_malformed_sequences_and_resets(void)
{
    uint8_t rx_storage[64];
    struct iap2_reassembler r;
    iap2_reassembler_init(&r, rx_storage, sizeof(rx_storage));
    struct iap2_assembled_packet out;

    /* Continuation without start */
    uint8_t unprompted_cont[8] = {0x01, IAP2_LINK_CONTROL_CONTINUE, 0, 0, 0, 0, 0, 0};
    assert(iap2_reassembler_process_report(&r, unprompted_cont, 8, 0x01, 8, &out) == IAP2_STATUS_MALFORMED);

    /* Incomplete packet followed by premature transfer end */
    const uint8_t vec2[14] = {
        0xFF, 0x5A, 0x00, 0x0E, 0x80, 0x05, 0x00, 0x01, 0x13,
        0x01, 0x02, 0x03, 0x04, 0xF6
    };
    uint8_t trunc_start[8] = {0x01, 0x00};
    memcpy(trunc_start + 2, vec2, 6);
    /* MoreToFollow is 0, but only 6 of 14 bytes provided */
    assert(iap2_reassembler_process_report(&r, trunc_start, 8, 0x01, 8, &out) == IAP2_STATUS_MALFORMED);
    assert(!r.in_progress);

    /* In-progress stale assembly reset by new valid probe */
    uint8_t part1[8] = {0x01, IAP2_LINK_CONTROL_MORE_TO_FOLLOW};
    memcpy(part1 + 2, vec2, 6);
    assert(iap2_reassembler_process_report(&r, part1, 8, 0x01, 8, &out) == IAP2_STATUS_IN_PROGRESS);
    assert(r.in_progress);

    uint8_t probe_rep[8] = {0x01, 0x00, 0xFF, 0x55, 0x02, 0x00, 0xEE, 0x10};
    assert(iap2_reassembler_process_report(&r, probe_rep, 8, 0x01, 8, &out) == IAP2_STATUS_OK);
    assert(out.type == IAP2_PACKET_TYPE_PROBE);
    assert(!r.in_progress);

    /* In-progress stale assembly reset by unclaimed iAP1 start */
    assert(iap2_reassembler_process_report(&r, part1, 8, 0x01, 8, &out) == IAP2_STATUS_IN_PROGRESS);
    uint8_t iap1_start[8] = {0x01, 0x00, 0xFF, 0x55, 0x04, 0x00, 0x01, 0x02};
    assert(iap2_reassembler_process_report(&r, iap1_start, 8, 0x01, 8, &out) == IAP2_STATUS_UNCLAIMED);
    assert(!r.in_progress);

    /* Overcapacity limit */
    struct iap2_reassembler small_r;
    uint8_t small_storage[10];
    iap2_reassembler_init(&small_r, small_storage, sizeof(small_storage));
    uint8_t over_rep[16] = {
        0x01, 0x00,
        0xFF, 0x5A, 0x00, 0x0E, 0x80, 0x05, 0x00, 0x01, 0x13,
        0x01, 0x02, 0x03, 0x04, 0xF6
    };
    assert(iap2_reassembler_process_report(&small_r, over_rep, 16, 0x01, 16, &out) == IAP2_STATUS_BUFFER_TOO_SMALL);
}

static void test_null_arguments(void)
{
    struct iap2_packet_view view;
    size_t out_len = 0;
    uint8_t buf[16] = {0};
    struct iap2_reassembler r = {0};
    struct iap2_assembled_packet out;

    assert(iap2_decode_packet(NULL, 10, &view) == IAP2_STATUS_INVALID_ARG);
    assert(iap2_decode_packet(buf, 10, NULL) == IAP2_STATUS_INVALID_ARG);

    assert(iap2_encode_packet(NULL, 16, 0x40, 0, 0, 0, NULL, 0, &out_len) == IAP2_STATUS_INVALID_ARG);
    assert(iap2_encode_packet(buf, 16, 0x40, 0, 0, 0, NULL, 0, NULL) == IAP2_STATUS_INVALID_ARG);
    assert(iap2_encode_packet(buf, 16, 0x40, 0, 0, 0, NULL, 5, &out_len) == IAP2_STATUS_INVALID_ARG);

    assert(iap2_detect_probe(NULL, 8, 0x01, 8) == IAP2_STATUS_INVALID_ARG);
    assert(iap2_detect_probe(buf, 8, 0x01, 1) == IAP2_STATUS_INVALID_ARG);

    assert(iap2_reassembler_process_report(NULL, buf, 8, 0x01, 8, &out) == IAP2_STATUS_INVALID_ARG);
    assert(iap2_reassembler_process_report(&r, NULL, 8, 0x01, 8, &out) == IAP2_STATUS_INVALID_ARG);
    assert(iap2_reassembler_process_report(&r, buf, 8, 0x01, 8, NULL) == IAP2_STATUS_INVALID_ARG);
}

static void test_all_splits_and_recovery(void)
{
    const uint8_t wire[] = {
        0xFF, 0x5A, 0x00, 0x0E, 0x80, 0x05, 0x00, 0x01, 0x13,
        0x01, 0x02, 0x03, 0x04, 0xF6
    };
    uint8_t storage[sizeof(wire)];
    struct iap2_reassembler r;
    struct iap2_assembled_packet out;
    iap2_reassembler_init(&r, storage, sizeof(storage));
    for (size_t split = 1; split < sizeof(wire); split++) {
        uint8_t first[sizeof(wire) + 2] = {1, 2};
        uint8_t last[sizeof(wire) + 5] = {2, 1};
        size_t last_len = sizeof(wire) - split + 5;
        memcpy(first + 2, wire, split);
        memcpy(last + 2, wire + split, sizeof(wire) - split);
        assert(iap2_reassembler_process_report(&r, first, split + 2, 1,
                   split + 2, &out) == IAP2_STATUS_IN_PROGRESS);
        assert(iap2_reassembler_process_report(&r, last, last_len, 2,
                   last_len, &out) == IAP2_STATUS_OK);
        assert(out.type == IAP2_PACKET_TYPE_LINK);
        assert(out.len == sizeof(wire));
        assert(memcmp(out.data, wire, sizeof(wire)) == 0);
        assert(out.view.payload_len == 4);
    }

    uint8_t start[] = {1, 2, 0xFF, 0x5A, 0, 14};
    uint8_t bad_id[] = {2, 1, 0, 0};
    uint8_t continuation[] = {1, 1, 0, 0};
    assert(iap2_reassembler_process_report(&r, start, 6, 1, 6, &out)
           == IAP2_STATUS_IN_PROGRESS);
    assert(iap2_reassembler_process_report(&r, bad_id, 4, 1, 4, &out)
           == IAP2_STATUS_UNCLAIMED);
    assert(!r.in_progress && !r.rx_len);
    assert(iap2_reassembler_process_report(&r, continuation, 4, 1, 4, &out)
           == IAP2_STATUS_MALFORMED);

    const uint8_t probe[] = {1, 0, 0xFF, 0x55, 2, 0, 0xEE, 0x10};
    for (unsigned i = 0; i < 2; i++) {
        assert(iap2_reassembler_process_report(&r, probe, 8, 1, 8, &out)
               == IAP2_STATUS_OK);
        assert(out.type == IAP2_PACKET_TYPE_PROBE && !r.in_progress);
    }
    assert(iap2_reassembler_process_report(&r, start, 6, 1, 6, &out)
           == IAP2_STATUS_IN_PROGRESS);
    iap2_reassembler_reset(&r);
    assert(iap2_reassembler_process_report(&r, continuation, 4, 1, 4, &out)
           == IAP2_STATUS_MALFORMED);
}

static void test_wire_limits_and_flags(void)
{
    static uint8_t payload[65525];
    static uint8_t wire[65535];
    struct iap2_packet_view view;
    size_t n = 0;
    memset(payload, 0xA5, sizeof(payload));
    assert(iap2_encode_packet(wire, sizeof(wire), 0x48, 255, 0, 3,
               payload, sizeof(payload), &n) == IAP2_STATUS_OK);
    assert(n == sizeof(wire));
    assert(iap2_decode_packet(wire, n, &view) == IAP2_STATUS_OK);
    assert(view.payload_len == sizeof(payload) && view.header.control == 0x48);
    assert(memcmp(view.payload, payload, sizeof(payload)) == 0);
    assert(iap2_encode_packet(wire, sizeof(wire), 0x40, 0, 0, 0,
               payload, sizeof(payload) + 1, &n) == IAP2_STATUS_INVALID_ARG);
    assert(n == 0);
    assert(iap2_encode_packet(wire, sizeof(wire), 0x40, 0, 0, 0,
               payload, SIZE_MAX, &n) == IAP2_STATUS_INVALID_ARG);
    assert(iap2_encode_packet(wire, 8, 0x40, 0, 0, 0, NULL, 0, &n)
           == IAP2_STATUS_BUFFER_TOO_SMALL);
    assert(n == 0);
    assert(iap2_encode_packet(wire, 10, 0x40, 0, 0, 0, payload, 1, &n)
           == IAP2_STATUS_BUFFER_TOO_SMALL);
    for (unsigned flags = 0; flags < 256; flags++) {
        assert(iap2_encode_packet(wire, sizeof(wire), (uint8_t)flags, 0,
                   0, 0, NULL, 0, &n) == IAP2_STATUS_OK);
        assert(iap2_decode_packet(wire, n, &view) == IAP2_STATUS_OK);
        assert(view.header.control == flags);
    }
    wire[n] = 0;
    assert(iap2_decode_packet(wire, n + 1, &view) == IAP2_STATUS_MALFORMED);
    assert(view.payload == NULL && view.header.length == 0);
}

static void test_reassembly_error_recovery(void)
{
    const uint8_t good[16] = {
        1, 0, 0xFF, 0x5A, 0, 9, 0x40, 1, 2, 0, 0x5B
    };
    const uint8_t start[] = {1, 2, 0xFF, 0x5A, 0, 14};
    uint8_t storage[32];
    struct iap2_reassembler r;
    struct iap2_assembled_packet out;
    iap2_reassembler_init(&r, storage, sizeof(storage));
    for (unsigned failure = 0; failure < 7; failure++) {
        uint8_t bad[sizeof(good)];
        memcpy(bad, good, sizeof(bad));
        size_t n = sizeof(bad);
        iap2_status_t expected = IAP2_STATUS_MALFORMED;
        assert(iap2_reassembler_process_report(&r, start, 6, 1, 6, &out)
               == IAP2_STATUS_IN_PROGRESS);
        switch (failure) {
        case 0: bad[1] = 2; break;
        case 1: bad[1] = 4; break;
        case 2: bad[10] ^= 1; expected = IAP2_STATUS_MALFORMED_CHECKSUM; break;
        case 3: bad[5] = 8; break;
        case 4: n = 1; break;
        case 5: n = 0; break;
        case 6: bad[0] = 2; expected = IAP2_STATUS_UNCLAIMED; break;
        }
        assert(iap2_reassembler_process_report(&r, bad, n, 1, 16, &out)
               == expected);
        assert(!r.in_progress && !r.rx_len && out.type == IAP2_PACKET_TYPE_NONE);
    }
    const uint8_t ff[] = {1, 2, 0xFF};
    const uint8_t wrong_sop[] = {1, 1, 0x55};
    assert(iap2_reassembler_process_report(&r, ff, 3, 1, 3, &out)
           == IAP2_STATUS_IN_PROGRESS);
    assert(iap2_reassembler_process_report(&r, wrong_sop, 3, 1, 3, &out)
           == IAP2_STATUS_UNCLAIMED);
    assert(!r.in_progress);
    assert(iap2_reassembler_process_report(&r, start, 6, 1, 6, &out)
           == IAP2_STATUS_IN_PROGRESS);
    assert(iap2_reassembler_process_report(&r, good, 16, 1, 1, &out)
           == IAP2_STATUS_INVALID_ARG);
    assert(r.in_progress && r.rx_len == 4);
    const uint8_t empty[] = {1, 3};
    assert(iap2_reassembler_process_report(&r, empty, 2, 1, 2, &out)
           == IAP2_STATUS_MALFORMED);
    assert(!r.in_progress);
    iap2_reassembler_init(&r, storage, 3);
    assert(iap2_reassembler_process_report(&r, start, 6, 1, 6, &out)
           == IAP2_STATUS_BUFFER_TOO_SMALL);
    assert(!r.in_progress);
    uint8_t other_id[] = {2, 0};
    assert(iap2_detect_probe(other_id, 2, 1, 16) == IAP2_STATUS_UNCLAIMED);
}

static uint32_t xorshift32(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static void test_deterministic_fuzz(void)
{
    uint32_t state = 0x1A2B3C4D;
    uint8_t storage[128];
    struct iap2_reassembler r;
    iap2_reassembler_init(&r, storage, sizeof(storage));
    struct iap2_assembled_packet out;
    struct iap2_packet_view view;

    for (int iter = 0; iter < 10000; iter++) {
        uint8_t fbuf[32];
        size_t flen = (size_t)(xorshift32(&state) % sizeof(fbuf));
        for (size_t i = 0; i < flen; i++)
            fbuf[i] = (uint8_t)xorshift32(&state);

        iap2_detect_probe(fbuf, flen, 0x01, flen);
        iap2_decode_packet(fbuf, flen, &view);
        iap2_status_t status = iap2_reassembler_process_report(
            &r, fbuf, flen, 0x01, flen, &out);
        assert(r.rx_len <= r.capacity);
        if (status == IAP2_STATUS_OK)
            assert(out.type != IAP2_PACKET_TYPE_NONE && !r.in_progress);
        else if (status == IAP2_STATUS_IN_PROGRESS)
            assert(r.in_progress);
        else if (status != IAP2_STATUS_INVALID_ARG)
            assert(!r.in_progress && out.type == IAP2_PACKET_TYPE_NONE);
    }
}

int main(void)
{
    test_probe_detection();
    test_wire_codec_explicit_vectors();
    test_checksum_corruption();
    test_boundary_lengths_and_controls();
    test_reassembler_framing_and_splits();
    test_reassembler_malformed_sequences_and_resets();
    test_null_arguments();
    test_all_splits_and_recovery();
    test_wire_limits_and_flags();
    test_reassembly_error_recovery();
    test_deterministic_fuzz();

    puts("iAP2 protocol unit and sanitizer tests passed.");
    return 0;
}
