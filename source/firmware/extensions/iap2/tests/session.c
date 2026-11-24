// SPDX-License-Identifier: GPL-3.0-only
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "control.h"
#include "link.h"
#include "protocol.h"
#include "session.h"

struct host_capture {
    uint16_t ctrl_ids[64];
    uint8_t ctrl_payloads[64][IAP2_SESSION_TX_CAPACITY];
    size_t ctrl_lens[64];
    unsigned ctrl_count;

    uint8_t file_pkts[32][IAP2_SESSION_TX_CAPACITY];
    size_t file_lens[32];
    unsigned file_count;

    bool auth_seen;
    bool ident_seen;
    bool audio_active;
    uint32_t audio_rate;
    unsigned audio_calls;
    iap2_remote_action_t actions[16];
    unsigned action_count;
    uint32_t last_seek_ms;
    unsigned seek_count;

    unsigned artwork_done_calls;
    uint64_t last_artwork_key;
    bool last_artwork_completed;

    unsigned lib_start_calls;
    uint32_t lib_start_mask;
    char lib_start_rev[IAP2_LIB_REVISION_MAX];
    unsigned lib_stop_calls;
    unsigned lib_batch_done_calls;
    uint32_t last_lib_batch_done_id;
    unsigned pl_req_calls;
    size_t last_pl_req_idx;
    uint64_t last_pl_req_id;
    unsigned pl_done_calls;
    size_t last_pl_done_idx;
    bool last_pl_done_ok;
    unsigned q_done_calls;
    bool last_q_done_ok;
    unsigned play_items_calls;
    uint64_t play_items[8];
    size_t play_items_cnt;
    uint32_t play_items_start;
    unsigned play_coll_calls;
    uint64_t play_coll_id;
    uint8_t play_coll_type;
    uint32_t play_coll_start;
    unsigned play_special_calls;
    uint8_t play_special_type;
    uint64_t play_special_start_id;
};

static uint64_t rd_be64(const uint8_t *p)
{
    uint64_t hi = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                  ((uint32_t)p[2] << 8) | p[3];
    uint64_t lo = ((uint32_t)p[4] << 24) | ((uint32_t)p[5] << 16) |
                  ((uint32_t)p[6] << 8) | p[7];
    return (hi << 32) | lo;
}

static uint16_t rd_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t test_rng(void *ctx)
{
    (void)ctx;
    return 0x12345678U;
}

static void on_auth(void *ctx, bool ok)
{
    struct host_capture *cap = ctx;
    assert(ok);
    cap->auth_seen = true;
}

static void on_ident(void *ctx, const char *name, uint16_t cur)
{
    (void)name;
    (void)cur;
    struct host_capture *cap = ctx;
    cap->ident_seen = true;
}

static void on_audio(void *ctx, bool active, uint32_t rate)
{
    struct host_capture *cap = ctx;
    cap->audio_active = active;
    cap->audio_rate = rate;
    cap->audio_calls++;
}

static void on_artwork_done(void *ctx, uint64_t track_key, bool completed)
{
    struct host_capture *cap = ctx;
    cap->artwork_done_calls++;
    cap->last_artwork_key = track_key;
    cap->last_artwork_completed = completed;
}

static void on_lib_start(void *ctx, uint32_t mask, const char *car_rev)
{
    struct host_capture *cap = ctx;
    cap->lib_start_calls++;
    cap->lib_start_mask = mask;
    memset(cap->lib_start_rev, 0, sizeof(cap->lib_start_rev));
    if (car_rev != NULL) {
        strncpy(cap->lib_start_rev, car_rev, sizeof(cap->lib_start_rev) - 1U);
    }
}

static void on_lib_stop(void *ctx)
{
    struct host_capture *cap = ctx;
    cap->lib_stop_calls++;
}

static void on_lib_batch_done(void *ctx, uint32_t batch_id)
{
    struct host_capture *cap = ctx;
    cap->lib_batch_done_calls++;
    cap->last_lib_batch_done_id = batch_id;
}

static void on_pl_req(void *ctx, size_t idx, uint64_t pid)
{
    struct host_capture *cap = ctx;
    cap->pl_req_calls++;
    cap->last_pl_req_idx = idx;
    cap->last_pl_req_id = pid;
}

static void on_pl_done(void *ctx, size_t idx, bool completed)
{
    struct host_capture *cap = ctx;
    cap->pl_done_calls++;
    cap->last_pl_done_idx = idx;
    cap->last_pl_done_ok = completed;
}

static void on_q_done(void *ctx, bool completed)
{
    struct host_capture *cap = ctx;
    cap->q_done_calls++;
    cap->last_q_done_ok = completed;
}

static void on_play_items(void *ctx, const uint64_t *ids, size_t count, uint32_t start_idx)
{
    struct host_capture *cap = ctx;
    cap->play_items_calls++;
    cap->play_items_cnt = count;
    cap->play_items_start = start_idx;
    for (size_t i = 0U; i < count && i < 8U; i++) {
        cap->play_items[i] = ids[i];
    }
}

static void on_play_coll(void *ctx, uint64_t coll_id, uint8_t coll_type, uint32_t start_idx)
{
    struct host_capture *cap = ctx;
    cap->play_coll_calls++;
    cap->play_coll_id = coll_id;
    cap->play_coll_type = coll_type;
    cap->play_coll_start = start_idx;
}

static void on_play_special(void *ctx, uint8_t stype, uint64_t start_item_id)
{
    struct host_capture *cap = ctx;
    cap->play_special_calls++;
    cap->play_special_type = stype;
    cap->play_special_start_id = start_item_id;
}

static void on_hid(void *ctx, iap2_remote_action_t action)
{
    struct host_capture *cap = ctx;
    assert(cap->action_count < 16U);
    cap->actions[cap->action_count++] = action;
}

static void on_seek(void *ctx, uint32_t ms)
{
    struct host_capture *cap = ctx;
    cap->last_seek_ms = ms;
    cap->seek_count++;
}

static void on_host_ctrl_msg(void *ctx, const struct iap2_control_message *msg)
{
    struct host_capture *cap = ctx;
    assert(cap->ctrl_count < 64U);
    unsigned idx = cap->ctrl_count++;
    cap->ctrl_ids[idx] = msg->id;
    size_t copy = msg->parameters_len < IAP2_SESSION_TX_CAPACITY
        ? msg->parameters_len : IAP2_SESSION_TX_CAPACITY;
    if (copy > 0U) {
        memcpy(cap->ctrl_payloads[idx], msg->parameters, copy);
    }
    cap->ctrl_lens[idx] = msg->parameters_len;
}

