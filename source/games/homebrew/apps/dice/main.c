/* SPDX-License-Identifier: GPL-3.0-only */
/* The screens, the menus and the saved settings around dice.c, after the flow
 * of Rockbox's apps/plugins/dice.c.
 * Copyright (C) 2005 by Brandon Low. Modified by the RevolaOS authors, 2026.
 * Rockbox is GPL-2.0-or-later; this version is GPL-3.0-only. */

#include "dice.h"
#include "eapp.h"
#include "eapp_gl.h"
#include "eapp_libc.h"
#include "eapp_ui.h"
#include "solids.h"

/* The roll screen: dice in centred rows of up to four above the total and a
 * hint line, cells FACE pixels wide. The dice take as few rows as fit, four
 * of them a square, spread evenly with the longer rows first; a percentile
 * pair stays in one row. Twelve dice fill the grid's band in three rows of
 * four. */
#define FACE 56
#define GAP 10
#define COLUMNS 4
#define GRID_TOP 8
#define GRID_HEIGHT (3 * FACE + 2 * GAP)
#define TOTAL_Y 204
#define HINT_Y 226

/* The camera looks straight down at the felt (the plane z = 0, up is +z)
 * without perspective, so a die at rest shows only its top face; one unit on
 * the felt is UNIT_PX pixels. A die is its solid scaled by DIE_SCALE, and
 * while it drops it grows as if seen from CAMERA units above the felt. Once
 * a d4 roll has settled, the camera tilts back about the middle of the grid
 * over TILT_MS and moves into a slight perspective, ending TILT_CAMERA units
 * away; one unit at that distance stays UNIT_PX pixels. The grid moves
 * TILT_DROP pixels down meanwhile, so the standing dice leave as much room
 * above them as below. The distance starts out endless; until it comes in
 * to FLAT_CAMERA the view stays without perspective, which looks the same
 * there. */
#define SURFACE_WIDTH 320
#define SURFACE_HEIGHT 240
#define SCREEN_BYTES (SURFACE_WIDTH * SURFACE_HEIGHT * 2)
#define UNIT_PX 40.0f
#define CAMERA 10.0f
#define DIE_SCALE (SOLID_TEXELS / UNIT_PX)
#define TILT_MS 700
#define TILT_CAMERA 40.0f
#define FLAT_CAMERA 200.0f
#define TILT_DROP 10

/* Light from the top left; shadows fall to the bottom right. A shadow is a
 * disc that fades from SHADOW_ALPHA in the middle to nothing at its rim, the
 * solid's shadow radius growing by SHADOW_GROW of itself per unit of height,
 * and SHADOW_SHIFT plus SHADOW_LEAN of the height away along (SHADOW_X,
 * SHADOW_Y).
 * At rest only a soft edge shows around the die; it fades as the die rises. */
#define SHADOW_GROW 0.11f
#define SHADOW_SHIFT 0.1f
#define SHADOW_LEAN 0.3f
#define SHADOW_X 0.6f
#define SHADOW_Y -0.8f
#define SHADOW_ALPHA 0.75f

/* The roll: each die drops from DROP_HEIGHT units above its place, hits the
 * felt FALL_END of the way through its DROP_MS and again at BOUNCE_END,
 * bouncing BOUNCE_HIGH and then BOUNCE_LOW of the drop height, and stops.
 * It spins 1 to 3 turns while it falls; the first hit tips it over by 90 to
 * 270 degrees during the bounce, and the second rocks it by 15 to 45 degrees
 * more, easing into the result. The dice are thrown in from one side: each
 * starts half to all of THROW_NEAR to THROW_FAR units off its place along
 * the roll's direction, give or take THROW_SCATTER across and along, and
 * covers all but SLIDE of the way while it falls, sliding the rest to a stop
 * during the bounces. It spins and tips over towards where it travels. The
 * first die stops DROP_MS after the roll starts, the others one by one in a
 * random order, SETTLE_FIRST_MS apart, the gaps stretching by SETTLE_STEP_MS
 * up to SETTLE_MAX_MS. */
#define DROP_MS 900
#define DROP_HEIGHT 5.0f
#define FALL_END 0.5f
#define BOUNCE_END 0.8f
#define BOUNCE_HIGH 0.25f
#define BOUNCE_LOW 0.08f
#define THROW_NEAR 0.6f
#define THROW_FAR 1.2f
#define THROW_SCATTER 0.2f
#define SLIDE 0.12f
#define SETTLE_FIRST_MS 80
#define SETTLE_STEP_MS 15
#define SETTLE_MAX_MS 160

/* Lists: a title, rows at twice the font size, a hint line. */
#define TITLE_Y 14
#define LIST_TOP 56
#define ROW 26
#define LIST_ROWS 6
#define LIST_LEFT 24
#define LIST_RIGHT 296

/* White dice with black pips on a green table. */
#define FELT 0x12c6
#define FELT_LIGHT 0x2c6a
#define WHITE 0xffff
#define BLACK 0x0000
#define DIM 0xae96

#define SAVE_NAME "dice"
#define SAVE_MAGIC 0x44494345 /* "DICE" */

enum state { MENU, COUNT, SIDES, ANIMATION, ROLL };
enum item { IT_ROLL, IT_COUNT, IT_SIDES, IT_ANIMATION, IT_QUIT, ITEMS };

/* The pips of a six-sided face as bits of a 3 x 3 grid, row by row. */
static const uint16_t pips[7] = {0, 0x010, 0x101, 0x111, 0x145, 0x155, 0x16d};

static struct dice dice;
static int animate = 1;
static struct eapp_save *save;
static enum state state;
static int seeded;

