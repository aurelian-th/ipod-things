/* SPDX-License-Identifier: GPL-3.0-only */
/* The screens, the wheel, the saved board and the high scores around game.c,
 * after the flow of Rockbox's apps/plugins/2048.c.
 * Copyright (C) 2014 Franklin Wei. Modified by the RevolaOS authors, 2026.
 * Rockbox is GPL-2.0-or-later; this version is GPL-3.0-only. */

#include "game.h"
#include "eapp.h"
#include "eapp_ui.h"

extern const uint16_t tiles[][48 * 48];
extern const unsigned tile_count;

#define TILE 48
#define BOARD_X 90
#define BOARD_Y 8
#define BOARD_SIDE 224
#define STEP_MS 50 /* one slide step, Rockbox's ANIM_SLEEPTIME */
#define SPLASH_MS 2000
#define RED 0xf800
#define WHITE 0xffff
/* Rockbox's page, board and text colours */
#define PAGE 0xffdd
#define BOARD 0xbd74
#define TEXT 0x736c

#define SAVE_NAME "2048"

enum state { MENU, PLAY, SLIDE, PAUSE, CONFIRM, SPLASH, OVER, SCORES, HELP };
enum item { IT_RESUME, IT_NEW, IT_SCORES, IT_HELP, IT_DISCARD, IT_QUIT };
enum action { ACT_NEW_GAME, ACT_DISCARD };

static const unsigned cell_offset[GRID_SIZE] = {6, 61, 115, 170};
static const char *const item_labels[] = {"RESUME GAME", "NEW GAME", "HIGH SCORES",
                                          "HELP", "QUIT WITHOUT SAVING", "QUIT"};
/* Lines of at most 24 characters, what fits at twice the font size. */
static const char *const help_lines[] = {
    "AIM",
    "JOIN THE NUMBERS TO GET",
    "TO THE 2048 TILE",
    "HOW TO PLAY",
    "TOUCH THE TOP RIGHT",
    "BOTTOM OR LEFT OF THE",
    "WHEEL TO SLIDE THE TILES",
    "TILES WITH THE SAME",
    "NUMBER MERGE INTO ONE",
    "CENTER TAKES BACK A MOVE",
    "MENU PAUSES THE GAME",
};
#define HELP_HEADING(i) ((i) == 0 || (i) == 3)

static struct game game;
static struct highscore scores[NUM_SCORES];
static uint32_t best;
static int have_game; /* a game to resume or save */
static struct eapp_save *save;
static enum state state, back_state;
static int seeded;
/* A move is a touch on the wheel without a click: the sector the finger landed
 * in counts when it lifts, unless a button was clicked meanwhile. */
static int touching, touch_clicked;
static uint8_t touch_directions;

static struct eapp_menu menu;
static struct eapp_menu_item items[6];
static enum item item_ids[6];

static enum action pending;
static enum direction slide_direction;
static unsigned slide_steps;
static uint32_t slide_at;
static struct game undo;
static int undo_valid;
static int over_row, scores_row = -1;
static const char *splash_text;
static uint32_t splash_until;
static enum state splash_next;
static int splash_board, splash_exit;
static char number_buffer[12], splash_buffer[32];

static const char *number(uint32_t value, char *buffer)
{
    unsigned i = 11;
    buffer[i] = 0;
    do {
        buffer[--i] = '0' + value % 10;
        value /= 10;
    } while (value && i);
    return buffer + i;
}

/* A row of a list: a bar in the board colour behind the selected one. */
static void draw_row(const struct eapp_framebuffer *fb, unsigned y, int selected)
{
    if (selected)
        eapp_fill_rect(fb, 12, y - 6, fb->width - 24, 26, BOARD);
}

/* Saves: one record in the host's save store, the scores and the board to
 * resume, written again whenever one of them changes. */

struct saved {
    struct highscore scores[NUM_SCORES];
    uint32_t have_game;
    struct game game;
    uint32_t checksum;
};

static int store(void)
{
    struct saved saved;
    for (unsigned i = 0; i < NUM_SCORES; ++i)
        saved.scores[i] = scores[i];
    saved.have_game = have_game;
    saved.game = game;
    saved.checksum = game_checksum(&game);
    return save && eapp_save_write(save, &saved, sizeof saved) == 0 ? 0 : -1;
}

/* The record into `scores` and, when it holds a valid board, `game`;
 * whether it did. A damaged record is ignored and replaced at the next
 * store. */
