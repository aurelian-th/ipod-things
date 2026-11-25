/* SPDX-License-Identifier: GPL-3.0-only */

/* Synced and unsynced .lrc lyrics viewer in song and Now Playing context menus.
 * Ported from RepriseOS payload/lyrics.c to the RevolaOS canvas and media APIs. */

#include "embedded.h"
#include "lrc.h"
#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_canvas.h"
#include "rv_libc.h"
#include "rv_media.h"
#include "rv_ui.h"
#include <stddef.h>

#define FORK_DATA          0x64617461u
#define TICK_MS            50U
#define MANUAL_HOLD_TICKS  60U /* 60 * 50 ms = 3 seconds */

struct legacy_path {
    uint16_t volume;
    char text[258];
};

static const struct rv_api *api;
static rv_extension_handle self;
static struct lrc_state *state;
static struct rv_timer *tick_timer;

static void copy_field(char *dst, size_t cap, const char *src)
{
    size_t i = 0U;

    if (!dst || cap == 0U)
        return;
    if (src) {
        while (src[i] && i + 1U < cap) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

static int str_eq(const char *a, const char *b)
{
    size_t i = 0U;

    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i])
            return 0;
        i++;
    }
    return a[i] == b[i];
}

static void normalize_track_path(char *dst, size_t cap, const char *src,
                                 char sep, int leading_sep)
{
    size_t pos = 0U;

    if (!dst || cap == 0U)
        return;
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

static int legacy_lyr_read(void *ctx, void *dst, size_t len, size_t *out_got)
{
    uint32_t got = 0U;
    int rc = File_ReadLegacyHandle(ctx, dst, (uint32_t)len, 0U, &got);

    if (out_got)
        *out_got = (size_t)got;
    return (rc == 0 || rc == (int)FileError_EndOfFile) && got > 0U;
}

static int legacy_lyr_seek(void *ctx, uint64_t offset)
{
    return File_SeekLegacyHandle(ctx, offset) == 0;
}

static int try_open_embedded(struct lrc_state *st, const void *item, char *scratch)
{
    char raw_path[258];
    uint32_t p[NativeString_bytes / 4];
    struct NativeString *pns = (struct NativeString *)p;

    raw_path[0] = '\0';
    MediaItem_GetPath(pns, (struct MediaItem *)(uintptr_t)item);
    copy_field(raw_path, sizeof(raw_path), NativeString_GetUtf8(pns));
    NativeString_Destroy(pns);
    if (raw_path[0] == '\0')
        return 0;

    char candidates[5][258];
    copy_field(candidates[0], sizeof(candidates[0]), raw_path);
    normalize_track_path(candidates[1], sizeof(candidates[1]), raw_path, '/', 0);
    normalize_track_path(candidates[2], sizeof(candidates[2]), raw_path, '/', 1);
    normalize_track_path(candidates[3], sizeof(candidates[3]), raw_path, '\\', 0);
    normalize_track_path(candidates[4], sizeof(candidates[4]), raw_path, '\\', 1);

    for (size_t i = 0U; i < 5U; i++) {
        if (candidates[i][0] == '\0')
            continue;
        int dup = 0;
        for (size_t prev = 0U; prev < i; prev++) {
            if (str_eq(candidates[prev], candidates[i])) {
                dup = 1;
                break;
            }
        }
        if (dup)
            continue;

        struct legacy_path lp;
        lp.volume = 0U;
        copy_field(lp.text, sizeof(lp.text), candidates[i]);
        void *handle = NULL;
        if (File_OpenLegacyPath(&lp, FORK_DATA, 1, &handle) != 0 || !handle)
            continue;

        struct lyrics_reader reader = {
            .context = handle,
            .read = legacy_lyr_read,
            .seek = legacy_lyr_seek,
        };
        int ok = lyrics_extract_embedded(&reader, st, scratch, LRC_FILE_MAX);
        File_CloseLegacyHandle(handle);
        if (ok && st->line_count > 0U)
            return 1;
    }
    return 0;
}

static int try_open_and_parse(struct lrc_state *st, const char *rel_path, char *buf)
{
    struct legacy_path path;
    void *handle = NULL;
    uint32_t total = 0U;

    path.volume = 0U;
    copy_field(path.text, sizeof(path.text), rel_path);

    if (File_OpenLegacyPath(&path, FORK_DATA, 1, &handle) != 0)
        return 0;

    while (total < LRC_FILE_MAX - 1U) {
        uint32_t got = 0U;
        if (File_ReadLegacyHandle(handle, buf + total,
                                  (LRC_FILE_MAX - 1U) - total, 0, &got) != 0 ||
            got == 0U) {
            break;
        }
        total += got;
    }
    buf[total] = '\0';
    File_CloseLegacyHandle(handle);

    if (total > 0U) {
        lrc_parse(st, buf, total);
        return 1;
    }
    return 0;
}

static void load_for_item(struct lrc_state *st, const void *item)
{
    char candidates[4][160];
    char *lrc_buf;
    size_t count;

    lrc_state_reset(st);
    st->song_title[0] = '\0';
    st->song_artist[0] = '\0';
    st->track_record = NULL;
    st->track_generation = 0U;

    if (!item || !rv_item_valid(item))
        return;

    st->track_record = RV_FIELD(item, MediaItem_record, void *);
    st->track_generation = RV_FIELD(item, MediaItem_generation, uint32_t);

    {
        uint32_t s[NativeString_bytes / 4];
        struct NativeString *str = (struct NativeString *)s;
        const char *title = rv_item_text(item, MediaItem_title_method, str);
        copy_field(st->song_title, sizeof(st->song_title), title);
        NativeString_Destroy(str);
    }
    {
        uint32_t s[NativeString_bytes / 4];
        struct NativeString *str = (struct NativeString *)s;
        const char *artist = rv_item_text(item, MediaItem_artist_method, str);
        copy_field(st->song_artist, sizeof(st->song_artist), artist);
        NativeString_Destroy(str);
    }

    lrc_buf = api->alloc(self, LRC_FILE_MAX);
    if (!lrc_buf)
        return;

    if (!try_open_embedded(st, item, lrc_buf)) {
        count = lrc_build_candidates(st->song_artist, st->song_title, candidates);
        for (size_t i = 0U; i < count; i++) {
            if (try_open_and_parse(st, candidates[i], lrc_buf))
                break;
            /* Also try forward slashes for FAT path compatibility. */
            char slash[160];
            size_t j = 0U;
            while (candidates[i][j] && j + 1U < sizeof(slash)) {
                slash[j] = (candidates[i][j] == '\\') ? '/' : candidates[i][j];
                j++;
            }
            slash[j] = '\0';
            if (strcmp(slash, candidates[i]) != 0 &&
                try_open_and_parse(st, slash, lrc_buf)) {
                break;
            }
        }
    }

    api->free(self, lrc_buf);
}

static void stop_tick_timer(void)
{
    if (tick_timer) {
        api->timer_stop(self, tick_timer);
        tick_timer = NULL;
    }
}

static void release_state(void)
{
    stop_tick_timer();
    if (state) {
        api->free(self, state);
        state = NULL;
    }
}

static void snap_to_active(struct lrc_state *st, int immediate)
{
    int32_t target = (st->has_synced_timestamps && st->current_line != LRC_LINE_NONE)
                         ? lrc_scroll_for_line(st, st->current_line)
                         : 0;
    st->target_scroll_offset_px = target;
    if (immediate)
        st->scroll_offset_px = target;
}

static void on_tick(void *arg)
{
    (void)arg;
    struct lrc_state *st = state;
    if (!st)
        return;

    int dirty = 0;

    if (st->follow_player) {
        void *player = rv_player_lock();
        uint32_t current_ms = 0U;
        int changed = 0;
        uint32_t cur_words[MediaItem_bytes / 4];
        struct MediaItem *cur = (struct MediaItem *)cur_words;

        MediaItem_InitFromRecord(cur, NULL, 0);
        if (player && RV_FIELD(player, MediaPlayer_current_track, void *)) {
            rv_player_current(player, cur);
            if (MediaItem_IsValid(cur)) {
                void *rec = RV_FIELD(cur, MediaItem_record, void *);
                uint32_t gen = RV_FIELD(cur, MediaItem_generation, uint32_t);
                if (rec != st->track_record || gen != st->track_generation)
                    changed = 1;
                current_ms = MediaPlayer_GetPosition(player);
            }
        }
        if (player)
            rv_player_unlock(player);

        if (changed) {
            load_for_item(st, cur);
            dirty = 1;
        }
        MediaItem_Destroy(cur);

        if (lrc_update_playback(st, current_ms))
            dirty = 1;
    }

    if (st->manual_scroll_ticks > 0U) {
        st->manual_scroll_ticks--;
        if (st->manual_scroll_ticks == 0U && st->has_synced_timestamps) {
            snap_to_active(st, 0);
            dirty = 1;
        }
    }

    if (st->scroll_offset_px != st->target_scroll_offset_px) {
        int32_t diff = st->target_scroll_offset_px - st->scroll_offset_px;
        int32_t step = diff / 2;
        if (step == 0)
            step = (diff > 0) ? 1 : -1;
        st->scroll_offset_px += step;
        dirty = 1;
    }

    if (dirty)
        api->ui_invalidate(self);
}

static void opened(const struct rv_context *ctx)
{
    release_state();

    state = api->alloc(self, sizeof *state);
    if (!state)
        return;

    uint32_t cur_words[MediaItem_bytes / 4];
    struct MediaItem *cur = (struct MediaItem *)cur_words;
    const void *target = ctx ? ctx->item : NULL;
    uint32_t current_ms = 0U;
    int player_has_track = 0;

    MediaItem_InitFromRecord(cur, NULL, 0);
    {
        void *player = rv_player_lock();
        if (player && RV_FIELD(player, MediaPlayer_current_track, void *)) {
            rv_player_current(player, cur);
            if (MediaItem_IsValid(cur)) {
                player_has_track = 1;
                current_ms = MediaPlayer_GetPosition(player);
            }
        }
        if (player)
            rv_player_unlock(player);
    }

    if (!target && player_has_track)
        target = cur;

    state->follow_player = (!ctx || !ctx->item ||
                            (player_has_track && rv_item_same(target, cur)))
                               ? 1
                               : 0;

    load_for_item(state, target);
    MediaItem_Destroy(cur);

    if (state->follow_player) {
        lrc_update_playback(state, current_ms);
        snap_to_active(state, 1);
    }

    tick_timer = api->timer_start(self, TICK_MS, 1, on_tick, NULL);
    api->ui_invalidate(self);
}

static void closed(const struct rv_context *ctx)
{
    (void)ctx;
    release_state();
}

static void draw(const struct rv_context *ctx, const struct rv_gfx *gfx, int width, int height)
{
    (void)ctx;
    struct rv_canvas canvas;
    const struct lrc_state *st = state;

    rv_canvas_begin(&canvas, gfx, width, height);

    if (!st || st->row_count == 0U) {
        rv_canvas_text(&canvas, 0, 56, width, 24, "No Lyrics Found", 1,
                       RV_ALIGN_CENTER, RV_CANVAS_TEXT);
        rv_canvas_text(&canvas, 8, 92, width - 16, 20,
                       "Add embedded lyrics or .lrc in /Lyrics/", 0,
                       RV_ALIGN_CENTER, RV_CANVAS_MUTED);
        if (st && st->song_title[0]) {
            rv_canvas_text(&canvas, 8, 128, width - 16, 20, st->song_title, 0,
                           RV_ALIGN_CENTER, RV_CANVAS_INACTIVE);
        }
        return;
    }

    int center_y = (height - LRC_LINE_HEIGHT) / 2;
    for (uint32_t r = 0U; r < st->row_count; r++) {
        int y = center_y + (int)r * LRC_LINE_HEIGHT - st->scroll_offset_px;
        if (y + LRC_LINE_HEIGHT < 0 || y >= height)
            continue;

        const struct lrc_row *row = &st->rows[r];
        if (row->len == 0U)
            continue;

        char seg[LRC_MAX_LINE_LEN];
        size_t len = row->len;
        if (len >= sizeof(seg))
            len = sizeof(seg) - 1U;
        memcpy(seg, st->lines[row->line_idx].text + row->offset, len);
        seg[len] = '\0';

        int active = (st->current_line != LRC_LINE_NONE &&
                      row->line_idx == st->current_line);
        /* Wrapped lines must keep one uniform font size across every segment;
         * passing large=1 per segment makes trailing short rows jump to RV_FONT_LARGE. */
        int large = active && st->line_row_count[row->line_idx] <= 1U;
        uint8_t color = active ? RV_CANVAS_ACCENT : RV_CANVAS_MUTED;
        rv_canvas_text(&canvas, 10, y, width - 20, LRC_LINE_HEIGHT, seg,
                       large, RV_ALIGN_CENTER, color);
    }
}

static int input(const struct rv_context *ctx, const struct rv_input *in)
{
    (void)ctx;
    struct lrc_state *st = state;

    if (!st || !in)
        return 0;

    if (in->kind == RV_INPUT_WHEEL) {
        if (st->row_count > 0U && in->delta != 0) {
            int32_t max_scroll = (int32_t)(st->row_count - 1U) * LRC_LINE_HEIGHT;
            int32_t next = st->target_scroll_offset_px + (int32_t)in->delta * LRC_LINE_HEIGHT;
            if (next < 0)
                next = 0;
            if (next > max_scroll)
                next = max_scroll;
            st->target_scroll_offset_px = next;
            st->scroll_offset_px = next;
            st->manual_scroll_ticks = MANUAL_HOLD_TICKS;
            api->ui_invalidate(self);
        }
        return 1;
    }

    if (in->kind == RV_INPUT_PRESS)
        return in->button == RV_BUTTON_CENTER;

    if (in->kind == RV_INPUT_RELEASE && in->button == RV_BUTTON_CENTER) {
        st->manual_scroll_ticks = 0U;
        if (st->has_synced_timestamps)
            snap_to_active(st, 0);
        api->ui_invalidate(self);
        return 1;
    }

    return 0;
}

static int has_song_item(const struct rv_context *ctx)
{
    return ctx && ctx->item != NULL && rv_item_valid(ctx->item);
}

static int has_now_playing_item(const struct rv_context *ctx)
{
    if (ctx && ctx->item && rv_item_valid(ctx->item))
        return 1;
    void *player = rv_player_lock();
    int active = player && RV_FIELD(player, MediaPlayer_current_track, void *) != NULL;
    if (player)
        rv_player_unlock(player);
    return active;
}

static const struct rv_screen screens[] = {
    RV_SCREEN_CANVAS("lyrics", "Lyrics", draw, input,
                     .opened = opened, .closed = closed),
};

static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_SONG_CONTEXT, 0, RV_ROW_PUSH("Lyrics", "lyrics"),
                  .available = has_song_item),
    RV_MENU_ENTRY(RV_MENU_NOW_PLAYING_CONTEXT, 0, RV_ROW_PUSH("Lyrics", "lyrics"),
                  .available = has_now_playing_item),
};

static const struct rv_descriptor extension = {
    .size = sizeof extension,
    .unload = release_state,
    RV_SCREENS(screens),
    RV_ENTRIES(entries),
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    api = core;
    self = handle;
    return &extension;
}
