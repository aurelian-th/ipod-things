/* SPDX-License-Identifier: GPL-3.0-only */
#include "doodle.h"

/* Screen states */
enum {
    SCR_SPLASH = 0,
    SCR_MAIN_MENU,
    SCR_TUTORIAL,
    SCR_PLAYING,
    SCR_PAUSE,
    SCR_GAME_OVER,
    SCR_HIGH_SCORES,
    SCR_OPTIONS,
    SCR_HELP,
    SCR_ABOUT,
    SCR_CONFIRM_RESET,
    SCR_CONFIRM_EXIT,
};

/* Physics constants in 20.11 fixed-point world units (exact match to f.java). */
enum {
    PLAT_WORLD_W = 116736, /* ca */
    PLAT_WORLD_H = 30720,  /* cb */
    ACCEL_X = 560000,      /* tuned for crisp iPod button control */
    DECEL_X = 1638400,     /* faster stop to eliminate overshoot */
    GRAVITY = 0x1B0000,    /* bi = 1769472 */
    MAX_VX = 640000,       /* controlled horizontal top speed */
    MIN_VY = -1105920,     /* bk */
    JUMP_VY = 0x10E000,    /* bl = 1105920 */
    LEGS_VY = 983040,      /* bm */
    CAM_THRESH = 471040,   /* bq */
    VANISH_SPEED = 614400, /* bv */
    SPRING_VY = 1843200,   /* bw */
    JET_RISE_TIME = 2730,  /* bn */
    JET_CRUISE_TIME = 3072,/* bo */
    JET_FALL_TIME = 1024,  /* bp */
};

/* Doodler powerup / status bitmask (bL) */
enum {
    ST_PROP_RISE = 0x00001,
    ST_PROP_CRUISE = 0x00002,
    ST_PROP_END = 0x00004,
    ST_PROP_DROP = 0x00008,
    ST_JET_RISE = 0x00010,
    ST_JET_CRUISE = 0x00020,
    ST_JET_END = 0x00040,
    ST_JET_DROP = 0x00080,
    ST_UFO_BEAM = 0x00100,
    ST_UFO_DONE = 0x00200,
    ST_FALL_OUT = 0x00400,
    ST_STUNNED = 0x00800,
    ST_SHIELD = 0x01000,
    ST_SHIELD_BLINK = 0x02000,
    ST_BH_SUCK = 0x04000,
    ST_BH_DONE = 0x08000,
    ST_DEAD_WAIT = 0x10000,
};

#define ST_PROP_MASK (ST_PROP_RISE | ST_PROP_CRUISE | ST_PROP_END)
#define ST_JET_MASK (ST_JET_RISE | ST_JET_CRUISE | ST_JET_END)
#define ST_NO_SHOOT_MASK (ST_PROP_MASK | ST_JET_MASK | ST_UFO_BEAM | ST_UFO_DONE | ST_FALL_OUT | ST_BH_SUCK | ST_DEAD_WAIT)
#define ST_LOCK_MOVE_MASK (ST_UFO_BEAM | ST_STUNNED | ST_BH_SUCK)
#define ST_FREEZE_INPUT_MASK (ST_FALL_OUT | ST_STUNNED | ST_DEAD_WAIT)

/* Menu items */
enum {
    MI_CONTINUE = 0,
    MI_PLAY,
    MI_GAME_MODE,
    MI_HIGH_SCORES,
    MI_OPTIONS,
    MI_HELP,
    MI_ABOUT,
    MI_EXIT,
    MI_PAUSE_RESUME,
    MI_PAUSE_SOUND,
    MI_PAUSE_OPTIONS,
    MI_PAUSE_MAIN_MENU,
    MI_OPT_SOUND,
    MI_OPT_MODE,
    MI_OPT_TUTORIAL,
    MI_OPT_RESET,
    MI_OPT_BACK,
    MI_OVER_AGAIN,
    MI_OVER_MENU,
    MI_CONFIRM_YES,
    MI_CONFIRM_NO,
};

#define SAVE_MAGIC 0x444A5332u /* "DJS2" */

struct save_data {
    uint32_t magic;
    uint8_t sound_on;
    uint8_t full_mode;
    uint8_t tutorial_on;
    uint8_t reserved;
    int32_t scores[2][HIGH_SCORE_SLOTS]; /* [0]=classic, [1]=full */
};

/* Binary index tables from "i" */
static int8_t *d_arr;
static int16_t *a_arr;
static int32_t *e_arr;

/* Palettes from "p" */
static uint16_t pals[8][256];

/* Sprites from "g0..g3" */
static struct doodle_sprite slots[MAX_PHYS_SLOTS];

/* Localization strings from "l" */
static char *strings[MAX_STRINGS];
static char *str_blob;
static char about_text[640];

/* Pattern tables from "b1" */
static uint8_t monster_anim[9];
static int16_t pat_low_start[8];
static int16_t pat_high_start[23];
static int16_t *pat_data;

/* Sounds from s00.wav..s14.wav */
static struct eapp_sound *sounds[MAX_SOUNDS];

/* Sine lookup table [-16384..+16384] */
static int16_t sin_lut[360];

/* Clipping rect for custom renderer */
static int clip_x, clip_y, clip_w, clip_h;

/* Scaling cache from af() */
static int32_t scale_x, scale_y; /* bt, bu */
static int32_t plat_px_w;        /* bZ */
static int32_t doodler_w, doodler_h;
static int32_t min_step_px;      /* cc */

/* Persistent settings & high scores */
static struct save_data save;
static int new_best_flag;

/* Game world state */
static struct platform plats[100];
static int plat_head, plat_tail; /* bX, bY */
static struct hazard hazards[10];
static int haz_head, haz_tail;   /* by, bz */
static int abduct_haz_idx;       /* bA */
static int last_hit_haz;         /* bB */
static struct bullet bullets[10];
static int bullet_count;         /* ch */
static int bullet_hit_idx;       /* ci */

static int32_t vx, vy, ax, ay, player_x, player_y; /* bD..bI */
static int facing_left, ufo_facing_left;           /* var_boolean_Q, R */
static uint32_t player_st;                         /* bL */
static int32_t state_timer, shield_timer;          /* bM, bT */
static int32_t deb_x, deb_y, deb_vx, deb_vy, deb_ax, deb_ay; /* bN..bS */
static int32_t next_hat_y, next_jet_y, jet_spawned;          /* bU..bW */

static uint32_t pat_low_mask, pat_high_mask;       /* cd, ce */
static int32_t cam_y, score, paper_scroll;         /* cu, cv, cw */
static int32_t phys_accum;                         /* ct */
static int32_t menu_doodler_accum;
static int32_t menu_doodler_y, menu_doodler_vy;
static int32_t menu_ufo_x, menu_ufo_y, menu_ufo_off_x, menu_ufo_off_y;
static int32_t menu_ufo_phase_x, menu_ufo_phase_y;
static int monster_snd_active, ufo_snd_active;     /* var_boolean_ab, ac */
static int game_paused_midrun;                     /* can show Continue */
static int bg_scroll_stopped;                      /* var_boolean_Y */

/* UI / screen navigation state */
static int screen;
static int prev_screen_before_options;
static int menu_items[10];
static int menu_count;
static int menu_sel;
static int menu_scroll_top;
static int hs_view_mode; /* 0=classic, 1=full */
static int text_scroll_y, text_max_scroll;
static int tutorial_step;

/* Timing & input state */
static uint32_t start_ms, game_time_ms, prev_update_ms, delta_ms;
static uint32_t splash_start_ms;
static uint8_t btn_prev_down, btn_next_down;
static uint32_t btn_prev_until_ms, btn_next_until_ms;
static int wheel_menu_accum;
static int ufo_hover_t;

/* ---------- Helpers ---------- */

static uint16_t make_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (uint16_t)(b >> 3));
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int32_t rand_pos(void)
{
    return (int32_t)(eapp_random() & 0x7FFFFFFFu);
}

static int32_t fp_mul(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * (int64_t)b) >> 11);
}

static int32_t fp_div(int32_t a, int32_t b)
{
    if (b == 0)
        return 0;
    return (int32_t)(((int64_t)a << 11) / b);
}

static int to_sx(int32_t x)
{
    return fp_mul(x, scale_x) >> 11;
}

static int to_sy(int32_t y)
{
    return DOODLE_SCR_H - (fp_mul(y, scale_y) >> 11);
}

static uint8_t *load_pkg_file(const char *name, uint32_t *out_size)
{
    struct eapp_file *f = eapp_file_open(name);
    if (!f)
        return NULL;
    uint32_t sz = eapp_file_size(f);
    uint8_t *buf = eapp_alloc(sz ? sz : 1);
    if (!buf) {
        eapp_file_close(f);
        return NULL;
    }
    if (sz > 0 && eapp_file_read(f, buf, sz) != (int)sz) {
        eapp_free(buf);
        eapp_file_close(f);
        return NULL;
    }
    eapp_file_close(f);
    if (out_size)
        *out_size = sz;
    return buf;
}

static size_t str_len(const char *s)
{
    size_t n = 0;
    while (s && s[n])
        ++n;
    return n;
}

static char *fmt_uint(char *out, int32_t val)
{
    char tmp[16];
    int i = 0;
    if (val < 0) {
        *out++ = '-';
        val = -val;
    }
    uint32_t u = (uint32_t)val;
    do {
        tmp[i++] = (char)('0' + (u % 10u));
        u /= 10u;
    } while (u);
    while (i > 0)
        *out++ = tmp[--i];
    *out = 0;
    return out;
}

static void fmt_comma_int(char *out, int32_t val)
{
    char raw[16];
    fmt_uint(raw, val);
    size_t len = str_len(raw);
    if (len <= 3 || raw[0] == '-') {
        memcpy(out, raw, len + 1);
        return;
    }
    size_t lead = len % 3;
    if (lead == 0)
        lead = 3;
    size_t pos = 0;
    size_t out_i = 0;
    for (; pos < lead; ++pos)
        out[out_i++] = raw[pos];
    while (pos < len) {
        out[out_i++] = ',';
        out[out_i++] = raw[pos++];
        out[out_i++] = raw[pos++];
        out[out_i++] = raw[pos++];
    }
    out[out_i] = 0;
}

static uint16_t utf8_next(const char **p)
{
    const uint8_t *s = (const uint8_t *)(*p);
    uint8_t c = s[0];
    if (c < 0x80u) {
        *p = (const char *)(s + 1);
        return c;
    }
    if ((c & 0xE0u) == 0xC0u && (s[1] & 0xC0u) == 0x80u) {
        *p = (const char *)(s + 2);
        return (uint16_t)(((c & 0x1Fu) << 6) | (s[1] & 0x3Fu));
    }
    if ((c & 0xF0u) == 0xE0u && (s[1] & 0xC0u) == 0x80u && (s[2] & 0xC0u) == 0x80u) {
        *p = (const char *)(s + 3);
        return (uint16_t)(((c & 0x0Fu) << 12) | ((s[1] & 0x3Fu) << 6) | (s[2] & 0x3Fu));
    }
    *p = (const char *)(s + 1);
    return c;
}

/* ---------- Sounds ---------- */

static void play_sfx(int idx, int loop)
{
    if (!save.sound_on || idx < 0 || idx >= 21)
        return;
    if (d_arr[3875 + idx] == 0)
        return;
    int slot = a_arr[2407 + idx];
    if (slot < 0 || slot >= MAX_SOUNDS || !sounds[slot])
        return;
    if (loop == 0) {
        if (eapp_sound_playing(sounds[slot]))
            return;
        eapp_sound_set_play_count(sounds[slot], 0);
    } else {
        eapp_sound_set_play_count(sounds[slot], 1);
    }
    eapp_sound_play(sounds[slot]);
}

static void stop_sfx(int idx)
{
    if (idx < 0 || idx >= 21 || d_arr[3875 + idx] == 0)
        return;
    int slot = a_arr[2407 + idx];
    if (slot >= 0 && slot < MAX_SOUNDS && sounds[slot])
        eapp_sound_stop(sounds[slot]);
}

static void stop_all_sfx(void)
{
    for (int i = 0; i < MAX_SOUNDS; ++i) {
        if (sounds[i])
            eapp_sound_stop(sounds[i]);
    }
    monster_snd_active = 0;
    ufo_snd_active = 0;
}

static void clear_looping_sfx(void)
{
    if (ufo_snd_active) {
        stop_sfx(SND_UFO_LOOP);
        ufo_snd_active = 0;
    }
    if (monster_snd_active) {
        stop_sfx(SND_MONSTER_LOOP);
        monster_snd_active = 0;
    }
}

/* ---------- Asset Initialization ---------- */

static void xform_flip(const uint16_t *srgb, const uint8_t *sa, uint16_t *drgb, uint8_t *da,
                     int w, int h, int fx, int fy)
{
    for (int y = 0; y < h; ++y) {
        int dy = fy ? (h - 1 - y) : y;
        for (int x = 0; x < w; ++x) {
            int dx = fx ? (w - 1 - x) : x;
            int si = y * w + x;
            int di = dy * w + dx;
            drgb[di] = srgb[si];
            if (da && sa)
                da[di] = sa[si];
        }
    }
}

static void xform_transpose(const uint16_t *srgb, const uint8_t *sa, uint16_t *drgb, uint8_t *da,
                            int w, int h, int bx, int by)
{
    /* Mirrors f.b(bx, by): destination dimensions are nw = h, nh = w. */
    int nw = h;
    for (int y = 0; y < h; ++y) {
        int dy_x = bx ? (h - 1 - y) : y;
        for (int x = 0; x < w; ++x) {
            int dx_y = by ? (w - 1 - x) : x;
            int si = y * w + x;
            int di = dx_y * nw + dy_x;
            drgb[di] = srgb[si];
            if (da && sa)
                da[di] = sa[si];
        }
    }
}

struct pack_stream {
    struct eapp_file *f;
    uint32_t file_pos;
    uint32_t file_sz;
    uint32_t buf_pos;
    uint32_t buf_len;
    int err;
};

static uint8_t pack_iobuf[16384];

static uint8_t ps_u8(struct pack_stream *ps)
{
    if (ps->err)
        return 0;
    if (ps->buf_pos >= ps->buf_len) {
        uint32_t rem = ps->file_sz - ps->file_pos;
        if (rem == 0) {
            ps->err = 1;
            return 0;
        }
        uint32_t chunk = rem > sizeof(pack_iobuf) ? (uint32_t)sizeof(pack_iobuf) : rem;
        int rd = eapp_file_read(ps->f, pack_iobuf, chunk);
        if (rd <= 0) {
            ps->err = 1;
            return 0;
        }
        ps->file_pos += (uint32_t)rd;
        ps->buf_pos = 0;
        ps->buf_len = (uint32_t)rd;
    }
    return pack_iobuf[ps->buf_pos++];
}

static uint16_t ps_be16(struct pack_stream *ps)
{
    uint16_t hi = ps_u8(ps);
    uint16_t lo = ps_u8(ps);
    return (uint16_t)((hi << 8) | lo);
}

static uint32_t ps_rem(const struct pack_stream *ps)
{
    return (ps->file_sz - ps->file_pos) + (ps->buf_len - ps->buf_pos);
}

