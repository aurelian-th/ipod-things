/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tx.h"

static const struct iap2_hid_report_row table[] = {
    {2, 0, 5},
    {4, 0, 13},
    {3, 0, 9},
    {0, 0, 0},
};

static void test_probe(void)
{
    struct iap2_link link;
    struct iap2_tx tx = {0};
    uint8_t report[14];
    size_t size = sizeof(report);
    assert(iap2_link_init(&link, IAP2_LINK_MAX_RX_PACKET) == IAP2_STATUS_OK);
    assert(iap2_tx_prepare(&tx, &link, table, 4, report, sizeof(report), &size) ==
           IAP2_STATUS_OK);
    assert(size == 0);

    assert(iap2_link_handle_probe(&link) == IAP2_STATUS_OK);
    memset(report, 0xa5, sizeof(report));
    assert(iap2_tx_prepare(&tx, &link, table, 4, report, sizeof(report), &size) ==
           IAP2_STATUS_OK);
    const uint8_t expected[] = {3, 0, 0xff, 0x55, 0x02, 0x00, 0xee, 0x10, 0, 0};
    assert(size == sizeof(expected));
    assert(memcmp(report, expected, size) == 0);
    assert(report[size] == 0xa5);
    assert(iap2_tx_prepare(&tx, &link, table, 4, report, sizeof(report), &size) ==
           IAP2_STATUS_IN_PROGRESS);
    assert(memcmp(report, expected, sizeof(expected)) == 0);
    assert(!link.tx_submitted);
    assert(iap2_tx_submitted(&tx, &link) == IAP2_STATUS_OK);
    assert(link.tx_submitted);
    assert(iap2_tx_complete(&tx, &link, true, 0) == IAP2_STATUS_OK);
    assert(!link.tx_in_flight && link.pending == 0);
}

static void test_fragmentation(bool reset_between_reports)
{
    struct iap2_link link;
    struct iap2_tx tx = {0};
    uint8_t report[14];
    uint8_t packet[32];
    size_t size;
    assert(iap2_link_init(&link, IAP2_LINK_MAX_RX_PACKET) == IAP2_STATUS_OK);
    link.pending = IAP2_LINK_PENDING_SYN_ACK;
    link.syn_ack_len = 12;
    memset(link.syn_ack_payload, 0x81, link.syn_ack_len);

    assert(iap2_tx_prepare(&tx, &link, table, 4, report, sizeof(report), &size) ==
           IAP2_STATUS_OK);
    assert(size == 14 && report[0] == 4);
    assert(report[1] == IAP2_LINK_CONTROL_MORE_TO_FOLLOW);
    memcpy(packet, link.tx_frame, link.tx_len);
    assert(memcmp(report + 2, packet, 12) == 0);
    assert(iap2_tx_submitted(&tx, &link) == IAP2_STATUS_OK);
    assert(!link.tx_submitted);
    assert(iap2_tx_complete(&tx, &link, true, 0) == IAP2_STATUS_OK);
    assert(link.tx_in_flight);
    assert(!link.syn_ack_sent);

    if (reset_between_reports) {
        assert(iap2_link_handle_probe(&link) == IAP2_STATUS_OK);
        assert(link.tx_stale);
    }

    assert(iap2_tx_prepare(&tx, &link, table, 4, report, sizeof(report), &size) ==
           IAP2_STATUS_OK);
    if (reset_between_reports) {
        assert(size == 10 && report[0] == 3 && report[1] == 0);
        assert(memcmp(report + 2, iap2_probe_wire, IAP2_PROBE_LEN) == 0);
    } else {
        assert(size == 14 && report[0] == 4);
        assert(report[1] == IAP2_LINK_CONTROL_CONTINUE);
        assert(memcmp(report + 2, packet + 12, 10) == 0);
        assert(report[12] == 0 && report[13] == 0);
    }
    assert(iap2_tx_submitted(&tx, &link) == IAP2_STATUS_OK);
    assert(link.tx_submitted);
    assert(iap2_tx_complete(&tx, &link, true, 0) == IAP2_STATUS_OK);
    assert(!link.tx_in_flight && link.pending == 0);
}

