/* SPDX-License-Identifier: GPL-3.0-only */

/* The drawing calls on a 16 x 12 framebuffer with a 20 pixel stride, checked
 * pixel by pixel: clipping on every side, the shapes' extents and symmetry,
 * blended triangles' coverage and alpha, bitmap keys and cuts, mono bit
 * order, text metrics, and the random stream. */

#include "eapp_draw.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define W 16
#define H 12
#define STRIDE 20
#define INK 0xffff
#define BG 0x0000

static uint16_t pixels[H * STRIDE];
static const struct eapp_framebuffer fb = {pixels, W, H, STRIDE};

static void clear(void)
{
    memset(pixels, 0, sizeof pixels);
}

static uint16_t at(int x, int y)
{
    return pixels[y * STRIDE + x];
}

static unsigned count(void)
{
    unsigned n = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            n += at(x, y) == INK;
    return n;
}

/* Nothing lands in the stride padding. */
static void padding_clean(void)
{
    for (int y = 0; y < H; ++y)
        for (int x = W; x < STRIDE; ++x)
            assert(at(x, y) == BG);
}

static void rects(void)
{
    clear();
    eapp_fill_rect(&fb, -2, -2, 5, 5, INK);
    assert(count() == 9 && at(2, 2) == INK && at(3, 3) == BG);
    eapp_fill_rect(&fb, 14, 10, 10, 10, INK);
    assert(count() == 13 && at(15, 11) == INK);
    padding_clean();

    clear();
    eapp_draw_rect(&fb, 2, 1, 5, 4, INK);
    assert(count() == 14);
    assert(at(2, 1) == INK && at(6, 1) == INK && at(2, 4) == INK && at(6, 4) == INK);
    assert(at(3, 2) == BG && at(4, 2) == BG);
    eapp_draw_rect(&fb, 0, 0, 1, 1, INK);
    assert(count() == 15 && at(0, 0) == INK);
    eapp_draw_rect(&fb, 0, 0, 0, 5, INK);
    assert(count() == 15);
    eapp_draw_rect(&fb, -1, -1, W + 2, H + 2, INK);
    assert(count() == 15);

    clear();
    eapp_clear(&fb, INK);
    assert(count() == W * H);
    padding_clean();
    eapp_fill_rect(NULL, 0, 0, 1, 1, INK);
}

static void lines(void)
{
    clear();
    eapp_draw_pixel(&fb, 3, 4, INK);
    eapp_draw_pixel(&fb, -1, 4, INK);
    eapp_draw_pixel(&fb, 3, H, INK);
    assert(count() == 1 && at(3, 4) == INK);

    clear();
    eapp_draw_line(&fb, 1, 1, 6, 1, INK);
    assert(count() == 6 && at(1, 1) == INK && at(6, 1) == INK);
    clear();
    eapp_draw_line(&fb, 6, 8, 6, 3, INK);
    assert(count() == 6 && at(6, 3) == INK && at(6, 8) == INK);
    clear();
    eapp_draw_line(&fb, 0, 0, 5, 5, INK);
    for (int i = 0; i <= 5; ++i)
        assert(at(i, i) == INK);
    assert(count() == 6);
    clear();
    eapp_draw_line(&fb, 10, 2, 0, 7, INK);
    assert(at(10, 2) == INK && at(0, 7) == INK && count() == 11);
    clear();
    eapp_draw_line(&fb, -5, 5, 20, 5, INK);
    assert(count() == W);
    padding_clean();
}

