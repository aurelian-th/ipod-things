/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Ported from Rockbox apps/plugins/dice.c: the dice and their roll.
 * Copyright (C) 2005 by Brandon Low. Modified by the RevolaOS authors, 2026. */

#ifndef DICE_H
#define DICE_H

#include <stdint.h>

#define MAX_DICE 12
/* A d100 is a pair of ten-sided dice, so it takes two places of the grid. */
#define MAX_PERCENTILE_DICE 6
#define SIDE_CHOICES 8

extern const uint8_t side_counts[SIDE_CHOICES];

struct dice {
    uint8_t values[MAX_DICE];
    unsigned total;
    unsigned count;       /* 1..MAX_DICE */
    unsigned sides_index; /* into side_counts */
};

void dice_init(struct dice *dice);
/* Pulls a loaded count and sides index back into range. */
void dice_clamp(struct dice *dice);
unsigned dice_sides(const struct dice *dice);
unsigned dice_max_count(const struct dice *dice);
void dice_roll(struct dice *dice);

#endif
