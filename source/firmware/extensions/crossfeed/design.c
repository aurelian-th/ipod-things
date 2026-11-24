/* SPDX-License-Identifier: GPL-3.0-only */

/* The Custom cross path from the settings, as Rockbox designs it: a bilinear
 * first-order high shelf whose cutoff is moved to the -3 dB point, scaled by
 * the cross gain. Fixed point throughout: gains are read from a quarter-decibel
 * table, the angle's sine and cosine come from a short series, and the one
 * division is a reciprocal by shift and subtract.
 *
 * Follows Rockbox lib/rbcodec/dsp/dsp_filter.c filter_shelf_coefs
 * (Copyright (C) 2006-2007 Thom Johansen) and lib/rbcodec/dsp/crossfeed.c
 * crossfeed_custom_update_filter (Copyright (C) 2006 Thom Johansen, 2010
 * Bertrik Sikken, 2012 Michael Sevakis). Modified by the RevolaOS authors,
 * 2026. Rockbox is GPL-2.0-or-later; this version is GPL-3.0-only. */

#include "crossfeed.h"

enum {
    GAIN_SHIFT = 28,   /* s3.28: the table, the shelf terms */
    TRIG_SHIFT = 30,   /* s1.30: sine and cosine, so 1.0 has room */
    TABLE_FIRST = -48, /* quarter decibels of the first entry, -12 dB */
    TABLE_LAST = 42,   /* +10.5 dB */
    QUARTER_PI_Q29 = 421657428,
    /* 0.45 pi / 4 in Q31: the shelf's cutoff stays below 0.45 of the sample rate. */
    QUARTER_LIMIT_Q31 = 758983371,
};

/* 10 to the power k / 80 for k from TABLE_FIRST to TABLE_LAST, in s3.28: the
 * cross gain, direct gain and the shelf's half gain and its inverse all lie in
 * this range in steps of a quarter decibel. */
static const uint32_t gains[TABLE_LAST - TABLE_FIRST + 1] = {
    67427938, 69396869, 71423294, 73508892, 75655391, 77864568, 80138254, 82478334, 84886745,
    87365482, 89916600, 92542213, 95244494, 98025683, 100888085, 103834070, 106866080,
    109986626, 113198293, 116503743, 119905714, 123407024, 127010574, 130719350, 134536424,
    138464958, 142508208, 146669523, 150952350, 155360238, 159896839, 164565911, 169371322,
    174317054, 179407204, 184645989, 190037749, 195586952, 201298194, 207176207, 213225862,
    219452169, 225860289, 232455528, 239243352, 246229384, 253419412, 260819392, 268435456,
    276273913, 284341257, 292644171, 301189535, 309984429, 319036137, 328352161, 337940217,
    347808249, 357964434, 368417184, 379175160, 390247274, 401642701, 413370879, 425441527,
    437864644, 450650522, 463809755, 477353244, 491292211, 505638202, 520403104, 535599149,
    551238927, 567335394, 583901887, 600952130, 618500249, 636560782, 655148692, 674279380,
    693968693, 714232945, 735088923, 756553907, 778645680, 801382545, 824783337, 848867446,
    873654823, 899166004,
};

static uint32_t gain_quarter_db(int quarters)
{
    return gains[quarters - TABLE_FIRST];
}

static inline int64_t mul(int32_t a, int32_t b)
{
    return (int64_t)a * b;
}

/* dividend / divisor by shift and subtract, for a quotient below 2^32: no
 * 64-bit division from the library. */
static uint32_t divide(uint64_t dividend, uint32_t divisor)
{
    uint64_t remainder = dividend;
    uint32_t quotient = 0;
    for (int bit = 31; bit >= 0; bit--) {
        uint64_t term = (uint64_t)divisor << bit;
        if (remainder >= term) {
            remainder -= term;
            quotient |= 1u << bit;
        }
    }
    return quotient;
}

/* 2^57 / divisor, for a divisor in s3.28 between 0.25 and 4: s2.29. */
static uint32_t reciprocal(uint32_t divisor)
{
    return divide((uint64_t)1 << 57, divisor);
}

/* Sine and cosine of four times x, x in Q31 below 0.7 rad, as s1.30: the
 * series to the thirteenth power for x, then the angle doubled twice. */
static void sine_cosine(uint32_t x, int32_t *sine, int32_t *cosine)
{
    int32_t x2 = (int32_t)(mul((int32_t)x, (int32_t)x) >> 31); /* Q31 */
    int32_t s = (int32_t)(x >> 1), term = s;                   /* Q30 */
    int32_t c = 1 << TRIG_SHIFT, cterm = c;
    for (int n = 1; n <= 6; n++) {
        cterm = (int32_t)(-mul(cterm, x2) >> 31) / ((2 * n - 1) * (2 * n));
        term = (int32_t)(-mul(term, x2) >> 31) / ((2 * n) * (2 * n + 1));
        c += cterm;
        s += term;
    }
    for (int twice = 0; twice < 2; twice++) {
        int32_t s2 = (int32_t)(mul(s, c) >> (TRIG_SHIFT - 1));
        c = (int32_t)((mul(c, c) - mul(s, s)) >> TRIG_SHIFT);
        s = s2;
    }
    *sine = s;
    *cosine = c;
}

