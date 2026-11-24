// SPDX-License-Identifier: GPL-3.0-only
/*
 * Unit and integration tests for iAP2 link layer state machine.
 * RevolaOS authors, 2026.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "link.h"
#include "protocol.h"

static void build_test_packet(uint8_t *buf,
                              size_t buf_sz,
                              uint8_t control,
                              uint8_t seq,
                              uint8_t ack,
                              uint8_t session,
                              const uint8_t *payload,
                              size_t payload_len,
                              size_t *out_len)
{
    iap2_status_t st = iap2_encode_packet(buf, buf_sz, control, seq, ack,
                                          session, payload, payload_len, out_len);
    assert(st == IAP2_STATUS_OK);
}

/*
 * Establish a synchronized, UP link state through real probe + SYN + SYN-ACK + ACK
 * production handshake APIs.
 */
static void establish_test_link_capacity(struct iap2_link *link, size_t capacity)
{
    iap2_status_t st = iap2_link_init(link, capacity);
    assert(st == IAP2_STATUS_OK);

    st = iap2_link_handle_probe(link);
    assert(st == IAP2_STATUS_OK);
    const uint8_t *tx_frame = NULL;
    size_t tx_len = 0;
    st = iap2_link_peek_tx(link, &tx_frame, &tx_len);
    assert(st == IAP2_STATUS_OK);
    st = iap2_link_finish_tx(link, true);
    assert(st == IAP2_STATUS_OK);

    uint8_t syn_payload[] = {
        1,          /* version */
        4,          /* window */
        0x04, 0x00, /* 1024 max packet */
        0x00, 0xC8, /* 200 ms RTO */
        0x00, 0x32, /* 50 ms CACK */
        2,          /* 2 retransmissions (max_sends = 3) */
        3,          /* 3 max cacks */
        1, 0, 1,    /* session 1: type 0 (ctrl) */
        2, 1, 1     /* session 2: type 1 (file) */
    };
    uint8_t wire[128];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx_res;

    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 0x20, 0, 0,
                      syn_payload, sizeof(syn_payload), &wire_len);
    st = iap2_link_receive_packet(link, wire, wire_len, &rx_res);
    assert(st == IAP2_STATUS_OK);
    assert(rx_res.packet_handled);
    assert(link->synced);
    assert(!link->link_up);

    st = iap2_link_peek_tx(link, &tx_frame, &tx_len);
    assert(st == IAP2_STATUS_OK);
    st = iap2_link_finish_tx(link, true);
    assert(st == IAP2_STATUS_OK);
    assert(link->syn_ack_sent);

    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x21, 1, 0, NULL, 0, &wire_len);
    st = iap2_link_receive_packet(link, wire, wire_len, &rx_res);
    assert(st == IAP2_STATUS_OK);
    assert(rx_res.packet_handled);
    assert(iap2_link_is_up(link));
    assert(link->state == IAP2_LINK_STATE_UP);
}

static void establish_test_link(struct iap2_link *link)
{
    establish_test_link_capacity(link, 8192);
}

static void test_probe_lifecycle(void)
{
    struct iap2_link link;
    assert(iap2_link_init(&link, 8192) == IAP2_STATUS_OK);

    assert(!iap2_link_is_up(&link));
    assert(link.state == IAP2_LINK_STATE_DOWN);

    iap2_status_t st = iap2_link_handle_probe(&link);
    assert(st == IAP2_STATUS_OK);
    assert(!iap2_link_is_up(&link));
    assert((link.pending & IAP2_LINK_PENDING_ECHO) != 0);

    const uint8_t *tx_frame = NULL;
    size_t tx_len = 0;
    st = iap2_link_peek_tx(&link, &tx_frame, &tx_len);
    assert(st == IAP2_STATUS_OK);
    assert(tx_frame != NULL);
    assert(tx_len == IAP2_PROBE_LEN);
    assert(memcmp(tx_frame, iap2_probe_wire, IAP2_PROBE_LEN) == 0);

    /* Failed TX submit leaves pending echo set */
    st = iap2_link_finish_tx(&link, false);
    assert(st == IAP2_STATUS_OK);
    assert((link.pending & IAP2_LINK_PENDING_ECHO) != 0);

    /* Duplicate probe resets session and re-queues echo */
    st = iap2_link_handle_probe(&link);
    assert(st == IAP2_STATUS_OK);
    assert(!iap2_link_is_up(&link));

    /* Successful submit clears echo */
    tx_frame = NULL;
    tx_len = 0;
    st = iap2_link_peek_tx(&link, &tx_frame, &tx_len);
    assert(st == IAP2_STATUS_OK);
    assert(tx_len == IAP2_PROBE_LEN);
    st = iap2_link_finish_tx(&link, true);
    assert(st == IAP2_STATUS_OK);
    assert((link.pending & IAP2_LINK_PENDING_ECHO) == 0);

    tx_frame = NULL;
    tx_len = 0;
    st = iap2_link_peek_tx(&link, &tx_frame, &tx_len);
    assert(st == IAP2_STATUS_OK);
    assert(tx_frame == NULL && tx_len == 0);

    printf("PASS: test_probe_lifecycle\n");
}

static void test_syn_parameter_validation(void)
{
    struct iap2_link link;
    assert(iap2_link_init(&link, 8192) == IAP2_STATUS_OK);

    uint8_t wire[128];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx_res;

    /* Fixed length < 10 */
    uint8_t syn_short[9] = {1, 2, 0x02, 0x00, 0, 100, 0, 50, 2};
    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 1, 0, 0, syn_short, sizeof(syn_short), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_MALFORMED);
    assert(!rx_res.packet_handled);

    /* Non-triple session alignment */
    uint8_t syn_bad_align[11] = {1, 2, 0x02, 0x00, 0, 100, 0, 50, 2, 5, 0x01};
    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 1, 0, 0, syn_bad_align, sizeof(syn_bad_align), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_MALFORMED);

    /* Version != 1 */
    uint8_t syn_bad_ver[13] = {2, 2, 0x02, 0x00, 0, 100, 0, 50, 2, 5, 1, 0, 1};
    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 1, 0, 0, syn_bad_ver, sizeof(syn_bad_ver), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_MALFORMED);

    /* Window == 0 */
    uint8_t syn_zero_win[13] = {1, 0, 0x02, 0x00, 0, 100, 0, 50, 2, 5, 1, 0, 1};
    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 1, 0, 0, syn_zero_win, sizeof(syn_zero_win), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_MALFORMED);

    /* Peer packet size < 11 */
    uint8_t syn_small_pkt[13] = {1, 2, 0x00, 0x0A, 0, 100, 0, 50, 2, 5, 1, 0, 1};
    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 1, 0, 0, syn_small_pkt, sizeof(syn_small_pkt), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_MALFORMED);

    /* Session ID == 0 */
    uint8_t syn_zero_sid[13] = {1, 2, 0x02, 0x00, 0, 100, 0, 50, 2, 5, 0, 0, 1};
    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 1, 0, 0, syn_zero_sid, sizeof(syn_zero_sid), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_MALFORMED);

    /* Duplicate Session ID */
    uint8_t syn_dup_sid[16] = {1, 2, 0x02, 0x00, 0, 100, 0, 50, 2, 5, 1, 0, 1, 1, 1, 1};
    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 1, 0, 0, syn_dup_sid, sizeof(syn_dup_sid), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_MALFORMED);

    /* Duplicate Session Type */
    uint8_t syn_dup_type[16] = {1, 2, 0x02, 0x00, 0, 100, 0, 50, 2, 5, 1, 0, 1, 2, 0, 1};
    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 1, 0, 0, syn_dup_type, sizeof(syn_dup_type), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_MALFORMED);

    /* Absent control session */
    uint8_t syn_no_ctrl[13] = {1, 2, 0x02, 0x00, 0, 100, 0, 50, 2, 5, 1, 1, 1};
    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 1, 0, 0, syn_no_ctrl, sizeof(syn_no_ctrl), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_MALFORMED);

    /* Retrans != 0 with rto_raw == 0 */
    uint8_t syn_zero_rto[13] = {1, 2, 0x02, 0x00, 0, 0, 0, 50, 2, 5, 1, 0, 1};
    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 1, 0, 0, syn_zero_rto, sizeof(syn_zero_rto), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_MALFORMED);

    printf("PASS: test_syn_parameter_validation\n");
}

