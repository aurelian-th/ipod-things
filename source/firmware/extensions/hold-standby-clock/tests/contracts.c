/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main.c"

static int marker_system, marker_next, marker_bl_next, marker_standby_next;
static int marker_registry, marker_screen, marker_navigator;
static uint8_t fake_controller[PowerController_sleeping + 4];
static void *standby_seen_in_bl_off;
static int next_calls, bl_next_calls, standby_next_calls;
static int create_calls, push_calls;
static int return_null_controller, return_null_registry, return_null_screen, return_null_navigator;
static uintptr_t last_system;
static uint32_t last_locked, last_screen_id, last_layout_id, last_transition, last_push_flags;
static void *last_push_nav, *last_push_ctrl;

void *PowerController_Get(void)
{
    return return_null_controller ? NULL : fake_controller;
}

void *UI_GetControllerRegistry(void)
{
    return return_null_registry ? NULL : &marker_registry;
}

void *UI_CreateScreenController(void *registry, uint32_t screen_id, uint32_t layout_id,
                                uint32_t transition)
{
    assert(registry == &marker_registry);
    create_calls++;
    last_screen_id = screen_id;
    last_layout_id = layout_id;
    last_transition = transition;
    return return_null_screen ? NULL : &marker_screen;
}

void *Navigator_Get(void)
{
    return return_null_navigator ? NULL : &marker_navigator;
}

void Navigator_Push(void *navigator, void *controller, uint32_t flags)
{
    push_calls++;
    last_push_nav = navigator;
    last_push_ctrl = controller;
    last_push_flags = flags;
}

static uint32_t stub_hook_next(const void *next, uintptr_t a, uintptr_t b,
                               uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f)
{
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    if (next == &marker_bl_next) {
        assert((void *)a == fake_controller);
        bl_next_calls++;
        standby_seen_in_bl_off =
            RV_FIELD(fake_controller, PowerController_standby_controller, void *);
        return 11;
    }
    if (next == &marker_standby_next) {
        assert((void *)a == fake_controller);
        standby_next_calls++;
        return 22;
    }
    assert(next == &marker_next);
    next_calls++;
    last_system = a;
    last_locked = (uint32_t)b;
    return 77;
}

static const struct rv_api test_api = {
    .hook_next = stub_hook_next,
};

int main(void)
{
    memset(fake_controller, 0, sizeof fake_controller);
    assert(rv_module_entry(&test_api, NULL) == &extension);

    /* Without Hold locked, EnterBacklightOff passes standby_controller through untouched. */
    RV_FIELD(fake_controller, PowerController_standby_controller, void *) = &marker_screen;
    assert(on_enter_backlight_off((uintptr_t)fake_controller, &marker_bl_next) == 11);
    assert(bl_next_calls == 1 && standby_seen_in_bl_off == &marker_screen);

    /* Unlocking Hold chains without creating or pushing a standby clock. */
    RV_FIELD(fake_controller, PowerController_standby_controller, void *) = NULL;
    assert(on_hold_change((uintptr_t)&marker_system, 0, &marker_next) == 77);
    assert(next_calls == 1 && last_system == (uintptr_t)&marker_system && last_locked == 0);
    assert(create_calls == 0 && push_calls == 0 && hold_locked == 0);

    /* Engaging Hold chains first, creates NowPlaying_Idle, records it, and pushes it. */
    assert(on_hold_change((uintptr_t)&marker_system, 1, &marker_next) == 77);
    assert(next_calls == 2 && last_system == (uintptr_t)&marker_system && last_locked == 1);
    assert(hold_locked == 1 && create_calls == 1 && push_calls == 1);
    assert(last_screen_id == RES_NowPlaying_Idle_Screen);
    assert(last_layout_id == RES_NowPlaying_Idle_Layout);
    assert(last_transition == 0);
    assert(RV_FIELD(fake_controller, PowerController_standby_controller, void *) == &marker_screen);
    assert(last_push_nav == &marker_navigator && last_push_ctrl == &marker_screen);
    assert(last_push_flags == 0);

    /* While Hold is locked, EnterBacklightOff hides standby_controller during the call and restores it. */
    assert(on_enter_backlight_off((uintptr_t)fake_controller, &marker_bl_next) == 11);
    assert(bl_next_calls == 2 && standby_seen_in_bl_off == NULL);
    assert(RV_FIELD(fake_controller, PowerController_standby_controller, void *) == &marker_screen);

    /* Duplicate EnterStandbyClock is skipped while standby_controller is already present. */
    assert(on_enter_standby_clock((uintptr_t)fake_controller, &marker_standby_next) == 0);
    assert(standby_next_calls == 0);

    /* Repeated Hold engage while standby_controller is active does not push a second screen. */
    assert(on_hold_change((uintptr_t)&marker_system, 1, &marker_next) == 77);
    assert(create_calls == 1 && push_calls == 1);

    /* Unlocking Hold clears hold_locked so normal EnterBacklightOff and EnterStandbyClock run again. */
    assert(on_hold_change((uintptr_t)&marker_system, 0, &marker_next) == 77);
    assert(hold_locked == 0);
    RV_FIELD(fake_controller, PowerController_standby_controller, void *) = NULL;
    assert(on_enter_standby_clock((uintptr_t)fake_controller, &marker_standby_next) == 22);
    assert(standby_next_calls == 1);

    /* Sleeping or missing dependencies fail closed without corrupting state. */
    RV_FIELD(fake_controller, PowerController_sleeping, uint8_t) = 1;
    assert(on_hold_change((uintptr_t)&marker_system, 1, &marker_next) == 77);
    assert(create_calls == 1 && push_calls == 1);

    RV_FIELD(fake_controller, PowerController_sleeping, uint8_t) = 0;
    return_null_navigator = 1;
    assert(on_hold_change((uintptr_t)&marker_system, 1, &marker_next) == 77);
    assert(create_calls == 2 && push_calls == 1);
    assert(RV_FIELD(fake_controller, PowerController_standby_controller, void *) == NULL);
    return_null_navigator = 0;

    return_null_screen = 1;
    assert(on_hold_change((uintptr_t)&marker_system, 1, &marker_next) == 77);
    assert(create_calls == 3 && push_calls == 1);
    return_null_screen = 0;

    return_null_registry = 1;
    assert(on_hold_change((uintptr_t)&marker_system, 1, &marker_next) == 77);
    assert(create_calls == 3 && push_calls == 1);
    return_null_registry = 0;

    return_null_controller = 1;
    assert(on_hold_change((uintptr_t)&marker_system, 1, &marker_next) == 77);
    assert(create_calls == 3 && push_calls == 1);
    return_null_controller = 0;

    /* Unloading clears hold_locked. */
    hold_locked = 1;
    on_unload();
    assert(hold_locked == 0);

    puts("Hold Standby Clock contracts passed");
    return 0;
}
