// SPDX-License-Identifier: GPL-3.0-only
#ifndef IAP2_SESSION_H
#define IAP2_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "control.h"
#include "link.h"

#define IAP2_SESSION_RX_CAPACITY      4096U
#define IAP2_SESSION_TX_CAPACITY      IAP2_LINK_MAX_PACKET
#define IAP2_SESSION_MAX_CAR_MSGS     64U
#define IAP2_SESSION_MAX_HID_CONTROLS 24U
#define IAP2_SESSION_MAX_HID_REPORTS  4U
#define IAP2_SESSION_MAX_HID_REPORT_LEN 16U

#define IAP2_LIB_REVISION_MAX   32U
#define IAP2_LIB_MAX_PLAYLISTS  32U
#define IAP2_LIB_MAX_PLAY_ITEMS 256U
#define IAP2_RX_LOG_HEAD           8U
#define IAP2_RX_LOG_TAIL           4U
#define IAP2_CONNECT_PLAY_GUARD_MS 6000U
#define IAP2_PLAY_ITEMS_DEDUP_MS   1500U

#define IAP2_ART_IDLE          0U
#define IAP2_ART_SETUP_PENDING 1U
#define IAP2_ART_WAIT_START    2U
#define IAP2_ART_SENDING       3U

#define IAP2_LIB_IDLE             0U
#define IAP2_LIB_WAIT_BEGIN       1U
#define IAP2_LIB_STREAMING_TRACKS 2U
#define IAP2_LIB_PLAYLIST_RECORDS 3U
#define IAP2_LIB_PLAYLIST_XFER    4U
#define IAP2_LIB_DONE             5U

#define IAP2_LX_IDLE          0U
#define IAP2_LX_SETUP_PENDING 1U
#define IAP2_LX_WAIT_START    2U
#define IAP2_LX_SENDING       3U

typedef enum {
    IAP2_PLAY_STOPPED = 0,
    IAP2_PLAY_PLAYING = 1,
    IAP2_PLAY_PAUSED  = 2,
} iap2_play_state_t;

typedef enum {
    IAP2_REPEAT_OFF = 0,
    IAP2_REPEAT_ONE = 1,
    IAP2_REPEAT_ALL = 2,
} iap2_repeat_mode_t;

typedef enum {
    IAP2_REMOTE_NONE = 0,
    IAP2_REMOTE_PLAY_PAUSE,
    IAP2_REMOTE_PLAY,
    IAP2_REMOTE_PAUSE,
    IAP2_REMOTE_NEXT,
    IAP2_REMOTE_PREV,
    IAP2_REMOTE_STOP,
    IAP2_REMOTE_FFWD,
    IAP2_REMOTE_REW,
    IAP2_REMOTE_SHUFFLE_TOGGLE,
    IAP2_REMOTE_REPEAT_NEXT,
    IAP2_REMOTE_VOL_UP,
    IAP2_REMOTE_VOL_DOWN,
} iap2_remote_action_t;

typedef enum {
    IAP2_SESSION_PHASE_IDLE = 0,
    IAP2_SESSION_PHASE_AUTH_WAIT,
    IAP2_SESSION_PHASE_IDENT_WAIT,
    IAP2_SESSION_PHASE_READY,
} iap2_session_phase_t;

struct iap2_media_state {
    char device_name[64];
    uint8_t battery_pct;
    bool charging;
    uint32_t sample_rate;
    bool has_track;
    uint8_t play_state;
    uint32_t elapsed_ms;
    uint32_t duration_ms;
    uint64_t track_id;
    uint16_t track_number;
    uint16_t disc_number;
    uint32_t queue_index;
    uint32_t queue_count;
    bool shuffle;
    uint8_t repeat_mode;
    char title[128];
    char artist[128];
    char album[128];
    char album_artist[128];
    char genre[64];
    char composer[64];
};

struct iap2_library_item {
    uint64_t persistent_id;
    uint32_t duration_ms;
    uint16_t track_number;
    uint16_t disc_number;
    char title[96];
    char artist[64];
    char album[64];
    char album_artist[64];
    char genre[48];
    char composer[48];
};

struct iap2_library_playlist {
    uint64_t persistent_id;
    uint64_t parent_id;
    uint32_t track_count;
    uint8_t is_folder;
    char name[64];
};

