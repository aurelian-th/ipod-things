/* SPDX-License-Identifier: GPL-3.0-only */

/* USB callbacks run serialized on USBDeviceTask. State stays in the module
 * image until core retirement; copied observations retain no native pointers.
 * rx_tap and rx_bounce are owned exclusively by USBDeviceTask; UI callbacks
 * only read snapshots copied from diag_state under short IRQ masking. Never
 * invoke api->hook_next, api->extension_hold, api->extension_release,
 * api->ui_refresh, or api->setting_int while IRQs are masked. */

#include "iap2.h"
#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_firmware.h"
#include "rv_menus.h"
#include "rv_sites.h"
#include "rv_ui.h"

#define KEY_PROTOCOL_MODE 4
#ifdef __arm__
#include "artwork.h"
#include "native_tx.h"
#include "rv_libc.h"
#include "rv_media.h"
#endif

#ifndef RV_HOOK_ARG
#define RV_HOOK_ARG(p) ((uint32_t)(uintptr_t)(p))
#endif

static const struct rv_api *api;
static rv_extension_handle self;
static struct iap2_diag_state diag_state;
static struct iap2_rx_tap rx_tap;
static uint8_t rx_bounce[IAP2_HID_HS_MAX_REPORT_LEN];
static uintptr_t active_hid_cookie;
static volatile uint8_t stopping;

#ifdef __arm__
static struct iap2_native_tx native_tx;
static struct iap2_session ctrl_session;
static uintptr_t blocked_hid;
static uint8_t tx_hooks_bound;
static struct rv_timer *media_timer;

static struct iap2_media_state shared_media;
static uint32_t shared_media_gen;
static uint32_t session_media_gen;
static uint32_t pending_actions_mask;
static uint32_t pending_seek_target_ms;
static uint8_t pending_seek_valid;

static uint8_t play_cmd_grace_ticks;
static uint8_t last_ui_mode;
static uint8_t last_ui_usb_conn;
static uint8_t last_ui_tap_active;
static uint8_t last_ui_tx_active;
static uint8_t last_ui_replug;
static uint8_t last_ui_phase;
static uint8_t last_ui_audio_active;
static uint32_t last_ui_audio_rate;
static uint8_t digital_audio_primed;
static uint8_t usb_audio_route_wanted;
static uint8_t usb_audio_route_applied;

enum {
  ART_SLOT_IDLE = 0,
  ART_SLOT_READY = 1,
  ART_SLOT_IN_USE = 2,
};

enum {
  LIB_BEGIN_IDLE = 0,
  LIB_BEGIN_READY = 1,
  LIB_BEGIN_IN_USE = 2,
};

enum {
  LIB_BATCH_IDLE = 0,
  LIB_BATCH_READY = 1,
  LIB_BATCH_IN_USE = 2,
};

enum {
  PL_SLOT_IDLE = 0,
  PL_SLOT_READY = 1,
  PL_SLOT_IN_USE = 2,
};

enum {
  Q_SLOT_IDLE = 0,
  Q_SLOT_READY = 1,
  Q_SLOT_IN_USE = 2,
};

enum {
  PLAY_CMD_NONE = 0,
  PLAY_CMD_ITEMS = 1,
  PLAY_CMD_COLLECTION = 2,
  PLAY_CMD_SPECIAL = 3,
};

#define LIB_BATCH_SIZE 32U
#define PL_KEYS_CAP 512U
#define Q_KEYS_CAP 256U
#define PLAY_MATCH_CAP 512U

static uint8_t *art_buf;
static uint8_t art_slot_state;
static uint8_t art_cancel_req;
static uint64_t art_slot_key;
static size_t art_slot_len;
static void *last_art_record;
static uint32_t last_art_gen;
static uint32_t last_art_session_gen;

static uint8_t lib_req_active;
static uint32_t lib_req_gen;

static uint8_t lib_begin_state;
static uint32_t lib_begin_gen;
static uint32_t lib_begin_total;
static char lib_begin_rev[IAP2_LIB_REVISION_MAX];
static struct iap2_library_playlist lib_begin_playlists[IAP2_LIB_MAX_PLAYLISTS];
static size_t lib_begin_pl_count;

static struct iap2_library_item *lib_batch_buf;
static uint8_t lib_batch_state;
static uint32_t lib_batch_gen;
static uint32_t lib_batch_id;
static size_t lib_batch_count;
static uint8_t lib_batch_eof;

static uint32_t ui_lib_gen;
static void *ui_lib_cursor_rec;
static uint32_t ui_lib_next_batch_id;
static uint8_t ui_lib_done;

static uint8_t pl_req_pending;
static uint32_t pl_req_gen;
static size_t pl_req_index;
static uint64_t pl_req_id;
static uint64_t *pl_keys_buf;
static uint8_t pl_slot_state;
static uint32_t pl_slot_gen;
static size_t pl_slot_index;
static size_t pl_slot_count;

static uint64_t *q_keys_buf;
static uint8_t q_slot_state;
static uint32_t q_slot_sig;
static size_t q_slot_count;
static uint8_t q_slot_from_car;

static uint32_t q_seen_sig;
static uint32_t q_sent_sig;
static uint32_t q_stable_ticks;
static uint8_t ui_queue_from_car;
static uint32_t last_q_session_gen;

static uint8_t pending_play_kind;
static size_t pending_play_count;
static uint32_t pending_play_start_idx;
static uint64_t pending_play_coll_id;
static uint8_t pending_play_coll_type;
static uint8_t pending_play_special_type;
static uint64_t pending_play_start_item_id;
static uint64_t pending_play_items[IAP2_LIB_MAX_PLAY_ITEMS];

static uint64_t ui_play_items[IAP2_LIB_MAX_PLAY_ITEMS];
static void *play_matched_recs[PLAY_MATCH_CAP];

static inline uint8_t media_player_ready(void) {
  return (MediaPlayer_Instance != NULL) ? 1U : 0U;
}

static inline uint64_t read_u64_le_safe(const void *base, size_t off) {
  const uint8_t *p = (const uint8_t *)base + off;
  uint32_t lo = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  uint32_t hi = (uint32_t)p[4] | ((uint32_t)p[5] << 8) |
                ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
  return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static inline uint64_t read_record_persistent_id(const void *rec) {
  if (rec == NULL)
    return 0U;
  uint64_t pid = read_u64_le_safe(rec, MediaRecord_persistent_id);
  return (pid != 0U) ? pid : (uint64_t)(uintptr_t)rec;
}

static inline uint8_t is_valid_track_data(const void *td) {
  return (td != NULL &&
          RV_FIELD(td, TrackData_magic, uint32_t) == TrackData_MagicValue)
             ? 1U
             : 0U;
}

static inline uint8_t is_valid_media_record(const void *rec, const void *td) {
  return (rec != NULL && td != NULL &&
          RV_FIELD(rec, MediaRecord_track_data, void *) == td)
             ? 1U
             : 0U;
}

static inline uint8_t is_music_record(const void *rec) {
  if (rec == NULL)
    return 0U;
  uint32_t k = RV_FIELD(rec, MediaRecord_media_kind, uint32_t);
  uint32_t excl = MediaKind_Movie | MediaKind_MusicVideo | MediaKind_TVShow |
                  MediaKind_Rental;
  return ((k == 0U || (k & MediaKind_Music) != 0U) && (k & excl) == 0U &&
          RV_FIELD(rec, MediaRecord_entries, void *) != NULL)
             ? 1U
             : 0U;
}

static uint32_t fnv1a_32_arm(const char *s) {
  uint32_t h = 2166136261U;
  if (s == NULL)
    return 1U;
  while (*s != '\0') {
    h = (h ^ (uint8_t)*s++) * 16777619U;
  }
  return (h != 0U) ? h : 1U;
}

static uint32_t fnv1a_u64_step(uint32_t h, uint64_t v) {
  for (unsigned i = 0U; i < 8U; i++) {
    h = (h ^ (uint8_t)(v & 0xFFU)) * 16777619U;
    v >>= 8;
  }
  return h;
}

static uint8_t arm_get_library_handles(void **out_owner, void **out_td,
                                       struct MediaList ***out_lists,
                                       uint32_t *out_list_count) {
  if (out_owner)
    *out_owner = NULL;
  if (out_td)
    *out_td = NULL;
  if (out_lists)
    *out_lists = NULL;
  if (out_list_count)
    *out_list_count = 0U;

  if (App == NULL)
    return 0U;
  void *browser = RV_FIELD(App, App_browser, void *);
  if (browser == NULL)
    return 0U;
  void *owner = MediaBrowser_GetOwner(browser);
  if (owner == NULL)
    return 0U;
  void *td = RV_FIELD(owner, MediaBrowser_track_data, void *);
  if (!is_valid_track_data(td))
    return 0U;

  struct MediaList **lists =
      RV_FIELD(owner, MediaBrowser_library, struct MediaList **);
  uint32_t list_count = RV_FIELD(owner, MediaBrowser_library_count, uint32_t);
  if (lists == NULL || list_count > 4096U)
    list_count = 0U;

  if (out_owner)
    *out_owner = owner;
  if (out_td)
    *out_td = td;
  if (out_lists)
    *out_lists = lists;
  if (out_list_count)
    *out_list_count = list_count;
  return 1U;
}

static uint8_t can_start_playback(void) {
  if (media_player_ready())
    return 1U;
  return (ArtworkDB_Instance != NULL &&
          arm_get_library_handles(NULL, NULL, NULL, NULL))
             ? 1U
             : 0U;
}

static void diag_sample_audio_fields_locked(void) {
  diag_state.audio_route_usb = RV_FIELD(
      AudioOutputRouteState, AudioOutputRouteState_usb_selected, uint8_t);
  diag_state.audio_route_applied = usb_audio_route_applied;
  diag_state.usb_audio_streaming =
      RV_FIELD(USBAudioState, USBAudioState_streaming, uint8_t);
  diag_state.usb_pcm_ready = (USBAudio_PcmCallback != NULL) ? 1U : 0U;
  diag_state.usb_host_rate =
      RV_FIELD(USBAudioState, USBAudioState_host_rate, uint32_t);
  diag_state.digital_active_rate =
      RV_FIELD(DigitalAudioState, DigitalAudioState_active_rate, uint32_t);
}

static void copy_cstr(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0U)
    return;
  size_t i = 0U;
  if (src) {
    while (i + 1U < cap && src[i] != '\0') {
      dst[i] = src[i];
      i++;
    }
  }
  dst[i] = '\0';
}

static uint32_t usb_now_ms(void) {
  static uint32_t last_us;
  static uint32_t total_ms;
  static uint32_t rem_us;
  static uint8_t init;
  uint32_t us = 0U;
  Clock_ReadMicroseconds(&us);
  if (!init) {
    last_us = us;
    init = 1U;
    return total_ms;
  }
  uint32_t delta = us - last_us;
  last_us = us;
  rem_us += delta;
  if (rem_us >= 1000U) {
    total_ms += rem_us / 1000U;
    rem_us %= 1000U;
  }
  return total_ms;
}

static void prime_digital_audio(uint32_t rate) {
  RV_FIELD(DigitalAudioState, DigitalAudioState_authenticated, uint8_t) = 1U;
  RV_FIELD(DigitalAudioState, DigitalAudioState_supported_rates, uint32_t) =
      DigitalAudio_AllRatesMask;
  RV_FIELD(DigitalAudioState, DigitalAudioState_active_rate, uint32_t) =
      rate ? rate : DigitalAudio_DefaultRate;
  digital_audio_primed = 1U;
}

static void unprime_digital_audio(void) {
  RV_FIELD(DigitalAudioState, DigitalAudioState_authenticated, uint8_t) = 0U;
  RV_FIELD(DigitalAudioState, DigitalAudioState_supported_rates, uint32_t) = 0U;
  digital_audio_primed = 0U;
}

static void arm_apply_usb_audio_route(uint8_t enable) {
  (void)AudioOutputDevice_Get();
  uint32_t payload[3] = {
      enable ? DigitalAudioEvent_Connected : DigitalAudioEvent_Disconnected,
      0U,
      0U,
  };
  (void)NotificationCenter_Post(NotificationTopic_DigitalAudio, 0U, payload,
                                12U);
  usb_audio_route_applied = enable ? 1U : 0U;
}