static void test_handshake_and_syn_ack(void)
{
    struct iap2_link link;
    assert(iap2_link_init(&link, 4096) == IAP2_STATUS_OK);

    iap2_link_handle_probe(&link);
    const uint8_t *tx_buf = NULL;
    size_t tx_sz = 0;
    iap2_link_peek_tx(&link, &tx_buf, &tx_sz);
    iap2_link_finish_tx(&link, true);

    uint8_t syn_payload[19] = {
        1,          /* ver */
        4,          /* window */
        0x04, 0x00, /* 1024 max packet */
        0x00, 0xC8, /* 200 ms RTO */
        0x00, 0x32, /* 50 ms CACK */
        2,          /* retransmissions */
        3,          /* max cacks */
        1, 0, 1,    /* session 1: type 0 (ctrl) */
        2, 1, 1,    /* session 2: type 1 (file) */
        3, 2, 1     /* session 3: type 2 (unsupported) */
    };
    uint8_t wire[128];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx_res;

    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 0x20, 0, 0,
                      syn_payload, sizeof(syn_payload), &wire_len);
    iap2_status_t st = iap2_link_receive_packet(&link, wire, wire_len, &rx_res);
    assert(st == IAP2_STATUS_OK);
    assert(rx_res.packet_handled);
    assert(!link.link_up);
    assert(link.synced);
    assert((link.pending & IAP2_LINK_PENDING_SYN_ACK) != 0);

    /* Peek SYN+ACK */
    st = iap2_link_peek_tx(&link, &tx_buf, &tx_sz);
    assert(st == IAP2_STATUS_OK);
    assert(tx_buf != NULL && tx_sz > IAP2_HEADER_LEN);

    struct iap2_packet_view syn_ack_view;
    assert(iap2_decode_packet(tx_buf, tx_sz, &syn_ack_view) == IAP2_STATUS_OK);
    assert(syn_ack_view.header.control == (IAP2_CTL_SYN | IAP2_CTL_ACK));
    assert(syn_ack_view.header.seq == 1);
    assert(syn_ack_view.header.ack == 0x20);
    assert(syn_ack_view.header.session == 0);

    /* Unsupported session 3 filtered out; offered window capped to 0x7F */
    assert(syn_ack_view.payload_len == 16);
    assert(syn_ack_view.payload[0] == 1);
    assert(syn_ack_view.payload[1] <= 0x7F);
    assert(syn_ack_view.payload[10] == 1 && syn_ack_view.payload[11] == 0);
    assert(syn_ack_view.payload[13] == 2 && syn_ack_view.payload[14] == 1);

    /* Failed submit retry check */
    st = iap2_link_finish_tx(&link, false);
    assert(st == IAP2_STATUS_OK);
    assert((link.pending & IAP2_LINK_PENDING_SYN_ACK) != 0);
    assert(!link.syn_ack_sent);

    /* Commit SYN+ACK */
    st = iap2_link_peek_tx(&link, &tx_buf, &tx_sz);
    assert(st == IAP2_STATUS_OK);
    st = iap2_link_finish_tx(&link, true);
    assert(st == IAP2_STATUS_OK);
    assert((link.pending & IAP2_LINK_PENDING_SYN_ACK) == 0);
    assert(link.syn_ack_sent);

    /* Duplicate SYN re-arms SYN+ACK without losing state */
    st = iap2_link_receive_packet(&link, wire, wire_len, &rx_res);
    assert(st == IAP2_STATUS_OK);
    assert(link.my_seq == 1);
    assert((link.pending & IAP2_LINK_PENDING_SYN_ACK) != 0);
    iap2_link_peek_tx(&link, &tx_buf, &tx_sz);
    iap2_link_finish_tx(&link, true);

    /* Peer ACK brings link UP */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x21, 1, 0, NULL, 0, &wire_len);
    st = iap2_link_receive_packet(&link, wire, wire_len, &rx_res);
    assert(st == IAP2_STATUS_OK);
    assert(iap2_link_is_up(&link));
    assert(link.state == IAP2_LINK_STATE_UP);

    printf("PASS: test_handshake_and_syn_ack\n");
}

static void test_data_queueing_and_tx(void)
{
    struct iap2_link link;
    establish_test_link(&link);
    link.peer_max = 512;

    /* Queue control payload */
    uint8_t payload1[] = {0x11, 0x22, 0x33, 0x44};
    iap2_status_t st = iap2_link_queue_payload(&link, 1, payload1, sizeof(payload1));
    assert(st == IAP2_STATUS_OK);
    assert(link.count == 1);

    /* Queue file payload */
    uint8_t payload2[] = {0xAA, 0xBB};
    st = iap2_link_queue_payload(&link, 2, payload2, sizeof(payload2));
    assert(st == IAP2_STATUS_OK);
    assert(link.count == 2);

    /* Invalid session rejected */
    st = iap2_link_queue_payload(&link, 3, payload1, sizeof(payload1));
    assert(st == IAP2_STATUS_INVALID_ARG);

    /* Oversized payload rejected */
    uint8_t big[600];
    memset(big, 0x55, sizeof(big));
    st = iap2_link_queue_payload(&link, 1, big, sizeof(big));
    assert(st == IAP2_STATUS_BUFFER_TOO_SMALL);

    /* Fill queue to max 4 */
    st = iap2_link_queue_payload(&link, 1, payload1, sizeof(payload1));
    assert(st == IAP2_STATUS_OK);
    st = iap2_link_queue_payload(&link, 1, payload1, sizeof(payload1));
    assert(st == IAP2_STATUS_OK);
    assert(link.count == 4);

    /* 5th queue fails */
    st = iap2_link_queue_payload(&link, 1, payload1, sizeof(payload1));
    assert(st == IAP2_STATUS_BUFFER_TOO_SMALL);

    /* Peek TX returns first packet */
    const uint8_t *tx_frame = NULL;
    size_t tx_len = 0;
    st = iap2_link_peek_tx(&link, &tx_frame, &tx_len);
    assert(st == IAP2_STATUS_OK);
    assert(tx_frame != NULL);
    assert(link.slots[0].in_flight);
    assert(iap2_link_mark_submitted(&link) == IAP2_STATUS_OK);

    /* In-flight ACK arrived while transmitting slot 0: must NOT free slot 0 */
    uint8_t wire[64];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx_res;
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x21, link.slots[0].seq, 0, NULL, 0, &wire_len);
    st = iap2_link_receive_packet(&link, wire, wire_len, &rx_res);
    assert(st == IAP2_STATUS_OK);
    assert(rx_res.packet_handled);
    assert(link.count == 4); /* Deferred until finish_tx */

    /* Complete TX: now freed */
    st = iap2_link_finish_tx(&link, true);
    assert(st == IAP2_STATUS_OK);
    assert(link.count == 3);

    printf("PASS: test_data_queueing_and_tx\n");
}

static void test_sequence_cumulative_future_and_wrap(void)
{
    struct iap2_link link;
    establish_test_link(&link);

    link.my_seq = 254;
    link.last_ack = 254;
    link.highest_sent_seq = 254;

    /* Queue 3 packets crossing sequence wrap: 255, 0, 1 */
    uint8_t p = 0x55;
    assert(iap2_link_queue_payload(&link, 1, &p, 1) == IAP2_STATUS_OK);
    assert(iap2_link_queue_payload(&link, 1, &p, 1) == IAP2_STATUS_OK);
    assert(iap2_link_queue_payload(&link, 1, &p, 1) == IAP2_STATUS_OK);
    assert(link.count == 3);
    assert(link.slots[0].seq == 255);
    assert(link.slots[1].seq == 0);
    assert(link.slots[2].seq == 1);

    /* Send all 3 */
    const uint8_t *frame;
    size_t len;
    iap2_link_peek_tx(&link, &frame, &len);
    iap2_link_finish_tx(&link, true);
    iap2_link_peek_tx(&link, &frame, &len);
    iap2_link_finish_tx(&link, true);
    iap2_link_peek_tx(&link, &frame, &len);
    iap2_link_finish_tx(&link, true);
    assert(link.count == 3);
    assert(link.highest_sent_seq == 1);

    uint8_t wire[64];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx_res;

    /* Future ACK (e.g. 5) must be ignored and not free unsent slots */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x11, 5, 0, NULL, 0, &wire_len);
    iap2_link_receive_packet(&link, wire, wire_len, &rx_res);
    assert(link.count == 3);

    /* Cumulative ACK for 0 frees 255 and 0 */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x11, 0, 0, NULL, 0, &wire_len);
    iap2_link_receive_packet(&link, wire, wire_len, &rx_res);
    assert(link.count == 1);
    assert(link.slots[link.head].seq == 1);

    /* Cumulative ACK for 1 frees the remaining slot */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x11, 1, 0, NULL, 0, &wire_len);
    iap2_link_receive_packet(&link, wire, wire_len, &rx_res);
    assert(link.count == 0);

    printf("PASS: test_sequence_cumulative_future_and_wrap\n");
}

static void test_peer_seq_duplicates_and_rx_payload(void)
{
    struct iap2_link link;
    establish_test_link(&link);
    link.peer_seq = 10;
    link.cack_ms = 50;
    link.max_cacks = 2;

    uint8_t wire[64];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx_res;
    uint8_t data[] = {0x12, 0x34};

    /* In-order packet (seq=11) -> delivered */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 11, 1, 1, data, sizeof(data), &wire_len);
    iap2_status_t st = iap2_link_receive_packet(&link, wire, wire_len, &rx_res);
    assert(st == IAP2_STATUS_OK);
    assert(rx_res.has_payload);
    assert(rx_res.payload_len == 2);
    assert(link.peer_seq == 11);

    /* Duplicate packet (seq=11) -> rejected, no payload delivered, bare ACK queued */
    rx_res.has_payload = false;
    st = iap2_link_receive_packet(&link, wire, wire_len, &rx_res);
    assert(st == IAP2_STATUS_OK);
    assert(!rx_res.has_payload);
    assert(link.peer_seq == 11);
    assert((link.pending & IAP2_LINK_PENDING_ACK) != 0);

    /* Bare ACK packet does NOT advance peer_seq */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 12, 1, 0, NULL, 0, &wire_len);
    st = iap2_link_receive_packet(&link, wire, wire_len, &rx_res);
    assert(st == IAP2_STATUS_OK);
    assert(!rx_res.has_payload);
    assert(link.peer_seq == 11);

    printf("PASS: test_peer_seq_duplicates_and_rx_payload\n");
}

