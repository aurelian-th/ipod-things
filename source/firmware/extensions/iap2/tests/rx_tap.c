// SPDX-License-Identifier: GPL-3.0-only
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rx_tap.h"
#include "tx.h"

static const struct iap2_hid_report_row hs_out_table[10] = {
    { 0x11, 0, 7 },
    { 0x12, 0, 19 },
    { 0x13, 0, 31 },
    { 0x14, 0, 63 },
    { 0x15, 0, 127 },
    { 0x16, 0, 255 },
    { 0x17, 0, 511 },
    { 0x18, 0, 767 },
    { 0x19, 0, 1023 },
    { 0, 0, 0 },
};

static const struct iap2_hid_report_row fs_out_table[6] = {
    { 0x21, 0, 7 },
    { 0x22, 0, 15 },
    { 0x23, 0, 27 },
    { 0x24, 0, 43 },
    { 0x25, 0, 63 },
    { 0, 0, 0 },
};

static void test_only_drain_tx(struct iap2_rx_tap *tap)
{
    static const struct iap2_hid_report_row host_input[] = {
        { 1, 0, 9 },
        { 2, 0, 17 },
        { 0, 0, 0 },
    };
    struct iap2_tx tx = {0};
    uint8_t report[18];
    uint8_t storage[64];
    struct iap2_reassembler host;
    iap2_reassembler_init(&host, storage, sizeof(storage));

    for (unsigned i = 0; ; i++) {
        assert(i < 8);
        size_t len = 0;
        assert(iap2_tx_prepare(&tx, &tap->link, host_input, 3,
                               report, sizeof(report), &len) == IAP2_STATUS_OK);
        if (len == 0)
            break;

        struct iap2_assembled_packet packet;
        size_t expected = 0;
        assert(iap2_hid_lookup_report_len(host_input, 3, sizeof(report), report[0], &expected));
        iap2_status_t result = iap2_reassembler_process_report(
            &host, report, len, report[0], expected, &packet);
        if (report[1] & IAP2_LINK_CONTROL_MORE_TO_FOLLOW) {
            assert(result == IAP2_STATUS_IN_PROGRESS);
        } else {
            assert(result == IAP2_STATUS_OK && packet.len == tap->link.tx_len);
            assert(memcmp(packet.data, tap->link.tx_frame, packet.len) == 0);
        }
        assert(iap2_tx_submitted(&tx, &tap->link) == IAP2_STATUS_OK);
        assert(iap2_tx_complete(&tx, &tap->link, true, 0) == IAP2_STATUS_OK);
    }
    assert(!host.in_progress);
    iap2_rx_tap_sync_summary(&tap->link, &tap->summary);
}

static void assert_zero_tx_in_flight(const struct iap2_rx_tap *tap)
{
    assert(!tap->link.tx_in_flight);
    assert(tap->link.tx_frame == NULL);
    assert(tap->link.tx_len == 0U);
    assert(!tap->link.tx_submitted);
}

static size_t build_syn_payload(uint8_t *buf, size_t cap)
{
    static const uint8_t syn_body[] = {
        0x01,       /* link version */
        0x04,       /* max outstanding packets */
        0x04, 0x00, /* max packet length = 1024 */
        0x03, 0xE8, /* retransmit timeout = 1000 ms */
        0x00, 0x64, /* cumulative ack timeout = 100 ms */
        0x1E,       /* max retransmissions = 30 */
        0x03,       /* max cumulative acks = 3 */
        0x0A, 0x00, 0x01, /* Control session: id=0x0A, type=0, ver=1 */
    };

    assert(cap >= sizeof(syn_body));
    memcpy(buf, syn_body, sizeof(syn_body));
    return sizeof(syn_body);
}

static void wrap_single_report(uint8_t *report,
                               size_t report_len,
                               uint8_t report_id,
                               uint8_t link_ctrl,
                               const uint8_t *packet,
                               size_t packet_len)
{
    assert(report_len >= 2U + packet_len);
    memset(report, 0, report_len);
    report[0] = report_id;
    report[1] = link_ctrl;
    memcpy(&report[2], packet, packet_len);
}