static void triangles(void)
{
    clear();
    eapp_fill_triangle(&fb, 2, 1, 2, 7, 8, 7, INK);
    assert(at(2, 1) == INK && at(2, 7) == INK && at(8, 7) == INK);
    assert(at(3, 1) == BG && at(8, 6) == BG && at(4, 5) == INK);
    unsigned right = count();
    assert(right >= 25 && right <= 28);

    clear();
    eapp_fill_triangle(&fb, 8, 7, 2, 1, 2, 7, INK);
    assert(count() == right);

    clear();
    eapp_fill_triangle(&fb, 5, 1, 5, 1, 5, 1, INK);
    assert(count() == 1 && at(5, 1) == INK);
    clear();
    eapp_fill_triangle(&fb, 1, 3, 9, 3, 5, 3, INK);
    assert(count() == 9);

    clear();
    eapp_fill_triangle(&fb, -4, -4, 30, -4, -4, 30, INK);
    assert(at(0, 0) == INK && at(15, 0) == INK && at(0, 11) == INK && at(15, 11) == INK);
    assert(count() == W * H);
    padding_clean();
}

#define PX(v) ((int32_t)((v) * 65536))

static void blend(const int32_t x[3], const int32_t y[3], uint8_t a0, uint8_t a1, uint8_t a2)
{
    const uint8_t alpha[3] = {a0, a1, a2};
    eapp_blend_triangle(&fb, x, y, alpha, INK);
}

static void blends(void)
{
    /* Two triangles that make the square 0..8 cover its 64 pixels once:
     * opaque they fill it exactly, half way every pixel gets one blend. */
    const int32_t ux[3] = {PX(0), PX(8), PX(0)}, uy[3] = {PX(0), PX(0), PX(8)};
    const int32_t lx[3] = {PX(8), PX(8), PX(0)}, ly[3] = {PX(0), PX(8), PX(8)};
    clear();
    blend(ux, uy, 255, 255, 255);
    blend(lx, ly, 255, 255, 255);
    assert(count() == 64 && at(7, 7) == INK && at(8, 0) == BG && at(0, 8) == BG);
    clear();
    blend(ux, uy, 128, 128, 128);
    blend(lx, ly, 128, 128, 128);
    uint16_t half = at(0, 0);
    assert(half != BG && half != INK);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            assert(at(x, y) == half);
    assert(at(8, 3) == BG && at(3, 8) == BG);

    /* Transparent corners leave the pixels alone; the alpha falls along a
     * row away from the opaque corner; the corner order does not matter. */
    clear();
    blend(ux, uy, 0, 0, 0);
    assert(count() == 0 && at(2, 2) == BG);
    clear();
    blend(ux, uy, 255, 0, 0);
    for (int x = 1; x < 7; ++x)
        assert((at(x, 0) >> 11) <= (at(x - 1, 0) >> 11));
    assert(at(0, 0) >> 11 > at(6, 0) >> 11);
    uint16_t before[H * STRIDE];
    memcpy(before, pixels, sizeof pixels);
    clear();
    const int32_t rx[3] = {PX(0), PX(0), PX(8)}, ry[3] = {PX(8), PX(0), PX(0)};
    blend(rx, ry, 0, 255, 0);
    assert(memcmp(before, pixels, sizeof pixels) == 0);

    /* Half pixels: a corner past a centre takes that pixel in. */
    clear();
    const int32_t hx[3] = {PX(2), PX(4.5), PX(2)}, hy[3] = {PX(2), PX(2), PX(4.5)};
    blend(hx, hy, 255, 255, 255);
    assert(at(2, 2) == INK && at(3, 2) == INK && at(2, 3) == INK && at(4, 2) == BG);

    /* Nothing for a flat triangle; clipped on every side. */
    clear();
    const int32_t fx[3] = {PX(1), PX(9), PX(5)}, fy[3] = {PX(3), PX(3), PX(3)};
    blend(fx, fy, 255, 255, 255);
    assert(count() == 0);
    clear();
    const int32_t cx[3] = {PX(-40), PX(90), PX(-40)}, cy[3] = {PX(-40), PX(-40), PX(90)};
    blend(cx, cy, 255, 255, 255);
    assert(count() == W * H);
    padding_clean();
}