static struct eapp_menu menu;
static struct eapp_menu_item items[MAX_DICE];
static unsigned main_selected, first_row;
static char count_labels[MAX_DICE][3];
static char side_labels[SIDE_CHOICES][4];
static char count_value[3], sides_value[4];
static const char *const on_off[] = {"OFF", "ON"};
/* Where each die is thrown from, off its place, and the degrees and axes of
 * its spin, tip and rock. */
static struct tumble {
    GLfloat from[2];
    GLfloat spin, spin_axis[3];
    GLfloat tip, tip_axis[2];
    GLfloat rock, rock_axis[2];
} tumbles[MAX_DICE];
/* The solids on screen (a d100 is two), when each stops, the order they
 * stop in and how many have. */
static unsigned solid_count;
static uint32_t stop_ms[MAX_DICE];
static uint8_t order[MAX_DICE];
static unsigned settled;
static uint32_t started, settled_at;
/* Frames the roll screen still draws: the surface has two buffers, so a
 * screen that stands still stops drawing once both hold it. */
static unsigned redraw;
/* The felt with the dice at rest and their shadows, as drawn in roll
 * `layer_roll` with the dice in the mask `layer_dice`. While more dice move,
 * frames copy it and add the dice that came to rest, rather than drawing
 * every resting die again. */
static uint16_t *rest_layer;
static unsigned rolls, layer_roll;
static uint16_t layer_dice;
/* How far each solid reaches from its centre at most, in its own units,
 * and how far its shadow at rest reaches from the die's place on the felt. */
static GLfloat solid_reach[SOLIDS], shadow_reach[SOLIDS];
/* Each triangle's outward normal, |x| + |y| + |z| = 1, for leaving out the
 * triangles that face away while the camera looks straight down
 * (`straight_down`): GL would cull them, but only after lighting their
 * corners. */
static GLfloat triangle_normals[SOLIDS][SOLID_TRIANGLES][3];
static int straight_down;
static char total_buffer[12];
static const char *hint;

/* `value` in decimal into `out`, which holds its digits and the end. */
static void format(char *out, unsigned value)
{
    char digits[10];
    unsigned n = 0;
    do {
        digits[n++] = '0' + value % 10;
        value /= 10;
    } while (value);
    while (n)
        *out++ = digits[--n];
    *out = 0;
}

/* Saved settings: one record in the host's save store, written when a setting
 * changes. A damaged record leaves the defaults. */

struct saved {
    uint32_t count;
    uint32_t sides_index;
    uint32_t animate;
    uint32_t checksum;
};

static uint32_t checksum(uint32_t count, uint32_t sides_index, uint32_t animate)
{
    return (count * 0x9e3779b9u) ^ (sides_index * 0x85ebca6bu) ^ (animate * 0xc2b2ae35u) ^
           SAVE_MAGIC;
}

static void store(void)
{
    struct saved saved = {dice.count, dice.sides_index, animate,
                          checksum(dice.count, dice.sides_index, animate)};
    if (save)
        eapp_save_write(save, &saved, sizeof saved);
}

static void restore(void)
{
    struct saved saved;
    save = eapp_save_open(SAVE_NAME);
    if (!save || eapp_save_read(save, &saved, sizeof saved, 0) != (int)sizeof saved ||
        saved.checksum != checksum(saved.count, saved.sides_index, saved.animate))
        return;
    dice.count = saved.count;
    dice.sides_index = saved.sides_index;
    animate = saved.animate != 0;
    dice_clamp(&dice);
}

/* States */

static void open_main(void)
{
    format(count_value, dice.count);
    format(sides_value, dice_sides(&dice));
    items[IT_ROLL] = (struct eapp_menu_item){"ROLL DICE", NULL};
    items[IT_COUNT] = (struct eapp_menu_item){"NUMBER OF DICE", count_value};
    items[IT_SIDES] = (struct eapp_menu_item){"NUMBER OF SIDES", sides_value};
    items[IT_ANIMATION] = (struct eapp_menu_item){"ANIMATION", on_off[animate]};
    items[IT_QUIT] = (struct eapp_menu_item){"QUIT", NULL};
    menu = (struct eapp_menu){"DICE", items, ITEMS, main_selected};
    first_row = 0;
    hint = "CENTER: SELECT   MENU: QUIT";
    state = MENU;
}

static void open_list(enum state which)
{
    if (which == COUNT) {
        for (unsigned i = 0; i < dice_max_count(&dice); ++i)
            items[i] = (struct eapp_menu_item){count_labels[i], NULL};
        menu = (struct eapp_menu){"NUMBER OF DICE", items, dice_max_count(&dice),
                                  dice.count - 1};
    } else if (which == SIDES) {
        for (unsigned i = 0; i < SIDE_CHOICES; ++i)
            items[i] = (struct eapp_menu_item){side_labels[i], NULL};
        menu = (struct eapp_menu){"NUMBER OF SIDES", items, SIDE_CHOICES, dice.sides_index};
    } else {
        items[0] = (struct eapp_menu_item){on_off[1], NULL};
        items[1] = (struct eapp_menu_item){on_off[0], NULL};
        menu = (struct eapp_menu){"ANIMATION", items, 2, !animate};
    }
    first_row = 0;
    hint = "CENTER: SELECT   MENU: BACK";
    state = which;
}

/* A random whole number from low to high, as a float. */
static GLfloat random_between(int low, int high)
{
    return (GLfloat)(low + (int)(eapp_random() % (unsigned)(high - low + 1)));
}

static void settle(void)
{
    settled = solid_count;
    settled_at = eapp_ticks_ms();
    redraw = 2;
}