static void test_table_lookup_and_validation(void)
{
    size_t expected = 0;

    /* HS OUT 9-row table with runtime IDs */
    assert(iap2_hid_lookup_report_len(hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN, 0x11, &expected));
    assert(expected == 8U);
    assert(iap2_hid_lookup_report_len(hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN, 0x15, &expected));
    assert(expected == 128U);
    assert(iap2_hid_lookup_report_len(hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN, 0x19, &expected));
    assert(expected == 1024U);

    /* FS OUT 5-row table with runtime IDs */
    assert(iap2_hid_lookup_report_len(fs_out_table, 6, IAP2_HID_FS_MAX_REPORT_LEN, 0x21, &expected));
    assert(expected == 8U);
    assert(iap2_hid_lookup_report_len(fs_out_table, 6, IAP2_HID_FS_MAX_REPORT_LEN, 0x25, &expected));
    assert(expected == 64U);

    /* Unknown ID */
    assert(!iap2_hid_lookup_report_len(hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN, 0x77, &expected));

    /* Invalid IDs: 0 and > 255 */
    assert(!iap2_hid_lookup_report_len(hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN, 0, &expected));
    assert(!iap2_hid_lookup_report_len(hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN, 256, &expected));

    /* Corrupt table rows with id == 0 or id > 255 must be rejected/skipped */
    {
        static const struct iap2_hid_report_row bad_id_table[] = {
            { 0, 0, 15 },
            { 257, 0, 15 }, /* low byte aliases 1 if unchecked */
            { 300, 0, 15 },
            { 0, 0, 0 },
        };
        assert(!iap2_hid_lookup_report_len(bad_id_table, 4, IAP2_HID_FS_MAX_REPORT_LEN, 0, &expected));
        assert(!iap2_hid_lookup_report_len(bad_id_table, 4, IAP2_HID_FS_MAX_REPORT_LEN, 1, &expected));
        assert(!iap2_hid_lookup_report_len(bad_id_table, 4, IAP2_HID_FS_MAX_REPORT_LEN, 257, &expected));
        assert(!iap2_hid_lookup_report_len(bad_id_table, 4, IAP2_HID_FS_MAX_REPORT_LEN, 300, &expected));
    }

    /* Unterminated table capped at IAP2_HID_MAX_REPORT_ROWS (16) */
    {
        struct iap2_hid_report_row unterminated[20];
        size_t i;

        for (i = 0; i < 20; i++) {
            unterminated[i].report_id = (uint16_t)(0x40U + i);
            unterminated[i].reserved = 0;
            unterminated[i].body_size = 15U;
        }

        assert(iap2_hid_lookup_report_len(unterminated, 20, IAP2_HID_FS_MAX_REPORT_LEN, 0x4F, &expected));
        assert(expected == 16U);
        /* Row 16 (0x50) is beyond IAP2_HID_MAX_REPORT_ROWS and must not be read */
        assert(!iap2_hid_lookup_report_len(unterminated, 20, IAP2_HID_FS_MAX_REPORT_LEN, 0x50, &expected));
    }

    /* Oversize row (> max_report_len) */
    {
        static const struct iap2_hid_report_row oversize_table[] = {
            { 0x31, 0, 64 }, /* 1 + 64 = 65 > FS max 64 */
            { 0, 0, 0 },
        };
        assert(!iap2_hid_lookup_report_len(oversize_table, 2, IAP2_HID_FS_MAX_REPORT_LEN, 0x31, &expected));
    }
}