static void sess_on_rx_payload(void *ctx, uint8_t sess_id,
                               const uint8_t *payload, size_t len,
                               uint32_t now_ms) {
  (void)ctx;
  if (!diag_state.tx_active)
    return;
  iap2_session_rx_payload(&ctrl_session, sess_id, payload, len, now_ms);
}

static void sess_on_rx_reset(void *ctx) {
  (void)ctx;
  iap2_session_reset(&ctrl_session);
}

static uint32_t sess_random32(void *ctx) {
  (void)ctx;
  uint32_t us = 0U;
  Clock_ReadMicroseconds(&us);
  return us ^ 0x5A5A9696U;
}

static void sess_on_usb_audio(void *ctx, bool active, uint32_t rate) {
  (void)ctx;
  if (active)
    prime_digital_audio(rate);
  else
    unprime_digital_audio();
  uint32_t irq = api->irq_save();
  if (!stopping)
    usb_audio_route_wanted = active ? 1U : 0U;
  api->irq_restore(irq);
}

static void sess_on_artwork_done(void *ctx, uint64_t track_key,
                                 bool completed) {
  (void)ctx;
  (void)track_key;
  (void)completed;
  uint32_t irq = api->irq_save();
  if (art_slot_state == ART_SLOT_IN_USE) {
    art_slot_state = ART_SLOT_IDLE;
    art_cancel_req = 0U;
  }
  api->irq_restore(irq);
}

static void sess_on_hid_action(void *ctx, iap2_remote_action_t action) {
  (void)ctx;
  if (action <= IAP2_REMOTE_NONE || (unsigned)action >= 32U)
    return;
  uint32_t irq = api->irq_save();
  pending_actions_mask |= (1U << (uint32_t)action);
  api->irq_restore(irq);
}

static void sess_on_seek_ms(void *ctx, uint32_t position_ms) {
  (void)ctx;
  uint32_t irq = api->irq_save();
  pending_seek_target_ms = position_ms;
  pending_seek_valid = 1U;
  api->irq_restore(irq);
}

static void sess_on_library_start(void *ctx, uint32_t mask,
                                  const char *car_revision) {
  (void)ctx;
  (void)mask;
  (void)car_revision;
  uint32_t irq = api->irq_save();
  lib_req_gen++;
  lib_req_active = 1U;
  if (lib_begin_state == LIB_BEGIN_READY)
    lib_begin_state = LIB_BEGIN_IDLE;
  if (lib_batch_state == LIB_BATCH_READY)
    lib_batch_state = LIB_BATCH_IDLE;
  pl_req_pending = 0U;
  if (pl_slot_state == PL_SLOT_READY)
    pl_slot_state = PL_SLOT_IDLE;
  api->irq_restore(irq);
}

static void sess_on_library_stop(void *ctx) {
  (void)ctx;
  uint32_t irq = api->irq_save();
  lib_req_gen++;
  lib_req_active = 0U;
  if (lib_begin_state == LIB_BEGIN_READY)
    lib_begin_state = LIB_BEGIN_IDLE;
  if (lib_batch_state == LIB_BATCH_READY)
    lib_batch_state = LIB_BATCH_IDLE;
  pl_req_pending = 0U;
  if (pl_slot_state == PL_SLOT_READY)
    pl_slot_state = PL_SLOT_IDLE;
  api->irq_restore(irq);
}

static void sess_on_library_batch_done(void *ctx, uint32_t batch_id) {
  (void)ctx;
  (void)batch_id;
  uint32_t irq = api->irq_save();
  if (lib_batch_state == LIB_BATCH_IN_USE)
    lib_batch_state = LIB_BATCH_IDLE;
  api->irq_restore(irq);
}

static void sess_on_playlist_keys_request(void *ctx, size_t playlist_index,
                                          uint64_t playlist_id) {
  (void)ctx;
  uint32_t irq = api->irq_save();
  if (lib_req_active) {
    pl_req_gen = lib_req_gen;
    pl_req_index = playlist_index;
    pl_req_id = playlist_id;
    pl_req_pending = 1U;
  }
  api->irq_restore(irq);
}

static void sess_on_playlist_keys_done(void *ctx, size_t playlist_index,
                                       bool completed) {
  (void)ctx;
  (void)playlist_index;
  (void)completed;
  uint32_t irq = api->irq_save();
  if (pl_slot_state == PL_SLOT_IN_USE)
    pl_slot_state = PL_SLOT_IDLE;
  api->irq_restore(irq);
}

static void sess_on_queue_keys_done(void *ctx, bool completed) {
  (void)ctx;
  (void)completed;
  uint32_t irq = api->irq_save();
  if (q_slot_state == Q_SLOT_IN_USE)
    q_slot_state = Q_SLOT_IDLE;
  api->irq_restore(irq);
}

static void sess_on_play_library_items(void *ctx, const uint64_t *ids,
                                       size_t count, uint32_t start_index) {
  (void)ctx;
  if (ids == NULL || count == 0U)
    return;
  if (count > IAP2_LIB_MAX_PLAY_ITEMS)
    count = IAP2_LIB_MAX_PLAY_ITEMS;
  uint32_t irq = api->irq_save();
  for (size_t i = 0U; i < count; i++)
    pending_play_items[i] = ids[i];
  pending_play_count = count;
  pending_play_start_idx = start_index;
  pending_play_kind = PLAY_CMD_ITEMS;
  api->irq_restore(irq);
}

static void sess_on_play_library_collection(void *ctx, uint64_t collection_id,
                                            uint8_t collection_type,
                                            uint32_t start_index) {
  (void)ctx;
  uint32_t irq = api->irq_save();
  pending_play_coll_id = collection_id;
  pending_play_coll_type = collection_type;
  pending_play_start_idx = start_index;
  pending_play_kind = PLAY_CMD_COLLECTION;
  api->irq_restore(irq);
}

static void sess_on_play_library_special(void *ctx, uint8_t special_type,
                                         uint64_t start_item_id) {
  (void)ctx;
  uint32_t irq = api->irq_save();
  pending_play_special_type = special_type;
  pending_play_start_item_id = start_item_id;
  pending_play_kind = PLAY_CMD_SPECIAL;
  api->irq_restore(irq);
}

static void usb_publish_state(void) {
  uint32_t irq = api->irq_save();
  if (!stopping) {
    diag_state.shared_rx_stats = rx_tap.stats;
    diag_state.shared_link_summary = rx_tap.summary;
    diag_state.shared_session_summary = ctrl_session.summary;
    diag_sample_audio_fields_locked();
    if (!native_tx.held)
      diag_state.tx_active = 0U;
  }
  api->irq_restore(irq);
}

static void usb_sync_and_pump(void *controller) {
  if (!native_tx.held || native_tx.controller != controller)
    return;

  uint32_t now_ms = usb_now_ms();
  struct iap2_media_state snap;
  uint8_t do_set = 0U;
  uint8_t do_clear_art = 0U;
  uint8_t do_set_art = 0U;
  uint64_t art_key = 0U;
  const uint8_t *art_ptr = NULL;
  size_t art_len = 0U;

  uint8_t do_lib_begin = 0U;
  uint32_t begin_total = 0U;
  char begin_rev[IAP2_LIB_REVISION_MAX];
  size_t begin_pl_count = 0U;

  uint8_t do_lib_batch = 0U;
  uint32_t batch_id = 0U;
  const struct iap2_library_item *batch_ptr = NULL;
  size_t batch_count = 0U;
  bool batch_eof = false;

  uint8_t do_pl_keys = 0U;
  size_t pl_idx = 0U;
  const uint64_t *pl_ptr = NULL;
  size_t pl_cnt = 0U;

  uint8_t do_queue = 0U;
  uint32_t q_sig = 0U;
  const uint64_t *q_ptr = NULL;
  size_t q_cnt = 0U;
  bool q_car = false;

  uint32_t irq = api->irq_save();
  if (!stopping) {
    if (shared_media_gen != session_media_gen) {
      snap = shared_media;
      session_media_gen = shared_media_gen;
      do_set = 1U;
    }
    if (art_cancel_req) {
      art_cancel_req = 0U;
      do_clear_art = 1U;
      if (art_slot_state == ART_SLOT_READY)
        art_slot_state = ART_SLOT_IDLE;
    } else if (art_slot_state == ART_SLOT_READY && art_buf != NULL &&
               art_slot_len > 0U) {
      art_slot_state = ART_SLOT_IN_USE;
      art_key = art_slot_key;
      art_ptr = art_buf;
      art_len = art_slot_len;
      do_set_art = 1U;
    }

    if (lib_begin_state == LIB_BEGIN_READY) {
      if (lib_req_active && lib_begin_gen == lib_req_gen) {
        lib_begin_state = LIB_BEGIN_IN_USE;
        begin_total = lib_begin_total;
        copy_cstr(begin_rev, sizeof(begin_rev), lib_begin_rev);
        begin_pl_count = lib_begin_pl_count;
        do_lib_begin = 1U;
      } else {
        lib_begin_state = LIB_BEGIN_IDLE;
      }
    }
    if (lib_batch_state == LIB_BATCH_READY) {
      if (lib_req_active && lib_batch_gen == lib_req_gen) {
        batch_id = lib_batch_id;
        batch_count = (lib_batch_buf != NULL) ? lib_batch_count : 0U;
        batch_eof = (lib_batch_eof != 0U);
        batch_ptr = (batch_count > 0U) ? lib_batch_buf : NULL;
        lib_batch_state =
            (batch_ptr != NULL) ? LIB_BATCH_IN_USE : LIB_BATCH_IDLE;
        do_lib_batch = 1U;
      } else {
        lib_batch_state = LIB_BATCH_IDLE;
      }
    }
    if (pl_slot_state == PL_SLOT_READY) {
      if (lib_req_active && pl_slot_gen == lib_req_gen) {
        pl_idx = pl_slot_index;
        pl_cnt = (pl_keys_buf != NULL) ? pl_slot_count : 0U;
        pl_ptr = (pl_cnt > 0U) ? pl_keys_buf : NULL;
        pl_slot_state = (pl_ptr != NULL) ? PL_SLOT_IN_USE : PL_SLOT_IDLE;
        do_pl_keys = 1U;
      } else {
        pl_slot_state = PL_SLOT_IDLE;
      }
    }
    if (q_slot_state == Q_SLOT_READY) {
      q_sig = q_slot_sig;
      q_car = (q_slot_from_car != 0U);
      q_cnt = (!q_car && q_keys_buf != NULL) ? q_slot_count : 0U;
      q_ptr = (q_cnt > 0U) ? q_keys_buf : NULL;
      q_slot_state = (q_ptr != NULL) ? Q_SLOT_IN_USE : Q_SLOT_IDLE;
      do_queue = 1U;
    }
  }
  api->irq_restore(irq);

  if (do_set)
    iap2_session_set_media(&ctrl_session, &snap);

  uint32_t host_rate =
      RV_FIELD(USBAudioState, USBAudioState_host_rate, uint32_t);
  if (host_rate >= 8000U && host_rate <= 48000U) {
    if (digital_audio_primed)
      RV_FIELD(DigitalAudioState, DigitalAudioState_active_rate, uint32_t) =
          host_rate;
    if (ctrl_session.media.sample_rate != host_rate) {
      struct iap2_media_state tmp = ctrl_session.media;
      tmp.sample_rate = host_rate;
      iap2_session_set_media(&ctrl_session, &tmp);
    }
  }

  if (do_clear_art) {
    iap2_session_clear_artwork(&ctrl_session);
    irq = api->irq_save();
    if (art_slot_state == ART_SLOT_IN_USE)
      art_slot_state = ART_SLOT_IDLE;
    api->irq_restore(irq);
  } else if (do_set_art) {
    iap2_session_set_artwork(&ctrl_session, art_key, art_ptr, art_len);
  }

  if (do_lib_begin) {
    iap2_session_library_set_playlists(
        &ctrl_session, begin_pl_count ? lib_begin_playlists : NULL,
        begin_pl_count);
    iap2_session_library_begin(&ctrl_session, begin_total, begin_rev);
    irq = api->irq_save();
    if (lib_begin_state == LIB_BEGIN_IN_USE)
      lib_begin_state = LIB_BEGIN_IDLE;
    api->irq_restore(irq);
  }
  if (do_lib_batch) {
    if (!iap2_session_library_supply_batch(&ctrl_session, batch_id, batch_ptr,
                                           batch_count, batch_eof) &&
        batch_ptr != NULL) {
      irq = api->irq_save();
      if (lib_batch_state == LIB_BATCH_IN_USE)
        lib_batch_state = LIB_BATCH_IDLE;
      api->irq_restore(irq);
    }
  }
  if (do_pl_keys) {
    if (!iap2_session_library_supply_playlist_keys(&ctrl_session, pl_idx,
                                                   pl_ptr, pl_cnt) &&
        pl_ptr != NULL) {
      irq = api->irq_save();
      if (pl_slot_state == PL_SLOT_IN_USE)
        pl_slot_state = PL_SLOT_IDLE;
      api->irq_restore(irq);
    }
  }
  if (do_queue) {
    if (!iap2_session_set_queue(&ctrl_session, q_sig, q_ptr, q_cnt, q_car) &&
        q_ptr != NULL) {
      irq = api->irq_save();
      if (q_slot_state == Q_SLOT_IN_USE)
        q_slot_state = Q_SLOT_IDLE;
      api->irq_restore(irq);
    }
  }

  iap2_session_tick(&ctrl_session, now_ms);
  iap2_native_tx_poll(&native_tx, controller);
  usb_publish_state();
}
#endif