static void roll(void)
{
    dice_roll(&dice);
    char *at = total_buffer;
    for (const char *s = "TOTAL "; *s; ++s)
        *at++ = *s;
    format(at, dice.total);
    hint = "CENTER: ROLL   MENU: SETUP";
    state = ROLL;
    ++rolls;
    solid_count = dice_sides(&dice) == 100 ? 2 * dice.count : dice.count;
    redraw = 2;
    if (!animate) {
        settle();
        settled_at -= TILT_MS;
        return;
    }
    settled = 0;
    /* The side the dice come from: a point 50 to 100 steps from a die's
     * place, any way round, each step a hundredth of the die's reach. */
    GLfloat throw_x, throw_y;
    do {
        throw_x = random_between(-100, 100);
        throw_y = random_between(-100, 100);
    } while (throw_x * throw_x + throw_y * throw_y < 2500 ||
             throw_x * throw_x + throw_y * throw_y > 10000);
    for (unsigned i = 0; i < solid_count; ++i) {
        unsigned j = eapp_random() % (i + 1);
        order[i] = order[j];
        order[j] = i;
        struct tumble *tumble = &tumbles[i];
        for (unsigned k = 0; k < 2; ++k)
            tumble->rock_axis[k] = random_between(-100, 100);
        /* A zero axis would not turn at all. */
        tumble->rock_axis[0] += tumble->rock_axis[0] == 0 && tumble->rock_axis[1] == 0;
        GLfloat reach = (THROW_NEAR + (THROW_FAR - THROW_NEAR) *
                                          random_between(0, 100) / 100) / 100;
        GLfloat scatter = THROW_SCATTER / 100;
        tumble->from[0] = throw_x * reach + random_between(-100, 100) * scatter;
        tumble->from[1] = throw_y * reach + random_between(-100, 100) * scatter;
        /* Turning about the felt's line across the path rolls the die along
         * it; the spin leans off that line by up to a quarter of the path
         * along it and up, so no two dice turn alike. The scatter is too
         * small to bring a path down to nothing. */
        GLfloat lean = random_between(-25, 25) / 100;
        GLfloat length = (tumble->from[0] < 0 ? -tumble->from[0] : tumble->from[0]) +
                         (tumble->from[1] < 0 ? -tumble->from[1] : tumble->from[1]);
        tumble->spin_axis[0] = -tumble->from[1] + lean * tumble->from[0];
        tumble->spin_axis[1] = tumble->from[0] + lean * tumble->from[1];
        tumble->spin_axis[2] = random_between(-25, 25) / 100 * length;
        tumble->tip_axis[0] = -tumble->from[1];
        tumble->tip_axis[1] = tumble->from[0];
        tumble->spin = random_between(360, 1080);
        tumble->tip = random_between(90, 270);
        tumble->rock = random_between(15, 45);
    }
    uint32_t stop = DROP_MS, gap = SETTLE_FIRST_MS;
    for (unsigned i = 0; i < solid_count; ++i) {
        stop_ms[order[i]] = stop;
        stop += gap;
        if (gap < SETTLE_MAX_MS)
            gap += SETTLE_STEP_MS;
    }
    started = eapp_ticks_ms();
}

/* Counts the solids that have stopped, in the order they stop. */
static void step(uint32_t now)
{
    while (settled < solid_count && now - started >= stop_ms[order[settled]])
        if (++settled == solid_count)
            settled_at = now;
}

static void choose(void)
{
    switch (state) {
    case MENU:
        main_selected = menu.selected;
        if (menu.selected == IT_ROLL)
            roll();
        else if (menu.selected == IT_COUNT)
            open_list(COUNT);
        else if (menu.selected == IT_SIDES)
            open_list(SIDES);
        else if (menu.selected == IT_ANIMATION)
            open_list(ANIMATION);
        else
            eapp_request_exit();
        break;
    case COUNT:
        dice.count = menu.selected + 1;
        store();
        open_main();
        break;
    case SIDES:
        dice.sides_index = menu.selected;
        dice_clamp(&dice);
        store();
        open_main();
        break;
    case ANIMATION:
        animate = menu.selected == 0;
        store();
        open_main();
        break;
    case ROLL:
        break;
    }
}

/* Drawing */

static void draw_hint(const struct eapp_framebuffer *fb)
{
    eapp_draw_text_in(fb, 0, HINT_Y, fb->width, hint, DIM, 1, EAPP_ALIGN_CENTER);
}

/* The rows that keep the selected one in view. */
static void draw_list(const struct eapp_framebuffer *fb)
{
    if (menu.selected < first_row)
        first_row = menu.selected;
    else if (menu.selected >= first_row + LIST_ROWS)
        first_row = menu.selected - LIST_ROWS + 1;
    eapp_clear(fb, FELT);
    eapp_draw_text(fb, 12, TITLE_Y, menu.title, WHITE, 3);
    for (unsigned i = first_row; i < menu.count && i - first_row < LIST_ROWS; ++i) {
        unsigned y = LIST_TOP + (i - first_row) * ROW;
        int selected = i == menu.selected;
        uint16_t color = selected ? WHITE : DIM;
        if (selected)
            eapp_fill_rect(fb, 12, y - 6, fb->width - 24, ROW, FELT_LIGHT);
        eapp_draw_text(fb, LIST_LEFT, y, menu.items[i].label, color, 2);
        if (menu.items[i].value)
            eapp_draw_text_in(fb, 0, y, LIST_RIGHT, menu.items[i].value, color, 2,
                              EAPP_ALIGN_RIGHT);
    }
    draw_hint(fb);
}

/* The solid and the result slot of solid i: a d100 is a tens die followed by
 * a units die, 00 and 0 showing 100. */