static void test_retransmission_and_timeout_exhaustion(void)
{
    struct iap2_link link;
    establish_test_link(&link);
    link.window = 1;
    link.rto_ms = 200;
    link.max_sends = 2;

    uint8_t p = 0x99;
    iap2_link_queue_payload(&link, 1, &p, 1);

    /* Transmit try 1 at 1000ms */
    link.last_now_ms = 1000;
    const uint8_t *frame;
    size_t len;
    iap2_link_peek_tx(&link, &frame, &len);
    iap2_link_finish_tx(&link, true);
    assert(link.slots[0].tries == 1);

    /* Tick at 1100ms: not expired */
    iap2_link_tick(&link, 1100);
    assert(!link.slots[0].queued);

    /* Tick at 1205ms: expired -> re-queued */
    iap2_link_tick(&link, 1205);
    assert(link.slots[0].queued);

    /* Transmit try 2 at 1205ms */
    iap2_link_peek_tx(&link, &frame, &len);
    iap2_link_finish_tx(&link, true);
    assert(link.slots[0].tries == 2);

    /* Tick at 1410ms: max_sends reached -> timeout exhaustion resets link and queues RST */
    iap2_link_tick(&link, 1410);
    assert(!link.link_up);
    assert(link.timeout_exhausted);
    assert((link.pending & IAP2_LINK_PENDING_RST) != 0);

    /* Peek TX sends RST */
    iap2_link_peek_tx(&link, &frame, &len);
    struct iap2_packet_view rst_view;
    assert(iap2_decode_packet(frame, len, &rst_view) == IAP2_STATUS_OK);
    assert(rst_view.header.control == IAP2_CTL_RST);

    printf("PASS: test_retransmission_and_timeout_exhaustion\n");
}

static void test_no_retransmission_zero_rto(void)
{
    struct iap2_link link;
    assert(iap2_link_init(&link, 8192) == IAP2_STATUS_OK);

    /* SYN with retrans = 0, rto_raw = 0 */
    uint8_t syn_zero_rto[] = {
        1,          /* version */
        2,          /* window = 2 */
        0x02, 0x00, /* 512 max packet */
        0x00, 0x00, /* 0 ms RTO */
        0x00, 0x32, /* 50 ms CACK */
        0,          /* retrans = 0 */
        3,          /* max cacks */
        1, 0, 1     /* ctrl session 1 */
    };
    uint8_t wire[128];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx_res;

    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 0x10, 0, 0,
                      syn_zero_rto, sizeof(syn_zero_rto), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_OK);

    const uint8_t *frame = NULL;
    size_t len = 0;
    iap2_link_peek_tx(&link, &frame, &len);
    iap2_link_finish_tx(&link, true);

    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x11, 1, 0, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_OK);
    assert(iap2_link_is_up(&link));
    assert(link.rto_ms == 0);
    assert(link.max_sends == 1);
    assert(link.window == 2);

    /* Queue 3 packets */
    uint8_t p1 = 0x11, p2 = 0x22, p3 = 0x33;
    assert(iap2_link_queue_payload(&link, 1, &p1, 1) == IAP2_STATUS_OK);
    assert(iap2_link_queue_payload(&link, 1, &p2, 1) == IAP2_STATUS_OK);
    assert(iap2_link_queue_payload(&link, 1, &p3, 1) == IAP2_STATUS_OK);
    assert(link.count == 3);

    /* Send packet 1: finish_tx must NOT drop unacked slot */
    iap2_link_peek_tx(&link, &frame, &len);
    iap2_link_finish_tx(&link, true);
    assert(link.count == 3);

    /* Send packet 2: consumes peer window 2 */
    iap2_link_peek_tx(&link, &frame, &len);
    iap2_link_finish_tx(&link, true);
    assert(link.count == 3);

    /* 3rd packet is withheld by window limit */
    frame = NULL;
    len = 0;
    iap2_link_peek_tx(&link, &frame, &len);
    assert(frame == NULL && len == 0);

    /* Tick must not expire or retransmit rto_ms == 0 slots */
    iap2_link_tick(&link, 50000);
    assert(link.count == 3);
    assert(!link.timeout_exhausted);

    /* Peer ACK frees packet 1 and advances window */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x12, link.slots[0].seq, 0, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_OK);
    assert(link.count == 2);

    /* Packet 3 is now released by peek_tx */
    iap2_link_peek_tx(&link, &frame, &len);
    assert(frame != NULL);
    iap2_link_finish_tx(&link, true);

    /* Cumulative ACK for packet 3 frees remaining slots */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x13, link.slots[link.head + 1].seq, 0, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_OK);
    assert(link.count == 0);

    printf("PASS: test_no_retransmission_zero_rto\n");
}

static void test_time_wrap_safe(void)
{
    struct iap2_link link;
    establish_test_link(&link);
    link.window = 1;
    link.rto_ms = 100;
    link.max_sends = 3;

    uint8_t p = 0x33;
    iap2_link_queue_payload(&link, 1, &p, 1);

    /* Sent near uint32 wrap */
    link.last_now_ms = 0xFFFFFFF0;
    const uint8_t *frame;
    size_t len;
    iap2_link_peek_tx(&link, &frame, &len);
    iap2_link_finish_tx(&link, true);

    /* Before expiry (wrapped) */
    iap2_link_tick(&link, 0x00000020);
    assert(!link.slots[0].queued);

    /* After expiry (wrapped) */
    iap2_link_tick(&link, 0x00000060);
    assert(link.slots[0].queued);

    printf("PASS: test_time_wrap_safe\n");
}

static void test_mutation_fuzz_and_null_pointers(void)
{
    struct iap2_link link;
    assert(iap2_link_init(&link, 8192) == IAP2_STATUS_OK);

    assert(iap2_link_handle_probe(NULL) == IAP2_STATUS_INVALID_ARG);
    assert(iap2_link_receive_packet(NULL, NULL, 0, NULL) == IAP2_STATUS_INVALID_ARG);
    assert(iap2_link_tick(NULL, 0) == IAP2_STATUS_INVALID_ARG);
    assert(iap2_link_queue_payload(NULL, 1, NULL, 0) == IAP2_STATUS_INVALID_ARG);
    assert(iap2_link_peek_tx(NULL, NULL, NULL) == IAP2_STATUS_INVALID_ARG);
    assert(iap2_link_mark_submitted(NULL) == IAP2_STATUS_INVALID_ARG);
    assert(iap2_link_finish_tx(NULL, false) == IAP2_STATUS_INVALID_ARG);
    iap2_link_reset(NULL);
    iap2_link_disconnect(NULL);

    uint8_t wire[64];
    size_t wire_len = 0;
    uint8_t data[] = {0xDE, 0xAD};
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 1, 1, 1, data, sizeof(data), &wire_len);

    struct iap2_link_rx_result rx_res;
    for (size_t i = 0; i < wire_len; i++) {
        uint8_t corrupted[64];
        memcpy(corrupted, wire, wire_len);
        corrupted[i] ^= 0xFF;
        iap2_status_t st = iap2_link_receive_packet(&link, corrupted, wire_len, &rx_res);
        assert(st != IAP2_STATUS_OK || !rx_res.has_payload);
    }

    printf("PASS: test_mutation_fuzz_and_null_pointers\n");
}

/* --------------------------------------------------------------------------
 * Targeted Regression Tests for Blocking Review Findings
 * -------------------------------------------------------------------------- */

static void test_tx_immutability_and_stale_fence(void)
{
    struct iap2_link link;
    establish_test_link(&link);

    uint8_t payload[] = {1, 2, 3, 4};
    assert(iap2_link_queue_payload(&link, 1, payload, sizeof(payload)) == IAP2_STATUS_OK);

    const uint8_t *frame = NULL;
    size_t len = 0;
    assert(iap2_link_peek_tx(&link, &frame, &len) == IAP2_STATUS_OK);
    assert(link.tx_in_flight);
    const uint8_t *in_flight_buf = frame;

    /* Peer probe arrives while transfer is on wire */
    assert(iap2_link_handle_probe(&link) == IAP2_STATUS_OK);
    assert(link.tx_stale);
    assert(link.tx_in_flight);

    /* In-flight pointer remains immutable; peek returns active transfer */
    const uint8_t *ref_frame = NULL;
    size_t ref_len = 0;
    assert(iap2_link_peek_tx(&link, &ref_frame, &ref_len) == IAP2_STATUS_OK);
    assert(ref_frame == in_flight_buf);

    /* Complete stale TX */
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert(!link.tx_in_flight);
    assert(!link.tx_stale);

    /* Now probe echo frame is released */
    assert(iap2_link_peek_tx(&link, &ref_frame, &ref_len) == IAP2_STATUS_OK);
    assert(ref_len == IAP2_PROBE_LEN);
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);

    printf("PASS: test_tx_immutability_and_stale_fence\n");
}

