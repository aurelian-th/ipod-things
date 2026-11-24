/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef DOODLE_H
#define DOODLE_H

#include "eapp_draw.h"
#include "eapp_libc.h"

#define DOODLE_SCR_W 320
#define DOODLE_SCR_H 240

/* Fixed-point world coordinates matching original Doodle Jump (20.11). */
#define FP_ONE (1 << 11)
#define WORLD_W (320 << 11) /* 655360 */
#define WORLD_H (480 << 11) /* 983040 */

/* Physical slots across g0..g3 and logical sprite table in i. */
#define MAX_PHYS_SLOTS 1073
#define MAX_LOGICAL_SPRITES 1241
#define MAX_STRINGS 72
#define MAX_SOUNDS 15
#define HIGH_SCORE_SLOTS 10

/* Sound index constants (indices passed into game_sound_play / stop). */
enum {
    SND_JUMP = 4,
    SND_BLACK_HOLE = 5,
    SND_BREAK = 6,
    SND_FALL = 7,
    SND_PROPELLER = 8,
    SND_STOMP = 9,
    SND_MONSTER_HIT = 10,
    SND_SHOOT_KILL = 11,
    SND_MONSTER_LOOP = 12,
    SND_JETPACK = 13,
    SND_SPRING = 16,
    SND_UFO_ABDUCT = 17,
    SND_UFO_LOOP = 19,
    SND_WHITE_VANISH = 20,
};

/* MIDP anchor flags */
enum {
    ANC_HCENTER = 1,
    ANC_VCENTER = 2,
    ANC_LEFT = 4,
    ANC_RIGHT = 8,
    ANC_TOP = 16,
    ANC_BOTTOM = 32,
    ANC_TOP_LEFT = ANC_TOP | ANC_LEFT,
    ANC_TOP_RIGHT = ANC_TOP | ANC_RIGHT,
    ANC_TOP_HCENTER = ANC_TOP | ANC_HCENTER,
    ANC_BOTTOM_LEFT = ANC_BOTTOM | ANC_LEFT,
    ANC_BOTTOM_RIGHT = ANC_BOTTOM | ANC_RIGHT,
    ANC_BOTTOM_HCENTER = ANC_BOTTOM | ANC_HCENTER,
    ANC_CENTER = ANC_HCENTER | ANC_VCENTER,
};

/* Transform codes in g0..g3 and draw_sprite() */
enum {
    XFORM_NONE = 0,
    XFORM_FLIP_X = 4,
    XFORM_FLIP_Y = 5,
    XFORM_ROT_180 = 2,
    XFORM_TRANSPOSE = 6,
};

struct doodle_sprite {
    uint16_t w;
    uint16_t h;
    uint8_t xform;     /* transform code to reach next variant, or 15 at end */
    uint8_t blend;     /* 0 = opaque, 1 = 0/255 mask, 2 = alpha blend */
    uint16_t *rgb;     /* w * h RGB565 pixels */
    uint8_t *alpha;    /* NULL when blend == 0, else w * h alpha bytes */
};

struct platform {
    int32_t x;         /* 20.11 fixed-point world X */
    int32_t y;         /* 20.11 fixed-point world Y */
    uint32_t flags;    /* platform kind & attachment bitmask */
    int32_t timer;     /* break/bomb animation timestamp */
    int32_t vx;        /* moving platform speed */
    int32_t item_dx;   /* X offset for attached spring/hat/jetpack */
};

struct hazard {
    int32_t type;      /* 4 = black hole, 6 = UFO, 7..11 = monsters, 20 = dead */
    int32_t hit_timer;
    int32_t x;
    int32_t y;
    int32_t vy_dead;   /* downward speed when stomped */
    int32_t phase_x;
    int32_t phase_y;
    int32_t off_x;
    int32_t off_y;
};

struct bullet {
    int32_t x;
    int32_t y;
    int32_t vx;
    int32_t vy;
};

int doodle_init(void);
void doodle_shutdown(void);
void doodle_on_button(uint8_t button, uint8_t type);
void doodle_on_wheel(const struct eapp_wheel *wheel);
void doodle_update(uint32_t now_ms);
void doodle_render(const struct eapp_framebuffer *fb);

#endif

