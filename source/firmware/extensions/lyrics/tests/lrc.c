/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lrc.h"

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

int main(void)
{
    test_parse_synced_and_sorted();
    test_parse_unsynced_and_offset_clamp();
    test_playback_sync_and_manual_hold();
    test_candidates_and_sanitize();
    puts("lyrics lrc tests passed");
    return 0;
}