static void test_syn_ack_premature_ack_rejected(void)
{
    struct iap2_link link;
    assert(iap2_link_init(&link, 8192) == IAP2_STATUS_OK);

    uint8_t syn_payload[13] = {1, 2, 0x02, 0x00, 0, 100, 0, 50, 2, 5, 1, 0, 1};
    uint8_t wire[128];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx_res;

    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 0x10, 0, 0,
                      syn_payload, sizeof(syn_payload), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_OK);
    assert(link.synced);
    assert(!link.syn_ack_sent);

    /* Early ACK received BEFORE SYN+ACK finish_tx(true) */
    uint8_t data[] = {0xAA};
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x11, 1, 1, data, sizeof(data), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_OK);
    assert(!iap2_link_is_up(&link));
    assert(!rx_res.has_payload);

    /* Transmit and finish SYN+ACK */
    const uint8_t *tx_frame;
    size_t tx_len;
    assert(iap2_link_peek_tx(&link, &tx_frame, &tx_len) == IAP2_STATUS_OK);
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert(link.syn_ack_sent);

    /* Valid handshake ACK now transitions link to UP */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x12, 1, 0, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_OK);
    assert(iap2_link_is_up(&link));

    printf("PASS: test_syn_ack_premature_ack_rejected\n");
}

static void test_ack_bounds_and_finish_failure_discard(void)
{
    struct iap2_link link;
    establish_test_link(&link);

    uint8_t p = 0x42;
    assert(iap2_link_queue_payload(&link, 1, &p, 1) == IAP2_STATUS_OK);
    assert(iap2_link_queue_payload(&link, 1, &p, 1) == IAP2_STATUS_OK);
    assert(link.count == 2);

    /* Peek packet 1 (seq = 2) */
    const uint8_t *frame;
    size_t len;
    assert(iap2_link_peek_tx(&link, &frame, &len) == IAP2_STATUS_OK);
    assert(iap2_link_mark_submitted(&link) == IAP2_STATUS_OK);
    assert(link.tx_submitted);

    uint8_t wire[64];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx_res;

    /* Future ACK for unsent packet (seq = 3) must be rejected */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x21, 3, 0, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_OK);
    assert(link.count == 2);

    /* ACK for in-flight packet (seq = 2) arrives before completion */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x22, 2, 0, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_OK);
    assert(link.has_deferred_ack);
    assert(link.count == 2);

    /* Bridge transfer fails: deferred ACK must be discarded */
    assert(iap2_link_finish_tx(&link, false) == IAP2_STATUS_OK);
    assert(!link.has_deferred_ack);
    assert(link.count == 2);
    assert(link.slots[0].queued);

    /* Retry packet 1 successfully */
    assert(iap2_link_peek_tx(&link, &frame, &len) == IAP2_STATUS_OK);
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert(link.highest_sent_seq == 2);

    /* Now ACK for seq = 2 frees packet 1 */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x23, 2, 0, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_OK);
    assert(link.count == 1);

    printf("PASS: test_ack_bounds_and_finish_failure_discard\n");
}

static void test_rx_arrival_during_inflight_preserves_ack(void)
{
    struct iap2_link link;
    establish_test_link(&link);
    link.cack_ms = 50;
    link.max_cacks = 1;

    uint8_t p = 0x55;
    assert(iap2_link_queue_payload(&link, 1, &p, 1) == IAP2_STATUS_OK);

    const uint8_t *frame;
    size_t len;
    assert(iap2_link_peek_tx(&link, &frame, &len) == IAP2_STATUS_OK);
    assert(link.tx_in_flight);

    /* RX packet arrives while DATA is in flight */
    uint8_t wire[64];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx_res;
    uint8_t rx_data[] = {0xDE, 0xAD};
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, (uint8_t)(link.peer_seq + 1U), 1, 1,
                      rx_data, sizeof(rx_data), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_OK);
    assert(rx_res.has_payload);

    /* Finish in-flight TX: pending ACK for newer RX must NOT be lost */
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert((link.pending & IAP2_LINK_PENDING_ACK) != 0);

    /* Peek TX emits bare ACK acknowledging newer RX seq */
    assert(iap2_link_peek_tx(&link, &frame, &len) == IAP2_STATUS_OK);
    struct iap2_packet_view ack_view;
    assert(iap2_decode_packet(frame, len, &ack_view) == IAP2_STATUS_OK);
    assert(ack_view.header.control == IAP2_CTL_ACK);
    assert(ack_view.header.ack == link.peer_seq);
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);

    printf("PASS: test_rx_arrival_during_inflight_preserves_ack\n");
}

static void test_max_sends_256_attempts(void)
{
    struct iap2_link link;
    assert(iap2_link_init(&link, 8192) == IAP2_STATUS_OK);

    /* SYN specifying 255 retransmissions -> max_sends = 256 */
    uint8_t syn_255[13] = {1, 2, 0x02, 0x00, 0, 100, 0, 50, 255, 3, 1, 0, 1};
    uint8_t wire[128];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx_res;

    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 0x10, 0, 0,
                      syn_255, sizeof(syn_255), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_OK);
    assert(link.max_sends == 256);

    const uint8_t *frame;
    size_t len;
    iap2_link_peek_tx(&link, &frame, &len);
    iap2_link_finish_tx(&link, true);

    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x11, 1, 0, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_OK);

    uint8_t p = 0x01;
    assert(iap2_link_queue_payload(&link, 1, &p, 1) == IAP2_STATUS_OK);

    /* Simulate 255 failed transmissions without 8-bit wrap */
    link.slots[0].tries = 255;
    link.slots[0].queued = false;
    link.slots[0].sent_ms = 1000;

    iap2_link_tick(&link, 1200);
    assert(link.slots[0].queued);
    assert(!link.timeout_exhausted);

    /* Attempt 256 reaches exhaustion */
    link.slots[0].tries = 256;
    link.slots[0].queued = false;
    link.slots[0].sent_ms = 1200;
    iap2_link_tick(&link, 1400);
    assert(!link.link_up);
    assert(link.timeout_exhausted);

    printf("PASS: test_max_sends_256_attempts\n");
}

static void test_bounded_input_and_malformed_syn_rejection(void)
{
    struct iap2_link link;
    assert(iap2_link_init(&link, 10) == IAP2_STATUS_INVALID_ARG);
    assert(iap2_link_init(&link, 9000) == IAP2_STATUS_INVALID_ARG);
    establish_test_link_capacity(&link, 2048);
    assert(link.rx_capacity == 2048);

    /* Packet larger than configured rx_capacity rejected */
    uint8_t large_packet[2050];
    memset(large_packet, 0, sizeof(large_packet));
    struct iap2_link_rx_result rx_res;
    assert(iap2_link_receive_packet(&link, large_packet, sizeof(large_packet), &rx_res) ==
           IAP2_STATUS_BUFFER_TOO_SMALL);
    assert(!rx_res.packet_handled);

    /* Integer overflow in payload queueing rejected */
    uint8_t p = 0xAA;
    assert(iap2_link_queue_payload(&link, 1, &p, (size_t)-1) == IAP2_STATUS_BUFFER_TOO_SMALL);

    /* Malformed SYN while link is UP must NOT tear down active session */
    uint8_t bad_syn_payload[10] = {2, 0, 0x02, 0x00, 0, 100, 0, 50, 2, 5};
    uint8_t wire[128];
    size_t wire_len = 0;
    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 0x50, 0, 0,
                      bad_syn_payload, sizeof(bad_syn_payload), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_MALFORMED);
    assert(!rx_res.packet_handled);
    assert(iap2_link_is_up(&link));

    /* Payload on unnegotiated session rejected without advancing peer_seq */
    uint8_t bad_sess_data[] = {1, 2};
    uint8_t cur_peer_seq = link.peer_seq;
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, (uint8_t)(cur_peer_seq + 1U), 1, 99,
                      bad_sess_data, sizeof(bad_sess_data), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_MALFORMED);
    assert(!rx_res.packet_handled);
    assert(link.peer_seq == cur_peer_seq);

    printf("PASS: test_bounded_input_and_malformed_syn_rejection\n");
}

