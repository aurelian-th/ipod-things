// SPDX-License-Identifier: GPL-3.0-only
#ifndef IAP2_NATIVE_TX_H
#define IAP2_NATIVE_TX_H

#include "osos-2.0.5.h"
#include "rv_api.h"
#include "tx.h"

struct iap2_native_tx {
    struct iap2_tx_pump pump;
    const struct rv_api *api;
    rv_extension_handle self;
    struct iap2_link *link;
    void *usb;
    void *controller;
    uint8_t *slot;
    uint8_t *buffer;
    size_t capacity;
    const struct iap2_hid_report_row *in_table;
    size_t in_count;
    uint32_t saved_in_mask;
    bool held;
    bool draining;
    _Alignas(4) uint8_t allocation[USBAlignedBuffer_bytes];
};

/* USBDeviceTask owns the state. The caller excludes the native HID sender before
 * opening and rejects a new EP3 configuration while a previous lease drains. */
bool iap2_native_tx_open(struct iap2_native_tx *tx, const struct rv_api *api,
                         rv_extension_handle self, struct iap2_link *link, void *hid);
void iap2_native_tx_poll(struct iap2_native_tx *tx, void *controller);
void iap2_native_tx_before_release(struct iap2_native_tx *tx, void *usb, uint32_t ep);
void iap2_native_tx_after_release(struct iap2_native_tx *tx, void *usb, uint32_t ep);
void iap2_native_tx_after_reset(struct iap2_native_tx *tx, void *controller);

#endif /* IAP2_NATIVE_TX_H */

