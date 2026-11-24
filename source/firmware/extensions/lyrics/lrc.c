/* SPDX-License-Identifier: GPL-3.0-only */
#include "lrc.h"

#ifdef __arm__
#include "rv_libc.h"
#else
#include <string.h>
#endif

static void copy_str(char *dst, const char *src, size_t cap)
{
    size_t i = 0;

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

static size_t append_str(char *dst, size_t at, size_t cap, const char *src)
{
    if (!src || !dst || cap == 0U)
        return at;
    while (*src && at + 1U < cap)
        dst[at++] = *src++;
    dst[at] = '\0';
    return at;
}

void lrc_state_reset(struct lrc_state *st)
{
    if (!st)
        return;
    st->line_count = 0;
    st->current_line = LRC_LINE_NONE;
    st->scroll_offset_px = 0;
    st->target_scroll_offset_px = 0;
    st->manual_scroll_ticks = 0;
    st->has_synced_timestamps = 0;
}

static const char *parse_timestamp(const char *ptr, uint32_t *out_ms)
{
    if (*ptr != '[')
        return NULL;
    ptr++;
    if (*ptr < '0' || *ptr > '9')
        return NULL;
    uint32_t min = 0;
    while (*ptr >= '0' && *ptr <= '9') {
        if (min <= 100000U)
            min = min * 10U + (uint32_t)(*ptr - '0');
        ptr++;
    }
    if (*ptr != ':')
        return NULL;
    ptr++;
    if (*ptr < '0' || *ptr > '9')
        return NULL;
    uint32_t sec = 0;
    while (*ptr >= '0' && *ptr <= '9') {
        if (sec <= 100000U)
            sec = sec * 10U + (uint32_t)(*ptr - '0');
        ptr++;
    }
    uint32_t frac_ms = 0;
    if (*ptr == '.' || *ptr == ':') {
        ptr++;
        uint32_t frac = 0;
        int digits = 0;
        while (*ptr >= '0' && *ptr <= '9') {
            if (digits < 6)
                frac = frac * 10U + (uint32_t)(*ptr - '0');
            digits++;
            ptr++;
        }
        if (digits == 1) {
            frac_ms = frac * 100U;
        } else if (digits == 2) {
            frac_ms = frac * 10U;
        } else if (digits >= 3) {
            int keep = digits < 6 ? digits : 6;
            while (keep > 3) {
                frac /= 10U;
                keep--;
            }
            frac_ms = frac;
        }
    }
    if (*ptr != ']')
        return NULL;
    *out_ms = (min * 60U + sec) * 1000U + frac_ms;
    return ptr + 1;
}

static int is_offset_tag(const char *s, const char *end)
{
    static const char tag[] = "[offset:";

    if ((size_t)(end - s) <= 8U)
        return 0;
    for (size_t i = 0; i < 8U; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + ('a' - 'A'));
        if (c != tag[i])
            return 0;
    }
    return 1;
}

uint32_t lrc_parse(struct lrc_state *st, const char *content, uint32_t length)
{
    if (!st)
        return 0;
    lrc_state_reset(st);
    if (!content || length == 0U)
        return 0;

    int32_t global_offset_ms = 0;
    const char *p = content;
    const char *end = content + length;

    while (p < end && st->line_count < LRC_MAX_LINES) {
        while (p < end && (*p == ' ' || *p == '\t'))
            p++;
        if (p >= end)
            break;

        const char *line_start = p;
        while (p < end && *p != '\r' && *p != '\n')
            p++;
        const char *line_end = p;
        while (p < end && (*p == '\r' || *p == '\n'))
            p++;

        if (line_end == line_start)
            continue;

        if (is_offset_tag(line_start, line_end)) {
            const char *opt = line_start + 8;
            int32_t sign = 1;
            if (*opt == '+') {
                opt++;
            } else if (*opt == '-') {
                sign = -1;
                opt++;
            }
            int32_t val = 0;
            while (opt < line_end && *opt >= '0' && *opt <= '9') {
                if (val <= 10000000)
                    val = val * 10 + (*opt - '0');
                opt++;
            }
            global_offset_ms = sign * val;
            continue;
        }

        if (line_start[0] == '[' && line_start + 3 < line_end &&
            line_start[1] >= 'a' && line_start[1] <= 'z' &&
            line_start[2] >= 'a' && line_start[2] <= 'z' &&
            line_start[3] == ':') {
            continue;
        }

        uint32_t timestamps[8];
        uint32_t ts_count = 0;
        const char *cur = line_start;

        while (cur < line_end && *cur == '[' && ts_count < 8U) {
            uint32_t ts = 0;
            const char *after = parse_timestamp(cur, &ts);
            if (!after)
                break;
            timestamps[ts_count++] = ts;
            cur = after;
        }

        while (cur < line_end && (*cur == ' ' || *cur == '\t'))
            cur++;
        const char *text_start = cur;
        const char *text_end = line_end;
        while (text_end > text_start &&
               (text_end[-1] == ' ' || text_end[-1] == '\t' ||
                text_end[-1] == '\r' || text_end[-1] == '\n'))
            text_end--;

        size_t text_len = (size_t)(text_end - text_start);
        if (text_len >= LRC_MAX_LINE_LEN)
            text_len = LRC_MAX_LINE_LEN - 1U;

        if (ts_count > 0U) {
            st->has_synced_timestamps = 1;
            for (uint32_t t = 0; t < ts_count && st->line_count < LRC_MAX_LINES; t++) {
                struct lrc_line *line = &st->lines[st->line_count++];
                int64_t adjusted = (int64_t)timestamps[t] + (int64_t)global_offset_ms;
                line->timestamp_ms = adjusted > 0 ? (uint32_t)adjusted : 0U;
                if (text_len > 0U)
                    memcpy(line->text, text_start, text_len);
                line->text[text_len] = '\0';
            }
        } else if (text_len > 0U) {
            struct lrc_line *line = &st->lines[st->line_count++];
            line->timestamp_ms = 0U;
            memcpy(line->text, text_start, text_len);
            line->text[text_len] = '\0';
        }
    }

    if (st->has_synced_timestamps && st->line_count > 1U) {
        for (uint32_t i = 1U; i < st->line_count; i++) {
            struct lrc_line key = st->lines[i];
            int j = (int)i - 1;
            while (j >= 0 && st->lines[j].timestamp_ms > key.timestamp_ms) {
                st->lines[j + 1] = st->lines[j];
                j--;
            }
            st->lines[j + 1] = key;
        }
    }

    return st->line_count;
}

