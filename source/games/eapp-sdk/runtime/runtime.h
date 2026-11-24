/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef EAPP_RUNTIME_H
#define EAPP_RUNTIME_H

#include "eapp_abi.h"

/* The entries the eApp header names: init (after startup.S cleared bss),
 * shutdown and frame. */
void eapp_runtime_init(void);
void eapp_runtime_shutdown(void);
void eapp_runtime_frame(const struct eapp_abi_frame_input *input,
                        struct eapp_abi_frame_output *output);
/* One of a frame's events: to the app's input callback, then a music
 * button's release to the host unless the app handles those. */
void eapp_runtime_input(const struct eapp_abi_input_event *native);

#endif
