/* SPDX-License-Identifier: GPL-3.0-only */
#include "doodle.h"

#define TARGET_FRAME_MS 33u /* ~30 FPS matching original MIDP tick */

static uint32_t last_frame_ms;

static void init(void)
{
    if (doodle_init() < 0) {
        eapp_log("doodlejump: failed to initialize assets");
        eapp_request_exit();
        return;
    }
    eapp_random_seed(eapp_ticks_ms() ^ 0x444A4950u);
    last_frame_ms = eapp_ticks_ms();
}

static void shutdown(void)
{
    doodle_shutdown();
}

static void input(const struct eapp_input_event *event)
{
    doodle_on_button(event->button, event->type);
}

static void frame(const struct eapp_frame *frame)
{
    struct eapp_wheel wheel = eapp_read_wheel();
    doodle_on_wheel(&wheel);

    uint32_t now = eapp_ticks_ms();
    doodle_update(now);

    if (frame->framebuffer.pixels)
        doodle_render(&frame->framebuffer);

    uint32_t elapsed = eapp_ticks_ms() - now;
    uint32_t wait_ms = (elapsed < TARGET_FRAME_MS) ? (TARGET_FRAME_MS - elapsed) : 8u;
    if (wait_ms < 8u)
        wait_ms = 8u;
    eapp_sleep_ms(wait_ms);
    last_frame_ms = now;
}

const struct eapp eapp_application = {
    .package_id = "doodlejump",
    .init = init,
    .input = input,
    .frame = frame,
    .shutdown = shutdown,
    .handles_music_buttons = 1,
};