static int load_sprites_pack(int gi)
{
    char name[4] = {'g', (char)('0' + gi), 0, 0};
    struct eapp_file *f = eapp_file_open(name);
    if (!f)
        return -1;

    struct pack_stream ps = {
        .f = f,
        .file_sz = eapp_file_size(f),
    };

    int slot = a_arr[1351 + gi];
    int end_slot = a_arr[1351 + gi + 1];

    while (slot < end_slot && ps_rem(&ps) >= 5 && !ps.err) {
        uint16_t w = ps_be16(&ps);
        uint16_t h = ps_be16(&ps);
        uint8_t fmt = ps_u8(&ps);
        if (ps.err) {
            eapp_file_close(f);
            return -1;
        }

        uint32_t npix = (uint32_t)w * (uint32_t)h;
        uint32_t pix_bytes = (fmt == 255) ? (npix * 4u) : npix;
        if (ps_rem(&ps) <= pix_bytes) {
            eapp_file_close(f);
            return -1;
        }

        uint16_t *rgb = eapp_alloc(npix ? npix * 2u : 2u);
        uint8_t *alpha = eapp_alloc(npix ? npix : 1u);
        if (!rgb || !alpha) {
            eapp_free(rgb);
            eapp_free(alpha);
            eapp_file_close(f);
            return -1;
        }

        uint8_t blend = 0;
        if (fmt == 255) {
            for (uint32_t k = 0; k < npix; ++k) {
                uint8_t b = ps_u8(&ps);
                uint8_t g = ps_u8(&ps);
                uint8_t r = ps_u8(&ps);
                uint8_t a = ps_u8(&ps);
                rgb[k] = make_rgb565(r, g, b);
                alpha[k] = a;
                if (a == 0 && blend == 0)
                    blend = 1;
                else if (a != 0 && a != 255)
                    blend = 2;
            }
        } else {
            for (uint32_t k = 0; k < npix; ++k) {
                uint8_t idx = ps_u8(&ps);
                if (idx == 0) {
                    rgb[k] = 0;
                    alpha[k] = 0;
                    if (blend == 0)
                        blend = 1;
                } else {
                    rgb[k] = pals[fmt][idx];
                    alpha[k] = 255;
                }
            }
        }

        if (blend == 0) {
            eapp_free(alpha);
            alpha = NULL;
        }

        slots[slot].w = w;
        slots[slot].h = h;
        slots[slot].xform = 0;
        slots[slot].blend = blend;
        slots[slot].rgb = rgb;
        slots[slot].alpha = alpha;

        uint8_t ntrans = ps_u8(&ps) & 0x0Fu;
        if (ps.err) {
            eapp_file_close(f);
            return -1;
        }
        if (ntrans > 0) {
            for (uint8_t k = 0; k < ntrans && ps_rem(&ps) > 0 && slot + 1 < end_slot; ++k) {
                uint8_t tc = ps_u8(&ps);
                uint16_t *trgb = eapp_alloc(npix ? npix * 2u : 2u);
                uint8_t *ta = alpha ? eapp_alloc(npix ? npix : 1u) : NULL;
                if (!trgb || (alpha && !ta)) {
                    eapp_free(trgb);
                    eapp_free(ta);
                    eapp_file_close(f);
                    return -1;
                }
                int swapped = 0;
                switch (tc) {
                case 4:
                    xform_flip(rgb, alpha, trgb, ta, w, h, 1, 0);
                    break;
                case 5:
                    xform_flip(rgb, alpha, trgb, ta, w, h, 0, 1);
                    break;
                case 2:
                    xform_flip(rgb, alpha, trgb, ta, w, h, 1, 1);
                    break;
                case 1:
                    xform_transpose(rgb, alpha, trgb, ta, w, h, 1, 0);
                    swapped = 1;
                    break;
                case 3:
                    xform_transpose(rgb, alpha, trgb, ta, w, h, 0, 1);
                    swapped = 1;
                    break;
                case 6:
                    xform_transpose(rgb, alpha, trgb, ta, w, h, 0, 0);
                    swapped = 1;
                    break;
                case 7:
                    xform_transpose(rgb, alpha, trgb, ta, w, h, 1, 1);
                    swapped = 1;
                    break;
                default:
                    memcpy(trgb, rgb, npix * 2u);
                    if (ta && alpha)
                        memcpy(ta, alpha, npix);
                    break;
                }
                slots[slot].xform = tc;
                ++slot;
                slots[slot].w = swapped ? h : w;
                slots[slot].h = swapped ? w : h;
                slots[slot].xform = 0;
                slots[slot].blend = blend;
                slots[slot].rgb = trgb;
                slots[slot].alpha = ta;
            }
            slots[slot].xform = 15;
        }
        ++slot;
    }

    eapp_file_close(f);
    return ps.err ? -1 : 0;
}

static void init_sin_lut(void)
{
    int32_t tmp[360];
    int64_t l = 0x40000000LL;
    int64_t l2 = 1073578287LL;
    tmp[0] = (int32_t)(l >> 14);
    tmp[1] = (int32_t)(l2 >> 14);
    for (int n = 2; n < 90; ++n) {
        int64_t l3 = ((l2 * 2147156574LL) >> 30) - l;
        tmp[n] = (int32_t)(l3 >> 14);
        l = l2;
        l2 = l3;
    }
    tmp[90] = 0;
    for (int n = 89; n > 0; --n)
        tmp[n + 90] = -tmp[90 - n];
    tmp[180] = -65536;
    for (int n = 179; n > 0; --n)
        tmp[n + 180] = tmp[180 - n];
    for (int n = 0; n < 360; ++n)
        sin_lut[n] = (int16_t)(tmp[(n + 270) % 360] >> 2);
}

static void reset_high_scores_mem(void)
{
    for (int m = 0; m < 2; ++m) {
        for (int k = 0; k < HIGH_SCORE_SLOTS; ++k)
            save.scores[m][k] = -1;
    }
}

static void save_persist(void)
{
    save.magic = SAVE_MAGIC;
    eapp_data_write("save.bin", &save, sizeof(save));
}

static void load_persist(void)
{
    memset(&save, 0, sizeof(save));
    save.magic = SAVE_MAGIC;
    save.sound_on = 1;
    save.full_mode = 1;
    save.tutorial_on = 0;
    reset_high_scores_mem();

    struct save_data tmp;
    int got = eapp_data_read("save.bin", &tmp, sizeof(tmp));
    if (got == (int)sizeof(tmp) && tmp.magic == SAVE_MAGIC)
        save = tmp;
}

static void insert_high_score(int mode, int32_t sc)
{
    new_best_flag = 0;
    if (sc <= 0)
        return;
    int32_t *arr = save.scores[mode ? 1 : 0];
    for (int i = 0; i < HIGH_SCORE_SLOTS; ++i) {
        if (sc > arr[i]) {
            for (int j = HIGH_SCORE_SLOTS - 1; j > i; --j)
                arr[j] = arr[j - 1];
            arr[i] = sc;
            new_best_flag = (i == 0) ? 2 : 1;
            save_persist();
            return;
        }
    }
}

/* ---------- Renderer & Fonts ---------- */

static void set_clip(int x, int y, int w, int h)
{
    clip_x = x;
    clip_y = y;
    clip_w = w;
    clip_h = h;
}

static void reset_clip(void)
{
    set_clip(0, 0, DOODLE_SCR_W, DOODLE_SCR_H);
}

static int lookup_slot(int spr_id, int xform)
{
    if (spr_id < 0 || spr_id >= MAX_LOGICAL_SPRITES)
        return -1;
    int s = a_arr[1356 + spr_id];
    if (s < 0 || s >= MAX_PHYS_SLOTS)
        return -1;
    if (xform != 0) {
        while (s + 1 < MAX_PHYS_SLOTS && slots[s].xform != 15 && slots[s].xform != 0) {
            int tc = slots[s].xform;
            ++s;
            if (tc == xform)
                return s;
        }
    }
    return a_arr[1356 + spr_id];
}

static int spr_w(int spr_id)
{
    int s = lookup_slot(spr_id, 0);
    return (s >= 0) ? (int)slots[s].w : 0;
}

static int spr_h(int spr_id)
{
    int s = lookup_slot(spr_id, 0);
    return (s >= 0) ? (int)slots[s].h : 0;
}

static void draw_sprite(const struct eapp_framebuffer *fb, int spr_id, int x, int y, int anchor,
                        int xform)
{
    int s = lookup_slot(spr_id, xform);
    if (s < 0 || !slots[s].rgb)
        return;

    const struct doodle_sprite *sp = &slots[s];
    int sw = (int)sp->w;
    int sh = (int)sp->h;

    if (anchor & ANC_HCENTER)
        x -= sw >> 1;
    else if (anchor & ANC_RIGHT)
        x -= sw;

    if (anchor & ANC_VCENTER)
        y -= sh >> 1;
    else if (anchor & ANC_BOTTOM)
        y -= sh;

    int cx0 = clip_x < 0 ? 0 : clip_x;
    int cy0 = clip_y < 0 ? 0 : clip_y;
    int cx1 = clip_x + clip_w;
    int cy1 = clip_y + clip_h;
    if (cx1 > (int)fb->width)
        cx1 = (int)fb->width;
    if (cy1 > (int)fb->height)
        cy1 = (int)fb->height;

    int x0 = x < cx0 ? cx0 : x;
    int y0 = y < cy0 ? cy0 : y;
    int x1 = x + sw > cx1 ? cx1 : x + sw;
    int y1 = y + sh > cy1 ? cy1 : y + sh;
    if (x0 >= x1 || y0 >= y1)
        return;

    int sx_off = x0 - x;
    int sy_off = y0 - y;
    int cw = x1 - x0;
    const uint16_t *src_rgb = sp->rgb;
    const uint8_t *src_a = sp->alpha;

    if (!src_a || sp->blend == 0) {
        for (int row = y0; row < y1; ++row) {
            const uint16_t *sr = src_rgb + (sy_off + row - y0) * sw + sx_off;
            uint16_t *dr = fb->pixels + (unsigned)row * fb->stride_pixels + (unsigned)x0;
            memcpy(dr, sr, (size_t)cw * 2u);
        }
    } else if (sp->blend == 1) {
        for (int row = y0; row < y1; ++row) {
            int off = (sy_off + row - y0) * sw + sx_off;
            const uint16_t *sr = src_rgb + off;
            const uint8_t *sa = src_a + off;
            uint16_t *dr = fb->pixels + (unsigned)row * fb->stride_pixels + (unsigned)x0;
            for (int col = 0; col < cw; ++col) {
                if (sa[col])
                    dr[col] = sr[col];
            }
        }
    } else {
        for (int row = y0; row < y1; ++row) {
            int off = (sy_off + row - y0) * sw + sx_off;
            const uint16_t *sr = src_rgb + off;
            const uint8_t *sa = src_a + off;
            uint16_t *dr = fb->pixels + (unsigned)row * fb->stride_pixels + (unsigned)x0;
            for (int col = 0; col < cw; ++col) {
                uint8_t a = sa[col];
                if (a == 255) {
                    dr[col] = sr[col];
                } else if (a > 0) {
                    int alpha = (int)a + ((int)a >> 7);
                    uint16_t s_px = sr[col];
                    uint16_t d_px = dr[col];
                    int s_r = s_px >> 11, s_g = (s_px >> 5) & 63, s_b = s_px & 31;
                    int d_r = d_px >> 11, d_g = (d_px >> 5) & 63, d_b = d_px & 31;
                    d_r += ((s_r - d_r) * alpha) >> 8;
                    d_g += ((s_g - d_g) * alpha) >> 8;
                    d_b += ((s_b - d_b) * alpha) >> 8;
                    dr[col] = (uint16_t)((d_r << 11) | (d_g << 5) | d_b);
                }
            }
        }
    }
}

static void tile_sprite(const struct eapp_framebuffer *fb, int spr_id, int x, int y, int w, int h)
{
    int sw = spr_w(spr_id);
    int sh = spr_h(spr_id);
    if (sw <= 0 || sh <= 0)
        return;
    int ox = clip_x, oy = clip_y, ow = clip_w, oh = clip_h;
    int nx0 = x > ox ? x : ox;
    int ny0 = y > oy ? y : oy;
    int nx1 = (x + w) < (ox + ow) ? (x + w) : (ox + ow);
    int ny1 = (y + h) < (oy + oh) ? (y + h) : (oy + oh);
    if (nx1 <= nx0 || ny1 <= ny0)
        return;
    set_clip(nx0, ny0, nx1 - nx0, ny1 - ny0);
    for (int py = y; py < y + h; py += sh) {
        for (int px = x; px < x + w; px += sw)
            draw_sprite(fb, spr_id, px, py, ANC_TOP_LEFT, XFORM_NONE);
    }
    set_clip(ox, oy, ow, oh);
}

static void stretch_h_sprite(const struct eapp_framebuffer *fb, int spr_id, int x, int y, int width)
{
    int sw = spr_w(spr_id);
    int sh = spr_h(spr_id);
    if (sw <= 0 || sh <= 0 || width <= 0)
        return;
    int full = width / sw;
    for (int i = 0; i < full; ++i) {
        draw_sprite(fb, spr_id, x, y, ANC_TOP_LEFT, XFORM_NONE);
        x += sw;
        width -= sw;
    }
    if (width > 0) {
        int ox = clip_x, oy = clip_y, ow = clip_w, oh = clip_h;
        int nx0 = x > ox ? x : ox;
        int ny0 = y > oy ? y : oy;
        int nx1 = (x + width) < (ox + ow) ? (x + width) : (ox + ow);
        int ny1 = (y + sh) < (oy + oh) ? (y + sh) : (oy + oh);
        if (nx1 > nx0 && ny1 > ny0) {
            set_clip(nx0, ny0, nx1 - nx0, ny1 - ny0);
            draw_sprite(fb, spr_id, x, y, ANC_TOP_LEFT, XFORM_NONE);
            set_clip(ox, oy, ow, oh);
        }
    }
}

static void draw_torn_panel(const struct eapp_framebuffer *fb, int x, int y, int w, int h)
{
    int cw = spr_w(728);
    int ch = spr_h(728);
    int tw = spr_w(729);
    int lh = spr_h(731);
    if (cw <= 0 || tw <= 0 || lh <= 0)
        return;

    /* Adjust size to fit cleanly like f.java:1655-1657 */
    int ox = clip_x, oy = clip_y, ow = clip_w, oh = clip_h;
    tile_sprite(fb, 736, x + 6, y + 6, w - 12, h - 12);

    draw_sprite(fb, 728, x, y, ANC_TOP_LEFT, 0);
    draw_sprite(fb, 730, x + w, y, ANC_TOP_RIGHT, 0);
    draw_sprite(fb, 733, x, y + h, ANC_BOTTOM_LEFT, 0);
    draw_sprite(fb, 735, x + w, y + h, ANC_BOTTOM_RIGHT, 0);

    set_clip(x + cw, y, w - cw * 2, h + 16);
    for (int px = x + cw; px < x + w - cw; px += tw) {
        draw_sprite(fb, 729, px, y, ANC_TOP_LEFT, 0);
        draw_sprite(fb, 734, px, y + h, ANC_BOTTOM_LEFT, 0);
    }
    set_clip(x, y + ch, w + 16, h - ch * 2);
    for (int py = y + ch; py < y + h - ch; py += lh) {
        draw_sprite(fb, 731, x, py, ANC_TOP_LEFT, 0);
        draw_sprite(fb, 732, x + w, py, ANC_TOP_RIGHT, 0);
    }
    set_clip(ox, oy, ow, oh);
}

/* Bitmap Font engine (fonts 3..7 from JAR) */

static int font_height(int font_id)
{
    if (font_id >= 3 && font_id <= 7)
        return d_arr[3870 + (font_id - 3)];
    return 14;
}

