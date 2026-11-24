/* SPDX-License-Identifier: GPL-3.0-only */

/* rv_limiter and rv_fade_run_limited on the host. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rv_audio.h"

enum { D = RV_LIMITER_FRAMES, FRAMES = 1 << 17 };

static int16_t out[FRAMES * 2];
static int32_t wide[FRAMES * 2];

static void limit(struct rv_limiter *limiter, uint32_t frames)
{
    for (uint32_t done = 0; done < frames; done += RV_LIMITER_CHUNK_FRAMES)
        rv_limiter_run(limiter, wide + done * 2, out + done * 2, RV_LIMITER_CHUNK_FRAMES);
}

/* Under full scale the output is the input, D frames late. */
static void passes_unchanged(void)
{
    struct rv_limiter limiter = {0};
    srand(1);
    for (uint32_t i = 0; i < 4096 * 2; i++)
        wide[i] = rand() % 65535 - 32767;
    limit(&limiter, 4096);
    for (uint32_t i = 0; i < D * 2; i++)
        assert(out[i] == 0);
    for (uint32_t i = D * 2; i < 4096 * 2; i++)
        assert(out[i] == wide[i - D * 2]);
}

/* A lone over plays at full scale with both channels scaled alike, the gain
 * eases down over the D frames before it and back up after. */
static void scales_an_over(void)
{
    struct rv_limiter limiter = {0};
    const uint32_t spike = 1000, frames = 1 << 17;
    for (uint32_t i = 0; i < frames; i++) {
        wide[i * 2] = i == spike ? 40000 : 10000;
        wide[i * 2 + 1] = i == spike ? -30000 : -10000;
    }
    limit(&limiter, frames);
    const int16_t *at = out + (spike + D) * 2;
    assert(at[0] >= 32755 && at[0] <= 32767); /* a Q12 gain: within 8 LSB */
    assert(abs(at[1] - (-30000 * 32767 / 40000)) <= 8);
    /* Unity before the over enters, a monotonic ramp to it, a hold. */
    for (uint32_t i = D; i < spike; i++)
        assert(out[i * 2] == 10000);
    for (uint32_t i = spike + 1; i < spike + D; i++)
        assert(out[i * 2] <= out[(i - 1) * 2] && out[i * 2 + 1] == -out[i * 2]);
    assert(abs(out[(spike + D - 1) * 2] - 10000 * 32767 / 40000) <= 3);
    assert(abs(out[(spike + D + 1) * 2] - out[(spike + D - 1) * 2]) <= 1);
    /* Release: rising, about 63 % of the way back after 4096 frames, unity at last. */
    for (uint32_t i = spike + D + 2; i < frames; i++)
        assert(out[i * 2] >= out[(i - 1) * 2] && out[i * 2] <= 10000);
    int32_t low = 10000 * 32767 / 40000, back = out[(spike + D + 1 + 4096) * 2] - low;
    assert(back > (10000 - low) * 55 / 100 && back < (10000 - low) * 70 / 100);
    assert(out[(frames - 1) * 2] == 10000 && limiter.cut == 0);
}

/* Every over plays at a gain that brings its frame's peak to full scale or
 * below, and the output never reaches -32768. */
static void check_bound(uint32_t frames)
{
    for (uint32_t i = D; i < frames; i++) {
        const int32_t *x = wide + (i - D) * 2;
        const int16_t *y = out + i * 2;
        int32_t a = rv_limiter_input(x[0]), b = rv_limiter_input(x[1]);
        int32_t peak = abs(a) > abs(b) ? abs(a) : abs(b);
        for (int c = 0; c < 2; c++) {
            assert(y[c] > -32768);
            if (peak > 32767)
                assert((int64_t)abs(y[c]) * peak <= (int64_t)32767 * abs(c ? b : a) + peak);
        }
    }
}

/* Noise up to +24 dB; rising overs at gaps of 1 to 64 frames; overs during
 * the release; a sawtooth to three times full scale; input past the clamp. */
