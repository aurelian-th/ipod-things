// SPDX-License-Identifier: GPL-3.0-only
/*
 * Adapted from PodBox firmware/usbstack/usb_iap2.c
 * (commit 7a89c33d0ed49c93ba49a8198e822b8adb72d0a5).
 * PodBox is GPL-2.0-or-later; this version is GPL-3.0-only.
 * Modified by the RevolaOS authors, 2026.
 */

#include "link.h"
#ifdef __arm__
#include "rv_libc.h"
#else
#include <string.h>
#endif

#define TX_TYPE_NONE    0
#define TX_TYPE_ECHO    1
#define TX_TYPE_SYN_ACK 2
#define TX_TYPE_RST     3
#define TX_TYPE_DATA    4
#define TX_TYPE_ACK     5

static inline uint8_t link_header_checksum(const uint8_t *p)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < 8; i++) {
        sum = (uint8_t)(sum + p[i]);
    }
    return (uint8_t)(-sum);
}

static inline bool link_time_after_eq(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) >= 0;
}

static void drop_acked(struct iap2_link *link)
{
    while (link->count > 0) {
        struct iap2_link_slot *s = &link->slots[link->head];
        if (!s->tries || s->in_flight) {
            break;
        }
        if ((uint8_t)(link->last_ack - s->seq) >= 128) {
            break;
        }
        s->queued = false;
        s->tries = 0;
        s->len = 0;
        link->head = (link->head + 1) % IAP2_LINK_MAX_SLOTS;
        link->count--;
    }
}

static struct iap2_link_slot *next_queued(struct iap2_link *link)
{
    if (!link || !link->link_up) {
        return NULL;
    }
    unsigned limit = link->count < link->window ? link->count : link->window;
    for (unsigned i = 0; i < limit; i++) {
        struct iap2_link_slot *s = &link->slots[(link->head + i) % IAP2_LINK_MAX_SLOTS];
        if (s->queued && !s->in_flight) {
            return s;
        }
    }
    return NULL;
}

static void reset_session(struct iap2_link *link)
{
    link->state = IAP2_LINK_STATE_DOWN;
    link->synced = false;
    link->link_up = false;
    link->syn_ack_sent = false;
    link->unacked = 0;
    link->unacked_since_ms = 0;
    link->tx_unacked = 0;
    link->tx_unacked_since_ms = 0;
    link->rx_accepted = 0;
    link->last_sent_rx_ack = 0;
    link->tx_rx_ack = 0;
    link->pending = 0;
    link->last_ack = link->my_seq;
    link->highest_sent_seq = link->my_seq;
    link->has_deferred_ack = false;
    link->deferred_ack = 0;

    if (link->tx_in_flight) {
        link->tx_stale = true;
        link->head = link->tx_slot
            ? (unsigned)((link->tx_slot - link->slots + 1) % IAP2_LINK_MAX_SLOTS)
            : 0;
        link->count = 0;
        for (size_t i = 0; i < IAP2_LINK_MAX_SLOTS; i++) {
            if (&link->slots[i] == link->tx_slot) {
                continue;
            }
            link->slots[i].queued = false;
            link->slots[i].in_flight = false;
            link->slots[i].tries = 0;
            link->slots[i].len = 0;
        }
    } else {
        link->tx_stale = false;
        link->tx_submitted = false;
        link->tx_type = TX_TYPE_NONE;
        link->tx_slot = NULL;
        link->tx_frame = NULL;
        link->tx_len = 0;
        link->head = 0;
        link->count = 0;
        for (size_t i = 0; i < IAP2_LINK_MAX_SLOTS; i++) {
            link->slots[i].queued = false;
            link->slots[i].in_flight = false;
            link->slots[i].tries = 0;
            link->slots[i].len = 0;
        }
    }
}

void iap2_link_reset(struct iap2_link *link)
{
    if (!link) {
        return;
    }
    reset_session(link);
}