static void drain_device_to_host(struct iap2_link *link,
                                 struct iap2_session *session,
                                 struct iap2_control_stream *host_ctrl,
                                 struct host_capture *cap,
                                 uint32_t *now_ms,
                                 uint8_t *host_seq)
{
    for (unsigned iter = 0; iter < 32U; iter++) {
        iap2_session_tick(session, *now_ms);
        iap2_link_tick(link, *now_ms);

        const uint8_t *frame = NULL;
        size_t flen = 0U;
        if (iap2_link_peek_tx(link, &frame, &flen) != IAP2_STATUS_OK || flen == 0U) {
            break;
        }
        assert(iap2_link_mark_submitted(link) == IAP2_STATUS_OK);

        if (flen > IAP2_PROBE_LEN && frame[0] == IAP2_SOP1 && frame[1] == IAP2_SOP2) {
            struct iap2_packet_view view;
            assert(iap2_decode_packet(frame, flen, &view) == IAP2_STATUS_OK);

            if ((view.header.control & (IAP2_CTL_SYN | IAP2_CTL_ACK)) ==
                (IAP2_CTL_SYN | IAP2_CTL_ACK)) {
                /* Host ACKs SYN-ACK */
                uint8_t ack_buf[32];
                size_t ack_len = 0U;
                assert(iap2_encode_packet(ack_buf, sizeof(ack_buf), IAP2_CTL_ACK,
                                          *host_seq, view.header.seq, 0U,
                                          NULL, 0U, &ack_len) == IAP2_STATUS_OK);
                assert(iap2_link_finish_tx(link, true) == IAP2_STATUS_OK);
                struct iap2_link_rx_result rx;
                assert(iap2_link_receive_packet(link, ack_buf, ack_len, &rx) == IAP2_STATUS_OK);
                continue;
            }

            if ((view.header.control & IAP2_CTL_ACK) != 0U && view.payload_len > 0U) {
                if (view.header.session == link->control_session) {
                    assert(iap2_control_feed(host_ctrl, view.payload, view.payload_len,
                                             on_host_ctrl_msg, cap) == IAP2_STATUS_OK);
                } else if (view.header.session == link->file_session) {
                    assert(cap->file_count < 32U);
                    size_t cpy = view.payload_len < IAP2_SESSION_TX_CAPACITY
                        ? view.payload_len : IAP2_SESSION_TX_CAPACITY;
                    memcpy(cap->file_pkts[cap->file_count], view.payload, cpy);
                    cap->file_lens[cap->file_count] = view.payload_len;
                    cap->file_count++;
                }

                /* Finish TX and send host ACK for this data packet */
                assert(iap2_link_finish_tx(link, true) == IAP2_STATUS_OK);
                uint8_t ack_buf[32];
                size_t ack_len = 0U;
                assert(iap2_encode_packet(ack_buf, sizeof(ack_buf), IAP2_CTL_ACK,
                                          *host_seq, view.header.seq, 0U,
                                          NULL, 0U, &ack_len) == IAP2_STATUS_OK);
                struct iap2_link_rx_result rx;
                assert(iap2_link_receive_packet(link, ack_buf, ack_len, &rx) == IAP2_STATUS_OK);
                continue;
            }
        }

        assert(iap2_link_finish_tx(link, true) == IAP2_STATUS_OK);
    }
}

static void host_send_ctrl(struct iap2_link *link, struct iap2_session *session,
                           const uint8_t *ctrl_msg, size_t ctrl_len,
                           uint8_t *host_seq, uint32_t now_ms)
{
    uint8_t pkt[IAP2_LINK_MAX_RX_PACKET];
    size_t pkt_len = 0U;
    (*host_seq)++;
    assert(iap2_encode_packet(pkt, sizeof(pkt), IAP2_CTL_ACK, *host_seq,
                              link->my_seq, link->control_session,
                              ctrl_msg, ctrl_len, &pkt_len) == IAP2_STATUS_OK);
    struct iap2_link_rx_result rx;
    assert(iap2_link_receive_packet(link, pkt, pkt_len, &rx) == IAP2_STATUS_OK);
    assert(rx.packet_handled && rx.has_payload);
    iap2_session_rx_payload(session, rx.session, rx.payload, rx.payload_len, now_ms);
}

static void host_send_file(struct iap2_link *link, struct iap2_session *session,
                           const uint8_t *file_msg, size_t file_len,
                           uint8_t *host_seq, uint32_t now_ms)
{
    uint8_t pkt[128];
    size_t pkt_len = 0U;
    (*host_seq)++;
    assert(iap2_encode_packet(pkt, sizeof(pkt), IAP2_CTL_ACK, *host_seq,
                              link->my_seq, link->file_session,
                              file_msg, file_len, &pkt_len) == IAP2_STATUS_OK);
    struct iap2_link_rx_result rx;
    assert(iap2_link_receive_packet(link, pkt, pkt_len, &rx) == IAP2_STATUS_OK);
    assert(rx.packet_handled && rx.has_payload);
    iap2_session_rx_payload(session, rx.session, rx.payload, rx.payload_len, now_ms);
}

static int find_msg_index(const struct host_capture *cap, uint16_t id, unsigned start)
{
    for (unsigned i = start; i < cap->ctrl_count; i++) {
        if (cap->ctrl_ids[i] == id) {
            return (int)i;
        }
    }
    return -1;
}

