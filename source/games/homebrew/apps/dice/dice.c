/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Ported from Rockbox apps/plugins/dice.c: the dice, their side counts and
 * the roll.
 *
 * Copyright (C) 2005 by Brandon Low
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

#include "dice.h"
#include "eapp.h"

const uint8_t side_counts[SIDE_CHOICES] = {3, 4, 6, 8, 10, 12, 20, 100};

#define INITIAL_COUNT 1
#define INITIAL_SIDES_INDEX 2 /* six sides */

void dice_init(struct dice *dice)
{
    dice->count = INITIAL_COUNT;
    dice->sides_index = INITIAL_SIDES_INDEX;
    dice->total = 0;
}

void dice_clamp(struct dice *dice)
{
    if (dice->sides_index >= SIDE_CHOICES)
        dice->sides_index = INITIAL_SIDES_INDEX;
    if (dice->count < 1)
        dice->count = 1;
    if (dice->count > dice_max_count(dice))
        dice->count = dice_max_count(dice);
}

unsigned dice_sides(const struct dice *dice)
{
    return side_counts[dice->sides_index];
}

unsigned dice_max_count(const struct dice *dice)
{
    return dice_sides(dice) == 100 ? MAX_PERCENTILE_DICE : MAX_DICE;
}

void dice_roll(struct dice *dice)
{
    unsigned sides = dice_sides(dice);
    dice->total = 0;
    for (unsigned i = 0; i < dice->count; ++i) {
        dice->values[i] = eapp_random() % sides + 1;
        dice->total += dice->values[i];
    }
}