void iap2_link_disconnect(struct iap2_link *link)
{
    if (!link) {
        return;
    }
    reset_session(link);
    link->my_seq = 1;
    link->peer_seq = 0;
    link->syn_seq = 0;
    link->last_ack = 1;
    link->highest_sent_seq = 1;
    link->timeout_exhausted = false;
    link->control_session = 0;
    link->file_session = 0;
    link->window = 1;
    link->peer_max = IAP2_LINK_MAX_PACKET;
    link->rto_ms = 0;
    link->cack_ms = 0;
    link->max_sends = 1;
    link->max_cacks = 0;
    link->syn_ack_len = 0;
    link->last_syn_len = 0;
    link->last_syn_seq = 0;
    link->last_sent_peer_ack = 0;
    link->tx_peer_ack = 0;
    link->ack_req_seq = 0;
    link->tx_ack_req_seq = 0;
    link->syn_req_seq = 0;
    link->tx_syn_req_seq = 0;
    link->rx_accepted = 0;
    link->last_sent_rx_ack = 0;
    link->tx_rx_ack = 0;
}

iap2_status_t iap2_link_init(struct iap2_link *link, size_t rx_capacity)
{
    if (!link) {
        return IAP2_STATUS_INVALID_ARG;
    }
    if (rx_capacity < IAP2_LINK_MIN_RX_PACKET || rx_capacity > IAP2_LINK_MAX_RX_PACKET) {
        return IAP2_STATUS_INVALID_ARG;
    }
    memset(link, 0, sizeof(*link));
    link->rx_capacity = rx_capacity;
    iap2_link_disconnect(link);
    return IAP2_STATUS_OK;
}

bool iap2_link_is_up(const struct iap2_link *link)
{
    return link ? link->link_up : false;
}

iap2_status_t iap2_link_handle_probe(struct iap2_link *link)
{
    if (!link) {
        return IAP2_STATUS_INVALID_ARG;
    }
    reset_session(link);
    link->timeout_exhausted = false;
    link->pending |= IAP2_LINK_PENDING_ECHO;
    return IAP2_STATUS_OK;
}