static const struct solid *solid_of(unsigned i, const GLfixed **rest)
{
    static const uint8_t kinds[SIDE_CHOICES] = {D3, D4, D6, D8, D10, D12, D20, D10_TENS};
    unsigned value = dice.values[dice_sides(&dice) == 100 ? i / 2 : i];
    unsigned kind = kinds[dice.sides_index], slot = value - 1;
    if (kind == D10_TENS) {
        kind = i & 1 ? D10_UNITS : D10_TENS;
        slot = i & 1 ? value % 10 : value % 100 / 10;
    }
    *rest = solids[kind].rest[slot];
    return &solids[kind];
}

/* Height above the resting place a share `u` of the way through a drop. */
static GLfloat drop_height(GLfloat u)
{
    if (u < FALL_END) {
        GLfloat s = u / FALL_END;
        return DROP_HEIGHT * (1.0f - s * s);
    }
    if (u < BOUNCE_END) {
        GLfloat s = (u - FALL_END) / (BOUNCE_END - FALL_END);
        return BOUNCE_HIGH * DROP_HEIGHT * 4.0f * s * (1.0f - s);
    }
    GLfloat s = (u - BOUNCE_END) / (1 - BOUNCE_END);
    return BOUNCE_LOW * DROP_HEIGHT * 4.0f * s * (1.0f - s);
}

/* The kind of solid on screen. */
static const struct solid *rolled_solid(void)
{
    const GLfixed *rest;
    return solid_of(0, &rest);
}

/* How far the camera has tilted at `now`, 0 to 1. */
static GLfloat tilted(uint32_t now)
{
    uint32_t t = now - settled_at;
    if (settled < solid_count || rolled_solid()->tilt == 0)
        return 0;
    if (t >= TILT_MS)
        return 1;
    GLfloat u = (GLfloat)t / TILT_MS;
    return u * u * (3 - 2 * u);
}

/* cos x for x up to a right angle, in radians, within a thousandth. */
static GLfloat cosine(GLfloat x)
{
    GLfloat x2 = x * x;
    return 1 - x2 / 2 * (1 - x2 / 12 * (1 - x2 / 30));
}

/* A die drawn this frame: where on the felt it stands, how high its centre
 * is off it, its scale, spin and solid, and how near it is to rest, 0 to 1. */
struct placed {
    GLfloat x, y, z, size, resting;
    GLfloat reach; /* on the felt around x, y: the die's, or its shadow's once it stops */
    GLfloat spin, tip, rock; /* degrees still to turn */
    const struct solid *solid;
    const GLfixed *rest;
    unsigned index;
};

/* A die with the depth test, or without when nothing may overlap it: a die
 * is convex and its back faces are culled, so it covers each of its pixels
 * once. */
static void die_state(int depth)
{
    glDisable(GL_BLEND);
    glDepthMask(depth ? GL_TRUE : GL_FALSE);
    if (depth)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
    glDisableClientState(GL_COLOR_ARRAY);
    glEnable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glColor4f(1, 1, 1, 1);
}

/* Depth only, for dice in the rest layer that others may pass behind: they
 * blend in as nothing. Turning the colour mask off and on again leaves the
 * host's GL drawing the next die black. */
static void depth_state(void)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_ONE);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

static void draw_die(const struct placed *die)
{
    glPushMatrix();
    glTranslatef(die->x, die->y, die->z);
    const struct tumble *tumble = &tumbles[die->index];
    if (die->spin != 0)
        glRotatef(die->spin, tumble->spin_axis[0], tumble->spin_axis[1], tumble->spin_axis[2]);
    if (die->tip != 0)
        glRotatef(die->tip, tumble->tip_axis[0], tumble->tip_axis[1], 0);
    if (die->rock != 0)
        glRotatef(die->rock, tumble->rock_axis[0], tumble->rock_axis[1], 0);
    glScalef(die->size, die->size, die->size);
    glMultMatrixx(die->rest);
    glVertexPointer(3, GL_FIXED, 0, die->solid->vertices);
    glNormalPointer(GL_FIXED, 0, die->solid->normals);
    glTexCoordPointer(2, GL_FIXED, 0, die->solid->uvs);
    const GLubyte *indices = die->solid->indices;
    unsigned count = die->solid->index_count;
    if (straight_down) {
        /* A triangle faces the camera when its normal turned into the eye's
         * space points up its z axis. Those just short of edge on stay, as
         * GL may round them either way. */
        static GLubyte front[SOLID_TRIANGLES * 3];
        GLfloat m[16];
        glGetFloatv(GL_MODELVIEW_MATRIX, m);
        const GLfloat(*normals)[3] = triangle_normals[die->solid - solids];
        count = 0;
        for (unsigned t = 0; t < die->solid->index_count / 3; ++t) {
            const GLfloat *n = normals[t];
            if (m[2] * n[0] + m[6] * n[1] + m[10] * n[2] > -0.01f * die->size) {
                front[count++] = indices[3 * t];
                front[count++] = indices[3 * t + 1];
                front[count++] = indices[3 * t + 2];
            }
        }
        indices = front;
    }
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_BYTE, indices);
    glPopMatrix();
}

/* A shadow is a fan, centre first, black throughout with only the middle's
 * alpha set per die. It leaves the depth alone, so that overlapping shadows
 * both blend, and the depth test hides it under the dice. */
static GLfloat fan[(SHADOW_RIM + 2) * 2];
static GLubyte shade[(SHADOW_RIM + 2) * 4];

static void shadow_state(void)
{
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, fan);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, shade);
}

/* The shadow on the felt, turning from a disc into the rim of a resting die
 * as the die comes to rest: its middle, the rim as offsets from the middle
 * in felt units, closed with the first point again, and the middle's
 * alpha. */
struct shadow {
    GLfloat x, y;
    GLfloat rim[(SHADOW_RIM + 1) * 2];
    GLubyte alpha;
};