static void test_bad_reports_and_mismatches(void)
{
    struct iap2_rx_tap tap;
    uint8_t buf[128];

    assert(iap2_rx_tap_init(&tap) == IAP2_STATUS_OK);
    memset(buf, 0, sizeof(buf));

    /* Unknown report ID */
    buf[0] = 0x7F;
    assert(iap2_rx_tap_process_report(&tap, hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN, buf, 8)
           == IAP2_TAP_PASSTHROUGH);
    assert(tap.stats.rx_bad_report == 1U);

    /* Length mismatch (expected 8 for 0x11, passed 9 and 7) */
    buf[0] = 0x11;
    assert(iap2_rx_tap_process_report(&tap, hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN, buf, 9)
           == IAP2_TAP_PASSTHROUGH);
    assert(iap2_rx_tap_process_report(&tap, hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN, buf, 7)
           == IAP2_TAP_PASSTHROUGH);
    assert(tap.stats.rx_bad_report == 3U);

    /* Oversize > max_report_len (65 > FS max 64) */
    buf[0] = 0x25;
    assert(iap2_rx_tap_process_report(&tap, fs_out_table, 6, IAP2_HID_FS_MAX_REPORT_LEN, buf, 65)
           == IAP2_TAP_PASSTHROUGH);
    assert(tap.stats.rx_bad_report == 4U);

    assert_zero_tx_in_flight(&tap);
}

static void test_probe_syn_ack_data_and_zero_retention(void)
{
    struct iap2_rx_tap tap;
    struct iap2_rx_stats snap_stats;
    struct iap2_link_summary snap_summary;
    uint8_t report[64];
    uint8_t pkt[64];
    uint8_t syn_payload[32];
    size_t syn_len;
    size_t pkt_len = 0;
    static const uint8_t probe_bytes[6] = { 0xFF, 0x55, 0x02, 0x00, 0xEE, 0x10 };
    static const uint8_t app_payload[6] = { 0x40, 0x40, 0xAA, 0xBB, 0x01, 0x02 };
    const uint8_t peer_syn_seq = 0x10;

    assert(iap2_rx_tap_init(&tap) == IAP2_STATUS_OK);
    assert_zero_tx_in_flight(&tap);

    /* 1. Valid probe -> ECHO pending */
    wrap_single_report(report, 8, 0x11, 0x00, probe_bytes, sizeof(probe_bytes));
    assert(iap2_rx_tap_process_report(&tap, hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN, report, 8)
           == IAP2_TAP_CLAIMED_ACK);
    assert(tap.stats.rx_iap2_probes == 1U);
    assert(tap.stats.tx_queued_events >= 1U);
    assert((tap.summary.pending_tx_mask & IAP2_LINK_PENDING_ECHO) != 0U);
    assert_zero_tx_in_flight(&tap);
    test_only_drain_tx(&tap);
    assert(tap.summary.pending_tx_mask == 0);

    /* 2. Valid SYN -> SYN_RCVD & SYN_ACK pending */
    syn_len = build_syn_payload(syn_payload, sizeof(syn_payload));
    assert(iap2_encode_packet(pkt, sizeof(pkt), IAP2_CTL_SYN, peer_syn_seq, 0x00, 0x00,
                              syn_payload, syn_len, &pkt_len) == IAP2_STATUS_OK);
    wrap_single_report(report, 32, 0x13, 0x00, pkt, pkt_len);
    assert(iap2_rx_tap_process_report(&tap, hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN, report, 32)
           == IAP2_TAP_CLAIMED_ACK);
    assert(tap.summary.link_state == IAP2_LINK_STATE_SYN_RCVD);
    assert((tap.summary.pending_tx_mask & IAP2_LINK_PENDING_SYN_ACK) != 0U);
    assert(tap.summary.peer_seq == peer_syn_seq);
    assert_zero_tx_in_flight(&tap);

    /* 3a. Without sending SYN+ACK (Milestone 1 receive-only), peer ACK stays SYN_RCVD */
    assert(iap2_encode_packet(pkt, sizeof(pkt), IAP2_CTL_ACK, peer_syn_seq, tap.summary.my_seq,
                              0x00, NULL, 0, &pkt_len) == IAP2_STATUS_OK);
    wrap_single_report(report, 20, 0x12, 0x00, pkt, pkt_len);
    assert(iap2_rx_tap_process_report(&tap, hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN, report, 20)
           == IAP2_TAP_CLAIMED_ACK);
    assert(tap.summary.link_state == IAP2_LINK_STATE_SYN_RCVD);
    assert_zero_tx_in_flight(&tap);

    /* 3b. Send SYN+ACK through outbound HID reports, then peer ACK -> UP. */
    test_only_drain_tx(&tap);
    assert(iap2_rx_tap_process_report(&tap, hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN, report, 20)
           == IAP2_TAP_CLAIMED_ACK);
    assert(tap.summary.link_state == IAP2_LINK_STATE_UP);
    assert_zero_tx_in_flight(&tap);

    /* 4. Valid data payload -> last_session/last_payload_len updated, zero retained pointers */
    assert(iap2_encode_packet(pkt, sizeof(pkt), IAP2_CTL_ACK, (uint8_t)(peer_syn_seq + 1U),
                              tap.summary.my_seq, 0x0A, app_payload, sizeof(app_payload),
                              &pkt_len) == IAP2_STATUS_OK);
    wrap_single_report(report, 32, 0x13, 0x00, pkt, pkt_len);
    assert(iap2_rx_tap_process_report(&tap, hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN, report, 32)
           == IAP2_TAP_CLAIMED_ACK);

    /* Clobber caller buffers to confirm no pointer retention */
    memset(report, 0xA5, sizeof(report));
    memset(pkt, 0x5A, sizeof(pkt));

    iap2_rx_tap_snapshot(&tap, &snap_stats, &snap_summary);
    assert(snap_stats.rx_iap2_packets == 4U);
    assert(snap_stats.rx_link_handled == 4U);
    assert(snap_stats.rx_link_payloads == 1U);
    assert(snap_summary.link_state == IAP2_LINK_STATE_UP);
    assert(snap_summary.last_session == 0x0AU);
    assert(snap_summary.last_payload_len == (uint16_t)sizeof(app_payload));
    assert(snap_summary.peer_seq == (uint8_t)(peer_syn_seq + 1U));
    assert_zero_tx_in_flight(&tap);

    /* Disconnect clears session metadata and resets link state */
    iap2_rx_tap_disconnect(&tap);
    assert(tap.summary.link_state == IAP2_LINK_STATE_DOWN);
    assert(tap.summary.last_session == 0U);
    assert(tap.summary.last_payload_len == 0U);
    assert_zero_tx_in_flight(&tap);
}