static iap2_status_t take_syn(struct iap2_link *link,
                              uint8_t seq,
                              const uint8_t *payload,
                              size_t payload_len)
{
    if (payload_len < IAP2_LINK_SYN_FIXED_LEN || payload_len > IAP2_LINK_SYN_ACK_MAX) {
        return IAP2_STATUS_MALFORMED;
    }
    if ((payload_len - IAP2_LINK_SYN_FIXED_LEN) % 3 != 0) {
        return IAP2_STATUS_MALFORMED;
    }
    if (!payload || payload[0] != 1) {
        return IAP2_STATUS_MALFORMED;
    }

    uint8_t peer_win = payload[1];
    if (peer_win == 0) {
        return IAP2_STATUS_MALFORMED;
    }
    uint16_t window = peer_win > IAP2_LINK_MAX_SLOTS ? IAP2_LINK_MAX_SLOTS : peer_win;

    uint16_t peer_rx = (uint16_t)(((uint16_t)payload[2] << 8) | payload[3]);
    if (peer_rx < IAP2_HEADER_LEN + 2) {
        return IAP2_STATUS_MALFORMED;
    }
    size_t peer_max = peer_rx > IAP2_LINK_MAX_PACKET ? IAP2_LINK_MAX_PACKET : peer_rx;

    size_t num_sessions = (payload_len - IAP2_LINK_SYN_FIXED_LEN) / 3;
    bool has_ctrl = false;
    uint8_t ctrl_id = 0;
    uint8_t file_id = 0;
    uint8_t seen_ids[16];
    uint8_t seen_types[16];

    for (size_t s = 0; s < num_sessions; s++) {
        size_t off = IAP2_LINK_SYN_FIXED_LEN + s * 3;
        uint8_t s_id = payload[off];
        uint8_t s_type = payload[off + 1];

        if (s_id == 0) {
            return IAP2_STATUS_MALFORMED;
        }
        for (size_t k = 0; k < s; k++) {
            if (seen_ids[k] == s_id || seen_types[k] == s_type) {
                return IAP2_STATUS_MALFORMED;
            }
        }
        seen_ids[s] = s_id;
        seen_types[s] = s_type;

        if (s_type == 0) {
            has_ctrl = true;
            ctrl_id = s_id;
        } else if (s_type == 1) {
            file_id = s_id;
        }
    }

    if (!has_ctrl) {
        return IAP2_STATUS_MALFORMED;
    }

    uint8_t retrans = payload[8];
    uint16_t rto_raw = (uint16_t)(((uint16_t)payload[4] << 8) | payload[5]);
    uint32_t rto_ms = 0;
    if (retrans != 0) {
        if (rto_raw == 0) {
            return IAP2_STATUS_MALFORMED;
        }
        rto_ms = rto_raw < 100 ? 100 : rto_raw;
    }
    uint16_t max_sends = (uint16_t)retrans + 1U;
    uint32_t cack_ms = (uint16_t)(((uint16_t)payload[6] << 8) | payload[7]);
    uint8_t max_cacks = payload[9];

    uint8_t syn_ack[IAP2_LINK_SYN_ACK_MAX];
    syn_ack[0] = 1;
    syn_ack[1] = payload[1] > 0x7F ? 0x7F : payload[1];

    size_t rx_cap = link->rx_capacity;
    uint16_t offered_rx = peer_rx > rx_cap ? (uint16_t)rx_cap : peer_rx;
    syn_ack[2] = (uint8_t)(offered_rx >> 8);
    syn_ack[3] = (uint8_t)(offered_rx & 0xFF);
    syn_ack[4] = (uint8_t)(rto_ms >> 8);
    syn_ack[5] = (uint8_t)(rto_ms & 0xFF);
    syn_ack[6] = payload[6];
    syn_ack[7] = payload[7];
    syn_ack[8] = payload[8];
    syn_ack[9] = payload[9];

    size_t syn_ack_len = 10;
    for (size_t s = 0; s < num_sessions; s++) {
        size_t off = IAP2_LINK_SYN_FIXED_LEN + s * 3;
        uint8_t s_type = payload[off + 1];
        if (s_type == 0 || s_type == 1) {
            if (syn_ack_len + 3 <= sizeof(syn_ack)) {
                syn_ack[syn_ack_len++] = payload[off];
                syn_ack[syn_ack_len++] = payload[off + 1];
                syn_ack[syn_ack_len++] = payload[off + 2];
            }
        }
    }

    if (!link->link_up && link->synced &&
        link->last_syn_len == payload_len &&
        link->last_syn_seq == seq &&
        memcmp(link->last_syn_payload, payload, payload_len) == 0) {
        link->pending |= IAP2_LINK_PENDING_SYN_ACK;
        link->syn_req_seq++;
        return IAP2_STATUS_OK;
    }

    reset_session(link);
    link->my_seq = 1;
    link->syn_seq = link->my_seq;
    link->peer_seq = seq;
    link->last_sent_peer_ack = seq;
    link->last_ack = link->my_seq;
    link->highest_sent_seq = link->my_seq;
    link->timeout_exhausted = false;
    link->syn_ack_sent = false;
    link->control_session = ctrl_id;
    link->file_session = file_id;
    link->window = window;
    link->peer_max = peer_max;
    link->rto_ms = rto_ms;
    link->max_sends = max_sends;
    link->cack_ms = cack_ms;
    link->max_cacks = max_cacks;
    memcpy(link->syn_ack_payload, syn_ack, syn_ack_len);
    link->syn_ack_len = syn_ack_len;
    memcpy(link->last_syn_payload, payload, payload_len);
    link->last_syn_len = payload_len;
    link->last_syn_seq = seq;
    link->synced = true;
    link->state = IAP2_LINK_STATE_SYN_RCVD;
    link->pending |= IAP2_LINK_PENDING_SYN_ACK;
    link->syn_req_seq++;

    return IAP2_STATUS_OK;
}

