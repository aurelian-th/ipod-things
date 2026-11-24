/* SPDX-License-Identifier: GPL-3.0-only */

/* The biquad cascade. Apple's saturates to 16 bits after every stage, so a boost
 * clips before a later cut or the next stage's precut can undo it. This one
 * carries samples with nine fraction bits in 32, which leaves 42 dB of headroom
 * between stages, and leaves its output wide for the limiter. */

#include "engine.h"

enum {
    HALF_STEP = 1 << 23, /* the error at rest, so the first output rounds */
};

void eq_cascade_rest(struct eq_stage *stages, unsigned int count)
{
    for (unsigned int i = 0; i < count; i++)
        for (unsigned int channel = 0; channel < 2; channel++) {
            for (unsigned int n = 0; n < 4; n++)
                stages[i].history[channel][n] = 0;
            stages[i].error[channel] = HALF_STEP;
        }
}

/* Direct form I. The 24 bits each output drops go into the next one (first-order
 * error feedback), so low-frequency poles do not amplify the rounding noise. */
static void run(struct eq_stage *stage, int32_t *samples, unsigned int frames)
{
    const int32_t b0 = stage->b0, b1 = stage->b1, b2 = stage->b2, a1 = stage->a1, a2 = stage->a2;
    for (unsigned int channel = 0; channel < 2; channel++) {
        int32_t *history = stage->history[channel];
        int32_t x1 = history[0], x2 = history[1], y1 = history[2], y2 = history[3];
        int32_t e = stage->error[channel];
        for (int32_t *sample = samples + channel, *end = samples + frames * 2; sample < end;
             sample += 2) {
            int32_t x = *sample;
            int64_t sum = (int64_t)b0 * x + (int64_t)b1 * x1 + (int64_t)b2 * x2 +
                          (int64_t)a1 * y1 + (int64_t)a2 * y2 + e;
            int64_t scaled = sum >> 24;
            int32_t y = (int32_t)scaled;
            e = (int32_t)sum & 0xffffff;
            if (scaled != y) {
                y = scaled < 0 ? INT32_MIN : INT32_MAX;
                e = HALF_STEP;
            }
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            *sample = y;
        }
        history[0] = x1;
        history[1] = x2;
        history[2] = y1;
        history[3] = y2;
        stage->error[channel] = e;
    }
}

void eq_cascade(struct eq_stage *stages, unsigned int count, const int16_t *in, int32_t *out,
                uint32_t frames)
{
    for (unsigned int i = 0; i < frames * 2; i++)
        out[i] = (int32_t)in[i] * 512;
    for (unsigned int i = 0; i < count; i++)
        if (!stages[i].bypass)
            run(&stages[i], out, frames);
    for (unsigned int i = 0; i < frames * 2; i++)
        out[i] = (out[i] >> 9) + ((out[i] >> 8) & 1);
}