static int restore(void)
{
    struct saved saved;
    save = eapp_save_open(SAVE_NAME);
    if (!save || eapp_save_read(save, &saved, sizeof saved, 0) != (int)sizeof saved)
        return 0;
    for (unsigned i = 0; i < NUM_SCORES; ++i)
        scores[i] = saved.scores[i];
    if (!saved.have_game || saved.checksum != game_checksum(&saved.game))
        return 0;
    game = saved.game;
    return 1;
}

static int record_score(void)
{
    int row = highscore_update(scores, game.score, game_biggest(&game));
    store();
    return row;
}

/* States */

static void open_menu(enum state which)
{
    unsigned count = 0;
    if (which == PAUSE)
        item_ids[count++] = IT_RESUME;
    else if (have_game)
        item_ids[count++] = IT_RESUME;
    item_ids[count++] = IT_NEW;
    item_ids[count++] = IT_SCORES;
    item_ids[count++] = IT_HELP;
    if (which == PAUSE)
        item_ids[count++] = IT_DISCARD;
    item_ids[count++] = IT_QUIT;
    for (unsigned i = 0; i < count; ++i)
        items[i] = (struct eapp_menu_item){item_labels[item_ids[i]], NULL};
    menu = (struct eapp_menu){"2048", items, count, 0};
    state = which;
}

static void enter_play(void)
{
    touching = 1; /* a finger still down from a menu does not move */
    touch_clicked = 1;
    state = PLAY;
}

static void leave_to(enum state which)
{
    if (which == PLAY)
        enter_play();
    else
        state = which;
}

static void splash(const char *text, enum state next, int board)
{
    splash_text = text;
    splash_until = eapp_ticks_ms() + SPLASH_MS;
    splash_next = next;
    splash_board = board;
    splash_exit = 0;
    state = SPLASH;
}

static void end_splash(void)
{
    if (splash_exit)
        eapp_request_exit();
    else
        leave_to(splash_next);
}

static void start_new_game(void)
{
    game_new(&game);
    have_game = 1;
    undo_valid = 0;
    best = scores[0].score;
    enter_play();
}

static void quit_saving(void)
{
    if (store() < 0) {
        splash("COULD NOT SAVE", MENU, 0);
        splash_exit = 1;
        return;
    }
    eapp_request_exit();
}

static void run_action(void)
{
    if (pending == ACT_NEW_GAME) {
        record_score();
        start_new_game();
    } else {
        have_game = 0;
        record_score();
        eapp_request_exit();
    }
}

static void choose(enum item item)
{
    switch (item) {
    case IT_RESUME:
        enter_play();
        break;
    case IT_NEW:
        if (have_game) {
            pending = ACT_NEW_GAME;
            back_state = state;
            state = CONFIRM;
        } else {
            start_new_game();
        }
        break;
    case IT_SCORES:
        scores_row = -1;
        back_state = state;
        state = SCORES;
        break;
    case IT_HELP:
        back_state = state;
        state = HELP;
        break;
    case IT_DISCARD:
        pending = ACT_DISCARD;
        back_state = state;
        state = CONFIRM;
        break;
    case IT_QUIT:
        quit_saving();
        break;
    }
}

static void begin_slide(enum direction direction)
{
    undo = game;
    undo_valid = 1;
    game_begin_move(&game);
    slide_direction = direction;
    slide_steps = 0;
    slide_at = eapp_ticks_ms() - STEP_MS;
    state = SLIDE;
}

/* The move a lifted touch asked for, when it was one of the four sectors. */
static void slide_for(uint8_t directions)
{
    if (directions == EAPP_DIRECTION_UP)
        begin_slide(DIR_UP);
    else if (directions == EAPP_DIRECTION_DOWN)
        begin_slide(DIR_DOWN);
    else if (directions == EAPP_DIRECTION_LEFT)
        begin_slide(DIR_LEFT);
    else if (directions == EAPP_DIRECTION_RIGHT)
        begin_slide(DIR_RIGHT);
}

static void finish_move(void)
{
    if (!game_end_move(&game))
        undo_valid = 0;
    if (!game_can_move(&game)) {
        have_game = 0;
        over_row = record_score();
        splash("GAME OVER", OVER, 1);
        return;
    }
    if (!game.already_won && game_biggest(&game) >= WINNING_TILE) {
        game.already_won = 1;
        splash("YOU WIN", PLAY, 1);
        return;
    }
    enter_play();
}