static void test_iap1_passthrough_single_and_fragmented(void)
{
    struct iap2_rx_tap tap;
    uint8_t report[16];

    assert(iap2_rx_tap_init(&tap) == IAP2_STATUS_OK);

    /* Single-packet iAP1 (FF 55 lingo=0x00 cmd=0x00, not probe) */
    memset(report, 0, sizeof(report));
    report[0] = 0x22; /* FS row: body_size=15 -> expected_len=16 */
    report[1] = 0x00;
    report[2] = 0xFF;
    report[3] = 0x55;
    report[4] = 0x04;
    report[5] = 0x00;
    report[6] = 0x02;
    report[7] = 0x00;
    report[8] = 0xFA;
    assert(iap2_rx_tap_process_report(&tap, fs_out_table, 6, IAP2_HID_FS_MAX_REPORT_LEN, report, 16)
           == IAP2_TAP_PASSTHROUGH);
    assert(!tap.reasm.in_progress);

    /* Fragmented iAP1: MORE_TO_FOLLOW then CONTINUE */
    memset(report, 0, sizeof(report));
    report[0] = 0x22;
    report[1] = IAP2_LINK_CONTROL_MORE_TO_FOLLOW;
    report[2] = 0xFF;
    report[3] = 0x55;
    report[4] = 0x10;
    report[5] = 0x04;
    assert(iap2_rx_tap_process_report(&tap, fs_out_table, 6, IAP2_HID_FS_MAX_REPORT_LEN, report, 16)
           == IAP2_TAP_PASSTHROUGH);
    assert(!tap.reasm.in_progress);

    memset(report, 0, sizeof(report));
    report[0] = 0x22;
    report[1] = IAP2_LINK_CONTROL_CONTINUE;
    report[2] = 0xAA;
    report[3] = 0xBB;
    assert(iap2_rx_tap_process_report(&tap, fs_out_table, 6, IAP2_HID_FS_MAX_REPORT_LEN, report, 16)
           == IAP2_TAP_PASSTHROUGH);
    assert(!tap.reasm.in_progress);

    /* CONTINUE | MORE_TO_FOLLOW middle fragment for iAP1 */
    report[1] = IAP2_LINK_CONTROL_CONTINUE | IAP2_LINK_CONTROL_MORE_TO_FOLLOW;
    assert(iap2_rx_tap_process_report(&tap, fs_out_table, 6, IAP2_HID_FS_MAX_REPORT_LEN, report, 16)
           == IAP2_TAP_PASSTHROUGH);

    assert(tap.stats.rx_iap1_passthrough == 4U);
    assert(tap.stats.rx_malformed == 0U);
    assert_zero_tx_in_flight(&tap);
}

