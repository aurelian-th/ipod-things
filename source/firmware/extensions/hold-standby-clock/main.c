/* SPDX-License-Identifier: GPL-3.0-only */

#include <stddef.h>

#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_sites.h"
#include "rv_ui.h"

static const struct rv_api *api;
static uint8_t hold_locked;

/* RetailOS 2.0.5 PowerController FSM wake/user-activity event (0x16).
 * Transitions all active/standby states (0, 1, 2, 3, 4) back to awake,
 * popping standby_controller and restoring the display and backlight. */
#define POWER_EVENT_USER_ACTIVITY 22U

static int disk_mode_active(void)
{
    return App && (RV_FIELD(App, App_disk_mode_requested, uint8_t) ||
                   RV_FIELD(App, App_in_disk_mode, uint8_t));
}

static void on_unload(void)
{
    hold_locked = 0;
}

static uint32_t on_hold_change(uintptr_t system, uint32_t locked, const void *next)
{
    hold_locked = locked ? 1 : 0;
    uint32_t result = api->hook_next(next, system, locked, 0, 0, 0, 0);
    void *controller = PowerController_Get();

    if (!locked) {
        /* Apple's Hold unlock posts event 17, which PowerController ignores in
         * playing states 2 (BL on) and 3 (BL off). Wake via event 22 so the
         * standby clock closes and the display/backlight return cleanly. */
        if (controller && RV_FIELD(controller, PowerController_standby_controller, void *)) {
            PowerController_PostEvent(controller, POWER_EVENT_USER_ACTIVITY);
            if (RV_FIELD(controller, PowerController_standby_controller, void *)) {
                RV_FIELD(controller, PowerController_standby_controller, void *) = NULL;
                Navigator_PopTop(0);
            }
        }
        return result;
    }

    if (disk_mode_active() || !controller ||
        RV_FIELD(controller, PowerController_sleeping, uint8_t) ||
        RV_FIELD(controller, PowerController_game_running, uint8_t) ||
        RV_FIELD(controller, PowerController_standby_controller, void *)) {
        return result;
    }

    void *registry = UI_GetControllerRegistry();
    void *screen = registry ? UI_CreateScreenController(registry, RES_NowPlaying_Idle_Screen,
                                                        RES_NowPlaying_Idle_Layout, 0)
                            : NULL;
    if (screen) {
        void *navigator = Navigator_Get();
        if (navigator) {
            RV_FIELD(controller, PowerController_standby_controller, void *) = screen;
            Navigator_Push(navigator, screen, 0);
        }
    }
    return result;
}

static uint32_t on_enter_backlight_off(uintptr_t controller, const void *next)
{
    void *ctrl = (void *)controller;
    if (!hold_locked || !ctrl || disk_mode_active()) {
        return api->hook_next(next, controller, 0, 0, 0, 0, 0);
    }

    void *standby = RV_FIELD(ctrl, PowerController_standby_controller, void *);
    if (!standby) {
        return api->hook_next(next, controller, 0, 0, 0, 0, 0);
    }

    /* Apple's state 1 entry pops standby_controller when leaving playback standby;
     * hide it across the transition so the standby clock stays up after backlight off. */
    RV_FIELD(ctrl, PowerController_standby_controller, void *) = NULL;
    uint32_t result = api->hook_next(next, controller, 0, 0, 0, 0, 0);
    RV_FIELD(ctrl, PowerController_standby_controller, void *) = standby;
    return result;
}

static uint32_t on_enter_standby_clock(uintptr_t controller, const void *next)
{
    void *ctrl = (void *)controller;
    if (ctrl && RV_FIELD(ctrl, PowerController_standby_controller, void *)) {
        return 0;
    }
    return api->hook_next(next, controller, 0, 0, 0, 0, 0);
}

static uint32_t on_disk_mode_exit(const void *next)
{
    void *controller = PowerController_Get();
    if (controller) {
        RV_FIELD(controller, PowerController_standby_controller, void *) = NULL;
    }
    return api->hook_next(next, 0, 0, 0, 0, 0, 0);
}

static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER(RV_SITE_SystemUI_HandleHoldChange_call, on_hold_change),
    RV_HOOK_FILTER(RV_SITE_PowerController_EnterBacklightOff_entry, on_enter_backlight_off),
    RV_HOOK_FILTER(RV_SITE_PowerController_EnterStandbyClock_entry, on_enter_standby_clock),
    RV_HOOK_FILTER(RV_SITE_DiskModeExit_PopToMainScreen_call, on_disk_mode_exit),
};

static const struct rv_descriptor extension = {
    .size = sizeof extension,
    RV_HOOKS(hooks),
    .unload = on_unload,
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    (void)handle;
    api = core;
    hold_locked = 0;
    return &extension;
}
