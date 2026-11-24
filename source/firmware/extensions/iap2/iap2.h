/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef IAP2_H
#define IAP2_H

#include <stddef.h>
#include <stdint.h>
#include "rx_tap.h"
#include "session.h"

#define IAP2_TEXT_CAPACITY 64

enum iap2_protocol_mode {
    IAP2_MODE_IAP1 = 0,
    IAP2_MODE_IAP2 = 1,
};

struct iap2_diag_state {
    uint8_t protocol_mode;      /* enum iap2_protocol_mode */
    uint8_t usb_connected;      /* 1 = USBHID Activate currently connected */
    uint8_t tap_desired;        /* 0 = iAP1 passthrough, 1 = arm iAP2 on HID activate */
    uint8_t tap_active;         /* 1 = active session currently tapping HID SET_REPORT */
    uint8_t tx_desired;         /* 0 = RX-only, 1 = native EP3 IN TX enabled */
    uint8_t tx_active;          /* 1 = native EP3 IN TX leased for active session */
    uint8_t session_active;     /* 1 = USBHID session currently bound */
    uint8_t hold_acquired;      /* 1 = extension_hold currently held */
    uint8_t audio_route_usb;    /* AudioOutputRouteState[1] */
    uint8_t audio_route_applied;/* 1 = Topic-10 USB connect sent */
    uint8_t usb_audio_streaming;/* USBAudioState[streaming] */
    uint8_t usb_pcm_ready;      /* 1 = USBAudio_PcmCallback != NULL */
    uint32_t session_gen;       /* Incremented on session transitions */
    uint32_t hold_acquired_count;
    uint32_t hold_refused_count;
    uint32_t usb_host_rate;
    uint32_t digital_active_rate;
    struct iap2_rx_stats shared_rx_stats;
    struct iap2_link_summary shared_link_summary;
    struct iap2_session_summary shared_session_summary;
};

/* Coherent snapshot copied under IRQ mask for safe UI formatting outside lock */
struct iap2_diag_snapshot {
    uint8_t protocol_mode;
    uint8_t usb_connected;
    uint8_t tap_desired;
    uint8_t tap_active;
    uint8_t tx_desired;
    uint8_t tx_active;
    uint8_t session_active;
    uint8_t hold_acquired;
    uint8_t audio_route_usb;
    uint8_t audio_route_applied;
    uint8_t usb_audio_streaming;
    uint8_t usb_pcm_ready;
    uint32_t session_gen;
    uint32_t hold_acquired_count;
    uint32_t hold_refused_count;
    uint32_t usb_host_rate;
    uint32_t digital_active_rate;
    struct iap2_rx_stats rx_stats;
    struct iap2_link_summary link_summary;
    struct iap2_session_summary session_summary;
};

/* Core state lifecycle and snapshot */
void iap2_diag_init(struct iap2_diag_state *state);
void iap2_diag_snapshot(const struct iap2_diag_state *state, struct iap2_diag_snapshot *out);

/* Consumer Settings UI text formatting from coherent snapshot (outside IRQ lock) */
const char *iap2_ui_settings_caption(const struct iap2_diag_snapshot *snap);
const char *iap2_ui_protocol_caption(const struct iap2_diag_snapshot *snap);
const char *iap2_ui_status_row(const struct iap2_diag_snapshot *snap);
const char *iap2_ui_status_caption(const struct iap2_diag_snapshot *snap);

#endif /* IAP2_H */
