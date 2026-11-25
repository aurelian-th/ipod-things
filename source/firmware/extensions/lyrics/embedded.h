/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef LYRICS_EMBEDDED_H
#define LYRICS_EMBEDDED_H

#include "lrc.h"
#include <stddef.h>
#include <stdint.h>

struct lyrics_reader {
    void *context;
    int (*read)(void *context, void *dst, size_t len, size_t *out_got);
    int (*seek)(void *context, uint64_t offset);
};

/* Extract embedded lyrics from an MP4/M4A/ALAC ('©lyr' atom) or MP3 ID3v2
 * ('SYLT' preferred over 'USLT') audio stream into `st`.
 * `scratch` must provide at least 256 bytes (ideally LRC_FILE_MAX).
 * Returns 1 if at least one lyric line was populated, 0 otherwise. */
int lyrics_extract_embedded(const struct lyrics_reader *reader,
                            struct lrc_state *st,
                            char *scratch,
                            size_t scratch_cap);

#endif /* LYRICS_EMBEDDED_H */

