/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "embedded.h"
#include "lrc.h"

struct mem_reader_ctx {
    const uint8_t *buf;
    size_t len;
    size_t pos;
};

static int mem_read(void *ctx, void *dst, size_t len, size_t *out_got)
{
    struct mem_reader_ctx *m = ctx;
    if (m->pos >= m->len) {
        if (out_got)
            *out_got = 0U;
        return 0;
    }
    size_t avail = m->len - m->pos;
    size_t take = len < avail ? len : avail;
    memcpy(dst, m->buf + m->pos, take);
    m->pos += take;
    if (out_got)
        *out_got = take;
    return take > 0U;
}

static int mem_seek(void *ctx, uint64_t offset)
{
    struct mem_reader_ctx *m = ctx;
    if (offset > m->len)
        return 0;
    m->pos = (size_t)offset;
    return 1;
}

static size_t append_be32(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v >> 24);
    dst[1] = (uint8_t)(v >> 16);
    dst[2] = (uint8_t)(v >> 8);
    dst[3] = (uint8_t)v;
    return 4U;
}

static size_t make_mp4_box(uint8_t *dst, const char tag[4], const uint8_t *payload, size_t plen)
{
    append_be32(dst, (uint32_t)(8U + plen));
    memcpy(dst + 4, tag, 4);
    if (plen > 0U)
        memcpy(dst + 8, payload, plen);
    return 8U + plen;
}

static void test_parse_synced_and_sorted(void)
{
    static const char sample[] =
        "[ar:Coldplay]\r\n"
        "[ti:Fix You]\r\n"
        "[offset:-500]\r\n"
        "[00:20.00][01:05.500]Lights will guide you home\r\n"
        "[00:05.5]When you try your best\r\n"
        "[00:10.25]   But you don't succeed   \r\n";
    struct lrc_state st;

    uint32_t n = lrc_parse(&st, sample, (uint32_t)strlen(sample));
    assert(n == 4U);
    assert(st.has_synced_timestamps == 1);
    assert(st.lines[0].timestamp_ms == 5000U); /* 5500 - 500 */
    assert(strcmp(st.lines[0].text, "When you try your best") == 0);
    assert(st.lines[1].timestamp_ms == 9750U); /* 10250 - 500 */
    assert(strcmp(st.lines[1].text, "But you don't succeed") == 0);
    assert(st.lines[2].timestamp_ms == 19500U);
    assert(strcmp(st.lines[2].text, "Lights will guide you home") == 0);
    assert(st.lines[3].timestamp_ms == 65000U);
    assert(strcmp(st.lines[3].text, "Lights will guide you home") == 0);
}

static void test_parse_unsynced_and_offset_clamp(void)
{
    static const char unsynced[] = "Line one\n\nLine two\n";
    struct lrc_state st;

    uint32_t n = lrc_parse(&st, unsynced, (uint32_t)strlen(unsynced));
    assert(n == 2U);
    assert(st.has_synced_timestamps == 0);
    assert(st.lines[0].timestamp_ms == 0U);
    assert(strcmp(st.lines[0].text, "Line one") == 0);
    assert(strcmp(st.lines[1].text, "Line two") == 0);

    static const char neg_clamp[] = "[offset:-5000]\n[00:01.00]Early\n";
    n = lrc_parse(&st, neg_clamp, (uint32_t)strlen(neg_clamp));
    assert(n == 1U);
    assert(st.lines[0].timestamp_ms == 0U);
}

static void test_playback_sync_and_manual_hold(void)
{
    static const char sample[] = "[00:02.00]First\n[00:05.00]Second\n[00:10.00]Third\n";
    struct lrc_state st;

    assert(lrc_parse(&st, sample, (uint32_t)strlen(sample)) == 3U);
    assert(lrc_find_active_line(st.lines, st.line_count, 1000U) == LRC_LINE_NONE);
    assert(lrc_find_active_line(st.lines, st.line_count, 2000U) == 0U);
    assert(lrc_find_active_line(st.lines, st.line_count, 7500U) == 1U);
    assert(lrc_find_active_line(st.lines, st.line_count, 10000U) == 2U);

    assert(lrc_update_playback(&st, 2500U) == 1);
    assert(st.current_line == 0U);
    assert(st.target_scroll_offset_px == 0);

    st.manual_scroll_ticks = 10U;
    st.target_scroll_offset_px = 44;
    assert(lrc_update_playback(&st, 6000U) == 1);
    assert(st.current_line == 1U);
    assert(st.target_scroll_offset_px == 44); /* preserved while user scrolls */

    st.manual_scroll_ticks = 0U;
    assert(lrc_update_playback(&st, 11000U) == 1);
    assert(st.current_line == 2U);
    assert(st.target_scroll_offset_px == 2 * LRC_LINE_HEIGHT);
}