static void test_multi_report_iap2_reassembly(void)
{
    struct iap2_rx_tap tap;
    uint8_t syn_payload[32];
    uint8_t pkt[64];
    uint8_t report[20];
    size_t syn_len;
    size_t pkt_len = 0;
    static const uint8_t payload[15] = {
        0x40, 0x40, 0x00, 0x0F, 0xEA, 0x01, 0x02, 0x03,
        0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
    };
    const uint8_t peer_syn_seq = 0x20;

    assert(iap2_rx_tap_init(&tap) == IAP2_STATUS_OK);

    /* Bring link UP first */
    syn_len = build_syn_payload(syn_payload, sizeof(syn_payload));
    assert(iap2_encode_packet(pkt, sizeof(pkt), IAP2_CTL_SYN, peer_syn_seq, 0x00, 0x00,
                              syn_payload, syn_len, &pkt_len) == IAP2_STATUS_OK);
    {
        uint8_t syn_rep[32];
        wrap_single_report(syn_rep, 32, 0x13, 0x00, pkt, pkt_len);
        assert(iap2_rx_tap_process_report(&tap, hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN,
                                          syn_rep, 32) == IAP2_TAP_CLAIMED_ACK);
    }
    assert(iap2_encode_packet(pkt, sizeof(pkt), IAP2_CTL_ACK, peer_syn_seq, tap.summary.my_seq,
                              0x00, NULL, 0, &pkt_len) == IAP2_STATUS_OK);
    test_only_drain_tx(&tap);
    wrap_single_report(report, 20, 0x12, 0x00, pkt, pkt_len);
    assert(iap2_rx_tap_process_report(&tap, hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN,
                                      report, 20) == IAP2_TAP_CLAIMED_ACK);
    assert(tap.summary.link_state == IAP2_LINK_STATE_UP);

    /* Encode 25-byte data packet (9 header + 15 payload + 1 checksum) across two 20-byte reports */
    assert(iap2_encode_packet(pkt, sizeof(pkt), IAP2_CTL_ACK, (uint8_t)(peer_syn_seq + 1U),
                              tap.summary.my_seq, 0x0A, payload, sizeof(payload),
                              &pkt_len) == IAP2_STATUS_OK);
    assert(pkt_len == 25U);

    /* Fragment 1: 18 packet bytes + MORE_TO_FOLLOW */
    memset(report, 0, sizeof(report));
    report[0] = 0x12; /* HS row: body_size=19 -> expected_len=20, chunk cap=18 */
    report[1] = IAP2_LINK_CONTROL_MORE_TO_FOLLOW;
    memcpy(&report[2], &pkt[0], 18U);
    assert(iap2_rx_tap_process_report(&tap, hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN,
                                      report, 20) == IAP2_TAP_CLAIMED_ACK);
    assert(tap.reasm.in_progress);
    assert(tap.stats.rx_link_payloads == 0U);

    /* Fragment 2: remaining 7 packet bytes + CONTINUE + zero padding */
    memset(report, 0, sizeof(report));
    report[0] = 0x12;
    report[1] = IAP2_LINK_CONTROL_CONTINUE;
    memcpy(&report[2], &pkt[18], 7U);
    assert(iap2_rx_tap_process_report(&tap, hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN,
                                      report, 20) == IAP2_TAP_CLAIMED_ACK);
    assert(!tap.reasm.in_progress);
    assert(tap.stats.rx_link_payloads == 1U);
    assert(tap.summary.last_session == 0x0AU);
    assert(tap.summary.last_payload_len == 15U);
    assert_zero_tx_in_flight(&tap);
}