static int font_char_index(int font_id, uint16_t cp)
{
    int n = font_id - 3;
    int start = a_arr[50 + n];
    int end = a_arr[50 + n + 1];
    for (int i = 0; i < end - start; ++i) {
        if ((uint16_t)a_arr[56 + start + i] == cp)
            return i;
    }
    return 0; /* space */
}

static int font_text_width(int font_id, const char *s)
{
    if (!s)
        return 0;
    if (font_id < 3 || font_id > 7)
        return (int)eapp_text_width(s, 1);
    int n = font_id - 3;
    int adv_base = a_arr[32 + n];
    int w = 0;
    const char *p = s;
    while (*p) {
        uint16_t cp = utf8_next(&p);
        int idx = font_char_index(font_id, cp);
        w += d_arr[adv_base + idx];
    }
    return w;
}

static int font_char_width(int font_id, uint16_t cp)
{
    int n = font_id - 3;
    int adv_base = a_arr[32 + n];
    int idx = font_char_index(font_id, cp);
    return d_arr[adv_base + idx];
}

static void draw_font_str(const struct eapp_framebuffer *fb, int font_id, const char *s, int x,
                          int y, int anchor)
{
    if (!s || !*s)
        return;
    if (font_id < 3 || font_id > 7) {
        eapp_draw_text(fb, x, y, s, 0x0000, 1);
        return;
    }

    int tw = font_text_width(font_id, s);
    int th = font_height(font_id);
    if (anchor & ANC_HCENTER)
        x -= tw >> 1;
    else if (anchor & ANC_RIGHT)
        x -= tw;
    if (anchor & ANC_VCENTER)
        y -= th >> 1;
    else if (anchor & ANC_BOTTOM)
        y -= th;

    int n = font_id - 3;
    int adv_base = a_arr[32 + n];
    int xoff_base = a_arr[38 + n];
    int yoff_base = a_arr[44 + n];
    int gmap_base = a_arr[2597 + n];

    const char *p = s;
    while (*p) {
        uint16_t cp = utf8_next(&p);
        int idx = font_char_index(font_id, cp);
        int gid = a_arr[2603 + gmap_base + idx];
        if (gid > 0) {
            draw_sprite(fb, gid - 1, x + d_arr[1290 + xoff_base + idx],
                        y + d_arr[2580 + yoff_base + idx], ANC_TOP_LEFT, XFORM_NONE);
        }
        x += d_arr[adv_base + idx];
    }
}

static void draw_pill_button(const struct eapp_framebuffer *fb, const char *label, int x, int y,
                             int w, int selected, const char *sub)
{
    int base = selected ? 7 : 4;
    draw_sprite(fb, base, x, y, ANC_TOP_LEFT, 0);
    int lw = spr_w(base);
    int rw = spr_w(base + 2);
    int mw = w - lw - rw;
    if (mw > 0) {
        int sw = spr_w(base + 1);
        int ox = clip_x, oy = clip_y, ow = clip_w, oh = clip_h;
        set_clip(x + lw > ox ? x + lw : ox, oy,
                 (x + lw + mw < ox + ow ? x + lw + mw : ox + ow) - (x + lw > ox ? x + lw : ox),
                 oh);
        for (int px = x + lw; px < x + w - rw; px += (sw > 0 ? sw : 4))
            draw_sprite(fb, base + 1, px, y, ANC_TOP_LEFT, 0);
        set_clip(ox, oy, ow, oh);
    }
    draw_sprite(fb, base + 2, x + w, y, ANC_TOP_RIGHT, 0);

    if (sub && *sub) {
        draw_font_str(fb, selected ? 5 : 3, label, x + (w >> 1), y + 2, ANC_TOP_HCENTER);
        draw_font_str(fb, 7, sub, x + (w >> 1), y + 23, ANC_TOP_HCENTER);
    } else {
        draw_font_str(fb, selected ? 5 : 3, label, x + (w >> 1), y + 3, ANC_TOP_HCENTER);
    }
}

static void draw_wrapped_text(const struct eapp_framebuffer *fb, int font_id, const char *text,
                              int x, int y, int max_w, int max_h, int scroll_y, int *out_total_h)
{
    int line_h = font_height(font_id);
    if (line_h < 16)
        line_h = 16;
    int step_y = line_h - 4;
    if (font_id == 7)
        step_y = line_h + 2;

    int cur_y = 0;
    int ox = clip_x, oy = clip_y, ow = clip_w, oh = clip_h;
    if (fb)
        set_clip(x, y, max_w, max_h);

    const char *p = text;
    char line_buf[128];
    while (*p) {
        if (*p == '\n') {
            ++p;
            cur_y += step_y;
            continue;
        }
        /* Handle inline control tokens \x14 (20), \x15 (21), \x13<n>: (19), \x19<id>: (25) */
        if ((uint8_t)*p == 19 || (uint8_t)*p == 27) {
            ++p;
            while (*p && *p != ':')
                ++p;
            if (*p == ':')
                ++p;
            continue;
        }
        if ((uint8_t)*p == 20 || (uint8_t)*p == 21 || (uint8_t)*p == 22) {
            ++p;
            continue;
        }
        if ((uint8_t)*p == 25) {
            ++p;
            int sid = 0;
            while (*p >= '0' && *p <= '9') {
                sid = sid * 10 + (*p - '0');
                ++p;
            }
            if (*p == ':')
                ++p;
            int dy = y + cur_y - scroll_y;
            if (sid == 1) {
                if (fb && dy + 12 >= y && dy < y + max_h) {
                    int px = x + 4;
                    draw_sprite(fb, 22, px, dy + 4, ANC_TOP_LEFT, 0);
                    stretch_h_sprite(fb, 23, px + spr_w(22), dy + 4, 24);
                    draw_sprite(fb, 24, px + spr_w(22) + 24, dy + 4, ANC_TOP_LEFT, 0);
                }
                cur_y += 20;
            } else if (sid == 2 || sid == 3) {
                int sp = (sid == 2) ? 25 : 26;
                int sh = spr_h(sp);
                if (fb && dy + sh >= y && dy < y + max_h)
                    draw_sprite(fb, sp, x + (sid == 2 ? 12 : 80), dy, ANC_TOP_LEFT, 0);
                if (sid == 3)
                    cur_y += 42;
            }
            continue;
        }

        /* Collect word-wrapped line */
        int lw = 0;
        int len = 0;
        int last_space_len = -1;
        const char *last_space_p = NULL;
        const char *scan = p;
        while (*scan && *scan != '\n' && (uint8_t)*scan != 25 && (uint8_t)*scan != 19 &&
               (uint8_t)*scan != 20 && (uint8_t)*scan != 21) {
            const char *prev_scan = scan;
            uint16_t cp = utf8_next(&scan);
            int ch_bytes = (int)(scan - prev_scan);
            int cw = font_char_width(font_id, cp);
            if (len + ch_bytes >= (int)sizeof(line_buf) - 1)
                break;
            if (lw + cw > max_w && len > 0) {
                if (last_space_len > 0) {
                    len = last_space_len;
                    scan = last_space_p;
                } else {
                    scan = prev_scan;
                }
                break;
            }
            memcpy(line_buf + len, prev_scan, (size_t)ch_bytes);
            len += ch_bytes;
            lw += cw;
            if (cp == ' ') {
                last_space_len = len - 1;
                last_space_p = scan;
            }
        }
        line_buf[len] = 0;
        if (fb && len > 0) {
            int dy = y + cur_y - scroll_y;
            if (dy + line_h >= y && dy < y + max_h)
                draw_font_str(fb, font_id, line_buf, x, dy, ANC_TOP_LEFT);
        }
        cur_y += step_y;
        p = scan;
        while (*p == ' ')
            ++p;
    }

    if (fb)
        set_clip(ox, oy, ow, oh);
    if (out_total_h)
        *out_total_h = cur_y;
}

/* ---------- World Spawning & Physics ---------- */

static void add_platform(int32_t x, int32_t y, uint32_t flags, int32_t pvx, int32_t idx)
{
    plats[plat_tail].x = x;
    plats[plat_tail].y = y;
    plats[plat_tail].flags = flags;
    plats[plat_tail].timer = 0;
    plats[plat_tail].vx = pvx;
    plats[plat_tail].item_dx = idx;
    plat_tail = (plat_tail + 1) % 100;
}

static void add_hazard(int32_t x, int32_t y, int32_t type)
{
    hazards[haz_tail].type = type;
    hazards[haz_tail].hit_timer = 0;
    hazards[haz_tail].x = x;
    hazards[haz_tail].y = y;
    hazards[haz_tail].vy_dead = 0;
    hazards[haz_tail].phase_x = 0;
    hazards[haz_tail].phase_y = 0;
    hazards[haz_tail].off_x = 0;
    hazards[haz_tail].off_y = 0;
    haz_tail = (haz_tail + 1) % 10;
}

static void trim_offscreen(void)
{
    while (plat_head != plat_tail && plats[plat_head].y - cam_y < -WORLD_H)
        plat_head = (plat_head + 1) % 100;
    while (haz_head != haz_tail && hazards[haz_head].y - cam_y < -WORLD_H)
        haz_head = (haz_head + 1) % 10;
}

static void fill_platforms(int allow_items, int32_t target_h)
{
    int32_t last_y = 0;
    uint32_t last_flags = 1;
    if (plat_head != plat_tail) {
        int last_idx = (plat_tail + 99) % 100;
        last_y = plats[last_idx].y;
        last_flags = plats[last_idx].flags;
    }
    int last_haz = (haz_tail + 9) % 10;
    if (haz_head != haz_tail && hazards[last_haz].y > last_y) {
        last_y = hazards[last_haz].y;
        last_flags = 0x200000u;
    }

    while (last_y - cam_y < target_h || (last_flags & 0x1E00000u) != 0) {
        int32_t alt = last_y >> 11;
        uint32_t fl = 1;
        int32_t pvx = 0;
        int32_t idx = 0;

        if ((rand_pos() % 5) == 0 && (last_flags & 0x1E00000u) == 0)
            fl = 0x200000u;

        if (alt > 5500 && (rand_pos() % 100) < alt / 400) {
            pvx = 60 * (1024 + (rand_pos() % 10) * 102) + fp_div(cam_y, 6144000);
            if (pvx > 307200)
                pvx = 307200;
            fl |= 4u;
        }

        int32_t px = (PLAT_WORLD_W >> 1) + (rand_pos() % (WORLD_W - PLAT_WORLD_W));
        int32_t gap = 25 + (rand_pos() % ((alt + 150) / 150));
        if (gap > 90)
            gap = 90;
        if ((last_flags & 0x1E00000u) != 0 && gap > 50)
            gap = 50;
        if ((last_flags & 0x38u) != 0 && (last_flags & 0x1E00000u) == 0) {
            gap += 15;
            int32_t extra = 30 + (rand_pos() % ((alt + 50) / 50));
            if ((last_flags & 0x10u) == 16)
                extra += 10;
            if (extra > 70)
                extra = 70;
            gap += extra;
        }
        if (gap < min_step_px && (last_flags & 0x1E00000u) == 0)
            gap = min_step_px;

        last_y = ((last_y >> 11) + gap) << 11;

        if ((fl & 0x1E00000u) == 0) {
            int32_t spring_half = fp_div(24576, scale_x);
            int full_mode = save.full_mode;
            if (full_mode && jet_spawned < 3 && next_jet_y < alt) {
                fl |= 0x10u;
                next_jet_y += rand_pos() % 9500 + 2500;
                int32_t span = PLAT_WORLD_W - fp_div(34816, scale_x) - spring_half;
                if (span > 1)
                    idx = (rand_pos() % span) - (span >> 1);
            } else if (full_mode && next_hat_y < alt) {
                fl |= 0x20u;
                next_hat_y += rand_pos() % 9000 + 1000;
                int32_t span = PLAT_WORLD_W - fp_div(47104, scale_x) - spring_half;
                if (span > 1)
                    idx = (rand_pos() % span) - (span >> 1);
            } else if ((rand_pos() % 100) > 90 && allow_items) {
                fl |= 8u;
                int32_t span = PLAT_WORLD_W - spring_half * 2;
                if (span > 1)
                    idx = (rand_pos() % span) - (span >> 1);
            }
        }
        add_platform(px, last_y, fl, pvx, idx);
        last_flags = fl;
    }
}

static void spawn_step(void)
{
    if (plat_head == plat_tail)
        add_platform(327680, 61440, 1u, 0, 0);

    int last_idx = (plat_tail + 99) % 100;
    int32_t top_y = plats[last_idx].y;

    if (top_y > 10240000 && top_y - cam_y < WORLD_H && (rand_pos() % 20) == 10 &&
        (player_st & 0x77u) == 0 && pat_data != NULL) {
        int start_plat = (plat_tail + 99) % 100;
        fill_platforms(0, plats[start_plat].y - cam_y + 1024000);

        last_idx = (plat_tail + 99) % 100;
        int32_t base_y = plats[last_idx].y;
        uint32_t base_fl = plats[last_idx].flags;
        int last_h = (haz_tail + 9) % 10;
        if (haz_head != haz_tail && base_y < hazards[last_h].y)
            base_y = hazards[last_h].y;

        int32_t pstart, pend;
        if (base_y < 30720000) {
            int k;
            do {
                k = rand_pos() % 7;
            } while (((1u << k) & pat_low_mask) != 0);
            pstart = pat_low_start[k];
            pend = pat_low_start[k + 1];
            pat_low_mask |= (1u << k);
            if (pat_low_mask == 127u)
                pat_low_mask = 0;
        } else {
            int k;
            do {
                k = rand_pos() % 22;
            } while (((1u << k) & pat_high_mask) != 0);
            pstart = pat_high_start[k];
            pend = pat_high_start[k + 1];
            pat_high_mask |= (1u << k);
            if (pat_high_mask == 0x3FFFFFu)
                pat_high_mask = 0;
        }

        int had_monster = 0;
        for (int p = pstart; p < pend; p += 3) {
            int32_t kind = pat_data[p];
            int32_t px = (int32_t)pat_data[p + 1] << 11;
            int32_t dy = pat_data[p + 2];
            if (p == pstart && dy < min_step_px && (base_fl & 0x1E00000u) == 0)
                base_y += (min_step_px - dy) << 11;
            int32_t py = base_y + (dy << 11);
            int32_t item_dx = (rand_pos() % (PLAT_WORLD_W - 24576)) - ((PLAT_WORLD_W - 24576) >> 1);
            int32_t pvx = 60 * (1024 + (rand_pos() % 10) * 102) + fp_div(cam_y, 6144000);
            if (pvx > 307200)
                pvx = 307200;

            switch (kind) {
            case 4:
                add_hazard(px, py, 4);
                break;
            case 0:
                add_platform(px, py, 1u, 0, 0);
                break;
            case 1:
                add_platform(px, py, 5u, pvx, 0);
                break;
            case 2:
                add_platform(px, py, 0x200000u, 0, 0);
                break;
            case 5:
                add_platform(px, py, 9u, 0, item_dx);
                break;
            case 3:
                add_platform(px, py, 2u, 0, 0);
                break;
            case 16:
                add_platform(px, py, 256u, 0, 0);
                break;
            case 17: {
                int32_t delay = (rand_pos() % 2000) + 1000;
                add_platform(px, py, 256u, 0, delay);
                break;
            }
            default:
                if (kind >= 6 && kind <= 11) {
                    add_hazard(px, py, kind);
                    had_monster = 1;
                }
                break;
            }
        }

        if (had_monster && save.full_mode) {
            int end_plat = (plat_tail + 99) % 100;
            if ((rand_pos() % 8) > 2 && start_plat != end_plat) {
                int span = (end_plat - start_plat + 100) % 100;
                for (int tries = 0; tries < 20; ++tries) {
                    int pick = (start_plat + (rand_pos() % span)) % 100;
                    if ((plats[pick].flags & 1u) != 0 && (plats[pick].flags & 0x38u) == 0) {
                        plats[pick].flags |= 0x80u;
                        break;
                    }
                }
            }
        }
    }

    fill_platforms(1, WORLD_H);
}

