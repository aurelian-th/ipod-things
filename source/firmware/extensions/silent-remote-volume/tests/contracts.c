/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main.c"

static uint8_t controller[PowerController_sleeping + 4];
static uint8_t event[InputEvent_key + 4];
static int marker_next, next_calls;
static uintptr_t last_controller, last_event;

static void reset_fixtures(uint8_t sleeping, uint32_t kind, uint32_t key)
{
    memset(controller, 0, sizeof controller);
    memset(event, 0, sizeof event);
    RV_FIELD(controller, PowerController_sleeping, uint8_t) = sleeping;
    RV_FIELD(event, InputEvent_kind, uint32_t) = kind;
    RV_FIELD(event, InputEvent_key, uint32_t) = key << 16;
}

static uint32_t stub_hook_next(const void *next, uintptr_t a, uintptr_t b,
                               uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f)
{
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    assert(next == &marker_next);
    next_calls++;
    last_controller = a;
    last_event = b;
    return 1;
}

static const struct rv_api test_api = {
    .hook_next = stub_hook_next,
};

int main(void)
{
    static const uint32_t suppressed_keys[] = {
        UIKey_MikeyVolumeDown,
        UIKey_MikeyVolumeUp,
        UIKey_RemoteVolumeDown,
        UIKey_RemoteVolumeUp,
    };
    static const uint32_t button_phases[] = {
        InputEvent_ButtonDown,
        7,
        InputEvent_ButtonPressAndHold,
        InputEvent_ButtonUp,
    };

    assert(rv_module_entry(&test_api, NULL) == &extension);

    /* Wired headphone and dock remote volume buttons never wake when active. */
    for (size_t i = 0; i < sizeof suppressed_keys / sizeof suppressed_keys[0]; i++) {
        for (size_t j = 0; j < sizeof button_phases / sizeof button_phases[0]; j++) {
            reset_fixtures(0, button_phases[j], suppressed_keys[i]);
            assert(on_filter_event((uintptr_t)controller, (uintptr_t)event, &marker_next) == 0);
            assert(next_calls == 0);
        }
    }

    /* Non-button phases and other keys still reach Apple's filter. */
    reset_fixtures(0, InputEvent_Wheel, UIKey_MikeyVolumeUp);
    assert(on_filter_event((uintptr_t)controller, (uintptr_t)event, &marker_next) == 1);
    assert(next_calls == 1);

    reset_fixtures(0, InputEvent_ButtonUp, UIKey_MikeyCenter);
    assert(on_filter_event((uintptr_t)controller, (uintptr_t)event, &marker_next) == 1);
    assert(next_calls == 2);

    reset_fixtures(0, InputEvent_ButtonUp, UIKey_Menu);
    assert(on_filter_event((uintptr_t)controller, (uintptr_t)event, &marker_next) == 1);
    assert(next_calls == 3);

    reset_fixtures(0, InputEvent_ButtonUp, UIKey_RemotePlayPause);
    assert(on_filter_event((uintptr_t)controller, (uintptr_t)event, &marker_next) == 1);
    assert(next_calls == 4);

    /* While actually sleeping, Apple handles wake behavior normally. */
    reset_fixtures(1, InputEvent_ButtonUp, UIKey_MikeyVolumeUp);
    assert(on_filter_event((uintptr_t)controller, (uintptr_t)event, &marker_next) == 1);
    assert(next_calls == 5);
    assert(last_controller == (uintptr_t)controller && last_event == (uintptr_t)event);

    /* Null arguments chain safely without dereferencing. */
    assert(on_filter_event(0, (uintptr_t)event, &marker_next) == 1);
    assert(next_calls == 6);
    assert(on_filter_event((uintptr_t)controller, 0, &marker_next) == 1);
    assert(next_calls == 7);

    puts("Silent Remote Volume contracts passed");
    return 0;
}

