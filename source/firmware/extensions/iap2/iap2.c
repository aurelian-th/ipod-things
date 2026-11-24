/* SPDX-License-Identifier: GPL-3.0-only */
#include "iap2.h"

#ifdef __arm__
#include "rv_libc.h"
#define format_text(buf, size, ...) rv_format(buf, size, __VA_ARGS__)
#else
#include <stdio.h>
#define format_text(buf, size, ...) ((uint32_t)snprintf(buf, size, __VA_ARGS__))
#endif

static void clear_rx_stats(struct iap2_rx_stats *stats)
{
    stats->rx_total_reports = 0;
    stats->rx_iap1_passthrough = 0;
    stats->rx_bad_report = 0;
    stats->rx_iap2_probes = 0;
    stats->rx_iap2_packets = 0;
    stats->rx_link_handled = 0;
    stats->rx_link_payloads = 0;
    stats->rx_bad_checksum = 0;
    stats->rx_malformed = 0;
    stats->tx_queued_events = 0;
}

static void clear_link_summary(struct iap2_link_summary *summary)
{
    summary->link_state = (iap2_link_state_t)0;
    summary->pending_tx_mask = 0;
    summary->queued_slots = 0;
    summary->link_rx_accepted = 0;
    summary->my_seq = 0;
    summary->peer_seq = 0;
    summary->last_ack = 0;
    summary->last_session = 0;
    summary->last_payload_len = 0;
}

static void clear_session_summary(struct iap2_session_summary *summary)
{
    summary->phase = IAP2_SESSION_PHASE_IDLE;
    summary->authenticated = false;
    summary->identified = false;
    summary->audio_active = false;
    summary->now_playing_active = false;
    summary->hid_active = false;
    summary->audio_rate = 0;
    summary->rx_messages = 0;
    summary->tx_messages = 0;
    summary->last_rx_msg_id = 0;
    summary->last_tx_msg_id = 0;
    summary->car_current_ma = 0;
    summary->hid_control_count = 0;
    summary->artwork_transfers = 0;
    summary->last_artwork_bytes = 0;
    summary->artwork_phase = 0;
    summary->media_mask = 0;
    summary->playback_mask = 0;
    summary->library_mask = 0;
    summary->lib_tracks_sent = 0;
    summary->lib_tracks_total = 0;
    summary->lib_playlists_sent = 0;
    summary->lib_playlists_total = 0;
    summary->lib_phase = 0;
    summary->play_cmd_count = 0;
    summary->last_play_cmd_id = 0;
    for (size_t i = 0; i < IAP2_RX_LOG_HEAD; i++)
        summary->rx_head_ids[i] = 0;
    summary->rx_head_count = 0;
    for (size_t i = 0; i < IAP2_RX_LOG_TAIL; i++)
        summary->rx_tail_ids[i] = 0;
    summary->rx_tail_count = 0;
}

void iap2_diag_init(struct iap2_diag_state *state)
{
    if (!state)
        return;
    state->protocol_mode = IAP2_MODE_IAP2;
    state->usb_connected = 0;
    state->tap_desired = 0;
    state->tap_active = 0;
    state->tx_desired = 0;
    state->tx_active = 0;
    state->session_active = 0;
    state->hold_acquired = 0;
    state->audio_route_usb = 0;
    state->audio_route_applied = 0;
    state->usb_audio_streaming = 0;
    state->usb_pcm_ready = 0;
    state->session_gen = 0;
    state->hold_acquired_count = 0;
    state->hold_refused_count = 0;
    state->usb_host_rate = 0;
    state->digital_active_rate = 0;
    clear_rx_stats(&state->shared_rx_stats);
    clear_link_summary(&state->shared_link_summary);
    clear_session_summary(&state->shared_session_summary);
}