static void take_ack(struct iap2_link *link, uint8_t ack)
{
    if (!link->link_up && link->synced) {
        bool syn_ack_live = link->syn_ack_sent ||
            (link->tx_in_flight && !link->tx_stale && link->tx_submitted &&
             link->tx_type == TX_TYPE_SYN_ACK);
        if (syn_ack_live && ack == link->syn_seq) {
            link->link_up = true;
            link->state = IAP2_LINK_STATE_UP;
            link->syn_ack_sent = true;
            link->pending &= ~IAP2_LINK_PENDING_SYN_ACK;
            link->last_ack = ack;
            link->highest_sent_seq = ack;
        }
        return;
    }

    if (!link->link_up) {
        return;
    }

    uint8_t span = (uint8_t)(link->highest_sent_seq - link->last_ack);
    uint8_t delta = (uint8_t)(ack - link->last_ack);

    if (delta > 0 && delta <= span) {
        link->last_ack = ack;
        drop_acked(link);
        return;
    }

    if (link->tx_in_flight && !link->tx_stale && link->tx_submitted &&
        link->tx_type == TX_TYPE_DATA && link->tx_slot != NULL &&
        link->tx_slot->tries == 0) {
        uint8_t inflight_span = (uint8_t)(link->tx_slot->seq - link->last_ack);
        if (delta > 0 && delta <= inflight_span) {
            link->has_deferred_ack = true;
            link->deferred_ack = ack;
        }
    }
}

iap2_status_t iap2_link_receive_packet(struct iap2_link *link,
                                       const uint8_t *packet,
                                       size_t packet_len,
                                       struct iap2_link_rx_result *out_result)
{
    if (!link || !packet || !out_result) {
        return IAP2_STATUS_INVALID_ARG;
    }

    out_result->packet_handled = false;
    out_result->has_payload = false;
    out_result->session = 0;
    out_result->payload = NULL;
    out_result->payload_len = 0;

    if (packet_len > link->rx_capacity) {
        return IAP2_STATUS_BUFFER_TOO_SMALL;
    }

    if (packet_len == IAP2_PROBE_LEN &&
        memcmp(packet, iap2_probe_wire, IAP2_PROBE_LEN) == 0) {
        iap2_link_handle_probe(link);
        out_result->packet_handled = true;
        return IAP2_STATUS_OK;
    }

    struct iap2_packet_view view;
    iap2_status_t st = iap2_decode_packet(packet, packet_len, &view);
    if (st != IAP2_STATUS_OK) {
        return st;
    }

    uint8_t control = view.header.control;
    uint8_t seq = view.header.seq;
    uint8_t ack = view.header.ack;
    uint8_t session = view.header.session;

    if ((control & (uint8_t)~(IAP2_CTL_SYN | IAP2_CTL_ACK |
                             IAP2_CTL_EAK | IAP2_CTL_RST)) != 0) {
        return IAP2_STATUS_MALFORMED;
    }

    if (control & IAP2_CTL_RST) {
        if (control != IAP2_CTL_RST || session != 0 || view.payload_len != 0) {
            return IAP2_STATUS_MALFORMED;
        }
        iap2_link_reset(link);
        out_result->packet_handled = true;
        return IAP2_STATUS_OK;
    }

    if (control & IAP2_CTL_SYN) {
        if (control != IAP2_CTL_SYN || session != 0) {
            return IAP2_STATUS_MALFORMED;
        }
        iap2_status_t syn_st = take_syn(link, seq, view.payload, view.payload_len);
        if (syn_st == IAP2_STATUS_OK) {
            out_result->packet_handled = true;
        }
        return syn_st;
    }

    if (!link->synced) {
        return IAP2_STATUS_UNCLAIMED;
    }

