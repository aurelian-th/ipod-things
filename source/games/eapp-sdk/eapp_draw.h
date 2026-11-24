/* SPDX-License-Identifier: GPL-3.0-only */

/* Pixel work on the RGB565 framebuffer a frame callback is handed. Every call
 * clips to the framebuffer and takes coordinates outside it; a NULL
 * framebuffer or pixels draws nothing. */

#ifndef EAPP_DRAW_H
#define EAPP_DRAW_H

#include "eapp.h"

#ifdef __cplusplus
extern "C" {
#endif

enum eapp_align { EAPP_ALIGN_LEFT = -1, EAPP_ALIGN_CENTER, EAPP_ALIGN_RIGHT };

/* RGB565 pixels, rows `stride_pixels` apart; pixels equal to `transparent`
 * are skipped, -1 draws every pixel. */
struct eapp_bitmap {
    const uint16_t *pixels;
    uint32_t width, height, stride_pixels;
    int32_t transparent;
};

void eapp_clear(const struct eapp_framebuffer *fb, uint16_t color);
void eapp_fill_rect(const struct eapp_framebuffer *fb, int x, int y, int width, int height,
                    uint16_t color);
/* The 1 px outline just inside the rectangle. */
void eapp_draw_rect(const struct eapp_framebuffer *fb, int x, int y, int width, int height,
                    uint16_t color);
void eapp_draw_pixel(const struct eapp_framebuffer *fb, int x, int y, uint16_t color);
/* Both ends included. */
void eapp_draw_line(const struct eapp_framebuffer *fb, int x0, int y0, int x1, int y1,
                    uint16_t color);
void eapp_fill_triangle(const struct eapp_framebuffer *fb, int x0, int y0, int x1, int y1,
                        int x2, int y2, uint16_t color);
/* Blends `color` over the triangle with corners x[i], y[i] in 16.16 pixels;
 * alpha goes linearly from alpha[i] (0 to 255) at each corner. Pixels whose
 * centres lie inside are drawn, so neighbouring triangles never both draw a
 * shared edge. */
void eapp_blend_triangle(const struct eapp_framebuffer *fb, const int32_t x[3],
                         const int32_t y[3], const uint8_t alpha[3], uint16_t color);
void eapp_draw_circle(const struct eapp_framebuffer *fb, int cx, int cy, int radius,
                      uint16_t color);
void eapp_fill_circle(const struct eapp_framebuffer *fb, int cx, int cy, int radius,
                      uint16_t color);
/* The 1 px outline of a rectangle with quarter-circle corners of `radius`,
 * cut to less than half the shorter side. */
void eapp_draw_round_rect(const struct eapp_framebuffer *fb, int x, int y, int width,
                          int height, int radius, uint16_t color);
/* The same, filled. */
void eapp_fill_round_rect(const struct eapp_framebuffer *fb, int x, int y, int width,
                          int height, int radius, uint16_t color);

void eapp_draw_bitmap(const struct eapp_framebuffer *fb, int x, int y,
                      const struct eapp_bitmap *bitmap);
/* The `width` by `height` part of the bitmap from (source_x, source_y), cut
 * to the bitmap. */
void eapp_draw_bitmap_part(const struct eapp_framebuffer *fb, int x, int y,
                           const struct eapp_bitmap *bitmap, int source_x, int source_y,
                           int width, int height);
/* One bit per pixel, most significant bit first, each row padded to a byte;
 * set bits are drawn in `color`. */
void eapp_draw_mono_bitmap(const struct eapp_framebuffer *fb, int x, int y,
                           const uint8_t *bits, unsigned width, unsigned height,
                           uint16_t color);

/* Text in the SDK's 5 x 7 font (digits, upper-case letters, '-', '+' and ':';
 * lower-case letters are drawn in upper case, other characters leave their
 * cell empty), every font pixel `scale` pixels wide and high. A glyph cell is
 * 6 x 7 font pixels; the last glyph has no trailing gap. */
void eapp_draw_text(const struct eapp_framebuffer *fb, int x, int y, const char *text,
                    uint16_t color, unsigned scale);
/* The same text placed by `align` across the `width` pixels from `x`. */
void eapp_draw_text_in(const struct eapp_framebuffer *fb, int x, int y, unsigned width,
                       const char *text, uint16_t color, unsigned scale,
                       enum eapp_align align);
unsigned eapp_text_width(const char *text, unsigned scale);

#ifdef __cplusplus
}
#endif
#endif