static void shadow_of(const struct placed *die, struct shadow *shadow)
{
    GLfloat lift = die->z - die->solid->height * DIE_SCALE;
    GLfloat grow = DIE_SCALE * (1 + SHADOW_GROW * lift);
    GLfloat shift = SHADOW_SHIFT + SHADOW_LEAN * lift;
    GLfloat disc = die->solid->disc;
    for (unsigned j = 0; j <= SHADOW_RIM; ++j) {
        unsigned d = j % SHADOW_RIM;
        GLfloat rim = die->solid->shadow ? die->solid->shadow[d] : disc;
        GLfloat r = (disc + die->resting * (rim - disc)) * grow;
        shadow->rim[2 * j] = shadow_directions[2 * d] * r;
        shadow->rim[2 * j + 1] = shadow_directions[2 * d + 1] * r;
    }
    shadow->x = die->x + SHADOW_X * shift;
    shadow->y = die->y + SHADOW_Y * shift;
    shadow->alpha = (GLubyte)(255 * SHADOW_ALPHA * (1.0f - 0.7f * lift / DROP_HEIGHT));
}

static void draw_shadow(const struct placed *die)
{
    struct shadow shadow;
    shadow_of(die, &shadow);
    for (unsigned k = 0; k < (SHADOW_RIM + 1) * 2; ++k)
        fan[2 + k] = shadow.rim[k];
    shade[3] = shadow.alpha;
    glPushMatrix();
    glTranslatef(shadow.x, shadow.y, 0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, SHADOW_RIM + 2);
    glPopMatrix();
}

/* draw_shadow with the CPU, for the moving dice, whose shadows skip the depth
 * test: the same fan on the screen, seen straight down, its corners in 16.16
 * pixels. */
static void blend_shadow(const struct eapp_framebuffer *fb, const struct placed *die)
{
    struct shadow shadow;
    shadow_of(die, &shadow);
    GLfloat cx = SURFACE_WIDTH / 2 + shadow.x * UNIT_PX;
    GLfloat cy = SURFACE_HEIGHT / 2 - shadow.y * UNIT_PX;
    int32_t rim_x[SHADOW_RIM + 1], rim_y[SHADOW_RIM + 1];
    for (unsigned j = 0; j <= SHADOW_RIM; ++j) {
        rim_x[j] = (int32_t)((cx + shadow.rim[2 * j] * UNIT_PX) * 65536);
        rim_y[j] = (int32_t)((cy - shadow.rim[2 * j + 1] * UNIT_PX) * 65536);
    }
    const uint8_t alpha[3] = {shadow.alpha, 0, 0};
    int32_t middle_x = (int32_t)(cx * 65536), middle_y = (int32_t)(cy * 65536);
    for (unsigned j = 0; j < SHADOW_RIM; ++j) {
        const int32_t x[3] = {middle_x, rim_x[j], rim_x[j + 1]};
        const int32_t y[3] = {middle_y, rim_y[j], rim_y[j + 1]};
        eapp_blend_triangle(fb, x, y, alpha, BLACK);
    }
}

/* Whether two dice may cover each other on the screen, seen straight down. */
static int may_overlap(const struct placed *a, const struct placed *b)
{
    GLfloat reach = a->reach + b->reach;
    GLfloat dx = a->x - b->x, dy = a->y - b->y;
    return dx < reach && -dx < reach && dy < reach && -dy < reach;
}

/* The middle of solid i's cell on the screen. */
static void place_in_grid(unsigned i, int screen_width, int *px, int *py)
{
    unsigned unit = dice_sides(&dice) == 100 ? 2 : 1;
    unsigned units = solid_count / unit;
    unsigned rows = solid_count == 4 ? 2 : (solid_count + COLUMNS - 1) / COLUMNS;
    unsigned row = 0, first = 0, size = units / rows + (units % rows > 0);
    while (i / unit >= first + size) {
        first += size;
        ++row;
        size = units / rows + (units % rows > row);
    }
    unsigned columns = size * unit, column = i - first * unit;
    int height = rows * FACE + (rows - 1) * GAP;
    int width = columns * FACE + (columns - 1) * GAP;
    *px = (screen_width - width) / 2 + (int)column * (FACE + GAP) + FACE / 2;
    *py = GRID_TOP + (GRID_HEIGHT - height) / 2 + (int)row * (FACE + GAP) + FACE / 2;
}

