/* SPDX-License-Identifier: GPL-3.0-only */
/* The dice as vertex arrays, written by solids.py into solids.c. */

#ifndef SOLIDS_H
#define SOLIDS_H

#include <stddef.h>

#include "eapp_gl.h"

#define ATLAS_WIDTH 512
#define ATLAS_HEIGHT 256
/* Atlas texels per unit of a solid. main.c draws a resting die at this many
 * pixels per unit, so a top face shows its label pixel for pixel. */
#define SOLID_TEXELS 32
#define SHADOW_RIM 24
/* The most triangles a solid may have; solids.py keeps to it. */
#define SOLID_TRIANGLES 156

enum solid_kind { D4, D6, D3, D8, D10, D10_TENS, D10_UNITS, D12, D20, SOLIDS };

/* A die as indexed triangles in 16.16 fixed point, lengths in the units of
 * the vertices. Faces are flat; the rounded edges and corners between them
 * sample the atlas's white texel. Solids of the same shape share vertices,
 * normals and indices. */
struct solid {
    const GLfixed *vertices;
    const GLfixed *normals;
    const GLubyte *indices;
    const GLfixed *uvs;
    /* rest[value - 1], or rest[value] on the percentile dice, turns a result
     * towards the camera, label upright; the d4's result corner points at
     * it. */
    const GLfixed (*rest)[16];
    /* The shadow's radius at rest every 360 / SHADOW_RIM degrees from the
     * felt's x axis, or NULL for a disc. */
    const GLfloat *shadow;
    unsigned index_count;
    GLfloat disc;   /* shadow radius in the air, and at rest without a rim */
    GLfloat height; /* of the centre above the felt at rest */
    GLfloat tilt;   /* degrees the camera tilts back once the roll settles */
};

/* One label in the atlas: centred at x, y of its cell and turned
 * counter-clockwise from upright by the angle whose cosine and sine are
 * cos64 / 64 and sin64 / 64. size is the font's pixel in quarter texels: an
 * upright multiple of 4 draws the font pixel-sharp, others smoothed. A label
 * with pips draws that many pips instead of text, size texels apart. */
struct solid_label {
    uint16_t cell_x, cell_y;
    uint8_t x, y;
    uint8_t size;
    int8_t cos64, sin64;
    uint8_t pips;
    uint8_t underline;
    char text[3];
};

extern const struct solid solids[SOLIDS];
extern const struct solid_label solid_labels[];
extern const unsigned solid_label_count;
/* The directions of a shadow's rim, x, y pairs. */
extern const GLfloat shadow_directions[SHADOW_RIM * 2];

#endif
