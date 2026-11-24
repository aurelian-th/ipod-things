/* SPDX-License-Identifier: GPL-3.0-only */

#include <stddef.h>

#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_sites.h"
#include "rv_ui.h"

static const struct rv_api *api;
static uint8_t hold_locked;

static void on_unload(void)
{
    hold_locked = 0;
}

static uint32_t on_hold_change(uintptr_t system, uint32_t locked, const void *next)
{
    hold_locked = locked ? 1 : 0;
    uint32_t result = api->hook_next(next, system, locked, 0, 0, 0, 0);
    if (!locked) {
        return result;
    }

    void *controller = PowerController_Get();
    if (!controller || RV_FIELD(controller, PowerController_sleeping, uint8_t) ||
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
    if (!hold_locked || !ctrl) {
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

static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER(RV_SITE_SystemUI_HandleHoldChange_call, on_hold_change),
    RV_HOOK_FILTER(RV_SITE_PowerController_EnterBacklightOff_entry, on_enter_backlight_off),
    RV_HOOK_FILTER(RV_SITE_PowerController_EnterStandbyClock_entry, on_enter_standby_clock),
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