static void test_mib3_full_flow(void)
{
    struct iap2_link link;
    assert(iap2_link_init(&link, IAP2_LINK_MAX_RX_PACKET) == IAP2_STATUS_OK);

    static struct host_capture cap;
    memset(&cap, 0, sizeof(cap));
    struct iap2_session_ops ops = {
        .context = &cap,
        .random32 = test_rng,
        .on_authenticated = on_auth,
        .on_identified = on_ident,
        .on_usb_audio = on_audio,
        .on_hid_action = on_hid,
        .on_seek_ms = on_seek,
        .on_artwork_done = on_artwork_done,
    };
    static struct iap2_session session;
    iap2_session_init(&session, &link, &ops);

    struct iap2_media_state media = {
        .device_name = "iPod7G",
        .battery_pct = 88U,
        .charging = true,
        .sample_rate = 44100U,
        .has_track = true,
        .play_state = IAP2_PLAY_PLAYING,
        .elapsed_ms = 12000U,
        .duration_ms = 215000U,
        .track_number = 3U,
        .queue_index = 2U,
        .queue_count = 10U,
        .shuffle = true,
        .repeat_mode = IAP2_REPEAT_ALL,
        .title = "Night Drive",
        .artist = "Test Artist",
        .album = "Test Album",
        .album_artist = "Album Artist",
    };
    iap2_session_set_media(&session, &media);

    uint8_t host_rx_buf[4096];
    struct iap2_control_stream host_ctrl;
    iap2_control_stream_init(&host_ctrl, host_rx_buf, sizeof(host_rx_buf));

    uint32_t now_ms = 1000U;
    uint8_t host_seq = 0x20U;

    /* 1. Link probe + SYN negotiating control(10) and file(11) sessions */
    assert(iap2_link_handle_probe(&link) == IAP2_STATUS_OK);
    static const uint8_t syn_payload[] = {
        0x01, 0x04, 0x04, 0x00, 0x03, 0xE8, 0x00, 0x14, 0x03, 0x03,
        10U, 0U, 1U,   /* session 10: control */
        11U, 1U, 1U,   /* session 11: file transfer */
    };
    uint8_t syn_pkt[64];
    size_t syn_len = 0U;
    assert(iap2_encode_packet(syn_pkt, sizeof(syn_pkt), IAP2_CTL_SYN, host_seq,
                              0U, 0U, syn_payload, sizeof(syn_payload), &syn_len) == IAP2_STATUS_OK);
    struct iap2_link_rx_result rx;
    assert(iap2_link_receive_packet(&link, syn_pkt, syn_len, &rx) == IAP2_STATUS_OK);

    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(link.link_up);
    assert(link.control_session == 10U && link.file_session == 11U);
    assert(find_msg_index(&cap, 0xAA00U, 0U) >= 0); /* REQUEST_CERTIFICATE */

    /* 2. Certificate (600 bytes -> 32-byte challenge) & ChallengeResponse */
    uint8_t msg_buf[1024];
    size_t msg_len = 0U;
    static uint8_t dummy_cert[600];
    memset(dummy_cert, 0x42, sizeof(dummy_cert));
    assert(iap2_control_start(msg_buf, sizeof(msg_buf), 0xAA01U, &msg_len) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg_buf, sizeof(msg_buf), &msg_len, 0U, dummy_cert, sizeof(dummy_cert)) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg_buf, msg_len, &host_seq, now_ms);
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);

    int ch_idx = find_msg_index(&cap, 0xAA02U, 0U);
    assert(ch_idx >= 0);
    assert(cap.ctrl_lens[ch_idx] == 36U); /* 4-byte param hdr + 32-byte challenge */

    static const uint8_t chal_rsp[4] = {1U, 2U, 3U, 4U};
    assert(iap2_control_start(msg_buf, sizeof(msg_buf), 0xAA03U, &msg_len) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg_buf, sizeof(msg_buf), &msg_len, 0U, chal_rsp, sizeof(chal_rsp)) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg_buf, msg_len, &host_seq, now_ms);

    now_ms += 19U;
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(!cap.auth_seen);

    now_ms += 1U; /* 20ms reached */
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(cap.auth_seen);
    assert(find_msg_index(&cap, 0xAA05U, 0U) >= 0);
    assert(find_msg_index(&cap, 0x1D00U, 0U) >= 0);

    /* 3. IdentificationInformation (param 7 msg list + param 17 BT component) */
    static const uint8_t receives[] = {
        0x4E, 0x0C, 0x4E, 0x0A, 0x4E, 0x09, 0x0B, 0x00, 0x0B, 0x03, 0x43, 0x00
    };
    static const uint8_t bt_grp[] = { 0x00, 0x06, 0x00, 0x00, 0x00, 0x07 };
    assert(iap2_control_start(msg_buf, sizeof(msg_buf), 0x1D01U, &msg_len) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg_buf, sizeof(msg_buf), &msg_len, 7U, receives, sizeof(receives)) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg_buf, sizeof(msg_buf), &msg_len, 17U, bt_grp, sizeof(bt_grp)) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg_buf, msg_len, &host_seq, now_ms);

    now_ms += 30U;
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(cap.ident_seen);
    assert(find_msg_index(&cap, 0x1D02U, 0U) >= 0);
    assert(find_msg_index(&cap, 0x4E0CU, 0U) >= 0);
    assert(find_msg_index(&cap, 0x4E0AU, 0U) >= 0);
    assert(find_msg_index(&cap, 0x4E09U, 0U) >= 0);
    assert(find_msg_index(&cap, 0x0B00U, 0U) >= 0);
    assert(find_msg_index(&cap, 0x0B03U, 0U) >= 0);

    /* 4. PowerUpdates + PowerSourceUpdate + StartHID + CarPlayAvailability */
    unsigned base_idx = cap.ctrl_count;
    assert(iap2_control_start(msg_buf, sizeof(msg_buf), 0xAE00U, &msg_len) == IAP2_STATUS_OK);
    for (uint16_t attr = 0U; attr <= 6U; attr++) {
        if (attr == 2U || attr == 3U) continue;
        assert(iap2_control_append(msg_buf, sizeof(msg_buf), &msg_len, attr, NULL, 0U) == IAP2_STATUS_OK);
    }
    host_send_ctrl(&link, &session, msg_buf, msg_len, &host_seq, now_ms);
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    unsigned pwr_seen = 0U;
    for (unsigned i = base_idx; i < cap.ctrl_count; i++) {
        if (cap.ctrl_ids[i] == 0xAE01U) pwr_seen++;
    }
    assert(pwr_seen == 5U); /* All 5 attributes sent without dropping any slot */

    static const uint8_t cur_3000[2] = { 0x0B, 0xB8 };
    assert(iap2_control_start(msg_buf, sizeof(msg_buf), 0xAE03U, &msg_len) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg_buf, sizeof(msg_buf), &msg_len, 0U, cur_3000, 2U) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg_buf, msg_len, &host_seq, now_ms);

    /* Consumer Page descriptor: 1-bit flags PlayPause(0xCD), Next(0xB5), Prev(0xB6) */
    static const uint8_t hid_desc[] = {
        0x05, 0x0C, /* Usage Page (Consumer) */
        0x09, 0x01, /* Usage (Consumer Control) */
        0xA1, 0x01, /* Collection (Application) */
        0x15, 0x00, /* Logical Min (0) */
        0x25, 0x01, /* Logical Max (1) */
        0x75, 0x01, /* Report Size (1) */
        0x95, 0x03, /* Report Count (3) */
        0x09, 0xCD, /* Usage (Play/Pause) */
        0x09, 0xB5, /* Usage (Scan Next) */
        0x09, 0xB6, /* Usage (Scan Previous) */
        0x81, 0x02, /* Input (Data, Var, Abs) */
        0xC0        /* End Collection */
    };
    static const uint8_t comp0[2] = { 0x00, 0x00 };
    assert(iap2_control_start(msg_buf, sizeof(msg_buf), 0x6800U, &msg_len) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg_buf, sizeof(msg_buf), &msg_len, 0U, comp0, 2U) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg_buf, sizeof(msg_buf), &msg_len, 4U, hid_desc, sizeof(hid_desc)) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg_buf, msg_len, &host_seq, now_ms);
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(find_msg_index(&cap, 0x4300U, base_idx) >= 0); /* CarPlayAvailability flushed on StartHID */

    /* 5. USB Audio start, rate switch to 48k, stop */
    base_idx = cap.ctrl_count;
    assert(iap2_control_start(msg_buf, sizeof(msg_buf), 0xDA00U, &msg_len) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg_buf, msg_len, &host_seq, now_ms);
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(cap.audio_active && cap.audio_rate == 44100U);
    int r_idx = find_msg_index(&cap, 0xDA01U, base_idx);
    assert(r_idx >= 0 && cap.ctrl_payloads[r_idx][4] == 7U); /* index 7 = 44100 */

    media.sample_rate = 48000U;
    iap2_session_set_media(&session, &media);
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    r_idx = find_msg_index(&cap, 0xDA01U, (unsigned)r_idx + 1U);
    assert(r_idx >= 0 && cap.ctrl_payloads[r_idx][4] == 8U); /* index 8 = 48000 */
    assert(cap.audio_active && cap.audio_rate == 48000U);
    assert(cap.audio_calls >= 2U);

    /* 6. Now Playing + Empty Artwork file transfer + 500ms position update */
    base_idx = cap.ctrl_count;
    static const uint8_t np_media_grp[] = {
        0x00, 0x04, 0x00, 0x00, /* Persistent ID */
        0x00, 0x04, 0x00, 0x01, /* Title */
        0x00, 0x04, 0x00, 0x04, /* Duration */
        0x00, 0x04, 0x00, 0x1A, /* Artwork ID */
    };
    static const uint8_t np_pb_grp[] = {
        0x00, 0x04, 0x00, 0x00, /* State */
        0x00, 0x04, 0x00, 0x01, /* Position */
    };
    assert(iap2_control_start(msg_buf, sizeof(msg_buf), 0x5000U, &msg_len) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg_buf, sizeof(msg_buf), &msg_len, 0U, np_media_grp, sizeof(np_media_grp)) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg_buf, sizeof(msg_buf), &msg_len, 1U, np_pb_grp, sizeof(np_pb_grp)) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg_buf, msg_len, &host_seq, now_ms);
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);

    assert(find_msg_index(&cap, 0x5001U, base_idx) >= 0);
    assert(cap.file_count == 1U && cap.file_pkts[0][0] == 0x80U && cap.file_pkts[0][1] == 0x04U);

    static const uint8_t file_start_req[2] = { 0x80U, 0x01U };
    host_send_file(&link, &session, file_start_req, 2U, &host_seq, now_ms);
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(cap.file_count == 2U && cap.file_pkts[1][0] == 0x80U && cap.file_pkts[1][1] == 0xC0U);

    base_idx = cap.ctrl_count;
    now_ms += 500U;
    media.elapsed_ms = 12500U;
    iap2_session_set_media(&session, &media);
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(find_msg_index(&cap, 0x5001U, base_idx) >= 0);

    /* 7. HID report edge triggering & SetNowPlayingInformation seek */
    uint8_t rpt_next[1] = { 0x02U }; /* bit 1 = Next */
    assert(iap2_control_start(msg_buf, sizeof(msg_buf), 0x6802U, &msg_len) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg_buf, sizeof(msg_buf), &msg_len, 1U, rpt_next, 1U) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg_buf, msg_len, &host_seq, now_ms);
    /* Re-sending held button must not fire twice */
    host_send_ctrl(&link, &session, msg_buf, msg_len, &host_seq, now_ms);
    assert(cap.action_count == 1U && cap.actions[0] == IAP2_REMOTE_NEXT);

    uint8_t rpt_play[1] = { 0x01U }; /* release Next, press PlayPause */
    assert(iap2_control_start(msg_buf, sizeof(msg_buf), 0x6802U, &msg_len) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg_buf, sizeof(msg_buf), &msg_len, 1U, rpt_play, 1U) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg_buf, msg_len, &host_seq, now_ms);
    assert(cap.action_count == 2U && cap.actions[1] == IAP2_REMOTE_PLAY_PAUSE);

    static const uint8_t seek_45s[4] = { 0x00, 0x00, 0xAF, 0xC8 };
    assert(iap2_control_start(msg_buf, sizeof(msg_buf), 0x5003U, &msg_len) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg_buf, sizeof(msg_buf), &msg_len, 0U, seek_45s, 4U) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg_buf, msg_len, &host_seq, now_ms);
    assert(cap.seek_count == 1U && cap.last_seek_ms == 45000U);

    /* 8. Stop USB Audio + Session reset clears audio cleanly */
    assert(iap2_control_start(msg_buf, sizeof(msg_buf), 0xDA02U, &msg_len) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg_buf, msg_len, &host_seq, now_ms);
    assert(!cap.audio_active);
    (void)rd_be16;
}

