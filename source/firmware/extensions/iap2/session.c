// SPDX-License-Identifier: GPL-3.0-only
/*
 * Adapted from PodBox firmware/usbstack/usb_iap2_control.c
 * (commit 7a89c33d0ed49c93ba49a8198e822b8adb72d0a5).
 * PodBox is GPL-2.0-or-later; this version is GPL-3.0-only.
 * Modified by the RevolaOS authors, 2026.
 */

#include "session.h"

#define IAP2_MSG_REQUEST_CERTIFICATE        0xAA00U
#define IAP2_MSG_CERTIFICATE                0xAA01U
#define IAP2_MSG_REQUEST_CHALLENGE_RESPONSE 0xAA02U
#define IAP2_MSG_CHALLENGE_RESPONSE         0xAA03U
#define IAP2_MSG_AUTHENTICATION_SUCCEEDED   0xAA05U

#define IAP2_MSG_START_IDENTIFICATION       0x1D00U
#define IAP2_MSG_IDENTIFICATION_INFORMATION 0x1D01U
#define IAP2_MSG_IDENTIFICATION_ACCEPTED    0x1D02U

#define IAP2_MSG_START_BLUETOOTH_PAIRING    0x0B00U
#define IAP2_MSG_STOP_BLUETOOTH_PAIRING     0x0B03U
#define IAP2_MSG_START_BT_CONNECTION        0x4E03U
#define IAP2_MSG_BT_CONNECTION_UPDATE       0x4E04U

#define IAP2_MSG_START_LIBRARY_INFORMATION  0x4C00U
#define IAP2_MSG_LIBRARY_INFORMATION        0x4C01U
#define IAP2_MSG_START_LIBRARY_UPDATES      0x4C03U
#define IAP2_MSG_LIBRARY_UPDATE             0x4C04U
#define IAP2_MSG_STOP_LIBRARY_UPDATES       0x4C05U
#define IAP2_MSG_PLAY_LIBRARY_CURRENT       0x4C06U
#define IAP2_MSG_PLAY_LIBRARY_ITEMS         0x4C07U
#define IAP2_MSG_PLAY_LIBRARY_COLLECTION    0x4C08U
#define IAP2_MSG_PLAY_LIBRARY_SPECIAL       0x4C09U

#define IAP2_MSG_DEVICE_INFORMATION_UPDATE  0x4E09U
#define IAP2_MSG_DEVICE_LANGUAGE_UPDATE     0x4E0AU
#define IAP2_MSG_DEVICE_UUID_UPDATE         0x4E0CU
#define IAP2_MSG_CARPLAY_AVAILABILITY       0x4300U

#define IAP2_MSG_START_POWER_UPDATES        0xAE00U
#define IAP2_MSG_POWER_UPDATE               0xAE01U
#define IAP2_MSG_POWER_SOURCE_UPDATE        0xAE03U

#define IAP2_MSG_START_NOW_PLAYING          0x5000U
#define IAP2_MSG_NOW_PLAYING_UPDATE         0x5001U
#define IAP2_MSG_STOP_NOW_PLAYING           0x5002U
#define IAP2_MSG_SET_NOW_PLAYING            0x5003U

#define IAP2_MSG_START_HID                  0x6800U
#define IAP2_MSG_ACCESSORY_HID_REPORT       0x6802U
#define IAP2_MSG_STOP_HID                   0x6803U

#define IAP2_MSG_START_USB_AUDIO            0xDA00U
#define IAP2_MSG_USB_AUDIO_INFORMATION      0xDA01U
#define IAP2_MSG_STOP_USB_AUDIO             0xDA02U

#define IAP2_MI_PERSISTENT_ID  0x00U
#define IAP2_MI_TITLE          0x01U
#define IAP2_MI_MEDIA_TYPE     0x02U
#define IAP2_MI_DURATION       0x04U
#define IAP2_MI_ALBUM          0x06U
#define IAP2_MI_TRACK_NUMBER   0x07U
#define IAP2_MI_DISC_NUMBER    0x09U
#define IAP2_MI_ARTIST         0x0CU
#define IAP2_MI_ALBUM_ARTIST   0x0EU
#define IAP2_MI_GENRE          0x10U
#define IAP2_MI_COMPOSER       0x12U
#define IAP2_MI_COMPILATION    0x13U
#define IAP2_MI_LIKE_SUPPORTED 0x15U
#define IAP2_MI_BANNED         0x18U
#define IAP2_MI_ARTWORK        0x1AU
#define IAP2_MI_CHAPTER_COUNT  0x1BU

#define IAP2_PB_STATE          0x00U
#define IAP2_PB_POSITION       0x01U
#define IAP2_PB_QUEUE_INDEX    0x02U
#define IAP2_PB_QUEUE_COUNT    0x03U
#define IAP2_PB_CHAPTER_INDEX  0x04U
#define IAP2_PB_SHUFFLE        0x05U
#define IAP2_PB_REPEAT         0x06U
#define IAP2_PB_APP_NAME       0x07U
#define IAP2_PB_LIBRARY        0x08U
#define IAP2_PB_SPEED          0x0CU
#define IAP2_PB_SEEKABLE       0x0DU
#define IAP2_PB_QUEUE_LIST     0x0EU
#define IAP2_PB_APP_BUNDLE     0x10U
#define IAP2_PB_QUEUE_LIST_ID  0x11U

#define IAP2_DEFAULT_MEDIA_MASK \
    ((1U << IAP2_MI_PERSISTENT_ID) | (1U << IAP2_MI_TITLE) | \
     (1U << IAP2_MI_DURATION) | (1U << IAP2_MI_ALBUM) | (1U << IAP2_MI_ARTIST))

#define IAP2_DEFAULT_PLAYBACK_MASK \
    ((1U << IAP2_PB_STATE) | (1U << IAP2_PB_POSITION) | \
     (1U << IAP2_PB_QUEUE_INDEX) | (1U << IAP2_PB_QUEUE_COUNT))

#define IAP2_DEVICE_UUID     "50F0D0B0-6E2B-4F8A-9C1D-0000000006C0"
#define IAP2_LIBRARY_ID      "50F0D0B0-6E2B-4F8A-9C1D-0000000006C1-MPN-26.6.2"
#define IAP2_APP_NAME        "Music"
#define IAP2_APP_BUNDLE      "com.apple.Music"
#define IAP2_DEFAULT_DEVNAME "iPod Classic"

#define POST_IDENT_UUID      (1U << 0)
#define POST_IDENT_LANG      (1U << 1)
#define POST_IDENT_NAME      (1U << 2)
#define POST_IDENT_BT_START  (1U << 3)
#define POST_IDENT_BT_STOP   (1U << 4)

#define LIB_PEND_INFO        (1U << 0)
#define LIB_PEND_FLAG8       (1U << 1)
#define LIB_PEND_FLAG9       (1U << 2)
#define LIB_PEND_ITEMS       (1U << 3)
#define LIB_PEND_REV_DONE    (1U << 4)

struct ctrl_tx_builder {
    uint8_t buf[IAP2_SESSION_TX_CAPACITY];
    size_t cap;
    size_t used;
    uint16_t id;
    bool ok;
};

static void s_memset(void *dst, uint8_t val, size_t n)
{
    uint8_t *p = dst;
    for (size_t i = 0; i < n; i++) {
        p[i] = val;
    }
}

static void s_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