static void test_cack_zero_no_bare_ack_and_piggyback(void)
{
    struct iap2_link link;
    establish_test_link(&link);
    link.cack_ms = 0;

    uint8_t wire[64];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx_res;
    uint8_t data[] = {1, 2};

    /* In-order packet for cack_ms == 0 suppresses bare ACK. */
    link.pending &= ~IAP2_LINK_PENDING_ACK;
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, (uint8_t)(link.peer_seq + 1U), 1, 1,
                      data, sizeof(data), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_OK);
    assert(rx_res.has_payload);
    assert((link.pending & IAP2_LINK_PENDING_ACK) == 0);

    /* Tick suppresses bare ACK when cack_ms == 0. */
    assert(iap2_link_tick(&link, 1000) == IAP2_STATUS_OK);
    assert((link.pending & IAP2_LINK_PENDING_ACK) == 0);

    const uint8_t *frame = NULL;
    size_t len = 0;
    assert(iap2_link_peek_tx(&link, &frame, &len) == IAP2_STATUS_OK);
    assert(frame == NULL && len == 0);

    /* Duplicate packet for cack_ms == 0 suppresses bare ACK. */
    rx_res.has_payload = false;
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, link.peer_seq, 1, 1,
                      data, sizeof(data), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_OK);
    assert(!rx_res.has_payload);
    assert((link.pending & IAP2_LINK_PENDING_ACK) == 0);

    assert(iap2_link_peek_tx(&link, &frame, &len) == IAP2_STATUS_OK);
    assert(frame == NULL && len == 0);

    /* Outgoing DATA must piggyback latest peer_seq. */
    uint8_t out_data[] = {0xAA, 0xBB};
    assert(iap2_link_queue_payload(&link, 1, out_data, sizeof(out_data)) == IAP2_STATUS_OK);
    assert(iap2_link_peek_tx(&link, &frame, &len) == IAP2_STATUS_OK);
    assert(frame != NULL && len > 0);

    struct iap2_packet_view pv;
    assert(iap2_decode_packet(frame, len, &pv) == IAP2_STATUS_OK);
    assert((pv.header.control & IAP2_CTL_ACK) != 0);
    assert(pv.header.ack == link.peer_seq);

    /* finish_tx bookkeeping does not trigger bare ACK for cack_ms == 0. */
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert((link.pending & IAP2_LINK_PENDING_ACK) == 0);

    frame = NULL;
    len = 0;
    assert(iap2_link_peek_tx(&link, &frame, &len) == IAP2_STATUS_OK);
    assert(frame == NULL && len == 0);

    printf("PASS: test_cack_zero_no_bare_ack_and_piggyback\n");
}

static void test_eak_validation(void)
{
    struct iap2_link link;
    establish_test_link(&link);

    uint8_t p = 0x11;
    assert(iap2_link_queue_payload(&link, 1, &p, 1) == IAP2_STATUS_OK);
    const uint8_t *frame;
    size_t len;
    iap2_link_peek_tx(&link, &frame, &len);
    iap2_link_finish_tx(&link, true);

    /* EAK listing seq must not re-queue already received sequence */
    uint8_t wire[64];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx_res;
    uint8_t eak_payload[] = {link.slots[0].seq};
    build_test_packet(wire, sizeof(wire), IAP2_CTL_EAK, 0x22, 1, 0,
                      eak_payload, sizeof(eak_payload), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_OK);
    assert(!link.slots[0].queued);

    printf("PASS: test_eak_validation\n");
}

static void test_malformed_eak_rejection(void)
{
    struct iap2_link link;
    establish_test_link(&link);

    uint8_t p = 0x77;
    assert(iap2_link_queue_payload(&link, 1, &p, 1) == IAP2_STATUS_OK);
    const uint8_t *frame;
    size_t len;
    assert(iap2_link_peek_tx(&link, &frame, &len) == IAP2_STATUS_OK);
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert(link.count == 1);

    uint8_t wire[64];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx_res;
    uint8_t eak_payload[] = {link.slots[0].seq};

    /* EAK on session != 0 rejected without state mutation. */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_EAK | IAP2_CTL_ACK, 0x22, link.slots[0].seq,
                      1, eak_payload, sizeof(eak_payload), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_MALFORMED);
    assert(!rx_res.packet_handled);
    assert(link.count == 1);

    /* Empty payload EAK rejected. */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_EAK, 0x22, 1, 0, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_MALFORMED);
    assert(!rx_res.packet_handled);
    assert(link.count == 1);

    /* Illegal control flags accompanying EAK rejected. */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_EAK | IAP2_CTL_RST, 0x22, 1, 0,
                      eak_payload, sizeof(eak_payload), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_MALFORMED);
    assert(!rx_res.packet_handled);
    assert(link.count == 1);

    printf("PASS: test_malformed_eak_rejection\n");
}

static void test_multi_slot_rto_scan(void)
{
    struct iap2_link link;
    establish_test_link(&link);
    link.window = 2;
    link.rto_ms = 100;
    link.max_sends = 3;

    uint8_t p1 = 0xAA, p2 = 0xBB;
    assert(iap2_link_queue_payload(&link, 1, &p1, 1) == IAP2_STATUS_OK);
    assert(iap2_link_queue_payload(&link, 1, &p2, 1) == IAP2_STATUS_OK);

    link.last_now_ms = 1000;
    const uint8_t *frame;
    size_t len;
    iap2_link_peek_tx(&link, &frame, &len);
    iap2_link_finish_tx(&link, true);

    link.last_now_ms = 1010;
    iap2_link_peek_tx(&link, &frame, &len);
    iap2_link_finish_tx(&link, true);

    /* Head is retransmitted at 1105ms, refreshing sent_ms */
    iap2_link_tick(&link, 1105);
    assert(link.slots[0].queued);
    link.last_now_ms = 1105;
    iap2_link_peek_tx(&link, &frame, &len);
    iap2_link_finish_tx(&link, true);

    /* At 1120ms, slot 1 has expired (1010 + 100 <= 1120). Tick scan must re-queue slot 1 */
    iap2_link_tick(&link, 1120);
    assert(link.slots[1].queued);

    printf("PASS: test_multi_slot_rto_scan\n");
}

/* --------------------------------------------------------------------------
 * Precise Regressions for Counter Debt Modulo and Early Mark Submitted ACKs
 * -------------------------------------------------------------------------- */

static void test_unacked_256_rx_during_tx_borrow_reproducer(void)
{
    struct iap2_link link;
    establish_test_link(&link);
    link.max_cacks = 0;
    link.cack_ms = 50;

    const uint8_t data = 0xA5;
    const uint8_t *frame = NULL;
    size_t frame_len = 0;
    size_t wire_len = 0;
    uint8_t wire[64];
    struct iap2_link_rx_result rx;

    assert(iap2_link_queue_payload(&link, 1, &data, 1) == IAP2_STATUS_OK);
    assert(iap2_link_peek_tx(&link, &frame, &frame_len) == IAP2_STATUS_OK);
    assert(link.tx_rx_ack == 0);

    /* 256 peer data packets arrive while DATA is borrowed */
    for (unsigned i = 0; i < 256; i++) {
        build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK,
                          (uint8_t)(link.peer_seq + 1U), 1, 1,
                          &data, 1, &wire_len);
        assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);
        assert(rx.has_payload);
    }

    assert(link.rx_accepted == 256);
    assert(link.unacked == 256);

    /* Complete transmission: modulo 256 must NOT erase unacked debt */
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert(link.unacked == 256);

    /* Tick expires CACK timer (50ms) */
    assert(iap2_link_tick(&link, 100) == IAP2_STATUS_OK);
    assert((link.pending & IAP2_LINK_PENDING_ACK) != 0);

    /* Outgoing bare ACK acknowledges debt */
    frame = NULL;
    frame_len = 0;
    assert(iap2_link_peek_tx(&link, &frame, &frame_len) == IAP2_STATUS_OK);
    assert(frame != NULL);
    assert(link.tx_rx_ack == 256);
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert(link.unacked == 0);
    assert((link.pending & IAP2_LINK_PENDING_ACK) == 0);

    printf("PASS: test_unacked_256_rx_during_tx_borrow_reproducer\n");
}

static void test_rx_counter_wrap_and_cack_debt(void)
{
    struct iap2_link link;
    establish_test_link(&link);
    link.max_cacks = 0;
    link.cack_ms = 50;

    /* Simulate wide counter operating near 32-bit integer wrap boundary */
    link.rx_accepted = UINT32_MAX - 2U;
    link.last_sent_rx_ack = UINT32_MAX - 2U;
    link.unacked = 0;

    const uint8_t data = 0x55;
    const uint8_t *frame = NULL;
    size_t frame_len = 0;
    size_t wire_len = 0;
    uint8_t wire[64];
    struct iap2_link_rx_result rx;

    assert(iap2_link_queue_payload(&link, 1, &data, 1) == IAP2_STATUS_OK);
    assert(iap2_link_peek_tx(&link, &frame, &frame_len) == IAP2_STATUS_OK);
    assert(link.tx_rx_ack == UINT32_MAX - 2U);

    /* Receive 5 packets: rx_accepted wraps around UINT32_MAX */
    for (unsigned i = 0; i < 5; i++) {
        build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK,
                          (uint8_t)(link.peer_seq + 1U), 1, 1,
                          &data, 1, &wire_len);
        assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);
    }

    assert(link.rx_accepted == 2U);
    assert(link.unacked == 5U);

    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert(link.unacked == 5U);

    assert(iap2_link_tick(&link, 100) == IAP2_STATUS_OK);
    assert((link.pending & IAP2_LINK_PENDING_ACK) != 0);

    assert(iap2_link_peek_tx(&link, &frame, &frame_len) == IAP2_STATUS_OK);
    assert(link.tx_rx_ack == 2U);
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert(link.unacked == 0);

    printf("PASS: test_rx_counter_wrap_and_cack_debt\n");
}