static void circles(void)
{
    clear();
    eapp_draw_circle(&fb, 7, 5, 4, INK);
    assert(at(3, 5) == INK && at(11, 5) == INK && at(7, 1) == INK && at(7, 9) == INK);
    assert(at(7, 5) == BG && at(2, 5) == BG);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (14 - x >= 0 && 14 - x < W && 10 - y >= 0 && 10 - y < H)
                assert(at(x, y) == at(14 - x, 10 - y));
    unsigned outline = count();
    uint16_t ring[H * STRIDE];
    memcpy(ring, pixels, sizeof ring);

    clear();
    eapp_fill_circle(&fb, 7, 5, 4, INK);
    assert(at(7, 5) == INK && at(3, 5) == INK && at(7, 1) == INK && at(2, 5) == BG);
    assert(at(3, 2) == BG && at(11, 8) == BG);
    unsigned filled = count();
    assert(filled > outline);
    /* The fill covers the outline and nothing outside its rows' extents. */
    for (int y = 0; y < H; ++y) {
        int first = -1, last = -1;
        for (int x = 0; x < W; ++x)
            if (ring[y * STRIDE + x] == INK) {
                if (first < 0)
                    first = x;
                last = x;
            }
        for (int x = 0; x < W; ++x)
            assert((at(x, y) == INK) == (first >= 0 && x >= first && x <= last));
    }
    for (int y = 0; y < H; ++y) {
        int first = -1, last = -1;
        for (int x = 0; x < W; ++x)
            if (at(x, y) == INK) {
                if (first < 0)
                    first = x;
                last = x;
            }
        for (int x = first; first >= 0 && x <= last; ++x)
            assert(at(x, y) == INK);
    }

    clear();
    eapp_draw_circle(&fb, 7, 5, 1, INK);
    assert(count() == 4 && at(7, 5) == BG);
    clear();
    eapp_fill_circle(&fb, 7, 5, 1, INK);
    assert(count() == 5 && at(7, 5) == INK && at(6, 4) == BG);
    clear();
    eapp_fill_circle(&fb, 7, 5, 0, INK);
    assert(count() == 1);
    clear();
    eapp_fill_circle(&fb, 0, 0, 30, INK);
    assert(count() == W * H);
    padding_clean();
}

static void round_rects(void)
{
    clear();
    eapp_fill_round_rect(&fb, 1, 1, 12, 9, 3, INK);
    /* The corners are cut, the edges and the middle filled. */
    assert(at(1, 1) == BG && at(12, 1) == BG && at(1, 9) == BG && at(12, 9) == BG);
    assert(at(4, 1) == INK && at(1, 4) == INK && at(6, 5) == INK && at(12, 4) == INK);
    assert(at(9, 9) == INK && at(0, 5) == BG && at(13, 5) == BG && at(6, 0) == BG);
    assert(at(6, 10) == BG);
    for (int y = 1; y <= 9; ++y)
        for (int x = 1; x <= 12; ++x)
            assert(at(x, y) == at(13 - x, y) && at(x, y) == at(x, 10 - y));
    unsigned filled = count();
    assert(filled < 12 * 9);
    uint16_t fill[H * STRIDE];
    memcpy(fill, pixels, sizeof fill);

    /* The outline lies on the fill and is thinner. */
    clear();
    eapp_draw_round_rect(&fb, 1, 1, 12, 9, 3, INK);
    assert(at(1, 1) == BG && at(4, 1) == INK && at(1, 4) == INK && at(6, 5) == BG);
    assert(at(12, 4) == INK && at(6, 9) == INK);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            assert(at(x, y) == BG || fill[y * STRIDE + x] == INK);
    assert(count() < filled);

    /* No radius is a plain rectangle; a radius of half the shorter side or
     * more is cut below it; clipping covers the framebuffer. */
    clear();
    eapp_fill_round_rect(&fb, 2, 2, 6, 4, 0, INK);
    assert(count() == 24 && at(2, 2) == INK);
    clear();
    eapp_fill_round_rect(&fb, 2, 2, 6, 4, 10, INK);
    assert(count() < 24 && at(2, 2) == BG && at(4, 3) == INK);
    unsigned cut = count();
    clear();
    eapp_fill_round_rect(&fb, 2, 2, 6, 4, 1, INK);
    assert(count() == cut);
    clear();
    eapp_fill_round_rect(&fb, -4, -4, 40, 30, 6, INK);
    assert(count() == W * H);
    padding_clean();
}