struct iap2_session_summary {
    iap2_session_phase_t phase;
    bool authenticated;
    bool identified;
    bool audio_active;
    bool now_playing_active;
    bool hid_active;
    uint32_t audio_rate;
    uint32_t rx_messages;
    uint32_t tx_messages;
    uint16_t last_rx_msg_id;
    uint16_t last_tx_msg_id;
    uint16_t car_current_ma;
    uint8_t hid_control_count;
    uint32_t artwork_transfers;
    uint32_t last_artwork_bytes;
    uint8_t artwork_phase;
    uint32_t media_mask;
    uint32_t playback_mask;
    uint32_t library_mask;
    uint32_t lib_tracks_sent;
    uint32_t lib_tracks_total;
    uint16_t lib_playlists_sent;
    uint16_t lib_playlists_total;
    uint8_t lib_phase;
    uint16_t play_cmd_count;
    uint16_t play_cmd_suppressed;
    uint16_t last_play_cmd_id;
    char car_name[32];
    char car_model[32];
    char car_manufacturer[32];
    uint16_t rx_head_ids[IAP2_RX_LOG_HEAD];
    uint8_t rx_head_count;
    uint16_t rx_tail_ids[IAP2_RX_LOG_TAIL];
    uint8_t rx_tail_count;
};

struct iap2_session_ops {
    void *context;
    uint32_t (*random32)(void *context);
    void (*on_authenticated)(void *context, bool ok);
    void (*on_identified)(void *context, const char *device_name, uint16_t car_current_ma);
    void (*on_usb_audio)(void *context, bool active, uint32_t sample_rate);
    void (*on_hid_action)(void *context, iap2_remote_action_t action);
    void (*on_seek_ms)(void *context, uint32_t position_ms);
    void (*on_artwork_done)(void *context, uint64_t track_key, bool completed);
    void (*on_library_start)(void *context, uint32_t mask, const char *car_revision);
    void (*on_library_stop)(void *context);
    void (*on_library_batch_done)(void *context, uint32_t batch_id);
    void (*on_playlist_keys_request)(void *context, size_t playlist_index, uint64_t playlist_id);
    void (*on_playlist_keys_done)(void *context, size_t playlist_index, bool completed);
    void (*on_queue_keys_done)(void *context, bool completed);
    void (*on_play_library_current)(void *context);
    void (*on_play_library_items)(void *context, const uint64_t *ids, size_t count, uint32_t start_index);
    void (*on_play_library_collection)(void *context, uint64_t collection_id, uint8_t collection_type, uint32_t start_index);
    void (*on_play_library_special)(void *context, uint8_t special_type, uint64_t start_item_id);
};

struct iap2_hid_control {
    uint8_t report_id;
    uint16_t bit;
    uint8_t size;
    uint8_t count;
    uint16_t usage;
};

struct iap2_session {
    struct iap2_link *link;
    struct iap2_session_ops ops;
    struct iap2_media_state media;
    struct iap2_control_stream control_stream;
    uint8_t control_rx_buf[IAP2_SESSION_RX_CAPACITY];

    bool link_was_up;
    bool auth_requested;
    bool authenticated;
    bool auth_due_armed;
    uint32_t auth_due_ms;

    bool identified;
    bool ident_due_armed;
    uint32_t ident_due_ms;
    uint32_t identified_ms;

    uint32_t last_play_items_ms;
    uint64_t last_play_items_target_id;
    size_t last_play_items_count;
    bool last_play_items_valid;

    bool availability_due_armed;
    uint32_t availability_due_ms;
    bool carplay_pending;

    uint16_t car_receives[IAP2_SESSION_MAX_CAR_MSGS];
    size_t car_receives_count;
    int32_t bt_component;
    uint16_t car_current_ma;
    uint16_t hid_component;

    uint8_t post_ident_pending;
    uint8_t power_pending_mask;
    uint8_t library_pending;
    uint32_t library_mask;

    bool lib_subscribed;
    bool lib_want_playlists;
    bool lib_need_reset;
    uint8_t lib_phase;
    char car_lib_rev[IAP2_LIB_REVISION_MAX];
    char lib_rev[IAP2_LIB_REVISION_MAX];
    uint32_t lib_tracks_total;
    uint32_t lib_tracks_sent;

