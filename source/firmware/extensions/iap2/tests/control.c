// SPDX-License-Identifier: GPL-3.0-only
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "control.h"

struct seen {
    uint16_t ids[4];
    unsigned count;
};

static void check_message(void *arg, const struct iap2_control_message *message)
{
    struct seen *seen = arg;
    assert(seen->count < sizeof(seen->ids) / sizeof(seen->ids[0]));
    seen->ids[seen->count++] = message->id;
    if (message->id == 0x1d01) {
        const uint8_t *cursor = message->parameters;
        size_t remaining = message->parameters_len;
        struct iap2_control_param param;
        assert(iap2_control_next_param(&cursor, &remaining, &param) == IAP2_STATUS_OK);
        assert(param.id == 3 && param.len == 3);
        assert(memcmp(param.data, "ABC", 3) == 0);
        assert(iap2_control_next_param(&cursor, &remaining, &param) == IAP2_STATUS_OK);
        assert(param.id == 3 && param.len == 0);
        assert(iap2_control_next_param(&cursor, &remaining, &param) == IAP2_STATUS_UNCLAIMED);
    } else {
        assert(message->parameters_len == 0);
    }
}

static void test_stream(void)
{
    static const uint8_t wire[] = {
        0x40, 0x40, 0, 17, 0x1d, 1,
        0, 7, 0, 3, 'A', 'B', 'C',
        0, 4, 0, 3,
        0x40, 0x40, 0, 6, 0x1d, 2,
    };
    uint8_t storage[32];
    for (size_t split = 0; split <= sizeof(wire); split++) {
        struct iap2_control_stream stream;
        struct seen seen = {0};
        iap2_control_stream_init(&stream, storage, sizeof(storage));
        iap2_status_t expected = (split == 0 || split == 17 || split == sizeof(wire))
            ? IAP2_STATUS_OK : IAP2_STATUS_IN_PROGRESS;
        assert(iap2_control_feed(&stream, wire, split, check_message, &seen) == expected);
        assert(iap2_control_feed(&stream, wire + split, sizeof(wire) - split,
                                 check_message, &seen) == IAP2_STATUS_OK);
        assert(seen.count == 2 && seen.ids[0] == 0x1d01 && seen.ids[1] == 0x1d02);
    }
}

static void test_bad_lengths(void)
{
    static const uint8_t too_large[] = {0x40, 0x40, 0, 64, 1, 2};
    static const uint8_t wrong_tlv[] = {0x40, 0x40, 0, 10, 1, 2, 0, 3, 0, 1};
    static const uint8_t good[] = {0x40, 0x40, 0, 6, 1, 2};
    struct iap2_control_stream stream;
    struct seen seen = {0};
    uint8_t storage[16];

    iap2_control_stream_init(&stream, storage, sizeof(storage));
    assert(iap2_control_feed(&stream, too_large, sizeof(too_large),
                             check_message, &seen) != IAP2_STATUS_OK);
    assert(seen.count == 0 && stream.used == 0);
    assert(iap2_control_feed(&stream, wrong_tlv, sizeof(wrong_tlv),
                             check_message, &seen) == IAP2_STATUS_MALFORMED);
    assert(seen.count == 0 && stream.used == 0);
    assert(iap2_control_feed(&stream, good, sizeof(good), check_message, &seen) == IAP2_STATUS_OK);
    assert(seen.count == 1 && seen.ids[0] == 0x0102);
}

static void test_encode(void)
{
    uint8_t buffer[16];
    uint8_t saved[16];
    size_t length = 0;
    memset(buffer, 0xa5, sizeof(buffer));
    assert(iap2_control_start(buffer, sizeof(buffer), 0xda02, &length) == IAP2_STATUS_OK);
    assert(iap2_control_append(buffer, sizeof(buffer), &length, 0, NULL, 0) == IAP2_STATUS_OK);
    assert(iap2_control_append(buffer, sizeof(buffer), &length, 1,
                               (const uint8_t *)"AB", 2) == IAP2_STATUS_OK);
    static const uint8_t expected[] = {
        0x40, 0x40, 0, 16, 0xda, 2, 0, 4, 0, 0, 0, 6, 0, 1, 'A', 'B',
    };
    assert(length == sizeof(expected));
    assert(memcmp(buffer, expected, length) == 0);
    memcpy(saved, buffer, sizeof(buffer));
    assert(iap2_control_append(buffer, sizeof(buffer), &length, 2, NULL, 0)
           == IAP2_STATUS_BUFFER_TOO_SMALL);
    assert(length == sizeof(expected) && memcmp(buffer, saved, length) == 0);
}

int main(void)
{
    test_stream();
    test_bad_lengths();
    test_encode();
    puts("iAP2 control stream tests passed");
}