static inline uint16_t read_le16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t read_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static void copy_bytes(uint8_t *dst, const uint8_t *src, size_t len) {
  for (size_t i = 0; i < len; i++)
    dst[i] = src[i];
}

#ifdef __arm__
static inline const struct iap2_hid_report_row *
live_out_report_table(uint8_t hs) {
  return (const struct iap2_hid_report_row
              *)(const void *)(hs ? USBHID_HsOutReports : USBHID_FsOutReports);
}
#else
static const struct iap2_hid_report_row *test_out_report_table(uint8_t hs);
static inline const struct iap2_hid_report_row *
live_out_report_table(uint8_t hs) {
  return test_out_report_table(hs);
}
#endif

/* USBHID::Activate vtable slot on USBDeviceTask:
 * (hid, high_speed) -> 1 on success, 0 to retry.
 */
static uint32_t on_hid_activate(uint8_t *hid, uint32_t high_speed,
                                const void *next) {
#ifdef __arm__
  uint32_t pre_irq = api->irq_save();
  uint8_t arm_block =
      (!stopping && diag_state.tap_desired && diag_state.tx_desired &&
       tx_hooks_bound && !native_tx.held && hid != NULL)
          ? 1U
          : 0U;
  if (arm_block)
    blocked_hid = (uintptr_t)hid;
  api->irq_restore(pre_irq);
#endif

  uint32_t res = api->hook_next(next, RV_HOOK_ARG(hid), high_speed, 0, 0, 0, 0);
  if (!hid || !res || hid[USBHID_active] == 0) {
#ifdef __arm__
    if (arm_block) {
      pre_irq = api->irq_save();
      if (blocked_hid == (uintptr_t)hid)
        blocked_hid = 0U;
      api->irq_restore(pre_irq);
    }
#endif
    return res;
  }

  uint32_t irq = api->irq_save();
  uint8_t stop = stopping;
  uint8_t want = (!stop && diag_state.tap_desired) ? 1U : 0U;
  uint8_t already_held = diag_state.hold_acquired;
  api->irq_restore(irq);

  if (!want) {
#ifdef __arm__
    if (arm_block) {
      irq = api->irq_save();
      if (blocked_hid == (uintptr_t)hid)
        blocked_hid = 0U;
      api->irq_restore(irq);
    }
#endif
    if (already_held) {
      uint8_t was_held = 0;

      irq = api->irq_save();
      diag_state.usb_connected = !stop ? 1U : 0U;
      diag_state.session_active = 0;
      diag_state.tap_active = 0;
      diag_state.tx_active = 0;
      active_hid_cookie = !stop ? (uintptr_t)hid : 0U;
      diag_state.session_gen++;
      was_held = diag_state.hold_acquired;
      diag_state.hold_acquired = 0;
      api->irq_restore(irq);

      iap2_rx_tap_disconnect(&rx_tap);

      irq = api->irq_save();
      if (!stopping) {
        diag_state.shared_link_summary = rx_tap.summary;
#ifdef __arm__
        diag_state.shared_session_summary = ctrl_session.summary;
#endif
      }
      api->irq_restore(irq);

      if (was_held && RV_HAS(api, extension_release) && api->extension_release)
        api->extension_release(self);
    } else {
      irq = api->irq_save();
      if (!stopping) {
        diag_state.usb_connected = 1U;
        active_hid_cookie = (uintptr_t)hid;
      }
      api->irq_restore(irq);
    }
    return res;
  }

  if (!already_held) {
    enum rv_result h = (RV_HAS(api, extension_hold) && api->extension_hold)
                           ? api->extension_hold(self)
                           : RV_BUSY;
    if (h != RV_OK) {
      irq = api->irq_save();
#ifdef __arm__
      if (arm_block && blocked_hid == (uintptr_t)hid)
        blocked_hid = 0U;
      diag_state.tx_active = 0;
#endif
      diag_state.hold_refused_count++;
      diag_state.usb_connected = !stopping ? 1U : 0U;
      diag_state.tap_active = 0;
      diag_state.session_active = 0;
      active_hid_cookie = !stopping ? (uintptr_t)hid : 0U;
      api->irq_restore(irq);
      return res;
    }
  }

  iap2_rx_tap_disconnect(&rx_tap);

  irq = api->irq_save();
  if (stopping) {
#ifdef __arm__
    if (arm_block && blocked_hid == (uintptr_t)hid)
      blocked_hid = 0U;
    diag_state.tx_active = 0;
#endif
    api->irq_restore(irq);
    if (!already_held && RV_HAS(api, extension_release) &&
        api->extension_release)
      api->extension_release(self);
    return res;
  }
  diag_state.usb_connected = 1U;
  diag_state.hold_acquired = 1;
  if (!already_held)
    diag_state.hold_acquired_count++;
  active_hid_cookie = (uintptr_t)hid;
  diag_state.tap_active = 1;
  diag_state.session_active = 1;
  diag_state.session_gen++;
  diag_state.shared_link_summary = rx_tap.summary;
#ifdef __arm__
  uint8_t want_tx = (!stopping && diag_state.tap_desired &&
                     diag_state.tx_desired && tx_hooks_bound && !native_tx.held)
                        ? 1U
                        : 0U;
#endif
  api->irq_restore(irq);

#ifdef __arm__
  if (want_tx) {
    if (!blocked_hid) {
      irq = api->irq_save();
      blocked_hid = (uintptr_t)hid;
      api->irq_restore(irq);
    }
    iap2_session_reset(&ctrl_session);
    if (iap2_native_tx_open(&native_tx, api, self, &rx_tap.link, hid)) {
      prime_digital_audio(DigitalAudio_DefaultRate);
      irq = api->irq_save();
      if (!stopping) {
        diag_state.tx_active = 1U;
        diag_state.shared_session_summary = ctrl_session.summary;
      }
      api->irq_restore(irq);
    } else {
      irq = api->irq_save();
      if (blocked_hid == (uintptr_t)hid)
        blocked_hid = 0U;
      diag_state.tx_active = 0U;
      api->irq_restore(irq);
    }
  } else if (arm_block) {
    irq = api->irq_save();
    if (blocked_hid == (uintptr_t)hid)
      blocked_hid = 0U;
    api->irq_restore(irq);
  }
#endif

  return res;
}

/* USBHID::Deactivate vtable slot on USBDeviceTask:
 * (hid) -> native result.
 */
static uint32_t on_hid_deactivate(uint8_t *hid, const void *next) {
  uint32_t res = api->hook_next(next, RV_HOOK_ARG(hid), 0, 0, 0, 0, 0);

  uint32_t irq = api->irq_save();
  uint8_t match = (hid != NULL && (uintptr_t)hid == active_hid_cookie &&
                   (diag_state.usb_connected || diag_state.session_active ||
                    diag_state.hold_acquired))
                      ? 1U
                      : 0U;
  uint8_t was_session = 0;
  uint8_t was_held = 0;
  if (match) {
    diag_state.usb_connected = 0;
    was_session =
        (diag_state.session_active || diag_state.hold_acquired) ? 1U : 0U;
    diag_state.session_active = 0;
    diag_state.tap_active = 0;
    active_hid_cookie = 0;
    diag_state.session_gen++;
    was_held = diag_state.hold_acquired;
    diag_state.hold_acquired = 0;
  }
  api->irq_restore(irq);

  if (was_session) {
    iap2_rx_tap_disconnect(&rx_tap);

    irq = api->irq_save();
#ifdef __arm__
    if (blocked_hid == (uintptr_t)hid)
      blocked_hid = 0U;
    diag_state.tx_active = 0;
    usb_audio_route_wanted = 0U;
    play_cmd_grace_ticks = 0U;
    if (art_slot_state == ART_SLOT_READY) {
      art_slot_state = ART_SLOT_IDLE;
      art_cancel_req = 0U;
    }
    lib_req_gen++;
    lib_req_active = 0U;
    if (lib_begin_state == LIB_BEGIN_READY)
      lib_begin_state = LIB_BEGIN_IDLE;
    if (lib_batch_state == LIB_BATCH_READY)
      lib_batch_state = LIB_BATCH_IDLE;
    pl_req_pending = 0U;
    if (pl_slot_state == PL_SLOT_READY)
      pl_slot_state = PL_SLOT_IDLE;
    if (q_slot_state == Q_SLOT_READY)
      q_slot_state = Q_SLOT_IDLE;
    pending_play_kind = PLAY_CMD_NONE;
    if (!stopping)
      diag_state.shared_session_summary = ctrl_session.summary;
#endif
    if (!stopping)
      diag_state.shared_link_summary = rx_tap.summary;
    api->irq_restore(irq);

#ifdef __arm__
    if (digital_audio_primed)
      unprime_digital_audio();
#endif

    if (was_held && RV_HAS(api, extension_release) && api->extension_release)
      api->extension_release(self);
  }

  return res;
}

/* USBHID::HandleControlWrite vtable slot on USBDeviceTask:
 * (hid, setup, received) -> 0 ACK, -5 DEFER, other nonzero STALL.
 * Receive-only tap copies Output SET_REPORT payloads into rx_bounce before
 * inspecting; iAP1 reports forward to native, iAP2 reports ACK with 0.
 */
