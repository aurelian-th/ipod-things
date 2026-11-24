/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef LYRICS_LRC_H
#define LYRICS_LRC_H

#include <stddef.h>
#include <stdint.h>

#define LRC_MAX_LINES    300U
#define LRC_MAX_LINE_LEN 96U
#define LRC_LINE_HEIGHT  22
#define LRC_FILE_MAX     16384U
#define LRC_LINE_NONE    0xffffffffu

struct lrc_line {
    uint32_t timestamp_ms;
    char text[LRC_MAX_LINE_LEN];
};

struct lrc_state {
    struct lrc_line lines[LRC_MAX_LINES];
    uint32_t line_count;
    uint32_t current_line;
    int32_t scroll_offset_px;
    int32_t target_scroll_offset_px;
    uint32_t manual_scroll_ticks;
    int has_synced_timestamps;
    int follow_player;
    void *track_record;
    uint32_t track_generation;
    char song_title[64];
    char song_artist[64];
};

void lrc_state_reset(struct lrc_state *st);
uint32_t lrc_parse(struct lrc_state *st, const char *content, uint32_t length);
uint32_t lrc_find_active_line(const struct lrc_line *lines, uint32_t line_count,
                              uint32_t current_ms);
int lrc_update_playback(struct lrc_state *st, uint32_t current_ms);
void lrc_sanitize_filename(char *dst, const char *src, size_t max_len);
size_t lrc_build_candidates(const char *artist, const char *title,
                            char out_paths[4][160]);

#endif /* LYRICS_LRC_H */