static void resolve_over(void)
{
    if (over_row >= 0) {
        scores_row = over_row;
        back_state = MENU;
        char *at = splash_buffer;
        for (const char *s = "NEW HIGH SCORE: "; *s; ++s)
            *at++ = *s;
        for (const char *s = number(game.score, number_buffer); *s; ++s)
            *at++ = *s;
        *at = 0;
        open_menu(MENU);
        splash(splash_buffer, SCORES, 0);
    } else {
        open_menu(MENU);
    }
}

/* Drawing */

static void draw_tile(const struct eapp_framebuffer *fb, int x, int y, uint32_t value)
{
    unsigned index = 0;
    for (uint32_t v = value; v > 1; v >>= 1)
        ++index;
    if (index >= tile_count)
        index = tile_count - 1;
    struct eapp_bitmap tile = {tiles[index], TILE, TILE, TILE, -1};
    eapp_draw_bitmap(fb, x, y, &tile);
}

static void draw_board(const struct eapp_framebuffer *fb)
{
    eapp_clear(fb, PAGE);
    eapp_fill_rect(fb, BOARD_X, BOARD_Y, BOARD_SIDE, BOARD_SIDE, BOARD);
    for (int x = 0; x < GRID_SIZE; ++x)
        for (int y = 0; y < GRID_SIZE; ++y)
            draw_tile(fb, BOARD_X + cell_offset[x], BOARD_Y + cell_offset[y], game.grid[x][y]);
    draw_tile(fb, 21, 8, WINNING_TILE);
    eapp_draw_text(fb, 12, 70, "SCORE", TEXT, 1);
    eapp_draw_text(fb, 12, 80, number(game.score, number_buffer), TEXT, 2);
    eapp_draw_text(fb, 12, 106, "BEST", TEXT, 1);
    eapp_draw_text(fb, 12, 116, number(best, number_buffer), TEXT, 2);
    eapp_draw_text(fb, 12, 220, "MENU: PAUSE", TEXT, 1);
}

static void draw_splash(const struct eapp_framebuffer *fb)
{
    int width = eapp_text_width(splash_text, 2) + 32;
    int x = (fb->width - width) / 2;
    eapp_fill_rect(fb, x, 100, width, 40, PAGE);
    eapp_draw_rect(fb, x, 100, width, 40, TEXT);
    eapp_draw_rect(fb, x + 1, 101, width - 2, 38, TEXT);
    eapp_draw_text_in(fb, x, 113, width, splash_text, TEXT, 2, EAPP_ALIGN_CENTER);
}

static void draw_menu(const struct eapp_framebuffer *fb)
{
    eapp_clear(fb, PAGE);
    draw_tile(fb, 12, 10, WINNING_TILE);
    for (unsigned i = 0; i < menu.count; ++i) {
        unsigned y = 70 + i * 26;
        int selected = i == menu.selected;
        draw_row(fb, y, selected);
        eapp_draw_text(fb, 24, y, menu.items[i].label, selected ? WHITE : TEXT, 2);
    }
}

/* The score and level columns end where their headings do. */
#define SCORE_RIGHT 118
#define LEVEL_RIGHT 262

static void draw_scores(const struct eapp_framebuffer *fb)
{
    eapp_clear(fb, PAGE);
    eapp_draw_text(fb, 12, 14, "HIGH SCORES", TEXT, 3);
    eapp_draw_text_in(fb, 0, 60, SCORE_RIGHT, "SCORE", TEXT, 2, EAPP_ALIGN_RIGHT);
    eapp_draw_text_in(fb, 0, 60, LEVEL_RIGHT, "LEVEL", TEXT, 2, EAPP_ALIGN_RIGHT);
    for (unsigned i = 0; i < NUM_SCORES; ++i) {
        int selected = (int)i == scores_row;
        unsigned y = 90 + i * 26;
        char row[3] = {'1' + i, ':', 0};
        uint16_t color = selected ? WHITE : TEXT;
        draw_row(fb, y, selected);
        eapp_draw_text(fb, 24, y, row, color, 2);
        eapp_draw_text_in(fb, 0, y, SCORE_RIGHT, number(scores[i].score, number_buffer), color,
                          2, EAPP_ALIGN_RIGHT);
        eapp_draw_text_in(fb, 0, y, LEVEL_RIGHT, number(scores[i].level, number_buffer), color,
                          2, EAPP_ALIGN_RIGHT);
    }
}