static uint32_t on_hid_control_write(uint8_t *hid, const uint8_t *setup,
                                     uint32_t received, const void *next) {
  uint32_t irq = api->irq_save();
  uint8_t active =
      (!stopping && diag_state.session_active && diag_state.tap_active &&
       hid != NULL && (uintptr_t)hid == active_hid_cookie)
          ? 1U
          : 0U;
  uint32_t gen = diag_state.session_gen;
  api->irq_restore(irq);

  if (!active || !setup || hid[USBHID_active] == 0)
    return api->hook_next(next, RV_HOOK_ARG(hid), RV_HOOK_ARG(setup), received,
                          0, 0, 0);

  uint16_t wValue = read_le16(setup + 2);
  if (setup[0] != 0x21U || setup[1] != 0x09U || (wValue >> 8) != 2U)
    return api->hook_next(next, RV_HOOK_ARG(hid), RV_HOOK_ARG(setup), received,
                          0, 0, 0);

  const uint8_t *out_buf = NULL;
  copy_bytes((uint8_t *)&out_buf, hid + USBHID_out_buffer, sizeof(out_buf));
  uint32_t out_cap = read_le32(hid + USBHID_out_capacity);
  const uint8_t *rmap = hid + USBHID_report_map;
  uint8_t hs = rmap[USBReportMap_speed];
  uint16_t row_count = read_le16(
      rmap + (hs ? USBReportMap_hs_out_count : USBReportMap_fs_out_count));

  if (!out_buf || received < 2U || received > out_cap ||
      received > sizeof(rx_bounce)) {
    irq = api->irq_save();
    if (!stopping && diag_state.session_active &&
        diag_state.session_gen == gen) {
      rx_tap.stats.rx_total_reports++;
      rx_tap.stats.rx_bad_report++;
      diag_state.shared_rx_stats = rx_tap.stats;
    }
    api->irq_restore(irq);
    return api->hook_next(next, RV_HOOK_ARG(hid), RV_HOOK_ARG(setup), received,
                          0, 0, 0);
  }

  copy_bytes(rx_bounce, out_buf, (size_t)received);

  if ((uint8_t)(wValue & 0xFFU) != rx_bounce[0]) {
    irq = api->irq_save();
    if (!stopping && diag_state.session_active &&
        diag_state.session_gen == gen) {
      rx_tap.stats.rx_total_reports++;
      rx_tap.stats.rx_bad_report++;
      diag_state.shared_rx_stats = rx_tap.stats;
    }
    api->irq_restore(irq);
    return api->hook_next(next, RV_HOOK_ARG(hid), RV_HOOK_ARG(setup), received,
                          0, 0, 0);
  }

  const struct iap2_hid_report_row *out_table = live_out_report_table(hs);
  size_t speed_cap = hs ? sizeof(rx_bounce) : IAP2_HID_FS_MAX_REPORT_LEN;
  size_t max_len = ((size_t)out_cap < speed_cap) ? (size_t)out_cap : speed_cap;
#ifdef __arm__
  rx_tap.now_ms = usb_now_ms();
#endif
  iap2_tap_action_t act =
      iap2_rx_tap_process_report(&rx_tap, out_table, (size_t)row_count, max_len,
                                 rx_bounce, (size_t)received);

  irq = api->irq_save();
  if (!stopping && diag_state.session_active && diag_state.session_gen == gen) {
    diag_state.shared_rx_stats = rx_tap.stats;
    diag_state.shared_link_summary = rx_tap.summary;
#ifdef __arm__
    diag_state.shared_session_summary = ctrl_session.summary;
#endif
  }
  api->irq_restore(irq);

#ifdef __arm__
  if (act != IAP2_TAP_PASSTHROUGH && native_tx.held && diag_state.tx_active)
    usb_sync_and_pump(native_tx.controller);
#endif

  if (act == IAP2_TAP_PASSTHROUGH)
    return api->hook_next(next, RV_HOOK_ARG(hid), RV_HOOK_ARG(setup), received,
                          0, 0, 0);

  return 0U;
}

static void read_ui_snapshot(struct iap2_diag_snapshot *out) {
  uint32_t irq = api->irq_save();
  iap2_diag_snapshot(&diag_state, out);
  api->irq_restore(irq);
}

static const char *pane_settings_caption(const struct rv_context *ctx) {
  (void)ctx;
  struct iap2_diag_snapshot snap;
  read_ui_snapshot(&snap);
  return iap2_ui_settings_caption(&snap);
}

static const char *pane_protocol_caption(const struct rv_context *ctx) {
  (void)ctx;
  struct iap2_diag_snapshot snap;
  read_ui_snapshot(&snap);
  return iap2_ui_protocol_caption(&snap);
}

static const char *row_car_status_text(const struct rv_context *ctx) {
  (void)ctx;
  struct iap2_diag_snapshot snap;
  read_ui_snapshot(&snap);
  return iap2_ui_status_row(&snap);
}

static const char *pane_status_caption(const struct rv_context *ctx) {
  (void)ctx;
  struct iap2_diag_snapshot snap;
  read_ui_snapshot(&snap);
  return iap2_ui_status_caption(&snap);
}

#ifdef __arm__

#define ART_FORK_DATA 0x64617461U

struct legacy_art_path {
  uint16_t volume;
  char text[258];
};

static uint8_t str_eq(const char *a, const char *b) {
  size_t i = 0U;
  while (a[i] != '\0' && b[i] != '\0') {
    if (a[i] != b[i])
      return 0U;
    i++;
  }
  return (a[i] == b[i]) ? 1U : 0U;
}

static void normalize_art_path(char *dst, size_t cap, const char *src, char sep,
                               uint8_t leading_sep) {
  if (!dst || cap == 0U)
    return;
  size_t pos = 0U;
  if (!src || src[0] == '\0') {
    dst[0] = '\0';
    return;
  }
  while (*src == ':' || *src == '/' || *src == '\\')
    src++;
  if (leading_sep && pos + 1U < cap)
    dst[pos++] = sep;
  while (*src != '\0' && pos + 1U < cap) {
    char ch = *src++;
    dst[pos++] = (ch == ':' || ch == '/' || ch == '\\') ? sep : ch;
  }
  dst[pos] = '\0';
}

static bool legacy_art_read(void *ctx, void *dst, size_t len, size_t *out_got) {
  uint32_t got = 0U;
  int rc = File_ReadLegacyHandle(ctx, dst, (uint32_t)len, 0U, &got);
  if (out_got != NULL)
    *out_got = (size_t)got;
  return (rc == 0 || rc == (int)FileError_EndOfFile) && got > 0U;
}

static bool legacy_art_seek(void *ctx, uint64_t offset) {
  return File_SeekLegacyHandle(ctx, offset) == 0;
}

static bool arm_extract_track_artwork(const char *raw_path, uint8_t *buf,
                                      size_t cap, size_t *out_len) {
  if (out_len != NULL)
    *out_len = 0U;
  if (!raw_path || raw_path[0] == '\0' || !buf || cap < 2U || !out_len)
    return false;

  char candidates[5][258];
  copy_cstr(candidates[0], sizeof(candidates[0]), raw_path);
  normalize_art_path(candidates[1], sizeof(candidates[1]), raw_path, '/', 0U);
  normalize_art_path(candidates[2], sizeof(candidates[2]), raw_path, '/', 1U);
  normalize_art_path(candidates[3], sizeof(candidates[3]), raw_path, '\\', 0U);
  normalize_art_path(candidates[4], sizeof(candidates[4]), raw_path, '\\', 1U);

  for (size_t i = 0U; i < 5U; i++) {
    if (candidates[i][0] == '\0')
      continue;
    uint8_t dup = 0U;
    for (size_t prev = 0U; prev < i; prev++) {
      if (str_eq(candidates[prev], candidates[i])) {
        dup = 1U;
        break;
      }
    }
    if (dup)
      continue;

    struct legacy_art_path p;
    p.volume = 0U;
    copy_cstr(p.text, sizeof(p.text), candidates[i]);
    void *handle = NULL;
    if (File_OpenLegacyPath(&p, ART_FORK_DATA, 1, &handle) != 0 ||
        handle == NULL)
      continue;

    struct iap2_art_reader reader = {
        .context = handle,
        .read = legacy_art_read,
        .seek = legacy_art_seek,
    };
    bool ok = iap2_artwork_extract_jpeg(&reader, buf, cap, out_len);
    (void)File_CloseLegacyHandle(handle);
    if (ok && *out_len > 0U)
      return true;
  }

  *out_len = 0U;
  return false;
}

static uint8_t arm_copy_item_text(const struct MediaItem *item, uint32_t slot,
                                  char *dst, size_t cap,
                                  struct NativeString *ns) {
  if (dst != NULL && cap > 0U)
    dst[0] = '\0';
  const char *t = rv_item_text(item, slot, ns);
  uint8_t has = (t != NULL) ? 1U : 0U;
  if (has && dst != NULL && cap > 0U)
    rv_copy_utf8(dst, (uint32_t)cap, t);
  NativeString_Destroy(ns);
  return has;
}

static void arm_poll_media(struct iap2_media_state *out, char *track_path_out,
                           size_t track_path_cap, void **out_record,
                           uint32_t *out_gen, uint8_t want_path) {
  memset(out, 0, sizeof(*out));
  copy_cstr(out->device_name, sizeof(out->device_name), "iPod Classic");
  out->battery_pct = 100U;
  out->charging = true;
  if (track_path_out && track_path_cap > 0U)
    track_path_out[0] = '\0';
  if (out_record)
    *out_record = NULL;
  if (out_gen)
    *out_gen = 0U;

  uint32_t rate = RV_FIELD(USBAudioState, USBAudioState_host_rate, uint32_t);
  out->sample_rate =
      (rate >= 8000U && rate <= 48000U) ? rate : DigitalAudio_DefaultRate;

  if (!media_player_ready())
    return;

  if (GameAudio_IsPlaying()) {
    out->play_state = IAP2_PLAY_PLAYING;
  } else if (GameAudio_IsPaused()) {
    out->play_state = IAP2_PLAY_PAUSED;
  } else {
    out->play_state = IAP2_PLAY_STOPPED;
  }
  out->elapsed_ms = GameAudio_GetPosition();
  out->queue_index = GameAudio_GetCurrentIndex();
  out->queue_count = GameAudio_GetEntryCount();
  out->shuffle = GameAudio_IsShuffleEnabled() != 0;
  if (GameAudio_IsRepeatOne()) {
    out->repeat_mode = IAP2_REPEAT_ONE;
  } else if (GameAudio_IsRepeatAll()) {
    out->repeat_mode = IAP2_REPEAT_ALL;
  } else {
    out->repeat_mode = IAP2_REPEAT_OFF;
  }

  void *player = rv_player_lock();
  if (player != NULL) {
    _Alignas(4) uint8_t item_raw[MediaItem_bytes];
    struct MediaItem *item = (struct MediaItem *)item_raw;
    rv_player_current(player, item);
    if (rv_item_valid(item)) {
      void *rec = RV_FIELD(item, MediaItem_record, void *);
      uint32_t gen = RV_FIELD(item, MediaItem_generation, uint32_t);
      if (out_record)
        *out_record = rec;
      if (out_gen)
        *out_gen = gen;

      out->has_track = true;
      out->track_id = (rec != NULL) ? read_record_persistent_id(rec)
                                    : (((uint64_t)gen << 32) | 1U);
      out->duration_ms = rv_item_value(item, MediaItem_duration_method);
      out->track_number =
          (uint16_t)rv_item_value(item, MediaItem_track_number_method);
      out->disc_number =
          (uint16_t)rv_item_value(item, MediaItem_disc_number_method);

      _Alignas(4) uint8_t s_raw[NativeString_bytes];
      struct NativeString *ns = (struct NativeString *)s_raw;

      arm_copy_item_text(item, MediaItem_title_method, out->title,
                         sizeof(out->title), ns);
      if (!arm_copy_item_text(item, MediaItem_artist_method, out->artist,
                              sizeof(out->artist), ns))
        arm_copy_item_text(item, MediaItem_display_artist_method, out->artist,
                           sizeof(out->artist), ns);
      arm_copy_item_text(item, MediaItem_album_method, out->album,
                         sizeof(out->album), ns);
      arm_copy_item_text(item, MediaItem_display_artist_method,
                         out->album_artist, sizeof(out->album_artist), ns);
      arm_copy_item_text(item, MediaItem_genre_method, out->genre,
                         sizeof(out->genre), ns);
      arm_copy_item_text(item, MediaItem_composer_method, out->composer,
                         sizeof(out->composer), ns);

      if ((want_path || rec != last_art_record || gen != last_art_gen) &&
          track_path_out != NULL && track_path_cap > 0U) {
        _Alignas(4) uint8_t p_raw[NativeString_bytes];
        struct NativeString *pns = (struct NativeString *)p_raw;
        MediaItem_GetPath(pns, item);
        const char *path = NativeString_GetUtf8(pns);
        if (path != NULL)
          copy_cstr(track_path_out, track_path_cap, path);
        NativeString_Destroy(pns);
      }
    }
    MediaItem_Destroy(item);
    rv_player_unlock(player);
  }
  if (out->play_state == IAP2_PLAY_PLAYING ||
      out->play_state == IAP2_PLAY_PAUSED) {
    play_cmd_grace_ticks = 0U;
  } else if (out->has_track && play_cmd_grace_ticks > 0U) {
    play_cmd_grace_ticks--;
    out->play_state = IAP2_PLAY_PLAYING;
  }
}