static void test_malformed_errors(void)
{
    struct iap2_rx_tap tap;
    uint8_t report[32];
    uint8_t pkt[32];
    size_t pkt_len = 0;

    assert(iap2_rx_tap_init(&tap) == IAP2_STATUS_OK);

    /* 1. Malformed checksum (corrupt header checksum at byte 8) */
    assert(iap2_encode_packet(pkt, sizeof(pkt), IAP2_CTL_ACK, 0x01, 0x00, 0x00,
                              NULL, 0, &pkt_len) == IAP2_STATUS_OK);
    pkt[8] ^= 0xFFU;
    wrap_single_report(report, 20, 0x12, 0x00, pkt, pkt_len);
    assert(iap2_rx_tap_process_report(&tap, hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN,
                                      report, 20) == IAP2_TAP_DROP_ERROR);
    assert(tap.stats.rx_bad_checksum == 1U);

    /* 2. Reserved bits in link_ctrl */
    assert(iap2_encode_packet(pkt, sizeof(pkt), IAP2_CTL_ACK, 0x01, 0x00, 0x00,
                              NULL, 0, &pkt_len) == IAP2_STATUS_OK);
    wrap_single_report(report, 20, 0x12, 0x04, pkt, pkt_len);
    assert(iap2_rx_tap_process_report(&tap, hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN,
                                      report, 20) == IAP2_TAP_DROP_ERROR);
    wrap_single_report(report, 20, 0x12, 0x05, pkt, pkt_len);
    assert(iap2_rx_tap_process_report(&tap, hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN,
                                      report, 20) == IAP2_TAP_DROP_ERROR);
    assert(tap.stats.rx_malformed == 2U);

    /* 3. Trailing non-zero padding in iAP2 packet report */
    wrap_single_report(report, 20, 0x12, 0x00, pkt, pkt_len);
    report[19] = 0x01;
    assert(iap2_rx_tap_process_report(&tap, hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN,
                                      report, 20) == IAP2_TAP_DROP_ERROR);
    assert(tap.stats.rx_malformed == 3U);

    /* 4. Trailing non-zero padding in probe report */
    {
        static const uint8_t probe_bytes[6] = { 0xFF, 0x55, 0x02, 0x00, 0xEE, 0x10 };
        wrap_single_report(report, 20, 0x12, 0x00, probe_bytes, sizeof(probe_bytes));
        report[10] = 0xAA;
        assert(iap2_rx_tap_process_report(&tap, hs_out_table, 10, IAP2_HID_HS_MAX_REPORT_LEN,
                                          report, 20) == IAP2_TAP_DROP_ERROR);
        assert(tap.stats.rx_malformed == 4U);
    }

    assert(!tap.reasm.in_progress);
    assert_zero_tx_in_flight(&tap);
}

int main(void)
{
    test_table_lookup_and_validation();
    test_bad_reports_and_mismatches();
    test_probe_syn_ack_data_and_zero_retention();
    test_iap1_passthrough_single_and_fragmented();
    test_multi_report_iap2_reassembly();
    test_malformed_errors();
    printf("rx_tap tests passed\n");
    return 0;
}