static void bitmaps(void)
{
    static const uint16_t art[3 * 5] = {
        1, 2, 3, 4, 9, /* a 4-wide picture with a 5 pixel stride */
        5, 0, 6, 7, 9, /* 0 is the key */
        8, 9, 0, 1, 9,
    };
    struct eapp_bitmap bitmap = {art, 4, 3, 5, -1};

    clear();
    eapp_draw_bitmap(&fb, 1, 1, &bitmap);
    assert(at(1, 1) == 1 && at(4, 1) == 4 && at(5, 1) == BG);
    assert(at(2, 2) == 0 && at(3, 3) == 0 && at(4, 3) == 1);

    clear();
    eapp_fill_rect(&fb, 0, 0, W, H, 7);
    bitmap.transparent = 0;
    eapp_draw_bitmap(&fb, 1, 1, &bitmap);
    assert(at(2, 2) == 7 && at(3, 3) == 7 && at(1, 1) == 1 && at(4, 3) == 1);

    clear();
    bitmap.transparent = -1;
    eapp_draw_bitmap_part(&fb, 0, 0, &bitmap, 1, 1, 2, 2);
    assert(at(0, 0) == 0 && at(1, 0) == 6 && at(0, 1) == 9 && at(1, 1) == 0);
    assert(count() == 0 && at(2, 0) == BG && at(0, 2) == BG);

    clear();
    eapp_draw_bitmap_part(&fb, 3, 3, &bitmap, -1, -1, 10, 10);
    assert(at(3, 3) == BG && at(4, 4) == 1 && at(7, 4) == 4 && at(8, 4) == BG && at(7, 6) == 1);

    clear();
    eapp_draw_bitmap(&fb, -2, -1, &bitmap);
    assert(at(0, 0) == 6 && at(1, 0) == 7 && at(0, 1) == 0 && at(1, 1) == 1 && at(2, 1) == BG);

    clear();
    eapp_draw_bitmap(&fb, 14, 10, &bitmap);
    assert(at(14, 10) == 1 && at(15, 10) == 2 && at(14, 11) == 5 && at(15, 11) == 0);
    padding_clean();

    eapp_draw_bitmap_part(&fb, 0, 0, &bitmap, 4, 0, 2, 2);
    eapp_draw_bitmap_part(&fb, 0, 0, &bitmap, 0, 3, 2, 2);
    eapp_draw_bitmap(&fb, 0, 0, NULL);

    static const uint8_t mono[] = {
        0xa5, 0x80, /* 1010 0101 1 */
        0x00, 0x00,
        0xff, 0x80,
    };
    clear();
    eapp_draw_mono_bitmap(&fb, 2, 2, mono, 9, 3, INK);
    assert(at(2, 2) == INK && at(3, 2) == BG && at(4, 2) == INK && at(5, 2) == BG);
    assert(at(6, 2) == BG && at(7, 2) == INK && at(9, 2) == INK && at(10, 2) == INK);
    assert(at(11, 2) == BG && at(2, 3) == BG);
    assert(count() == 5 + 9);
    clear();
    eapp_draw_mono_bitmap(&fb, -1, -1, mono, 9, 3, INK);
    assert(at(0, 1) == INK && at(7, 1) == INK && at(8, 1) == BG && count() == 8);
    padding_clean();
}