static void test_short_and_cancelled_reports(void)
{
    struct iap2_link link;
    struct iap2_tx tx = {0};
    uint8_t report[14];
    size_t size;
    assert(iap2_link_init(&link, IAP2_LINK_MAX_RX_PACKET) == IAP2_STATUS_OK);
    assert(iap2_link_handle_probe(&link) == IAP2_STATUS_OK);
    assert(iap2_tx_prepare(&tx, &link, table, 4, report, 6, &size) != IAP2_STATUS_OK);
    assert(size == 0 && !link.tx_in_flight && link.pending != 0);

    assert(iap2_tx_prepare(&tx, &link, table, 4, report, sizeof(report), &size) ==
           IAP2_STATUS_OK);
    iap2_tx_abort(&tx, &link);
    assert(!link.tx_in_flight && link.pending != 0);

    assert(iap2_tx_prepare(&tx, &link, table, 4, report, sizeof(report), &size) ==
           IAP2_STATUS_OK);
    assert(iap2_tx_submitted(&tx, &link) == IAP2_STATUS_OK);
    assert(iap2_tx_complete(&tx, &link, true, 1) == IAP2_STATUS_OK);
    assert(!link.tx_in_flight && link.pending != 0);

    assert(iap2_tx_prepare(&tx, &link, table, 4, report, sizeof(report), &size) ==
           IAP2_STATUS_OK);
    assert(iap2_tx_submitted(&tx, &link) == IAP2_STATUS_OK);
    assert(iap2_link_handle_probe(&link) == IAP2_STATUS_OK);
    assert(iap2_tx_complete(&tx, &link, true, 0) == IAP2_STATUS_OK);
    assert(!link.tx_in_flight && link.pending == IAP2_LINK_PENDING_ECHO);
}

struct controller_fixture {
    uint32_t now;
    bool ready;
    int submit_result;
    int completion_result;
    unsigned submissions;
    unsigned completions;
};

static uint32_t test_clock(void *context)
{
    return ((struct controller_fixture *)context)->now;
}

static bool test_ready(void *context)
{
    return ((struct controller_fixture *)context)->ready;
}

static int test_submit(void *context, size_t length)
{
    struct controller_fixture *controller = context;
    assert(length == 10);
    controller->submissions++;
    return controller->submit_result;
}

static int test_completion(void *context, size_t *residual)
{
    struct controller_fixture *controller = context;
    controller->completions++;
    if (controller->completion_result != -3)
        *residual = 0;
    return controller->completion_result;
}

static void test_async_pump(void)
{
    struct iap2_link link;
    struct iap2_tx_pump pump = {0};
    uint8_t report[14];
    uint8_t original_report[14];
    struct controller_fixture controller = {
        .now = UINT32_MAX - 100,
        .ready = true,
        .submit_result = -1,
        .completion_result = -3,
    };
    const struct iap2_tx_io io = {
        .context = &controller,
        .buffer = report,
        .capacity = sizeof(report),
        .in_table = table,
        .in_count = sizeof(table) / sizeof(table[0]),
        .clock_us = test_clock,
        .ready = test_ready,
        .submit = test_submit,
        .completion = test_completion,
    };
    assert(iap2_link_init(&link, IAP2_LINK_MAX_RX_PACKET) == IAP2_STATUS_OK);
    assert(iap2_link_handle_probe(&link) == IAP2_STATUS_OK);
    memset(report, 0x7a, sizeof(report));

    assert(iap2_tx_pump(&pump, &link, &io) == IAP2_STATUS_IN_PROGRESS);
    assert(controller.submissions == 1 && !link.tx_in_flight);
    controller.submit_result = 0;
    controller.now = 1480;
    assert(iap2_tx_pump(&pump, &link, &io) == IAP2_STATUS_OK);
    assert(controller.submissions == 2 && link.tx_submitted);
    assert(pump.elapsed_ms == 1 && pump.remaining_us == 581);
    memcpy(original_report, report, sizeof(report));

    controller.ready = false;
    controller.now = 1900;
    assert(iap2_link_handle_probe(&link) == IAP2_STATUS_OK);
    assert(iap2_tx_pump(&pump, &link, &io) == IAP2_STATUS_IN_PROGRESS);
    assert(controller.submissions == 2 && controller.completions == 1);
    assert(memcmp(report, original_report, sizeof(report)) == 0);
    assert(pump.elapsed_ms == 2 && pump.remaining_us == 1);
    assert(link.tx_stale);

    controller.completion_result = 0;
    assert(iap2_tx_pump(&pump, &link, &io) == IAP2_STATUS_IN_PROGRESS);
    assert(controller.submissions == 2 && controller.completions == 2);
    assert(!link.tx_in_flight && link.pending == IAP2_LINK_PENDING_ECHO);

    controller.ready = true;
    assert(iap2_tx_pump(&pump, &link, &io) == IAP2_STATUS_OK);
    assert(controller.submissions == 3 && controller.completions == 2);
    assert(iap2_tx_pump(&pump, &link, &io) == IAP2_STATUS_OK);
    assert(controller.submissions == 3 && controller.completions == 3);
    assert(!link.tx_in_flight && link.pending == 0);
}

int main(void)
{
    test_probe();
    test_fragmentation(false);
    test_fragmentation(true);
    test_short_and_cancelled_reports();
    test_async_pump();
    puts("iAP2 HID transmit tests passed");
}