static void test_candidates_and_sanitize(void)
{
    char paths[4][160];
    size_t count = lrc_build_candidates("AC/DC", "Back:In*Black?", paths);

    assert(count == 4U);
    assert(strcmp(paths[0], "Lyrics\\AC_DC - Back_In_Black_.lrc") == 0);
    assert(strcmp(paths[1], "Lyrics\\Back_In_Black_.lrc") == 0);
    assert(strcmp(paths[2], "AC_DC - Back_In_Black_.lrc") == 0);
    assert(strcmp(paths[3], "Back_In_Black_.lrc") == 0);

    count = lrc_build_candidates("", "Fix You", paths);
    assert(count == 2U);
    assert(strcmp(paths[0], "Lyrics\\Fix You.lrc") == 0);
    assert(strcmp(paths[1], "Fix You.lrc") == 0);
}

static void test_word_wrap_and_utf8(void)
{
    static const char sample[] =
        "[00:01.00]Short line\n"
        "[00:04.00]When you try your best but you do not succeed at everything in this entire world today\n"
        "[00:08.00]Supercalifragilisticexpialidociousunbrokenlongwordexamplethatwraps\n"
        "[00:12.00]End\n";
    struct lrc_state st;

    assert(lrc_parse(&st, sample, (uint32_t)strlen(sample)) == 4U);
    assert(st.row_count > 4U);
    assert(st.line_row_count[0] == 1U);
    assert(st.line_row_count[1] >= 2U);
    assert(st.line_row_count[2] >= 2U);
    assert(st.line_row_count[3] == 1U);
    for (uint32_t i = 0U; i < st.line_row_count[1]; i++) {
        const struct lrc_row *r = &st.rows[st.line_first_row[1] + i];
        assert(r->line_idx == 1U);
        assert(r->len > 0U);
    }
    int32_t scroll_wrapped = lrc_scroll_for_line(&st, 1U);
    assert(scroll_wrapped >= (int32_t)(st.line_first_row[1] * LRC_LINE_HEIGHT));
    assert(lrc_update_playback(&st, 4500U) == 1);
    assert(st.current_line == 1U);
    assert(st.target_scroll_offset_px == scroll_wrapped);
}

