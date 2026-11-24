// SPDX-License-Identifier: GPL-3.0-only
#ifndef IAP2_TX_H
#define IAP2_TX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "link.h"
#include "rx_tap.h"

/*
 * Transmit bridge state for chunked iAP2 HID reports.
 *
 * Buffer and memory ownership:
 * - A statically zeroed struct iap2_tx represents an idle, initialized state.
 * - Outbound frame buffers are borrowed from struct iap2_link via iap2_link_peek_tx().
 *   The borrowed frame remains immutable and owned by the link layer across report
 *   preparation, submission, and DMA transfer until released via iap2_link_finish_tx().
 * - Output report buffer 'out' is owned and provided by the caller; no heap allocation
 *   or native memory referencing is performed.
 * - Access to a given tx and link instance must be serialized by the caller.
 */
struct iap2_tx {
    size_t offset;
    size_t chunk_len;
    size_t report_len;
};

/*
 * Prepare the next HID report from the current or next queued link frame.
 *
 * Scans up to 16 rows of the report table (ending at body_size == 0) to select
 * the smallest body fitting remaining bytes plus flag, or the largest body to split.
 * Formats report ID, link control flags, and payload into 'out'.
 */
iap2_status_t iap2_tx_prepare(struct iap2_tx *tx,
                              struct iap2_link *link,
                              const struct iap2_hid_report_row *table,
                              size_t row_count,
                              uint8_t *out,
                              size_t out_capacity,
                              size_t *out_len);

/*
 * Confirm submission of the current prepared report to the native transport.
 * If this report completes the link frame, marks it submitted in the link layer.
 */
iap2_status_t iap2_tx_submitted(struct iap2_tx *tx, struct iap2_link *link);

/*
 * Complete transfer of the current report following DMA termination.
 * On success with zero residual, advances cursor and finishes final frame with true.
 * On failure, residual, or stale link session, finishes with false and resets tx.
 */
iap2_status_t iap2_tx_complete(struct iap2_tx *tx,
                               struct iap2_link *link,
                               bool success,
                               size_t residual);

/*
 * Abort active transmission and release link frame borrow.
 * Callable only after caller proves shared DMA is idle.
 */
void iap2_tx_abort(struct iap2_tx *tx, struct iap2_link *link);

struct iap2_tx_pump {
    struct iap2_tx tx;
    uint32_t last_us;
    uint32_t elapsed_ms;
    uint16_t remaining_us;
    bool clock_initialized;
};

/* USBDeviceTask owns these callbacks and the report buffer. A rejected submit
 * leaves DMA idle. A terminal completion (anything except -3) ends its use of
 * the report. ready gates submission only, including during shutdown. */
struct iap2_tx_io {
    void *context;
    uint8_t *buffer;
    size_t capacity;
    const struct iap2_hid_report_row *in_table;
    size_t in_count;
    uint32_t (*clock_us)(void *context);
    bool (*ready)(void *context);
    int (*submit)(void *context, size_t length);
    int (*completion)(void *context, size_t *residual);
};

/* Use a zeroed pump per connection. Progress at most one completion and one
 * submission per call. Abort or reset the pump only after DMA is idle. */
iap2_status_t iap2_tx_pump(struct iap2_tx_pump *pump, struct iap2_link *link,
                          const struct iap2_tx_io *io);

#endif /* IAP2_TX_H */
