/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Ported from Rockbox apps/plugins/2048.c and apps/plugins/lib/highscore.c:
 * the tile movement, spawning and high score table.
 *
 * Copyright (C) 2014 Franklin Wei
 * Copyright (C) 2005 Linus Nielsen Feltzing
 *
 * Clone of 2048 by Gabriele Cirulli
 *
 * Modified by the RevolaOS authors, 2026.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 */

#include "game.h"
#include "eapp.h"

#define SPACES (GRID_SIZE * GRID_SIZE)

static uint8_t merged[GRID_SIZE][GRID_SIZE];
static uint32_t before_move[GRID_SIZE][GRID_SIZE];

static int rand_range(int min, int max)
{
    return eapp_random() % (max - min + 1) + min;
}

/* 1 in 10 chance of a four */
static uint32_t rand_2_or_4(void)
{
    return eapp_random() % 10 == 0 ? 4 : 2;
}

static void copy_grid(uint32_t to[GRID_SIZE][GRID_SIZE], const uint32_t from[GRID_SIZE][GRID_SIZE])
{
    for (int x = 0; x < GRID_SIZE; ++x)
        for (int y = 0; y < GRID_SIZE; ++y)
            to[x][y] = from[x][y];
}

static int grids_equal(const uint32_t a[GRID_SIZE][GRID_SIZE],
                       const uint32_t b[GRID_SIZE][GRID_SIZE])
{
    for (int x = 0; x < GRID_SIZE; ++x)
        for (int y = 0; y < GRID_SIZE; ++y)
            if (a[x][y] != b[x][y])
                return 0;
    return 1;
}

/* place a 2 or 4 in a random empty space */
static void place_random(struct game *game)
{
    int xpos[SPACES], ypos[SPACES];
    int back = 0;
    for (int y = 0; y < GRID_SIZE; ++y)
        for (int x = 0; x < GRID_SIZE; ++x)
            if (!game->grid[x][y]) {
                xpos[back] = x;
                ypos[back++] = y;
            }
    if (!back)
        return;
    int idx = rand_range(0, back - 1);
    game->grid[xpos[idx]][ypos[idx]] = rand_2_or_4();
}

void game_new(struct game *game)
{
    for (int x = 0; x < GRID_SIZE; ++x)
        for (int y = 0; y < GRID_SIZE; ++y)
            game->grid[x][y] = 0;
    place_random(game);
    place_random(game);
    game->score = 0;
    game->already_won = 0;
}

/* this function performs the tile movement */
static void slide_internal(struct game *game, int startx, int starty, int stopx, int stopy,
                           int dx, int dy, int lookx, int looky)
{
    uint32_t(*grid)[GRID_SIZE] = game->grid;
    for (int y = starty; y != stopy; y += dy) {
        for (int x = startx; x != stopx; x += dx) {
            if (grid[x + lookx][y + looky] == grid[x][y] && grid[x][y] &&
                !merged[x + lookx][y + looky] && !merged[x][y]) {
                /* Each merged tile cannot be merged again */
                merged[x + lookx][y + looky] = 1;
                grid[x + lookx][y + looky] = 2 * grid[x][y];
                game->score += grid[x + lookx][y + looky];
                grid[x][y] = 0;
            } else if (grid[x + lookx][y + looky] == 0) {
                grid[x + lookx][y + looky] = grid[x][y];
                grid[x][y] = 0;
            }
        }
    }
}

/* moves each tile one space in the direction */
static void slide(struct game *game, enum direction direction)
{
    switch (direction) {
    case DIR_UP:
        slide_internal(game, 0, 1, GRID_SIZE, GRID_SIZE, 1, 1, 0, -1);
        break;
    case DIR_DOWN:
        slide_internal(game, 0, GRID_SIZE - 2, GRID_SIZE, -1, 1, -1, 0, 1);
        break;
    case DIR_LEFT:
        slide_internal(game, 1, 0, GRID_SIZE, GRID_SIZE, 1, 1, -1, 0);
        break;
    case DIR_RIGHT:
        slide_internal(game, GRID_SIZE - 2, 0, -1, GRID_SIZE, -1, 1, 1, 0);
        break;
    }
}

void game_begin_move(struct game *game)
{
    for (int x = 0; x < GRID_SIZE; ++x)
        for (int y = 0; y < GRID_SIZE; ++y)
            merged[x][y] = 0;
    copy_grid(before_move, game->grid);
}

int game_step(struct game *game, enum direction direction)
{
    uint32_t before[GRID_SIZE][GRID_SIZE];
    copy_grid(before, game->grid);
    slide(game, direction);
    return !grids_equal(before, game->grid);
}

int game_end_move(struct game *game)
{
    if (grids_equal(before_move, game->grid))
        return 0;
    place_random(game);
    return 1;
}

int game_can_move(const struct game *game)
{
    for (enum direction direction = DIR_UP; direction <= DIR_RIGHT; ++direction) {
        struct game probe = *game;
        for (int x = 0; x < GRID_SIZE; ++x)
            for (int y = 0; y < GRID_SIZE; ++y)
                merged[x][y] = 0;
        slide(&probe, direction);
        if (!grids_equal(probe.grid, game->grid))
            return 1;
    }
    return 0;
}

uint32_t game_biggest(const struct game *game)
{
    uint32_t biggest = 0;
    for (int x = 0; x < GRID_SIZE; ++x)
        for (int y = 0; y < GRID_SIZE; ++y)
            if (game->grid[x][y] > biggest)
                biggest = game->grid[x][y];
    return biggest;
}

uint32_t game_checksum(const struct game *game)
{
    uint32_t sum = 0;
    for (int x = 0; x < GRID_SIZE; ++x)
        for (int y = 0; y < GRID_SIZE; ++y)
            sum += game->grid[x][y];
    return sum ^ game->score;
}

int highscore_update(struct highscore *scores, uint32_t score, uint32_t level)
{
    if (score <= scores[NUM_SCORES - 1].score)
        return -1;
    int pos = NUM_SCORES - 1;
    while (pos > 0 && score > scores[pos - 1].score) {
        scores[pos] = scores[pos - 1];
        pos--;
    }
    scores[pos].score = score;
    scores[pos].level = level;
    return pos;
}
