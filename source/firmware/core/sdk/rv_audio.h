/* SPDX-License-Identifier: GPL-3.0-only */

/* What an audio effect shares with the others: the output format behind the
 * PCM site, the fixed-point steps of a sample loop, the handoff of a set of
 * values from the task that chose them to the pump task that plays them, the
 * fade from the state that played to the one that takes over, and the
 * look-ahead limiter that keeps peaks under full scale (rv_audio.c). */

#ifndef RV_AUDIO_H
#define RV_AUDIO_H
#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_libc.h"

/* The output of the AudioEffectsContext a filter on
 * RV_SITE_AudioOutputQueue_Get_call receives as its third argument: 1 when
 * it is 16-bit stereo, with *rate its sample rate. */
static inline int rv_audio_stereo16(const void *context, uint32_t *rate)
{
    *rate = RV_FIELD(context, AudioEffectsContext_output_rate, uint32_t);
    return RV_FIELD(context, AudioEffectsContext_output_bits, uint32_t) == 16 &&
           RV_FIELD(context, AudioEffectsContext_output_channels, uint32_t) == 2;
}

/* a times b, both Q31. One smull in ARM, a library call in Thumb. */
static inline int32_t rv_fracmul(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 31);
}

static inline int16_t rv_saturate16(int32_t sample)
{
    return sample > 32767 ? 32767 : sample < -32768 ? -32768 : (int16_t)sample;
}

/* A set of values left for the pump task. The task that chose them copies
 * them into `next` with interrupts masked and moves the generation; the pump
 * task, which alone reads `taken`, copies `next` into `playing` once it sees
 * the generation move, and again when it moved under the copy. */
struct rv_handoff {
    volatile uint32_t generation;
    uint32_t taken;
};

static inline void rv_handoff_publish(const struct rv_api *api, struct rv_handoff *handoff,
                                      void *next, const void *set, uint32_t bytes)
{
    uint32_t state = api->irq_save();
    memcpy(next, set, bytes);
    handoff->generation++;
    api->irq_restore(state);
}

static inline int rv_handoff_pending(const struct rv_handoff *handoff)
{
    return handoff->taken != handoff->generation;
}

static inline void rv_handoff_take(struct rv_handoff *handoff, void *playing, const void *next,
                                   uint32_t bytes)
{
    uint32_t seen;
    do {
        seen = handoff->generation;
        memcpy(playing, next, bytes);
    } while (seen != handoff->generation);
    handoff->taken = seen;
}

/* A state that takes over at a buffer edge steps the output, which clicks.
 * An effect keeps the state that played as a second instance and fades its
 * output into the new one's over RV_FADE_FRAMES frames, in chunks of
 * RV_FADE_CHUNK_FRAMES. `lead` frames before the fade play the old state alone
 * while both run, for a new state that must fill (a delay line) before it is
 * heard. A new fade restarts from what plays; `remaining = 0` cancels one. */
enum { RV_FADE_SHIFT = 7, RV_FADE_FRAMES = 1 << RV_FADE_SHIFT, RV_FADE_CHUNK_FRAMES = 32 };

struct rv_fade {
    uint32_t remaining; /* lead frames at weight 0, then RV_FADE_FRAMES of blend */
    uint32_t switching; /* rv_fade_run_limited: one state passes the audio through */
};

/* Runs `state` on `frames` interleaved stereo S16 frames in place. */
typedef void (*rv_fade_run_fn)(void *state, int16_t *samples, uint32_t frames);

static inline void rv_fade_start(struct rv_fade *fade, uint32_t lead)
{
    fade->remaining = RV_FADE_FRAMES + lead;
}

/* Runs `playing` over the buffer; while a fade lasts, `fading` too, and mixes
 * the two. The weight runs 1 to RV_FADE_FRAMES so the last faded frame is the
 * new state alone and joins the rest without a step. */