static void start_power(uint32_t bit)
{
    player_st |= bit;
    state_timer = 0;
    if ((bit & (ST_SHIELD | ST_SHIELD_BLINK)) != 0)
        shield_timer = 0;
}

static void end_power(uint32_t bit)
{
    player_st &= ~bit;
}

static void update_hazards(int32_t dt)
{
    int32_t kill_margin = fp_div(88 << 11, scale_y) >> 1;
    for (int i = haz_head; i != haz_tail; i = (i + 1) % 10) {
        struct hazard *h = &hazards[i];
        if (h->y - cam_y < -kill_margin && h->type != 20) {
            h->type = 20;
            clear_looping_sfx();
            continue;
        }
        if (h->type == 20 || i == last_hit_haz)
            continue;
        if (h->vy_dead != 0) {
            h->y += fp_mul(h->vy_dead, dt);
            continue;
        }
        if (h->type == 10 || h->type == 11 || h->type == 22 || h->type == 21 ||
            h->type == 8 || h->type == 7) {
            int32_t dx = (10 * sin_lut[(h->phase_x >> 11) % 360]) >> 3;
            h->x += dx - h->off_x;
            h->off_x = dx;
            h->phase_x += 28672;
            while (h->phase_x >= 737280)
                h->phase_x -= 737280;

            int32_t dy = (5 * sin_lut[(h->phase_y >> 11) % 360]) >> 3;
            h->y += dy - h->off_y;
            h->off_y = dy;
            h->phase_y += 5734;
            while (h->phase_y >= 737280)
                h->phase_y -= 737280;

            if (h->type == 22) {
                h->hit_timer += dt;
                if (h->hit_timer > 192)
                    h->type = 21;
            }
        } else if (h->type == 9) {
            if (h->phase_x == 0)
                h->phase_x = 245760;
            h->x += fp_mul(dt, h->phase_x);
            int32_t hw = fp_mul(55296, scale_x) >> 1;
            if (h->x + hw > WORLD_W) {
                h->x = WORLD_W - hw;
                h->phase_x = -245760;
            } else if (h->x - hw < 0) {
                h->x = hw;
                h->phase_x = 245760;
            }
            int32_t dy = (10 * sin_lut[(h->phase_y >> 11) % 360]) >> 3;
            h->y += dy - h->off_y;
            h->off_y = dy;
            h->phase_y += 17408;
            while (h->phase_y >= 737280)
                h->phase_y -= 737280;
        } else if (h->type == 6) {
            int32_t deg_x = (fp_div(fp_mul(h->phase_x, 40), 12868) * 360) >> 11;
            if (deg_x < 0)
                deg_x = -deg_x;
            int32_t dx = 20 * (sin_lut[deg_x % 360] >> 3);
            h->x += dx - h->off_x;
            h->off_x = dx;
            h->phase_x += 2048;
            if (h->phase_x >= 645120)
                h->phase_x = 0;

            int32_t deg_y = (fp_div(fp_mul(h->phase_y, 81), 12868) * 360) >> 11;
            if (deg_y < 0)
                deg_y = -deg_y;
            int32_t dy = 10 * (sin_lut[deg_y % 360] >> 3);
            h->y += dy - h->off_y;
            h->off_y = dy;
            h->phase_y += 2048;
            if (h->phase_y >= 323584)
                h->phase_y = 0;
        }
    }
}

static int check_bullet_hit(int32_t hx, int32_t hy, int32_t hw, int32_t hh)
{
    int32_t pad_x = fp_div(16384, scale_x) >> 1;
    int32_t pad_y = fp_div(16384, scale_y) >> 1;
    int32_t min_x = hx - (hw >> 1) - pad_x;
    int32_t max_x = hx + (hw >> 1) + pad_x;
    int32_t min_y = hy - (hh >> 1) - pad_y;
    int32_t max_y = hy + (hh >> 1) + pad_y;

    for (int i = 0; i < bullet_count; ++i) {
        if (bullets[i].x > min_x && bullets[i].x < max_x &&
            bullets[i].y > min_y && bullets[i].y < max_y) {
            bullet_hit_idx = i;
            return 1;
        }
    }
    return 0;
}

static void enter_screen(int scr);

static void finish_game(void)
{
    clear_looping_sfx();
    stop_all_sfx();
    game_paused_midrun = 0;
    insert_high_score(save.full_mode, score);
    enter_screen(SCR_GAME_OVER);
}

static void step_player(int32_t dt)
{
    (void)dt;
    /* Update bullets */
    for (int i = 0; i < bullet_count; ++i) {
        if (i == bullet_hit_idx)
            continue;
        bullets[i].x += fp_mul(34, bullets[i].vx);
        bullets[i].y += fp_mul(34, bullets[i].vy);
    }
    for (int i = bullet_count - 1; i >= 0; --i) {
        if (bullets[i].y > WORLD_H) {
            for (int j = i; j < bullet_count - 1; ++j)
                bullets[j] = bullets[j + 1];
            --bullet_count;
            if (bullet_hit_idx > i)
                --bullet_hit_idx;
        }
    }

    if ((player_st & (ST_FALL_OUT | ST_DEAD_WAIT)) != 0 &&
        (player_st & (ST_UFO_BEAM | ST_UFO_DONE | ST_BH_SUCK | ST_BH_DONE)) == 0) {
        bg_scroll_stopped = 0;
        vy += fp_mul(ay, 34);
        if (vy < MIN_VY)
            vy = MIN_VY;
        player_y += fp_mul(vy, 34);

        if ((player_st & ST_DEAD_WAIT) != 0) {
            paper_scroll = 0;
            bg_scroll_stopped = 1;
            if (state_timer > 1269)
                finish_game();
            return;
        }
        if ((player_st & ST_FALL_OUT) != 0) {
            if (state_timer > 1638) {
                paper_scroll = 0;
                bg_scroll_stopped = 1;
                if (state_timer > 1751) {
                    end_power(ST_FALL_OUT);
                    start_power(ST_DEAD_WAIT);
                    player_y = CAM_THRESH;
                }
                return;
            }
            int32_t fall_cam = 34 * 1200;
            cam_y -= fall_cam;
            int s = (int)(((cam_y >> 11) - score) % 69);
            if (s < 0)
                s += 69;
            paper_scroll = s;
            return;
        }
    }

    update_hazards(34);
    vy += fp_mul(ay, 34);
    if (vy < MIN_VY)
        vy = MIN_VY;
    player_y += fp_mul(vy, 34);

    if (player_y < 0 && (player_st & (ST_UFO_BEAM | ST_UFO_DONE | ST_BH_SUCK | ST_BH_DONE)) == 0) {
        play_sfx(SND_FALL, 1);
        start_power(ST_FALL_OUT);
        while (plat_head != plat_tail && plats[plat_head].y - cam_y < 0)
            plat_head = (plat_head + 1) % 100;
        while (haz_head != haz_tail && hazards[haz_head].y - cam_y < -WORLD_H)
            haz_head = (haz_head + 1) % 10;
        return;
    }

    int32_t cam_delta = 0;
    if (player_y > CAM_THRESH) {
        cam_delta = player_y - CAM_THRESH;
        player_y = CAM_THRESH;
        cam_y += cam_delta;
        score = cam_y >> 11;
    }

    if ((player_st & ST_PROP_RISE) && state_timer > 2048) {
        end_power(ST_PROP_RISE);
        start_power(ST_PROP_CRUISE);
        ay = 0;
    }
    if ((player_st & ST_PROP_CRUISE) && state_timer > 2048) {
        end_power(ST_PROP_CRUISE);
        start_power(ST_PROP_END);
        ay = -884736;
    }
    if ((player_st & ST_PROP_END) && state_timer > 2048) {
        end_power(ST_PROP_END);
        start_power(ST_PROP_DROP);
        ay = -GRAVITY;
        ufo_facing_left = facing_left;
        int32_t offx = fp_div(43008, scale_x);
        deb_x = facing_left ? (player_x + offx) : (player_x - offx);
        deb_y = player_y - fp_div(-59392, scale_y);
        deb_vy = vy;
        deb_ay = -3686400;
    }
    if (player_st & ST_PROP_DROP) {
        deb_vy += fp_mul(deb_ay, 34);
        if (deb_vy < -614400)
            deb_vy = -614400;
        deb_y += fp_mul(deb_vy, 34) - cam_delta;
        if (deb_y < 0)
            end_power(ST_PROP_DROP);
    }

    if ((player_st & ST_JET_RISE) && state_timer > JET_RISE_TIME) {
        end_power(ST_JET_RISE);
        start_power(ST_JET_CRUISE);
        ay = 0;
    }
    if ((player_st & ST_JET_CRUISE) && state_timer > JET_CRUISE_TIME) {
        end_power(ST_JET_CRUISE);
        start_power(ST_JET_END);
        ay = -737280;
    }
    if ((player_st & ST_JET_END) && state_timer > JET_FALL_TIME) {
        end_power(ST_JET_END);
        start_power(ST_JET_DROP);
        ay = -GRAVITY;
        ufo_facing_left = facing_left;
        int32_t offx = fp_div(8192, scale_x);
        if (facing_left) {
            deb_x = player_x + offx;
            deb_vx = -368640;
            deb_ax = 368640;
        } else {
            deb_x = player_x - offx;
            deb_vx = 368640;
            deb_ax = -368640;
        }
        deb_y = player_y - fp_div(-59392, scale_y);
        deb_vy = vy + 245760;
        deb_ay = -3686400;
    }
    if (player_st & ST_JET_DROP) {
        deb_vy += fp_mul(deb_ay, 34);
        if (deb_vy < -614400)
            deb_vy = -614400;
        deb_y += fp_mul(deb_vy, 34) - cam_delta;
        if (deb_y < 0)
            end_power(ST_JET_DROP);
        deb_vx += fp_mul(deb_ax, 34);
        deb_x += fp_mul(deb_vx, 34);
    }

    if ((player_st & ST_SHIELD) && shield_timer > 8533) {
        end_power(ST_SHIELD);
        start_power(ST_SHIELD_BLINK);
    }
    if ((player_st & ST_SHIELD_BLINK) && shield_timer > 4096)
        end_power(ST_SHIELD_BLINK);

    if (player_st & ST_UFO_BEAM) {
        if ((player_x + 20480 > hazards[abduct_haz_idx].x &&
             player_x - 20480 < hazards[abduct_haz_idx].x &&
             player_y + (doodler_h >> 1) + 20480 + cam_y > hazards[abduct_haz_idx].y &&
             player_y + (doodler_h >> 1) - 20480 + cam_y < hazards[abduct_haz_idx].y) ||
            state_timer > 1228) {
            end_power(ST_UFO_BEAM);
            start_power(ST_UFO_DONE);
        }
    }
    if (player_st & ST_BH_SUCK) {
        int32_t bh_off = fp_div(90112, scale_y);
        if ((player_x + 20480 > hazards[abduct_haz_idx].x &&
             player_x - 20480 < hazards[abduct_haz_idx].x &&
             player_y + (doodler_h >> 1) + 20480 + cam_y > hazards[abduct_haz_idx].y + bh_off &&
             player_y + (doodler_h >> 1) - 20480 + cam_y < hazards[abduct_haz_idx].y + bh_off) ||
            state_timer > 2048) {
            end_power(ST_BH_SUCK);
            start_power(ST_BH_DONE);
        }
    }
    if ((player_st & (ST_UFO_DONE | ST_BH_DONE)) != 0)
        finish_game();
}