static void text(void)
{
    assert(eapp_text_width("", 1) == 0);
    assert(eapp_text_width(NULL, 2) == 0);
    assert(eapp_text_width("A", 1) == 5);
    assert(eapp_text_width("AB", 1) == 11);
    assert(eapp_text_width("AB", 3) == 33);
    assert(eapp_text_width("A B", 1) == 17);

    clear();
    eapp_draw_text(&fb, 1, 1, "-", INK, 1);
    assert(count() == 5 && at(1, 4) == INK && at(5, 4) == INK && at(1, 3) == BG);
    clear();
    eapp_draw_text(&fb, 1, 1, "--", INK, 1);
    assert(count() == 10 && at(6, 4) == BG && at(7, 4) == INK && at(11, 4) == INK);
    clear();
    eapp_draw_text(&fb, 1, 1, "- -", INK, 1);
    assert(count() == 8 && at(13, 4) == INK && at(15, 4) == INK);
    clear();
    eapp_draw_text(&fb, 1, 1, "-", INK, 2);
    assert(count() == 20 && at(1, 7) == INK && at(10, 8) == INK && at(1, 6) == BG);
    clear();
    eapp_draw_text(&fb, 1, 1, "-", INK, 0);
    assert(count() == 0);

    clear();
    eapp_draw_text(&fb, -2, 1, "-", INK, 1);
    assert(count() == 3 && at(0, 4) == INK && at(2, 4) == INK);
    clear();
    eapp_draw_text(&fb, 13, 1, "-", INK, 1);
    assert(count() == 3 && at(13, 4) == INK && at(15, 4) == INK);
    clear();
    eapp_draw_text(&fb, 1, -3, ":", INK, 1);
    assert(count() == 2 && at(3, 1) == INK && at(3, 2) == INK);
    clear();
    eapp_draw_text(&fb, 1, 9, ":", INK, 1);
    assert(count() == 2 && at(3, 10) == INK && at(3, 11) == INK);
    clear();
    eapp_draw_text(&fb, 1, 1, "x", INK, 1);
    uint16_t lower[H * STRIDE];
    memcpy(lower, pixels, sizeof lower);
    clear();
    eapp_draw_text(&fb, 1, 1, "X", INK, 1);
    assert(!memcmp(lower, pixels, sizeof lower) && count() == 13);
    padding_clean();

    clear();
    eapp_draw_text_in(&fb, 0, 1, W, "-", INK, 1, EAPP_ALIGN_LEFT);
    assert(at(0, 4) == INK && at(4, 4) == INK && count() == 5);
    clear();
    eapp_draw_text_in(&fb, 0, 1, W, "-", INK, 1, EAPP_ALIGN_RIGHT);
    assert(at(11, 4) == INK && at(15, 4) == INK && count() == 5);
    clear();
    eapp_draw_text_in(&fb, 2, 1, 10, "-", INK, 1, EAPP_ALIGN_CENTER);
    assert(at(4, 4) == INK && at(8, 4) == INK && count() == 5);
    clear();
    eapp_draw_text_in(&fb, 2, 1, 3, "-", INK, 1, EAPP_ALIGN_RIGHT);
    assert(at(0, 4) == INK && at(4, 4) == INK && count() == 5);
}

static void random_stream(void)
{
    uint32_t a = eapp_random(), b = eapp_random();
    assert(a && b && a != b);
    eapp_random_seed(12345);
    uint32_t first[4];
    for (unsigned i = 0; i < 4; ++i)
        first[i] = eapp_random();
    eapp_random_seed(12345);
    for (unsigned i = 0; i < 4; ++i)
        assert(eapp_random() == first[i]);
    assert(first[0] != first[1] && first[1] != first[2]);
    eapp_random_seed(0);
    uint32_t zero = eapp_random();
    assert(zero);
    eapp_random_seed(1);
    assert(eapp_random() != zero);
    eapp_random_seed(0);
    assert(eapp_random() == zero);
}

int main(void)
{
    rects();
    lines();
    triangles();
    blends();
    circles();
    round_rects();
    bitmaps();
    text();
    random_stream();
    puts("draw_test passed");
    return 0;
}