static void test_newer_rx_while_data_ack_inflight(void)
{
    struct iap2_link link;
    establish_test_link(&link);
    link.max_cacks = 3;
    link.cack_ms = 50;

    const uint8_t data = 0x33;
    uint8_t wire[64];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx;

    /* 1 packet received: unacked = 1 */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK,
                      (uint8_t)(link.peer_seq + 1U), 1, 1,
                      &data, 1, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);
    assert(link.unacked == 1);

    /* Queue and peek DATA: captures tx_unacked = 1, live unacked reset to 0 */
    const uint8_t *frame = NULL;
    size_t frame_len = 0;
    assert(iap2_link_queue_payload(&link, 1, &data, 1) == IAP2_STATUS_OK);
    assert(iap2_link_peek_tx(&link, &frame, &frame_len) == IAP2_STATUS_OK);
    assert(link.tx_rx_ack == 1);
    assert(link.tx_unacked == 1);
    assert(link.unacked == 0);

    /* 3 newer packets arrive while DATA is in flight */
    for (unsigned i = 0; i < 3; i++) {
        build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK,
                          (uint8_t)(link.peer_seq + 1U), 1, 1,
                          &data, 1, &wire_len);
        assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);
    }
    assert(link.rx_accepted == 4);
    assert(link.unacked == 3);
    assert((link.pending & IAP2_LINK_PENDING_ACK) != 0);

    /* Finish DATA: pre-borrow debt acknowledged, post-borrow debt remains 3 */
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert(link.unacked == 3);
    assert((link.pending & IAP2_LINK_PENDING_ACK) != 0);

    /* Peek bare ACK: captures tx_unacked = 3, live unacked reset to 0 */
    assert(iap2_link_peek_tx(&link, &frame, &frame_len) == IAP2_STATUS_OK);
    assert(link.tx_type == 5 /* TX_TYPE_ACK */);
    assert(link.tx_rx_ack == 4);
    assert(link.tx_unacked == 3);
    assert(link.unacked == 0);

    /* 2 more packets arrive while bare ACK is in flight */
    link.last_now_ms = 1000;
    for (unsigned i = 0; i < 2; i++) {
        build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK,
                          (uint8_t)(link.peer_seq + 1U), 1, 1,
                          &data, 1, &wire_len);
        assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);
    }
    assert(link.rx_accepted == 6);
    assert(link.unacked == 2);

    /* Finish bare ACK: unacked debt is 2 < max_cacks(3), pending ACK cleared */
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert(link.unacked == 2);
    assert((link.pending & IAP2_LINK_PENDING_ACK) == 0);

    /* Tick expires CACK timer (50ms) and raises pending ACK for the remaining 2 packets */
    assert(iap2_link_tick(&link, 1060) == IAP2_STATUS_OK);
    assert((link.pending & IAP2_LINK_PENDING_ACK) != 0);

    printf("PASS: test_newer_rx_while_data_ack_inflight\n");
}

static void test_zero_cack_never_bare_ack_or_debt(void)
{
    struct iap2_link link;
    establish_test_link(&link);
    link.cack_ms = 0;
    link.max_cacks = 0;
    link.rto_ms = 0; /* Isolate ACK scheduling from DATA retransmission. */

    const uint8_t data = 0x7E;
    uint8_t wire[64];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx;

    const uint8_t *frame = NULL;
    size_t frame_len = 0;
    assert(iap2_link_queue_payload(&link, 1, &data, 1) == IAP2_STATUS_OK);
    assert(iap2_link_peek_tx(&link, &frame, &frame_len) == IAP2_STATUS_OK);

    /* Receive 256 data packets when cack_ms == 0 */
    for (unsigned i = 0; i < 256; i++) {
        build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK,
                          (uint8_t)(link.peer_seq + 1U), 1, 1,
                          &data, 1, &wire_len);
        assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);
    }

    assert(link.unacked == 0);
    assert((link.pending & IAP2_LINK_PENDING_ACK) == 0);

    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert(link.unacked == 0);
    assert((link.pending & IAP2_LINK_PENDING_ACK) == 0);

    assert(iap2_link_tick(&link, 5000) == IAP2_STATUS_OK);
    assert((link.pending & IAP2_LINK_PENDING_ACK) == 0);

    frame = NULL;
    frame_len = 0;
    assert(iap2_link_peek_tx(&link, &frame, &frame_len) == IAP2_STATUS_OK);
    assert(frame == NULL && frame_len == 0);

    printf("PASS: test_zero_cack_never_bare_ack_or_debt\n");
}

static void test_syn_ack_mark_submitted_early_ack(void)
{
    struct iap2_link link;
    assert(iap2_link_init(&link, 8192) == IAP2_STATUS_OK);

    const uint8_t syn[] = {1, 4, 4, 0, 0, 100, 0, 50, 2, 3, 1, 0, 1};
    uint8_t wire[64];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx;
    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 0x20, 0, 0, syn, sizeof(syn), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);

    const uint8_t *frame = NULL;
    size_t frame_len = 0;
    assert(iap2_link_peek_tx(&link, &frame, &frame_len) == IAP2_STATUS_OK);
    assert(iap2_link_mark_submitted(&link) == IAP2_STATUS_OK);
    assert(link.tx_submitted);

    /* Host ACK arrives before local USB completion */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x21, 1, 0, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);
    assert(iap2_link_is_up(&link));
    assert(link.state == IAP2_LINK_STATE_UP);

    /* Asynchronous USB completion fires subsequently */
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert(iap2_link_is_up(&link));
    assert(!link.tx_in_flight);

    printf("PASS: test_syn_ack_mark_submitted_early_ack\n");
}

static void test_syn_ack_unmarked_early_ack_rejected(void)
{
    struct iap2_link link;
    assert(iap2_link_init(&link, 8192) == IAP2_STATUS_OK);

    const uint8_t syn[] = {1, 4, 4, 0, 0, 100, 0, 50, 2, 3, 1, 0, 1};
    uint8_t wire[64];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx;
    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 0x20, 0, 0, syn, sizeof(syn), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);

    /* Peek SYN+ACK without calling mark_submitted */
    const uint8_t *frame = NULL;
    size_t frame_len = 0;
    assert(iap2_link_peek_tx(&link, &frame, &frame_len) == IAP2_STATUS_OK);
    assert(!link.tx_submitted);

    /* Premature ACK for unsubmitted SYN+ACK must be rejected */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x21, 1, 0, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);
    assert(!iap2_link_is_up(&link));
    assert(link.state == IAP2_LINK_STATE_SYN_RCVD);

    /* Bridge marks frame submitted */
    assert(iap2_link_mark_submitted(&link) == IAP2_STATUS_OK);
    assert(link.tx_submitted);

    /* Valid host ACK now brings link up */
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);
    assert(iap2_link_is_up(&link));
    assert(link.state == IAP2_LINK_STATE_UP);

    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);

    printf("PASS: test_syn_ack_unmarked_early_ack_rejected\n");
}

static void test_data_marked_vs_unmarked_deferred_ack_bounds(void)
{
    struct iap2_link link;
    establish_test_link(&link);

    const uint8_t data = 0xAA;
    assert(iap2_link_queue_payload(&link, 1, &data, 1) == IAP2_STATUS_OK);

    const uint8_t *frame = NULL;
    size_t frame_len = 0;
    assert(iap2_link_peek_tx(&link, &frame, &frame_len) == IAP2_STATUS_OK);
    assert(link.slots[0].in_flight);
    assert(!link.tx_submitted);

    uint8_t wire[64];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx;

    /* ACK for in-flight sequence arriving before mark_submitted must be rejected */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x22, link.slots[0].seq, 0, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);
    assert(!link.has_deferred_ack);
    assert(link.count == 1);

    /* Mark submitted: ACK is now recognized and deferred */
    assert(iap2_link_mark_submitted(&link) == IAP2_STATUS_OK);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);
    assert(link.has_deferred_ack);
    assert(link.deferred_ack == link.slots[0].seq);
    assert(link.count == 1);

    /* Completing TX frees slot */
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert(!link.has_deferred_ack);
    assert(link.count == 0);

    printf("PASS: test_data_marked_vs_unmarked_deferred_ack_bounds\n");
}

static void test_syn_ack_ack_with_piggybacked_payload(void)
{
    struct iap2_link link;
    assert(iap2_link_init(&link, 8192) == IAP2_STATUS_OK);

    const uint8_t syn[] = {1, 4, 4, 0, 0, 100, 0, 50, 2, 3, 1, 0, 1};
    uint8_t wire[64];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx;
    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 0x20, 0, 0, syn, sizeof(syn), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);

    const uint8_t *frame = NULL;
    size_t frame_len = 0;
    assert(iap2_link_peek_tx(&link, &frame, &frame_len) == IAP2_STATUS_OK);
    assert(iap2_link_mark_submitted(&link) == IAP2_STATUS_OK);

    /* Peer ACK packet carries piggybacked data payload on control session 1 */
    const uint8_t piggyback[] = {'H', 'E', 'L', 'L', 'O'};
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x21, 1, 1,
                      piggyback, sizeof(piggyback), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);
    assert(iap2_link_is_up(&link));
    assert(rx.has_payload);
    assert(rx.session == 1);
    assert(rx.payload_len == sizeof(piggyback));
    assert(memcmp(rx.payload, piggyback, sizeof(piggyback)) == 0);

    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert(iap2_link_is_up(&link));

    printf("PASS: test_syn_ack_ack_with_piggybacked_payload\n");
}