static void setup_identified_session(struct iap2_link *link,
                                     struct iap2_session *session,
                                     struct iap2_control_stream *host_ctrl,
                                     uint8_t *host_rx_buf,
                                     size_t host_rx_cap,
                                     struct host_capture *cap,
                                     uint32_t *now_ms,
                                     uint8_t *host_seq)
{
    assert(iap2_link_init(link, IAP2_LINK_MAX_RX_PACKET) == IAP2_STATUS_OK);
    memset(cap, 0, sizeof(*cap));
    struct iap2_session_ops ops = {
        .context = cap,
        .random32 = test_rng,
        .on_authenticated = on_auth,
        .on_identified = on_ident,
        .on_usb_audio = on_audio,
        .on_hid_action = on_hid,
        .on_seek_ms = on_seek,
        .on_artwork_done = on_artwork_done,
    };
    iap2_session_init(session, link, &ops);
    iap2_control_stream_init(host_ctrl, host_rx_buf, host_rx_cap);

    assert(iap2_link_handle_probe(link) == IAP2_STATUS_OK);
    static const uint8_t syn_payload[] = {
        0x01, 0x04, 0x04, 0x00, 0x03, 0xE8, 0x00, 0x14, 0x03, 0x03,
        10U, 0U, 1U,
        11U, 1U, 1U,
    };
    uint8_t pkt[128];
    size_t plen = 0U;
    assert(iap2_encode_packet(pkt, sizeof(pkt), IAP2_CTL_SYN, *host_seq,
                              0U, 0U, syn_payload, sizeof(syn_payload), &plen) == IAP2_STATUS_OK);
    struct iap2_link_rx_result rx;
    assert(iap2_link_receive_packet(link, pkt, plen, &rx) == IAP2_STATUS_OK);
    drain_device_to_host(link, session, host_ctrl, cap, now_ms, host_seq);

    uint8_t msg[256];
    size_t mlen = 0U;
    static const uint8_t cert[16] = { 1U };
    assert(iap2_control_start(msg, sizeof(msg), 0xAA01U, &mlen) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg, sizeof(msg), &mlen, 0U, cert, sizeof(cert)) == IAP2_STATUS_OK);
    host_send_ctrl(link, session, msg, mlen, host_seq, *now_ms);
    static const uint8_t chal[4] = { 9U };
    assert(iap2_control_start(msg, sizeof(msg), 0xAA03U, &mlen) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg, sizeof(msg), &mlen, 0U, chal, sizeof(chal)) == IAP2_STATUS_OK);
    host_send_ctrl(link, session, msg, mlen, host_seq, *now_ms);
    *now_ms += 25U;
    drain_device_to_host(link, session, host_ctrl, cap, now_ms, host_seq);

    assert(iap2_control_start(msg, sizeof(msg), 0x1D01U, &mlen) == IAP2_STATUS_OK);
    host_send_ctrl(link, session, msg, mlen, host_seq, *now_ms);
    *now_ms += 35U;
    drain_device_to_host(link, session, host_ctrl, cap, now_ms, host_seq);
    assert(cap->ident_seen);
}

