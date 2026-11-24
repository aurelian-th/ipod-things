// SPDX-License-Identifier: GPL-3.0-only
#ifndef IAP2_ARTWORK_H
#define IAP2_ARTWORK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IAP2_ARTWORK_MAX_BYTES 524288U

struct iap2_art_reader {
    void *context;
    bool (*read)(void *context, void *buf, size_t len, size_t *out_read);
    bool (*seek)(void *context, uint64_t offset);
};

bool iap2_artwork_extract_jpeg(const struct iap2_art_reader *reader,
                               uint8_t *out_buf,
                               size_t out_cap,
                               size_t *out_len);

#endif /* IAP2_ARTWORK_H */