static int check_collisions(void)
{
    if ((player_st & (ST_BH_SUCK | ST_BH_DONE | ST_UFO_DONE | ST_STUNNED | ST_DEAD_WAIT)) != 0)
        return 0;

    for (int i = haz_head; i != haz_tail; i = (i + 1) % 10) {
        struct hazard *h = &hazards[i];
        int32_t hx = h->x;
        int32_t hy = h->y - cam_y;

        if (h->type == 4 && (player_st & (ST_PROP_MASK | ST_JET_MASK | ST_UFO_BEAM)) == 0) {
            int32_t x0 = hx - fp_div(33792, scale_x) - (doodler_w >> 1);
            int32_t x1 = hx + fp_div(33792, scale_x) + (doodler_w >> 1);
            int32_t y0 = hy - fp_div(33792, scale_y) - doodler_h;
            int32_t y1 = hy + fp_div(33792, scale_y);
            if (player_x >= x0 && player_x <= x1 && player_y >= y0 && player_y <= y1) {
                start_power(ST_BH_SUCK);
                abduct_haz_idx = i;
                ax = 0;
                ay = 0;
                vx = -(player_x - h->x);
                vy = -(player_y + cam_y + (doodler_h >> 1) - h->y);
                play_sfx(SND_BLACK_HOLE, 1);
                clear_looping_sfx();
                continue;
            }
        }

        int32_t hw = 0, hh = 0;
        switch (h->type) {
        case 9:
            hw = fp_div(27648, scale_x);
            hh = fp_div(35840, scale_y);
            break;
        case 10:
            hw = fp_div(59392, scale_x);
            hh = fp_div(33792, scale_y);
            break;
        case 8:
            hw = fp_div(33792, scale_x);
            hh = fp_div(27648, scale_y);
            break;
        case 11:
        case 21:
            hw = fp_div(61440, scale_x);
            hh = fp_div(44032, scale_y);
            break;
        case 7:
            hw = fp_div(39936, scale_x);
            hh = fp_div(35840, scale_y);
            break;
        case 6:
            hw = fp_div(58368, scale_x);
            hh = fp_div(24576, scale_y);
            hy += fp_div(65536, scale_y);
            break;
        default:
            continue;
        }

        hw -= hw >> 4;
        hh -= hh >> 4;

        if (check_bullet_hit(hx, hy, hw << 1, hh << 1)) {
            last_hit_haz = i;
            if (h->type != 11)
                clear_looping_sfx();
            play_sfx(SND_SHOOT_KILL, 1);
            continue;
        }

        if ((player_st & ST_JET_MASK) != 0) {
            int32_t jpad = hh + fp_div(55296, scale_y);
            if (player_x >= hx - hw && player_x <= hx + hw &&
                player_y >= hy - jpad - (doodler_h >> 1) &&
                player_y <= hy + jpad - (doodler_h >> 1) && h->vy_dead == 0) {
                h->type = 20;
                clear_looping_sfx();
                play_sfx(SND_SHOOT_KILL, 1);
            }
            continue;
        }

        if ((player_st & ST_PROP_MASK) != 0)
            continue;

        if (h->type == 6) {
            hy = h->y - cam_y;
            hw = fp_div(63488, scale_x);
            hh = fp_div(90112, scale_y);
        }

        int32_t cw = hw + (doodler_w >> 1);
        int32_t ch = hh + (doodler_h >> 1);
        if (player_x >= hx - cw && player_x <= hx + cw &&
            player_y >= hy - ch - (doodler_h >> 1) &&
            player_y <= hy + ch - (doodler_h >> 1) && h->vy_dead == 0) {
            if (vy >= 0 && (player_st & (ST_SHIELD | ST_SHIELD_BLINK)) == 0) {
                if (h->type != 6) {
                    start_power(ST_STUNNED);
                    vy = 0;
                    vx = -vx;
                    play_sfx(SND_MONSTER_HIT, 1);
                    clear_looping_sfx();
                } else {
                    start_power(ST_UFO_BEAM);
                    play_sfx(SND_UFO_ABDUCT, 1);
                    clear_looping_sfx();
                    abduct_haz_idx = i;
                    vx = -(player_x - h->x);
                    vy = -(player_y + cam_y + (doodler_h >> 1) - (h->y + ch));
                    ax = 0;
                    ay = 0;
                }
                continue;
            }
            if (vy < 0) {
                h->vy_dead = vy;
                vy = SPRING_VY;
                play_sfx(SND_STOMP, 1);
                clear_looping_sfx();
                continue;
            }
        }

        if (hy < ((WORLD_H << 2) - WORLD_H) >> 1 && hy > 0 && h->type != 4 && h->type != 20) {
            if (h->type == 6 && !ufo_snd_active) {
                play_sfx(SND_UFO_LOOP, 0);
                ufo_snd_active = 1;
            } else if (h->type != 6 && !monster_snd_active) {
                play_sfx(SND_MONSTER_LOOP, 0);
                monster_snd_active = 1;
            }
        }
    }

    for (int i = plat_head; i != plat_tail; i = (i + 1) % 100) {
        struct platform *pl = &plats[i];
        int32_t px = pl->x;
        int32_t py = pl->y - cam_y;
        uint32_t fl = pl->flags;
        int32_t ph = fp_div(22528, scale_y);
        if (ph <= PLAT_WORLD_H)
            ph = PLAT_WORLD_H;
        int32_t y_bot = py - ph;
        int32_t x0 = px - (PLAT_WORLD_W >> 1) - (doodler_w >> 1);
        int32_t x1 = px + (PLAT_WORLD_W >> 1) + (doodler_w >> 1);

        if ((fl & 8u) != 0) {
            int32_t sx0 = px + pl->item_dx - fp_div(12288, scale_x) - (doodler_w >> 1);
            int32_t sx1 = px + pl->item_dx + fp_div(12288, scale_x) + (doodler_w >> 1);
            int32_t sy1 = py + fp_div(16384, scale_y);
            if (vy < 0 && player_x >= sx0 && player_x <= sx1 && player_y >= py && player_y <= sy1) {
                player_y = py + 38912;
                vy = SPRING_VY;
                ay = -GRAVITY;
                pl->flags = (pl->flags & ~8u) | 0x40u;
                play_sfx(SND_SPRING, 1);
                return 1;
            }
        } else if ((fl & 0x10u) != 0 && (player_st & 0x77u) == 0) {
            int32_t sx0 = px + pl->item_dx - fp_div(17408, scale_x) - (doodler_w >> 1);
            int32_t sx1 = px + pl->item_dx + fp_div(17408, scale_x) + (doodler_w >> 1);
            int32_t sy0 = py - doodler_h;
            int32_t sy1 = py + fp_div(53248, scale_y);
            if (player_x >= sx0 && player_x <= sx1 && player_y >= sy0 && player_y <= sy1) {
                ++jet_spawned;
                vy = JUMP_VY;
                ay = 1253376;
                pl->flags &= ~0x10u;
                start_power(ST_PROP_RISE);
                play_sfx(SND_PROPELLER, 1);
                return 1;
            }
        } else if ((fl & 0x20u) != 0 && (player_st & 0x77u) == 0) {
            int32_t sx0 = px + pl->item_dx - fp_div(23552, scale_x) - (doodler_w >> 1);
            int32_t sx1 = px + pl->item_dx + fp_div(23552, scale_x) + (doodler_w >> 1);
            int32_t sy0 = py - doodler_h;
            int32_t sy1 = py + fp_div(40960, scale_y);
            if (player_x >= sx0 && player_x <= sx1 && player_y >= sy0 && player_y <= sy1) {
                pl->flags &= ~0x20u;
                start_power(ST_JET_RISE);
                if (vy > 0) {
                    ay = fp_div(1228800 - vy, 2730);
                } else {
                    vy = 245760;
                    ay = 737280;
                }
                play_sfx(SND_JETPACK, 1);
            }
        } else if ((fl & 0x80u) != 0) {
            int32_t sx0 = px + pl->item_dx - fp_div(26624, scale_x) - (doodler_w >> 1);
            int32_t sx1 = px + pl->item_dx + fp_div(26624, scale_x) + (doodler_w >> 1);
            int32_t sy0 = py - doodler_h;
            int32_t sy1 = py + fp_div(51200, scale_y);
            if (player_x >= sx0 && player_x <= sx1 && player_y >= sy0 && player_y <= sy1) {
                pl->flags &= ~0x80u;
                start_power(ST_SHIELD);
            }
        }

        if (fl <= 0x200004u && vy < 0 && player_x >= x0 && player_x <= x1 &&
            player_y >= y_bot && player_y <= py) {
            if (fl < 0x200000u) {
                player_y = py - fp_div(6144, scale_y);
                vy = JUMP_VY;
                ay = -GRAVITY;
                if (fl == 2u) {
                    pl->flags = 0x40000000u;
                    play_sfx(SND_WHITE_VANISH, 1);
                } else {
                    play_sfx(SND_JUMP, 1);
                }
                return 1;
            }
            uint32_t keep_move = pl->flags & 4u;
            pl->flags = 0x400000u | keep_move;
            pl->timer = (int32_t)game_time_ms;
            play_sfx(SND_BREAK, 1);
        }
    }
    return 0;
}

static void fire_bullet(void)
{
    if ((player_st & ST_NO_SHOOT_MASK) != 0 || bullet_count >= 10)
        return;

    int32_t bvx = 0;
    if (haz_head != haz_tail) {
        for (int i = haz_head; i != haz_tail; i = (i + 1) % 10) {
            const struct hazard *h = &hazards[i];
            if (h->type == 4 || h->type == 20)
                continue;
            int32_t hy = h->y - cam_y;
            if (hy > player_y && hy < WORLD_H) {
                int32_t denom = hy - player_y - doodler_h;
                if (denom != 0)
                    bvx = (h->x - player_x) * (2457600 / denom);
                break;
            }
        }
    }

    bullets[bullet_count].x = player_x;
    bullets[bullet_count].y = player_y + doodler_h;
    bullets[bullet_count].vx = bvx;
    bullets[bullet_count].vy = 2457600;
    ++bullet_count;
}

static void init_menu_doodler(void)
{
    menu_doodler_y = 0;
    menu_doodler_vy = JUMP_VY;
    menu_ufo_x = 532480;
    menu_ufo_y = 778240;
    menu_ufo_off_x = 0;
    menu_ufo_off_y = 0;
    menu_ufo_phase_x = 0;
    menu_ufo_phase_y = 0;
    menu_doodler_accum = 0;
}

static void step_menu_doodler(int32_t dt_fp)
{
    menu_doodler_accum += dt_fp;
    if (menu_doodler_accum < 34)
        return;

    while (menu_doodler_accum > 34) {
        if ((menu_doodler_vy += fp_mul(-GRAVITY, 34)) < MIN_VY)
            menu_doodler_vy = MIN_VY;
        menu_doodler_y += fp_mul(menu_doodler_vy, 34);
        int32_t py = 163840;
        int32_t py0 = py - PLAT_WORLD_H;
        if (menu_doodler_vy < 0 && menu_doodler_y >= py0 && menu_doodler_y <= py) {
            menu_doodler_y = py;
            menu_doodler_vy = JUMP_VY;
        }

        int32_t deg_x = (fp_div(fp_mul(menu_ufo_phase_x, 40), 12868) * 360) >> 11;
        if (deg_x < 0)
            deg_x = -deg_x;
        int32_t dx = 20 * (sin_lut[deg_x % 360] >> 3);
        menu_ufo_x += dx - menu_ufo_off_x;
        menu_ufo_off_x = dx;
        menu_ufo_phase_x += 2048;
        if (menu_ufo_phase_x >= 645120)
            menu_ufo_phase_x = 0;

        int32_t deg_y = (fp_div(fp_mul(menu_ufo_phase_y, 81), 12868) * 360) >> 11;
        if (deg_y < 0)
            deg_y = -deg_y;
        int32_t dy = 10 * (sin_lut[deg_y % 360] >> 3);
        menu_ufo_y += dy - menu_ufo_off_y;
        menu_ufo_off_y = dy;
        menu_ufo_phase_y += 2048;
        if (menu_ufo_phase_y >= 323584)
            menu_ufo_phase_y = 0;

        menu_doodler_accum -= 34;
    }
}

static void start_new_game(void)
{
    clear_looping_sfx();
    plat_head = 0;
    plat_tail = 0;
    haz_head = 0;
    haz_tail = 0;
    bullet_count = 0;
    bullet_hit_idx = -1;

    vx = 0;
    vy = JUMP_VY;
    ax = 0;
    ay = -GRAVITY;
    player_x = WORLD_W >> 1;
    player_y = 0;
    facing_left = 1;
    player_st = 0;
    state_timer = 0;
    shield_timer = 0;
    next_hat_y = (rand_pos() % 9000) + 1000;
    next_jet_y = (rand_pos() % 9500) + 2500;
    jet_spawned = 0;
    cam_y = 0;
    score = 0;
    paper_scroll = 0;
    phys_accum = 0;
    pat_low_mask = 0;
    pat_high_mask = 0;
    bg_scroll_stopped = 0;
    game_paused_midrun = 1;
    btn_prev_down = 0;
    btn_next_down = 0;
    btn_prev_until_ms = 0;
    btn_next_until_ms = 0;

    add_platform(327680, 61440, 1u, 0, 0);
    fill_platforms(1, WORLD_H);
}

static void build_menu(int scr)
{
    menu_count = 0;
    if (scr == SCR_MAIN_MENU) {
        if (game_paused_midrun)
            menu_items[menu_count++] = MI_CONTINUE;
        menu_items[menu_count++] = MI_PLAY;
        menu_items[menu_count++] = MI_HIGH_SCORES;
        menu_items[menu_count++] = MI_OPTIONS;
        menu_items[menu_count++] = MI_HELP;
        menu_items[menu_count++] = MI_ABOUT;
        menu_items[menu_count++] = MI_EXIT;
    } else if (scr == SCR_PAUSE) {
        menu_items[menu_count++] = MI_PAUSE_RESUME;
        menu_items[menu_count++] = MI_PAUSE_SOUND;
        menu_items[menu_count++] = MI_PAUSE_OPTIONS;
        menu_items[menu_count++] = MI_PAUSE_MAIN_MENU;
    } else if (scr == SCR_OPTIONS) {
        menu_items[menu_count++] = MI_OPT_SOUND;
        menu_items[menu_count++] = MI_OPT_MODE;
        menu_items[menu_count++] = MI_OPT_TUTORIAL;
        menu_items[menu_count++] = MI_OPT_RESET;
        menu_items[menu_count++] = MI_OPT_BACK;
    } else if (scr == SCR_GAME_OVER) {
        menu_items[menu_count++] = MI_OVER_AGAIN;
        menu_items[menu_count++] = MI_OVER_MENU;
    } else if (scr == SCR_CONFIRM_RESET || scr == SCR_CONFIRM_EXIT) {
        menu_items[menu_count++] = MI_CONFIRM_YES;
        menu_items[menu_count++] = MI_CONFIRM_NO;
    }
    menu_sel = 0;
    menu_scroll_top = 0;
}

static void enter_screen(int scr)
{
    screen = scr;
    text_scroll_y = 0;
    text_max_scroll = 0;
    wheel_menu_accum = 0;
    btn_prev_down = 0;
    btn_next_down = 0;
    btn_prev_until_ms = 0;
    btn_next_until_ms = 0;
    if (scr == SCR_MAIN_MENU)
        init_menu_doodler();
    if (scr == SCR_HIGH_SCORES)
        hs_view_mode = save.full_mode ? 1 : 0;
    build_menu(scr);
}

static void activate_menu_item(int id)
{
    switch (id) {
    case MI_CONTINUE:
        btn_prev_down = 0;
        btn_next_down = 0;
        btn_prev_until_ms = 0;
        btn_next_until_ms = 0;
        screen = SCR_PLAYING;
        prev_update_ms = eapp_ticks_ms();
        break;
    case MI_PLAY:
        if (save.tutorial_on) {
            tutorial_step = 0;
            screen = SCR_TUTORIAL;
        } else {
            start_new_game();
            screen = SCR_PLAYING;
            prev_update_ms = eapp_ticks_ms();
        }
        break;
    case MI_GAME_MODE:
    case MI_OPT_MODE:
        save.full_mode = !save.full_mode;
        save_persist();
        play_sfx(SND_JUMP, 1);
        break;
    case MI_HIGH_SCORES:
        enter_screen(SCR_HIGH_SCORES);
        break;
    case MI_OPTIONS:
        prev_screen_before_options = SCR_MAIN_MENU;
        enter_screen(SCR_OPTIONS);
        break;
    case MI_PAUSE_OPTIONS:
        prev_screen_before_options = SCR_PAUSE;
        enter_screen(SCR_OPTIONS);
        break;
    case MI_HELP:
        enter_screen(SCR_HELP);
        break;
    case MI_ABOUT:
        enter_screen(SCR_ABOUT);
        break;
    case MI_EXIT:
        enter_screen(SCR_CONFIRM_EXIT);
        break;
    case MI_PAUSE_RESUME:
        btn_prev_down = 0;
        btn_next_down = 0;
        btn_prev_until_ms = 0;
        btn_next_until_ms = 0;
        screen = SCR_PLAYING;
        prev_update_ms = eapp_ticks_ms();
        break;
    case MI_PAUSE_SOUND:
    case MI_OPT_SOUND:
        save.sound_on = !save.sound_on;
        if (!save.sound_on)
            stop_all_sfx();
        else
            play_sfx(SND_JUMP, 1);
        save_persist();
        break;
    case MI_OPT_TUTORIAL:
        save.tutorial_on = !save.tutorial_on;
        save_persist();
        play_sfx(SND_JUMP, 1);
        break;
    case MI_OPT_RESET:
        enter_screen(SCR_CONFIRM_RESET);
        break;
    case MI_OPT_BACK:
        enter_screen(prev_screen_before_options);
        break;
    case MI_PAUSE_MAIN_MENU:
        clear_looping_sfx();
        game_paused_midrun = ((player_st & (ST_FALL_OUT | ST_DEAD_WAIT | ST_UFO_DONE | ST_BH_DONE)) == 0) ? 1 : 0;
        enter_screen(SCR_MAIN_MENU);
        break;
    case MI_OVER_AGAIN:
        start_new_game();
        screen = SCR_PLAYING;
        prev_update_ms = eapp_ticks_ms();
        break;
    case MI_OVER_MENU:
        game_paused_midrun = 0;
        enter_screen(SCR_MAIN_MENU);
        break;
    case MI_CONFIRM_YES:
        if (screen == SCR_CONFIRM_EXIT) {
            eapp_request_exit();
        } else if (screen == SCR_CONFIRM_RESET) {
            reset_high_scores_mem();
            save_persist();
            enter_screen(SCR_OPTIONS);
        }
        break;
    case MI_CONFIRM_NO:
        if (screen == SCR_CONFIRM_EXIT)
            enter_screen(SCR_MAIN_MENU);
        else
            enter_screen(SCR_OPTIONS);
        break;
    default:
        break;
    }
}