static inline void rv_fade_run(struct rv_fade *fade, rv_fade_run_fn run, void *playing,
                               void *fading, int16_t *samples, uint32_t frames)
{
    while (fade->remaining && frames) {
        int16_t previous[RV_FADE_CHUNK_FRAMES * 2];
        uint32_t chunk = frames < RV_FADE_CHUNK_FRAMES ? frames : RV_FADE_CHUNK_FRAMES;
        if (chunk > fade->remaining)
            chunk = fade->remaining;
        memcpy(previous, samples, chunk * 2 * sizeof *samples);
        run(fading, previous, chunk);
        run(playing, samples, chunk);
        for (uint32_t i = 0; i < chunk * 2; i += 2) {
            int32_t weight = fade->remaining > RV_FADE_FRAMES
                                 ? 0
                                 : (int32_t)(RV_FADE_FRAMES + 1 - fade->remaining);
            for (uint32_t n = i; n < i + 2; n++)
                samples[n] = (int16_t)((previous[n] * (RV_FADE_FRAMES - weight) +
                                        samples[n] * weight) >>
                                       RV_FADE_SHIFT);
            fade->remaining--;
        }
        frames -= chunk;
        samples += chunk * 2;
    }
    run(playing, samples, frames);
}

/* Peaks over full scale, pulled down before they play instead of clipped. An
 * effect that can go over keeps its output wide, int32 at the S16 scale, and
 * the limiter plays it RV_LIMITER_FRAMES frames late. A sample over full scale
 * sets the gain that brings it to full scale; the gain ramps there linearly
 * over the frames before that sample plays, holds until the last over has
 * played, and returns to unity with a time constant of
 * 1 << RV_LIMITER_RELEASE_SHIFT frames. One gain serves both channels. At unity
 * the samples pass unchanged. A zeroed limiter is at rest. */
enum {
    RV_LIMITER_SHIFT = 5,
    RV_LIMITER_FRAMES = 1 << RV_LIMITER_SHIFT, /* about 0.7 ms at 44.1 kHz */
    RV_LIMITER_RELEASE_SHIFT = 12,             /* about 90 ms at 44.1 kHz */
    RV_LIMITER_INPUT = (1 << 19) - 1,          /* the widest sample taken: +24 dB */
    RV_LIMITER_CHUNK_FRAMES = 16,
};
#define RV_LIMITER_UNITY (1 << 30)

struct rv_limiter {
    int32_t delay[RV_LIMITER_FRAMES][2];
    uint32_t index;
    int32_t cut, target, slope; /* the gain below unity, Q30 */
    uint32_t hold;              /* frames until the last over has played */
};

static inline void rv_limiter_rest(struct rv_limiter *limiter)
{
    memset(limiter, 0, sizeof *limiter);
}

static inline int32_t rv_limiter_input(int32_t sample)
{
    return sample > RV_LIMITER_INPUT    ? RV_LIMITER_INPUT
           : sample < -RV_LIMITER_INPUT ? -RV_LIMITER_INPUT
                                        : sample;
}

/* Takes `frames` wide stereo frames and writes as many S16 frames, delayed.
 * The gain multiply stays 32-bit, a Q12 gain times at most 2^19, and only an
 * over divides. */
void rv_limiter_run(struct rv_limiter *limiter, const int32_t *in, int16_t *out, uint32_t frames);

/* Runs `state` on `frames` S16 frames from `in`, at most
 * RV_LIMITER_CHUNK_FRAMES, into wide frames in `out`. Returns 0, with `out`
 * untouched, for a state that passes the audio through unchanged (Off). */
typedef int (*rv_fade_wide_fn)(void *state, const int16_t *in, int32_t *out, uint32_t frames);

/* rv_fade_run for an effect behind a limiter. While both states run, their
 * wide outputs blend before the limiter, so the fade carries no delay of its
 * own. While one passes the audio through, the limited output of the other
 * blends with the undelayed buffer: an effect that turns on rests the limiter
 * and starts the fade with a lead of at least RV_LIMITER_FRAMES, so the blend
 * starts after the limiter has filled. Nothing runs per sample while
 * `playing` passes through and no fade lasts. */
void rv_fade_run_limited(struct rv_fade *fade, struct rv_limiter *limiter, rv_fade_wide_fn run,
                         void *playing, void *fading, int16_t *samples, uint32_t frames);

/* 1 while a fade between a state that passes the audio through and one that
 * runs lasts. Its two outputs are 0.7 ms apart, so a take restarting it then
 * would step the output: an effect leaves a pending set until this clears. */
static inline int rv_fade_switching(const struct rv_fade *fade)
{
    return fade->remaining && fade->switching;
}

#endif