static void test_artwork_transfers(void)
{
    struct iap2_link link;
    static struct iap2_session session;
    uint8_t host_rx_buf[4096];
    struct iap2_control_stream host_ctrl;
    static struct host_capture cap;
    uint32_t now_ms = 5000U;
    uint8_t host_seq = 0x10U;

    setup_identified_session(&link, &session, &host_ctrl, host_rx_buf, sizeof(host_rx_buf),
                             &cap, &now_ms, &host_seq);

    /* Build deterministic 2500-byte JPEG fixture */
    uint8_t jpeg_big[2500];
    jpeg_big[0] = 0xFFU; jpeg_big[1] = 0xD8U;
    for (size_t i = 2U; i < sizeof(jpeg_big) - 2U; i++) {
        jpeg_big[i] = (uint8_t)(i * 13U + 5U);
    }
    jpeg_big[sizeof(jpeg_big) - 2U] = 0xFFU;
    jpeg_big[sizeof(jpeg_big) - 1U] = 0xD9U;

    /* 1. Multi-packet artwork present when track starts */
    struct iap2_media_state media = {
        .has_track = true,
        .track_id = 0x1111ULL,
        .play_state = IAP2_PLAY_PLAYING,
        .duration_ms = 180000U,
        .title = "Track One",
        .artist = "Artist One",
        .album = "Album One",
    };
    iap2_session_set_media(&session, &media);
    iap2_session_set_artwork(&session, 0x1111ULL, jpeg_big, sizeof(jpeg_big));

    static const uint8_t np_media[] = {
        0x00, 0x04, 0x00, 0x00,
        0x00, 0x04, 0x00, 0x01,
        0x00, 0x04, 0x00, 0x1A,
    };
    static const uint8_t np_pb[] = {
        0x00, 0x04, 0x00, 0x00,
        0x00, 0x04, 0x00, 0x01,
        0x00, 0x04, 0x00, 0x07,
        0x00, 0x04, 0x00, 0x10,
    };
    uint8_t msg[256];
    size_t mlen = 0U;
    assert(iap2_control_start(msg, sizeof(msg), 0x5000U, &mlen) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg, sizeof(msg), &mlen, 0U, np_media, sizeof(np_media)) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg, sizeof(msg), &mlen, 1U, np_pb, sizeof(np_pb)) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg, mlen, &host_seq, now_ms);
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);

    /* Setup packet [id, 0x04, 8-byte BE size=2500, 0x00, 0x02] */
    assert(cap.file_count == 1U);
    uint8_t tid = cap.file_pkts[0][0];
    assert(cap.file_pkts[0][1] == 0x04U);
    assert(cap.file_lens[0] == 12U);
    assert(((uint32_t)cap.file_pkts[0][8] << 8 | cap.file_pkts[0][9]) == sizeof(jpeg_big));

    uint8_t start_ack[2] = { tid, 0x01U };
    host_send_file(&link, &session, start_ack, 2U, &host_seq, now_ms);
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);

    /* Expect 3 data packets (0x80, 0x00, 0x40) totaling 2500 bytes */
    assert(cap.file_count == 4U);
    assert(cap.file_pkts[1][0] == tid && cap.file_pkts[1][1] == 0x80U);
    assert(cap.file_pkts[2][0] == tid && cap.file_pkts[2][1] == 0x00U);
    assert(cap.file_pkts[3][0] == tid && cap.file_pkts[3][1] == 0x40U);

    uint8_t reassembled[2500];
    size_t rpos = 0U;
    for (unsigned p = 1U; p <= 3U; p++) {
        size_t chunk = cap.file_lens[p] - 2U;
        assert(rpos + chunk <= sizeof(reassembled));
        memcpy(reassembled + rpos, cap.file_pkts[p] + 2U, chunk);
        rpos += chunk;
    }
    assert(rpos == sizeof(jpeg_big));
    assert(memcmp(reassembled, jpeg_big, sizeof(jpeg_big)) == 0);
    assert(cap.artwork_done_calls == 1U && cap.last_artwork_completed);
    assert(session.summary.artwork_transfers == 1U);
    assert(session.summary.last_artwork_bytes == sizeof(jpeg_big));

    /* 2. Late artwork follow-up after initial blank transfer */
    cap.file_count = 0U;
    unsigned prev_ctrl = cap.ctrl_count;
    media.track_id = 0x2222ULL;
    strcpy(media.title, "Track Two");
    iap2_session_set_media(&session, &media);
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(cap.file_count == 1U && cap.file_pkts[0][1] == 0x04U);
    uint8_t blank_tid = cap.file_pkts[0][0];
    assert(cap.file_pkts[0][8] == 0U && cap.file_pkts[0][9] == 0U);

    /* Now late artwork arrives before car sends 0x01 for blank_tid */
    uint8_t jpeg_small[200];
    jpeg_small[0] = 0xFFU; jpeg_small[1] = 0xD8U;
    memset(jpeg_small + 2, 0x5AU, sizeof(jpeg_small) - 4U);
    jpeg_small[sizeof(jpeg_small) - 2U] = 0xFFU;
    jpeg_small[sizeof(jpeg_small) - 1U] = 0xD9U;
    iap2_session_set_artwork(&session, 0x2222ULL, jpeg_small, sizeof(jpeg_small));
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);

    /* Should emit a follow-up NowPlayingUpdate + new setup with size 200 */
    assert(find_msg_index(&cap, 0x5001U, prev_ctrl + 1U) >= 0);
    assert(cap.file_count == 2U && cap.file_pkts[1][1] == 0x04U);
    uint8_t late_tid = cap.file_pkts[1][0];
    assert(late_tid != blank_tid);
    assert(cap.file_pkts[1][9] == sizeof(jpeg_small));

    /* Car acks old blank_tid AND new late_tid */
    uint8_t ack_blank[2] = { blank_tid, 0x01U };
    host_send_file(&link, &session, ack_blank, 2U, &host_seq, now_ms);
    uint8_t ack_late[2] = { late_tid, 0x01U };
    host_send_file(&link, &session, ack_late, 2U, &host_seq, now_ms);
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(cap.file_count == 4U);
    assert(cap.file_pkts[2][0] == blank_tid && cap.file_pkts[2][1] == 0xC0U && cap.file_lens[2] == 2U);
    assert(cap.file_pkts[3][0] == late_tid && cap.file_pkts[3][1] == 0xC0U);
    assert(cap.file_lens[3] == 2U + sizeof(jpeg_small));
    assert(memcmp(cap.file_pkts[3] + 2U, jpeg_small, sizeof(jpeg_small)) == 0);
    assert(cap.artwork_done_calls == 2U && cap.last_artwork_completed);
    assert(session.summary.artwork_transfers == 2U);

    /* 3. Mid-transfer track change cancels previous setup and notifies on_artwork_done(false) */
    cap.file_count = 0U;
    media.track_id = 0x3333ULL;
    strcpy(media.title, "Track Three");
    iap2_session_set_media(&session, &media);
    iap2_session_set_artwork(&session, 0x3333ULL, jpeg_small, sizeof(jpeg_small));
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(cap.file_count == 1U && cap.file_pkts[0][1] == 0x04U);
    uint8_t tid3 = cap.file_pkts[0][0];

    /* Switch to Track Four while tid3 is WAIT_START */
    media.track_id = 0x4444ULL;
    strcpy(media.title, "Track Four");
    iap2_session_set_media(&session, &media);
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(cap.artwork_done_calls == 3U && !cap.last_artwork_completed && cap.last_artwork_key == 0x3333ULL);
    assert(cap.file_count == 3U);
    assert(cap.file_pkts[1][0] == tid3 && cap.file_pkts[1][1] == 0x02U);
    assert(cap.file_pkts[2][1] == 0x04U);

    /* 4. 5-second WAIT_START timeout cancels cleanly and frees artwork */
    cap.file_count = 0U;
    media.track_id = 0x5555ULL;
    strcpy(media.title, "Track Five");
    iap2_session_set_media(&session, &media);
    iap2_session_set_artwork(&session, 0x5555ULL, jpeg_small, sizeof(jpeg_small));
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(cap.file_count == 2U && cap.file_pkts[0][1] == 0x02U && cap.file_pkts[1][1] == 0x04U);
    uint8_t tid5 = cap.file_pkts[1][0];

    now_ms += 5000U;
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(cap.file_count == 3U && cap.file_pkts[2][0] == tid5 && cap.file_pkts[2][1] == 0x02U);
    assert(cap.artwork_done_calls == 4U && !cap.last_artwork_completed && cap.last_artwork_key == 0x5555ULL);
}