static void draw_roll(const struct eapp_framebuffer *fb, uint32_t now)
{
    uint32_t elapsed = now - started;
    /* The dice drawn this frame, lowest first; the first `landed` rest on
     * the felt. */
    struct placed placed[MAX_DICE];
    unsigned shown = 0, landed = 0, stopped = 0;
    /* The camera: straight down, or tilting back and into perspective while
     * the rows spread about the middle of the grid, keeping their spacing on
     * the screen. */
    GLfloat share = tilted(now), distance = CAMERA;
    GLfloat middle = (GLfloat)(SURFACE_HEIGHT / 2 - (GRID_TOP + GRID_HEIGHT / 2)) / UNIT_PX;
    GLfloat spread = 1 / cosine(rolled_solid()->tilt * share * 3.14159265f / 180);

    for (unsigned i = 0; i < solid_count; ++i) {
        struct placed die = {.resting = 1};
        GLfloat lift = 0, off = 0;
        int moving = settled < solid_count && elapsed < stop_ms[i];
        stopped += !moving;
        if (moving) {
            if (elapsed + DROP_MS < stop_ms[i])
                continue;
            const struct tumble *tumble = &tumbles[i];
            GLfloat u = (GLfloat)(elapsed + DROP_MS - stop_ms[i]) / DROP_MS;
            lift = drop_height(u);
            if (u < FALL_END) {
                off = 1 - (1 - SLIDE) * u / FALL_END;
            } else {
                GLfloat s = 1 - (u - FALL_END) / (1 - FALL_END);
                off = SLIDE * s * s;
            }
            die.resting = u;
            die.rock = tumble->rock;
            if (u < BOUNCE_END) {
                GLfloat v = (u - FALL_END) / (BOUNCE_END - FALL_END);
                die.tip = u < FALL_END ? tumble->tip : tumble->tip * (1 - v);
                die.spin = u < FALL_END ? tumble->spin * (1 - u / FALL_END) : 0;
            } else {
                GLfloat v = 1 - (u - BOUNCE_END) / (1 - BOUNCE_END);
                die.rock *= v * v;
            }
        }
        die.solid = solid_of(i, &die.rest);
        int px, py;
        place_in_grid(i, fb->width, &px, &py);
        die.x = (GLfloat)(px - SURFACE_WIDTH / 2) / UNIT_PX + tumbles[i].from[0] * off;
        die.y = middle + ((GLfloat)(SURFACE_HEIGHT / 2 - py) / UNIT_PX - middle) * spread +
                tumbles[i].from[1] * off;
        die.z = die.solid->height * DIE_SCALE + lift;
        die.size = DIE_SCALE * CAMERA / (CAMERA - lift);
        die.index = i;
        die.reach = solid_reach[die.solid - solids] * die.size;
        if (!moving && die.reach < shadow_reach[die.solid - solids])
            die.reach = shadow_reach[die.solid - solids];
        unsigned k = shown++;
        for (; k > 0 && placed[k - 1].z > die.z; --k)
            placed[k] = placed[k - 1];
        placed[k] = die;
        landed += lift == 0;
    }

    GLfloat w = SURFACE_WIDTH / 2 / UNIT_PX, h = SURFACE_HEIGHT / 2 / UNIT_PX;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    straight_down = share == 0;
    if (share * FLAT_CAMERA < TILT_CAMERA) {
        glOrthof(-w, w, h, -h, 0, 2 * CAMERA);
    } else {
        distance = TILT_CAMERA / share;
        GLfloat near = distance - 2 * h, far = distance + 2 * h;
        glFrustumf(-w * near / distance, w * near / distance, h * near / distance,
                   -h * near / distance, near, far);
    }
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0, middle - share * TILT_DROP / UNIT_PX, -distance);
    glRotatef(-rolled_solid()->tilt * share, 1, 0, 0);
    glTranslatef(0, -middle, 0);

    /* The dice on the felt, lit and labelled, then their shadows, which the
     * depth test keeps off them. Then, lowest first, each die in the air
     * after its shadow, which falls over everything below it. The camera
     * only tilts once every die rests.
     * While the camera looks straight down and every die on the felt has
     * stopped, the felt comes from the rest layer, and dice that came to
     * rest since are added to it: the copy, plus the depth of each die in
     * the layer that a new or moving die may overlap, gives the same
     * pixels. Otherwise the felt is drawn, and kept if it may be. */
    int keep = rest_layer && share == 0 && stopped == landed;
    int layered = keep && layer_roll == rolls;
    /* added[k]: die k came to rest since the layer was drawn; overlaps[k]:
     * it may overlap another die drawn this frame, one of the two not in
     * the layer. */
    uint8_t added[MAX_DICE] = {0}, overlaps[MAX_DICE] = {0};
    int adding = 0, overlapping = 0;
    for (unsigned k = 0; k < landed; ++k)
        if (layered && !(layer_dice >> placed[k].index & 1))
            added[k] = 1, adding = 1;
    for (unsigned k = 0; k < shown; ++k)
        for (unsigned m = k + 1; m < shown; ++m)
            if ((m >= landed || added[k] || added[m]) && may_overlap(&placed[k], &placed[m]))
                overlaps[k] = overlaps[m] = 1, overlapping = 1;
    if (layered) {
        memcpy(fb->pixels, rest_layer, SCREEN_BYTES);
        if (adding || overlapping) {
            glClear(GL_DEPTH_BUFFER_BIT);
            depth_state();
            for (unsigned k = 0; k < landed; ++k)
                if (overlaps[k] && !added[k])
                    draw_die(&placed[k]);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        if (adding) {
            die_state(1);
            for (unsigned k = 0; k < landed; ++k)
                if (added[k])
                    draw_die(&placed[k]);
            shadow_state();
            for (unsigned k = 0; k < landed; ++k)
                if (added[k]) {
                    draw_shadow(&placed[k]);
                    layer_dice |= 1u << placed[k].index;
                }
            glFinish();
            memcpy(rest_layer, fb->pixels, SCREEN_BYTES);
        }
    } else {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        die_state(1);
        for (unsigned k = 0; k < landed; ++k)
            draw_die(&placed[k]);
        shadow_state();
        for (unsigned k = 0; k < landed; ++k)
            draw_shadow(&placed[k]);
        if (keep) {
            glFinish();
            memcpy(rest_layer, fb->pixels, SCREEN_BYTES);
            layer_roll = rolls;
            layer_dice = 0;
            for (unsigned k = 0; k < landed; ++k)
                layer_dice |= 1u << placed[k].index;
        }
    }
    for (unsigned k = landed; k < shown; ++k) {
        glFinish();
        blend_shadow(fb, &placed[k]);
        die_state(overlaps[k]);
        draw_die(&placed[k]);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    if (settled == solid_count)
        eapp_draw_text_in(fb, 0, TOTAL_Y, fb->width, total_buffer, WHITE, 2,
                          EAPP_ALIGN_CENTER);
    draw_hint(fb);
}

/* Smoothed labels are drawn SUPER times larger into a SIDE x SIDE pixel
 * square of scratch and averaged over SUPER x SUPER samples per pixel. */
enum { SUPER = 4, SIDE = 32, BIG = SIDE * SUPER };

/* The atlas is drawn BAND rows at a time in RGB565 and kept in 8-bit grey,
 * all the labels need. */
enum { BAND = 32 };

/* `label` into the band of the atlas whose first row is `top`: pips,
 * pixel-sharp text at a whole scale, or smoothed text at quarter steps or
 * turned (the d4's corners), drawn upright into `scratch` first. */
static void draw_label(const struct eapp_framebuffer *band, int top,
                       const struct solid_label *label, uint16_t *scratch_pixels)
{
    int cx = label->cell_x + label->x, cy = label->cell_y + label->y - top;
    /* No label reaches further than BIG / 2 from its centre. */
    if (cy + BIG / 2 < 0 || cy - BIG / 2 >= BAND)
        return;
    if (label->pips) {
        /* Each pip's radius 5 / 14 of their spacing. */
        int spacing = label->size, radius = label->size * 5 / 14;
        for (unsigned cell = 0; cell < 9; ++cell)
            if (pips[label->pips] & (1u << cell))
                eapp_fill_circle(band, cx + ((int)(cell % 3) - 1) * spacing,
                                 cy + ((int)(cell / 3) - 1) * spacing, radius, BLACK);
        return;
    }
    if (label->size % SUPER == 0 && label->cos64 == 64) {
        unsigned s = label->size / SUPER;
        int w = (int)eapp_text_width(label->text, s), h = 7 * (int)s;
        int y = cy - (label->underline ? h + 2 * (int)s : h) / 2;
        eapp_draw_text(band, cx - w / 2, y, label->text, BLACK, s);
        if (label->underline)
            eapp_fill_rect(band, cx - w / 2, y + h + (int)s, w, s, BLACK);
        return;
    }
    if (cy + SIDE / 2 < 0 || cy - SIDE / 2 >= BAND)
        return;
    unsigned q = label->size;
    int w = (int)eapp_text_width(label->text, q), h = 7 * (int)q;
    int box_h = label->underline ? h + 2 * (int)q : h;
    struct eapp_framebuffer scratch = {scratch_pixels, BIG, BIG, BIG};
    eapp_clear(&scratch, WHITE);
    eapp_draw_text(&scratch, (BIG - w) / 2, (BIG - box_h) / 2, label->text, BLACK, q);
    if (label->underline)
        eapp_fill_rect(&scratch, (BIG - w) / 2, (BIG - box_h) / 2 + h + (int)q, w, q, BLACK);
    int c = label->cos64, n = label->sin64;
    int ac = c < 0 ? -c : c, an = n < 0 ? -n : n;
    /* Half the turned box in atlas pixels, a pixel over. */
    int ex = (ac * w + an * box_h) / (2 * 64 * SUPER) + 1;
    int ey = (an * w + ac * box_h) / (2 * 64 * SUPER) + 1;
    ex = ex < SIDE / 2 ? ex : SIDE / 2;
    ey = ey < SIDE / 2 ? ey : SIDE / 2;
    for (int dy = -ey; dy < ey; ++dy)
        for (int dx = -ex; dx < ex; ++dx) {
            unsigned ink = 0;
            for (int oy = 0; oy < SUPER; ++oy)
                for (int ox = 0; ox < SUPER; ++ox) {
                    /* The upright sample this one came from: turned back by
                     * the angle, with y growing downwards. */
                    int px = dx * SUPER + ox, py = dy * SUPER + oy;
                    int sx = (c * px - n * py) / 64 + BIG / 2;
                    int sy = (n * px + c * py) / 64 + BIG / 2;
                    ink += sx >= 0 && sy >= 0 && sx < BIG && sy < BIG &&
                           !scratch_pixels[sy * BIG + sx];
                }
            if (ink) {
                unsigned g = 255 - ink * 255 / (SUPER * SUPER);
                eapp_draw_pixel(band, cx + dx, cy + dy,
                                (uint16_t)((g >> 3) << 11 | (g >> 2) << 5 | g >> 3));
            }
        }
}

/* The label atlas, drawn with the SDK into heap buffers and handed to GL,
 * which keeps its own copy. Up close a resting top face meets its texels one
 * to one, so magnification takes the nearest texel and keeps the labels
 * sharp; faces seen at a slant are minified and filtered. */
static void load_atlas(void)
{
    uint8_t *grey = eapp_alloc(ATLAS_WIDTH * ATLAS_HEIGHT);
    uint16_t *pixels = eapp_alloc(ATLAS_WIDTH * BAND * sizeof *pixels);
    uint16_t *scratch = eapp_alloc(BIG * BIG * sizeof *scratch);
    int drawn = grey && pixels && scratch;
    if (drawn) {
        struct eapp_framebuffer band = {pixels, ATLAS_WIDTH, BAND, ATLAS_WIDTH};
        for (int top = 0; top < ATLAS_HEIGHT; top += BAND) {
            eapp_clear(&band, WHITE);
            for (unsigned i = 0; i < solid_label_count; ++i)
                draw_label(&band, top, &solid_labels[i], scratch);
            uint8_t *row = grey + top * ATLAS_WIDTH;
            for (unsigned i = 0; i < ATLAS_WIDTH * BAND; ++i)
                row[i] = (uint8_t)((pixels[i] >> 5 & 63) * 255 / 63);
        }
    }
    /* Freed before GL takes its copy, which lowers the peak. */
    eapp_free(scratch);
    eapp_free(pixels);
    if (drawn) {
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, ATLAS_WIDTH, ATLAS_HEIGHT, 0, GL_LUMINANCE,
                     GL_UNSIGNED_BYTE, grey);
    }
    eapp_free(grey);
}

