// SPDX-License-Identifier: GPL-3.0-only
#include "rx_tap.h"

#ifdef __arm__
#include "rv_libc.h"
#else
#include <string.h>
#endif

void iap2_link_summary_clear_session(struct iap2_link_summary *summary)
{
    if (summary == NULL) {
        return;
    }

    summary->last_session = 0;
    summary->last_payload_len = 0;
}

void iap2_rx_tap_sync_summary(const struct iap2_link *link,
                              struct iap2_link_summary *summary)
{
    if (link == NULL || summary == NULL) {
        return;
    }

    summary->link_state = link->state;
    summary->pending_tx_mask = link->pending;
    summary->queued_slots = (uint32_t)link->count;
    summary->link_rx_accepted = link->rx_accepted;
    summary->my_seq = link->my_seq;
    summary->peer_seq = link->peer_seq;
    summary->last_ack = link->last_ack;
}

iap2_status_t iap2_rx_tap_init(struct iap2_rx_tap *tap)
{
    iap2_status_t st;

    if (tap == NULL) {
        return IAP2_STATUS_INVALID_ARG;
    }

    memset(tap, 0, sizeof(*tap));
    iap2_reassembler_init(&tap->reasm, tap->reasm_buf, sizeof(tap->reasm_buf));

    st = iap2_link_init(&tap->link, sizeof(tap->reasm_buf));
    if (st != IAP2_STATUS_OK) {
        return st;
    }

    iap2_link_summary_clear_session(&tap->summary);
    iap2_rx_tap_sync_summary(&tap->link, &tap->summary);
    return IAP2_STATUS_OK;
}

void iap2_rx_tap_reset(struct iap2_rx_tap *tap)
{
    if (tap == NULL) {
        return;
    }

    iap2_reassembler_reset(&tap->reasm);
    iap2_link_reset(&tap->link);
    iap2_link_summary_clear_session(&tap->summary);
    iap2_rx_tap_sync_summary(&tap->link, &tap->summary);
    if (tap->on_reset != NULL) {
        tap->on_reset(tap->callback_context);
    }
}

void iap2_rx_tap_disconnect(struct iap2_rx_tap *tap)
{
    if (tap == NULL) {
        return;
    }

    iap2_reassembler_reset(&tap->reasm);
    iap2_link_disconnect(&tap->link);
    iap2_link_summary_clear_session(&tap->summary);
    iap2_rx_tap_sync_summary(&tap->link, &tap->summary);
    if (tap->on_reset != NULL) {
        tap->on_reset(tap->callback_context);
    }
}

void iap2_rx_tap_snapshot(const struct iap2_rx_tap *tap,
                          struct iap2_rx_stats *out_stats,
                          struct iap2_link_summary *out_summary)
{
    if (tap == NULL) {
        return;
    }

    if (out_stats != NULL) {
        memcpy(out_stats, &tap->stats, sizeof(*out_stats));
    }
    if (out_summary != NULL) {
        memcpy(out_summary, &tap->summary, sizeof(*out_summary));
    }
}

bool iap2_hid_lookup_report_len(const struct iap2_hid_report_row *table,
                                size_t max_rows,
                                size_t max_report_len,
                                uint16_t report_id,
                                size_t *out_expected_len)
{
    size_t row_limit;
    size_t i;

    if (table == NULL || out_expected_len == NULL || max_rows == 0 ||
        max_report_len < 2U || report_id == 0U || report_id > 255U) {
        return false;
    }

    row_limit = (max_rows > IAP2_HID_MAX_REPORT_ROWS)
                    ? IAP2_HID_MAX_REPORT_ROWS
                    : max_rows;

    for (i = 0; i < row_limit; i++) {
        const struct iap2_hid_report_row *row = &table[i];

        if (row->body_size == 0U) {
            break;
        }
        if (row->report_id == 0U || row->report_id > 255U) {
            continue;
        }
        if (row->report_id == report_id) {
            if ((size_t)row->body_size > max_report_len - 1U) {
                return false;
            }
            *out_expected_len = 1U + (size_t)row->body_size;
            return true;
        }
    }

    return false;
}

