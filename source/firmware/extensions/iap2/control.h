// SPDX-License-Identifier: GPL-3.0-only
#ifndef IAP2_CONTROL_H
#define IAP2_CONTROL_H

#include <stddef.h>
#include <stdint.h>
#include "protocol.h"

#define IAP2_CONTROL_SOM1             0x40U
#define IAP2_CONTROL_SOM2             0x40U
#define IAP2_CONTROL_HEADER_LEN       6U
#define IAP2_CONTROL_PARAM_HEADER_LEN 4U

struct iap2_control_message {
    uint16_t id;
    const uint8_t *parameters;
    size_t parameters_len;
};

struct iap2_control_param {
    uint16_t id;
    const uint8_t *data;
    size_t len;
};

struct iap2_control_stream {
    uint8_t *storage;
    size_t capacity;
    size_t used;
    size_t expected;
};

typedef void (*iap2_control_handler)(void *context,
                                     const struct iap2_control_message *message);

void iap2_control_stream_init(struct iap2_control_stream *stream,
                              uint8_t *storage, size_t capacity);
void iap2_control_stream_reset(struct iap2_control_stream *stream);
iap2_status_t iap2_control_feed(struct iap2_control_stream *stream,
                                const uint8_t *data, size_t len,
                                iap2_control_handler handler, void *context);
iap2_status_t iap2_control_next_param(const uint8_t **cursor, size_t *remaining,
                                      struct iap2_control_param *out);
iap2_status_t iap2_control_start(uint8_t *buf, size_t cap, uint16_t id,
                                 size_t *used);
iap2_status_t iap2_control_append(uint8_t *buf, size_t cap, size_t *used,
                                  uint16_t id, const uint8_t *value,
                                  size_t len);

#endif /* IAP2_CONTROL_H */