    /* This engine supports ACK-bearing data; bare ACKs use link session 0. */
    if (control & IAP2_CTL_EAK) {
        if (session != 0 || view.payload_len == 0 ||
            (control & (uint8_t)~(IAP2_CTL_EAK | IAP2_CTL_ACK)) != 0) {
            return IAP2_STATUS_MALFORMED;
        }
    } else {
        if (control != IAP2_CTL_ACK) {
            return IAP2_STATUS_MALFORMED;
        }
        if (view.payload_len == 0) {
            if (session != 0) {
                return IAP2_STATUS_MALFORMED;
            }
        } else if (session == 0 ||
                   (session != link->control_session &&
                    (link->file_session == 0 || session != link->file_session))) {
            return IAP2_STATUS_MALFORMED;
        }
    }

    if (control & IAP2_CTL_ACK) {
        take_ack(link, ack);
    }

    /* EAK selective hints are ignored; retransmission uses the RTO. */
    if (!(control & IAP2_CTL_EAK) && view.payload_len > 0) {
        if (link->link_up && seq == (uint8_t)(link->peer_seq + 1U)) {
            link->peer_seq = seq;
            out_result->session = session;
            out_result->payload = view.payload;
            out_result->payload_len = view.payload_len;
            out_result->has_payload = true;

            /* The pinned reference's zero-timeout peer stops reading on bare ACK. */
            if (link->cack_ms > 0) {
                if (link->unacked == 0) {
                    link->unacked_since_ms = link->last_now_ms;
                }
                link->rx_accepted++;
                if (link->unacked < UINT32_MAX) {
                    link->unacked++;
                }
                if (link->max_cacks > 0 && link->unacked >= link->max_cacks) {
                    link->pending |= IAP2_LINK_PENDING_ACK;
                    link->ack_req_seq++;
                }
            }
        } else if (link->link_up && link->cack_ms > 0 &&
                   (uint8_t)(link->peer_seq - seq) < 128) {
            link->pending |= IAP2_LINK_PENDING_ACK;
            link->ack_req_seq++;
        }
    }

    out_result->packet_handled = true;
    return IAP2_STATUS_OK;
}

iap2_status_t iap2_link_tick(struct iap2_link *link, uint32_t now_ms)
{
    if (!link) {
        return IAP2_STATUS_INVALID_ARG;
    }

    link->last_now_ms = now_ms;

    if (link->unacked > 0 && link->cack_ms > 0) {
        if (link_time_after_eq(now_ms, link->unacked_since_ms + link->cack_ms)) {
            link->pending |= IAP2_LINK_PENDING_ACK;
            link->ack_req_seq++;
        }
    }

    if (link->link_up && link->count > 0 && link->rto_ms > 0) {
        unsigned limit = link->count < link->window ? link->count : link->window;
        for (unsigned i = 0; i < limit; i++) {
            struct iap2_link_slot *s = &link->slots[(link->head + i) % IAP2_LINK_MAX_SLOTS];
            if ((uint8_t)(link->last_ack - s->seq) < 128) {
                continue;
            }
            if (s->tries > 0 && !s->queued && !s->in_flight) {
                if (link_time_after_eq(now_ms, s->sent_ms + link->rto_ms)) {
                    if (s->tries >= link->max_sends) {
                        iap2_link_reset(link);
                        link->timeout_exhausted = true;
                        link->pending |= IAP2_LINK_PENDING_RST;
                        break;
                    } else {
                        s->queued = true;
                    }
                }
            }
        }
    }

    return IAP2_STATUS_OK;
}