static void arm_populate_library_item(struct iap2_library_item *dst,
                                      void *rec) {
  memset(dst, 0, sizeof(*dst));
  if (rec == NULL)
    return;

  _Alignas(4) uint8_t item_raw[MediaItem_bytes];
  struct MediaItem *item = (struct MediaItem *)item_raw;
  MediaItem_InitFromRecord(item, rec, 0U);
  if (rv_item_valid(item)) {
    dst->persistent_id = read_record_persistent_id(rec);
    dst->duration_ms = rv_item_value(item, MediaItem_duration_method);
    dst->track_number =
        (uint16_t)rv_item_value(item, MediaItem_track_number_method);
    dst->disc_number =
        (uint16_t)rv_item_value(item, MediaItem_disc_number_method);

    _Alignas(4) uint8_t s_raw[NativeString_bytes];
    struct NativeString *ns = (struct NativeString *)s_raw;
    arm_copy_item_text(item, MediaItem_title_method, dst->title,
                       sizeof(dst->title), ns);
    if (!arm_copy_item_text(item, MediaItem_artist_method, dst->artist,
                            sizeof(dst->artist), ns))
      arm_copy_item_text(item, MediaItem_display_artist_method, dst->artist,
                         sizeof(dst->artist), ns);
    arm_copy_item_text(item, MediaItem_album_method, dst->album,
                       sizeof(dst->album), ns);
    arm_copy_item_text(item, MediaItem_display_artist_method, dst->album_artist,
                       sizeof(dst->album_artist), ns);
    arm_copy_item_text(item, MediaItem_genre_method, dst->genre,
                       sizeof(dst->genre), ns);
    arm_copy_item_text(item, MediaItem_composer_method, dst->composer,
                       sizeof(dst->composer), ns);
  }
  MediaItem_Destroy(item);
}

static uint8_t arm_record_matches_collection(void *rec, uint8_t coll_type,
                                             uint32_t target_hash) {
  _Alignas(4) uint8_t item_raw[MediaItem_bytes];
  struct MediaItem *item = (struct MediaItem *)item_raw;
  MediaItem_InitFromRecord(item, rec, 0U);
  uint8_t match = 0U;
  if (rv_item_valid(item)) {
    _Alignas(4) uint8_t s_raw[NativeString_bytes];
    struct NativeString *ns = (struct NativeString *)s_raw;
    const char *text = NULL;
    switch (coll_type) {
    case 1U:
      text = rv_item_text(item, MediaItem_artist_method, ns);
      if (!text) {
        NativeString_Destroy(ns);
        text = rv_item_text(item, MediaItem_display_artist_method, ns);
      }
      break;
    case 2U:
      text = rv_item_text(item, MediaItem_album_method, ns);
      break;
    case 3U:
      text = rv_item_text(item, MediaItem_display_artist_method, ns);
      break;
    case 4U:
      text = rv_item_text(item, MediaItem_genre_method, ns);
      break;
    case 5U:
      text = rv_item_text(item, MediaItem_composer_method, ns);
      break;
    default:
      break;
    }
    if (text != NULL && fnv1a_32_arm(text) == target_hash)
      match = 1U;
    if (coll_type >= 1U && coll_type <= 5U)
      NativeString_Destroy(ns);
  }
  MediaItem_Destroy(item);
  return match;
}

static void arm_dispatch_play_command(void) {
  uint32_t irq = api->irq_save();
  uint8_t kind = pending_play_kind;
  if (kind == PLAY_CMD_NONE) {
    api->irq_restore(irq);
    return;
  }
  size_t cmd_count = pending_play_count;
  uint32_t start_idx = pending_play_start_idx;
  uint64_t coll_id = pending_play_coll_id;
  uint8_t coll_type = pending_play_coll_type;
  uint8_t special_type = pending_play_special_type;
  uint64_t start_item_id = pending_play_start_item_id;
  if (kind == PLAY_CMD_ITEMS) {
    if (cmd_count > IAP2_LIB_MAX_PLAY_ITEMS)
      cmd_count = IAP2_LIB_MAX_PLAY_ITEMS;
    for (size_t i = 0U; i < cmd_count; i++)
      ui_play_items[i] = pending_play_items[i];
  }
  pending_play_kind = PLAY_CMD_NONE;
  api->irq_restore(irq);

  (void)special_type;

  void *owner = NULL;
  void *td = NULL;
  struct MediaList **lists = NULL;
  uint32_t list_count = 0U;
  if (!can_start_playback() ||
      !arm_get_library_handles(&owner, &td, &lists, &list_count)) {
    if (kind == PLAY_CMD_SPECIAL && media_player_ready() &&
        !GameAudio_IsPlaying())
      GameAudio_TogglePlayPause(0U);
    return;
  }
  (void)owner;

  size_t match_count = 0U;
  uint32_t target_idx = start_idx;

  if (kind == PLAY_CMD_ITEMS) {
    size_t want = (cmd_count < PLAY_MATCH_CAP) ? cmd_count : PLAY_MATCH_CAP;
    for (size_t i = 0U; i < want; i++)
      play_matched_recs[i] = NULL;
    size_t found = 0U;
    void *rec = RV_FIELD(td, TrackData_first_record, void *);
    uint32_t steps = 0U;
    while (found < want && steps < 100000U && is_valid_media_record(rec, td)) {
      if (is_music_record(rec)) {
        uint64_t pid = read_record_persistent_id(rec);
        for (size_t k = 0U; k < want; k++) {
          if (play_matched_recs[k] == NULL && ui_play_items[k] == pid) {
            play_matched_recs[k] = rec;
            found++;
          }
        }
      }
      rec = RV_FIELD(rec, MediaRecord_next, void *);
      steps++;
    }
    uint32_t compacted_target = 0U;
    for (size_t k = 0U; k < want; k++) {
      if (play_matched_recs[k] == NULL)
        continue;
      if (k == start_idx)
        compacted_target = (uint32_t)match_count;
      play_matched_recs[match_count++] = play_matched_recs[k];
    }
    target_idx = (compacted_target < match_count) ? compacted_target : 0U;
  } else if (kind == PLAY_CMD_COLLECTION && coll_type == 0U && lists != NULL) {
    struct MediaList *target_list = NULL;
    for (uint32_t i = 0U; i < list_count; i++) {
      if (lists[i] != NULL &&
          read_u64_le_safe(lists[i], MediaList_persistent_id) == coll_id) {
        target_list = lists[i];
        break;
      }
    }
    if (target_list != NULL) {
      struct MediaEntry *root =
          RV_FIELD(target_list, MediaList_root, struct MediaEntry *);
      struct MediaEntry *e =
          root ? RV_FIELD(root, MediaEntry_group_first, struct MediaEntry *)
               : NULL;
      uint32_t steps = 0U;
      while (e != NULL && match_count < PLAY_MATCH_CAP && steps < 100000U) {
        if ((RV_FIELD(e, MediaEntry_flags, uint32_t) & MediaEntryFlags_Group) ==
            0U) {
          void *rec = RV_FIELD(e, MediaEntry_song_record, void *);
          if (is_valid_media_record(rec, td) && is_music_record(rec))
            play_matched_recs[match_count++] = rec;
        }
        e = RV_FIELD(e, MediaEntry_next, struct MediaEntry *);
        steps++;
      }
    }
    if (target_idx >= match_count)
      target_idx = 0U;
  } else if (kind == PLAY_CMD_COLLECTION && coll_type >= 1U &&
             coll_type <= 5U) {
    uint32_t target_hash = (uint32_t)(coll_id & 0xFFFFFFFFU);
    void *rec = RV_FIELD(td, TrackData_first_record, void *);
    uint32_t steps = 0U;
    TrackData_AcquireStringCaches(td);
    while (match_count < PLAY_MATCH_CAP && steps < 100000U &&
           is_valid_media_record(rec, td)) {
      if (is_music_record(rec) &&
          arm_record_matches_collection(rec, coll_type, target_hash)) {
        play_matched_recs[match_count++] = rec;
      }
      rec = RV_FIELD(rec, MediaRecord_next, void *);
      steps++;
    }
    TrackData_ReleaseStringCaches(td);
    if (target_idx >= match_count)
      target_idx = 0U;
  } else if (kind == PLAY_CMD_SPECIAL) {
    target_idx = 0U;
    void *rec = RV_FIELD(td, TrackData_first_record, void *);
    uint32_t steps = 0U;
    while (match_count < PLAY_MATCH_CAP && steps < 100000U &&
           is_valid_media_record(rec, td)) {
      if (is_music_record(rec)) {
        if (start_item_id != 0U &&
            read_record_persistent_id(rec) == start_item_id)
          target_idx = (uint32_t)match_count;
        play_matched_recs[match_count++] = rec;
      }
      rec = RV_FIELD(rec, MediaRecord_next, void *);
      steps++;
    }
  }

  if (match_count == 0U) {
    if (kind == PLAY_CMD_SPECIAL && media_player_ready() &&
        !GameAudio_IsPlaying())
      GameAudio_TogglePlayPause(0U);
    return;
  }

  uint32_t added = 0U;
  void *player = rv_player_lock();
  if (player != NULL) {
    ArtworkSession_Stop((uint8_t *)player + MediaPlayer_artwork_session);
    MediaPlayer_InitializePlaybackList(player, 0U, 3U);
    void *view = (uint8_t *)player + MediaPlayer_playback_view;
    struct MediaList *dst =
        RV_FIELD(view, MediaListView_list, struct MediaList *);
    struct MediaEntry *root =
        (dst != NULL) ? RV_FIELD(dst, MediaList_root, struct MediaEntry *)
                      : NULL;
    if (MediaListView_IsValid(view) && dst != NULL && root != NULL) {
      struct MediaEntry *tail = NULL;
      uint32_t play_at = 0U;
      TrackData_AcquireStringCaches(td);
      for (size_t i = 0U; i < match_count; i++) {
        void *rec = play_matched_recs[i];
        if (!is_valid_media_record(rec, td))
          continue;
        struct MediaEntry *src =
            RV_FIELD(rec, MediaRecord_entries, struct MediaEntry *);
        if (src == NULL || (RV_FIELD(src, MediaEntry_flags, uint32_t) &
                            MediaEntryFlags_Group) != 0U) {
          continue;
        }
        if (i == target_idx)
          play_at = added;
        struct MediaEntry *cursor[2] = {root, tail};
        struct MediaEntry *ins =
            MediaList_CopyEntryAtPosition(src, dst, cursor, 0U);
        if (ins != NULL) {
          tail = ins;
          added++;
        }
      }
      TrackData_ReleaseStringCaches(td);
      if (added > 0U) {
        if (play_at >= added)
          play_at = 0U;
        MediaList_NotifyChanged(dst);
        MediaPlayer_ClearPreparedTrack(player);
        Audio_SetPlaybackSource(
            RV_FIELD(player, MediaPlayer_playback_source, void *));
        MediaPlayer_PlayIndex(player, (int)play_at, 0U, 0U, 0U);
        ui_queue_from_car = 1U;
        q_sent_sig = 0U;
        play_cmd_grace_ticks = 15U;
      }
    }
    rv_player_unlock(player);
  }

  if (added > 0U) {
    if (App != NULL)
      App_SyncCurrentItem(App);
    if (NowPlayingModel_Instance != NULL)
      NowPlayingModel_RefreshCurrentItem(NowPlayingModel_Instance);
  }
}

