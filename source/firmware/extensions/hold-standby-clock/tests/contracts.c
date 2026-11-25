/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main.c"

static int marker_system, marker_next, marker_bl_next, marker_standby_next, marker_disk_exit_next;
static int marker_registry, marker_screen, marker_navigator;
static uint8_t fake_controller[PowerController_game_running + 4];
static uint8_t fake_app[App_in_disk_mode + 4];
void *App = fake_app;
static void *standby_seen_in_bl_off;
static int next_calls, bl_next_calls, standby_next_calls, disk_exit_next_calls;
static int create_calls, push_calls, post_event_calls, pop_top_calls;
static int return_null_controller, return_null_registry, return_null_screen, return_null_navigator;
static int post_event_clears_standby = 1;
static uintptr_t last_system;
static uint32_t last_locked, last_screen_id, last_layout_id, last_transition, last_push_flags;
static uint32_t last_post_event, last_pop_flags;
static void *last_push_nav, *last_push_ctrl, *last_post_ctrl;

void *PowerController_Get(void)
{
    return return_null_controller ? NULL : fake_controller;
}

void PowerController_PostEvent(void *controller, uint32_t event)
{
    post_event_calls++;
    last_post_ctrl = controller;
    last_post_event = event;
    if (post_event_clears_standby && controller && event == POWER_EVENT_USER_ACTIVITY) {
        RV_FIELD(controller, PowerController_standby_controller, void *) = NULL;
    }
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

void Navigator_PopTop(uint32_t flags)
{
    pop_top_calls++;
    last_pop_flags = flags;
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
    if (next == &marker_disk_exit_next) {
        disk_exit_next_calls++;
        return 33;
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
    memset(fake_app, 0, sizeof fake_app);
    assert(rv_module_entry(&test_api, NULL) == &extension);

    /* Without Hold locked, EnterBacklightOff passes standby_controller through untouched. */
    RV_FIELD(fake_controller, PowerController_standby_controller, void *) = &marker_screen;
    assert(on_enter_backlight_off((uintptr_t)fake_controller, &marker_bl_next) == 11);
    assert(bl_next_calls == 1 && standby_seen_in_bl_off == &marker_screen);

    /* Unlocking Hold when standby_controller is already NULL does not post wake event. */
    RV_FIELD(fake_controller, PowerController_standby_controller, void *) = NULL;
    assert(on_hold_change((uintptr_t)&marker_system, 0, &marker_next) == 77);
    assert(next_calls == 1 && last_system == (uintptr_t)&marker_system && last_locked == 0);
    assert(create_calls == 0 && push_calls == 0 && post_event_calls == 0 && hold_locked == 0);

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

    /* Unlocking Hold when Apple left standby_controller active (states 2/3) posts wake event 22. */
    assert(on_hold_change((uintptr_t)&marker_system, 0, &marker_next) == 77);
    assert(hold_locked == 0);
    assert(post_event_calls == 1 && last_post_ctrl == fake_controller);
    assert(last_post_event == POWER_EVENT_USER_ACTIVITY);
    assert(pop_top_calls == 0);
    assert(RV_FIELD(fake_controller, PowerController_standby_controller, void *) == NULL);
    assert(on_enter_standby_clock((uintptr_t)fake_controller, &marker_standby_next) == 22);
    assert(standby_next_calls == 1);

    /* Fallback pops top screen directly if event 22 did not clear standby_controller. */
    post_event_clears_standby = 0;
    RV_FIELD(fake_controller, PowerController_standby_controller, void *) = &marker_screen;
    assert(on_hold_change((uintptr_t)&marker_system, 0, &marker_next) == 77);
    assert(post_event_calls == 2 && pop_top_calls == 1 && last_pop_flags == 0);
    assert(RV_FIELD(fake_controller, PowerController_standby_controller, void *) == NULL);
    post_event_clears_standby = 1;

    /* Disk mode requested/active blocks pushing standby clock and stops EnterBacklightOff hiding it. */
    RV_FIELD(fake_app, App_disk_mode_requested, uint8_t) = 1;
    assert(on_hold_change((uintptr_t)&marker_system, 1, &marker_next) == 77);
    assert(create_calls == 1 && push_calls == 1);
    RV_FIELD(fake_controller, PowerController_standby_controller, void *) = &marker_screen;
    assert(on_enter_backlight_off((uintptr_t)fake_controller, &marker_bl_next) == 11);
    assert(bl_next_calls == 3 && standby_seen_in_bl_off == &marker_screen);
    RV_FIELD(fake_app, App_disk_mode_requested, uint8_t) = 0;

    RV_FIELD(fake_app, App_in_disk_mode, uint8_t) = 1;
    RV_FIELD(fake_controller, PowerController_standby_controller, void *) = NULL;
    assert(on_hold_change((uintptr_t)&marker_system, 1, &marker_next) == 77);
    assert(create_calls == 1 && push_calls == 1);
    RV_FIELD(fake_app, App_in_disk_mode, uint8_t) = 0;

    /* Disk mode exit clears any stale standby_controller before popping to main screen. */
    RV_FIELD(fake_controller, PowerController_standby_controller, void *) = &marker_screen;
    assert(on_disk_mode_exit(&marker_disk_exit_next) == 33);
    assert(disk_exit_next_calls == 1);
    assert(RV_FIELD(fake_controller, PowerController_standby_controller, void *) == NULL);

    /* Running game, sleeping, or missing dependencies fail closed without corrupting state. */
    RV_FIELD(fake_controller, PowerController_game_running, uint8_t) = 1;
    assert(on_hold_change((uintptr_t)&marker_system, 1, &marker_next) == 77);
    assert(create_calls == 1 && push_calls == 1);
    RV_FIELD(fake_controller, PowerController_game_running, uint8_t) = 0;

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