static void test_embedded_mp4_and_id3(void)
{
    /* 1. MP4 'ftyp' + 'mdat' (skipped) + 'moov/udta/meta/ilst/©lyr/data' */
    uint8_t data_box[128];
    const char *mp4_txt = "[00:02.00]Embedded MP4 line one\n[00:05.00]Line two\n";
    uint8_t data_payload[120];
    append_be32(data_payload, 1U); /* UTF-8 text */
    append_be32(data_payload + 4, 0U);
    memcpy(data_payload + 8, mp4_txt, strlen(mp4_txt));
    size_t data_len = make_mp4_box(data_box, "data", data_payload, 8U + strlen(mp4_txt));

    uint8_t lyr_box[160];
    uint8_t lyr_tag[4] = {0xA9U, 'l', 'y', 'r'};
    size_t lyr_len = make_mp4_box(lyr_box, (const char *)lyr_tag, data_box, data_len);

    uint8_t ilst_box[192];
    size_t ilst_len = make_mp4_box(ilst_box, "ilst", lyr_box, lyr_len);

    uint8_t meta_payload[200];
    memset(meta_payload, 0, 4); /* FullBox flags */
    memcpy(meta_payload + 4, ilst_box, ilst_len);
    uint8_t meta_box[220];
    size_t meta_len = make_mp4_box(meta_box, "meta", meta_payload, 4U + ilst_len);

    uint8_t udta_box[240];
    size_t udta_len = make_mp4_box(udta_box, "udta", meta_box, meta_len);

    uint8_t moov_box[260];
    size_t moov_len = make_mp4_box(moov_box, "moov", udta_box, udta_len);

    uint8_t mp4[512];
    size_t pos = 0U;
    pos += make_mp4_box(mp4 + pos, "ftyp", (const uint8_t *)"M4A ", 4U);
    pos += make_mp4_box(mp4 + pos, "mdat", (const uint8_t *)"AUDIOBYTES", 10U);
    memcpy(mp4 + pos, moov_box, moov_len);
    pos += moov_len;

    struct lrc_state st;
    char scratch[LRC_FILE_MAX];
    struct mem_reader_ctx mctx = { .buf = mp4, .len = pos, .pos = 0U };
    struct lyrics_reader r = { .context = &mctx, .read = mem_read, .seek = mem_seek };
    assert(lyrics_extract_embedded(&r, &st, scratch, sizeof(scratch)) == 1);
    assert(st.line_count == 2U);
    assert(st.has_synced_timestamps == 1);
    assert(st.lines[0].timestamp_ms == 2000U);
    assert(strcmp(st.lines[0].text, "Embedded MP4 line one") == 0);

    /* 2. ID3v2.3 with USLT (UTF-16 LE BOM) AND SYLT (prefers SYLT) */
    uint8_t id3[512];
    memset(id3, 0, sizeof(id3));
    memcpy(id3, "ID3\x03\x00\x00", 6);
    size_t fpos = 10U;

    /* USLT frame */
    static const uint8_t uslt_body[] = {
        0x01, 'e', 'n', 'g', 0xFF, 0xFE, 0x00, 0x00, /* desc "" */
        0xFF, 0xFE, 'U', 0, 's', 0, 'l', 0, 't', 0
    };
    memcpy(id3 + fpos, "USLT", 4);
    append_be32(id3 + fpos + 4, sizeof(uslt_body));
    fpos += 10U;
    memcpy(id3 + fpos, uslt_body, sizeof(uslt_body));
    fpos += sizeof(uslt_body);

    /* SYLT frame (UTF-8, ts_format=2 ms) */
    uint8_t sylt_body[64];
    size_t sp = 0U;
    sylt_body[sp++] = 0x03U; /* UTF-8 */
    sylt_body[sp++] = 'e'; sylt_body[sp++] = 'n'; sylt_body[sp++] = 'g';
    sylt_body[sp++] = 2U; /* ms */
    sylt_body[sp++] = 1U; /* lyrics */
    sylt_body[sp++] = 0U; /* empty descriptor */
    const char *s1 = "First synced\n";
    memcpy(sylt_body + sp, s1, strlen(s1) + 1U);
    sp += strlen(s1) + 1U;
    sp += append_be32(sylt_body + sp, 3500U);
    const char *s2 = "Second synced";
    memcpy(sylt_body + sp, s2, strlen(s2) + 1U);
    sp += strlen(s2) + 1U;
    sp += append_be32(sylt_body + sp, 1200U);

    memcpy(id3 + fpos, "SYLT", 4);
    append_be32(id3 + fpos + 4, (uint32_t)sp);
    fpos += 10U;
    memcpy(id3 + fpos, sylt_body, sp);
    fpos += sp;

    uint32_t tsz = (uint32_t)(fpos - 10U);
    id3[6] = (uint8_t)((tsz >> 21) & 0x7FU);
    id3[7] = (uint8_t)((tsz >> 14) & 0x7FU);
    id3[8] = (uint8_t)((tsz >> 7) & 0x7FU);
    id3[9] = (uint8_t)(tsz & 0x7FU);

    mctx = (struct mem_reader_ctx){ .buf = id3, .len = fpos, .pos = 0U };
    assert(lyrics_extract_embedded(&r, &st, scratch, sizeof(scratch)) == 1);
    assert(st.line_count == 2U);
    assert(st.has_synced_timestamps == 1);
    assert(st.lines[0].timestamp_ms == 1200U);
    assert(strcmp(st.lines[0].text, "Second synced") == 0);
    assert(st.lines[1].timestamp_ms == 3500U);
    assert(strcmp(st.lines[1].text, "First synced") == 0);

    /* 3. ID3v2.3 with USLT UTF-16 LE only (ignore SYLT so USLT runs) */
    size_t s_off = 10U + 10U + sizeof(uslt_body);
    memcpy(id3 + s_off, "XXXX", 4);
    mctx = (struct mem_reader_ctx){ .buf = id3, .len = fpos, .pos = 0U };
    assert(lyrics_extract_embedded(&r, &st, scratch, sizeof(scratch)) == 1);
    assert(st.line_count == 1U);
    assert(strcmp(st.lines[0].text, "Uslt") == 0);
}

int main(void)
{
    test_parse_synced_and_sorted();
    test_parse_unsynced_and_offset_clamp();
    test_playback_sync_and_manual_hold();
    test_candidates_and_sanitize();
    test_word_wrap_and_utf8();
    test_embedded_mp4_and_id3();
    puts("lyrics lrc tests passed");
    return 0;
}

