/* SPDX-License-Identifier: GPL-3.0-only */
/* The rules of 2048 and its high score table, from Rockbox's
 * apps/plugins/2048.c and apps/plugins/lib/highscore.c.
 * Copyright (C) 2014 Franklin Wei, (C) 2005 Linus Nielsen Feltzing.
 * Modified by the RevolaOS authors, 2026.
 * Rockbox is GPL-2.0-or-later; this version is GPL-3.0-only. */

#ifndef GAME_H
#define GAME_H

#include <stdint.h>

#define GRID_SIZE 4
#define WINNING_TILE 2048u
#define NUM_SCORES 5

enum direction { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };

struct game {
    uint32_t grid[GRID_SIZE][GRID_SIZE]; /* [x][y], 0 for an empty cell */
    uint32_t score;
    uint32_t already_won;
};

struct highscore {
    uint32_t score;
    uint32_t level; /* the biggest tile */
};

/* An empty board with the two starting tiles. */
void game_new(struct game *game);
/* A move is up to GRID_SIZE - 1 steps in one direction, each shown. Begin
 * clears the merge marks, step slides every tile one cell and returns 1 when
 * something moved, end places a new tile when the move changed the board and
 * returns whether it did. */
void game_begin_move(struct game *game);
int game_step(struct game *game, enum direction direction);
int game_end_move(struct game *game);
int game_can_move(const struct game *game);
uint32_t game_biggest(const struct game *game);
uint32_t game_checksum(const struct game *game);
/* Inserts a score, keeping the table sorted; its row, or -1 when it does not
 * enter. */
int highscore_update(struct highscore *scores, uint32_t score, uint32_t level);

#endif
