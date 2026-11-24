# Audio effects

An audio effect filters the output PCM at one core site, `RV_SITE_AudioOutputQueue_Get_call`, after Apple's EQ. It applies to everything the iPod plays. Bind with `RV_HOOK_FILTER_AT` and the priority of the effect's stage, so the order is fixed whichever effects are enabled.

| Stage | Priority |
|---|---|
| `RV_AUDIO_STAGE_CROSSFEED` | 100 |
| `RV_AUDIO_STAGE_TONE` | 200 |
| `RV_AUDIO_STAGE_BASS_ENHANCEMENT` | 300 |
| `RV_AUDIO_STAGE_FATIGUE_REDUCTION` | 400 |
| `RV_AUDIO_STAGE_SURROUND` | 500 |
| `RV_AUDIO_STAGE_CHANNELS` | 600 |
| `RV_AUDIO_STAGE_COMPRESSOR` | 700 |
| `RV_AUDIO_STAGE_DITHER` | 900 |

```c
static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER_AT(RV_SITE_AudioOutputQueue_Get_call, on_output, RV_AUDIO_STAGE_COMPRESSOR),
};
```

The stream is 16-bit stereo, which is everything the iPod plays, at the output sample rate of 8 to 48 kHz. An effect whose mode is Off should pass the buffer through and cost nothing per sample. Loops that run per sample are built as ARM: a 32 by 32 to 64 bit multiply is one `smull` there and a library call in Thumb; see [Modules](modules.md).

## Shared helpers

`rv_audio.h` holds what the effects have in common, as inline functions. The limiter and the fade behind it are in `rv_audio.c`, which every module links; a module that does not call them carries none of it.

| Call | |
|---|---|
| `rv_audio_stereo16(context, &rate)` | 1 when the output behind the site's third argument is 16-bit stereo, with `rate` its sample rate |
| `rv_fracmul(a, b)` | a times b, both Q31 |
| `rv_saturate16(sample)` | clamped to S16 |
| `struct rv_handoff`, `rv_handoff_publish(api, &handoff, &next, set, bytes)`, `rv_handoff_pending(&handoff)`, `rv_handoff_take(&handoff, &playing, &next, bytes)` | a set of values the UI task leaves for the pump task: `publish` copies it into `next` with interrupts masked and moves the generation; the pump task calls `take` when `pending` says the generation moved, which copies `next` into `playing` and again when it moved under the copy |
| `struct rv_fade`, `rv_fade_start(&fade, lead)`, `rv_fade_run(&fade, run, &playing, &fading, samples, frames)` | the fade from the state that played to the one that takes over: `run` processes a state over a buffer in place; `rv_fade_run` runs `playing`, and while a fade lasts runs `fading` on a copy and mixes the two |
| `struct rv_limiter`, `rv_limiter_rest(&limiter)`, `rv_limiter_run(&limiter, wide, samples, frames)` | a look-ahead peak limiter: takes int32 frames at the S16 scale that may exceed it and writes S16 frames `RV_LIMITER_FRAMES` (32) late, the gain eased down before an over plays |
| `rv_fade_run_limited(&fade, &limiter, run, &playing, &fading, samples, frames)`, `rv_fade_switching(&fade)` | `rv_fade_run` for an effect behind a limiter: `run` writes a state's wide output and returns 0 for a state that passes the audio through; `switching` is 1 while a fade between such a state and a running one lasts |

```c
static uint32_t on_output(uint32_t samples, uint32_t frames, uint32_t context, const void *next)
{
    uint32_t rate;
    if (frames && rv_audio_stereo16((const void *)context, &rate))
        effect_process((int16_t *)samples, frames, rate);
    return api->hook_next(next, samples, frames, context, 0, 0, 0);
}
```

## Changing a set while audio plays

A set taken at a buffer edge steps the output, and every wheel step clicks. An effect keeps the state that played as a second instance: in its take it copies `playing` into `fading`, installs the new set in `playing` and calls `rv_fade_start`. `rv_fade_run` then blends the old instance's output into the new one's over `RV_FADE_FRAMES` frames, 128, about 3 ms at 44.1 kHz, in chunks of `RV_FADE_CHUNK_FRAMES`. A new state that must fill before it is heard, such as a look-ahead delay line, passes its length as `lead`: those frames play the old state alone while both run. Fade on a change of values, not on a change of sample rate, which comes with a track: clear the state and set `remaining` to 0. A set published while a fade lasts restarts the fade from what plays.

## Keeping peaks under full scale

A precut that removes an effect's largest gain at any frequency still lets some samples over: filters shift the phase of each band differently, so peaks the input kept apart can line up. Saturating those samples clips. An effect that can go over writes its output wide instead, int32 at the S16 scale, and hands it to an `rv_limiter`. A sample over full scale sets the gain that brings it to full scale. The gain ramps down linearly over the 32 frames before that sample plays, holds until the last over has played and returns to unity with a time constant of 4,096 frames, about 90 ms at 44.1 kHz. Both channels share the gain, so the image does not move. At unity the samples pass unchanged, 32 frames late. Input past +24 dB is clamped before it is limited. A zeroed limiter is at rest.

With a second state for fades, call `rv_fade_run_limited` instead of `rv_fade_run`. Its `run` reads S16 from `in`, writes at most `RV_LIMITER_CHUNK_FRAMES` wide frames to `out` and returns 1, or returns 0 without writing for a state that passes the audio through, such as Off. While both states run, their wide outputs blend before the one limiter, so a fade adds no delay and nothing is copied between states. When one of the two passes the audio through, the other's limited output blends with the undelayed buffer. An effect that turns on rests the limiter and starts the fade with a lead of at least `RV_LIMITER_FRAMES`, so the blend starts once the limiter has filled. While `playing` passes through and no fade lasts, nothing runs per sample.

Turning on or off moves the output by the limiter's 32 frames: turning on repeats them and turning off skips them, both inside the blend. Mixing a signal with itself 32 frames apart is a comb filter, with notches every 1,378 Hz from 689 Hz at 44.1 kHz, so the 128-frame blend is not seamless, only short. `rv_fade_switching(&fade)` is 1 while such a blend lasts; restarting it would step the output, so take a pending set only once it is 0:

```c
if ((rv_handoff_pending(&handoff) && !rv_fade_switching(&fade)) || rate != rate_playing)
    take(rate);
```

```c
static int run(void *state, const int16_t *in, int32_t *out, uint32_t frames)
{
    if (((struct state *)state)->mode == MODE_OFF)
        return 0;
    /* ... out[i] = wide result, rounded to the S16 scale ... */
    return 1;
}

rv_fade_run_limited(&fade, &limiter, run, &playing, &fading, samples, frames);
```

Filter designs and the curves an editor draws use `rv_math.h`: `rv_exp`, `rv_log`, `rv_sin_cos`, `rv_sqrt` and `rv_decibels` in double precision without a math library, for the UI task only.