/* How far each solid and its shadow reach, and its triangles' normals. */
static void measure_solids(void)
{
    for (unsigned kind = 0; kind < SOLIDS; ++kind) {
        const struct solid *solid = &solids[kind];
        GLfloat far = 0;
        for (unsigned t = 0; t < solid->index_count / 3; ++t) {
            GLfloat v[3][3];
            for (unsigned c = 0; c < 3; ++c)
                for (unsigned k = 0; k < 3; ++k)
                    v[c][k] = solid->vertices[3 * solid->indices[3 * t + c] + k] / 65536.0f;
            GLfloat a[3], b[3], n[3], out = 0, size = 0;
            for (unsigned k = 0; k < 3; ++k) {
                a[k] = v[1][k] - v[0][k];
                b[k] = v[2][k] - v[0][k];
            }
            n[0] = a[1] * b[2] - a[2] * b[1];
            n[1] = a[2] * b[0] - a[0] * b[2];
            n[2] = a[0] * b[1] - a[1] * b[0];
            /* The solids are convex about their centre. */
            for (unsigned k = 0; k < 3; ++k) {
                out += n[k] * (v[0][k] + v[1][k] + v[2][k]);
                size += n[k] < 0 ? -n[k] : n[k];
            }
            for (unsigned k = 0; k < 3; ++k)
                triangle_normals[kind][t][k] = (out < 0 ? -n[k] : n[k]) / size;
            for (unsigned c = 0; c < 3; ++c) {
                GLfloat d = v[c][0] * v[c][0] + v[c][1] * v[c][1] + v[c][2] * v[c][2];
                far = d > far ? d : far;
            }
        }
        /* Newton's square root, from above, so never short. */
        GLfloat root = far + 1;
        for (unsigned i = 0; i < 20; ++i)
            root = (root + far / root) / 2;
        solid_reach[kind] = root * 1.001f;
        GLfloat rim = solid->disc;
        for (unsigned d = 0; solid->shadow && d < SHADOW_RIM; ++d)
            rim = solid->shadow[d] > rim ? solid->shadow[d] : rim;
        shadow_reach[kind] = rim * DIE_SCALE + SHADOW_SHIFT;
    }
}