uint32_t lrc_find_active_line(const struct lrc_line *lines, uint32_t line_count,
                              uint32_t current_ms)
{
    if (!lines || line_count == 0U)
        return LRC_LINE_NONE;
    if (current_ms < lines[0].timestamp_ms)
        return LRC_LINE_NONE;

    uint32_t active = 0U;
    for (uint32_t i = 1U; i < line_count; i++) {
        if (lines[i].timestamp_ms <= current_ms)
            active = i;
        else
            break;
    }
    return active;
}

int lrc_update_playback(struct lrc_state *st, uint32_t current_ms)
{
    if (!st || !st->has_synced_timestamps || st->line_count == 0U)
        return 0;

    uint32_t active = lrc_find_active_line(st->lines, st->line_count, current_ms);
    if (active != st->current_line) {
        st->current_line = active;
        if (st->manual_scroll_ticks == 0U) {
            st->target_scroll_offset_px =
                (active != LRC_LINE_NONE) ? (int32_t)(active * LRC_LINE_HEIGHT) : 0;
        }
        return 1;
    }
    return 0;
}

void lrc_sanitize_filename(char *dst, const char *src, size_t max_len)
{
    size_t i = 0U;

    if (!dst || max_len == 0U)
        return;
    if (src) {
        while (*src && i + 1U < max_len) {
            char c = *src++;
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                c == '"' || c == '<' || c == '>' || c == '|') {
                c = '_';
            }
            dst[i++] = c;
        }
    }
    dst[i] = '\0';
}

size_t lrc_build_candidates(const char *artist, const char *title,
                            char out_paths[4][160])
{
    char clean_artist[64];
    char clean_title[64];
    size_t count = 0U;

    lrc_sanitize_filename(clean_artist, artist, sizeof(clean_artist));
    lrc_sanitize_filename(clean_title, title, sizeof(clean_title));

    if (clean_artist[0] && clean_title[0]) {
        size_t at = 0U;
        copy_str(out_paths[count], "Lyrics\\", 160U);
        at = 7U;
        at = append_str(out_paths[count], at, 160U, clean_artist);
        at = append_str(out_paths[count], at, 160U, " - ");
        at = append_str(out_paths[count], at, 160U, clean_title);
        append_str(out_paths[count], at, 160U, ".lrc");
        count++;
    }

    if (clean_title[0]) {
        size_t at = 0U;
        copy_str(out_paths[count], "Lyrics\\", 160U);
        at = 7U;
        at = append_str(out_paths[count], at, 160U, clean_title);
        append_str(out_paths[count], at, 160U, ".lrc");
        count++;
    }

    if (clean_artist[0] && clean_title[0]) {
        size_t at = 0U;
        out_paths[count][0] = '\0';
        at = append_str(out_paths[count], at, 160U, clean_artist);
        at = append_str(out_paths[count], at, 160U, " - ");
        at = append_str(out_paths[count], at, 160U, clean_title);
        append_str(out_paths[count], at, 160U, ".lrc");
        count++;
    }

    if (clean_title[0]) {
        size_t at = 0U;
        out_paths[count][0] = '\0';
        at = append_str(out_paths[count], at, 160U, clean_title);
        append_str(out_paths[count], at, 160U, ".lrc");
        count++;
    }

    return count;
}

