/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/include/game_ui.h (8fc840d) by lgreitis. */

#ifndef EAPP_UI_H
#define EAPP_UI_H

#include "eapp_draw.h"

#ifdef __cplusplus
extern "C" {
#endif

struct eapp_menu_item {
    const char *label;
    const char *value;
};

struct eapp_menu {
    const char *title;
    const struct eapp_menu_item *items;
    unsigned count;
    unsigned selected;
};

enum eapp_menu_action {
    EAPP_MENU_NONE,
    EAPP_MENU_SELECT,
    EAPP_MENU_BACK,
};

enum eapp_menu_action eapp_menu_input(struct eapp_menu *menu,
                                      const struct eapp_input_event *event);
void eapp_menu_scroll(struct eapp_menu *menu, const struct eapp_wheel *wheel);
void eapp_menu_draw(const struct eapp_menu *menu, const struct eapp_framebuffer *fb);

/* The files with an extension under a folder of the package ("" for its root)
 * and its subfolders, sorted by name; the extension's case does not matter and
 * hidden paths are skipped. Open returns 0, -1 on failure, -2 for more than 64
 * files. The picker owns its labels and paths until it is closed. Zero it first;
 * close it before opening it again. */
struct eapp_file_picker {
    struct eapp_menu menu;
    struct eapp_menu_item items[64];
};

int eapp_file_picker_open(struct eapp_file_picker *picker, const char *title,
                          const char *directory, const char *extension);
void eapp_file_picker_close(struct eapp_file_picker *picker);

#ifdef __cplusplus
}
#endif
#endif