    const struct iap2_library_item *lib_batch_items;
    size_t lib_batch_count;
    size_t lib_batch_pos;
    uint32_t lib_batch_id;
    bool lib_batch_eof;

    struct iap2_library_playlist lib_playlists[IAP2_LIB_MAX_PLAYLISTS];
    uint8_t lib_playlist_xfer_ids[IAP2_LIB_MAX_PLAYLISTS];
    size_t lib_playlist_count;
    size_t lib_playlist_rec;
    size_t lib_playlist_xfer;
    uint16_t lib_playlists_sent;
    bool lib_playlist_xfer_asked;

    const uint64_t *pl_keys;
    size_t pl_keys_count;
    size_t pl_keys_index;
    bool pl_keys_ready;

    bool queue_pending;
    bool queue_from_car;
    uint32_t queue_sig;
    const uint64_t *queue_keys;
    size_t queue_count;

    uint8_t lx_phase;
    uint8_t lx_id;
    uint8_t lx_cancel_id;
    bool lx_is_queue;
    size_t lx_pl_index;
    uint64_t lx_pl_id;
    const uint64_t *lx_keys;
    size_t lx_count;
    size_t lx_sent;
    uint32_t lx_started_ms;

    uint64_t play_items_scratch[IAP2_LIB_MAX_PLAY_ITEMS];

    bool audio_on;
    bool rate_pending;
    uint32_t rate_sent;

    bool now_playing;
    bool np_full;
    bool media_synced;
    uint32_t media_mask;
    uint32_t playback_mask;
    uint64_t np_key;
    uint8_t np_state;
    uint8_t np_modes;
    uint32_t np_elapsed_ms;
    uint32_t np_tick_ms;

    uint8_t file_tx_buf[IAP2_SESSION_TX_CAPACITY];
    uint8_t next_transfer_id;
    uint8_t art_phase;
    uint8_t art_id;
    uint8_t art_cancel_id;
    uint8_t art_blank_id;
    bool art_announced_real;
    bool art_late_pending;
    uint64_t art_data_key;
    const uint8_t *art_data;
    uint32_t art_data_len;
    uint32_t art_size;
    uint32_t art_sent;
    uint32_t art_started_ms;

    bool hid_report_ids;
    struct iap2_hid_control hid_controls[IAP2_SESSION_MAX_HID_CONTROLS];
    size_t hid_control_count;
    uint8_t last_report_id[IAP2_SESSION_MAX_HID_REPORTS];
    uint8_t last_report[IAP2_SESSION_MAX_HID_REPORTS][IAP2_SESSION_MAX_HID_REPORT_LEN];

    uint32_t prng_state;
    struct iap2_session_summary summary;
};

void iap2_session_init(struct iap2_session *session,
                       struct iap2_link *link,
                       const struct iap2_session_ops *ops);
void iap2_session_reset(struct iap2_session *session);
void iap2_session_mark_media_pending(struct iap2_session *session);
void iap2_session_set_media(struct iap2_session *session,
                            const struct iap2_media_state *media);
void iap2_session_set_artwork(struct iap2_session *session,
                              uint64_t track_key,
                              const uint8_t *jpeg,
                              size_t jpeg_len);
void iap2_session_clear_artwork(struct iap2_session *session);
void iap2_session_library_begin(struct iap2_session *session,
                                uint32_t total_tracks,
                                const char *revision);
bool iap2_session_library_supply_batch(struct iap2_session *session,
                                       uint32_t batch_id,
                                       const struct iap2_library_item *items,
                                       size_t count,
                                       bool eof);
void iap2_session_library_set_playlists(struct iap2_session *session,
                                        const struct iap2_library_playlist *lists,
                                        size_t count);
bool iap2_session_library_supply_playlist_keys(struct iap2_session *session,
                                               size_t playlist_index,
                                               const uint64_t *keys,
                                               size_t count);
bool iap2_session_set_queue(struct iap2_session *session,
                            uint32_t sig,
                            const uint64_t *keys,
                            size_t count,
                            bool from_car);
void iap2_session_rx_payload(struct iap2_session *session,
                             uint8_t link_session,
                             const uint8_t *payload,
                             size_t len,
                             uint32_t now_ms);
void iap2_session_tick(struct iap2_session *session, uint32_t now_ms);

#endif /* IAP2_SESSION_H */