static void arm_step_library_sync(uint8_t lib_phase) {
  uint32_t irq = api->irq_save();
  uint8_t req_active = lib_req_active;
  uint32_t req_gen = lib_req_gen;
  uint8_t begin_st = lib_begin_state;
  uint8_t batch_st = lib_batch_state;
  api->irq_restore(irq);

  if (!req_active)
    return;

  if (ui_lib_gen != req_gen) {
    if (begin_st != LIB_BEGIN_IDLE)
      return;

    void *td = NULL;
    if (!arm_get_library_handles(NULL, &td, NULL, NULL))
      return;

    uint32_t total = 0U;
    uint32_t hash = 2166136261U;
    void *rec = RV_FIELD(td, TrackData_first_record, void *);
    uint32_t steps = 0U;
    while (steps < 100000U && is_valid_media_record(rec, td)) {
      if (is_music_record(rec)) {
        total++;
        hash = fnv1a_u64_step(hash, read_record_persistent_id(rec));
      }
      rec = RV_FIELD(rec, MediaRecord_next, void *);
      steps++;
    }

    char rev[IAP2_LIB_REVISION_MAX];
    rv_format(rev, sizeof(rev), "%08lx-%lu", (unsigned long)hash,
              (unsigned long)total);

    size_t pl_count = 0U;
    int32_t gm_lib = GameMetadata_LibraryCreate();
    if (gm_lib >= 0) {
      uint32_t raw_pl = GameMetadata_LibraryGetPlaylistCount(gm_lib);
      if (raw_pl > 256U)
        raw_pl = 256U;
      for (uint32_t i = 0U; i < raw_pl && pl_count < IAP2_LIB_MAX_PLAYLISTS;
           i++) {
        int32_t pl = GameMetadata_LibraryGetPlaylistAt(gm_lib, i);
        if (pl < 0)
          continue;
        if (!GameMetadata_PlaylistIsLibrary(pl)) {
          uint16_t cnt = GameMetadata_PlaylistGetCount(pl);
          uint64_t pid = GameMetadata_PlaylistGetPersistentId(pl);
          if (cnt > 0U && pid != 0U) {
            struct iap2_library_playlist *dst = &lib_begin_playlists[pl_count];
            memset(dst, 0, sizeof(*dst));
            dst->persistent_id = pid;
            dst->parent_id = 0U;
            dst->track_count = cnt;
            dst->is_folder = 0U;
            uint32_t inout = (uint32_t)(sizeof(dst->name) - 1U);
            GameMetadata_PlaylistCopyName(pl, dst->name, &inout);
            dst->name[sizeof(dst->name) - 1U] = '\0';
            if (dst->name[0] == '\0')
              copy_cstr(dst->name, sizeof(dst->name), "Playlist");
            pl_count++;
          }
        }
        GameMetadata_PlaylistDestroy(pl);
      }
      GameMetadata_LibraryDestroy(gm_lib);
    }

    irq = api->irq_save();
    if (!stopping && lib_req_active && lib_req_gen == req_gen &&
        lib_begin_state == LIB_BEGIN_IDLE) {
      lib_begin_gen = req_gen;
      lib_begin_total = total;
      copy_cstr(lib_begin_rev, sizeof(lib_begin_rev), rev);
      lib_begin_pl_count = pl_count;
      lib_begin_state = LIB_BEGIN_READY;
      ui_lib_gen = req_gen;
      ui_lib_cursor_rec = RV_FIELD(td, TrackData_first_record, void *);
      ui_lib_next_batch_id = 1U;
      ui_lib_done = 0U;
    }
    api->irq_restore(irq);
    return;
  }

  if (begin_st == LIB_BEGIN_IDLE && !ui_lib_done &&
      batch_st == LIB_BATCH_IDLE) {
    if (lib_phase == IAP2_LIB_PLAYLIST_RECORDS ||
        lib_phase == IAP2_LIB_PLAYLIST_XFER || lib_phase == IAP2_LIB_DONE) {
      ui_lib_done = 1U;
      return;
    }
    if (lib_phase != IAP2_LIB_STREAMING_TRACKS)
      return;
    if (lib_batch_buf == NULL && RV_HAS(api, alloc) && api->alloc) {
      lib_batch_buf = api->alloc(self, LIB_BATCH_SIZE * sizeof(*lib_batch_buf));
    }
    if (lib_batch_buf == NULL)
      return;

    void *td = NULL;
    size_t n = 0U;
    uint8_t eof = 1U;
    if (arm_get_library_handles(NULL, &td, NULL, NULL)) {
      void *rec = ui_lib_cursor_rec;
      uint32_t scanned = 0U;
      TrackData_AcquireStringCaches(td);
      while (n < LIB_BATCH_SIZE && scanned < 4096U &&
             is_valid_media_record(rec, td)) {
        if (is_music_record(rec)) {
          arm_populate_library_item(&lib_batch_buf[n], rec);
          n++;
        }
        rec = RV_FIELD(rec, MediaRecord_next, void *);
        scanned++;
      }
      TrackData_ReleaseStringCaches(td);
      eof = !is_valid_media_record(rec, td) ? 1U : 0U;
      ui_lib_cursor_rec = rec;
    }

    irq = api->irq_save();
    if (!stopping && lib_req_active && lib_req_gen == req_gen &&
        lib_batch_state == LIB_BATCH_IDLE) {
      lib_batch_gen = req_gen;
      lib_batch_id = ui_lib_next_batch_id++;
      lib_batch_count = n;
      lib_batch_eof = eof;
      lib_batch_state = LIB_BATCH_READY;
      if (eof)
        ui_lib_done = 1U;
    }
    api->irq_restore(irq);
  }
}

static void arm_step_playlist_keys(void) {
  uint32_t irq = api->irq_save();
  if (!pl_req_pending) {
    api->irq_restore(irq);
    return;
  }
  if (!lib_req_active || pl_req_gen != lib_req_gen) {
    pl_req_pending = 0U;
    api->irq_restore(irq);
    return;
  }
  if (pl_slot_state != PL_SLOT_IDLE) {
    api->irq_restore(irq);
    return;
  }
  uint32_t req_gen = pl_req_gen;
  size_t req_idx = pl_req_index;
  uint64_t req_id = pl_req_id;
  api->irq_restore(irq);

  if (pl_keys_buf == NULL && RV_HAS(api, alloc) && api->alloc) {
    pl_keys_buf = api->alloc(self, PL_KEYS_CAP * sizeof(*pl_keys_buf));
  }
  if (pl_keys_buf == NULL)
    return;

  size_t n = 0U;
  void *td = NULL;
  struct MediaList **lists = NULL;
  uint32_t list_count = 0U;
  if (arm_get_library_handles(NULL, &td, &lists, &list_count) &&
      lists != NULL) {
    struct MediaList *target = NULL;
    for (uint32_t i = 0U; i < list_count; i++) {
      if (lists[i] != NULL &&
          read_u64_le_safe(lists[i], MediaList_persistent_id) == req_id) {
        target = lists[i];
        break;
      }
    }
    if (target != NULL) {
      struct MediaEntry *root =
          RV_FIELD(target, MediaList_root, struct MediaEntry *);
      struct MediaEntry *e =
          root ? RV_FIELD(root, MediaEntry_group_first, struct MediaEntry *)
               : NULL;
      uint32_t steps = 0U;
      while (e != NULL && n < PL_KEYS_CAP && steps < 100000U) {
        if ((RV_FIELD(e, MediaEntry_flags, uint32_t) & MediaEntryFlags_Group) ==
            0U) {
          void *rec = RV_FIELD(e, MediaEntry_song_record, void *);
          if (is_valid_media_record(rec, td) && is_music_record(rec))
            pl_keys_buf[n++] = read_record_persistent_id(rec);
        }
        e = RV_FIELD(e, MediaEntry_next, struct MediaEntry *);
        steps++;
      }
    }
  }

  irq = api->irq_save();
  if (!stopping && lib_req_active && lib_req_gen == req_gen && pl_req_pending &&
      pl_req_gen == req_gen && pl_slot_state == PL_SLOT_IDLE) {
    pl_req_pending = 0U;
    pl_slot_gen = req_gen;
    pl_slot_index = req_idx;
    pl_slot_count = n;
    pl_slot_state = PL_SLOT_READY;
  } else if (pl_req_pending && pl_req_gen != lib_req_gen) {
    pl_req_pending = 0U;
  }
  api->irq_restore(irq);
}

static void arm_step_queue_sync(uint32_t cur_session_gen,
                                const struct iap2_session_summary *sum) {
  if (cur_session_gen != last_q_session_gen) {
    last_q_session_gen = cur_session_gen;
    q_seen_sig = 0U;
    q_sent_sig = 0U;
    q_stable_ticks = 0U;
    ui_queue_from_car = 0U;
  }

  if (!media_player_ready() || !sum->now_playing_active ||
      (sum->playback_mask & (1U << 0x0EU)) == 0U) {
    return;
  }

  if (q_keys_buf == NULL && RV_HAS(api, alloc) && api->alloc) {
    q_keys_buf = api->alloc(self, Q_KEYS_CAP * sizeof(*q_keys_buf));
  }

  void *td = NULL;
  if (!arm_get_library_handles(NULL, &td, NULL, NULL))
    return;

  void *player = rv_player_lock();
  if (player == NULL)
    return;

  void *view = (uint8_t *)player + MediaPlayer_playback_view;
  struct MediaList *list =
      MediaListView_IsValid(view)
          ? RV_FIELD(view, MediaListView_list, struct MediaList *)
          : NULL;
  struct MediaEntry *root =
      (list != NULL) ? RV_FIELD(list, MediaList_root, struct MediaEntry *)
                     : NULL;
  if (list == NULL || root == NULL) {
    rv_player_unlock(player);
    return;
  }

  uint32_t count = RV_FIELD(root, MediaEntry_groups, uint32_t);
  uint8_t flags = RV_FIELD(list, MediaList_flags, uint8_t);
  uint8_t order_flags = RV_FIELD(list, MediaList_order_flags, uint8_t);
  uint32_t sig = ((uint32_t)(uintptr_t)list) ^ (count * 2654435761U) ^
                 (((uint32_t)flags << 8) | order_flags);
  if (count > 0U && count <= 100000U) {
    struct MediaEntry *e0 = PlaybackList_GetEntryByIndex(view, 0U);
    struct MediaEntry *e1 =
        (count > 1U) ? PlaybackList_GetEntryByIndex(view, count - 1U) : e0;
    void *r0 =
        (e0 != NULL) ? RV_FIELD(e0, MediaEntry_song_record, void *) : NULL;
    void *r1 =
        (e1 != NULL) ? RV_FIELD(e1, MediaEntry_song_record, void *) : NULL;
    if (is_valid_media_record(r0, td))
      sig ^= (uint32_t)(uintptr_t)r0 * 16777619U;
    if (is_valid_media_record(r1, td))
      sig ^= (uint32_t)(uintptr_t)r1 * 2166136261U;
  }
  if (sig == 0U)
    sig = 1U;

  if (sig != q_seen_sig) {
    q_seen_sig = sig;
    q_stable_ticks = 0U;
    rv_player_unlock(player);
    return;
  }
  if (q_stable_ticks < 10U) {
    q_stable_ticks++;
    rv_player_unlock(player);
    return;
  }
  if (sig == q_sent_sig) {
    rv_player_unlock(player);
    return;
  }

  uint32_t irq = api->irq_save();
  uint8_t q_st = q_slot_state;
  api->irq_restore(irq);
  if (q_st != Q_SLOT_IDLE) {
    rv_player_unlock(player);
    return;
  }

  uint8_t from_car = (ui_queue_from_car || count == 0U || count > Q_KEYS_CAP ||
                      q_keys_buf == NULL)
                         ? 1U
                         : 0U;
  size_t n = 0U;
  if (!from_car) {
    for (uint32_t i = 0U; i < count && n < Q_KEYS_CAP; i++) {
      struct MediaEntry *e = PlaybackList_GetEntryByIndex(view, i);
      void *rec =
          (e != NULL) ? RV_FIELD(e, MediaEntry_song_record, void *) : NULL;
      if (is_valid_media_record(rec, td))
        q_keys_buf[n++] = read_record_persistent_id(rec);
    }
    if (n == 0U)
      from_car = 1U;
  }
  rv_player_unlock(player);

  irq = api->irq_save();
  if (!stopping && q_slot_state == Q_SLOT_IDLE) {
    q_slot_sig = sig;
    q_slot_count = from_car ? 0U : n;
    q_slot_from_car = from_car;
    q_slot_state = Q_SLOT_READY;
  }
  api->irq_restore(irq);

  q_sent_sig = sig;
  ui_queue_from_car = 0U;
}