static iap2_tap_action_t handle_assembled_packet(
    struct iap2_rx_tap *tap,
    const struct iap2_assembled_packet *pkt)
{
    if (pkt->type == IAP2_PACKET_TYPE_PROBE) {
        iap2_status_t st = iap2_link_handle_probe(&tap->link);
        if (st != IAP2_STATUS_OK) {
            tap->stats.rx_malformed++;
            iap2_rx_tap_sync_summary(&tap->link, &tap->summary);
            return IAP2_TAP_DROP_ERROR;
        }

        tap->stats.rx_iap2_probes++;
        if (tap->link.pending != 0U) {
            tap->stats.tx_queued_events++;
        }
        iap2_rx_tap_sync_summary(&tap->link, &tap->summary);
        return IAP2_TAP_CLAIMED_ACK;
    }

    if (pkt->type == IAP2_PACKET_TYPE_LINK) {
        struct iap2_link_rx_result rx_res;
        iap2_status_t st;

        tap->stats.rx_iap2_packets++;
        memset(&rx_res, 0, sizeof(rx_res));

        st = iap2_link_receive_packet(&tap->link, pkt->data, pkt->len, &rx_res);
        if (st == IAP2_STATUS_MALFORMED_CHECKSUM) {
            tap->stats.rx_bad_checksum++;
            iap2_rx_tap_sync_summary(&tap->link, &tap->summary);
            return IAP2_TAP_DROP_ERROR;
        }
        if (st != IAP2_STATUS_OK) {
            tap->stats.rx_malformed++;
            iap2_rx_tap_sync_summary(&tap->link, &tap->summary);
            return IAP2_TAP_DROP_ERROR;
        }

        if (rx_res.packet_handled) {
            tap->stats.rx_link_handled++;
        }
        if (rx_res.has_payload) {
            tap->stats.rx_link_payloads++;
            tap->summary.last_session = rx_res.session;
            tap->summary.last_payload_len = (uint16_t)rx_res.payload_len;
            if (tap->on_payload != NULL) {
                tap->on_payload(tap->callback_context, rx_res.session,
                                rx_res.payload, rx_res.payload_len,
                                tap->now_ms);
            }
        } else if (tap->link.state == IAP2_LINK_STATE_DOWN) {
            iap2_link_summary_clear_session(&tap->summary);
            if (tap->on_reset != NULL) {
                tap->on_reset(tap->callback_context);
            }
        }
        if (tap->link.pending != 0U) {
            tap->stats.tx_queued_events++;
        }

        iap2_rx_tap_sync_summary(&tap->link, &tap->summary);
        return IAP2_TAP_CLAIMED_ACK;
    }

    tap->stats.rx_malformed++;
    iap2_rx_tap_sync_summary(&tap->link, &tap->summary);
    return IAP2_TAP_DROP_ERROR;
}

iap2_tap_action_t iap2_rx_tap_process_report(
    struct iap2_rx_tap *tap,
    const struct iap2_hid_report_row *out_table,
    size_t max_rows,
    size_t max_report_len,
    const uint8_t *report,
    size_t report_len)
{
    size_t expected_report_len = 0;
    struct iap2_assembled_packet pkt;
    iap2_status_t st;

    if (tap == NULL) {
        return IAP2_TAP_DROP_ERROR;
    }

    tap->stats.rx_total_reports++;

    if (report == NULL || report_len == 0U || max_report_len < 2U ||
        report_len > max_report_len) {
        tap->stats.rx_bad_report++;
        return IAP2_TAP_PASSTHROUGH;
    }

    if (!iap2_hid_lookup_report_len(out_table, max_rows, max_report_len,
                                    (uint16_t)report[0], &expected_report_len)) {
        tap->stats.rx_bad_report++;
        return IAP2_TAP_PASSTHROUGH;
    }

    if (report_len != expected_report_len) {
        tap->stats.rx_bad_report++;
        return IAP2_TAP_PASSTHROUGH;
    }

    /*
     * Apple HID transports fragmented iAP1 reports with link_control in 0..3.
     * When no iAP2 assembly is in progress, a valid CONTINUE report belongs
     * to iAP1 and must pass through rather than being rejected as malformed.
     */
    if (!tap->reasm.in_progress && report_len >= 2U &&
        (report[1] & IAP2_LINK_CONTROL_CONTINUE) != 0 &&
        (report[1] & IAP2_LINK_CONTROL_RESERVED_MASK) == 0) {
        tap->stats.rx_iap1_passthrough++;
        return IAP2_TAP_PASSTHROUGH;
    }

    memset(&pkt, 0, sizeof(pkt));
    st = iap2_reassembler_process_report(&tap->reasm,
                                         report,
                                         report_len,
                                         report[0],
                                         expected_report_len,
                                         &pkt);

    switch (st) {
    case IAP2_STATUS_UNCLAIMED:
        tap->stats.rx_iap1_passthrough++;
        return IAP2_TAP_PASSTHROUGH;

    case IAP2_STATUS_IN_PROGRESS:
        return IAP2_TAP_CLAIMED_ACK;

    case IAP2_STATUS_OK:
        return handle_assembled_packet(tap, &pkt);

    case IAP2_STATUS_MALFORMED_CHECKSUM:
        iap2_reassembler_reset(&tap->reasm);
        tap->stats.rx_bad_checksum++;
        return IAP2_TAP_DROP_ERROR;

    case IAP2_STATUS_MALFORMED:
    case IAP2_STATUS_BUFFER_TOO_SMALL:
    case IAP2_STATUS_INVALID_ARG:
    default:
        iap2_reassembler_reset(&tap->reasm);
        tap->stats.rx_malformed++;
        return IAP2_TAP_DROP_ERROR;
    }
}