static const uint8_t *host_find_param(const struct host_capture *cap, unsigned msg_idx,
                                      uint16_t id, size_t *out_len)
{
    const uint8_t *cursor = cap->ctrl_payloads[msg_idx];
    size_t rem = cap->ctrl_lens[msg_idx];
    struct iap2_control_param p;
    while (iap2_control_next_param(&cursor, &rem, &p) == IAP2_STATUS_OK) {
        if (p.id == id) {
            if (out_len != NULL) {
                *out_len = p.len;
            }
            return p.data;
        }
    }
    return NULL;
}

static void test_library_streaming_and_playlists(void)
{
    struct iap2_link link;
    static struct iap2_session session;
    uint8_t host_rx_buf[4096];
    struct iap2_control_stream host_ctrl;
    static struct host_capture cap;
    uint32_t now_ms = 8000U;
    uint8_t host_seq = 0x30U;

    setup_identified_session(&link, &session, &host_ctrl, host_rx_buf, sizeof(host_rx_buf),
                             &cap, &now_ms, &host_seq);

    struct iap2_session_ops ops = session.ops;
    ops.on_library_start = on_lib_start;
    ops.on_library_stop = on_lib_stop;
    ops.on_library_batch_done = on_lib_batch_done;
    ops.on_playlist_keys_request = on_pl_req;
    ops.on_playlist_keys_done = on_pl_done;
    session.ops = ops;

    /* Library stays stopped (has_track == false) */
    cap.ctrl_count = 0U;
    cap.file_count = 0U;

    uint8_t msg[256];
    size_t mlen = 0U;
    assert(iap2_control_start(msg, sizeof(msg), 0x4C00U, &mlen) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg, mlen, &host_seq, now_ms);
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(find_msg_index(&cap, 0x4C01U, 0U) >= 0);

    /* Send 0x4C03 requesting items + playlists (param 2 + param 3) */
    cap.ctrl_count = 0U;
    static const uint8_t lib_props[] = {
        0x00, 0x04, 0x00, 0x00, /* PID */
        0x00, 0x04, 0x00, 0x01, /* Title */
        0x00, 0x04, 0x00, 0x04, /* Duration */
        0x00, 0x04, 0x00, 0x06, /* Album */
        0x00, 0x04, 0x00, 0x0C, /* Artist */
        0x00, 0x04, 0x00, 0x10, /* Genre */
    };
    assert(iap2_control_start(msg, sizeof(msg), 0x4C03U, &mlen) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg, sizeof(msg), &mlen, 2U, lib_props, sizeof(lib_props)) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg, sizeof(msg), &mlen, 3U, NULL, 0U) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg, mlen, &host_seq, now_ms);
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);

    assert(cap.lib_start_calls == 1U);
    assert(session.summary.lib_phase == IAP2_LIB_WAIT_BEGIN);
    /* First two 0x4C04s must be flag 8 (0) and flag 9 (1); no empty progress-100 reset! */
    assert(cap.ctrl_count == 2U);
    assert(cap.ctrl_ids[0] == 0x4C04U && cap.ctrl_ids[1] == 0x4C04U);
    size_t plen = 0U;
    const uint8_t *p8 = host_find_param(&cap, 0U, 8U, &plen);
    const uint8_t *p9 = host_find_param(&cap, 1U, 9U, &plen);
    assert(p8 != NULL && p8[0] == 0U);
    assert(p9 != NULL && p9[0] == 1U);

    /* Supply playlists BEFORE library_begin, then begin track streaming */
    static const struct iap2_library_playlist pls[2] = {
        { .persistent_id = 0xA1B2C3D4E5F60001ULL, .track_count = 2U, .is_folder = 0U, .name = "Rock Mix" },
        { .persistent_id = 0xA1B2C3D4E5F60002ULL, .track_count = 0U, .is_folder = 0U, .name = "Empty List" },
    };
    iap2_session_library_set_playlists(&session, pls, 2U);
    iap2_session_library_begin(&session, 12U, "deadbeef");

    static struct iap2_library_item batch1[10];
    static struct iap2_library_item batch2[2];
    for (size_t i = 0U; i < 10U; i++) {
        memset(&batch1[i], 0, sizeof(batch1[i]));
        batch1[i].persistent_id = 0x1000ULL + i;
        batch1[i].duration_ms = 180000U + (uint32_t)i * 1000U;
        batch1[i].track_number = (uint16_t)(i + 1U);
        snprintf(batch1[i].title, sizeof(batch1[i].title),
                 "A Long Test Track Title For Packet Splitting #%u", (unsigned)(i + 1U));
        strcpy(batch1[i].artist, "Coldplay");
        strcpy(batch1[i].album, "X&Y");
        strcpy(batch1[i].genre, "Alternative Rock");
    }
    for (size_t i = 0U; i < 2U; i++) {
        memset(&batch2[i], 0, sizeof(batch2[i]));
        batch2[i].persistent_id = 0x2000ULL + i;
        batch2[i].duration_ms = 200000U;
        snprintf(batch2[i].title, sizeof(batch2[i].title), "Tail Track #%u", (unsigned)(i + 1U));
        strcpy(batch2[i].artist, "Dido");
        strcpy(batch2[i].album, "No Angel");
    }

    cap.ctrl_count = 0U;
    assert(iap2_session_library_supply_batch(&session, 1U, batch1, 10U, false));
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(cap.lib_batch_done_calls == 1U && cap.last_lib_batch_done_id == 1U);
    assert(cap.ctrl_count >= 2U); /* Must split 10 items across >=2 0x4C04 packets */
    assert(host_find_param(&cap, 0U, 6U, &plen) != NULL); /* reset on first item packet */
    assert(host_find_param(&cap, 1U, 6U, &plen) == NULL); /* no reset on second packet */
    const uint8_t *r0 = host_find_param(&cap, 0U, 1U, &plen);
    const uint8_t *pr0 = host_find_param(&cap, 0U, 7U, &plen);
    assert(r0 != NULL && strcmp((const char *)r0, "0") == 0);
    assert(pr0 != NULL && pr0[0] == 0U);
    const uint8_t *r1 = host_find_param(&cap, 1U, 1U, &plen);
    const uint8_t *pr1 = host_find_param(&cap, 1U, 7U, &plen);
    assert(r1 != NULL && strcmp((const char *)r1, "0") == 0);
    assert(pr1 != NULL && pr1[0] == 0U);

    unsigned before_b2 = cap.ctrl_count;
    assert(iap2_session_library_supply_batch(&session, 2U, batch2, 2U, true));
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(cap.lib_batch_done_calls == 2U && cap.last_lib_batch_done_id == 2U);
    assert(session.summary.lib_tracks_sent == 12U);
    assert(session.summary.lib_tracks_total == 12U);

    /* Find final track update ("deadbeef" + progress 100) and playlist update ("deadbeef.p2") */
    bool saw_final_tracks = false;
    bool saw_pl_records = false;
    uint8_t rock_tid = 0U;
    for (unsigned i = before_b2; i < cap.ctrl_count; i++) {
        assert(cap.ctrl_ids[i] == 0x4C04U);
        const uint8_t *rv = host_find_param(&cap, i, 1U, &plen);
        const uint8_t *pg = host_find_param(&cap, i, 7U, &plen);
        if (rv != NULL && strcmp((const char *)rv, "deadbeef") == 0 && pg != NULL && pg[0] == 100U) {
            saw_final_tracks = true;
        }
        if (rv != NULL && strcmp((const char *)rv, "deadbeef.p2") == 0 && pg != NULL && pg[0] == 100U) {
            saw_pl_records = true;
            /* Inspect first param 3 ("Rock Mix") for transfer ID in subparam 5 */
            const uint8_t *cursor = cap.ctrl_payloads[i];
            size_t rem = cap.ctrl_lens[i];
            struct iap2_control_param p;
            unsigned pl_seen = 0U;
            while (iap2_control_next_param(&cursor, &rem, &p) == IAP2_STATUS_OK) {
                if (p.id == 3U) {
                    const uint8_t *gc = p.data;
                    size_t gr = p.len;
                    struct iap2_control_param sub;
                    uint8_t sub_tid = 0U;
                    while (iap2_control_next_param(&gc, &gr, &sub) == IAP2_STATUS_OK) {
                        if (sub.id == 5U && sub.len == 1U) {
                            sub_tid = sub.data[0];
                        }
                    }
                    if (pl_seen == 0U) {
                        assert(sub_tid >= 0x80U);
                        rock_tid = sub_tid;
                    } else {
                        assert(sub_tid == 0U); /* Empty playlist omits param 5 */
                    }
                    pl_seen++;
                }
            }
            assert(pl_seen == 2U);
        }
    }
    assert(saw_final_tracks && saw_pl_records);
    assert(cap.pl_req_calls == 1U && cap.last_pl_req_idx == 0U);
    assert(cap.last_pl_req_id == 0xA1B2C3D4E5F60001ULL);

    /* Supply keys for playlist 0 and complete File Transfer 0x00 0x04 */
    static const uint64_t rock_keys[2] = { 0x1000ULL, 0x2001ULL };
    cap.file_count = 0U;
    assert(iap2_session_library_supply_playlist_keys(&session, 0U, rock_keys, 2U));
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(cap.file_count == 1U);
    assert(cap.file_pkts[0][0] == rock_tid && cap.file_pkts[0][1] == 0x04U);
    assert(cap.file_pkts[0][9] == 16U); /* 2 keys * 8 bytes */
    assert(cap.file_pkts[0][10] == 0x00U && cap.file_pkts[0][11] == 0x04U);
    assert(rd_be64(cap.file_pkts[0] + 12U) == 0xA1B2C3D4E5F60001ULL);

    uint8_t ack[2] = { rock_tid, 0x01U };
    host_send_file(&link, &session, ack, 2U, &host_seq, now_ms);
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(cap.file_count == 2U);
    assert(cap.file_pkts[1][0] == rock_tid && cap.file_pkts[1][1] == 0xC0U);
    assert(cap.file_lens[1] == 18U);
    assert(rd_be64(cap.file_pkts[1] + 2U) == 0x1000ULL);
    assert(rd_be64(cap.file_pkts[1] + 10U) == 0x2001ULL);
    assert(cap.pl_done_calls == 1U && cap.last_pl_done_idx == 0U && cap.last_pl_done_ok);
    assert(session.summary.lib_phase == IAP2_LIB_DONE);
    assert(session.summary.lib_playlists_sent == 2U);

    /* Cached-revision match skips track streaming and jumps to playlists */
    cap.ctrl_count = 0U;
    cap.file_count = 0U;
    static const char cached_rev[] = "deadbeef.p2";
    assert(iap2_control_start(msg, sizeof(msg), 0x4C03U, &mlen) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg, sizeof(msg), &mlen, 1U, (const uint8_t *)cached_rev, sizeof(cached_rev)) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg, sizeof(msg), &mlen, 3U, NULL, 0U) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg, mlen, &host_seq, now_ms);
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(strcmp(cap.lib_start_rev, "deadbeef.p2") == 0);
    cap.ctrl_count = 0U;
    iap2_session_library_set_playlists(&session, &pls[1], 1U);
    iap2_session_library_begin(&session, 6U, "deadbeef");
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(cap.ctrl_count == 2U); /* rev done + 1 empty-list playlist record */
    assert(host_find_param(&cap, 0U, 6U, &plen) == NULL);
    assert(session.summary.lib_phase == IAP2_LIB_DONE);
}

