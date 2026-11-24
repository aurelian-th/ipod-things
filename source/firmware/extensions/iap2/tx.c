// SPDX-License-Identifier: GPL-3.0-only
#include "tx.h"

#ifdef __arm__
#include "rv_libc.h"
#else
#include <string.h>
#endif

iap2_status_t iap2_tx_prepare(struct iap2_tx *tx,
                              struct iap2_link *link,
                              const struct iap2_hid_report_row *table,
                              size_t row_count,
                              uint8_t *out,
                              size_t out_capacity,
                              size_t *out_len)
{
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (tx == NULL || link == NULL || table == NULL || out == NULL || out_len == NULL) {
        return IAP2_STATUS_INVALID_ARG;
    }
    if (tx->report_len != 0) {
        return IAP2_STATUS_IN_PROGRESS;
    }
    if (row_count == 0 || out_capacity < 2) {
        return IAP2_STATUS_INVALID_ARG;
    }

    /*
     * If link was reset between chunks, abandon the stale borrow and select
     * from the new session before reading any frame.
     */
    if (link->tx_stale) {
        iap2_tx_abort(tx, link);
    }

    const uint8_t *frame = NULL;
    size_t frame_len = 0;
    iap2_status_t status = iap2_link_peek_tx(link, &frame, &frame_len);
    if (status != IAP2_STATUS_OK) {
        return status;
    }
    if (frame == NULL) {
        return IAP2_STATUS_OK;
    }

    if (tx->offset >= frame_len) {
        iap2_tx_abort(tx, link);
        return IAP2_STATUS_MALFORMED;
    }

    size_t rem = frame_len - tx->offset;
    size_t limit = row_count;
    if (limit > IAP2_HID_MAX_REPORT_ROWS) {
        limit = IAP2_HID_MAX_REPORT_ROWS;
    }

    const struct iap2_hid_report_row *best_fit = NULL;
    const struct iap2_hid_report_row *best_split = NULL;
    size_t needed = rem + 1;

    for (size_t i = 0; i < limit; i++) {
        const struct iap2_hid_report_row *row = &table[i];
        if (row->body_size == 0) {
            break;
        }
        if (row->report_id < 1 || row->report_id > 255) {
            continue;
        }
        if (row->body_size < 2 || row->body_size > (out_capacity - 1)) {
            continue;
        }

        if (row->body_size >= needed) {
            if (best_fit == NULL || row->body_size < best_fit->body_size) {
                best_fit = row;
            }
        } else {
            if (best_split == NULL || row->body_size > best_split->body_size) {
                best_split = row;
            }
        }
    }

    const struct iap2_hid_report_row *selected = best_fit;
    if (selected == NULL && frame_len != IAP2_PROBE_LEN)
        selected = best_split;
    if (selected == NULL) {
        iap2_tx_abort(tx, link);
        return IAP2_STATUS_BUFFER_TOO_SMALL;
    }

    size_t max_payload = (size_t)selected->body_size - 1;
    size_t chunk = (rem <= max_payload) ? rem : max_payload;

    uint8_t flags = 0;
    if (tx->offset > 0) {
        flags |= IAP2_LINK_CONTROL_CONTINUE;
    }
    if (tx->offset + chunk < frame_len) {
        flags |= IAP2_LINK_CONTROL_MORE_TO_FOLLOW;
    }

    out[0] = (uint8_t)selected->report_id;
    out[1] = flags;
    memcpy(&out[2], &frame[tx->offset], chunk);

    memset(out + 2 + chunk, 0, max_payload - chunk);
    size_t total_len = (size_t)selected->body_size + 1;
    tx->chunk_len = chunk;
    tx->report_len = total_len;
    *out_len = total_len;

    return IAP2_STATUS_OK;
}

iap2_status_t iap2_tx_submitted(struct iap2_tx *tx, struct iap2_link *link)
{
    if (tx == NULL || link == NULL || tx->report_len == 0 || !link->tx_in_flight) {
        return IAP2_STATUS_INVALID_ARG;
    }
    if (tx->offset + tx->chunk_len == link->tx_len) {
        return iap2_link_mark_submitted(link);
    }

    return IAP2_STATUS_OK;
}

iap2_status_t iap2_tx_complete(struct iap2_tx *tx,
                               struct iap2_link *link,
                               bool success,
                               size_t residual)
{
    if (tx == NULL || link == NULL || tx->report_len == 0 || !link->tx_in_flight) {
        return IAP2_STATUS_INVALID_ARG;
    }

    if (!success || residual != 0 || link->tx_stale) {
        iap2_tx_abort(tx, link);
        return IAP2_STATUS_OK;
    }

    tx->offset += tx->chunk_len;
    tx->chunk_len = 0;
    tx->report_len = 0;

    if (tx->offset == link->tx_len) {
        tx->offset = 0;
        return iap2_link_finish_tx(link, true);
    }

    return IAP2_STATUS_OK;
}

void iap2_tx_abort(struct iap2_tx *tx, struct iap2_link *link)
{
    if (tx == NULL || link == NULL)
        return;
    (void)iap2_link_finish_tx(link, false);
    *tx = (struct iap2_tx){0};
}

iap2_status_t iap2_tx_pump(struct iap2_tx_pump *pump, struct iap2_link *link,
                          const struct iap2_tx_io *io)
{
    if (pump == NULL || link == NULL || io == NULL || io->clock_us == NULL ||
        io->ready == NULL || io->submit == NULL || io->completion == NULL)
        return IAP2_STATUS_INVALID_ARG;

    uint32_t now = io->clock_us(io->context);
    if (pump->clock_initialized) {
        uint32_t delta = now - pump->last_us;
        uint32_t remainder = delta % 1000U + pump->remaining_us;
        pump->elapsed_ms += delta / 1000U + remainder / 1000U;
        pump->remaining_us = (uint16_t)(remainder % 1000U);
    }
    pump->clock_initialized = true;
    pump->last_us = now;
    iap2_status_t status = iap2_link_tick(link, pump->elapsed_ms);

    if (pump->tx.report_len != 0) {
        size_t residual = 0;
        int result = io->completion(io->context, &residual);
        if (result == -3)
            return IAP2_STATUS_IN_PROGRESS;
        iap2_status_t completed = iap2_tx_complete(&pump->tx, link, result == 0, residual);
        if (completed != IAP2_STATUS_OK) {
            iap2_tx_abort(&pump->tx, link);
            return completed;
        }
    }
    if (status != IAP2_STATUS_OK)
        return status;
    if (!io->ready(io->context))
        return link->pending || link->count || link->tx_in_flight
                   ? IAP2_STATUS_IN_PROGRESS : IAP2_STATUS_OK;

    size_t length = 0;
    status = iap2_tx_prepare(&pump->tx, link, io->in_table, io->in_count,
                             io->buffer, io->capacity, &length);
    if (status != IAP2_STATUS_OK || length == 0)
        return status;

    if (io->submit(io->context, length) != 0) {
        iap2_tx_abort(&pump->tx, link);
        return IAP2_STATUS_IN_PROGRESS;
    }
    return iap2_tx_submitted(&pump->tx, link);
}