/* Callbacks */

static void init(void)
{
    for (unsigned i = 0; i < MAX_DICE; ++i)
        format(count_labels[i], i + 1);
    for (unsigned i = 0; i < SIDE_CHOICES; ++i)
        format(side_labels[i], side_counts[i]);
    dice_init(&dice);
    restore();
    open_main();
}

static void start(void)
{
    static const GLfloat light[4] = {-0.4f, 0.5f, 1.0f, 0.0f};
    static const GLfloat ambient[4] = {0.45f, 0.45f, 0.45f, 1.0f};
    static const GLfloat diffuse[4] = {0.65f, 0.65f, 0.65f, 1.0f};
    glViewport(0, 0, SURFACE_WIDTH, SURFACE_HEIGHT);
    /* The host's colour buffer has its first row at the top, where GL puts
     * the bottom: draw_roll's projections flip y, which also turns the
     * winding. */
    glFrontFace(GL_CW);
    glLoadIdentity();
    glClearColor((FELT >> 11) / 31.0f, (FELT >> 5 & 63) / 63.0f, (FELT & 31) / 31.0f, 1.0f);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    /* The modelview only turns and scales evenly. */
    glEnable(GL_RESCALE_NORMAL);
    /* Smooth for the rounded edges and the shadows' fade; a flat face has
     * one normal at every corner and stays flat. */
    glShadeModel(GL_SMOOTH);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLightfv(GL_LIGHT0, GL_POSITION, light);
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glEnableClientState(GL_VERTEX_ARRAY);
    measure_solids();
    load_atlas();
    /* After the atlas, which needs more of the heap only while it loads. */
    rest_layer = eapp_alloc(SCREEN_BYTES);
}

static void input(const struct eapp_input_event *event)
{
    if (!seeded) {
        struct eapp_wheel wheel = eapp_read_wheel();
        eapp_random_seed(event->timestamp_ms ^ (eapp_ticks_ms() << 8) ^
                         ((uint32_t)wheel.position << 24));
        /* The first outputs after a seed with few set bits are small; skip them. */
        for (unsigned i = 0; i < 16; ++i)
            eapp_random();
        seeded = 1;
    }
    if (event->type != EAPP_INPUT_PRESS)
        return;
    if (state == ROLL) {
        if (event->button == EAPP_BUTTON_CENTER && settled < solid_count)
            settle();
        else if (event->button == EAPP_BUTTON_CENTER)
            roll();
        else if (event->button == EAPP_BUTTON_MENU)
            open_main();
        return;
    }
    enum eapp_menu_action action = eapp_menu_input(&menu, event);
    if (action == EAPP_MENU_SELECT)
        choose();
    else if (action == EAPP_MENU_BACK && state == MENU)
        eapp_request_exit();
    else if (action == EAPP_MENU_BACK)
        open_main();
}

static void frame(const struct eapp_frame *frame)
{
    const struct eapp_framebuffer *fb = &frame->framebuffer;
    if (!fb->pixels)
        return;
    struct eapp_wheel wheel = eapp_read_wheel();
    if (state == ROLL) {
        uint32_t now = eapp_ticks_ms();
        step(now);
        int moving = settled < solid_count || (rolled_solid()->tilt != 0 && now - settled_at < TILT_MS);
        if (moving)
            redraw = 2;
        if (redraw) {
            draw_roll(fb, now);
            if (!moving)
                --redraw;
        }
    } else {
        eapp_menu_scroll(&menu, &wheel);
        draw_list(fb);
    }
}

static void shutdown(void)
{
    if (save)
        eapp_save_close(save);
}

const struct eapp eapp_application = {
    .package_id = "dice",
    .init = init,
    .start = start,
    .input = input,
    .frame = frame,
    .shutdown = shutdown,
};