static void draw_help(const struct eapp_framebuffer *fb)
{
    eapp_clear(fb, PAGE);
    for (unsigned i = 0; i < sizeof help_lines / sizeof help_lines[0]; ++i)
        eapp_draw_text(fb, 12, 14 + i * 19, help_lines[i], HELP_HEADING(i) ? RED : TEXT, 2);
}

static void draw_confirm(const struct eapp_framebuffer *fb)
{
    eapp_clear(fb, PAGE);
    eapp_draw_text(fb, 12, 24, "ARE YOU SURE", TEXT, 3);
    eapp_draw_text(fb, 12, 76, "THIS WILL CLEAR THE", TEXT, 2);
    eapp_draw_text(fb, 12, 100, "CURRENT GAME", TEXT, 2);
    eapp_draw_text(fb, 12, 164, "CENTER: YES", TEXT, 2);
    eapp_draw_text(fb, 12, 190, "MENU: NO", TEXT, 2);
}

/* Callbacks */

static void init(void)
{
    have_game = restore();
    best = scores[0].score;
    if (have_game && game.score > best)
        best = game.score;
    open_menu(MENU);
}

static void input(const struct eapp_input_event *event)
{
    if (!seeded) {
        struct eapp_wheel wheel = eapp_read_wheel();
        eapp_random_seed(event->timestamp_ms ^ (eapp_ticks_ms() << 8) ^
                         ((uint32_t)wheel.position << 24));
        seeded = 1;
    }
    if (event->type != EAPP_INPUT_PRESS)
        return;

    switch (state) {
    case MENU:
    case PAUSE: {
        enum eapp_menu_action action = eapp_menu_input(&menu, event);
        if (action == EAPP_MENU_SELECT)
            choose(item_ids[menu.selected]);
        else if (action == EAPP_MENU_BACK && state == PAUSE)
            enter_play();
        else if (action == EAPP_MENU_BACK)
            quit_saving();
        break;
    }
    case PLAY:
        touch_clicked = 1;
        if (event->button == EAPP_BUTTON_MENU) {
            open_menu(PAUSE);
        } else if (event->button == EAPP_BUTTON_CENTER && undo_valid) {
            game = undo;
            undo_valid = 0;
        }
        break;
    case SLIDE:
    case OVER:
        break;
    case CONFIRM:
        if (event->button == EAPP_BUTTON_CENTER)
            run_action();
        else if (event->button == EAPP_BUTTON_MENU)
            state = back_state;
        break;
    case SPLASH:
        if (event->button == EAPP_BUTTON_CENTER)
            end_splash();
        break;
    case SCORES:
    case HELP:
        leave_to(back_state);
        break;
    }
}

static void frame(const struct eapp_frame *frame)
{
    const struct eapp_framebuffer *fb = &frame->framebuffer;
    if (!fb->pixels)
        return;
    struct eapp_wheel wheel = eapp_read_wheel();
    uint32_t now = eapp_ticks_ms();

    switch (state) {
    case MENU:
    case PAUSE:
        eapp_menu_scroll(&menu, &wheel);
        draw_menu(fb);
        break;
    case PLAY:
        if (wheel.touched && !touching) {
            touching = 1;
            touch_clicked = 0;
            touch_directions = eapp_wheel_directions(&wheel, EAPP_WHEEL_TOP_POSITION);
        } else if (!wheel.touched && touching) {
            touching = 0;
            if (!touch_clicked)
                slide_for(touch_directions);
        }
        draw_board(fb);
        break;
    case SLIDE:
        if (now - slide_at >= STEP_MS) {
            int moved = game_step(&game, slide_direction);
            if (game.score > best)
                best = game.score;
            slide_at = now;
            if (!moved || ++slide_steps == GRID_SIZE - 1)
                finish_move();
        }
        draw_board(fb);
        break;
    case CONFIRM:
        draw_confirm(fb);
        break;
    case SPLASH:
        if (splash_board)
            draw_board(fb);
        else
            eapp_clear(fb, 0);
        draw_splash(fb);
        if ((int32_t)(now - splash_until) >= 0)
            end_splash();
        break;
    case OVER:
        resolve_over();
        draw_board(fb);
        break;
    case SCORES:
        draw_scores(fb);
        break;
    case HELP:
        draw_help(fb);
        break;
    }
}

static void shutdown(void)
{
    store();
    eapp_save_close(save);
}

const struct eapp eapp_application = {
    .package_id = "2048",
    .init = init,
    .input = input,
    .frame = frame,
    .shutdown = shutdown,
};
