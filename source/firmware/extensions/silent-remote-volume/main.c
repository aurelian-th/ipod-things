/* SPDX-License-Identifier: GPL-3.0-only */

#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_sites.h"
#include "rv_ui.h"

static const struct rv_api *api;

static int is_remote_volume_key(uint32_t key)
{
    return key == UIKey_MikeyVolumeDown || key == UIKey_MikeyVolumeUp ||
           key == UIKey_RemoteVolumeDown || key == UIKey_RemoteVolumeUp;
}

static int is_button_phase(uint32_t kind)
{
    /* PowerController_FilterEvent checks button event phases 6, 7, 8, and 13. */
    return kind == InputEvent_ButtonDown || kind == 7 ||
           kind == InputEvent_ButtonPressAndHold || kind == InputEvent_ButtonUp;
}

static int should_suppress_wake(const void *controller, const void *event)
{
    if (!controller || !event)
        return 0;
    if (RV_FIELD(controller, PowerController_sleeping, uint8_t))
        return 0;
    uint32_t kind = RV_FIELD(event, InputEvent_kind, uint32_t);
    if (!is_button_phase(kind))
        return 0;
    uint32_t key = (RV_FIELD(event, InputEvent_key, uint32_t) >> 16) & 0xffu;
    return is_remote_volume_key(key);
}

static uint32_t on_filter_event(uintptr_t controller, uintptr_t event, const void *next)
{
    if (should_suppress_wake((const void *)controller, (const void *)event))
        return 0;
    return api->hook_next(next, controller, event, 0, 0, 0, 0);
}

static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER(RV_SITE_PowerController_FilterEvent_entry, on_filter_event),
};

static const struct rv_descriptor extension = {
    .size = sizeof extension,
    RV_HOOKS(hooks),
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    (void)handle;
    api = core;
    return &extension;
}

