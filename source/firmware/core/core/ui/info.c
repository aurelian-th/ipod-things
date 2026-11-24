/* SPDX-License-Identifier: GPL-3.0-only */

/* Settings > RevisionOS info, after Legal: a text page with the version, what
 * RevisionOS is, and the credits and licence from the NOTICE files.
 * The system font has no "+", ";", ">" or "_", so the text avoids them. */

#include "ui/info.h"
#include "osos-2.0.5.h"
#include "ui/menus.h"
#include "ui/screens.h"
#include <stddef.h>

/* "RevisionOS 0.0.0-dev (ead7b622)": CORE_VERSION with its build in brackets,
 * as the system font has no "+". The pane shows it without "RevisionOS ". */
static char heading[56];

static void format_version(void)
{
    char *out = heading, *end = heading + sizeof heading - 3;
    int build = 0;
    for (const char *c = "RevisionOS " CORE_VERSION; *c && out < end; c++) {
        if (*c == '+' && !build) {
            *out++ = ' ';
            *out++ = '(';
            build = 1;
        } else {
            *out++ = *c;
        }
    }
    if (build)
        *out++ = ')';
    *out = 0;
}

static const char *const paragraphs[] = {
    heading,
    "Custom firmware for iPod classic that runs extensions. github.com/aurelian-th/ipod-things",
    "Built on RevolaOS (github.com/RevolaOS) and derived from RepriseOS by lgreitis and the "
    "RepriseOS contributors: lgreitis, Alex Ball (alexballvideos-netizen), RaduTek, "
    "Swimming33, brilliant-alien, kayboll.",
    "Car Audio iAP2 protocol framing references PodBox by Anthony Fletcher. GPL-2.0-or-later.",
    "giek2000: iPod firmware research.",
    "Rockbox contributors: the bootloader, dual-boot installer, hardware drivers and USB "
    "helper, and the Crossfeed (Thom Johansen, Bertrik Sikken, Michael Sevakis), Channels "
    "(Thom Johansen, Michael Sevakis) and Compressor (Jeffrey Goode) extensions. "
    "GPL-2.0-or-later.",
    "Homebrew from Rockbox: 2048 by Franklin Wei with the high score table by Linus "
    "Nielsen Feltzing, Dice by Brandon Low. GPL-2.0-or-later.",
    "wInd3x by Serge \"q3k\" Bazanski: BootROM exploit and DFU tooling. GPL-2.0-or-later.",
    "Cupertino Icons (Framework7 Icons) by Vladimir Kharlampidi: the Settings and "
    "Appearance glyphs. MIT License.",
    "Provided as is, without warranty of any kind. GPL-3.0-only. Licence and NOTICE: "
    "github.com/aurelian-th/ipod-things",
};

static const struct rv_bitmap icon = {"info"};

static const struct rv_screen screen = {
    .id = "core.info", .title = "RevisionOS info", .kind = RV_SCREEN_KIND_TEXT,
    .text = {paragraphs, sizeof paragraphs / sizeof paragraphs[0]},
};

static const struct rv_menu_entry entry = {
    .menu = RV_MENU_SETTINGS, .after = RES_SettingsMenu_Legal_Item,
    .row = {.label = "RevisionOS info", .kind = RV_ROW_KIND_PUSH, .screen = "core.info"},
    .caption = heading + sizeof "RevisionOS",
    .bitmap = &icon,
};

void core_info_register(void)
{
    format_version();
    core_screens_register(NULL, 0, &screen, 1);
    core_menus_register(NULL, &entry, 1);
}
