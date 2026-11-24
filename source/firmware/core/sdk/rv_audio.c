/* SPDX-License-Identifier: GPL-3.0-only */

/* The audio helpers too large to inline: the limiter and the fade behind it.
 * Thumb, like the rest of the SDK's sources; the per-sample work is 32-bit. */

#include "rv_audio.h"

/* Pump task only. */
static int32_t wide[RV_LIMITER_CHUNK_FRAMES * 2], previous[RV_LIMITER_CHUNK_FRAMES * 2];
static int16_t limited[RV_LIMITER_CHUNK_FRAMES * 2];

void rv_limiter_run(struct rv_limiter *limiter, const int32_t *in, int16_t *out, uint32_t frames)
{
    int32_t cut = limiter->cut, target = limiter->target, slope = limiter->slope;
    uint32_t hold = limiter->hold, index = limiter->index;
    for (const int32_t *end = in + frames * 2; in < end; in += 2, out += 2) {
        int32_t left = rv_limiter_input(in[0]), right = rv_limiter_input(in[1]);
        uint32_t peak = (uint32_t)(left < 0 ? -left : left);
        uint32_t right_peak = (uint32_t)(right < 0 ? -right : right);
        if (right_peak > peak)
            peak = right_peak;
        if (peak > 32767) {
            int32_t need = RV_LIMITER_UNITY - (int32_t)(((32767u << 15) / peak) << 15);
            if (need > target) {
                target = need;
                /* Rounded up, so the ramp ends before this sample plays. */
                int32_t steep = (target - cut + RV_LIMITER_FRAMES - 1) >> RV_LIMITER_SHIFT;
                if (steep > slope)
                    slope = steep;
            }
            hold = RV_LIMITER_FRAMES + 1;
        }
        if (hold) {
            hold--;
            if (cut < target) {
                cut = cut + slope < target ? cut + slope : target;
            } else {
                /* At the target: a later, deeper over ramps from here. */
                slope = 0;
            }
        } else if (cut) {
            target = slope = 0;
            cut -= (cut >> RV_LIMITER_RELEASE_SHIFT) + 1;
            if (cut < 0)
                cut = 0;
        }

        int32_t *delayed = limiter->delay[index];
        int32_t delayed_left = delayed[0], delayed_right = delayed[1];
        delayed[0] = left;
        delayed[1] = right;
        index = (index + 1) & (RV_LIMITER_FRAMES - 1);
        if (cut) {
            int32_t gain = (RV_LIMITER_UNITY - cut) >> 18;
            delayed_left = (delayed_left * gain + (1 << 11)) >> 12;
            delayed_right = (delayed_right * gain + (1 << 11)) >> 12;
        }
        out[0] = rv_saturate16(delayed_left);
        out[1] = rv_saturate16(delayed_right);
    }
    limiter->cut = cut;
    limiter->target = target;
    limiter->slope = slope;
    limiter->hold = hold;
    limiter->index = index;
}

static inline int32_t rv_fade_weight(const struct rv_fade *fade)
{
    return fade->remaining > RV_FADE_FRAMES ? 0
                                            : (int32_t)(RV_FADE_FRAMES + 1 - fade->remaining);
}

void rv_fade_run_limited(struct rv_fade *fade, struct rv_limiter *limiter, rv_fade_wide_fn run,
                         void *playing, void *fading, int16_t *samples, uint32_t frames)
{
    while (frames) {
        uint32_t chunk = frames < RV_LIMITER_CHUNK_FRAMES ? frames : RV_LIMITER_CHUNK_FRAMES;
        if (!fade->remaining) {
            if (!run(playing, samples, wide, chunk))
                return;
            rv_limiter_run(limiter, wide, samples, chunk);
        } else {
            if (chunk > fade->remaining)
                chunk = fade->remaining;
            int was = run(fading, samples, previous, chunk);
            int now = run(playing, samples, wide, chunk);
            fade->switching = was != now;
            if (was && now) {
                for (uint32_t i = 0; i < chunk * 2; i += 2) {
                    int32_t weight = rv_fade_weight(fade);
                    for (uint32_t n = i; n < i + 2; n++)
                        wide[n] = (rv_limiter_input(previous[n]) * (RV_FADE_FRAMES - weight) +
                                   rv_limiter_input(wide[n]) * weight) >>
                                  RV_FADE_SHIFT;
                    fade->remaining--;
                }
                rv_limiter_run(limiter, wide, samples, chunk);
            } else if (was || now) {
                rv_limiter_run(limiter, was ? previous : wide, limited, chunk);
                for (uint32_t i = 0; i < chunk * 2; i += 2) {
                    int32_t weight = rv_fade_weight(fade);
                    if (was)
                        weight = RV_FADE_FRAMES - weight;
                    for (uint32_t n = i; n < i + 2; n++)
                        samples[n] = (int16_t)((samples[n] * (RV_FADE_FRAMES - weight) +
                                                limited[n] * weight) >>
                                               RV_FADE_SHIFT);
                    fade->remaining--;
                }
            } else {
                fade->remaining -= chunk;
            }
        }
        frames -= chunk;
        samples += chunk * 2;
    }
}