static void arm_dispatch_remote(uint32_t actions, uint8_t has_seek,
                                uint32_t seek_ms) {
  if (!media_player_ready())
    return;
  if (actions & (1U << IAP2_REMOTE_PLAY_PAUSE))
    GameAudio_TogglePlayPause(0U);
  if (actions & (1U << IAP2_REMOTE_PLAY)) {
    if (!GameAudio_IsPlaying())
      GameAudio_TogglePlayPause(0U);
  }
  if (actions & (1U << IAP2_REMOTE_PAUSE)) {
    if (GameAudio_IsPlaying())
      GameAudio_TogglePlayPause(0U);
  }
  if (actions & (1U << IAP2_REMOTE_STOP))
    GameAudio_Stop();
  if (actions & (1U << IAP2_REMOTE_NEXT))
    GameAudio_NextTrack();
  if (actions & (1U << IAP2_REMOTE_PREV))
    GameAudio_PreviousTrack();
  if (actions & (1U << IAP2_REMOTE_FFWD)) {
    uint32_t pos = GameAudio_GetPosition();
    GameAudio_Seek(pos + 10000U);
  }
  if (actions & (1U << IAP2_REMOTE_REW)) {
    uint32_t pos = GameAudio_GetPosition();
    GameAudio_Seek(pos > 10000U ? pos - 10000U : 0U);
  }
  if (actions & (1U << IAP2_REMOTE_SHUFFLE_TOGGLE))
    GameAudio_SetPlaybackOrder(GameAudio_IsShuffleEnabled() ? 0U : 1U);
  if (actions & (1U << IAP2_REMOTE_REPEAT_NEXT)) {
    uint8_t next_rep =
        GameAudio_IsRepeatOne() ? 2U : (GameAudio_IsRepeatAll() ? 0U : 1U);
    GameAudio_SetRepeatMode(next_rep);
  }
  if (has_seek)
    GameAudio_Seek(seek_ms);
}

static void on_media_timer(void *arg) {
  (void)arg;
  if (stopping)
    return;

  uint32_t irq = api->irq_save();
  uint32_t actions = pending_actions_mask;
  pending_actions_mask = 0U;
  uint8_t has_seek = pending_seek_valid;
  uint32_t seek_ms = pending_seek_target_ms;
  pending_seek_valid = 0U;
  uint8_t active = diag_state.tx_active;
  uint8_t mode = diag_state.protocol_mode;
  uint8_t usb_conn = diag_state.usb_connected;
  uint8_t tap_act = diag_state.tap_active;
  uint8_t replug = (usb_conn &&
                    (diag_state.tap_desired != tap_act ||
                     (diag_state.tap_desired &&
                      diag_state.tx_desired != active)))
                       ? 1U
                       : 0U;
  uint32_t cur_session_gen = diag_state.session_gen;
  uint8_t want_route = usb_audio_route_wanted;
  diag_sample_audio_fields_locked();
  struct iap2_session_summary sum = diag_state.shared_session_summary;
  api->irq_restore(irq);

  if (want_route != usb_audio_route_applied) {
    arm_apply_usb_audio_route(want_route);
    irq = api->irq_save();
    if (!stopping)
      diag_sample_audio_fields_locked();
    api->irq_restore(irq);
  }

  if (actions != 0U || has_seek)
    arm_dispatch_remote(actions, has_seek, seek_ms);

  arm_dispatch_play_command();

  if (active) {
    struct iap2_media_state polled;
    char track_path[258];
    void *rec = NULL;
    uint32_t gen = 0U;
    uint8_t want_path = (cur_session_gen != last_art_session_gen) ? 1U : 0U;
    arm_poll_media(&polled, track_path, sizeof(track_path), &rec, &gen,
                   want_path);

    irq = api->irq_save();
    if (!stopping) {
      shared_media = polled;
      shared_media_gen++;
    }
    api->irq_restore(irq);

    if (polled.has_track && rec != NULL) {
      if (rec != last_art_record || gen != last_art_gen ||
          cur_session_gen != last_art_session_gen) {
        irq = api->irq_save();
        uint8_t slot_st = art_slot_state;
        api->irq_restore(irq);

        if (slot_st == ART_SLOT_IDLE) {
          if (art_buf == NULL && RV_HAS(api, alloc) && api->alloc)
            art_buf = api->alloc(self, IAP2_ARTWORK_MAX_BYTES);
          size_t art_len = 0U;
          if (art_buf != NULL && track_path[0] != '\0' &&
              arm_extract_track_artwork(track_path, art_buf,
                                        IAP2_ARTWORK_MAX_BYTES, &art_len) &&
              art_len > 0U) {
            irq = api->irq_save();
            if (!stopping && art_slot_state == ART_SLOT_IDLE) {
              art_slot_key = polled.track_id;
              art_slot_len = art_len;
              art_slot_state = ART_SLOT_READY;
              art_cancel_req = 0U;
            }
            api->irq_restore(irq);
          }
          last_art_record = rec;
          last_art_gen = gen;
          last_art_session_gen = cur_session_gen;
        } else {
          irq = api->irq_save();
          if (!stopping && art_slot_state != ART_SLOT_IDLE)
            art_cancel_req = 1U;
          api->irq_restore(irq);
        }
      }
    } else if (last_art_record != NULL) {
      irq = api->irq_save();
      if (!stopping && art_slot_state != ART_SLOT_IDLE)
        art_cancel_req = 1U;
      api->irq_restore(irq);
      last_art_record = NULL;
      last_art_gen = 0U;
    }

    arm_step_library_sync(sum.lib_phase);
    arm_step_playlist_keys();
    arm_step_queue_sync(cur_session_gen, &sum);
  } else {
    last_art_record = NULL;
    last_art_gen = 0U;
    last_art_session_gen = 0U;
    ui_lib_gen = 0U;
    ui_lib_cursor_rec = NULL;
    ui_lib_next_batch_id = 0U;
    ui_lib_done = 0U;
    q_seen_sig = 0U;
    q_sent_sig = 0U;
    q_stable_ticks = 0U;
    ui_queue_from_car = 0U;
    last_q_session_gen = 0U;
    if (art_buf != NULL) {
      irq = api->irq_save();
      uint8_t can_free = (art_slot_state == ART_SLOT_IDLE) ? 1U : 0U;
      api->irq_restore(irq);
      if (can_free) {
        if (RV_HAS(api, free) && api->free)
          api->free(self, art_buf);
        art_buf = NULL;
      }
    }
    if (lib_batch_buf != NULL) {
      irq = api->irq_save();
      uint8_t can_free = (lib_batch_state == LIB_BATCH_IDLE) ? 1U : 0U;
      api->irq_restore(irq);
      if (can_free) {
        if (RV_HAS(api, free) && api->free)
          api->free(self, lib_batch_buf);
        lib_batch_buf = NULL;
      }
    }
    if (pl_keys_buf != NULL) {
      irq = api->irq_save();
      uint8_t can_free = (pl_slot_state == PL_SLOT_IDLE) ? 1U : 0U;
      api->irq_restore(irq);
      if (can_free) {
        if (RV_HAS(api, free) && api->free)
          api->free(self, pl_keys_buf);
        pl_keys_buf = NULL;
      }
    }
    if (q_keys_buf != NULL) {
      irq = api->irq_save();
      uint8_t can_free = (q_slot_state == Q_SLOT_IDLE) ? 1U : 0U;
      api->irq_restore(irq);
      if (can_free) {
        if (RV_HAS(api, free) && api->free)
          api->free(self, q_keys_buf);
        q_keys_buf = NULL;
      }
    }
  }

  uint8_t audio_act = sum.audio_active ? 1U : 0U;
  if (mode != last_ui_mode || usb_conn != last_ui_usb_conn ||
      tap_act != last_ui_tap_active || active != last_ui_tx_active ||
      replug != last_ui_replug || (uint8_t)sum.phase != last_ui_phase ||
      audio_act != last_ui_audio_active ||
      sum.audio_rate != last_ui_audio_rate) {
    last_ui_mode = mode;
    last_ui_usb_conn = usb_conn;
    last_ui_tap_active = tap_act;
    last_ui_tx_active = active;
    last_ui_replug = replug;
    last_ui_phase = (uint8_t)sum.phase;
    last_ui_audio_active = audio_act;
    last_ui_audio_rate = sum.audio_rate;
    if (RV_HAS(api, ui_refresh) && api->ui_refresh)
      api->ui_refresh(self, "car_audio");
    if (RV_HAS(api, pane_refresh) && api->pane_refresh)
      api->pane_refresh(self);
  }
}

static uint32_t on_hid_send_framed(uint8_t *hid, const void *data,
                                   uint32_t length, uint32_t flags,
                                   const void *next) {
  uint32_t irq = api->irq_save();
  uintptr_t blk = blocked_hid;
  api->irq_restore(irq);
  if (blk != 0U && (uintptr_t)hid == blk)
    return 0U;
  return api->hook_next(next, RV_HOOK_ARG(hid), RV_HOOK_ARG(data), length,
                        flags, 0, 0);
}

static uint32_t on_hid_send_framed_adjusted(uint8_t *transport,
                                            const void *data, uint32_t length,
                                            uint32_t flags, const void *next) {
  uint32_t irq = api->irq_save();
  uintptr_t blk = blocked_hid;
  api->irq_restore(irq);
  if (blk != 0U && transport != NULL &&
      (uintptr_t)(transport - USBHID_secondary_transport) == blk) {
    return 0U;
  }
  return api->hook_next(next, RV_HOOK_ARG(transport), RV_HOOK_ARG(data), length,
                        flags, 0, 0);
}

static uint32_t on_poll_timeout(void *controller, const void *next) {
  uint32_t res = api->hook_next(next, RV_HOOK_ARG(controller), 0, 0, 0, 0, 0);
  usb_sync_and_pump(controller);
  return res;
}

static uint32_t on_poll_wake(void *controller, const void *next) {
  uint32_t res = api->hook_next(next, RV_HOOK_ARG(controller), 0, 0, 0, 0, 0);
  usb_sync_and_pump(controller);
  return res;
}

static uint32_t on_release_endpoint(void *usb, uint32_t endpoint,
                                    const void *next) {
  iap2_native_tx_before_release(&native_tx, usb, endpoint);
  uint32_t res = api->hook_next(next, RV_HOOK_ARG(usb), endpoint, 0, 0, 0, 0);
  iap2_native_tx_after_release(&native_tx, usb, endpoint);
  usb_publish_state();
  return res;
}

static uint32_t on_shutdown_reset(void *controller, uint8_t *status,
                                  const void *next) {
  uint32_t res = api->hook_next(next, RV_HOOK_ARG(controller),
                                RV_HOOK_ARG(status), 0, 0, 0, 0);
  iap2_native_tx_after_reset(&native_tx, controller);
  usb_publish_state();
  return res;
}

static uint32_t on_configure_endpoint(void *usb, uint32_t endpoint,
                                      uint32_t type, uint32_t packet_size,
                                      const void *next, uint32_t arg4,
                                      uint32_t arg5) {
  if (native_tx.held && native_tx.draining && usb == native_tx.usb &&
      endpoint == USBHID_InEndpoint) {
    return (uint32_t)-1;
  }
  return api->hook_next7(next, RV_HOOK_ARG(usb), endpoint, type, packet_size,
                         arg4, arg5, 0);
}

static const struct rv_hook tx_hooks[] = {
    RV_HOOK_FILTER(RV_SITE_USBHID_SendFramedBytes_slot, on_hid_send_framed),
    RV_HOOK_FILTER(RV_SITE_USBHID_SendFramedBytes_Adjusted_slot,
                   on_hid_send_framed_adjusted),
    RV_HOOK_FILTER(RV_SITE_USBController_WaitEvent_PollTimeout_call,
                   on_poll_timeout),
    RV_HOOK_FILTER(RV_SITE_USBController_WaitEvent_PollWake_call, on_poll_wake),
    RV_HOOK_FILTER(RV_SITE_USBHID_Deactivate_ReleaseEndpoint_call,
                   on_release_endpoint),
    RV_HOOK_FILTER(RV_SITE_USB_DeactivateConfiguration_ReleaseEndpoint_call,
                   on_release_endpoint),
    RV_HOOK_FILTER(RV_SITE_USBController_Shutdown_Reset_call,
                   on_shutdown_reset),
    RV_HOOK_FILTER(RV_SITE_USBHID_Activate_ConfigureEndpoint_call,
                   on_configure_endpoint),
    RV_HOOK_FILTER(RV_SITE_USBMassStorage_Activate_ConfigureInEndpoint_call,
                   on_configure_endpoint),
};