void crossfeed_design(struct crossfeed_custom *out, unsigned int rate, int direct_tenths,
                      int cross_tenths, int hf_tenths, unsigned int cutoff_hz,
                      int precut_tenths)
{
    /* All gains come in steps of half a decibel, so the quarter-decibel table
     * holds every gain and every half gain used here. */
    uint32_t scaler = gain_quarter_db(cross_tenths * 2 / 5);
    uint32_t direct = gain_quarter_db(direct_tenths * 2 / 5);

    /* The shelf lifts the cross gain to the attenuation at high frequencies;
     * g is the square root of that gain. The cutoff is divided by g to sit at
     * the -3 dB point. */
    int half = (hf_tenths - cross_tenths) / 5;
    uint32_t g = gain_quarter_db(half), inverse_g = gain_quarter_db(-half);

    /* A quarter of the half angle, pi f / (4 g rate). The deepest shelf cut
     * moves a 2 kHz cutoff to 6.7 kHz, above Nyquist at the low rates, where
     * the bilinear shelf turns unstable; held at 0.45 of the rate the
     * denominator stays positive. */
    uint32_t quarter =
        (uint32_t)(mul((int32_t)crossfeed_ratio_q31(cutoff_hz, rate), QUARTER_PI_Q29) >> 29);
    quarter = (uint32_t)(mul((int32_t)quarter, (int32_t)inverse_g) >> GAIN_SHIFT);
    if (quarter > QUARTER_LIMIT_Q31)
        quarter = QUARTER_LIMIT_Q31;
    int32_t sine, cosine;
    sine_cosine(quarter, &sine, &cosine);

    /* s3.28 shelf terms: b0 = s + c g, b1 = s - c g, a0 = s + c / g, a1 = s - c / g. */
    int32_t s = sine >> (TRIG_SHIFT - GAIN_SHIFT);
    int32_t cg = (int32_t)(mul(cosine, (int32_t)g) >> TRIG_SHIFT);
    int32_t c_over_g = (int32_t)(mul(cosine, (int32_t)inverse_g) >> TRIG_SHIFT);
    int32_t b0 = s + cg, b1 = s - cg, a0 = s + c_over_g, a1 = s - c_over_g;

    /* The cross gain scales the numerator before the division, which keeps
     * every product below full scale. */
    b0 = (int32_t)(mul(b0, (int32_t)scaler) >> GAIN_SHIFT);
    b1 = (int32_t)(mul(b1, (int32_t)scaler) >> GAIN_SHIFT);
    int32_t rcp = (int32_t)reciprocal((uint32_t)a0);
    int32_t c0 = (int32_t)(mul(b0, rcp) >> 26), c1 = (int32_t)(mul(b1, rcp) >> 26);
    int32_t c2 = (int32_t)(-mul(a1, rcp) >> 26);

    /* The precut lowers both paths. The largest output either path can sum to
     * is direct plus the sum of the cross path's impulse response magnitudes:
     * c0, then (c1 + c2 c0) decaying by c2 each sample. With treble above the
     * cross gain that exceeds direct + cross, the bass gain. Auto takes the
     * smallest half-decibel step that keeps the sum at or below 1, so the output
     * never exceeds its input. The sum is at most 2 and the product of two
     * s3.28 values compares against 1 as s6.56. */
    int precut_quarters = precut_tenths * 2 / 5;
    if (precut_tenths == CROSSFEED_PRECUT_AUTO) {
        int64_t tail = c1 + (mul(c2, c0) >> 31);
        uint32_t decay = (1u << 31) - (uint32_t)(c2 < 0 ? -c2 : c2);
        uint64_t cross = (uint64_t)(c0 < 0 ? -(int64_t)c0 : c0) +
                         divide((uint64_t)(tail < 0 ? -tail : tail) << 31, decay);
        int32_t sum = (int32_t)(direct + (uint32_t)(cross >> 3));
        precut_quarters = 0;
        while (mul((int32_t)gain_quarter_db(precut_quarters), sum) > (int64_t)1 << (2 * GAIN_SHIFT))
            precut_quarters -= 2;
    }
    int32_t precut = (int32_t)gain_quarter_db(precut_quarters);
    direct = (uint32_t)(mul((int32_t)direct, precut) >> GAIN_SHIFT);
    out->precut_tenths = (int16_t)(precut_quarters * 5 / 2);
    out->coefs[0] = (int32_t)(mul(c0, precut) >> GAIN_SHIFT);
    out->coefs[1] = (int32_t)(mul(c1, precut) >> GAIN_SHIFT);
    out->coefs[2] = c2;
    out->gain = direct >= (1u << GAIN_SHIFT) ? 0x7fffffff : (int32_t)(direct << 3);
}
