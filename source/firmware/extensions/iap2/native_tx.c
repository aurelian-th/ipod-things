// SPDX-License-Identifier: GPL-3.0-only
#include "native_tx.h"

#include "rv_libc.h"
#include <stddef.h>
#include <stdint.h>

static void native_tx_clear(struct iap2_native_tx *tx)
{
    memset(tx, 0, sizeof(*tx));
}

static bool native_is_idle(const uint8_t *slot, const void *controller)
{
    uint32_t ep_ctl = USBController_Ep3InControl;
    return slot != NULL && controller != NULL &&
           RV_FIELD(slot, USBEndpointSlot_active, uint8_t) != 0U &&
           RV_FIELD(slot, USBEndpointSlot_busy, uint8_t) == 0U &&
           RV_FIELD(slot, USBEndpointSlot_dispatch, uint8_t) == 0U &&
           (ep_ctl & USBController_InCtl_Active) != 0U &&
           (ep_ctl & USBController_InCtl_Enable) == 0U &&
           USBController_DeferredEndpoint == 0U &&
           RV_FIELD(controller, USBController_ep3_suspended, uint8_t) == 0U &&
           (USBController_GlobalInterruptStatus & USBController_ResetPending) == 0U;
}

static uint32_t native_clock_us(void *context)
{
    (void)context;
    uint32_t now = 0U;
    Clock_ReadMicroseconds(&now);
    return now;
}

static bool native_ready(void *context)
{
    const struct iap2_native_tx *tx = context;
    return tx != NULL && tx->held && !tx->draining &&
           native_is_idle(tx->slot, tx->controller);
}

static int native_submit(void *context, size_t length)
{
    struct iap2_native_tx *tx = context;
    if (tx == NULL || !tx->held || tx->draining || tx->buffer == NULL ||
        length == 0U || length > tx->capacity) {
        return -1;
    }

    uint32_t flags = tx->api->irq_save();
    bool idle = native_is_idle(tx->slot, tx->controller);
    if (idle) {
        tx->saved_in_mask = USBController_InInterruptMask;
    }
    tx->api->irq_restore(flags);

    if (!idle) {
        return -1;
    }

    return USBController_SubmitTransfer(tx->controller, USBHID_InEndpoint,
                                        tx->buffer, (uint32_t)length);
}

static int native_completion(void *context, size_t *residual)
{
    const struct iap2_native_tx *tx = context;
    if (tx == NULL || !tx->held) {
        return -1;
    }

    if ((USBController_Ep3InControl & USBController_InCtl_Enable) != 0U ||
        USBController_DeferredEndpoint == USBHID_InEndpoint ||
        RV_FIELD(tx->controller, USBController_ep3_suspended, uint8_t) != 0U) {
        return -3;
    }

    uint32_t hw_residual = 0U;
    int rc = USBController_ReadTransferResult(tx->controller, USBHID_InEndpoint,
                                              &hw_residual);
    if (rc != -3 && residual != NULL) {
        *residual = (size_t)hw_residual;
    }
    return rc;
}

static void native_tx_scrub(struct iap2_native_tx *tx)
{
    uint32_t flags = tx->api->irq_save();
    if (tx->pump.tx.report_len != 0U && tx->buffer != NULL) {
        uintptr_t base = (uintptr_t)tx->buffer & ~(uintptr_t)DRAM_UNCACHED_ALIAS;
        uintptr_t end = base + tx->capacity;
        uintptr_t dma = (uintptr_t)USBController_Ep3InDma;
        if (dma >= base && dma <= end) {
            RV_FIELD(tx->controller, USBController_ep3_suspended, uint8_t) = 0U;
            if (USBController_DeferredEndpoint == USBHID_InEndpoint) {
                USBController_DeferredEndpoint = 0U;
                if (USBController_InInterruptMask == USBController_DeferredInMask) {
                    USBController_InInterruptMask =
                        (USBController_InInterruptMask & ~USBController_DeferredInBits) |
                        (tx->saved_in_mask & USBController_DeferredInBits);
                }
            }
        }
    }
    tx->api->irq_restore(flags);
}

static void native_tx_close(struct iap2_native_tx *tx)
{
    const struct rv_api *api = tx->api;
    rv_extension_handle self = tx->self;

    if (tx->link != NULL) {
        iap2_tx_abort(&tx->pump.tx, tx->link);
    }
    USBAlignedBuffer_Destroy(tx->allocation);
    native_tx_clear(tx);
    if (api != NULL && api->extension_release != NULL && self != NULL) {
        api->extension_release(self);
    }
}

