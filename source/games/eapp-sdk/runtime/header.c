/* SPDX-License-Identifier: GPL-3.0-only */

/* The eApp header at the start of the image: what the host reads to find
 * the import list and the entries. */

#include "runtime.h"

void eapp_start(void);
uint32_t eapp_reserved_entry(void);
extern const uint8_t eapp_imports[];

const struct eapp_abi_header eapp_abi_header __attribute__((section(".eapp"), used)) = {
    .magic = GameImage_Magic,
    .version = GameImage_MaxVersion,
    .entry_count = EAPP_ABI_SLOT_COUNT,
    .imports = (uint32_t)eapp_imports,
    .entries = {
        [EAPP_ABI_SLOT_INIT] = (uint32_t)eapp_start,
        [EAPP_ABI_SLOT_SHUTDOWN] = (uint32_t)eapp_runtime_shutdown,
        [2] = (uint32_t)eapp_reserved_entry,
        [3] = (uint32_t)eapp_reserved_entry,
        [EAPP_ABI_SLOT_FRAME] = (uint32_t)eapp_runtime_frame,
    },
};
