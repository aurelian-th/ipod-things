// SPDX-License-Identifier: GPL-3.0-only
/*
 * Adapted from PodBox firmware/usbstack/usb_iap2_control.c
 * (commit 7a89c33d0ed49c93ba49a8198e822b8adb72d0a5).
 * PodBox is GPL-2.0-or-later; this version is GPL-3.0-only.
 * Modified by the RevolaOS authors, 2026.
 */

#include "control.h"

static void copy_bytes(uint8_t *dst, const uint8_t *src, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        dst[i] = src[i];
    }
}

static uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static void write_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFU);
}

void iap2_control_stream_init(struct iap2_control_stream *stream,
                              uint8_t *storage, size_t capacity)
{
    if (stream == NULL) {
        return;
    }
    stream->storage = storage;
    stream->capacity = (storage != NULL) ? capacity : 0U;
    stream->used = 0U;
    stream->expected = 0U;
}

void iap2_control_stream_reset(struct iap2_control_stream *stream)
{
    if (stream == NULL) {
        return;
    }
    stream->used = 0U;
    stream->expected = 0U;
}

iap2_status_t iap2_control_feed(struct iap2_control_stream *stream,
                                const uint8_t *data, size_t len,
                                iap2_control_handler handler, void *context)
{
    if (stream == NULL || stream->storage == NULL ||
        stream->capacity < IAP2_CONTROL_HEADER_LEN || handler == NULL ||
        (len > 0U && data == NULL)) {
        return IAP2_STATUS_INVALID_ARG;
    }

    size_t pos = 0U;
    while (pos < len) {
        if (stream->used < IAP2_CONTROL_HEADER_LEN) {
            size_t avail = len - pos;
            size_t need = IAP2_CONTROL_HEADER_LEN - stream->used;
            size_t take = (avail < need) ? avail : need;
            copy_bytes(stream->storage + stream->used, data + pos, take);
            stream->used += take;
            pos += take;

            if (stream->used >= 1U && stream->storage[0] != IAP2_CONTROL_SOM1) {
                iap2_control_stream_reset(stream);
                return IAP2_STATUS_MALFORMED;
            }
            if (stream->used >= 2U && stream->storage[1] != IAP2_CONTROL_SOM2) {
                iap2_control_stream_reset(stream);
                return IAP2_STATUS_MALFORMED;
            }
            if (stream->used < IAP2_CONTROL_HEADER_LEN) {
                break;
            }

            uint16_t msg_len = read_be16(stream->storage + 2U);
            if (msg_len < IAP2_CONTROL_HEADER_LEN) {
                iap2_control_stream_reset(stream);
                return IAP2_STATUS_MALFORMED;
            }
            if ((size_t)msg_len > stream->capacity) {
                iap2_control_stream_reset(stream);
                return IAP2_STATUS_BUFFER_TOO_SMALL;
            }
            stream->expected = (size_t)msg_len;
        }

        if (stream->used < stream->expected && pos < len) {
            size_t avail = len - pos;
            size_t need = stream->expected - stream->used;
            size_t take = (avail < need) ? avail : need;
            copy_bytes(stream->storage + stream->used, data + pos, take);
            stream->used += take;
            pos += take;
        }

        if (stream->used == stream->expected) {
            struct iap2_control_message msg = {
                .id = read_be16(stream->storage + 4U),
                .parameters = stream->storage + IAP2_CONTROL_HEADER_LEN,
                .parameters_len = stream->expected - IAP2_CONTROL_HEADER_LEN,
            };
            const uint8_t *cursor = msg.parameters;
            size_t remaining = msg.parameters_len;
            struct iap2_control_param param;
            iap2_status_t st = IAP2_STATUS_OK;
            while (st == IAP2_STATUS_OK) {
                st = iap2_control_next_param(&cursor, &remaining, &param);
            }
            if (st != IAP2_STATUS_UNCLAIMED) {
                iap2_control_stream_reset(stream);
                return IAP2_STATUS_MALFORMED;
            }
            handler(context, &msg);
            iap2_control_stream_reset(stream);
        }
    }

    return (stream->used > 0U) ? IAP2_STATUS_IN_PROGRESS : IAP2_STATUS_OK;
}

iap2_status_t iap2_control_next_param(const uint8_t **cursor, size_t *remaining,
                                      struct iap2_control_param *out)
{
    if (cursor == NULL || *cursor == NULL || remaining == NULL || out == NULL) {
        return IAP2_STATUS_INVALID_ARG;
    }

    if (*remaining == 0U) {
        return IAP2_STATUS_UNCLAIMED;
    }
    if (*remaining < IAP2_CONTROL_PARAM_HEADER_LEN) {
        return IAP2_STATUS_MALFORMED;
    }

    const uint8_t *p = *cursor;
    uint16_t param_len = read_be16(p);
    if (param_len < IAP2_CONTROL_PARAM_HEADER_LEN ||
        (size_t)param_len > *remaining) {
        return IAP2_STATUS_MALFORMED;
    }

    out->id = read_be16(p + 2U);
    out->data = p + IAP2_CONTROL_PARAM_HEADER_LEN;
    out->len = (size_t)param_len - IAP2_CONTROL_PARAM_HEADER_LEN;
    *cursor = p + param_len;
    *remaining -= param_len;
    return IAP2_STATUS_OK;
}

iap2_status_t iap2_control_start(uint8_t *buf, size_t cap, uint16_t id,
                                 size_t *used)
{
    if (buf == NULL || used == NULL) {
        return IAP2_STATUS_INVALID_ARG;
    }
    if (cap < IAP2_CONTROL_HEADER_LEN) {
        return IAP2_STATUS_BUFFER_TOO_SMALL;
    }

    buf[0] = IAP2_CONTROL_SOM1;
    buf[1] = IAP2_CONTROL_SOM2;
    write_be16(buf + 2U, IAP2_CONTROL_HEADER_LEN);
    write_be16(buf + 4U, id);
    *used = IAP2_CONTROL_HEADER_LEN;
    return IAP2_STATUS_OK;
}

iap2_status_t iap2_control_append(uint8_t *buf, size_t cap, size_t *used,
                                  uint16_t id, const uint8_t *value,
                                  size_t len)
{
    if (buf == NULL || used == NULL || (len > 0U && value == NULL)) {
        return IAP2_STATUS_INVALID_ARG;
    }
    if (*used < IAP2_CONTROL_HEADER_LEN || *used > cap) {
        return IAP2_STATUS_INVALID_ARG;
    }
    if (len > (size_t)0xFFFFU - IAP2_CONTROL_PARAM_HEADER_LEN) {
        return IAP2_STATUS_BUFFER_TOO_SMALL;
    }

    size_t param_len = IAP2_CONTROL_PARAM_HEADER_LEN + len;
    if (param_len > cap - *used || *used + param_len > (size_t)0xFFFFU) {
        return IAP2_STATUS_BUFFER_TOO_SMALL;
    }

    uint8_t *p = buf + *used;
    write_be16(p, (uint16_t)param_len);
    write_be16(p + 2U, id);
    if (len > 0U) {
        copy_bytes(p + IAP2_CONTROL_PARAM_HEADER_LEN, value, len);
    }

    *used += param_len;
    write_be16(buf + 2U, (uint16_t)*used);
    return IAP2_STATUS_OK;
}