static void bounds_overs(void)
{
    struct rv_limiter limiter = {0};
    srand(2);
    for (uint32_t i = 0; i < FRAMES * 2; i++)
        wide[i] = (rand() % (2 * RV_LIMITER_INPUT + 1)) - RV_LIMITER_INPUT;
    limit(&limiter, FRAMES);
    check_bound(FRAMES);

    rv_limiter_rest(&limiter);
    uint32_t i = 0;
    for (uint32_t gap = 1; gap <= 64 && i < FRAMES; gap++)
        for (int32_t level = 33000; level < 400000 && i < FRAMES; level += 9000)
            for (uint32_t n = 0; n < gap && i < FRAMES; n++, i++) {
                wide[i * 2] = n ? 20000 : level;
                wide[i * 2 + 1] = n ? -20000 : -(level / 2);
            }
    for (; i < FRAMES; i++) {
        uint32_t phase = i % 6000;
        wide[i * 2] = phase == 0 ? 60000 : phase == 3000 ? 40000 : (int32_t)(i % 300) * 330 - 49000;
        wide[i * 2 + 1] = i % 9000 ? 1000 : 2000000;
    }
    limit(&limiter, FRAMES);
    check_bound(FRAMES);
}

struct state {
    int on;
    uint32_t calls;
};

/* On: the input unchanged, so the limiter is a pure delay and every output
 * frame has one expected value. */
static int run(void *instance, const int16_t *in, int32_t *wide_out, uint32_t frames)
{
    struct state *state = instance;
    state->calls++;
    if (!state->on)
        return 0;
    for (uint32_t i = 0; i < frames * 2; i++)
        wide_out[i] = in[i];
    return 1;
}

/* Off to on, on to off and on to on, on noise in buffers of uneven sizes:
 * every output frame is the dry input, the input D frames earlier or the
 * blend of the two at its weight, and nothing else replays or drops. */
static void fades(void)
{
    static const int changes[][2] = {{0, 1}, {1, 0}, {1, 1}};
    static const uint32_t sizes[] = {7, 50, 13, 1, 16, 33, 100, 3, 64, 200};
    enum { TOTAL = 1200, SWITCH = 300 };
    static int16_t input[TOTAL * 2], output[TOTAL * 2];
    srand(3);
    for (uint32_t i = 0; i < TOTAL * 2; i++)
        input[i] = (int16_t)(rand() % 40001 - 20000);
    for (unsigned int k = 0; k < 3; k++) {
        struct rv_limiter limiter = {0};
        struct rv_fade fade = {0};
        struct state fading = {changes[k][0], 0}, playing = {changes[k][0], 0};
        uint32_t lead = changes[k][1] && !changes[k][0] ? D : 0;
        memcpy(output, input, sizeof output);
        for (uint32_t done = 0, n = 0; done < TOTAL; n++) {
            uint32_t size = sizes[n % 10];
            if (done < SWITCH && done + size > SWITCH)
                size = SWITCH - done;
            if (done + size > TOTAL)
                size = TOTAL - done;
            if (done == SWITCH) {
                fading = playing;
                playing.on = changes[k][1];
                if (lead)
                    rv_limiter_rest(&limiter);
                rv_fade_start(&fade, lead);
            }
            rv_fade_run_limited(&fade, &limiter, run, &playing, &fading, output + done * 2, size);
            if (done >= SWITCH && done + size < SWITCH + lead + RV_FADE_FRAMES)
                assert(rv_fade_switching(&fade) == (changes[k][0] != changes[k][1]));
            done += size;
        }
        assert(!fade.remaining && !rv_fade_switching(&fade));
        for (uint32_t i = D; i < TOTAL; i++)
            for (int c = 0; c < 2; c++) {
                int32_t dry = input[i * 2 + c], late = input[(i - D) * 2 + c], expected;
                int was = changes[k][0], now = changes[k][1];
                int32_t blend = (int32_t)i - (int32_t)(SWITCH + lead);
                if (i < SWITCH)
                    expected = was ? late : dry;
                else if (blend < 0)
                    expected = was ? late : dry; /* the lead: what played */
                else if (blend >= RV_FADE_FRAMES)
                    expected = now ? late : dry;
                else if (was == now)
                    expected = late;
                else {
                    int32_t weight = blend + 1; /* toward the new state */
                    int32_t old_value = was ? late : dry, new_value = now ? late : dry;
                    expected = (old_value * (RV_FADE_FRAMES - weight) + new_value * weight) >>
                               RV_FADE_SHIFT;
                }
                assert(output[i * 2 + c] == expected);
            }
    }
    /* Off with no fade runs once per call, not per chunk. */
    struct rv_limiter limiter = {0};
    struct rv_fade fade = {0};
    struct state off = {0, 0};
    int16_t buffer[512 * 2] = {0};
    rv_fade_run_limited(&fade, &limiter, run, &off, &off, buffer, 512);
    assert(off.calls == 1);
}

int main(void)
{
    passes_unchanged();
    scales_an_over();
    bounds_overs();
    fades();
    puts("rv_audio: limiter and limited fade");
    return 0;
}