static void test_queue_and_play_commands_and_reset(void)
{
    struct iap2_link link;
    static struct iap2_session session;
    uint8_t host_rx_buf[4096];
    struct iap2_control_stream host_ctrl;
    static struct host_capture cap;
    uint32_t now_ms = 9000U;
    uint8_t host_seq = 0x40U;

    setup_identified_session(&link, &session, &host_ctrl, host_rx_buf, sizeof(host_rx_buf),
                             &cap, &now_ms, &host_seq);

    struct iap2_session_ops ops = session.ops;
    ops.on_library_start = on_lib_start;
    ops.on_library_stop = on_lib_stop;
    ops.on_library_batch_done = on_lib_batch_done;
    ops.on_playlist_keys_request = on_pl_req;
    ops.on_playlist_keys_done = on_pl_done;
    ops.on_queue_keys_done = on_q_done;
    ops.on_play_library_items = on_play_items;
    ops.on_play_library_collection = on_play_coll;
    ops.on_play_library_special = on_play_special;
    session.ops = ops;

    struct iap2_media_state media = {
        .has_track = true,
        .track_id = 0x1111ULL,
        .play_state = IAP2_PLAY_PLAYING,
        .duration_ms = 180000U,
        .title = "Song",
        .artist = "Artist",
    };
    iap2_session_set_media(&session, &media);

    /* StartNowPlaying requesting PB_QUEUE_LIST (0x0E) */
    static const uint8_t np_m[] = { 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01 };
    static const uint8_t np_p[] = {
        0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x00, 0x04, 0x00, 0x0E
    };
    uint8_t msg[256];
    size_t mlen = 0U;
    assert(iap2_control_start(msg, sizeof(msg), 0x5000U, &mlen) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg, sizeof(msg), &mlen, 0U, np_m, sizeof(np_m)) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg, sizeof(msg), &mlen, 1U, np_p, sizeof(np_p)) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg, mlen, &host_seq, now_ms);
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);

    /* Queue list transfer (0x00 0x03) */
    cap.ctrl_count = 0U;
    cap.file_count = 0U;
    static const uint64_t qkeys[3] = { 0x11ULL, 0x22ULL, 0x33ULL };
    assert(iap2_session_set_queue(&session, 1U, qkeys, 3U, false));
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(cap.ctrl_count == 1U && cap.ctrl_ids[0] == 0x5001U);
    assert(cap.file_count == 1U && cap.file_pkts[0][1] == 0x04U);
    uint8_t qtid = cap.file_pkts[0][0];
    assert(cap.file_pkts[0][10] == 0x00U && cap.file_pkts[0][11] == 0x03U);
    uint8_t qack[2] = { qtid, 0x01U };
    host_send_file(&link, &session, qack, 2U, &host_seq, now_ms);
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(cap.file_count == 2U && cap.file_pkts[1][0] == qtid && cap.file_pkts[1][1] == 0xC0U);
    assert(cap.file_lens[1] == 26U);
    assert(rd_be64(cap.file_pkts[1] + 2U) == 0x11ULL);
    assert(rd_be64(cap.file_pkts[1] + 18U) == 0x33ULL);
    assert(cap.q_done_calls == 1U && cap.last_q_done_ok);

    /* from_car queue notifies without file transfer */
    cap.ctrl_count = 0U;
    cap.file_count = 0U;
    assert(iap2_session_set_queue(&session, 2U, NULL, 0U, true));
    drain_device_to_host(&link, &session, &host_ctrl, &cap, &now_ms, &host_seq);
    assert(cap.ctrl_count == 1U && cap.ctrl_ids[0] == 0x5001U);
    assert(cap.file_count == 0U);
    assert(cap.q_done_calls == 1U);

    /* Car Play commands 0x4C06, 0x4C07, 0x4C08, 0x4C09 */
    assert(iap2_control_start(msg, sizeof(msg), 0x4C06U, &mlen) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg, mlen, &host_seq, now_ms);
    assert(cap.play_special_calls == 1U && cap.play_special_type == 0U);

    static const uint8_t item_ids[16] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x02,
    };
    static const uint8_t start1[4] = { 0x00, 0x00, 0x00, 0x01 };
    assert(iap2_control_start(msg, sizeof(msg), 0x4C07U, &mlen) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg, sizeof(msg), &mlen, 0U, item_ids, sizeof(item_ids)) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg, sizeof(msg), &mlen, 1U, start1, 4U) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg, mlen, &host_seq, now_ms);
    assert(cap.play_items_calls == 1U && cap.play_items_cnt == 2U && cap.play_items_start == 1U);
    assert(cap.play_items[0] == 0x1001ULL && cap.play_items[1] == 0x1002ULL);

    static const uint8_t coll_id[8] = { 0x00, 0x00, 0x00, 0x00, 0xDE, 0xAD, 0xBE, 0xEF };
    static const uint8_t coll_type2[1] = { 2U };
    static const uint8_t start0[4] = { 0x00, 0x00, 0x00, 0x00 };
    assert(iap2_control_start(msg, sizeof(msg), 0x4C08U, &mlen) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg, sizeof(msg), &mlen, 0U, coll_id, 8U) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg, sizeof(msg), &mlen, 1U, coll_type2, 1U) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg, sizeof(msg), &mlen, 2U, start0, 4U) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg, mlen, &host_seq, now_ms);
    assert(cap.play_coll_calls == 1U && cap.play_coll_id == 0xDEADBEEFULL && cap.play_coll_type == 2U);

    static const uint8_t stype1[1] = { 1U };
    static const uint8_t start_id[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55 };
    assert(iap2_control_start(msg, sizeof(msg), 0x4C09U, &mlen) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg, sizeof(msg), &mlen, 1U, stype1, 1U) == IAP2_STATUS_OK);
    assert(iap2_control_append(msg, sizeof(msg), &mlen, 2U, start_id, 8U) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg, mlen, &host_seq, now_ms);
    assert(cap.play_special_calls == 2U && cap.play_special_type == 1U);
    assert(cap.play_special_start_id == 0x55ULL);
    assert(session.summary.play_cmd_count == 4U);
    assert(session.summary.last_play_cmd_id == 0x4C09U);
    assert(session.summary.rx_head_count == 8U);
    assert(session.summary.rx_tail_count == 4U);
    assert(session.summary.rx_tail_ids[0] == 0x4C06U);
    assert(session.summary.rx_tail_ids[3] == 0x4C09U);

    /* Reset releases in-flight borrowed batch & queue buffers cleanly */
    assert(iap2_control_start(msg, sizeof(msg), 0x4C03U, &mlen) == IAP2_STATUS_OK);
    host_send_ctrl(&link, &session, msg, mlen, &host_seq, now_ms);
    iap2_session_library_begin(&session, 2U, "r1");
    static struct iap2_library_item b_hold[2] = {
        { .persistent_id = 1ULL, .title = "A" },
        { .persistent_id = 2ULL, .title = "B" },
    };
    assert(iap2_session_library_supply_batch(&session, 99U, b_hold, 2U, true));
    assert(iap2_session_set_queue(&session, 3U, qkeys, 3U, false));
    unsigned prev_b = cap.lib_batch_done_calls;
    unsigned prev_q = cap.q_done_calls;
    unsigned prev_s = cap.lib_stop_calls;
    iap2_session_reset(&session);
    assert(cap.lib_batch_done_calls == prev_b + 1U && cap.last_lib_batch_done_id == 99U);
    assert(cap.q_done_calls == prev_q + 1U && !cap.last_q_done_ok);
    assert(cap.lib_stop_calls == prev_s + 1U);
}

int main(void)
{
    test_mib3_full_flow();
    test_artwork_transfers();
    test_library_streaming_and_playlists();
    test_queue_and_play_commands_and_reset();
    printf("iAP2 session flow tests passed\n");
    return 0;
}