/* ---------- Public Lifecycle & Input ---------- */

int doodle_init(void)
{
    uint32_t i_sz = 0, p_sz = 0, l_sz = 0, b1_sz = 0;
    uint8_t *i_raw = load_pkg_file("i", &i_sz);
    uint8_t *p_raw = load_pkg_file("p", &p_sz);
    uint8_t *l_raw = load_pkg_file("l", &l_sz);
    uint8_t *b1_raw = load_pkg_file("b1", &b1_sz);
    if (!i_raw || !p_raw || !l_raw || !b1_raw || i_sz < 12360)
        return -1;

    d_arr = (int8_t *)i_raw;
    a_arr = eapp_alloc(3893 * sizeof(int16_t));
    e_arr = eapp_alloc(169 * sizeof(int32_t));
    if (!a_arr || !e_arr)
        return -1;

    for (int k = 0; k < 3893; ++k)
        a_arr[k] = (int16_t)be16(i_raw + 3898 + k * 2);
    for (int k = 0; k < 169; ++k)
        e_arr[k] = (int32_t)be32(i_raw + 3898 + 3893 * 2 + k * 4);

    /* Decode palettes from "p" (big-endian RGB565 entries). */
    uint8_t num_pals = p_raw[0];
    uint32_t pp = 1;
    for (int pi = 0; pi < num_pals && pi < 8 && pp < p_sz; ++pi) {
        uint8_t cnt = p_raw[pp++];
        pals[pi][0] = 0;
        for (int ci = 1; ci < cnt && pp + 2 <= p_sz; ++ci) {
            pals[pi][ci] = be16(p_raw + pp);
            pp += 2;
        }
    }
    eapp_free(p_raw);

    /* Load all 4 sprite banks into SDRAM heap */
    for (int gi = 0; gi < 4; ++gi) {
        if (load_sprites_pack(gi) < 0)
            return -1;
    }

    /* Decode 72 strings from "l" */
    str_blob = eapp_alloc(l_sz + MAX_STRINGS);
    if (!str_blob)
        return -1;
    uint32_t loff = 0, bout = 0;
    for (int si = 0; si < MAX_STRINGS && loff + 2 <= l_sz; ++si) {
        uint16_t sl = be16(l_raw + loff);
        loff += 2;
        if (loff + sl > l_sz)
            sl = (uint16_t)(l_sz - loff);
        strings[si] = str_blob + bout;
        memcpy(str_blob + bout, l_raw + loff, sl);
        bout += sl;
        str_blob[bout++] = 0;
        loff += sl;
    }
    eapp_free(l_raw);
    strings[33] = (char *)"Guide the Doodler with Left and Right buttons. Jump on a \x19"
                          "1: to climb higher and higher. Press Center button to shoot. "
                          "Above all, avoid these: \x19"
                          "2: \x19"
                          "3:";
    {
        static const char prefix[] = "Doodle Jump\nVersion 1.3.0\n";
        static const char credit[] = "Ported by 0xAu\n";
        const char *orig = strings[35] ? strings[35] : "";
        size_t plen = sizeof(prefix) - 1;
        size_t clen = sizeof(credit) - 1;
        size_t olen = str_len(orig);
        int has_prefix = 1;
        for (size_t k = 0; k < plen; ++k) {
            if (orig[k] != prefix[k]) {
                has_prefix = 0;
                break;
            }
        }
        if (has_prefix && plen + clen + (olen - plen) < sizeof(about_text)) {
            memcpy(about_text, prefix, plen);
            memcpy(about_text + plen, credit, clen);
            memcpy(about_text + plen + clen, orig + plen, olen - plen + 1);
        } else if (clen + 1 + olen < sizeof(about_text)) {
            memcpy(about_text, credit, clen);
            about_text[clen] = '\n';
            memcpy(about_text + clen + 1, orig, olen + 1);
        }
        strings[35] = about_text;
    }
    strings[65] = (char *)"Use Left and Right buttons to move";
    strings[67] = (char *)"Press Center button to shoot";

    /* Decode monster animation & 28 spawn pattern tables from b1 */
    uint32_t anim_off = (uint32_t)e_arr[80];
    uint32_t anim_len = (uint32_t)e_arr[81];
    if (anim_off + anim_len <= b1_sz && anim_len <= sizeof(monster_anim))
        memcpy(monster_anim, b1_raw + anim_off, anim_len);

    int16_t total_shorts = 0;
    for (int k = 0; k < 7; ++k) {
        pat_low_start[k] = total_shorts;
        total_shorts += (int16_t)(e_arr[73 + (k + 3) * 3 + 2] >> 1);
    }
    pat_low_start[7] = total_shorts;
    for (int k = 0; k < 22; ++k) {
        pat_high_start[k] = total_shorts;
        total_shorts += (int16_t)(e_arr[73 + (k + 10) * 3 + 2] >> 1);
    }
    pat_high_start[22] = total_shorts;
    pat_data = eapp_alloc((size_t)total_shorts * sizeof(int16_t));
    if (!pat_data)
        return -1;
    for (int k = 0; k < 7; ++k) {
        int off = e_arr[73 + (k + 3) * 3 + 1];
        int bytes = e_arr[73 + (k + 3) * 3 + 2];
        for (int b = 0; b < bytes; b += 2)
            pat_data[pat_low_start[k] + (b >> 1)] = (int16_t)be16(b1_raw + off + b);
    }
    for (int k = 0; k < 22; ++k) {
        int off = e_arr[73 + (k + 10) * 3 + 1];
        int bytes = e_arr[73 + (k + 10) * 3 + 2];
        for (int b = 0; b < bytes; b += 2)
            pat_data[pat_high_start[k] + (b >> 1)] = (int16_t)be16(b1_raw + off + b);
    }
    eapp_free(b1_raw);

    /* Load WAV SFX files s00.wav .. s14.wav */
    char wav_name[12] = "s00.wav";
    for (int k = 0; k < MAX_SOUNDS; ++k) {
        wav_name[1] = (char)('0' + (k / 10));
        wav_name[2] = (char)('0' + (k % 10));
        sounds[k] = eapp_sound_load(wav_name);
    }

    init_sin_lut();
    load_persist();

    /* Compute 320x240 layout scalers (af() with var_int_f=320, var_int_g=240) */
    scale_x = fp_div(DOODLE_SCR_W << 11, WORLD_W);
    scale_y = fp_div(DOODLE_SCR_H << 11, WORLD_H);
    plat_px_w = fp_mul(scale_x, PLAT_WORLD_W);
    doodler_w = fp_div(40960, scale_x);
    doodler_h = fp_div(67584, scale_y);
    min_step_px = (30720 + (PLAT_WORLD_H >> 1)) >> 11;

    reset_clip();
    start_ms = eapp_ticks_ms();
    prev_update_ms = start_ms;
    splash_start_ms = start_ms;
    enter_screen(SCR_SPLASH);
    return 0;
}

void doodle_shutdown(void)
{
    stop_all_sfx();
    for (int k = 0; k < MAX_SOUNDS; ++k)
        eapp_sound_free(sounds[k]);
}

static void move_menu(int dir)
{
    if (menu_count <= 0)
        return;
    int next = menu_sel + dir;
    if (next < 0)
        next = 0;
    if (next >= menu_count)
        next = menu_count - 1;
    menu_sel = next;
    if (menu_sel < menu_scroll_top)
        menu_scroll_top = menu_sel;
    else if (menu_sel >= menu_scroll_top + 4)
        menu_scroll_top = menu_sel - 3;
}

void doodle_on_button(uint8_t button, uint8_t type)
{
    if (button == EAPP_BUTTON_PREVIOUS) {
        btn_prev_down = (type == EAPP_INPUT_PRESS) ? 1 : 0;
        if (type == EAPP_INPUT_PRESS && screen == SCR_PLAYING) {
            btn_prev_until_ms = eapp_ticks_ms() + 35u;
            btn_next_until_ms = 0;
            btn_next_down = 0;
            if ((player_st & (ST_LOCK_MOVE_MASK | ST_FREEZE_INPUT_MASK)) == 0) {
                if (vx > -(MAX_VX >> 3))
                    vx = -(MAX_VX >> 3);
                facing_left = 1;
            }
        }
    }
    if (button == EAPP_BUTTON_NEXT) {
        btn_next_down = (type == EAPP_INPUT_PRESS) ? 1 : 0;
        if (type == EAPP_INPUT_PRESS && screen == SCR_PLAYING) {
            btn_next_until_ms = eapp_ticks_ms() + 35u;
            btn_prev_until_ms = 0;
            btn_prev_down = 0;
            if ((player_st & (ST_LOCK_MOVE_MASK | ST_FREEZE_INPUT_MASK)) == 0) {
                if (vx < (MAX_VX >> 3))
                    vx = (MAX_VX >> 3);
                facing_left = 0;
            }
        }
    }

    if (type != EAPP_INPUT_PRESS)
        return;

    if (screen == SCR_SPLASH) {
        enter_screen(SCR_MAIN_MENU);
        return;
    }

    if (screen == SCR_TUTORIAL) {
        if (button == EAPP_BUTTON_CENTER || button == EAPP_BUTTON_NEXT) {
            if (tutorial_step < 3) {
                ++tutorial_step;
            } else {
                save.tutorial_on = 0;
                save_persist();
                start_new_game();
                screen = SCR_PLAYING;
                prev_update_ms = eapp_ticks_ms();
            }
        } else if (button == EAPP_BUTTON_PREVIOUS && tutorial_step > 0) {
            --tutorial_step;
        } else if (button == EAPP_BUTTON_MENU) {
            enter_screen(SCR_MAIN_MENU);
        }
        return;
    }

    if (screen == SCR_PLAYING) {
        if (button == EAPP_BUTTON_CENTER) {
            fire_bullet();
        } else if (button == EAPP_BUTTON_MENU || button == EAPP_BUTTON_PLAY) {
            clear_looping_sfx();
            enter_screen(SCR_PAUSE);
        }
        return;
    }

    if (screen == SCR_PAUSE) {
        if (button == EAPP_BUTTON_MENU || button == EAPP_BUTTON_PLAY) {
            screen = SCR_PLAYING;
            prev_update_ms = eapp_ticks_ms();
        } else if (button == EAPP_BUTTON_PREVIOUS) {
            move_menu(-1);
        } else if (button == EAPP_BUTTON_NEXT) {
            move_menu(1);
        } else if (button == EAPP_BUTTON_CENTER) {
            activate_menu_item(menu_items[menu_sel]);
        }
        return;
    }

    if (screen == SCR_HIGH_SCORES) {
        if (button == EAPP_BUTTON_MENU) {
            enter_screen(SCR_MAIN_MENU);
        } else if (button == EAPP_BUTTON_PREVIOUS || button == EAPP_BUTTON_NEXT ||
                   button == EAPP_BUTTON_CENTER) {
            hs_view_mode = !hs_view_mode;
        }
        return;
    }

    if (screen == SCR_HELP || screen == SCR_ABOUT) {
        if (button == EAPP_BUTTON_MENU || button == EAPP_BUTTON_CENTER) {
            enter_screen(SCR_MAIN_MENU);
        } else if (button == EAPP_BUTTON_NEXT) {
            text_scroll_y += 28;
            if (text_scroll_y > text_max_scroll)
                text_scroll_y = text_max_scroll;
        } else if (button == EAPP_BUTTON_PREVIOUS) {
            text_scroll_y -= 28;
            if (text_scroll_y < 0)
                text_scroll_y = 0;
        }
        return;
    }

    /* Standard list menus: Main Menu, Options, Game Over, Confirm dialogs */
    if (button == EAPP_BUTTON_MENU) {
        if (screen == SCR_MAIN_MENU)
            enter_screen(SCR_CONFIRM_EXIT);
        else if (screen == SCR_OPTIONS)
            enter_screen(prev_screen_before_options);
        else if (screen == SCR_CONFIRM_RESET)
            enter_screen(SCR_OPTIONS);
        else
            enter_screen(SCR_MAIN_MENU);
    } else if (button == EAPP_BUTTON_PREVIOUS) {
        move_menu(-1);
    } else if (button == EAPP_BUTTON_NEXT) {
        move_menu(1);
    } else if (button == EAPP_BUTTON_CENTER) {
        if (menu_count > 0)
            activate_menu_item(menu_items[menu_sel]);
    } else if (button == EAPP_BUTTON_PLAY) {
        if (screen == SCR_MAIN_MENU && game_paused_midrun)
            activate_menu_item(MI_CONTINUE);
    }
}

void doodle_on_wheel(const struct eapp_wheel *wheel)
{
    if (screen == SCR_PLAYING || wheel->delta == 0)
        return;

    if (screen == SCR_HELP || screen == SCR_ABOUT) {
        text_scroll_y += wheel->delta * 14;
        if (text_scroll_y < 0)
            text_scroll_y = 0;
        if (text_scroll_y > text_max_scroll)
            text_scroll_y = text_max_scroll;
        return;
    }

    if (screen == SCR_MAIN_MENU || screen == SCR_PAUSE || screen == SCR_OPTIONS ||
        screen == SCR_GAME_OVER || screen == SCR_CONFIRM_RESET || screen == SCR_CONFIRM_EXIT) {
        wheel_menu_accum += wheel->delta;
        if (wheel_menu_accum >= 2) {
            move_menu(1);
            wheel_menu_accum = 0;
        } else if (wheel_menu_accum <= -2) {
            move_menu(-1);
            wheel_menu_accum = 0;
        }
    }
}