static uint8_t bind_tx_hooks(void) {
  if (tx_hooks_bound)
    return 1U;
  if (!RV_HAS(api, hook_bind) || !api->hook_bind)
    return 0U;
  size_t n = sizeof(tx_hooks) / sizeof(tx_hooks[0]);
  for (size_t i = 0U; i < n; i++) {
    if (api->hook_bind(self, &tx_hooks[i]) != RV_OK) {
      while (i > 0U) {
        i--;
        if (RV_HAS(api, hook_unbind) && api->hook_unbind)
          api->hook_unbind(self, &tx_hooks[i]);
      }
      return 0U;
    }
  }
  tx_hooks_bound = 1U;
  return 1U;
}

static void unbind_tx_hooks(void) {
  if (!tx_hooks_bound)
    return;
  if (RV_HAS(api, hook_unbind) && api->hook_unbind) {
    size_t n = sizeof(tx_hooks) / sizeof(tx_hooks[0]);
    for (size_t i = n; i > 0U; i--)
      api->hook_unbind(self, &tx_hooks[i - 1U]);
  }
  tx_hooks_bound = 0U;
}
#endif

static const struct rv_row car_rows[] = {
    RV_ROW_SETTING("Protocol", KEY_PROTOCOL_MODE),
    RV_ROW_INFO("Status", row_car_status_text),
    RV_ROW_PUSH("About Modes", "car_about"),
};

static const struct rv_pane car_panes[] = {
    {.caption_text = pane_protocol_caption,
     .bitmap = RV_BITMAP("settings-car")},
    {.caption_text = pane_status_caption, .bitmap = RV_BITMAP("settings-car")},
    {.caption = "Older vs. newer", .bitmap = RV_BITMAP("settings-car")},
};

static const char *const about_paragraphs[] = {
    "Choose how your iPod talks to car stereos and docks over USB.",
    "iAP2 (Newer Cars): Works with modern head units like VW and Skoda MIB3. "
    "Supports USB digital audio, full library browsing, album artwork, and "
    "steering wheel controls.",
    "iAP1 (Older Cars): Uses Apple's classic accessory protocol for older "
    "30-pin car stereos, speaker docks, and legacy cables.",
    "If your iPod is currently plugged in, unplug and reconnect the USB cable "
    "after switching modes.",
};

static const struct rv_screen screens[] = {
    RV_SCREEN_LIST_PANES("car_audio", "Car Audio", car_rows, car_panes),
    RV_SCREEN_TEXT("car_about", "Car Audio", about_paragraphs),
};

static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_SETTINGS, RV_SETTINGS_SOUND_CHECK,
                  RV_ROW_PUSH("Car Audio", "car_audio"),
                  .caption_text = pane_settings_caption,
                  .bitmap = RV_BITMAP("settings-car")),
};

static const char *const protocol_labels[] = {"iAP1", "iAP2"};
static const struct rv_setting settings[] = {
    {
        .key = KEY_PROTOCOL_MODE,
        .type = RV_SETTING_TYPE_INT,
        .style = RV_SETTING_STYLE_NONE,
        .fallback = IAP2_MODE_IAP2,
        .minimum = IAP2_MODE_IAP1,
        .maximum = IAP2_MODE_IAP2,
        .label = "Car Audio Protocol",
        .labels = protocol_labels,
        .label_count = RV_COUNT(protocol_labels),
    },
};

static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER(RV_SITE_USBHID_Activate_slot, on_hid_activate),
    RV_HOOK_FILTER(RV_SITE_USBHID_Deactivate_slot, on_hid_deactivate),
    RV_HOOK_FILTER(RV_SITE_USBHID_HandleControlWrite_slot,
                   on_hid_control_write),
};

static void on_setting_changed(uint16_t key) {
  if (key != KEY_PROTOCOL_MODE && key != RV_SETTING_KEY_ALL)
    return;

  int mode = (api->setting_int(self, KEY_PROTOCOL_MODE) == IAP2_MODE_IAP1)
                 ? IAP2_MODE_IAP1
                 : IAP2_MODE_IAP2;
#ifdef __arm__
  uint8_t bound_ok = 0U;
  if (mode == IAP2_MODE_IAP2 && !stopping)
    bound_ok = bind_tx_hooks();
#endif
  uint32_t irq = api->irq_save();
  if (!stopping) {
    diag_state.protocol_mode = (uint8_t)mode;
    diag_state.tap_desired = (mode == IAP2_MODE_IAP2) ? 1U : 0U;
#ifdef __arm__
    diag_state.tx_desired = (mode == IAP2_MODE_IAP2 && bound_ok) ? 1U : 0U;
#else
    diag_state.tx_desired = 0U;
#endif
  }
  api->irq_restore(irq);

  if (RV_HAS(api, ui_refresh) && api->ui_refresh)
    api->ui_refresh(self, "car_audio");
  if (RV_HAS(api, pane_refresh) && api->pane_refresh)
    api->pane_refresh(self);
}

static void unload(void) {
#ifdef __arm__
  if (media_timer && RV_HAS(api, timer_stop) && api->timer_stop) {
    api->timer_stop(self, media_timer);
    media_timer = NULL;
  }
#endif
  uint32_t irq = api->irq_save();
  stopping = 1;
  diag_state.usb_connected = 0;
  diag_state.tap_desired = 0;
  diag_state.tap_active = 0;
  diag_state.tx_desired = 0;
  diag_state.tx_active = 0;
  diag_state.session_active = 0;
#ifdef __arm__
  blocked_hid = 0;
  usb_audio_route_wanted = 0U;
  play_cmd_grace_ticks = 0U;
  art_cancel_req = 0U;
  art_slot_state = ART_SLOT_IDLE;
  lib_req_active = 0U;
  lib_req_gen++;
  lib_begin_state = LIB_BEGIN_IDLE;
  lib_batch_state = LIB_BATCH_IDLE;
  pl_req_pending = 0U;
  pl_slot_state = PL_SLOT_IDLE;
  q_slot_state = Q_SLOT_IDLE;
  pending_play_kind = PLAY_CMD_NONE;
#endif
  api->irq_restore(irq);
#ifdef __arm__
  unbind_tx_hooks();
  if (digital_audio_primed)
    unprime_digital_audio();
  if (usb_audio_route_applied)
    arm_apply_usb_audio_route(0U);
  if (art_buf != NULL) {
    if (RV_HAS(api, free) && api->free)
      api->free(self, art_buf);
    art_buf = NULL;
  }
  if (lib_batch_buf != NULL) {
    if (RV_HAS(api, free) && api->free)
      api->free(self, lib_batch_buf);
    lib_batch_buf = NULL;
  }
  if (pl_keys_buf != NULL) {
    if (RV_HAS(api, free) && api->free)
      api->free(self, pl_keys_buf);
    pl_keys_buf = NULL;
  }
  if (q_keys_buf != NULL) {
    if (RV_HAS(api, free) && api->free)
      api->free(self, q_keys_buf);
    q_keys_buf = NULL;
  }
#endif
}

static const struct rv_descriptor descriptor = {
    .size = sizeof descriptor,
    RV_HOOKS(hooks),
    RV_SETTINGS(settings),
    .setting_changed = on_setting_changed,
    RV_SCREENS(screens),
    RV_ENTRIES(entries),
    .unload = unload,
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core,
                                            rv_extension_handle handle) {
  api = core;
  self = handle;
  stopping = 0;
  active_hid_cookie = 0;
  iap2_diag_init(&diag_state);
  iap2_rx_tap_init(&rx_tap);
#ifdef __arm__
  digital_audio_primed = 0U;
  usb_audio_route_wanted = 0U;
  usb_audio_route_applied = 0U;
  blocked_hid = 0U;
  tx_hooks_bound = 0U;
  media_timer = NULL;
  shared_media_gen = 0U;
  session_media_gen = 0U;
  pending_actions_mask = 0U;
  pending_seek_target_ms = 0U;
  pending_seek_valid = 0U;
  play_cmd_grace_ticks = 0U;
  last_ui_mode = IAP2_MODE_IAP2;
  last_ui_usb_conn = 0U;
  last_ui_tap_active = 0U;
  last_ui_tx_active = 0U;
  last_ui_replug = 0U;
  last_ui_phase = 0U;
  last_ui_audio_active = 0U;
  last_ui_audio_rate = 0U;

  art_buf = NULL;
  art_slot_state = ART_SLOT_IDLE;
  art_cancel_req = 0U;
  art_slot_key = 0U;
  art_slot_len = 0U;
  last_art_record = NULL;
  last_art_gen = 0U;
  last_art_session_gen = 0U;

  lib_req_active = 0U;
  lib_req_gen = 0U;
  lib_begin_state = LIB_BEGIN_IDLE;
  lib_begin_gen = 0U;
  lib_begin_total = 0U;
  lib_begin_rev[0] = '\0';
  lib_begin_pl_count = 0U;
  lib_batch_buf = NULL;
  lib_batch_state = LIB_BATCH_IDLE;
  lib_batch_gen = 0U;
  lib_batch_id = 0U;
  lib_batch_count = 0U;
  lib_batch_eof = 0U;
  ui_lib_gen = 0U;
  ui_lib_cursor_rec = NULL;
  ui_lib_next_batch_id = 0U;
  ui_lib_done = 0U;
  pl_req_pending = 0U;
  pl_req_gen = 0U;
  pl_req_index = 0U;
  pl_req_id = 0U;
  pl_keys_buf = NULL;
  pl_slot_state = PL_SLOT_IDLE;
  pl_slot_gen = 0U;
  pl_slot_index = 0U;
  pl_slot_count = 0U;
  q_keys_buf = NULL;
  q_slot_state = Q_SLOT_IDLE;
  q_slot_sig = 0U;
  q_slot_count = 0U;
  q_slot_from_car = 0U;
  q_seen_sig = 0U;
  q_sent_sig = 0U;
  q_stable_ticks = 0U;
  ui_queue_from_car = 0U;
  last_q_session_gen = 0U;
  pending_play_kind = PLAY_CMD_NONE;
  pending_play_count = 0U;
  pending_play_start_idx = 0U;
  pending_play_coll_id = 0U;
  pending_play_coll_type = 0U;
  pending_play_special_type = 0U;
  pending_play_start_item_id = 0U;

  rx_tap.callback_context = &ctrl_session;
  rx_tap.on_payload = sess_on_rx_payload;
  rx_tap.on_reset = sess_on_rx_reset;
  struct iap2_session_ops sops = {
      .context = NULL,
      .random32 = sess_random32,
      .on_authenticated = NULL,
      .on_identified = NULL,
      .on_usb_audio = sess_on_usb_audio,
      .on_artwork_done = sess_on_artwork_done,
      .on_hid_action = sess_on_hid_action,
      .on_seek_ms = sess_on_seek_ms,
      .on_library_start = sess_on_library_start,
      .on_library_stop = sess_on_library_stop,
      .on_library_batch_done = sess_on_library_batch_done,
      .on_playlist_keys_request = sess_on_playlist_keys_request,
      .on_playlist_keys_done = sess_on_playlist_keys_done,
      .on_queue_keys_done = sess_on_queue_keys_done,
      .on_play_library_items = sess_on_play_library_items,
      .on_play_library_collection = sess_on_play_library_collection,
      .on_play_library_special = sess_on_play_library_special,
  };
  iap2_session_init(&ctrl_session, &rx_tap.link, &sops);

  memset(&shared_media, 0, sizeof(shared_media));
  copy_cstr(shared_media.device_name, sizeof(shared_media.device_name),
            "iPod Classic");
  shared_media.battery_pct = 100U;
  shared_media.charging = true;
  shared_media.sample_rate = DigitalAudio_DefaultRate;
  shared_media_gen = 1U;
  iap2_session_set_media(&ctrl_session, &shared_media);
  diag_state.shared_session_summary = ctrl_session.summary;
#endif
  diag_state.shared_rx_stats = rx_tap.stats;
  diag_state.shared_link_summary = rx_tap.summary;
#ifdef __arm__
  if (RV_HAS(api, timer_start) && api->timer_start)
    media_timer = api->timer_start(self, 50U, 1, on_media_timer, NULL);
#endif
  return &descriptor;
}