iap2_status_t iap2_link_queue_payload(struct iap2_link *link,
                                      uint8_t session,
                                      const uint8_t *payload,
                                      size_t payload_len)
{
    if (!link || !payload || payload_len == 0) {
        return IAP2_STATUS_INVALID_ARG;
    }
    if (!link->link_up) {
        return IAP2_STATUS_UNCLAIMED;
    }
    if (session == 0) {
        return IAP2_STATUS_INVALID_ARG;
    }
    if (session != link->control_session &&
        (link->file_session == 0 || session != link->file_session)) {
        return IAP2_STATUS_INVALID_ARG;
    }

    size_t cap = link->peer_max < IAP2_LINK_MAX_PACKET ? link->peer_max : IAP2_LINK_MAX_PACKET;
    if (cap <= IAP2_HEADER_LEN + 1 || payload_len > cap - IAP2_HEADER_LEN - 1) {
        return IAP2_STATUS_BUFFER_TOO_SMALL;
    }

    unsigned max_active = IAP2_LINK_MAX_SLOTS -
        ((link->tx_stale && link->tx_slot) ? 1U : 0U);
    if (link->count >= max_active) {
        return IAP2_STATUS_BUFFER_TOO_SMALL;
    }

    uint8_t next_seq = (uint8_t)(link->my_seq + 1U);
    struct iap2_link_slot *s = &link->slots[(link->head + link->count) % IAP2_LINK_MAX_SLOTS];
    size_t out_len = 0;
    iap2_status_t st = iap2_encode_packet(s->packet,
                                          sizeof(s->packet),
                                          IAP2_CTL_ACK,
                                          next_seq,
                                          link->peer_seq,
                                          session,
                                          payload,
                                          payload_len,
                                          &out_len);
    if (st != IAP2_STATUS_OK) {
        return st;
    }

    link->my_seq = next_seq;
    s->seq = next_seq;
    s->len = out_len;
    s->tries = 0;
    s->sent_ms = 0;
    s->queued = true;
    s->in_flight = false;
    link->count++;

    return IAP2_STATUS_OK;
}

iap2_status_t iap2_link_peek_tx(struct iap2_link *link,
                                const uint8_t **out_frame,
                                size_t *out_len)
{
    if (!link || !out_frame || !out_len) {
        return IAP2_STATUS_INVALID_ARG;
    }

    *out_frame = NULL;
    *out_len = 0;

    if (link->tx_in_flight) {
        *out_frame = link->tx_frame;
        *out_len = link->tx_len;
        return IAP2_STATUS_OK;
    }

    if (link->pending & IAP2_LINK_PENDING_ECHO) {
        link->tx_type = TX_TYPE_ECHO;
        link->tx_frame = iap2_probe_wire;
        link->tx_len = IAP2_PROBE_LEN;
        link->tx_in_flight = true;
        link->tx_submitted = false;
        *out_frame = link->tx_frame;
        *out_len = link->tx_len;
        return IAP2_STATUS_OK;
    }

    if (link->pending & IAP2_LINK_PENDING_SYN_ACK) {
        size_t enc_len = 0;
        iap2_status_t st = iap2_encode_packet(link->ctl_buf,
                                              sizeof(link->ctl_buf),
                                              IAP2_CTL_SYN | IAP2_CTL_ACK,
                                              link->syn_seq,
                                              link->peer_seq,
                                              0,
                                              link->syn_ack_payload,
                                              link->syn_ack_len,
                                              &enc_len);
        if (st != IAP2_STATUS_OK) {
            return st;
        }
        link->tx_type = TX_TYPE_SYN_ACK;
        link->tx_frame = link->ctl_buf;
        link->tx_len = enc_len;
        link->tx_in_flight = true;
        link->tx_submitted = false;
        link->tx_syn_req_seq = link->syn_req_seq;
        *out_frame = link->tx_frame;
        *out_len = link->tx_len;
        return IAP2_STATUS_OK;
    }

    if (link->pending & IAP2_LINK_PENDING_RST) {
        size_t enc_len = 0;
        iap2_status_t st = iap2_encode_packet(link->ctl_buf,
                                              sizeof(link->ctl_buf),
                                              IAP2_CTL_RST,
                                              link->my_seq,
                                              link->peer_seq,
                                              0,
                                              NULL,
                                              0,
                                              &enc_len);
        if (st != IAP2_STATUS_OK) {
            return st;
        }
        link->tx_type = TX_TYPE_RST;
        link->tx_frame = link->ctl_buf;
        link->tx_len = enc_len;
        link->tx_in_flight = true;
        link->tx_submitted = false;
        *out_frame = link->tx_frame;
        *out_len = link->tx_len;
        return IAP2_STATUS_OK;
    }

    struct iap2_link_slot *s = next_queued(link);
    if (s != NULL) {
        s->packet[6] = link->peer_seq;
        s->packet[8] = link_header_checksum(s->packet);
        s->in_flight = true;
        link->tx_peer_ack = link->peer_seq;
        link->tx_ack_req_seq = link->ack_req_seq;
        link->tx_rx_ack = link->rx_accepted;
        link->tx_unacked = link->unacked;
        link->tx_unacked_since_ms = link->unacked_since_ms;
        link->unacked = 0;
        link->unacked_since_ms = 0;
        link->tx_slot = s;
        link->tx_type = TX_TYPE_DATA;
        link->tx_frame = s->packet;
        link->tx_len = s->len;
        link->tx_in_flight = true;
        link->tx_submitted = false;
        *out_frame = link->tx_frame;
        *out_len = link->tx_len;
        return IAP2_STATUS_OK;
    }

    if (link->link_up && link->cack_ms > 0 &&
        (link->pending & IAP2_LINK_PENDING_ACK)) {
        size_t enc_len = 0;
        iap2_status_t st = iap2_encode_packet(link->ctl_buf,
                                              sizeof(link->ctl_buf),
                                              IAP2_CTL_ACK,
                                              link->my_seq,
                                              link->peer_seq,
                                              0,
                                              NULL,
                                              0,
                                              &enc_len);
        if (st != IAP2_STATUS_OK) {
            return st;
        }
        link->tx_peer_ack = link->peer_seq;
        link->tx_ack_req_seq = link->ack_req_seq;
        link->tx_rx_ack = link->rx_accepted;
        link->tx_unacked = link->unacked;
        link->tx_unacked_since_ms = link->unacked_since_ms;
        link->unacked = 0;
        link->unacked_since_ms = 0;
        link->tx_type = TX_TYPE_ACK;
        link->tx_frame = link->ctl_buf;
        link->tx_len = enc_len;
        link->tx_in_flight = true;
        link->tx_submitted = false;
        *out_frame = link->tx_frame;
        *out_len = link->tx_len;
        return IAP2_STATUS_OK;
    }

    return IAP2_STATUS_OK;
}