void doodle_update(uint32_t now_ms)
{
    delta_ms = now_ms - prev_update_ms;
    if (delta_ms > 100u)
        delta_ms = 100u;
    prev_update_ms = now_ms;
    game_time_ms += delta_ms;
    ufo_hover_t = (int)now_ms;

    if (screen == SCR_SPLASH) {
        if ((now_ms - splash_start_ms) > 1800u)
            enter_screen(SCR_MAIN_MENU);
        return;
    }

    if (screen == SCR_MAIN_MENU) {
        int32_t dt_fp = (int32_t)((delta_ms << 11) / 1000u);
        if (dt_fp > 204)
            dt_fp = 204;
        step_menu_doodler(dt_fp);
        return;
    }

    if (screen != SCR_PLAYING)
        return;

    spawn_step();

    int32_t dt_fp = (int32_t)((delta_ms << 11) / 1000u);
    if (dt_fp > 204)
        dt_fp = 204;
    phys_accum += dt_fp;
    if (phys_accum < 34)
        return;

    int move_left = btn_prev_down || ((int32_t)(btn_prev_until_ms - now_ms) > 0);
    int move_right = btn_next_down || ((int32_t)(btn_next_until_ms - now_ms) > 0);

    int32_t h_step = phys_accum;
    int apply_friction = 0;
    if ((player_st & ST_LOCK_MOVE_MASK) == 0) {
        if (move_left && !move_right) {
            ax = (vx > 0) ? -DECEL_X : -ACCEL_X;
        } else if (move_right && !move_left) {
            ax = (vx < 0) ? DECEL_X : ACCEL_X;
        } else {
            ax = (vx > 0) ? -DECEL_X : (vx < 0 ? DECEL_X : 0);
            apply_friction = 1;
        }
    }
    if ((player_st & ST_FREEZE_INPUT_MASK) != 0) {
        ax = (vx > 0) ? -DECEL_X : (vx < 0 ? DECEL_X : 0);
        apply_friction = 1;
    }

    int32_t prev_vx = vx;
    vx += fp_mul(ax, h_step);
    if (apply_friction && ((prev_vx > 0 && vx < 0) || (prev_vx < 0 && vx > 0)))
        vx = 0;
    if (vx > MAX_VX)
        vx = MAX_VX;
    else if (vx < -MAX_VX)
        vx = -MAX_VX;

    player_x += fp_mul(vx, h_step);
    if (player_x < 0)
        player_x = WORLD_W;
    else if (player_x > WORLD_W)
        player_x = 0;

    /* Update moving, breaking, and timed bomb platforms */
    for (int i = plat_head; i != plat_tail; i = (i + 1) % 100) {
        struct platform *pl = &plats[i];
        if (pl->flags >= 0x400000u && pl->flags < 0x1000000u &&
            (int32_t)game_time_ms - pl->timer > 30) {
            while ((int32_t)game_time_ms - pl->timer > 30 && pl->flags < 0x1000000u) {
                uint32_t mv = pl->flags & 4u;
                pl->flags = ((pl->flags & ~4u) << 1) | mv;
                pl->timer += 30;
            }
        } else if ((pl->flags & 0x1000000u) == 0x1000000u) {
            pl->y -= fp_mul(VANISH_SPEED, h_step);
        }

        if ((pl->flags & 4u) == 4u) {
            pl->x += fp_mul(pl->vx, h_step);
            if (pl->x - (PLAT_WORLD_W >> 1) < 0)
                pl->vx = pl->vx < 0 ? -pl->vx : pl->vx;
            else if (pl->x + (PLAT_WORLD_W >> 1) > WORLD_W)
                pl->vx = pl->vx > 0 ? -pl->vx : pl->vx;
        }

        if ((pl->flags & 0x100u) == 256u) {
            if (pl->item_dx == 0 && pl->y < cam_y + 307200) {
                pl->flags = 512u;
                pl->timer = (int32_t)game_time_ms;
            } else if (pl->timer == 0 && pl->item_dx > 0 && pl->y - cam_y < WORLD_H) {
                pl->timer = (int32_t)game_time_ms;
            } else if (pl->timer > 0 && pl->item_dx > 0 &&
                       (int32_t)game_time_ms - pl->timer > pl->item_dx) {
                pl->flags = 512u;
                pl->timer = (int32_t)game_time_ms;
            }
        } else if ((pl->flags & 0x200u) && (int32_t)game_time_ms - pl->timer > 150) {
            pl->flags = 1024u;
            pl->timer = (int32_t)game_time_ms;
        } else if ((pl->flags & 0x400u) && (int32_t)game_time_ms - pl->timer > 150) {
            pl->flags = 2048u;
            pl->timer = (int32_t)game_time_ms;
        } else if ((pl->flags & 0x800u) && (int32_t)game_time_ms - pl->timer > 150) {
            pl->flags = 4096u;
            pl->timer = (int32_t)game_time_ms;
        } else if ((pl->flags & 0x1000u) && (int32_t)game_time_ms - pl->timer > 450) {
            pl->flags = 8192u;
            pl->timer = (int32_t)game_time_ms;
        } else if ((pl->flags & 0x2000u) && (int32_t)game_time_ms - pl->timer > 100) {
            pl->flags = 16384u;
            pl->timer = (int32_t)game_time_ms;
        } else if ((pl->flags & 0x4000u) && (int32_t)game_time_ms - pl->timer > 100) {
            pl->flags = 32768u;
            pl->timer = (int32_t)game_time_ms;
        } else if ((pl->flags & 0x8000u) && (int32_t)game_time_ms - pl->timer > 100) {
            pl->flags = 0x40000000u;
        }
    }

    int stop_sub = 0;
    while (phys_accum > 34 && !stop_sub) {
        step_player(34);
        if (screen != SCR_PLAYING)
            return;
        if ((player_st & (ST_FALL_OUT | ST_UFO_BEAM | ST_UFO_DONE | ST_BH_SUCK | ST_BH_DONE |
                          ST_DEAD_WAIT)) == 0) {
            stop_sub = check_collisions();
        }
        state_timer += 34;
        if ((player_st & (ST_SHIELD | ST_SHIELD_BLINK)) != 0)
            shield_timer += 34;
        phys_accum -= 34;
        if (haz_head == haz_tail)
            clear_looping_sfx();
    }

    spawn_step();
    trim_offscreen();
}

/* ---------- Scene Rendering ---------- */

static void draw_paper_bg(const struct eapp_framebuffer *fb, int with_bottom_tear)
{
    int scroll = 0;
    if (screen == SCR_PLAYING && (player_st & (ST_FALL_OUT | ST_DEAD_WAIT)) == 0)
        scroll = (int)(((cam_y >> 11) >> 1) % 69);
    else if (screen == SCR_PLAYING && !bg_scroll_stopped && (player_st & ST_FALL_OUT))
        scroll = paper_scroll;

    int y0 = scroll - 69;
    tile_sprite(fb, 2, 0, y0, 19, DOODLE_SCR_H + 138);
    tile_sprite(fb, 0, 19, y0, DOODLE_SCR_W - 19, DOODLE_SCR_H + 138);

    if (with_bottom_tear && (screen != SCR_PLAYING || bg_scroll_stopped)) {
        int n = 18;
        for (int x = 0; x < DOODLE_SCR_W; x += 38) {
            draw_sprite(fb, n, x, DOODLE_SCR_H, ANC_BOTTOM_LEFT, 0);
            if (++n > 20)
                n = 18;
        }
    }
}

static void draw_platforms(const struct eapp_framebuffer *fb)
{
    int pw = plat_px_w >> 11;
    int bw_rem = pw - 12 - 11 - 10;
    int adj = 0;
    if (bw_rem < 0) {
        adj = bw_rem;
        bw_rem = 0;
    }
    bw_rem -= bw_rem >> 1;

    for (int i = plat_head; i != plat_tail; i = (i + 1) % 100) {
        const struct platform *pl = &plats[i];
        uint32_t c = pl->flags;
        int px_c = to_sx(pl->x);
        int px_l = px_c - (pw >> 1);
        int py = to_sy(pl->y - cam_y);
        if (py < -40 || py > DOODLE_SCR_H + 40)
            continue;

        if ((c & 0xFF03u) != 0) {
            int s0 = 22, s1 = 23, s2 = 24;
            if ((c & 5u) == 5u) {
                s0 = 754; s1 = 755; s2 = 756;
            } else if ((c & 2u) == 2u) {
                s0 = 757; s1 = 758; s2 = 759;
            } else if (c & 0x100u) {
                s0 = 760; s1 = 761; s2 = 762;
            } else if (c & 0x200u) {
                s0 = 763; s1 = 764; s2 = 765;
            } else if (c & 0x400u) {
                s0 = 766; s1 = 767; s2 = 768;
            } else if (c & 0x800u) {
                s0 = 769; s1 = 770; s2 = 771;
            } else if (c & 0x1000u) {
                s0 = 772; s1 = 773; s2 = 774;
            } else if (c & 0x2000u) {
                s0 = 775; s1 = 776; s2 = 777;
            } else if (c & 0x4000u) {
                s0 = 778; s1 = 779; s2 = 780;
            } else if (c & 0x8000u) {
                s0 = 781; s1 = 782; s2 = 783;
            }
            int w0 = spr_w(s0);
            int w2 = spr_w(s2);
            int mid = pw - w0 - w2;
            int m_adj = 0;
            if (mid < 0) {
                m_adj = mid;
                mid = 0;
            }
            draw_sprite(fb, s0, px_l, py, ANC_TOP_LEFT, 0);
            stretch_h_sprite(fb, s1, px_l + w0, py, mid);
            draw_sprite(fb, s2, px_l + w0 + mid + m_adj, py, ANC_TOP_LEFT, 0);
        } else if (c & 0x400000u) {
            draw_sprite(fb, 748, px_l, py, ANC_TOP_LEFT, 0);
            draw_sprite(fb, 749, px_c + (pw >> 1) + adj, py, ANC_TOP_RIGHT, 0);
        } else if (c & 0x800000u) {
            draw_sprite(fb, 750, px_l, py, ANC_TOP_LEFT, 0);
            draw_sprite(fb, 751, px_c + (pw >> 1) + adj, py, ANC_TOP_RIGHT, 0);
        } else if (c & 0x1000000u) {
            draw_sprite(fb, 752, px_l, py, ANC_TOP_LEFT, 0);
            draw_sprite(fb, 753, px_c + (pw >> 1) + adj, py, ANC_TOP_RIGHT, 0);
        } else if ((c & 0x200000u) == 0x200000u) {
            draw_sprite(fb, 743, px_l, py, ANC_TOP_LEFT, 0);
            int px = px_l + 12;
            stretch_h_sprite(fb, 744, px, py, bw_rem);
            px += bw_rem;
            draw_sprite(fb, 745, px, py, ANC_TOP_LEFT, 0);
            px += 10;
            stretch_h_sprite(fb, 746, px, py, bw_rem);
            px += bw_rem;
            draw_sprite(fb, 747, px + adj, py, ANC_TOP_LEFT, 0);
        }
    }

    /* Draw power-ups attached to platforms (reverse pass so newest sit in front) */
    if (plat_head != plat_tail) {
        int i = (plat_tail + 99) % 100;
        for (;;) {
            const struct platform *pl = &plats[i];
            uint32_t c = pl->flags;
            if (c & (0x08u | 0x10u | 0x20u | 0x40u | 0x80u)) {
                int ix = to_sx(pl->x + pl->item_dx);
                int iy = to_sy(pl->y - cam_y);
                if (c & 0x10u)
                    draw_sprite(fb, 786, ix, iy, ANC_BOTTOM_HCENTER, 0);
                if (c & 0x20u)
                    draw_sprite(fb, 796, ix, iy, ANC_BOTTOM_HCENTER, 0);
                if (c & 0x08u)
                    draw_sprite(fb, 784, ix, iy, ANC_BOTTOM_HCENTER, 0);
                else if (c & 0x40u)
                    draw_sprite(fb, 785, ix, iy, ANC_BOTTOM_HCENTER, 0);
                if (c & 0x80u)
                    draw_sprite(fb, 800, ix, iy, ANC_BOTTOM_HCENTER, 0);
            }
            if (i == plat_head)
                break;
            i = (i + 99) % 100;
        }
    }
}

static void draw_hazards_bg(const struct eapp_framebuffer *fb)
{
    for (int i = haz_head; i != haz_tail; i = (i + 1) % 10) {
        if (hazards[i].type == 4) {
            draw_sprite(fb, 25, to_sx(hazards[i].x), to_sy(hazards[i].y - cam_y), ANC_CENTER, 0);
        }
    }
}

static void draw_hazards_fg(const struct eapp_framebuffer *fb)
{
    for (int i = haz_head; i != haz_tail; i = (i + 1) % 10) {
        const struct hazard *h = &hazards[i];
        int sx = to_sx(h->x);
        int sy = to_sy(h->y - cam_y);
        switch (h->type) {
        case 9:
            draw_sprite(fb, 26, sx, sy, ANC_CENTER, h->phase_x > 0 ? 0 : XFORM_FLIP_X);
            break;
        case 10: {
            int f = monster_anim[(ufo_hover_t >> 5) % 9];
            draw_sprite(fb, 804 + f, sx, sy, ANC_CENTER, 0);
            break;
        }
        case 11:
            draw_sprite(fb, 809, sx, sy, ANC_CENTER, 0);
            break;
        case 21:
            draw_sprite(fb, 812 + ((ufo_hover_t >> 6) % 6), sx, sy, ANC_CENTER, 0);
            break;
        case 22:
            draw_sprite(fb, 809 - ((h->hit_timer >> 6) & 1), sx, sy, ANC_CENTER, 0);
            break;
        case 8:
            draw_sprite(fb, 819, sx, sy, ANC_CENTER, 0);
            break;
        case 7:
            draw_sprite(fb, 820, sx, sy, ANC_CENTER, 0);
            break;
        case 6:
            if ((rand_pos() % 3) == 1)
                draw_sprite(fb, 32, sx, sy - 32, ANC_CENTER, 0);
            else
                draw_sprite(fb, 31, sx, sy, ANC_CENTER, 0);
            break;
        default:
            break;
        }
    }
}

static void draw_doodler(const struct eapp_framebuffer *fb)
{
    if (screen == SCR_PLAYING) {
        if (vx < 0)
            facing_left = 1;
        else if (vx > 0)
            facing_left = 0;
    }

    int py = to_sy(player_y);
    if (player_y < 0 && (player_st & ST_FALL_OUT)) {
        py = (-player_y > CAM_THRESH) ? to_sy(CAM_THRESH) : to_sy(-player_y);
    }

    int base_spr = 27;
    if (player_st & ST_BH_SUCK) {
        base_spr = 27 + state_timer / 1024;
        if (base_spr > 30)
            base_spr = 30;
        py -= (33 - spr_h(base_spr)) >> 1;
    } else if (player_st & ST_UFO_BEAM) {
        base_spr = 27 + state_timer / 614;
        if (base_spr > 30)
            base_spr = 30;
        py -= (33 - spr_h(base_spr)) >> 1;
    }

    int head_y = py;

    if (player_st & ST_SHIELD) {
        draw_sprite(fb, 801 + (rand_pos() % 2), to_sx(player_x), py - 16, ANC_CENTER, 0);
    } else if (player_st & ST_SHIELD_BLINK) {
        int s = 801 + (rand_pos() % 4);
        if (s <= 803)
            draw_sprite(fb, s, to_sx(player_x), py - 16, ANC_CENTER, 0);
    }

    int dx;
    if (screen == SCR_PLAYING && (bullet_hit_idx != -1 || bullet_count > 0)) {
        dx = to_sx(player_x);
        if (vy > LEGS_VY) {
            draw_sprite(fb, 737, dx, py - 2, ANC_BOTTOM_HCENTER, 0);
            py -= 2;
        } else {
            draw_sprite(fb, 738, dx, py, ANC_BOTTOM_HCENTER, 0);
        }
        draw_sprite(fb, 739, dx, head_y - 28, ANC_BOTTOM_HCENTER, 0);
        head_y -= 28;
    } else {
        int xf = 0;
        if (facing_left) {
            dx = to_sx(player_x) - 7;
        } else {
            dx = to_sx(player_x) + 7;
            xf = XFORM_FLIP_X;
        }
        if (vy > LEGS_VY)
            draw_sprite(fb, 30, dx, py - 3, ANC_BOTTOM_HCENTER, xf);
        else
            draw_sprite(fb, base_spr, dx, py, ANC_BOTTOM_HCENTER, xf);
        head_y -= 33;
    }

    if (player_st & ST_STUNNED) {
        int star = ((ufo_hover_t >> 6) % 2) + 740;
        draw_sprite(fb, star, dx + (facing_left ? 7 : -7), head_y, ANC_CENTER,
                    facing_left ? 0 : XFORM_FLIP_X);
    }

    if (player_st & ST_PROP_MASK) {
        int px = dx + (facing_left ? 21 : -21);
        int xf = facing_left ? 0 : XFORM_FLIP_X;
        int hat_y = to_sy(player_y) + 4 - 33;
        int sp;
        if ((player_st & ST_PROP_RISE) && state_timer > 61)
            sp = 787 + (ufo_hover_t % 3);
        else if ((player_st & ST_PROP_END) && state_timer > 102)
            sp = 793 + (ufo_hover_t % 3);
        else
            sp = 790 + (ufo_hover_t % 3);
        draw_sprite(fb, sp, px, hat_y, ANC_TOP_HCENTER, xf);
    }

    if (player_st & ST_PROP_DROP) {
        draw_sprite(fb, 795, to_sx(deb_x), to_sy(deb_y), ANC_TOP_HCENTER,
                    ufo_facing_left ? 0 : XFORM_FLIP_X);
    }

    if (player_st & ST_JET_MASK) {
        int fy = head_y + 7;
        int fx = dx + (facing_left ? 6 : -6);
        draw_sprite(fb, 797 + (ufo_hover_t % 3), fx, fy, ANC_BOTTOM_HCENTER,
                    facing_left ? 0 : XFORM_FLIP_X);
    }

    if (player_st & ST_JET_DROP)
        draw_sprite(fb, 796, to_sx(deb_x), to_sy(deb_y), ANC_BOTTOM_HCENTER, 0);
}