static void test_failed_tx_syn_ack_and_data(void)
{
    /* 1. SYN+ACK submitted, peer ACK arrives (delivery proof), then local USB fails */
    struct iap2_link link;
    assert(iap2_link_init(&link, 8192) == IAP2_STATUS_OK);
    const uint8_t syn[] = {1, 4, 4, 0, 0, 100, 0, 50, 2, 3, 1, 0, 1};
    uint8_t wire[64];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx;
    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 0x20, 0, 0, syn, sizeof(syn), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);

    const uint8_t *frame = NULL;
    size_t frame_len = 0;
    assert(iap2_link_peek_tx(&link, &frame, &frame_len) == IAP2_STATUS_OK);
    assert(iap2_link_mark_submitted(&link) == IAP2_STATUS_OK);

    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x21, 1, 0, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);
    assert(iap2_link_is_up(&link));

    /* Local transfer completion reports failure, but peer ACK proved delivery */
    assert(iap2_link_finish_tx(&link, false) == IAP2_STATUS_OK);
    assert(iap2_link_is_up(&link));
    assert((link.pending & IAP2_LINK_PENDING_SYN_ACK) == 0);

    /* 2. SYN+ACK submitted, NO peer ACK arrives, local USB fails */
    iap2_link_disconnect(&link);
    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 0x30, 0, 0, syn, sizeof(syn), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);
    assert(iap2_link_peek_tx(&link, &frame, &frame_len) == IAP2_STATUS_OK);
    assert(iap2_link_mark_submitted(&link) == IAP2_STATUS_OK);
    assert(iap2_link_finish_tx(&link, false) == IAP2_STATUS_OK);
    assert(!iap2_link_is_up(&link));
    assert((link.pending & IAP2_LINK_PENDING_SYN_ACK) != 0);

    /* 3. Completion-first caller: finish_tx(true) without mark_submitted */
    frame = NULL;
    frame_len = 0;
    assert(iap2_link_peek_tx(&link, &frame, &frame_len) == IAP2_STATUS_OK);
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert(link.syn_ack_sent);
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x31, 1, 0, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);
    assert(iap2_link_is_up(&link));

    printf("PASS: test_failed_tx_syn_ack_and_data\n");
}

static void test_tick_already_acked_behind_inflight_head_at_maxtries(void)
{
    struct iap2_link link;
    establish_test_link(&link);
    link.window = 2;
    link.rto_ms = 200;
    link.max_sends = 2;

    uint8_t p1 = 0xAA, p2 = 0xBB;
    assert(iap2_link_queue_payload(&link, 1, &p1, 1) == IAP2_STATUS_OK);
    assert(iap2_link_queue_payload(&link, 1, &p2, 1) == IAP2_STATUS_OK);

    /* Initial transmission of both slots */
    link.last_now_ms = 1000;
    const uint8_t *frame;
    size_t len;
    assert(iap2_link_peek_tx(&link, &frame, &len) == IAP2_STATUS_OK);
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    link.last_now_ms = 1010;
    assert(iap2_link_peek_tx(&link, &frame, &len) == IAP2_STATUS_OK);
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);

    /* Model slot 1 at its final attempt; only the older head is due yet. */
    link.slots[1].tries = link.max_sends;
    assert(iap2_link_tick(&link, 1200) == IAP2_STATUS_OK);
    assert(link.slots[0].queued && !link.slots[1].queued);
    assert(iap2_link_peek_tx(&link, &frame, &len) == IAP2_STATUS_OK);
    assert(link.slots[0].in_flight);

    /* Cumulative ACK arrives acknowledging both slots (ack = slot[1].seq) */
    uint8_t wire[64];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx_res;
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x50, link.slots[1].seq, 0, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_OK);
    assert(link.last_ack == link.slots[1].seq);

    /* Slot 0 is in-flight so drop_acked stops; slot 1 remains in slots[] blocked behind head */
    assert(link.count == 2);

    /* Tick at expiry: must skip already-acknowledged slot 1 rather than resetting link */
    assert(iap2_link_tick(&link, 1500) == IAP2_STATUS_OK);
    assert(iap2_link_is_up(&link));
    assert(!link.timeout_exhausted);
    assert((link.pending & IAP2_LINK_PENDING_RST) == 0);

    /* Completing slot 0 drops both acknowledged slots */
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert(link.count == 0);

    printf("PASS: test_tick_already_acked_behind_inflight_head_at_maxtries\n");
}

static void test_queue_4_while_one_inflight(void)
{
    struct iap2_link link;
    establish_test_link(&link);
    link.window = 4;

    uint8_t p = 0x5A;
    assert(iap2_link_queue_payload(&link, 1, &p, 1) == IAP2_STATUS_OK);
    assert(iap2_link_queue_payload(&link, 1, &p, 1) == IAP2_STATUS_OK);
    assert(iap2_link_queue_payload(&link, 1, &p, 1) == IAP2_STATUS_OK);
    assert(link.count == 3);

    /* Peek slot 0 into flight (not stale) */
    const uint8_t *frame;
    size_t len;
    assert(iap2_link_peek_tx(&link, &frame, &len) == IAP2_STATUS_OK);
    assert(link.tx_in_flight);
    assert(!link.tx_stale);
    assert(link.tx_slot == &link.slots[0]);

    /* Queue 4th payload while slot 0 is in flight: capacity must be 4, not 3 */
    assert(iap2_link_queue_payload(&link, 1, &p, 1) == IAP2_STATUS_OK);
    assert(link.count == 4);

    /* 5th payload rejected */
    assert(iap2_link_queue_payload(&link, 1, &p, 1) == IAP2_STATUS_BUFFER_TOO_SMALL);

    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    printf("PASS: test_queue_4_while_one_inflight\n");
}

static void test_marked_future_ack_then_failed_tx_leaves_prior_slots_unchanged(void)
{
    struct iap2_link link;
    establish_test_link(&link);

    uint8_t p1 = 0x11, p2 = 0x22;
    assert(iap2_link_queue_payload(&link, 1, &p1, 1) == IAP2_STATUS_OK);
    assert(iap2_link_queue_payload(&link, 1, &p2, 1) == IAP2_STATUS_OK);

    /* Send slot 0 successfully */
    const uint8_t *frame;
    size_t len;
    assert(iap2_link_peek_tx(&link, &frame, &len) == IAP2_STATUS_OK);
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert(link.highest_sent_seq == link.slots[0].seq);
    assert(link.last_ack == 1);
    assert(link.count == 2);

    /* Peek slot 1 and mark submitted */
    assert(iap2_link_peek_tx(&link, &frame, &len) == IAP2_STATUS_OK);
    assert(iap2_link_mark_submitted(&link) == IAP2_STATUS_OK);
    assert(link.tx_submitted);

    /* Incoming packet arrives carrying future cumulative ACK covering slot 1 */
    uint8_t wire[64];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx_res;
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x60, link.slots[1].seq, 0, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_OK);

    /* ACK must be deferred without prematurely dropping slot 0 or mutating last_ack */
    assert(link.has_deferred_ack);
    assert(link.deferred_ack == link.slots[1].seq);
    assert(link.last_ack == 1);
    assert(link.count == 2);

    /* Slot 1 transmission fails locally on the wire */
    assert(iap2_link_finish_tx(&link, false) == IAP2_STATUS_OK);

    /* Deferred ACK is discarded; slot 0 and slot 1 remain unchanged in queue */
    assert(!link.has_deferred_ack);
    assert(link.last_ack == 1);
    assert(link.count == 2);
    assert(link.slots[0].tries == 1);
    assert(link.slots[1].queued);

    printf("PASS: test_marked_future_ack_then_failed_tx_leaves_prior_slots_unchanged\n");
}

