/* SPDX-License-Identifier: GPL-3.0-only */

/* What each ear hears, for the editor's plot: the Custom design of design.c
 * and the Simple loop of crossfeed.c in double precision without a math
 * library, then |H|^2 along a log frequency axis. Only the editor module
 * carries this.
 *
 * The Simple loop follows Rockbox lib/rbcodec/dsp/crossfeed.c
 * (Copyright (C) 2006 Thom Johansen, 2010 Bertrik Sikken, 2012 Michael
 * Sevakis). Modified by the RevolaOS authors, 2026. Rockbox is
 * GPL-2.0-or-later; this version is GPL-3.0-only. */

#include "shared.h"
#include "rv_math.h"

/* |b0 + b1 z^-1 + b2 z^-2|^2 at the angle whose cosines are c and c2 (of twice
 * the angle). */
static double power(double b0, double b1, double b2, double c, double c2)
{
    return b0 * b0 + b1 * b1 + b2 * b2 + 2.0 * (b0 * b1 + b1 * b2) * c + 2.0 * b0 * b2 * c2;
}

static int16_t to_tenths(double power_ratio)
{
    double db = rv_log(power_ratio) * 43.42944819032518;
    return (int16_t)(db + (db < 0.0 ? -0.5 : 0.5));
}

/* The Custom cross path: a first-order high shelf lifting the cross gain to
 * the attenuation, g its half gain, the cutoff divided by g to sit at the
 * -3 dB point and held below 0.45 of the rate. The precut lowers the shelf and
 * the flat direct path; Auto is design.c's: the smallest half-decibel step that
 * keeps the direct gain plus the shelf's summed impulse response at or below 1. */
static void custom(const struct crossfeed_set *set, unsigned int rate, int16_t *same,
                   int16_t *other, unsigned int points, double ratio)
{
    double g = rv_decibels(set->hf_tenths - set->cross_tenths, 400);
    double angle = 3.141592653589793238 * set->cutoff_hz / (g * rate);
    if (angle > 0.45 * 3.141592653589793238)
        angle = 0.45 * 3.141592653589793238;
    double s, c;
    rv_sin_cos(angle, &s, &c);
    double b0 = s + c * g, b1 = s - c * g, a0 = s + c / g, a1 = s - c / g;
    int precut = set->precut_tenths;
    if (precut == CROSSFEED_PRECUT_AUTO) {
        double cross = rv_decibels(set->cross_tenths, 200);
        double c0 = cross * b0 / a0, pole = -a1 / a0, tail = cross * b1 / a0 + pole * c0;
        double sum = rv_decibels(set->direct_tenths, 200) + (c0 < 0 ? -c0 : c0) +
                     (tail < 0 ? -tail : tail) / (1.0 - (pole < 0 ? -pole : pole));
        precut = 0;
        if (sum > 1.0) {
            double half_steps = rv_log(sum) * 17.37177927613007; /* 40 / ln 10 */
            int steps = (int)half_steps;
            if (steps < half_steps)
                steps++;
            precut = -5 * steps;
        }
    }
    double scaler = rv_decibels(set->cross_tenths + precut, 200);
    double hz = 20.0;
    for (unsigned int i = 0; i < points; i++) {
        rv_sin_cos(6.283185307179586477 * hz / rate, &s, &c);
        same[i] = (int16_t)(set->direct_tenths + precut);
        other[i] = to_tenths(scaler * scaler * power(b0, b1, 0.0, c, 0.0) /
                             power(a0, a1, 0.0, c, 0.0));
        hz *= ratio;
    }
}

/* Simple, from the loop in crossfeed.c: with c1 = 2128 / rate and
 * c2 = 1000 / rate, the state d = vcl - vcr follows
 * d[n+1] = (1 - c1) d[n] - 2 c2 (in[n-1] + d[n-1]) for in = L - R, and the
 * left output is L + d / 2, so the cross path is
 * H(z) = c2 z^-2 / (1 - (1 - c1) z^-1 + 2 c2 z^-2) and the own channel 1 - H. */
static void simple(unsigned int rate, int16_t *same, int16_t *other, unsigned int points,
                   double ratio)
{
    double c1 = 2128.0 / rate, c2 = 1000.0 / rate;
    double a1 = -(1.0 - c1), a2 = 2.0 * c2;
    double hz = 20.0;
    for (unsigned int i = 0; i < points; i++) {
        double s, c;
        rv_sin_cos(6.283185307179586477 * hz / rate, &s, &c);
        double c_twice = 2.0 * c * c - 1.0; /* cos 2w */
        double denominator = power(1.0, a1, a2, c, c_twice);
        other[i] = to_tenths(c2 * c2 / denominator);
        /* 1 - H = (1 + a1 z^-1 + (a2 - c2) z^-2) / (1 + a1 z^-1 + a2 z^-2). */
        same[i] = to_tenths(power(1.0, a1, a2 - c2, c, c_twice) / denominator);
        hz *= ratio;
    }
}

void crossfeed_response(const struct crossfeed_set *set, unsigned int rate, int16_t *same,
                        int16_t *other, unsigned int points)
{
    /* 20 Hz to 20 kHz is three decades; each point is one step of that. */
    double ratio = rv_exp(6.907755278982137052 / (points - 1));
    if (set->mode == CROSSFEED_CUSTOM) {
        custom(set, rate, same, other, points, ratio);
    } else if (set->mode == CROSSFEED_SIMPLE) {
        simple(rate, same, other, points, ratio);
    } else {
        for (unsigned int i = 0; i < points; i++) {
            same[i] = 0;
            other[i] = CROSSFEED_NO_PATH;
        }
    }
}