iap2_status_t iap2_link_mark_submitted(struct iap2_link *link)
{
    if (!link) {
        return IAP2_STATUS_INVALID_ARG;
    }
    if (!link->tx_in_flight) {
        return IAP2_STATUS_INVALID_ARG;
    }
    link->tx_submitted = true;
    return IAP2_STATUS_OK;
}

iap2_status_t iap2_link_finish_tx(struct iap2_link *link, bool success)
{
    if (!link) {
        return IAP2_STATUS_INVALID_ARG;
    }

    if (!link->tx_in_flight) {
        return IAP2_STATUS_OK;
    }

    if (link->tx_stale) {
        if (link->tx_slot) {
            link->tx_slot->in_flight = false;
            link->tx_slot->queued = false;
            link->tx_slot->tries = 0;
            link->tx_slot->len = 0;
        }
        link->tx_in_flight = false;
        link->tx_stale = false;
        link->tx_submitted = false;
        link->tx_type = TX_TYPE_NONE;
        link->tx_slot = NULL;
        link->tx_frame = NULL;
        link->tx_len = 0;
        link->tx_unacked = 0;
        link->tx_unacked_since_ms = 0;
        link->has_deferred_ack = false;
        link->deferred_ack = 0;
        return IAP2_STATUS_OK;
    }

    if (success) {
        switch (link->tx_type) {
        case TX_TYPE_ECHO:
            link->pending &= ~IAP2_LINK_PENDING_ECHO;
            break;
        case TX_TYPE_SYN_ACK:
            if (link->link_up || link->syn_req_seq == link->tx_syn_req_seq) {
                link->pending &= ~IAP2_LINK_PENDING_SYN_ACK;
            }
            link->syn_ack_sent = true;
            break;
        case TX_TYPE_RST:
            link->pending &= ~IAP2_LINK_PENDING_RST;
            break;
        case TX_TYPE_ACK:
            link->last_sent_peer_ack = link->tx_peer_ack;
            link->last_sent_rx_ack = link->tx_rx_ack;
            link->tx_unacked = 0;
            link->tx_unacked_since_ms = 0;
            if (link->ack_req_seq == link->tx_ack_req_seq) {
                link->pending &= ~IAP2_LINK_PENDING_ACK;
            }
            if (link->cack_ms > 0 && link->max_cacks > 0 &&
                link->unacked >= link->max_cacks) {
                link->pending |= IAP2_LINK_PENDING_ACK;
            }
            break;
        case TX_TYPE_DATA: {
            link->last_sent_peer_ack = link->tx_peer_ack;
            link->last_sent_rx_ack = link->tx_rx_ack;
            link->tx_unacked = 0;
            link->tx_unacked_since_ms = 0;
            if (link->ack_req_seq == link->tx_ack_req_seq) {
                link->pending &= ~IAP2_LINK_PENDING_ACK;
            }
            if (link->cack_ms > 0 && link->max_cacks > 0 &&
                link->unacked >= link->max_cacks) {
                link->pending |= IAP2_LINK_PENDING_ACK;
            }

            struct iap2_link_slot *s = link->tx_slot;
            if (s) {
                s->in_flight = false;
                s->queued = false;
                if (s->tries == 0) {
                    link->highest_sent_seq = s->seq;
                }
                s->tries++;
                s->sent_ms = link->last_now_ms;

                link->tx_in_flight = false;
                if (link->has_deferred_ack) {
                    uint8_t def_ack = link->deferred_ack;
                    link->has_deferred_ack = false;
                    link->deferred_ack = 0;
                    take_ack(link, def_ack);
                }
                drop_acked(link);
            }
            break;
        }
        default:
            break;
        }
    } else {
        if (link->tx_type == TX_TYPE_SYN_ACK) {
            if (link->link_up) {
                /* Peer ACK already arrived before completion, proving delivery */
                link->pending &= ~IAP2_LINK_PENDING_SYN_ACK;
                link->syn_ack_sent = true;
            } else {
                link->pending |= IAP2_LINK_PENDING_SYN_ACK;
            }
        } else if (link->tx_type == TX_TYPE_ACK || link->tx_type == TX_TYPE_DATA) {
            if (link->tx_unacked > 0) {
                uint64_t total = (uint64_t)link->tx_unacked + (uint64_t)link->unacked;
                link->unacked = total > UINT32_MAX ? UINT32_MAX : (uint32_t)total;
                link->unacked_since_ms = link->tx_unacked_since_ms;
                if (link->cack_ms > 0 && link->max_cacks > 0 &&
                    link->unacked >= link->max_cacks) {
                    link->pending |= IAP2_LINK_PENDING_ACK;
                    link->ack_req_seq++;
                }
            }
            link->tx_unacked = 0;
            link->tx_unacked_since_ms = 0;

            if (link->tx_type == TX_TYPE_ACK) {
                link->pending |= IAP2_LINK_PENDING_ACK;
            } else if (link->tx_slot) {
                struct iap2_link_slot *s = link->tx_slot;
                link->has_deferred_ack = false;
                s->in_flight = false;
                uint8_t span = (uint8_t)(link->highest_sent_seq - link->last_ack);
                uint8_t delta = (uint8_t)(s->seq - link->last_ack);
                if (s->tries == 0 || (delta > 0 && delta <= span)) {
                    s->queued = true;
                }
                drop_acked(link);
            }
        }
    }

    link->tx_in_flight = false;
    link->tx_submitted = false;
    link->tx_type = TX_TYPE_NONE;
    link->tx_slot = NULL;
    link->tx_frame = NULL;
    link->tx_len = 0;

    return IAP2_STATUS_OK;
}