static size_t s_strlen(const char *s)
{
    size_t n = 0U;
    if (s == NULL) {
        return 0U;
    }
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static void s_copy_cstr(char *dst, size_t cap, const char *src)
{
    if (dst == NULL || cap == 0U) {
        return;
    }
    size_t i = 0U;
    if (src != NULL) {
        while (i + 1U < cap && src[i] != '\0') {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

static size_t s_append_u32(char *dst, size_t cap, size_t pos, uint32_t v)
{
    char tmp[12];
    size_t n = 0U;
    do {
        tmp[n++] = (char)('0' + (v % 10U));
        v /= 10U;
    } while (v != 0U && n < sizeof(tmp));
    while (n > 0U && pos + 1U < cap) {
        dst[pos++] = tmp[--n];
    }
    if (pos < cap) {
        dst[pos] = '\0';
    }
    return pos;
}

static bool s_revision_matches(const char *car_rev, const char *cur_rev)
{
    if (car_rev == NULL || car_rev[0] == '\0' ||
        cur_rev == NULL || cur_rev[0] == '\0') {
        return false;
    }
    size_t n = s_strlen(cur_rev);
    for (size_t i = 0U; i < n; i++) {
        if (car_rev[i] != cur_rev[i]) {
            return false;
        }
    }
    return car_rev[n] == '\0' || car_rev[n] == '.';
}

static uint32_t fnv1a_32(const char *s)
{
    uint32_t h = 2166136261U;
    if (s == NULL) {
        return h;
    }
    while (*s != '\0') {
        h = (h ^ (uint8_t)*s++) * 16777619U;
    }
    return h;
}

static uint64_t media_track_key(const struct iap2_media_state *m)
{
    if (!m->has_track) {
        return 0U;
    }
    if (m->track_id != 0U) {
        return m->track_id;
    }
    uint64_t hi = (uint64_t)fnv1a_32(m->title[0] ? m->title : "track");
    uint64_t lo = (uint64_t)fnv1a_32(m->artist) ^ ((uint64_t)fnv1a_32(m->album) << 1);
    uint64_t key = (hi << 32) | (lo & 0xFFFFFFFFU);
    return (key != 0U) ? key : 1U;
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t be64(const uint8_t *p)
{
    return ((uint64_t)be32(p) << 32) | (uint64_t)be32(p + 4U);
}

static void write_be64_raw(uint8_t *p, uint64_t v)
{
    p[0] = (uint8_t)(v >> 56);
    p[1] = (uint8_t)(v >> 48);
    p[2] = (uint8_t)(v >> 40);
    p[3] = (uint8_t)(v >> 32);
    p[4] = (uint8_t)(v >> 24);
    p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >> 8);
    p[7] = (uint8_t)v;
}

static bool time_reached(uint32_t now_ms, uint32_t due_ms)
{
    return (int32_t)(now_ms - due_ms) >= 0;
}

static bool link_can_queue(const struct iap2_link *link)
{
    return link != NULL && link->link_up && !link->tx_stale &&
           link->count < IAP2_LINK_MAX_SLOTS;
}

static size_t link_payload_cap(const struct iap2_link *link)
{
    if (link == NULL) {
        return 0U;
    }
    size_t max_pkt = IAP2_LINK_MAX_PACKET;
    if (link->peer_max != 0U && link->peer_max < max_pkt) {
        max_pkt = link->peer_max;
    }
    if (max_pkt <= IAP2_HEADER_LEN + 1U) {
        return 0U;
    }
    size_t cap = max_pkt - (IAP2_HEADER_LEN + 1U);
    if (cap > IAP2_SESSION_TX_CAPACITY) {
        cap = IAP2_SESSION_TX_CAPACITY;
    }
    return cap;
}

static bool builder_begin(struct ctrl_tx_builder *b,
                          const struct iap2_link *link,
                          uint16_t id)
{
    s_memset(b, 0, sizeof(*b));
    b->cap = link_payload_cap(link);
    b->id = id;
    if (!link_can_queue(link) || b->cap < IAP2_CONTROL_HEADER_LEN) {
        return false;
    }
    if (iap2_control_start(b->buf, b->cap, id, &b->used) != IAP2_STATUS_OK) {
        return false;
    }
    b->ok = true;
    return true;
}

static void builder_param(struct ctrl_tx_builder *b, uint16_t id,
                          const uint8_t *val, size_t len)
{
    if (!b->ok) {
        return;
    }
    if (iap2_control_append(b->buf, b->cap, &b->used, id, val, len) != IAP2_STATUS_OK) {
        b->ok = false;
    }
}

static void builder_u8(struct ctrl_tx_builder *b, uint16_t id, uint8_t v)
{
    builder_param(b, id, &v, 1U);
}

static void builder_u16(struct ctrl_tx_builder *b, uint16_t id, uint16_t v)
{
    uint8_t raw[2] = { (uint8_t)(v >> 8), (uint8_t)v };
    builder_param(b, id, raw, sizeof(raw));
}

static void builder_u32(struct ctrl_tx_builder *b, uint16_t id, uint32_t v)
{
    uint8_t raw[4] = {
        (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v
    };
    builder_param(b, id, raw, sizeof(raw));
}

static void builder_u64(struct ctrl_tx_builder *b, uint16_t id, uint64_t v)
{
    uint8_t raw[8];
    write_be64_raw(raw, v);
    builder_param(b, id, raw, sizeof(raw));
}

static void builder_str(struct ctrl_tx_builder *b, uint16_t id, const char *s)
{
    if (!b->ok) {
        return;
    }
    if (s == NULL) {
        s = "";
    }
    size_t n = s_strlen(s);
    if (n > 200U) {
        n = 200U;
        while (n > 0U && (((uint8_t)s[n] & 0xC0U) == 0x80U)) {
            n--;
        }
    }
    uint8_t tmp[201];
    if (n > 0U) {
        s_memcpy(tmp, s, n);
    }
    tmp[n] = 0U;
    builder_param(b, id, tmp, n + 1U);
}

static size_t builder_group_begin(struct ctrl_tx_builder *b, uint16_t id)
{
    size_t mark = b->used;
    builder_param(b, id, NULL, 0U);
    return mark;
}

static void builder_group_end(struct ctrl_tx_builder *b, size_t mark)
{
    if (!b->ok || b->used < mark + IAP2_CONTROL_PARAM_HEADER_LEN) {
        return;
    }
    size_t grp_len = b->used - mark;
    b->buf[mark] = (uint8_t)(grp_len >> 8);
    b->buf[mark + 1U] = (uint8_t)grp_len;
}

static void builder_rollback(struct ctrl_tx_builder *b, size_t mark)
{
    if (mark >= IAP2_CONTROL_HEADER_LEN && mark <= b->cap) {
        b->used = mark;
        b->buf[2U] = (uint8_t)(mark >> 8);
        b->buf[3U] = (uint8_t)mark;
        b->ok = true;
    }
}

static bool builder_send(struct iap2_session *s, const struct ctrl_tx_builder *b)
{
    if (!b->ok || s->link == NULL) {
        return false;
    }
    if (iap2_link_queue_payload(s->link, s->link->control_session,
                                b->buf, b->used) != IAP2_STATUS_OK) {
        return false;
    }
    s->summary.tx_messages++;
    s->summary.last_tx_msg_id = b->id;
    return true;
}

static bool send_empty_ctrl(struct iap2_session *s, uint16_t msg_id)
{
    struct ctrl_tx_builder b;
    if (!builder_begin(&b, s->link, msg_id)) {
        return false;
    }
    return builder_send(s, &b);
}

static bool send_file_packet(struct iap2_session *s, uint8_t transfer_id,
                             uint8_t op, const uint8_t *data, size_t len)
{
    if (s->link == NULL || !s->link->link_up || s->link->tx_stale ||
        s->link->file_session == 0U || !link_can_queue(s->link)) {
        return false;
    }
    size_t cap = link_payload_cap(s->link);
    if (cap > sizeof(s->file_tx_buf)) {
        cap = sizeof(s->file_tx_buf);
    }
    if (len + 2U > cap) {
        return false;
    }
    s->file_tx_buf[0] = transfer_id;
    s->file_tx_buf[1] = op;
    if (len > 0U && data != NULL) {
        s_memcpy(s->file_tx_buf + 2U, data, len);
    }
    return iap2_link_queue_payload(s->link, s->link->file_session,
                                   s->file_tx_buf, len + 2U) == IAP2_STATUS_OK;
}

static bool send_list_keys_chunk(struct iap2_session *s, uint8_t transfer_id,
                                 uint8_t op, const uint64_t *keys, size_t count)
{
    if (s->link == NULL || !s->link->link_up || s->link->tx_stale ||
        s->link->file_session == 0U || !link_can_queue(s->link) ||
        keys == NULL || count == 0U) {
        return false;
    }
    size_t cap = link_payload_cap(s->link);
    if (cap > sizeof(s->file_tx_buf)) {
        cap = sizeof(s->file_tx_buf);
    }
    if (2U + count * 8U > cap) {
        return false;
    }
    s->file_tx_buf[0] = transfer_id;
    s->file_tx_buf[1] = op;
    for (size_t i = 0U; i < count; i++) {
        write_be64_raw(s->file_tx_buf + 2U + i * 8U, keys[i]);
    }
    return iap2_link_queue_payload(s->link, s->link->file_session,
                                   s->file_tx_buf, 2U + count * 8U) == IAP2_STATUS_OK;
}

static const uint8_t *find_param(const uint8_t *params, size_t len,
                                 uint16_t id, size_t *out_len)
{
    const uint8_t *cursor = params;
    size_t rem = len;
    struct iap2_control_param p;
    while (iap2_control_next_param(&cursor, &rem, &p) == IAP2_STATUS_OK) {
        if (p.id == id) {
            *out_len = p.len;
            return p.data;
        }
    }
    return NULL;
}

static uint32_t wanted_mask(const uint8_t *group, size_t len, uint32_t fallback)
{
    if (group == NULL) {
        return fallback;
    }
    const uint8_t *cursor = group;
    size_t rem = len;
    struct iap2_control_param p;
    uint32_t mask = 0U;
    while (iap2_control_next_param(&cursor, &rem, &p) == IAP2_STATUS_OK) {
        if (p.id < 32U) {
            mask |= (1U << p.id);
        }
    }
    return mask;
}

static bool car_takes(const struct iap2_session *s, uint16_t msg_id)
{
    for (size_t i = 0; i < s->car_receives_count; i++) {
        if (s->car_receives[i] == msg_id) {
            return true;
        }
    }
    return false;
}

static const char *effective_device_name(const struct iap2_session *s)
{
    return (s->media.device_name[0] != '\0') ? s->media.device_name : IAP2_DEFAULT_DEVNAME;
}

static uint8_t next_transfer_id(struct iap2_session *s)
{
    uint8_t id = s->next_transfer_id;
    if (id < 0x80U) {
        id = 0x80U;
    }
    s->next_transfer_id = (id == 0xFFU) ? 0x80U : (uint8_t)(id + 1U);
    return id;
}

static void release_lib_batch(struct iap2_session *s)
{
    if (s->lib_batch_items != NULL) {
        uint32_t bid = s->lib_batch_id;
        s->lib_batch_items = NULL;
        s->lib_batch_count = 0U;
        s->lib_batch_pos = 0U;
        if (s->ops.on_library_batch_done != NULL) {
            s->ops.on_library_batch_done(s->ops.context, bid);
        }
    }
}

static void release_pl_keys(struct iap2_session *s, bool completed)
{
    if (s->pl_keys_ready) {
        size_t idx = s->pl_keys_index;
        s->pl_keys_ready = false;
        s->pl_keys = NULL;
        s->pl_keys_count = 0U;
        if (s->ops.on_playlist_keys_done != NULL) {
            s->ops.on_playlist_keys_done(s->ops.context, idx, completed);
        }
    }
}

static void release_q_keys(struct iap2_session *s, bool completed)
{
    if (s->queue_keys != NULL) {
        s->queue_keys = NULL;
        s->queue_count = 0U;
        if (s->ops.on_queue_keys_done != NULL) {
            s->ops.on_queue_keys_done(s->ops.context, completed);
        }
    }
}

static void finish_active_lx(struct iap2_session *s, bool completed, bool queue_cancel)
{
    if (s->lx_phase == IAP2_LX_IDLE) {
        return;
    }
    if (queue_cancel &&
        (s->lx_phase == IAP2_LX_WAIT_START || s->lx_phase == IAP2_LX_SENDING) &&
        s->lx_id != 0U) {
        s->lx_cancel_id = s->lx_id;
    }
    bool is_queue = s->lx_is_queue;
    s->lx_phase = IAP2_LX_IDLE;
    s->lx_id = 0U;
    s->lx_is_queue = false;
    s->lx_keys = NULL;
    s->lx_count = 0U;
    s->lx_sent = 0U;

    if (is_queue) {
        release_q_keys(s, completed);
    } else {
        release_pl_keys(s, completed);
        if (s->lib_phase == IAP2_LIB_PLAYLIST_XFER) {
            s->lib_playlist_xfer++;
            s->lib_playlist_xfer_asked = false;
            s->lib_playlists_sent++;
            s->summary.lib_playlists_sent = s->lib_playlists_sent;
            if (s->lib_playlist_xfer >= s->lib_playlist_count) {
                s->lib_phase = IAP2_LIB_DONE;
                s->summary.lib_phase = IAP2_LIB_DONE;
            }
        }
    }
}

static void cancel_library_sync(struct iap2_session *s, bool notify_stop)
{
    bool was_sub = s->lib_subscribed;
    s->lib_subscribed = false;
    s->library_pending &= (uint8_t)~(LIB_PEND_FLAG8 | LIB_PEND_FLAG9 |
                                     LIB_PEND_ITEMS | LIB_PEND_REV_DONE);
    s->lib_need_reset = false;
    s->lib_batch_eof = false;
    s->lib_playlist_xfer_asked = false;

    release_lib_batch(s);
    if (s->lx_phase != IAP2_LX_IDLE && !s->lx_is_queue) {
        finish_active_lx(s, false, true);
    } else {
        release_pl_keys(s, false);
    }

    s->lib_phase = IAP2_LIB_IDLE;
    s->summary.lib_phase = IAP2_LIB_IDLE;
    if (notify_stop && was_sub && s->ops.on_library_stop != NULL) {
        s->ops.on_library_stop(s->ops.context);
    }
}

static uint8_t usb_rate_index(uint32_t *rate_io)
{
    static const uint32_t rates[9] = {
        8000U, 11025U, 12000U, 16000U, 22050U, 24000U, 32000U, 44100U, 48000U
    };
    uint32_t rate = *rate_io;
    for (size_t i = 0; i < 9U; i++) {
        if (rates[i] == rate) {
            return (uint8_t)i;
        }
    }
    *rate_io = 44100U;
    return 7U;
}

static bool send_usb_audio_rate(struct iap2_session *s, uint32_t rate)
{
    uint8_t idx = usb_rate_index(&rate);

    struct ctrl_tx_builder b;
    if (!builder_begin(&b, s->link, IAP2_MSG_USB_AUDIO_INFORMATION)) {
        return false;
    }
    builder_u8(&b, 0U, (uint8_t)idx);
    builder_u32(&b, 1U, 0U);
    builder_u32(&b, 2U, 0U);
    if (!builder_send(s, &b)) {
        return false;
    }
    s->rate_sent = rate;
    s->rate_pending = false;
    s->summary.audio_rate = rate;
    if (s->audio_on && s->ops.on_usb_audio != NULL) {
        s->ops.on_usb_audio(s->ops.context, true, rate);
    }
    return true;
}

static void send_challenge(struct iap2_session *s, size_t cert_len)
{
    const size_t n = (cert_len > 800U) ? 20U : 32U;
    if (s->prng_state == 0U) {
        uint32_t seed = (s->ops.random32 != NULL)
            ? s->ops.random32(s->ops.context)
            : 0xA5A55A5AU;
        s->prng_state = (seed != 0U) ? seed : 0xA5A55A5AU;
    }
    uint8_t challenge[32];
    uint32_t r = s->prng_state;
    for (size_t i = 0; i < n; i++) {
        r ^= r << 13;
        r ^= r >> 17;
        r ^= r << 5;
        challenge[i] = (uint8_t)r;
    }
    s->prng_state = r;

    struct ctrl_tx_builder b;
    if (!builder_begin(&b, s->link, IAP2_MSG_REQUEST_CHALLENGE_RESPONSE)) {
        return;
    }
    builder_param(&b, 0U, challenge, n);
    (void)builder_send(s, &b);
}

static void copy_param_str(char *dst, size_t cap,
                           const uint8_t *params, size_t len, uint16_t id)
{
    if (dst == NULL || cap == 0U) {
        return;
    }
    dst[0] = '\0';
    size_t vlen = 0U;
    const uint8_t *v = find_param(params, len, id, &vlen);
    if (v == NULL || vlen == 0U) {
        return;
    }
    size_t i = 0U;
    while (i < vlen && i + 1U < cap && v[i] != '\0') {
        dst[i] = (char)v[i];
        i++;
    }
    dst[i] = '\0';
}

static void take_identification(struct iap2_session *s,
                                const uint8_t *params, size_t len,
                                uint32_t now_ms)
{
    copy_param_str(s->summary.car_name, sizeof(s->summary.car_name), params, len, 0U);
    copy_param_str(s->summary.car_model, sizeof(s->summary.car_model), params, len, 1U);
    copy_param_str(s->summary.car_manufacturer, sizeof(s->summary.car_manufacturer), params, len, 2U);

    size_t list_len = 0U;
    const uint8_t *list = find_param(params, len, 7U, &list_len);
    s->car_receives_count = 0U;
    for (size_t i = 0U; list != NULL && i + 1U < list_len &&
                        s->car_receives_count < IAP2_SESSION_MAX_CAR_MSGS;
         i += 2U) {
        s->car_receives[s->car_receives_count++] = be16(list + i);
    }

    size_t bt_len = 0U;
    const uint8_t *bt = find_param(params, len, 17U, &bt_len);
    s->bt_component = -1;
    if (bt != NULL) {
        size_t comp_len = 0U;
        const uint8_t *comp = find_param(bt, bt_len, 0U, &comp_len);
        if (comp != NULL && comp_len == 2U) {
            s->bt_component = (int32_t)be16(comp);
        }
    }

    s->ident_due_armed = true;
    s->ident_due_ms = now_ms + 30U;
}

static void accept_identification(struct iap2_session *s, uint32_t now_ms)
{
    if (!send_empty_ctrl(s, IAP2_MSG_IDENTIFICATION_ACCEPTED)) {
        s->ident_due_armed = true;
        return;
    }
    s->identified = true;
    s->identified_ms = now_ms;
    s->summary.identified = true;
    s->summary.phase = IAP2_SESSION_PHASE_READY;

    s->post_ident_pending = 0U;
    if (car_takes(s, IAP2_MSG_DEVICE_UUID_UPDATE)) {
        s->post_ident_pending |= POST_IDENT_UUID;
    }
    if (car_takes(s, IAP2_MSG_DEVICE_LANGUAGE_UPDATE)) {
        s->post_ident_pending |= POST_IDENT_LANG;
    }
    if (car_takes(s, IAP2_MSG_DEVICE_INFORMATION_UPDATE)) {
        s->post_ident_pending |= POST_IDENT_NAME;
    }
    if (s->bt_component >= 0 &&
        car_takes(s, IAP2_MSG_START_BLUETOOTH_PAIRING) &&
        car_takes(s, IAP2_MSG_STOP_BLUETOOTH_PAIRING)) {
        s->post_ident_pending |= (POST_IDENT_BT_START | POST_IDENT_BT_STOP);
    }

    if (s->ops.on_identified != NULL) {
        s->ops.on_identified(s->ops.context, effective_device_name(s),
                             s->car_current_ma);
    }
}

static void flush_post_ident(struct iap2_session *s)
{
    if ((s->post_ident_pending & POST_IDENT_UUID) != 0U &&
        link_can_queue(s->link)) {
        struct ctrl_tx_builder b;
        if (builder_begin(&b, s->link, IAP2_MSG_DEVICE_UUID_UPDATE)) {
            builder_str(&b, 0U, IAP2_DEVICE_UUID);
            if (builder_send(s, &b)) {
                s->post_ident_pending &= (uint8_t)~POST_IDENT_UUID;
            }
        }
    }
    if ((s->post_ident_pending & POST_IDENT_LANG) != 0U &&
        link_can_queue(s->link)) {
        struct ctrl_tx_builder b;
        if (builder_begin(&b, s->link, IAP2_MSG_DEVICE_LANGUAGE_UPDATE)) {
            builder_str(&b, 0U, "en");
            if (builder_send(s, &b)) {
                s->post_ident_pending &= (uint8_t)~POST_IDENT_LANG;
            }
        }
    }
    if ((s->post_ident_pending & POST_IDENT_NAME) != 0U &&
        link_can_queue(s->link)) {
        struct ctrl_tx_builder b;
        if (builder_begin(&b, s->link, IAP2_MSG_DEVICE_INFORMATION_UPDATE)) {
            builder_str(&b, 0U, effective_device_name(s));
            if (builder_send(s, &b)) {
                s->post_ident_pending &= (uint8_t)~POST_IDENT_NAME;
            }
        }
    }
    if ((s->post_ident_pending & POST_IDENT_BT_START) != 0U &&
        s->bt_component >= 0 && link_can_queue(s->link)) {
        struct ctrl_tx_builder b;
        if (builder_begin(&b, s->link, IAP2_MSG_START_BLUETOOTH_PAIRING)) {
            builder_u16(&b, 0U, (uint16_t)s->bt_component);
            if (builder_send(s, &b)) {
                s->post_ident_pending &= (uint8_t)~POST_IDENT_BT_START;
            }
        }
    }
    if ((s->post_ident_pending & POST_IDENT_BT_STOP) != 0U &&
        (s->post_ident_pending & POST_IDENT_BT_START) == 0U &&
        s->bt_component >= 0 && link_can_queue(s->link)) {
        struct ctrl_tx_builder b;
        if (builder_begin(&b, s->link, IAP2_MSG_STOP_BLUETOOTH_PAIRING)) {
            builder_u16(&b, 0U, (uint16_t)s->bt_component);
            builder_param(&b, 2U, NULL, 0U);
            if (builder_send(s, &b)) {
                s->post_ident_pending &= (uint8_t)~POST_IDENT_BT_STOP;
            }
        }
    }
}

static void flush_power_updates(struct iap2_session *s)
{
    static const uint8_t order[5] = {4U, 5U, 0U, 1U, 6U};
    for (size_t i = 0U; i < 5U; i++) {
        uint8_t bit = (uint8_t)(1U << i);
        if ((s->power_pending_mask & bit) == 0U) {
            continue;
        }
        if (!link_can_queue(s->link)) {
            break;
        }
        uint8_t attr = order[i];
        struct ctrl_tx_builder b;
        if (!builder_begin(&b, s->link, IAP2_MSG_POWER_UPDATE)) {
            break;
        }
        if (attr == 5U) {
            builder_u8(&b, attr, s->media.charging ? 1U : 2U);
        } else if (attr == 0U) {
            uint16_t cur = (s->car_current_ma != 0U && s->car_current_ma < 1500U)
                ? s->car_current_ma : 1500U;
            builder_u16(&b, attr, cur);
        } else if (attr == 6U) {
            uint16_t pct = (s->media.battery_pct <= 100U) ? s->media.battery_pct : 100U;
            builder_u16(&b, attr, pct);
        } else {
            builder_u8(&b, attr, 1U);
        }
        if (builder_send(s, &b)) {
            s->power_pending_mask &= (uint8_t)~bit;
        }
    }
}

static void flush_carplay_availability(struct iap2_session *s)
{
    if (!s->carplay_pending || !link_can_queue(s->link)) {
        return;
    }
    struct ctrl_tx_builder b;
    if (!builder_begin(&b, s->link, IAP2_MSG_CARPLAY_AVAILABILITY)) {
        return;
    }
    size_t g0 = builder_group_begin(&b, 0U);
    builder_u8(&b, 0U, 0U);
    builder_group_end(&b, g0);
    size_t g1 = builder_group_begin(&b, 1U);
    builder_u8(&b, 0U, 0U);
    builder_group_end(&b, g1);
    if (builder_send(s, &b)) {
        s->carplay_pending = false;
        s->availability_due_armed = false;
    }
}

static void append_named_field(struct ctrl_tx_builder *b, uint32_t mask,
                               uint16_t id_param, const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return;
    }
    if ((mask & (1U << id_param)) != 0U) {
        uint32_t h = fnv1a_32(name);
        if (h == 0U) {
            h = 1U;
        }
        builder_u64(b, id_param, (uint64_t)h);
    }
    if ((mask & (1U << (id_param + 1U))) != 0U) {
        builder_str(b, (uint16_t)(id_param + 1U), name);
    }
}

static void put_library_item(struct ctrl_tx_builder *b,
                             const struct iap2_library_item *item,
                             uint32_t m)
{
    size_t g = builder_group_begin(b, 2U);
    if ((m & (1U << 0)) != 0U) {
        builder_u64(b, 0U, item->persistent_id);
    }
    if ((m & (1U << 1)) != 0U && item->title[0] != '\0') {
        builder_str(b, 1U, item->title);
    }
    if ((m & (1U << 2)) != 0U) {
        builder_u8(b, 2U, 0U);
    }
    if ((m & (1U << 3)) != 0U) {
        builder_u8(b, 3U, 0U);
    }
    if ((m & (1U << 4)) != 0U && item->duration_ms != 0U) {
        builder_u32(b, 4U, item->duration_ms);
    }
    append_named_field(b, m, 5U, item->album);
    if ((m & (1U << 7)) != 0U && item->track_number > 0U) {
        builder_u16(b, 7U, item->track_number);
    }
    if ((m & (1U << 9)) != 0U && item->disc_number > 0U) {
        builder_u16(b, 9U, item->disc_number);
    }
    append_named_field(b, m, 11U, item->artist);
    append_named_field(b, m, 13U, item->album_artist);
    append_named_field(b, m, 15U, item->genre);
    append_named_field(b, m, 17U, item->composer);
    if ((m & (1U << 19)) != 0U) {
        builder_u8(b, 19U, 0U);
    }
    if ((m & (1U << 25)) != 0U) {
        builder_u8(b, 25U, 1U);
    }
    if ((m & (1U << 27)) != 0U) {
        builder_u16(b, 27U, 0U);
    }
    builder_group_end(b, g);
}

static void flush_library(struct iap2_session *s)
{
    if ((s->library_pending & LIB_PEND_INFO) != 0U && link_can_queue(s->link)) {
        struct ctrl_tx_builder b;
        if (builder_begin(&b, s->link, IAP2_MSG_LIBRARY_INFORMATION)) {
            size_t g = builder_group_begin(&b, 0U);
            builder_str(&b, 0U, effective_device_name(s));
            builder_str(&b, 1U, IAP2_LIBRARY_ID);
            builder_u8(&b, 2U, 0U);
            builder_group_end(&b, g);
            if (builder_send(s, &b)) {
                s->library_pending &= (uint8_t)~LIB_PEND_INFO;
            }
        }
    }
    if ((s->library_pending & LIB_PEND_FLAG8) != 0U && link_can_queue(s->link)) {
        struct ctrl_tx_builder b;
        if (builder_begin(&b, s->link, IAP2_MSG_LIBRARY_UPDATE)) {
            builder_str(&b, 0U, IAP2_LIBRARY_ID);
            builder_u8(&b, 8U, 0U);
            if (builder_send(s, &b)) {
                s->library_pending &= (uint8_t)~LIB_PEND_FLAG8;
            }
        }
    }
    if ((s->library_pending & LIB_PEND_FLAG9) != 0U &&
        (s->library_pending & LIB_PEND_FLAG8) == 0U &&
        link_can_queue(s->link)) {
        struct ctrl_tx_builder b;
        if (builder_begin(&b, s->link, IAP2_MSG_LIBRARY_UPDATE)) {
            builder_str(&b, 0U, IAP2_LIBRARY_ID);
            builder_u8(&b, 9U, 1U);
            if (builder_send(s, &b)) {
                s->library_pending &= (uint8_t)~LIB_PEND_FLAG9;
            }
        }
    }

    /* Legacy fallback when no library provider callback is registered */
    if ((s->library_pending & LIB_PEND_ITEMS) != 0U &&
        (s->library_pending & (LIB_PEND_FLAG8 | LIB_PEND_FLAG9)) == 0U &&
        link_can_queue(s->link)) {
        struct ctrl_tx_builder b;
        if (builder_begin(&b, s->link, IAP2_MSG_LIBRARY_UPDATE)) {
            builder_str(&b, 0U, IAP2_LIBRARY_ID);
            if (s->media.has_track) {
                struct iap2_library_item single = {
                    .persistent_id = media_track_key(&s->media),
                    .duration_ms = s->media.duration_ms,
                    .track_number = s->media.track_number,
                    .disc_number = s->media.disc_number,
                };
                s_copy_cstr(single.title, sizeof(single.title), s->media.title);
                s_copy_cstr(single.artist, sizeof(single.artist), s->media.artist);
                s_copy_cstr(single.album, sizeof(single.album), s->media.album);
                s_copy_cstr(single.album_artist, sizeof(single.album_artist), s->media.album_artist);
                s_copy_cstr(single.genre, sizeof(single.genre), s->media.genre);
                s_copy_cstr(single.composer, sizeof(single.composer), s->media.composer);
                builder_param(&b, 6U, NULL, 0U);
                put_library_item(&b, &single, s->library_mask);
            }
            builder_str(&b, 1U, "1");
            builder_u8(&b, 7U, 100U);
            if (builder_send(s, &b)) {
                s->library_pending &= (uint8_t)~LIB_PEND_ITEMS;
            }
        }
    }

    if ((s->library_pending & (LIB_PEND_FLAG8 | LIB_PEND_FLAG9)) != 0U) {
        return;
    }

    if ((s->library_pending & LIB_PEND_REV_DONE) != 0U && link_can_queue(s->link)) {
        struct ctrl_tx_builder b;
        if (builder_begin(&b, s->link, IAP2_MSG_LIBRARY_UPDATE)) {
            builder_str(&b, 0U, IAP2_LIBRARY_ID);
            builder_str(&b, 1U, (s->lib_rev[0] != '\0') ? s->lib_rev : "1");
            builder_u8(&b, 7U, 100U);
            if (builder_send(s, &b)) {
                s->library_pending &= (uint8_t)~LIB_PEND_REV_DONE;
            }
        }
        return;
    }

    while (s->lib_phase == IAP2_LIB_STREAMING_TRACKS &&
           link_can_queue(s->link) &&
           s->link->count + 1U < IAP2_LINK_MAX_SLOTS) {
        bool have_items = (s->lib_batch_items != NULL && s->lib_batch_pos < s->lib_batch_count);
        if (!have_items && !s->lib_batch_eof) {
            break;
        }
        struct ctrl_tx_builder b;
        if (!builder_begin(&b, s->link, IAP2_MSG_LIBRARY_UPDATE)) {
            break;
        }
        builder_str(&b, 0U, IAP2_LIBRARY_ID);
        if (s->lib_need_reset) {
            builder_param(&b, 6U, NULL, 0U);
        }

        const char *final_rev = (s->lib_rev[0] != '\0') ? s->lib_rev : "1";
        size_t rev_reserve = 4U + s_strlen(final_rev) + 1U + 5U;
        size_t saved_pos = s->lib_batch_pos;
        uint32_t items_in_pkt = 0U;

        while (s->lib_batch_items != NULL && s->lib_batch_pos < s->lib_batch_count) {
            size_t mark = b.used;
            put_library_item(&b, &s->lib_batch_items[s->lib_batch_pos], s->library_mask);
            if (!b.ok || b.used + rev_reserve > b.cap) {
                builder_rollback(&b, mark);
                if (items_in_pkt == 0U) {
                    s->lib_batch_pos++;
                    continue;
                }
                break;
            }
            s->lib_batch_pos++;
            items_in_pkt++;
        }

        bool done = (s->lib_batch_eof &&
                     (s->lib_batch_items == NULL || s->lib_batch_pos >= s->lib_batch_count));
        builder_str(&b, 1U, done ? final_rev : "0");
        builder_u8(&b, 7U, done ? 100U : 0U);

        if (!builder_send(s, &b)) {
            s->lib_batch_pos = saved_pos;
            break;
        }

        s->lib_need_reset = false;
        s->lib_tracks_sent += items_in_pkt;
        s->summary.lib_tracks_sent = s->lib_tracks_sent;

        if (s->lib_batch_items != NULL && s->lib_batch_pos >= s->lib_batch_count) {
            release_lib_batch(s);
        }
        if (done) {
            s->lib_batch_eof = false;
            s->lib_playlist_rec = 0U;
            s->lib_playlist_xfer = 0U;
            s->lib_playlist_xfer_asked = false;
            if (s->lib_want_playlists && s->lib_playlist_count > 0U) {
                s->lib_phase = IAP2_LIB_PLAYLIST_RECORDS;
            } else {
                s->lib_phase = IAP2_LIB_DONE;
            }
            s->summary.lib_phase = s->lib_phase;
            break;
        }
    }

    while (s->lib_phase == IAP2_LIB_PLAYLIST_RECORDS &&
           link_can_queue(s->link) &&
           s->link->count + 1U < IAP2_LINK_MAX_SLOTS) {
        if (s->lib_playlist_rec >= s->lib_playlist_count) {
            s->lib_phase = (s->link->file_session != 0U)
                ? IAP2_LIB_PLAYLIST_XFER : IAP2_LIB_DONE;
            s->lib_playlist_xfer = 0U;
            s->lib_playlist_xfer_asked = false;
            s->summary.lib_phase = s->lib_phase;
            break;
        }
        struct ctrl_tx_builder b;
        if (!builder_begin(&b, s->link, IAP2_MSG_LIBRARY_UPDATE)) {
            break;
        }
        builder_str(&b, 0U, IAP2_LIBRARY_ID);

        const char *base_rev = (s->lib_rev[0] != '\0') ? s->lib_rev : "1";
        size_t rev_reserve = 4U + s_strlen(base_rev) + 14U + 5U;
        size_t saved_rec = s->lib_playlist_rec;
        uint8_t saved_tid = s->next_transfer_id;
        size_t recs_in_pkt = 0U;

        while (s->lib_playlist_rec < s->lib_playlist_count) {
            size_t mark = b.used;
            uint8_t iter_tid = s->next_transfer_id;
            const struct iap2_library_playlist *pl = &s->lib_playlists[s->lib_playlist_rec];
            size_t g = builder_group_begin(&b, 3U);
            builder_u64(&b, 0U, pl->persistent_id);
            builder_str(&b, 1U, pl->name);
            builder_u64(&b, 2U, pl->parent_id);
            builder_u8(&b, 3U, 0U);
            builder_u8(&b, 4U, pl->is_folder ? 1U : 0U);
            uint8_t xid = 0U;
            if (!pl->is_folder && pl->track_count > 0U) {
                xid = next_transfer_id(s);
                builder_u8(&b, 5U, xid);
            }
            builder_group_end(&b, g);

            if (!b.ok || b.used + rev_reserve > b.cap) {
                builder_rollback(&b, mark);
                s->next_transfer_id = iter_tid;
                if (recs_in_pkt == 0U) {
                    s->lib_playlist_xfer_ids[s->lib_playlist_rec] = 0U;
                    s->lib_playlist_rec++;
                    continue;
                }
                break;
            }
            s->lib_playlist_xfer_ids[s->lib_playlist_rec] = xid;
            s->lib_playlist_rec++;
            recs_in_pkt++;
        }

        char rev_buf[IAP2_LIB_REVISION_MAX + 16U];
        s_copy_cstr(rev_buf, sizeof(rev_buf), base_rev);
        size_t pos = s_strlen(rev_buf);
        if (pos + 2U < sizeof(rev_buf)) {
            rev_buf[pos++] = '.';
            rev_buf[pos++] = 'p';
            rev_buf[pos] = '\0';
            (void)s_append_u32(rev_buf, sizeof(rev_buf), pos, (uint32_t)s->lib_playlist_rec);
        }
        builder_str(&b, 1U, rev_buf);
        builder_u8(&b, 7U, 100U);

        if (!builder_send(s, &b)) {
            s->lib_playlist_rec = saved_rec;
            s->next_transfer_id = saved_tid;
            break;
        }
        if (s->lib_playlist_rec >= s->lib_playlist_count) {
            s->lib_phase = (s->link->file_session != 0U)
                ? IAP2_LIB_PLAYLIST_XFER : IAP2_LIB_DONE;
            s->lib_playlist_xfer = 0U;
            s->lib_playlist_xfer_asked = false;
            s->summary.lib_phase = s->lib_phase;
            break;
        }
    }
}

static void release_art_data(struct iap2_session *s, bool completed)
{
    if (s->art_data != NULL) {
        uint64_t key = s->art_data_key;
        s->art_data = NULL;
        s->art_data_len = 0U;
        s->art_data_key = 0U;
        if (s->ops.on_artwork_done != NULL) {
            s->ops.on_artwork_done(s->ops.context, key, completed);
        }
    }
}

static void abort_active_artwork(struct iap2_session *s, bool queue_cancel)
{
    if (queue_cancel &&
        (s->art_phase == IAP2_ART_WAIT_START || s->art_phase == IAP2_ART_SENDING) &&
        s->art_id != 0U) {
        s->art_cancel_id = s->art_id;
    }
    s->art_phase = IAP2_ART_IDLE;
    s->art_id = 0U;
    s->art_size = 0U;
    s->art_sent = 0U;
    s->art_announced_real = false;
    s->art_late_pending = false;
    s->summary.artwork_phase = IAP2_ART_IDLE;
}

static void flush_artwork_file(struct iap2_session *s, uint32_t now_ms)
{
    if (s->link == NULL || !s->link->link_up || s->link->tx_stale) {
        return;
    }
    if (s->link->file_session == 0U) {
        if (s->art_phase != IAP2_ART_IDLE || s->art_data != NULL) {
            abort_active_artwork(s, false);
            release_art_data(s, false);
        }
        s->art_cancel_id = 0U;
        s->art_blank_id = 0U;
        return;
    }

    if (s->art_cancel_id != 0U) {
        if (!send_file_packet(s, s->art_cancel_id, 0x02U, NULL, 0U)) {
            return;
        }
        s->art_cancel_id = 0U;
    }

    if (s->art_late_pending && s->art_phase == IAP2_ART_IDLE) {
        if (!s->identified || !s->now_playing ||
            (s->media_mask & (1U << IAP2_MI_ARTWORK)) == 0U ||
            s->art_data == NULL || s->art_data_len == 0U ||
            s->art_data_key != s->np_key) {
            s->art_late_pending = false;
            release_art_data(s, false);
        } else if (s->link->count + 1U < IAP2_LINK_MAX_SLOTS && link_can_queue(s->link)) {
            struct ctrl_tx_builder b;
            if (builder_begin(&b, s->link, IAP2_MSG_NOW_PLAYING_UPDATE)) {
                uint8_t new_id = next_transfer_id(s);
                size_t g0 = builder_group_begin(&b, 0U);
                builder_u8(&b, IAP2_MI_ARTWORK, new_id);
                builder_group_end(&b, g0);
                if (builder_send(s, &b)) {
                    s->art_late_pending = false;
                    s->art_id = new_id;
                    s->art_size = s->art_data_len;
                    s->art_sent = 0U;
                    s->art_announced_real = true;
                    s->art_phase = IAP2_ART_SETUP_PENDING;
                    s->summary.artwork_phase = IAP2_ART_SETUP_PENDING;
                }
            }
        }
    }

    if (s->art_phase == IAP2_ART_SETUP_PENDING) {
        uint8_t setup[10] = {
            0U, 0U, 0U, 0U,
            (uint8_t)(s->art_size >> 24),
            (uint8_t)(s->art_size >> 16),
            (uint8_t)(s->art_size >> 8),
            (uint8_t)s->art_size,
            0U, 2U
        };
        if (send_file_packet(s, s->art_id, 0x04U, setup, sizeof(setup))) {
            s->art_phase = IAP2_ART_WAIT_START;
            s->art_started_ms = now_ms;
            s->summary.artwork_phase = IAP2_ART_WAIT_START;
        }
        return;
    }

    if (s->art_phase == IAP2_ART_WAIT_START) {
        if (time_reached(now_ms, s->art_started_ms + 5000U)) {
            if (send_file_packet(s, s->art_id, 0x02U, NULL, 0U)) {
                abort_active_artwork(s, false);
                release_art_data(s, false);
            }
        }
        return;
    }

    while (s->art_phase == IAP2_ART_SENDING) {
        if (s->art_size == 0U) {
            if (send_file_packet(s, s->art_id, 0xC0U, NULL, 0U)) {
                abort_active_artwork(s, false);
            }
            break;
        }
        if (s->art_data == NULL || s->art_sent >= s->art_size) {
            abort_active_artwork(s, true);
            release_art_data(s, false);
            break;
        }
        if (!link_can_queue(s->link) || s->link->count + 1U >= IAP2_LINK_MAX_SLOTS) {
            break;
        }
        size_t cap = link_payload_cap(s->link);
        if (cap > sizeof(s->file_tx_buf)) {
            cap = sizeof(s->file_tx_buf);
        }
        if (cap <= 2U) {
            break;
        }
        size_t max_chunk = cap - 2U;
        size_t rem = (size_t)(s->art_size - s->art_sent);
        size_t take = (rem < max_chunk) ? rem : max_chunk;
        bool first = (s->art_sent == 0U);
        bool last = ((uint32_t)take >= (s->art_size - s->art_sent));
        uint8_t op = first ? (last ? 0xC0U : 0x80U) : (last ? 0x40U : 0x00U);
        if (!send_file_packet(s, s->art_id, op, s->art_data + s->art_sent, take)) {
            break;
        }
        s->art_sent += (uint32_t)take;
        if (last) {
            s->summary.artwork_transfers++;
            s->summary.last_artwork_bytes = s->art_size;
            abort_active_artwork(s, false);
            release_art_data(s, true);
            break;
        }
    }
}

static void flush_list_file(struct iap2_session *s, uint32_t now_ms)
{
    if (s->link == NULL || !s->link->link_up || s->link->tx_stale) {
        return;
    }
    if (s->link->file_session == 0U) {
        if (s->lx_phase != IAP2_LX_IDLE) {
            finish_active_lx(s, false, false);
        }
        s->lx_cancel_id = 0U;
        if (s->queue_pending) {
            s->queue_pending = false;
            release_q_keys(s, false);
        }
        return;
    }

    if (s->lx_cancel_id != 0U) {
        if (!send_file_packet(s, s->lx_cancel_id, 0x02U, NULL, 0U)) {
            return;
        }
        s->lx_cancel_id = 0U;
    }

    if (s->lx_phase == IAP2_LX_IDLE) {
        if (s->queue_pending && s->identified && s->now_playing &&
            (s->playback_mask & (1U << IAP2_PB_QUEUE_LIST)) != 0U &&
            s->link->count + 1U < IAP2_LINK_MAX_SLOTS &&
            link_can_queue(s->link)) {
            bool whole = (!s->queue_from_car && s->queue_keys != NULL && s->queue_count > 0U);
            struct ctrl_tx_builder b;
            if (builder_begin(&b, s->link, IAP2_MSG_NOW_PLAYING_UPDATE)) {
                size_t g1 = builder_group_begin(&b, 1U);
                builder_u8(&b, IAP2_PB_QUEUE_LIST, whole ? 1U : 0U);
                if (whole) {
                    builder_param(&b, IAP2_PB_QUEUE_LIST_ID, NULL, 0U);
                }
                builder_group_end(&b, g1);
                if (builder_send(s, &b)) {
                    s->queue_pending = false;
                    if (whole) {
                        s->lx_id = next_transfer_id(s);
                        s->lx_is_queue = true;
                        s->lx_keys = s->queue_keys;
                        s->lx_count = s->queue_count;
                        s->lx_sent = 0U;
                        s->lx_phase = IAP2_LX_SETUP_PENDING;
                    } else {
                        release_q_keys(s, true);
                    }
                }
            }
        }
    }

    if (s->lx_phase == IAP2_LX_IDLE && s->lib_phase == IAP2_LIB_PLAYLIST_XFER) {
        while (s->lib_playlist_xfer < s->lib_playlist_count) {
            const struct iap2_library_playlist *pl = &s->lib_playlists[s->lib_playlist_xfer];
            if (pl->is_folder == 0U && s->lib_playlist_xfer_ids[s->lib_playlist_xfer] != 0U) {
                break;
            }
            s->lib_playlist_xfer++;
            s->lib_playlist_xfer_asked = false;
            s->lib_playlists_sent++;
            s->summary.lib_playlists_sent = s->lib_playlists_sent;
        }
        if (s->lib_playlist_xfer >= s->lib_playlist_count) {
            s->lib_phase = IAP2_LIB_DONE;
            s->summary.lib_phase = IAP2_LIB_DONE;
        } else if (s->pl_keys_ready && s->pl_keys_index == s->lib_playlist_xfer) {
            if ((s->pl_keys_count == 0U || s->pl_keys == NULL) &&
                s->lib_playlist_xfer_ids[s->lib_playlist_xfer] == 0U) {
                release_pl_keys(s, true);
                s->lib_playlist_xfer++;
                s->lib_playlist_xfer_asked = false;
                s->lib_playlists_sent++;
                s->summary.lib_playlists_sent = s->lib_playlists_sent;
                if (s->lib_playlist_xfer >= s->lib_playlist_count) {
                    s->lib_phase = IAP2_LIB_DONE;
                    s->summary.lib_phase = IAP2_LIB_DONE;
                }
            } else {
                bool empty = (s->pl_keys_count == 0U || s->pl_keys == NULL);
                s->lx_id = s->lib_playlist_xfer_ids[s->lib_playlist_xfer];
                s->lx_is_queue = false;
                s->lx_pl_index = s->lib_playlist_xfer;
                s->lx_pl_id = s->lib_playlists[s->lib_playlist_xfer].persistent_id;
                s->lx_keys = empty ? NULL : s->pl_keys;
                s->lx_count = empty ? 0U : s->pl_keys_count;
                s->lx_sent = 0U;
                s->lx_phase = IAP2_LX_SETUP_PENDING;
            }
        } else if (!s->lib_playlist_xfer_asked) {
            s->lib_playlist_xfer_asked = true;
            if (s->ops.on_playlist_keys_request != NULL) {
                s->ops.on_playlist_keys_request(
                    s->ops.context,
                    s->lib_playlist_xfer,
                    s->lib_playlists[s->lib_playlist_xfer].persistent_id);
            } else {
                s->lib_playlist_xfer++;
                s->lib_playlist_xfer_asked = false;
                s->lib_playlists_sent++;
                s->summary.lib_playlists_sent = s->lib_playlists_sent;
                if (s->lib_playlist_xfer >= s->lib_playlist_count) {
                    s->lib_phase = IAP2_LIB_DONE;
                    s->summary.lib_phase = IAP2_LIB_DONE;
                }
            }
        }
    }

    if (s->lx_phase == IAP2_LX_SETUP_PENDING) {
        uint8_t setup[8U + 2U + 8U + sizeof(IAP2_LIBRARY_ID)];
        uint32_t size = (uint32_t)(s->lx_count * 8U);
        size_t n = 0U;
        setup[n++] = 0U;
        setup[n++] = 0U;
        setup[n++] = 0U;
        setup[n++] = 0U;
        setup[n++] = (uint8_t)(size >> 24);
        setup[n++] = (uint8_t)(size >> 16);
        setup[n++] = (uint8_t)(size >> 8);
        setup[n++] = (uint8_t)size;
        setup[n++] = 0U;
        setup[n++] = s->lx_is_queue ? 3U : 4U;
        if (!s->lx_is_queue) {
            write_be64_raw(setup + n, s->lx_pl_id);
            n += 8U;
            s_memcpy(setup + n, IAP2_LIBRARY_ID, sizeof(IAP2_LIBRARY_ID));
            n += sizeof(IAP2_LIBRARY_ID);
        }
        if (send_file_packet(s, s->lx_id, 0x04U, setup, n)) {
            s->lx_phase = IAP2_LX_WAIT_START;
            s->lx_started_ms = now_ms;
        }
        return;
    }

    if (s->lx_phase == IAP2_LX_WAIT_START) {
        if (time_reached(now_ms, s->lx_started_ms + 5000U)) {
            if (send_file_packet(s, s->lx_id, 0x02U, NULL, 0U)) {
                finish_active_lx(s, false, false);
            }
        }
        return;
    }

    while (s->lx_phase == IAP2_LX_SENDING) {
        if (s->lx_count == 0U) {
            if (send_file_packet(s, s->lx_id, 0xC0U, NULL, 0U)) {
                finish_active_lx(s, true, false);
            }
            break;
        }
        if (s->lx_keys == NULL || s->lx_sent >= s->lx_count) {
            finish_active_lx(s, true, false);
            break;
        }
        if (!link_can_queue(s->link) || s->link->count + 1U >= IAP2_LINK_MAX_SLOTS) {
            break;
        }
        size_t cap = link_payload_cap(s->link);
        if (cap > sizeof(s->file_tx_buf)) {
            cap = sizeof(s->file_tx_buf);
        }
        if (cap < 10U) {
            break;
        }
        size_t max_keys = (cap - 2U) / 8U;
        size_t rem = s->lx_count - s->lx_sent;
        size_t take = (rem < max_keys) ? rem : max_keys;
        bool first = (s->lx_sent == 0U);
        bool last = (s->lx_sent + take >= s->lx_count);
        uint8_t op = first ? (last ? 0xC0U : 0x80U) : (last ? 0x40U : 0x00U);
        if (!send_list_keys_chunk(s, s->lx_id, op, s->lx_keys + s->lx_sent, take)) {
            break;
        }
        s->lx_sent += take;
        if (last) {
            finish_active_lx(s, true, false);
            break;
        }
    }
}

static void send_now_playing_full(struct iap2_session *s, bool include_media,
                                  uint32_t now_ms)
{
    if (!link_can_queue(s->link)) {
        return;
    }

    struct ctrl_tx_builder b;
    if (!builder_begin(&b, s->link, IAP2_MSG_NOW_PLAYING_UPDATE)) {
        return;
    }

    uint64_t current_key = media_track_key(&s->media);
    uint8_t art_id = 0U;
    if (include_media && s->media.has_track) {
        size_t g0 = builder_group_begin(&b, 0U);
        if (s->media_mask & (1U << IAP2_MI_PERSISTENT_ID)) {
            builder_u64(&b, IAP2_MI_PERSISTENT_ID, current_key);
        }
        if (s->media_mask & (1U << IAP2_MI_TITLE)) {
            builder_str(&b, IAP2_MI_TITLE,
                        s->media.title[0] ? s->media.title : "Track");
        }
        if (s->media_mask & (1U << IAP2_MI_MEDIA_TYPE)) {
            builder_u8(&b, IAP2_MI_MEDIA_TYPE, 0U);
        }
        if (s->media_mask & (1U << IAP2_MI_DURATION)) {
            builder_u32(&b, IAP2_MI_DURATION, s->media.duration_ms);
        }
        if ((s->media_mask & (1U << IAP2_MI_ALBUM)) && s->media.album[0] != '\0') {
            builder_str(&b, IAP2_MI_ALBUM, s->media.album);
        }
        if ((s->media_mask & (1U << IAP2_MI_TRACK_NUMBER)) && s->media.track_number > 0U) {
            builder_u16(&b, IAP2_MI_TRACK_NUMBER, s->media.track_number);
        }
        if ((s->media_mask & (1U << IAP2_MI_DISC_NUMBER)) && s->media.disc_number > 0U) {
            builder_u16(&b, IAP2_MI_DISC_NUMBER, s->media.disc_number);
        }
        if ((s->media_mask & (1U << IAP2_MI_ARTIST)) && s->media.artist[0] != '\0') {
            builder_str(&b, IAP2_MI_ARTIST, s->media.artist);
        }
        if ((s->media_mask & (1U << IAP2_MI_ALBUM_ARTIST)) && s->media.album_artist[0] != '\0') {
            builder_str(&b, IAP2_MI_ALBUM_ARTIST, s->media.album_artist);
        }
        if ((s->media_mask & (1U << IAP2_MI_GENRE)) && s->media.genre[0] != '\0') {
            builder_str(&b, IAP2_MI_GENRE, s->media.genre);
        }
        if ((s->media_mask & (1U << IAP2_MI_COMPOSER)) && s->media.composer[0] != '\0') {
            builder_str(&b, IAP2_MI_COMPOSER, s->media.composer);
        }
        if (s->media_mask & (1U << IAP2_MI_COMPILATION)) {
            builder_u8(&b, IAP2_MI_COMPILATION, 0U);
        }
        for (uint16_t a = IAP2_MI_LIKE_SUPPORTED; a <= IAP2_MI_BANNED; a++) {
            if (s->media_mask & (1U << a)) {
                builder_u8(&b, a, 0U);
            }
        }
        if (s->media_mask & (1U << IAP2_MI_CHAPTER_COUNT)) {
            builder_u16(&b, IAP2_MI_CHAPTER_COUNT, 0U);
        }
        if ((s->media_mask & (1U << IAP2_MI_ARTWORK)) &&
            s->link->file_session != 0U) {
            art_id = next_transfer_id(s);
            builder_u8(&b, IAP2_MI_ARTWORK, art_id);
        }
        builder_group_end(&b, g0);
    }

    size_t g1 = builder_group_begin(&b, 1U);
    if (s->playback_mask & (1U << IAP2_PB_STATE)) {
        builder_u8(&b, IAP2_PB_STATE, s->media.play_state);
    }
    if (s->playback_mask & (1U << IAP2_PB_POSITION)) {
        builder_u32(&b, IAP2_PB_POSITION, s->media.elapsed_ms);
    }
    if (s->playback_mask & (1U << IAP2_PB_QUEUE_INDEX)) {
        builder_u32(&b, IAP2_PB_QUEUE_INDEX, s->media.queue_index);
    }
    if (s->playback_mask & (1U << IAP2_PB_QUEUE_COUNT)) {
        builder_u32(&b, IAP2_PB_QUEUE_COUNT, s->media.queue_count);
    }
    if (s->playback_mask & (1U << IAP2_PB_CHAPTER_INDEX)) {
        builder_u32(&b, IAP2_PB_CHAPTER_INDEX, 0U);
    }
    if (s->playback_mask & (1U << IAP2_PB_SHUFFLE)) {
        builder_u8(&b, IAP2_PB_SHUFFLE, s->media.shuffle ? 1U : 0U);
    }
    if (s->playback_mask & (1U << IAP2_PB_REPEAT)) {
        builder_u8(&b, IAP2_PB_REPEAT,
                   (s->media.repeat_mode <= IAP2_REPEAT_ALL) ? s->media.repeat_mode : 0U);
    }
    if (s->playback_mask & (1U << IAP2_PB_APP_NAME)) {
        builder_str(&b, IAP2_PB_APP_NAME, IAP2_APP_NAME);
    }
    if (s->playback_mask & (1U << IAP2_PB_LIBRARY)) {
        builder_str(&b, IAP2_PB_LIBRARY, IAP2_LIBRARY_ID);
    }
    if (s->playback_mask & (1U << IAP2_PB_SPEED)) {
        builder_u16(&b, IAP2_PB_SPEED, 100U);
    }
    if (s->playback_mask & (1U << IAP2_PB_SEEKABLE)) {
        builder_u8(&b, IAP2_PB_SEEKABLE, 1U);
    }
    if (s->playback_mask & (1U << IAP2_PB_APP_BUNDLE)) {
        builder_str(&b, IAP2_PB_APP_BUNDLE, IAP2_APP_BUNDLE);
    }
    builder_group_end(&b, g1);

    if (!builder_send(s, &b)) {
        return;
    }

    s->np_key = current_key;
    s->np_state = s->media.play_state;
    s->np_modes = (uint8_t)((s->media.shuffle ? 4U : 0U) | (s->media.repeat_mode & 3U));
    s->np_elapsed_ms = s->media.elapsed_ms;
    s->np_tick_ms = now_ms;
    s->np_full = false;
    if (include_media) {
        bool cancel_old = (s->art_phase == IAP2_ART_WAIT_START || s->art_phase == IAP2_ART_SENDING);
        abort_active_artwork(s, cancel_old);
        if (art_id != 0U) {
            s->art_id = art_id;
            s->art_sent = 0U;
            if (s->art_data != NULL && s->art_data_len > 0U && s->art_data_key == current_key) {
                s->art_size = s->art_data_len;
                s->art_announced_real = true;
            } else {
                if (s->art_data != NULL) {
                    release_art_data(s, false);
                }
                s->art_size = 0U;
                s->art_announced_real = false;
            }
            s->art_phase = IAP2_ART_SETUP_PENDING;
            s->summary.artwork_phase = IAP2_ART_SETUP_PENDING;
        } else if (s->art_data != NULL) {
            release_art_data(s, false);
        }
    }
}

static void send_now_playing_position(struct iap2_session *s, uint32_t now_ms)
{
    if ((s->playback_mask & (1U << IAP2_PB_POSITION)) == 0U ||
        !link_can_queue(s->link)) {
        return;
    }
    struct ctrl_tx_builder b;
    if (!builder_begin(&b, s->link, IAP2_MSG_NOW_PLAYING_UPDATE)) {
        return;
    }
    size_t g1 = builder_group_begin(&b, 1U);
    builder_u32(&b, IAP2_PB_POSITION, s->media.elapsed_ms);
    builder_group_end(&b, g1);
    if (builder_send(s, &b)) {
        s->np_elapsed_ms = s->media.elapsed_ms;
        s->np_tick_ms = now_ms;
    }
}

static void poll_now_playing(struct iap2_session *s, uint32_t now_ms)
{
    if (!s->identified || !s->now_playing || !s->media_synced) {
        return;
    }
    uint64_t key = media_track_key(&s->media);
    uint8_t state = s->media.play_state;
    uint32_t elapsed = s->media.elapsed_ms;
    uint32_t expected = s->np_elapsed_ms;
    if (s->np_state == IAP2_PLAY_PLAYING) {
        expected += (now_ms - s->np_tick_ms);
    }
    int32_t drift = (int32_t)(elapsed - expected);
    bool track_changed = (key != s->np_key) || s->np_full;
    uint8_t modes = (uint8_t)((s->media.shuffle ? 4U : 0U) | (s->media.repeat_mode & 3U));

    if (!track_changed && state == s->np_state && modes == s->np_modes &&
        drift > -3000 && drift < 3000) {
        if (state == IAP2_PLAY_PLAYING && (now_ms - s->np_tick_ms) >= 500U) {
            send_now_playing_position(s, now_ms);
        }
        return;
    }

    send_now_playing_full(s, track_changed, now_ms);
}

static uint32_t hid_field(const uint8_t *r, size_t len, uint32_t bit, uint32_t size)
{
    uint32_t v = 0U;
    for (uint32_t i = 0U; i < size; i++, bit++) {
        if ((size_t)(bit / 8U) < len && ((r[bit / 8U] >> (bit % 8U)) & 1U) != 0U) {
            v |= (1U << i);
        }
    }
    return v;
}

static void parse_hid_descriptor(struct iap2_session *s, const uint8_t *d, size_t len)
{
    uint32_t page = 0U, size = 0U, count = 0U, id = 0U, logical_min = 0U;
    uint32_t usages[16] = {0};
    uint32_t usage_min = 0U;
    size_t n_usages = 0U;
    uint32_t bits[8] = {0};

    s->hid_control_count = 0U;
    s->hid_report_ids = false;
    s_memset(s->last_report_id, 0, sizeof(s->last_report_id));
    s_memset(s->last_report, 0, sizeof(s->last_report));

    for (size_t i = 0U; i < len;) {
        uint8_t prefix = d[i];
        if (prefix == 0xFEU) {
            size_t long_len = (i + 1U < len) ? d[i + 1U] : 0U;
            i += 3U + long_len;
            continue;
        }
        size_t n = ((prefix & 3U) == 3U) ? 4U : (size_t)(prefix & 3U);
        if (i + 1U + n > len) {
            break;
        }
        uint32_t v = 0U;
        if (n == 4U) {
            v = (uint32_t)d[i + 1U] | ((uint32_t)d[i + 2U] << 8) |
                ((uint32_t)d[i + 3U] << 16) | ((uint32_t)d[i + 4U] << 24);
        } else if (n == 2U) {
            v = (uint32_t)d[i + 1U] | ((uint32_t)d[i + 2U] << 8);
        } else if (n == 1U) {
            v = (uint32_t)d[i + 1U];
        }
        i += 1U + n;

        switch (prefix & 0xFCU) {
        case 0x04U: page = v; break;
        case 0x14U: logical_min = v; break;
        case 0x74U: size = v; break;
        case 0x84U: id = v; s->hid_report_ids = true; break;
        case 0x94U: count = v; break;
        case 0x08U:
            if (n_usages < 16U) {
                usages[n_usages++] = (n == 4U) ? v : ((page << 16) | v);
            }
            break;
        case 0x18U: usage_min = v; break;
        case 0x80U: {
            uint32_t *at = &bits[id & 7U];
            if ((v & 1U) == 0U && page == 0x0CU &&
                s->hid_control_count < IAP2_SESSION_MAX_HID_CONTROLS) {
                if ((v & 2U) != 0U) {
                    for (uint32_t k = 0U; k < count && k < n_usages; k++) {
                        if (size == 1U && (usages[k] >> 16) == 0x0CU &&
                            s->hid_control_count < IAP2_SESSION_MAX_HID_CONTROLS) {
                            s->hid_controls[s->hid_control_count++] = (struct iap2_hid_control){
                                .report_id = (uint8_t)id,
                                .bit = (uint16_t)(*at + k),
                                .size = 1U,
                                .count = 1U,
                                .usage = (uint16_t)(usages[k] & 0xFFFFU),
                            };
                        }
                    }
                } else if (size > 0U && size <= 32U && count <= 16U) {
                    s->hid_controls[s->hid_control_count++] = (struct iap2_hid_control){
                        .report_id = (uint8_t)id,
                        .bit = (uint16_t)*at,
                        .size = (uint8_t)size,
                        .count = (uint8_t)count,
                        .usage = (uint16_t)(usage_min - logical_min),
                    };
                }
            }
            *at += size * count;
        }
        /* fall through */
        case 0x90U: case 0xB0U: case 0xA0U: case 0xC0U:
            n_usages = 0U;
            usage_min = 0U;
            break;
        default:
            break;
        }
    }
    s->summary.hid_active = (s->hid_control_count > 0U);
    s->summary.hid_control_count = (uint8_t)s->hid_control_count;
}

static void dispatch_usage(struct iap2_session *s, uint16_t usage)
{
    iap2_remote_action_t action = IAP2_REMOTE_NONE;
    switch (usage) {
    case 0xCDU: action = IAP2_REMOTE_PLAY_PAUSE; break;
    case 0xB0U:
        if (s->media.play_state != IAP2_PLAY_PLAYING) {
            action = IAP2_REMOTE_PLAY;
        }
        break;
    case 0xB1U:
        if (s->media.play_state == IAP2_PLAY_PLAYING) {
            action = IAP2_REMOTE_PAUSE;
        }
        break;
    case 0xB5U: action = IAP2_REMOTE_NEXT; break;
    case 0xB6U: action = IAP2_REMOTE_PREV; break;
    case 0xB7U: action = IAP2_REMOTE_STOP; break;
    case 0xB3U: action = IAP2_REMOTE_FFWD; break;
    case 0xB4U: action = IAP2_REMOTE_REW; break;
    case 0xE9U: action = IAP2_REMOTE_VOL_UP; break;
    case 0xEAU: action = IAP2_REMOTE_VOL_DOWN; break;
    case 0xB9U: action = IAP2_REMOTE_SHUFFLE_TOGGLE; break;
    case 0xBCU: action = IAP2_REMOTE_REPEAT_NEXT; break;
    default: break;
    }
    if (action != IAP2_REMOTE_NONE && s->ops.on_hid_action != NULL) {
        s->ops.on_hid_action(s->ops.context, action);
    }
}

static void take_hid_report(struct iap2_session *s, const uint8_t *r, size_t len)
{
    uint8_t id = 0U;
    if (s->hid_report_ids && len > 0U) {
        id = *r++;
        len--;
    }
    size_t slot = 0U;
    while (slot < IAP2_SESSION_MAX_HID_REPORTS &&
           s->last_report_id[slot] != (uint8_t)(id + 1U)) {
        slot++;
    }
    if (slot == IAP2_SESSION_MAX_HID_REPORTS) {
        slot = 0U;
        while (slot < IAP2_SESSION_MAX_HID_REPORTS - 1U &&
               s->last_report_id[slot] != 0U) {
            slot++;
        }
        s->last_report_id[slot] = (uint8_t)(id + 1U);
        s_memset(s->last_report[slot], 0, sizeof(s->last_report[slot]));
    }
    uint8_t *last = s->last_report[slot];
    if (len > IAP2_SESSION_MAX_HID_REPORT_LEN) {
        len = IAP2_SESSION_MAX_HID_REPORT_LEN;
    }

    for (size_t i = 0U; i < s->hid_control_count; i++) {
        const struct iap2_hid_control *c = &s->hid_controls[i];
        if (c->report_id != id) {
            continue;
        }
        if (c->count == 1U && c->size == 1U) {
            if (hid_field(r, len, c->bit, 1U) != 0U &&
                hid_field(last, len, c->bit, 1U) == 0U) {
                dispatch_usage(s, c->usage);
            }
            continue;
        }
        for (uint8_t k = 0U; k < c->count; k++) {
            uint32_t v = hid_field(r, len, (uint32_t)c->bit + (uint32_t)k * c->size, c->size);
            bool was = false;
            for (uint8_t j = 0U; j < c->count && v != 0U; j++) {
                if (hid_field(last, len, (uint32_t)c->bit + (uint32_t)j * c->size, c->size) == v) {
                    was = true;
                }
            }
            if (v != 0U && !was) {
                dispatch_usage(s, (uint16_t)(c->usage + v));
            }
        }
    }
    if (len > 0U) {
        s_memcpy(last, r, len);
    }
}

struct dispatch_ctx {
    struct iap2_session *session;
    uint32_t now_ms;
};

static void record_rx_msg_id(struct iap2_session_summary *sum, uint16_t id)
{
    sum->rx_messages++;
    sum->last_rx_msg_id = id;
    if (sum->rx_head_count < IAP2_RX_LOG_HEAD) {
        sum->rx_head_ids[sum->rx_head_count++] = id;
    }
    if (sum->rx_tail_count < IAP2_RX_LOG_TAIL) {
        sum->rx_tail_ids[sum->rx_tail_count++] = id;
    } else {
        for (size_t i = 0U; i + 1U < IAP2_RX_LOG_TAIL; i++) {
            sum->rx_tail_ids[i] = sum->rx_tail_ids[i + 1U];
        }
        sum->rx_tail_ids[IAP2_RX_LOG_TAIL - 1U] = id;
    }
}

static bool suppress_connect_play_cmd(struct iap2_session *s, uint32_t now_ms)
{
    if (s->identified &&
        (uint32_t)(now_ms - s->identified_ms) < IAP2_CONNECT_PLAY_GUARD_MS) {
        s->summary.play_cmd_suppressed++;
        s->np_full = true;
        return true;
    }
    return false;
}

static void on_control_message(void *ctx_ptr, const struct iap2_control_message *msg)
{
    struct dispatch_ctx *ctx = ctx_ptr;
    struct iap2_session *s = ctx->session;
    uint32_t now_ms = ctx->now_ms;
    const uint8_t *p = msg->parameters;
    size_t plen = msg->parameters_len;
    size_t n = 0U;
    const uint8_t *v = NULL;

    record_rx_msg_id(&s->summary, msg->id);

    switch (msg->id) {
    case IAP2_MSG_CERTIFICATE:
        v = find_param(p, plen, 0U, &n);
        send_challenge(s, (v != NULL) ? n : 0U);
        break;
    case IAP2_MSG_CHALLENGE_RESPONSE:
        s->auth_due_armed = true;
        s->auth_due_ms = now_ms + 20U;
        break;
    case IAP2_MSG_IDENTIFICATION_INFORMATION:
        take_identification(s, p, plen, now_ms);
        break;
    case IAP2_MSG_START_POWER_UPDATES: {
        uint32_t mask = wanted_mask(p, plen, 0x00000073U);
        static const uint8_t order[5] = {4U, 5U, 0U, 1U, 6U};
        for (size_t i = 0U; i < 5U; i++) {
            if ((mask & (1U << order[i])) != 0U) {
                s->power_pending_mask |= (uint8_t)(1U << i);
            }
        }
        flush_power_updates(s);
        break;
    }
    case IAP2_MSG_POWER_SOURCE_UPDATE:
        v = find_param(p, plen, 0U, &n);
        if (v != NULL && n == 2U) {
            s->car_current_ma = be16(v);
            s->summary.car_current_ma = s->car_current_ma;
        }
        if (s->identified && car_takes(s, IAP2_MSG_CARPLAY_AVAILABILITY)) {
            s->availability_due_armed = true;
            s->availability_due_ms = now_ms + 50U;
        }
        break;
    case IAP2_MSG_START_BT_CONNECTION:
        v = find_param(p, plen, 0U, &n);
        if (v != NULL && n == 2U && link_can_queue(s->link)) {
            struct ctrl_tx_builder b;
            if (builder_begin(&b, s->link, IAP2_MSG_BT_CONNECTION_UPDATE)) {
                builder_u16(&b, 0U, be16(v));
                builder_param(&b, 1U, NULL, 0U);
                (void)builder_send(s, &b);
            }
        }
        break;
    case IAP2_MSG_START_LIBRARY_INFORMATION:
        s->library_pending |= LIB_PEND_INFO;
        flush_library(s);
        break;
    case IAP2_MSG_START_LIBRARY_UPDATES: {
        cancel_library_sync(s, false);
        v = find_param(p, plen, 1U, &n);
        if (v != NULL && n > 0U && v[n - 1U] == '\0') {
            size_t cpy = (n - 1U < sizeof(s->car_lib_rev) - 1U)
                ? (n - 1U) : (sizeof(s->car_lib_rev) - 1U);
            if (cpy > 0U) {
                s_memcpy(s->car_lib_rev, v, cpy);
            }
            s->car_lib_rev[cpy] = '\0';
        } else {
            s->car_lib_rev[0] = '\0';
        }
        v = find_param(p, plen, 2U, &n);
        s->library_mask = wanted_mask(v, n, 0x0A0FFFFFU);
        s->summary.library_mask = s->library_mask;
        s->lib_want_playlists = (find_param(p, plen, 3U, &n) != NULL) || (v == NULL);
        s->lib_tracks_sent = 0U;
        s->lib_tracks_total = 0U;
        s->lib_playlist_count = 0U;
        s->lib_playlist_rec = 0U;
        s->lib_playlist_xfer = 0U;
        s->lib_playlists_sent = 0U;
        s->summary.lib_tracks_sent = 0U;
        s->summary.lib_tracks_total = 0U;
        s->summary.lib_playlists_sent = 0U;
        s->summary.lib_playlists_total = 0U;

        s->library_pending |= (LIB_PEND_FLAG8 | LIB_PEND_FLAG9);
        s->lib_subscribed = true;

        if (s->ops.on_library_start != NULL) {
            s->lib_phase = IAP2_LIB_WAIT_BEGIN;
            s->summary.lib_phase = IAP2_LIB_WAIT_BEGIN;
            flush_library(s);
            s->ops.on_library_start(s->ops.context, s->library_mask, s->car_lib_rev);
        } else {
            s->library_pending |= LIB_PEND_ITEMS;
            s->lib_phase = IAP2_LIB_DONE;
            s->summary.lib_phase = IAP2_LIB_DONE;
            flush_library(s);
        }
        break;
    }
    case IAP2_MSG_STOP_LIBRARY_UPDATES:
        cancel_library_sync(s, true);
        break;
    case IAP2_MSG_PLAY_LIBRARY_CURRENT:
        s->summary.play_cmd_count++;
        s->summary.last_play_cmd_id = msg->id;
        if (!suppress_connect_play_cmd(s, now_ms) &&
            s->ops.on_play_library_current != NULL) {
            s->ops.on_play_library_current(s->ops.context);
        }
        break;
    case IAP2_MSG_PLAY_LIBRARY_ITEMS:
        s->summary.play_cmd_count++;
        s->summary.last_play_cmd_id = msg->id;
        v = find_param(p, plen, 0U, &n);
        if (v != NULL && n >= 8U) {
            size_t cnt = n / 8U;
            if (cnt > IAP2_LIB_MAX_PLAY_ITEMS) {
                cnt = IAP2_LIB_MAX_PLAY_ITEMS;
            }
            for (size_t i = 0U; i < cnt; i++) {
                s->play_items_scratch[i] = be64(v + i * 8U);
            }
            size_t sn = 0U;
            const uint8_t *sv = find_param(p, plen, 1U, &sn);
            uint32_t start_idx = (sv != NULL && sn == 4U) ? be32(sv) : 0U;
            if (suppress_connect_play_cmd(s, now_ms)) {
                break;
            }
            uint64_t target_id =
                s->play_items_scratch[(start_idx < cnt) ? start_idx : 0U];
            if (s->last_play_items_valid &&
                (uint32_t)(now_ms - s->last_play_items_ms) < IAP2_PLAY_ITEMS_DEDUP_MS &&
                s->last_play_items_target_id == target_id &&
                s->last_play_items_count == cnt) {
                s->summary.play_cmd_suppressed++;
                s->np_full = true;
                break;
            }
            s->last_play_items_ms = now_ms;
            s->last_play_items_target_id = target_id;
            s->last_play_items_count = cnt;
            s->last_play_items_valid = true;
            if (s->ops.on_play_library_items != NULL) {
                s->ops.on_play_library_items(s->ops.context, s->play_items_scratch,
                                             cnt, start_idx);
            }
        }
        break;
    case IAP2_MSG_PLAY_LIBRARY_COLLECTION: {
        s->summary.play_cmd_count++;
        s->summary.last_play_cmd_id = msg->id;
        if (!suppress_connect_play_cmd(s, now_ms) &&
            s->ops.on_play_library_collection != NULL) {
            uint64_t cid = 0U;
            v = find_param(p, plen, 0U, &n);
            if (v != NULL && n == 8U) {
                cid = be64(v);
            } else if (v != NULL && n == 4U) {
                cid = (uint64_t)be32(v);
            }
            uint8_t ctype = 0U;
            v = find_param(p, plen, 1U, &n);
            if (v != NULL && n >= 1U) {
                ctype = v[0];
            }
            uint32_t start_idx = 0U;
            v = find_param(p, plen, 2U, &n);
            if (v != NULL && n == 4U) {
                start_idx = be32(v);
            }
            s->ops.on_play_library_collection(s->ops.context, cid, ctype, start_idx);
        }
        break;
    }
    case IAP2_MSG_PLAY_LIBRARY_SPECIAL: {
        s->summary.play_cmd_count++;
        s->summary.last_play_cmd_id = msg->id;
        if (!suppress_connect_play_cmd(s, now_ms) &&
            s->ops.on_play_library_special != NULL) {
            uint8_t stype = 0U;
            v = find_param(p, plen, 1U, &n);
            if (v != NULL && n >= 1U) {
                stype = v[0];
            }
            uint64_t start_id = 0U;
            v = find_param(p, plen, 2U, &n);
            if (v != NULL && n == 8U) {
                start_id = be64(v);
            }
            s->ops.on_play_library_special(s->ops.context, stype, start_id);
        }
        break;
    }
    case IAP2_MSG_START_USB_AUDIO: {
        s->audio_on = true;
        s->summary.audio_active = true;
        uint32_t rate = (s->media.sample_rate != 0U) ? s->media.sample_rate : 44100U;
        if (!send_usb_audio_rate(s, rate)) {
            s->rate_pending = true;
            if (s->ops.on_usb_audio != NULL) {
                s->ops.on_usb_audio(s->ops.context, true, rate);
            }
        }
        break;
    }
    case IAP2_MSG_STOP_USB_AUDIO:
        if (s->audio_on) {
            s->audio_on = false;
            s->rate_pending = false;
            s->summary.audio_active = false;
            if (s->ops.on_usb_audio != NULL) {
                s->ops.on_usb_audio(s->ops.context, false, s->rate_sent);
            }
        }
        break;
    case IAP2_MSG_START_NOW_PLAYING:
        v = find_param(p, plen, 0U, &n);
        s->media_mask = wanted_mask(v, n, IAP2_DEFAULT_MEDIA_MASK);
        v = find_param(p, plen, 1U, &n);
        s->playback_mask = wanted_mask(v, n, IAP2_DEFAULT_PLAYBACK_MASK);
        s->summary.media_mask = s->media_mask;
        s->summary.playback_mask = s->playback_mask;
        s->now_playing = true;
        s->np_full = true;
        s->summary.now_playing_active = true;
        poll_now_playing(s, now_ms);
        break;
    case IAP2_MSG_STOP_NOW_PLAYING:
        s->now_playing = false;
        s->summary.now_playing_active = false;
        s->queue_pending = false;
        if (s->lx_phase != IAP2_LX_IDLE && s->lx_is_queue) {
            finish_active_lx(s, false, true);
        } else {
            release_q_keys(s, false);
        }
        iap2_session_clear_artwork(s);
        break;
    case IAP2_MSG_SET_NOW_PLAYING:
        v = find_param(p, plen, 0U, &n);
        if (v != NULL && n == 4U && s->ops.on_seek_ms != NULL) {
            s->ops.on_seek_ms(s->ops.context, be32(v));
        }
        break;
    case IAP2_MSG_START_HID:
        v = find_param(p, plen, 0U, &n);
        s->hid_component = (v != NULL && n == 2U) ? be16(v) : 0U;
        v = find_param(p, plen, 4U, &n);
        if (v != NULL) {
            parse_hid_descriptor(s, v, n);
        }
        if (s->availability_due_armed || s->carplay_pending) {
            s->carplay_pending = true;
            flush_carplay_availability(s);
        }
        break;
    case IAP2_MSG_ACCESSORY_HID_REPORT:
        v = find_param(p, plen, 1U, &n);
        if (v != NULL) {
            take_hid_report(s, v, n);
        }
        break;
    case IAP2_MSG_STOP_HID:
        s->hid_control_count = 0U;
        s->summary.hid_active = false;
        s->summary.hid_control_count = 0U;
        break;
    default:
        break;
    }
}

static void sync_link_state(struct iap2_session *s)
{
    if (s->link == NULL) {
        return;
    }
    bool up = s->link->link_up && !s->link->tx_stale;
    if (!up && s->link_was_up) {
        iap2_session_reset(s);
    }
    if (up && !s->auth_requested && link_can_queue(s->link)) {
        if (send_empty_ctrl(s, IAP2_MSG_REQUEST_CERTIFICATE)) {
            s->auth_requested = true;
            s->summary.phase = IAP2_SESSION_PHASE_AUTH_WAIT;
        }
    }
    s->link_was_up = up;
}

void iap2_session_init(struct iap2_session *session,
                       struct iap2_link *link,
                       const struct iap2_session_ops *ops)
{
    if (session == NULL) {
        return;
    }
    s_memset(session, 0, sizeof(*session));
    session->link = link;
    if (ops != NULL) {
        session->ops = *ops;
    }
    session->media.battery_pct = 100U;
    session->media.charging = true;
    session->media.sample_rate = 44100U;
    session->bt_component = -1;
    session->next_transfer_id = 0x80U;
    session->media_synced = true;
    iap2_control_stream_init(&session->control_stream,
                             session->control_rx_buf,
                             sizeof(session->control_rx_buf));
}

void iap2_session_reset(struct iap2_session *session)
{
    if (session == NULL) {
        return;
    }
    bool was_audio = session->audio_on;
    uint32_t last_rate = session->rate_sent;
    bool had_art = (session->art_data != NULL);
    uint64_t art_key = session->art_data_key;

    bool had_batch = (session->lib_batch_items != NULL);
    uint32_t batch_id = session->lib_batch_id;
    bool had_pl_keys = session->pl_keys_ready;
    size_t pl_idx = session->pl_keys_index;
    bool had_q_keys = (session->queue_keys != NULL);
    bool was_lib = session->lib_subscribed;

    struct iap2_link *link = session->link;
    struct iap2_session_ops ops = session->ops;
    struct iap2_media_state media = session->media;
    uint32_t prng = session->prng_state;

    s_memset(session, 0, sizeof(*session));
    session->link = link;
    session->ops = ops;
    session->media = media;
    session->prng_state = prng;
    session->bt_component = -1;
    session->next_transfer_id = 0x80U;
    session->media_synced = true;
    iap2_control_stream_init(&session->control_stream,
                             session->control_rx_buf,
                             sizeof(session->control_rx_buf));

    if (had_art && ops.on_artwork_done != NULL) {
        ops.on_artwork_done(ops.context, art_key, false);
    }
    if (had_batch && ops.on_library_batch_done != NULL) {
        ops.on_library_batch_done(ops.context, batch_id);
    }
    if (had_pl_keys && ops.on_playlist_keys_done != NULL) {
        ops.on_playlist_keys_done(ops.context, pl_idx, false);
    }
    if (had_q_keys && ops.on_queue_keys_done != NULL) {
        ops.on_queue_keys_done(ops.context, false);
    }
    if (was_lib && ops.on_library_stop != NULL) {
        ops.on_library_stop(ops.context);
    }
    if (was_audio && ops.on_usb_audio != NULL) {
        ops.on_usb_audio(ops.context, false, last_rate);
    }
}

void iap2_session_mark_media_pending(struct iap2_session *session)
{
    if (session == NULL) {
        return;
    }
    session->media_synced = false;
    session->media.has_track = false;
    session->media.play_state = IAP2_PLAY_STOPPED;
}

void iap2_session_set_media(struct iap2_session *session,
                            const struct iap2_media_state *media)
{
    if (session == NULL || media == NULL) {
        return;
    }
    uint32_t new_rate = (media->sample_rate != 0U) ? media->sample_rate : 44100U;
    session->media = *media;
    session->media_synced = true;
    session->media.device_name[sizeof(session->media.device_name) - 1U] = '\0';
    session->media.title[sizeof(session->media.title) - 1U] = '\0';
    session->media.artist[sizeof(session->media.artist) - 1U] = '\0';
    session->media.album[sizeof(session->media.album) - 1U] = '\0';
    session->media.album_artist[sizeof(session->media.album_artist) - 1U] = '\0';
    session->media.genre[sizeof(session->media.genre) - 1U] = '\0';
    session->media.composer[sizeof(session->media.composer) - 1U] = '\0';
    if (session->audio_on && new_rate != session->rate_sent) {
        session->rate_pending = true;
    }
}

void iap2_session_clear_artwork(struct iap2_session *session)
{
    if (session == NULL) {
        return;
    }
    abort_active_artwork(session, true);
    release_art_data(session, false);
}

void iap2_session_set_artwork(struct iap2_session *session,
                              uint64_t track_key,
                              const uint8_t *jpeg,
                              size_t jpeg_len)
{
    if (session == NULL) {
        return;
    }
    if (jpeg == NULL || jpeg_len == 0U || jpeg_len > UINT32_MAX) {
        iap2_session_clear_artwork(session);
        return;
    }

    if (session->art_phase != IAP2_ART_IDLE) {
        if (!session->art_announced_real &&
            session->art_phase == IAP2_ART_SETUP_PENDING &&
            session->np_key == track_key) {
            if (session->art_data != NULL) {
                release_art_data(session, false);
            }
            session->art_data_key = track_key;
            session->art_data = jpeg;
            session->art_data_len = (uint32_t)jpeg_len;
            session->art_size = (uint32_t)jpeg_len;
            session->art_announced_real = true;
            return;
        }
        if (!session->art_announced_real &&
            session->art_phase == IAP2_ART_WAIT_START &&
            session->np_key == track_key && session->art_id != 0U) {
            session->art_blank_id = session->art_id;
            abort_active_artwork(session, false);
        } else {
            abort_active_artwork(session, true);
        }
    }
    if (session->art_data != NULL) {
        release_art_data(session, false);
    }

    session->art_data_key = track_key;
    session->art_data = jpeg;
    session->art_data_len = (uint32_t)jpeg_len;

    if (session->identified && session->now_playing &&
        (session->media_mask & (1U << IAP2_MI_ARTWORK)) != 0U &&
        session->link != NULL && session->link->file_session != 0U &&
        session->media.has_track && track_key == session->np_key &&
        !session->np_full && !session->art_announced_real) {
        session->art_late_pending = true;
    }
}

void iap2_session_library_set_playlists(struct iap2_session *session,
                                        const struct iap2_library_playlist *lists,
                                        size_t count)
{
    if (session == NULL) {
        return;
    }
    if (lists == NULL || count == 0U) {
        session->lib_playlist_count = 0U;
        session->summary.lib_playlists_total = 0U;
        return;
    }
    if (count > IAP2_LIB_MAX_PLAYLISTS) {
        count = IAP2_LIB_MAX_PLAYLISTS;
    }
    for (size_t i = 0U; i < count; i++) {
        session->lib_playlists[i] = lists[i];
        session->lib_playlists[i].name[sizeof(session->lib_playlists[i].name) - 1U] = '\0';
        session->lib_playlist_xfer_ids[i] = 0U;
    }
    session->lib_playlist_count = count;
    session->summary.lib_playlists_total = (uint16_t)count;
}

void iap2_session_library_begin(struct iap2_session *session,
                                uint32_t total_tracks,
                                const char *revision)
{
    if (session == NULL || !session->identified || !session->lib_subscribed ||
        session->lib_phase != IAP2_LIB_WAIT_BEGIN) {
        return;
    }
    session->lib_tracks_total = total_tracks;
    session->lib_tracks_sent = 0U;
    session->summary.lib_tracks_total = total_tracks;
    session->summary.lib_tracks_sent = 0U;
    s_copy_cstr(session->lib_rev, sizeof(session->lib_rev),
                (revision != NULL && revision[0] != '\0') ? revision : "1");

    session->lib_playlist_rec = 0U;
    session->lib_playlist_xfer = 0U;
    session->lib_playlists_sent = 0U;
    session->lib_playlist_xfer_asked = false;
    session->summary.lib_playlists_sent = 0U;

    if (s_revision_matches(session->car_lib_rev, session->lib_rev)) {
        session->library_pending |= LIB_PEND_REV_DONE;
        session->lib_need_reset = false;
        session->lib_batch_eof = false;
        if (session->lib_want_playlists && session->lib_playlist_count > 0U) {
            session->lib_phase = IAP2_LIB_PLAYLIST_RECORDS;
        } else {
            session->lib_phase = IAP2_LIB_DONE;
        }
    } else {
        session->lib_need_reset = true;
        session->lib_batch_eof = false;
        session->lib_phase = IAP2_LIB_STREAMING_TRACKS;
    }
    session->summary.lib_phase = session->lib_phase;
}

bool iap2_session_library_supply_batch(struct iap2_session *session,
                                       uint32_t batch_id,
                                       const struct iap2_library_item *items,
                                       size_t count,
                                       bool eof)
{
    if (session == NULL || !session->lib_subscribed ||
        session->lib_phase != IAP2_LIB_STREAMING_TRACKS ||
        session->lib_batch_items != NULL ||
        (count > 0U && items == NULL)) {
        return false;
    }
    session->lib_batch_id = batch_id;
    session->lib_batch_items = (count > 0U) ? items : NULL;
    session->lib_batch_count = count;
    session->lib_batch_pos = 0U;
    session->lib_batch_eof = eof;
    return true;
}

bool iap2_session_library_supply_playlist_keys(struct iap2_session *session,
                                               size_t playlist_index,
                                               const uint64_t *keys,
                                               size_t count)
{
    if (session == NULL || !session->lib_subscribed ||
        session->lib_phase != IAP2_LIB_PLAYLIST_XFER ||
        playlist_index != session->lib_playlist_xfer ||
        session->pl_keys_ready ||
        (count > 0U && keys == NULL)) {
        return false;
    }
    session->pl_keys_index = playlist_index;
    session->pl_keys = (count > 0U) ? keys : NULL;
    session->pl_keys_count = count;
    session->pl_keys_ready = true;
    return true;
}

bool iap2_session_set_queue(struct iap2_session *session,
                            uint32_t sig,
                            const uint64_t *keys,
                            size_t count,
                            bool from_car)
{
    if (session == NULL || session->link == NULL ||
        !session->link->link_up || session->link->tx_stale ||
        !session->identified || !session->now_playing ||
        (session->playback_mask & (1U << IAP2_PB_QUEUE_LIST)) == 0U) {
        return false;
    }
    if (!from_car && count > 0U && keys == NULL) {
        return false;
    }
    if (session->lx_phase != IAP2_LX_IDLE && session->lx_is_queue) {
        finish_active_lx(session, false, true);
    } else if (session->queue_keys != NULL) {
        release_q_keys(session, false);
    }

    session->queue_sig = sig;
    session->queue_from_car = from_car;
    session->queue_keys = (!from_car && count > 0U) ? keys : NULL;
    session->queue_count = (!from_car && count > 0U) ? count : 0U;
    session->queue_pending = true;
    return true;
}

void iap2_session_rx_payload(struct iap2_session *session,
                             uint8_t link_session,
                             const uint8_t *payload,
                             size_t len,
                             uint32_t now_ms)
{
    if (session == NULL || session->link == NULL || payload == NULL || len == 0U) {
        return;
    }
    sync_link_state(session);

    if (link_session == session->link->control_session) {
        struct dispatch_ctx ctx = { .session = session, .now_ms = now_ms };
        iap2_status_t st = iap2_control_feed(&session->control_stream, payload, len,
                                             on_control_message, &ctx);
        if (st == IAP2_STATUS_MALFORMED || st == IAP2_STATUS_BUFFER_TOO_SMALL) {
            iap2_control_stream_reset(&session->control_stream);
        }
    } else if (session->link->file_session != 0U &&
               link_session == session->link->file_session) {
        if (len >= 2U) {
            if (session->art_blank_id != 0U && payload[0] == session->art_blank_id) {
                if (payload[1] == 0x01U) {
                    (void)send_file_packet(session, session->art_blank_id, 0xC0U, NULL, 0U);
                }
                if (payload[1] == 0x01U || payload[1] == 0x02U) {
                    session->art_blank_id = 0U;
                }
                return;
            }
            if (session->art_id != 0U && payload[0] == session->art_id) {
                if (payload[1] == 0x01U && session->art_phase == IAP2_ART_WAIT_START) {
                    session->art_phase = IAP2_ART_SENDING;
                    session->summary.artwork_phase = IAP2_ART_SENDING;
                    flush_artwork_file(session, now_ms);
                } else if (payload[1] == 0x02U && session->art_phase != IAP2_ART_IDLE) {
                    abort_active_artwork(session, false);
                    release_art_data(session, false);
                }
                return;
            }
            if (session->lx_id != 0U && payload[0] == session->lx_id &&
                session->lx_phase != IAP2_LX_IDLE) {
                if (payload[1] == 0x01U && session->lx_phase == IAP2_LX_WAIT_START) {
                    session->lx_phase = IAP2_LX_SENDING;
                    flush_list_file(session, now_ms);
                } else if (payload[1] == 0x02U) {
                    finish_active_lx(session, false, false);
                }
            }
        }
    }
}

void iap2_session_tick(struct iap2_session *session, uint32_t now_ms)
{
    if (session == NULL || session->link == NULL) {
        return;
    }
    sync_link_state(session);
    if (!session->link->link_up || session->link->tx_stale) {
        return;
    }

    if (session->auth_due_armed && time_reached(now_ms, session->auth_due_ms)) {
        if (link_can_queue(session->link) &&
            send_empty_ctrl(session, IAP2_MSG_AUTHENTICATION_SUCCEEDED)) {
            session->auth_due_armed = false;
            session->authenticated = true;
            session->summary.authenticated = true;
            session->summary.phase = IAP2_SESSION_PHASE_IDENT_WAIT;
            if (session->ops.on_authenticated != NULL) {
                session->ops.on_authenticated(session->ops.context, true);
            }
            if (!send_empty_ctrl(session, IAP2_MSG_START_IDENTIFICATION)) {
                session->post_ident_pending |= 0x80U;
            }
        }
    }
    if ((session->post_ident_pending & 0x80U) != 0U && link_can_queue(session->link)) {
        if (send_empty_ctrl(session, IAP2_MSG_START_IDENTIFICATION)) {
            session->post_ident_pending &= (uint8_t)~0x80U;
        }
    }

    if (session->ident_due_armed && time_reached(now_ms, session->ident_due_ms)) {
        session->ident_due_armed = false;
        accept_identification(session, now_ms);
    }

    if (session->availability_due_armed &&
        time_reached(now_ms, session->availability_due_ms)) {
        session->availability_due_armed = false;
        session->carplay_pending = true;
    }

    flush_post_ident(session);
    flush_carplay_availability(session);
    flush_power_updates(session);
    flush_library(session);

    if (session->audio_on) {
        uint32_t wanted = (session->media.sample_rate != 0U)
            ? session->media.sample_rate : 44100U;
        if (session->rate_pending || wanted != session->rate_sent) {
            (void)send_usb_audio_rate(session, wanted);
        }
    }

    poll_now_playing(session, now_ms);
    flush_artwork_file(session, now_ms);
    flush_list_file(session, now_ms);
}