static void test_duplicate_syn_during_inflight_syn_ack(void)
{
    struct iap2_link link;
    assert(iap2_link_init(&link, 8192) == IAP2_STATUS_OK);

    const uint8_t syn[] = {1, 4, 4, 0, 0, 100, 0, 50, 2, 3, 1, 0, 1};
    uint8_t wire[64];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx;
    build_test_packet(wire, sizeof(wire), IAP2_CTL_SYN, 0x10, 0, 0, syn, sizeof(syn), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);

    /* Peek SYN+ACK: tx_syn_req_seq is captured */
    const uint8_t *frame = NULL;
    size_t frame_len = 0;
    assert(iap2_link_peek_tx(&link, &frame, &frame_len) == IAP2_STATUS_OK);
    assert(link.tx_type == 2 /* TX_TYPE_SYN_ACK */);

    /* Duplicate SYN arrives while SYN+ACK #1 is in flight */
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);
    assert((link.pending & IAP2_LINK_PENDING_SYN_ACK) != 0);

    /* Finish SYN+ACK #1: duplicate SYN must not be swallowed; pending remains set */
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert(!iap2_link_is_up(&link));
    assert((link.pending & IAP2_LINK_PENDING_SYN_ACK) != 0);

    /* Next peek emits retry SYN+ACK */
    frame = NULL;
    frame_len = 0;
    assert(iap2_link_peek_tx(&link, &frame, &frame_len) == IAP2_STATUS_OK);
    assert(frame != NULL);
    assert(link.tx_type == 2 /* TX_TYPE_SYN_ACK */);
    assert(iap2_link_mark_submitted(&link) == IAP2_STATUS_OK);

    /* Peer ACK arrives: handshake brings link UP and suppresses further SYN+ACK */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x11, 1, 0, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);
    assert(iap2_link_is_up(&link));

    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert((link.pending & IAP2_LINK_PENDING_SYN_ACK) == 0);

    printf("PASS: test_duplicate_syn_during_inflight_syn_ack\n");
}

static void test_control_zero_and_bare_ack_session_validation(void)
{
    struct iap2_link link;
    establish_test_link(&link);

    uint8_t wire[64];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx_res;

    /* 1. Bare ACK with session != 0 must be rejected with MALFORMED */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x22, 1, 1, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_MALFORMED);
    assert(!rx_res.packet_handled);

    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x22, 1, 99, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_MALFORMED);
    assert(!rx_res.packet_handled);

    /* 2. Packet with control == 0 and empty payload rejected with MALFORMED */
    build_test_packet(wire, sizeof(wire), 0, 0x22, 1, 0, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_MALFORMED);
    assert(!rx_res.packet_handled);

    /* 3. Packet with control == 0 and payload > 0 rejected with MALFORMED */
    uint8_t data[] = {1, 2, 3};
    build_test_packet(wire, sizeof(wire), 0, 0x22, 1, 1, data, sizeof(data), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_MALFORMED);
    assert(!rx_res.packet_handled);

    /* 4. Valid bare ACK on session 0 accepted */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x22, 1, 0, NULL, 0, &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_OK);
    assert(rx_res.packet_handled);

    /* 5. Valid data packet with IAP2_CTL_ACK on session 1 accepted */
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, (uint8_t)(link.peer_seq + 1U), 1, 1,
                      data, sizeof(data), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx_res) == IAP2_STATUS_OK);
    assert(rx_res.has_payload);

    printf("PASS: test_control_zero_and_bare_ack_session_validation\n");
}

static void test_cack_borrow_timing_and_zero_timestamp(void)
{
    struct iap2_link link;
    establish_test_link(&link);
    link.cack_ms = 50;
    link.max_cacks = 0; /* timer only */

    uint8_t p = 0x77;
    assert(iap2_link_queue_payload(&link, 1, &p, 1) == IAP2_STATUS_OK);

    /* Borrow DATA at timestamp 0ms */
    link.last_now_ms = 0;
    const uint8_t *frame;
    size_t len;
    assert(iap2_link_peek_tx(&link, &frame, &len) == IAP2_STATUS_OK);
    assert(link.unacked == 0);

    /* In-order packet arrives at 10ms while DATA is borrowed */
    link.last_now_ms = 10;
    uint8_t wire[64];
    size_t wire_len = 0;
    struct iap2_link_rx_result rx;
    uint8_t rx_data[] = {0x01};
    build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, (uint8_t)(link.peer_seq + 1U), 1, 1,
                      rx_data, sizeof(rx_data), &wire_len);
    assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);
    assert(link.unacked == 1);
    assert(link.unacked_since_ms == 10);

    /* Tick at 55ms: 10 + 50 = 60ms > 55ms, no ACK requested */
    assert(iap2_link_tick(&link, 55) == IAP2_STATUS_OK);
    assert((link.pending & IAP2_LINK_PENDING_ACK) == 0);

    /* Tick at 60ms: CACK timer expires, ACK requested */
    assert(iap2_link_tick(&link, 60) == IAP2_STATUS_OK);
    assert((link.pending & IAP2_LINK_PENDING_ACK) != 0);

    /* DATA finish_tx clears borrowed state without losing post-borrow timer tick */
    assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
    assert((link.pending & IAP2_LINK_PENDING_ACK) != 0);
    assert(link.unacked == 1);

    printf("PASS: test_cack_borrow_timing_and_zero_timestamp\n");
}

static void test_retransmit_ack_bounds_and_deferred_cleanup(void)
{
    for (unsigned wrap = 0; wrap < 2; wrap++) {
        for (unsigned future = 0; future < 2; future++) {
            struct iap2_link link;
            establish_test_link(&link);
            if (wrap) {
                link.my_seq = 254;
                link.last_ack = 254;
                link.highest_sent_seq = 254;
            }
            assert(iap2_link_tick(&link, 1000) == IAP2_STATUS_OK);
            const uint8_t data = 0x5a;
            for (unsigned i = 0; i < 3; i++) {
                assert(iap2_link_queue_payload(&link, 1, &data, 1) == IAP2_STATUS_OK);
            }
            const uint8_t first = link.slots[0].seq;
            const uint8_t second = link.slots[1].seq;
            const uint8_t third = link.slots[2].seq;
            const uint8_t *frame;
            size_t len;
            for (unsigned i = 0; i < 2; i++) {
                assert(iap2_link_peek_tx(&link, &frame, &len) == IAP2_STATUS_OK);
                assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
            }
            assert(iap2_link_tick(&link, 1201) == IAP2_STATUS_OK);
            assert(iap2_link_peek_tx(&link, &frame, &len) == IAP2_STATUS_OK);
            assert(link.tx_slot->seq == first && link.tx_slot->tries == 1);
            assert(iap2_link_mark_submitted(&link) == IAP2_STATUS_OK);

            uint8_t wire[64];
            size_t wire_len;
            struct iap2_link_rx_result rx;
            build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x30,
                              second, 0, NULL, 0, &wire_len);
            assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);
            assert(link.last_ack == second && link.count == 3);
            build_test_packet(wire, sizeof(wire), IAP2_CTL_ACK, 0x30,
                              future ? third : first, 0, NULL, 0, &wire_len);
            assert(iap2_link_receive_packet(&link, wire, wire_len, &rx) == IAP2_STATUS_OK);
            assert(!link.has_deferred_ack && link.last_ack == second);

            assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
            assert(link.count == 1 && !link.has_deferred_ack);
            assert(iap2_link_peek_tx(&link, &frame, &len) == IAP2_STATUS_OK);
            struct iap2_packet_view view;
            assert(iap2_decode_packet(frame, len, &view) == IAP2_STATUS_OK);
            assert(view.header.seq == third);
            assert(iap2_link_finish_tx(&link, true) == IAP2_STATUS_OK);
            assert(link.count == 1 && link.last_ack == second);
        }
    }
    printf("PASS: test_retransmit_ack_bounds_and_deferred_cleanup\n");
}

int main(void)
{
    test_probe_lifecycle();
    test_syn_parameter_validation();
    test_handshake_and_syn_ack();
    test_data_queueing_and_tx();
    test_sequence_cumulative_future_and_wrap();
    test_peer_seq_duplicates_and_rx_payload();
    test_retransmission_and_timeout_exhaustion();
    test_no_retransmission_zero_rto();
    test_time_wrap_safe();
    test_mutation_fuzz_and_null_pointers();

    test_tx_immutability_and_stale_fence();
    test_syn_ack_premature_ack_rejected();
    test_ack_bounds_and_finish_failure_discard();
    test_rx_arrival_during_inflight_preserves_ack();
    test_max_sends_256_attempts();
    test_bounded_input_and_malformed_syn_rejection();
    test_cack_zero_no_bare_ack_and_piggyback();
    test_eak_validation();
    test_malformed_eak_rejection();
    test_multi_slot_rto_scan();

    /* CACK, Handshake, and Wrap regressions */
    test_unacked_256_rx_during_tx_borrow_reproducer();
    test_rx_counter_wrap_and_cack_debt();
    test_newer_rx_while_data_ack_inflight();
    test_zero_cack_never_bare_ack_or_debt();
    test_syn_ack_mark_submitted_early_ack();
    test_syn_ack_unmarked_early_ack_rejected();
    test_data_marked_vs_unmarked_deferred_ack_bounds();
    test_syn_ack_ack_with_piggybacked_payload();
    test_failed_tx_syn_ack_and_data();

    /* Review finding regressions */
    test_tick_already_acked_behind_inflight_head_at_maxtries();
    test_queue_4_while_one_inflight();
    test_marked_future_ack_then_failed_tx_leaves_prior_slots_unchanged();
    test_duplicate_syn_during_inflight_syn_ack();
    test_control_zero_and_bare_ack_session_validation();
    test_cack_borrow_timing_and_zero_timestamp();

    test_retransmit_ack_bounds_and_deferred_cleanup();

    printf("\nALL IAP2 LINK TESTS PASSED (36 SUITES).\n");
    return 0;
}