void iap2_diag_snapshot(const struct iap2_diag_state *state, struct iap2_diag_snapshot *out)
{
    if (!out)
        return;
    if (!state) {
        out->protocol_mode = IAP2_MODE_IAP2;
        out->usb_connected = 0;
        out->tap_desired = 0;
        out->tap_active = 0;
        out->tx_desired = 0;
        out->tx_active = 0;
        out->session_active = 0;
        out->hold_acquired = 0;
        out->audio_route_usb = 0;
        out->audio_route_applied = 0;
        out->usb_audio_streaming = 0;
        out->usb_pcm_ready = 0;
        out->session_gen = 0;
        out->hold_acquired_count = 0;
        out->hold_refused_count = 0;
        out->usb_host_rate = 0;
        out->digital_active_rate = 0;
        clear_rx_stats(&out->rx_stats);
        clear_link_summary(&out->link_summary);
        clear_session_summary(&out->session_summary);
        return;
    }
    out->protocol_mode = state->protocol_mode;
    out->usb_connected = state->usb_connected;
    out->tap_desired = state->tap_desired;
    out->tap_active = state->tap_active;
    out->tx_desired = state->tx_desired;
    out->tx_active = state->tx_active;
    out->session_active = state->session_active;
    out->hold_acquired = state->hold_acquired;
    out->audio_route_usb = state->audio_route_usb;
    out->audio_route_applied = state->audio_route_applied;
    out->usb_audio_streaming = state->usb_audio_streaming;
    out->usb_pcm_ready = state->usb_pcm_ready;
    out->session_gen = state->session_gen;
    out->hold_acquired_count = state->hold_acquired_count;
    out->hold_refused_count = state->hold_refused_count;
    out->usb_host_rate = state->usb_host_rate;
    out->digital_active_rate = state->digital_active_rate;
    out->rx_stats = state->shared_rx_stats;
    out->link_summary = state->shared_link_summary;
    out->session_summary = state->shared_session_summary;
}

static int is_replug_pending(const struct iap2_diag_snapshot *snap)
{
    if (!snap || !snap->usb_connected)
        return 0;
    if (snap->tap_desired != snap->tap_active)
        return 1;
    if (snap->tap_desired && snap->tx_desired != snap->tx_active)
        return 1;
    return 0;
}

const char *iap2_ui_settings_caption(const struct iap2_diag_snapshot *snap)
{
    if (snap && snap->tx_active && snap->session_summary.phase == IAP2_SESSION_PHASE_READY)
        return "iAP2 Connected";
    if (!snap || snap->protocol_mode == IAP2_MODE_IAP2)
        return "iAP2 (Newer cars)";
    return "iAP1 (Older cars)";
}

const char *iap2_ui_protocol_caption(const struct iap2_diag_snapshot *snap)
{
    if (!snap || snap->protocol_mode == IAP2_MODE_IAP2)
        return "Newer cars (USB)";
    return "Older cars & docks";
}

const char *iap2_ui_status_row(const struct iap2_diag_snapshot *snap)
{
    static char buf[IAP2_TEXT_CAPACITY];
    if (!snap)
        return "Ready (iAP2)";
    if (is_replug_pending(snap))
        return "Replug USB";
    if (snap->tx_active) {
        if (snap->session_summary.phase == IAP2_SESSION_PHASE_READY) {
            if (snap->session_summary.audio_active &&
                snap->session_summary.audio_rate >= 1000U) {
                format_text(buf, sizeof(buf), "Connected (%luk)",
                            (unsigned long)(snap->session_summary.audio_rate / 1000U));
                return buf;
            }
            return "Connected";
        }
        return "Connecting...";
    }
    if (snap->protocol_mode == IAP2_MODE_IAP1)
        return "Classic (iAP1)";
    return "Ready (iAP2)";
}

const char *iap2_ui_status_caption(const struct iap2_diag_snapshot *snap)
{
    if (!snap)
        return "Waiting for car";
    if (is_replug_pending(snap))
        return "Replug to switch";
    if (snap->tx_active) {
        if (snap->session_summary.phase == IAP2_SESSION_PHASE_READY) {
            if (snap->session_summary.audio_active)
                return "USB audio active";
            return "Car connected";
        }
        return "Connecting...";
    }
    if (snap->protocol_mode == IAP2_MODE_IAP1)
        return "Stock iAP1 active";
    return "Waiting for car";
}