bool iap2_native_tx_open(struct iap2_native_tx *tx, const struct rv_api *api,
                         rv_extension_handle self, struct iap2_link *link, void *hid)
{
    if (tx == NULL || api == NULL || self == NULL || link == NULL || hid == NULL ||
        tx->held || api->irq_save == NULL || api->irq_restore == NULL ||
        api->extension_hold == NULL || api->extension_release == NULL) {
        return false;
    }

    void *usb = RV_FIELD(hid, USBHID_usb, void *);
    if (usb == NULL) {
        return false;
    }
    void *controller = RV_FIELD(usb, USBDevice_controller, void *);
    if (controller == NULL) {
        return false;
    }
    const uint8_t *map = (const uint8_t *)hid + USBHID_report_map;

    uint8_t speed = RV_FIELD(map, USBReportMap_speed, uint8_t);
    if (speed > 1U) {
        return false;
    }
    size_t in_count = (speed != 0U)
        ? (size_t)RV_FIELD(map, USBReportMap_hs_in_count, uint16_t)
        : (size_t)RV_FIELD(map, USBReportMap_fs_in_count, uint16_t);
    size_t speed_cap = (speed != 0U)
        ? IAP2_HID_HS_MAX_REPORT_LEN
        : IAP2_HID_FS_MAX_REPORT_LEN;
    const struct iap2_hid_report_row *in_table =
        RV_FIELD(map, USBReportMap_current_in_table, const struct iap2_hid_report_row *);
    if (in_table == NULL || in_count == 0U || in_count > IAP2_HID_MAX_REPORT_ROWS) {
        return false;
    }

    uint8_t *slot = NULL;
    for (size_t i = 0; i < USBEndpointSlot_Count; i++) {
        uint8_t *cand = USBEndpointSlots + i * USBEndpointSlot_bytes;
        if (RV_FIELD(cand, USBEndpointSlot_address, uint8_t) == USBHID_InEndpoint) {
            slot = cand;
            break;
        }
    }
    if (slot == NULL || !native_is_idle(slot, controller)) {
        return false;
    }

    uint32_t max_transfer = RV_FIELD(slot, USBEndpointSlot_max_transfer, uint32_t);
    size_t cap = (max_transfer < speed_cap) ? (size_t)max_transfer : speed_cap;
    if (cap < 8U) {
        return false;
    }

    native_tx_clear(tx);
    if (api->extension_hold(self) != RV_OK) {
        return false;
    }

    if (USBAlignedBuffer_Init(tx->allocation, (uint32_t)cap, USBAlignedBuffer_Heap) == NULL) {
        api->extension_release(self);
        native_tx_clear(tx);
        return false;
    }

    uint8_t *buf = RV_FIELD(tx->allocation, USBAlignedBuffer_data, uint8_t *);
    if (buf == NULL ||
        (((uintptr_t)buf & DRAM_UNCACHED_ALIAS) == 0U) ||
        (((uintptr_t)buf & 31U) != 0U)) {
        USBAlignedBuffer_Destroy(tx->allocation);
        api->extension_release(self);
        native_tx_clear(tx);
        return false;
    }

    uint32_t flags = api->irq_save();
    bool still_idle = native_is_idle(slot, controller);
    api->irq_restore(flags);
    if (!still_idle) {
        USBAlignedBuffer_Destroy(tx->allocation);
        api->extension_release(self);
        native_tx_clear(tx);
        return false;
    }

    uint32_t stale_residual = 0U;
    (void)USBController_ReadTransferResult(controller, USBHID_InEndpoint, &stale_residual);

    tx->api = api;
    tx->self = self;
    tx->link = link;
    tx->usb = usb;
    tx->controller = controller;
    tx->slot = slot;
    tx->buffer = buf;
    tx->capacity = cap;
    tx->in_table = in_table;
    tx->in_count = in_count;
    tx->held = true;
    tx->draining = false;
    return true;
}

void iap2_native_tx_poll(struct iap2_native_tx *tx, void *controller)
{
    if (tx == NULL || !tx->held || tx->draining || controller != tx->controller ||
        tx->link == NULL) {
        return;
    }

    struct iap2_tx_io io = {
        .context = tx,
        .buffer = tx->buffer,
        .capacity = tx->capacity,
        .in_table = tx->in_table,
        .in_count = tx->in_count,
        .clock_us = native_clock_us,
        .ready = native_ready,
        .submit = native_submit,
        .completion = native_completion,
    };
    (void)iap2_tx_pump(&tx->pump, tx->link, &io);
}

void iap2_native_tx_before_release(struct iap2_native_tx *tx, void *usb, uint32_t ep)
{
    if (tx == NULL || !tx->held || usb != tx->usb || ep != USBHID_InEndpoint) {
        return;
    }

    tx->draining = true;
    native_tx_scrub(tx);
}

void iap2_native_tx_after_release(struct iap2_native_tx *tx, void *usb, uint32_t ep)
{
    if (tx == NULL || !tx->held || usb != tx->usb || ep != USBHID_InEndpoint) {
        return;
    }

    native_tx_scrub(tx);
    if (tx->pump.tx.report_len == 0U) {
        native_tx_close(tx);
    }
}

void iap2_native_tx_after_reset(struct iap2_native_tx *tx, void *controller)
{
    if (tx == NULL || !tx->held || !tx->draining || controller != tx->controller) {
        return;
    }

    native_tx_scrub(tx);

    uint32_t flags = tx->api->irq_save();
    uint32_t crst = USBController_CoreResetControl;
    uint32_t ep_ctl = USBController_Ep3InControl;
    uint32_t deferred = USBController_DeferredEndpoint;
    uint8_t suspended = RV_FIELD(tx->controller, USBController_ep3_suspended, uint8_t);
    tx->api->irq_restore(flags);

    if ((crst & USBController_AhbIdle) != 0U &&
        (crst & USBController_CoreReset) == 0U &&
        (ep_ctl & USBController_InCtl_Enable) == 0U &&
        deferred != USBHID_InEndpoint &&
        suspended == 0U) {
        native_tx_close(tx);
    }
}