static void draw_bullets(const struct eapp_framebuffer *fb)
{
    for (int i = 0; i < bullet_count; ++i)
        draw_sprite(fb, 821, to_sx(bullets[i].x), to_sy(bullets[i].y), ANC_CENTER, 0);
    if (bullet_hit_idx != -1) {
        for (int i = bullet_hit_idx; i < bullet_count - 1; ++i)
            bullets[i] = bullets[i + 1];
        --bullet_count;
        bullet_hit_idx = -1;
        if (last_hit_haz >= 0) {
            hazards[last_hit_haz].type = (hazards[last_hit_haz].type == 11) ? 22 : 20;
            last_hit_haz = -1;
        }
    }
}

static void draw_menu_bugs(const struct eapp_framebuffer *fb, int compact)
{
    if (compact) {
        draw_sprite(fb, 725, 18, 82, ANC_TOP_LEFT, 0);
        draw_sprite(fb, 726, 26, 152, ANC_TOP_LEFT, 0);
        draw_sprite(fb, 724, DOODLE_SCR_W - 16, 88, ANC_TOP_RIGHT, 0);
        draw_sprite(fb, 727, DOODLE_SCR_W - 20, 148, ANC_TOP_RIGHT, 0);
        draw_sprite(fb, 723, DOODLE_SCR_W - 14, 204, ANC_BOTTOM_RIGHT, 0);
    } else {
        draw_sprite(fb, 723, DOODLE_SCR_W - (DOODLE_SCR_W >> 5),
                    DOODLE_SCR_H - (DOODLE_SCR_H >> 3), ANC_BOTTOM_RIGHT, 0);
        draw_sprite(fb, 724, DOODLE_SCR_W - (DOODLE_SCR_W >> 5),
                    (DOODLE_SCR_H >> 1) - (DOODLE_SCR_H >> 4), ANC_TOP_RIGHT, 0);
        draw_sprite(fb, 725, DOODLE_SCR_W >> 4, DOODLE_SCR_H >> 2, ANC_TOP_LEFT, 0);
        draw_sprite(fb, 726, DOODLE_SCR_W >> 3,
                    (DOODLE_SCR_H >> 1) - (DOODLE_SCR_H >> 3), ANC_TOP_LEFT, 0);
        draw_sprite(fb, 727, DOODLE_SCR_W - (DOODLE_SCR_W >> 4),
                    (DOODLE_SCR_H >> 1) + (DOODLE_SCR_H >> 3), ANC_TOP_RIGHT, 0);
    }
}

static const char *menu_item_label(int id)
{
    switch (id) {
    case MI_CONTINUE: return strings[18];
    case MI_PLAY: return strings[56];
    case MI_GAME_MODE:
    case MI_OPT_MODE:
        return save.full_mode ? "Mode: Full" : "Mode: Classic";
    case MI_HIGH_SCORES: return strings[62];
    case MI_OPTIONS: return strings[12];
    case MI_HELP: return strings[34];
    case MI_ABOUT: return strings[36];
    case MI_EXIT: return strings[64];
    case MI_PAUSE_RESUME: return strings[15];
    case MI_PAUSE_SOUND:
    case MI_OPT_SOUND:
        return save.sound_on ? "Sound: On" : "Sound: Off";
    case MI_PAUSE_OPTIONS: return strings[12];
    case MI_PAUSE_MAIN_MENU: return strings[63];
    case MI_OPT_TUTORIAL:
        return save.tutorial_on ? "Tutorial: On" : "Tutorial: Off";
    case MI_OPT_RESET: return strings[37];
    case MI_OPT_BACK: return strings[6];
    case MI_OVER_AGAIN: return strings[39];
    case MI_OVER_MENU: return strings[63];
    case MI_CONFIRM_YES: return strings[2];
    case MI_CONFIRM_NO: return strings[3];
    default: return "";
    }
}

static void draw_main_menu_scene(const struct eapp_framebuffer *fb)
{
    int pw = plat_px_w >> 11;
    int px_c = to_sx(81920);
    int px_l = px_c - (pw >> 1);
    int py = to_sy(163840);
    int w0 = spr_w(22);
    int w2 = spr_w(24);
    int mid = pw - w0 - w2;
    int m_adj = 0;
    if (mid < 0) {
        m_adj = mid;
        mid = 0;
    }
    draw_sprite(fb, 22, px_l, py, ANC_TOP_LEFT, 0);
    stretch_h_sprite(fb, 23, px_l + w0, py, mid);
    draw_sprite(fb, 24, px_l + w0 + mid + m_adj, py, ANC_TOP_LEFT, 0);

    int dy = to_sy(menu_doodler_y);
    int dx = px_c + 7;
    if (menu_doodler_vy > LEGS_VY)
        draw_sprite(fb, 30, dx, dy - 3, ANC_BOTTOM_HCENTER, XFORM_FLIP_X);
    else
        draw_sprite(fb, 27, dx, dy, ANC_BOTTOM_HCENTER, XFORM_FLIP_X);

    int ux = to_sx(menu_ufo_x);
    int uy = to_sy(menu_ufo_y);
    if ((rand_pos() % 3) == 1)
        draw_sprite(fb, 32, ux, uy - 32, ANC_CENTER, 0);
    else
        draw_sprite(fb, 31, ux, uy, ANC_CENTER, 0);
}

static void draw_menu_list(const struct eapp_framebuffer *fb, int y_start, int max_vis)
{
    int btn_w = 168;
    int bx = (DOODLE_SCR_W - btn_w) >> 1;
    int y = y_start;

    if (menu_scroll_top > 0)
        draw_sprite(fb, 15, DOODLE_SCR_W >> 1, y_start - 2, ANC_BOTTOM_HCENTER, 0);

    int shown = 0;
    for (int i = menu_scroll_top; i < menu_count && shown < max_vis; ++i, ++shown) {
        int id = menu_items[i];
        const char *lbl = menu_item_label(id);
        int bh = 30;
        if (y + bh > DOODLE_SCR_H - 16)
            break;
        draw_pill_button(fb, lbl, bx, y, btn_w, i == menu_sel, NULL);
        y += bh + 5;
    }

    if (menu_scroll_top + shown < menu_count)
        draw_sprite(fb, 14, DOODLE_SCR_W >> 1, y - 1, ANC_TOP_HCENTER, 0);
}

static void draw_gameplay(const struct eapp_framebuffer *fb)
{
    draw_paper_bg(fb, bg_scroll_stopped);

    /* Right-edge saved high score markers */
    {
        const int32_t *hs = save.scores[save.full_mode ? 1 : 0];
        for (int i = 0; i < HIGH_SCORE_SLOTS; ++i) {
            if (hs[i] <= 0)
                break;
            if (hs[i] < (cam_y >> 11) + (WORLD_H >> 11) && hs[i] > (cam_y >> 11)) {
                int my = to_sy((hs[i] << 11) - cam_y);
                draw_sprite(fb, 822, DOODLE_SCR_W, my, ANC_TOP_RIGHT, 0);
                char rank_buf[16];
                rank_buf[0] = '#';
                fmt_uint(rank_buf + 1, i + 1);
                draw_font_str(fb, 7, rank_buf, DOODLE_SCR_W - 2, my - font_height(7), ANC_TOP_RIGHT);
            }
        }
    }

    draw_hazards_bg(fb);
    draw_platforms(fb);
    draw_hazards_fg(fb);
    draw_doodler(fb);
    draw_bullets(fb);

    /* Top translucent HUD header (exact match to f.java:1640-1643) */
    tile_sprite(fb, 1, 0, 0, DOODLE_SCR_W, 38);
    char sbuf[20];
    fmt_comma_int(sbuf, score);
    draw_font_str(fb, 4, sbuf, DOODLE_SCR_W >> 4, 22, ANC_BOTTOM_LEFT);
    draw_font_str(fb, 7, save.full_mode ? strings[30] : strings[29], DOODLE_SCR_W - 34, 6,
                  ANC_TOP_RIGHT);
    draw_sprite(fb, 823, DOODLE_SCR_W - 8, 4, ANC_TOP_RIGHT, 0);
}

void doodle_render(const struct eapp_framebuffer *fb)
{
    reset_clip();

    if (screen == SCR_SPLASH) {
        uint32_t el = eapp_ticks_ms() - splash_start_ms;
        draw_paper_bg(fb, 1);
        draw_sprite(fb, 720, 146, 6, ANC_TOP_HCENTER, 0);
        if (el > 200u)
            draw_sprite(fb, 719, 10, 74, ANC_TOP_LEFT, 0);
        if (el > 450u)
            draw_sprite(fb, 718, DOODLE_SCR_W - 8, 52, ANC_TOP_RIGHT, 0);
        if (el > 700u) {
            draw_sprite(fb, 721, 16, DOODLE_SCR_H - 44, ANC_BOTTOM_LEFT, 0);
            draw_sprite(fb, 722, DOODLE_SCR_W - 16, DOODLE_SCR_H - 42, ANC_BOTTOM_RIGHT, 0);
        }
        return;
    }

    if (screen == SCR_PLAYING) {
        draw_gameplay(fb);
        return;
    }

    if (screen == SCR_PAUSE) {
        draw_gameplay(fb);
        draw_torn_panel(fb, 46, 26, DOODLE_SCR_W - 92, DOODLE_SCR_H - 52);
        draw_font_str(fb, 3, strings[17], DOODLE_SCR_W >> 1, 32, ANC_TOP_HCENTER);
        draw_menu_list(fb, 62, 4);
        return;
    }

    if (screen == SCR_GAME_OVER) {
        draw_paper_bg(fb, 1);
        draw_menu_bugs(fb, 1);
        draw_sprite(fb, 21, 0, 0, ANC_TOP_LEFT, 0);

        char num_buf[20];
        draw_font_str(fb, 5, "Game Over!", DOODLE_SCR_W >> 1, 26, ANC_TOP_HCENTER);
        int y = 66;
        if (new_best_flag == 2) {
            draw_font_str(fb, 5, "New High Score!", DOODLE_SCR_W >> 1, 56, ANC_TOP_HCENTER);
            y = 86;
        }
        draw_font_str(fb, 3, strings[54], 44, y, ANC_TOP_LEFT);
        fmt_comma_int(num_buf, score);
        draw_font_str(fb, 4, num_buf, DOODLE_SCR_W - 44, y + 2, ANC_TOP_RIGHT);

        int32_t best = save.scores[save.full_mode ? 1 : 0][0];
        if (best < score)
            best = score;
        draw_font_str(fb, 3, strings[55], 44, y + 26, ANC_TOP_LEFT);
        fmt_comma_int(num_buf, best);
        draw_font_str(fb, 4, num_buf, DOODLE_SCR_W - 44, y + 28, ANC_TOP_RIGHT);

        draw_menu_list(fb, 148, 2);
        return;
    }

    /* Main Menu & sub-screens */
    draw_paper_bg(fb, 1);
    if (screen == SCR_MAIN_MENU) {
        draw_menu_bugs(fb, 0);
        draw_main_menu_scene(fb);
        draw_sprite(fb, 21, 0, 0, ANC_TOP_LEFT, 0);
        draw_menu_list(fb, 66, 4);
        return;
    }

    if (screen != SCR_HELP && screen != SCR_ABOUT)
        draw_menu_bugs(fb, 1);
    draw_sprite(fb, 21, 0, 0, ANC_TOP_LEFT, 0);

    if (screen == SCR_OPTIONS) {
        draw_font_str(fb, 3, strings[12], DOODLE_SCR_W - 18, 16, ANC_TOP_RIGHT);
        draw_menu_list(fb, 62, 4);
        return;
    }

    if (screen == SCR_HIGH_SCORES) {
        draw_font_str(fb, 3, strings[62], DOODLE_SCR_W - 18, 16, ANC_TOP_RIGHT);
        draw_pill_button(fb, hs_view_mode ? "Mode: Full" : "Mode: Classic",
                         (DOODLE_SCR_W - 168) >> 1, 46, 168, 1, NULL);

        const int32_t *hs = save.scores[hs_view_mode ? 1 : 0];
        char row_l[24], row_r[24];
        int hy = 84;
        for (int i = 0; i < 6; ++i) {
            fmt_uint(row_l, i + 1);
            size_t ll = str_len(row_l);
            row_l[ll] = '.';
            row_l[ll + 1] = 0;
            draw_font_str(fb, 3, row_l, 56, hy, ANC_TOP_LEFT);
            if (hs[i] > 0)
                fmt_comma_int(row_r, hs[i]);
            else {
                row_r[0] = '-';
                row_r[1] = 0;
            }
            draw_font_str(fb, 4, row_r, DOODLE_SCR_W - 56, hy + 2, ANC_TOP_RIGHT);
            hy += 22;
        }
        return;
    }

    if (screen == SCR_HELP || screen == SCR_ABOUT) {
        const char *title = (screen == SCR_HELP) ? strings[34] : strings[36];
        const char *body = (screen == SCR_HELP) ? strings[33] : strings[35];
        draw_font_str(fb, 3, title, DOODLE_SCR_W - 18, 16, ANC_TOP_RIGHT);
        int tot = 0;
        int view_h = DOODLE_SCR_H - 64;
        draw_wrapped_text(fb, 3, body, 16, 48, DOODLE_SCR_W - 32, view_h, text_scroll_y, &tot);
        text_max_scroll = (tot > view_h) ? (tot - view_h) : 0;
        if (text_scroll_y > 0)
            draw_sprite(fb, 15, DOODLE_SCR_W >> 1, 44, ANC_BOTTOM_HCENTER, 0);
        if (text_scroll_y < text_max_scroll)
            draw_sprite(fb, 14, DOODLE_SCR_W >> 1, 48 + view_h, ANC_TOP_HCENTER, 0);
        return;
    }

    if (screen == SCR_TUTORIAL) {
        draw_torn_panel(fb, 26, 46, DOODLE_SCR_W - 52, DOODLE_SCR_H - 92);
        const char *msg = strings[65];
        if (tutorial_step == 1)
            msg = strings[66];
        else if (tutorial_step == 2)
            msg = strings[67];
        else if (tutorial_step >= 3)
            msg = strings[68];
        draw_wrapped_text(fb, 3, msg, 42, 64, DOODLE_SCR_W - 84, DOODLE_SCR_H - 128, 0, NULL);
        return;
    }

    if (screen == SCR_CONFIRM_RESET || screen == SCR_CONFIRM_EXIT) {
        const char *q_lbl = (screen == SCR_CONFIRM_EXIT) ? strings[64] : strings[37];
        draw_torn_panel(fb, 36, 44, DOODLE_SCR_W - 72, DOODLE_SCR_H - 88);
        draw_font_str(fb, 3, q_lbl, DOODLE_SCR_W >> 1, 58, ANC_TOP_HCENTER);
        draw_menu_list(fb, 94, 2);
    }
}
