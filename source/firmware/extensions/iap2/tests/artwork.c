// SPDX-License-Identifier: GPL-3.0-only
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "artwork.h"

struct mem_src {
    const uint8_t *data;
    size_t len;
    uint64_t pos;
    size_t max_chunk;
};

static bool mem_read(void *ctx, void *buf, size_t len, size_t *out_read)
{
    struct mem_src *m = ctx;
    if (m->pos >= m->len) {
        *out_read = 0U;
        return true;
    }
    size_t avail = m->len - (size_t)m->pos;
    size_t take = (len < avail) ? len : avail;
    if (m->max_chunk > 0U && take > m->max_chunk) {
        take = m->max_chunk;
    }
    memcpy(buf, m->data + m->pos, take);
    m->pos += take;
    *out_read = take;
    return true;
}

static bool mem_seek(void *ctx, uint64_t offset)
{
    struct mem_src *m = ctx;
    if (offset > m->len) {
        return false;
    }
    m->pos = offset;
    return true;
}

static size_t put_be32(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v >> 24);
    dst[1] = (uint8_t)(v >> 16);
    dst[2] = (uint8_t)(v >> 8);
    dst[3] = (uint8_t)v;
    return 4U;
}

static size_t put_be64(uint8_t *dst, uint64_t v)
{
    put_be32(dst, (uint32_t)(v >> 32));
    put_be32(dst + 4, (uint32_t)v);
    return 8U;
}

static void put_synchsafe(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)((v >> 21) & 0x7FU);
    dst[1] = (uint8_t)((v >> 14) & 0x7FU);
    dst[2] = (uint8_t)((v >> 7) & 0x7FU);
    dst[3] = (uint8_t)(v & 0x7FU);
}

static size_t wrap_mp4_box(uint8_t *dst, const char *type,
                           const uint8_t *payload, size_t plen)
{
    put_be32(dst, (uint32_t)(8U + plen));
    memcpy(dst + 4, type, 4U);
    if (plen > 0U) {
        memcpy(dst + 8, payload, plen);
    }
    return 8U + plen;
}

static size_t wrap_mp4_box64(uint8_t *dst, const char *type,
                             const uint8_t *payload, size_t plen)
{
    put_be32(dst, 1U);
    memcpy(dst + 4, type, 4U);
    put_be64(dst + 8, (uint64_t)(16U + plen));
    if (plen > 0U) {
        memcpy(dst + 16, payload, plen);
    }
    return 16U + plen;
}

static void build_sample_jpeg(uint8_t *buf, size_t len)
{
    assert(len >= 4U);
    buf[0] = 0xFFU;
    buf[1] = 0xD8U;
    buf[2] = 0xFFU;
    buf[3] = 0xE0U;
    for (size_t i = 4U; i + 2U < len; i++) {
        buf[i] = (uint8_t)(i * 37U + 11U);
    }
    buf[len - 2U] = 0xFFU;
    buf[len - 1U] = 0xD9U;
}

static void test_mp4_basic_and_largesize(void)
{
    uint8_t jpeg[256];
    build_sample_jpeg(jpeg, sizeof(jpeg));

    /* Build data atom (type=13 JPEG) */
    uint8_t data_payload[8 + sizeof(jpeg)];
    put_be32(data_payload, 13U);
    put_be32(data_payload + 4, 0U);
    memcpy(data_payload + 8, jpeg, sizeof(jpeg));

    uint8_t data_box[512];
    size_t data_len = wrap_mp4_box(data_box, "data", data_payload, sizeof(data_payload));

    /* Also add a PNG data box before JPEG to verify skipping non-JPEG */
    uint8_t png_payload[16];
    put_be32(png_payload, 14U);
    put_be32(png_payload + 4, 0U);
    memcpy(png_payload + 8, "\x89PNG\r\n\x1a\n", 8U);

    uint8_t covr_inner[600];
    size_t pos = 0U;
    pos += wrap_mp4_box(covr_inner + pos, "data", png_payload, sizeof(png_payload));
    memcpy(covr_inner + pos, data_box, data_len);
    pos += data_len;

    uint8_t covr_box[700];
    size_t covr_len = wrap_mp4_box(covr_box, "covr", covr_inner, pos);

    uint8_t ilst_box[800];
    size_t ilst_len = wrap_mp4_box(ilst_box, "ilst", covr_box, covr_len);

    uint8_t meta_inner[900];
    memset(meta_inner, 0, 4U);
    memcpy(meta_inner + 4U, ilst_box, ilst_len);
    uint8_t meta_box[950];
    size_t meta_len = wrap_mp4_box(meta_box, "meta", meta_inner, 4U + ilst_len);

    uint8_t udta_box[1000];
    size_t udta_len = wrap_mp4_box(udta_box, "udta", meta_box, meta_len);

    uint8_t moov_box[1100];
    size_t moov_len = wrap_mp4_box64(moov_box, "moov", udta_box, udta_len);

    uint8_t file_buf[1500];
    size_t fpos = 0U;
    static const uint8_t ftyp_payload[12] = {
        'M', '4', 'A', ' ', 0, 0, 0, 0, 'M', '4', 'A', ' '
    };
    fpos += wrap_mp4_box(file_buf + fpos, "ftyp", ftyp_payload, sizeof(ftyp_payload));
    uint8_t dummy_mdat[64];
    memset(dummy_mdat, 0xAA, sizeof(dummy_mdat));
    fpos += wrap_mp4_box64(file_buf + fpos, "mdat", dummy_mdat, sizeof(dummy_mdat));
    memcpy(file_buf + fpos, moov_box, moov_len);
    fpos += moov_len;

    /* Test normal and chunked reads */
    for (size_t chunk = 0U; chunk <= 7U; chunk += 7U) {
        struct mem_src src = { .data = file_buf, .len = fpos, .pos = 0U, .max_chunk = chunk };
        struct iap2_art_reader r = { .context = &src, .read = mem_read, .seek = mem_seek };
        uint8_t out[512];
        size_t out_len = 0U;
        assert(iap2_artwork_extract_jpeg(&r, out, sizeof(out), &out_len));
        assert(out_len == sizeof(jpeg));
        assert(memcmp(out, jpeg, sizeof(jpeg)) == 0);
    }

    /* Oversized rejection (buffer too small) */
    {
        struct mem_src src = { .data = file_buf, .len = fpos };
        struct iap2_art_reader r = { .context = &src, .read = mem_read, .seek = mem_seek };
        uint8_t small[128];
        size_t out_len = 999U;
        assert(!iap2_artwork_extract_jpeg(&r, small, sizeof(small), &out_len));
        assert(out_len == 0U);
    }
}

static void test_mp4_reject_png_and_corrupt(void)
{
    /* Type 0 with PNG magic must fail */
    uint8_t png_payload[16];
    put_be32(png_payload, 0U);
    put_be32(png_payload + 4, 0U);
    memcpy(png_payload + 8, "\x89PNG\r\n\x1a\n", 8U);

    uint8_t b[256], tmp[256];
    size_t len = wrap_mp4_box(b, "data", png_payload, sizeof(png_payload));
    len = wrap_mp4_box(tmp, "covr", b, len);
    len = wrap_mp4_box(b, "ilst", tmp, len);

    uint8_t meta_in[256];
    memset(meta_in, 0, 4U);
    memcpy(meta_in + 4, b, len);
    len = wrap_mp4_box(tmp, "meta", meta_in, 4U + len);
    len = wrap_mp4_box(b, "udta", tmp, len);
    len = wrap_mp4_box(tmp, "moov", b, len);

    uint8_t file_buf[300];
    static const uint8_t ftyp_payload[4] = { 'M', '4', 'A', ' ' };
    size_t fpos = wrap_mp4_box(file_buf, "ftyp", ftyp_payload, sizeof(ftyp_payload));
    memcpy(file_buf + fpos, tmp, len);
    fpos += len;

    struct mem_src src = { .data = file_buf, .len = fpos };
    struct iap2_art_reader r = { .context = &src, .read = mem_read, .seek = mem_seek };
    uint8_t out[256];
    size_t out_len = 1U;
    assert(!iap2_artwork_extract_jpeg(&r, out, sizeof(out), &out_len));
    assert(out_len == 0U);

    /* Corrupt box size < 8 */
    uint8_t corrupt[24];
    wrap_mp4_box(corrupt, "ftyp", ftyp_payload, sizeof(ftyp_payload));
    put_be32(corrupt + 12, 3U); /* invalid < 8 */
    memcpy(corrupt + 16, "moov", 4U);
    struct mem_src csrc = { .data = corrupt, .len = 20U };
    struct iap2_art_reader cr = { .context = &csrc, .read = mem_read, .seek = mem_seek };
    assert(!iap2_artwork_extract_jpeg(&cr, out, sizeof(out), &out_len));
    assert(out_len == 0U);
}

static void test_id3_v23_v24_v22(void)
{
    uint8_t jpeg[120];
    build_sample_jpeg(jpeg, sizeof(jpeg));

    /* 1. ID3v2.3 with extended header + UTF-16 APIC */
    {
        uint8_t apic[256];
        size_t ap = 0U;
        apic[ap++] = 1U; /* UTF-16 with BOM */
        memcpy(apic + ap, "image/jpeg", 11U);
        ap += 11U;
        apic[ap++] = 3U; /* Front cover */
        /* UTF-16 BOM + "Hi" + UTF-16 NUL */
        apic[ap++] = 0xFFU; apic[ap++] = 0xFEU;
        apic[ap++] = 'H';   apic[ap++] = 0x00U;
        apic[ap++] = 'i';   apic[ap++] = 0x00U;
        apic[ap++] = 0x00U; apic[ap++] = 0x00U;
        memcpy(apic + ap, jpeg, sizeof(jpeg));
        ap += sizeof(jpeg);

        uint8_t tag[512];
        size_t p = 10U;
        /* v2.3 extended header: size=6 (excludes the 4 size bytes) + 6 bytes */
        put_be32(tag + p, 6U); p += 4U;
        memset(tag + p, 0, 6U); p += 6U;
        /* Dummy TIT2 frame */
        memcpy(tag + p, "TIT2", 4U); p += 4U;
        put_be32(tag + p, 4U); p += 4U;
        tag[p++] = 0U; tag[p++] = 0U;
        memcpy(tag + p, "\0abc", 4U); p += 4U;
        /* APIC frame */
        memcpy(tag + p, "APIC", 4U); p += 4U;
        put_be32(tag + p, (uint32_t)ap); p += 4U;
        tag[p++] = 0U; tag[p++] = 0U;
        memcpy(tag + p, apic, ap); p += ap;

        memcpy(tag, "ID3", 3U);
        tag[3] = 3U; tag[4] = 0U; tag[5] = 0x40U; /* ext header flag */
        put_synchsafe(tag + 6, (uint32_t)(p - 10U));

        struct mem_src src = { .data = tag, .len = p, .max_chunk = 5U };
        struct iap2_art_reader r = { .context = &src, .read = mem_read, .seek = mem_seek };
        uint8_t out[256];
        size_t out_len = 0U;
        assert(iap2_artwork_extract_jpeg(&r, out, sizeof(out), &out_len));
        assert(out_len == sizeof(jpeg));
        assert(memcmp(out, jpeg, sizeof(jpeg)) == 0);
    }

    /* 2. ID3v2.4 with synchsafe frame size + data length indicator */
    {
        uint8_t apic[256];
        size_t ap = 0U;
        put_synchsafe(apic + ap, 12U + sizeof(jpeg));
        ap += 4U;
        apic[ap++] = 3U; /* UTF-8 */
        memcpy(apic + ap, "image/jpg", 10U);
        ap += 10U;
        apic[ap++] = 3U;
        apic[ap++] = 0U; /* empty NUL desc */
        memcpy(apic + ap, jpeg, sizeof(jpeg));
        ap += sizeof(jpeg);

        uint8_t tag[512];
        size_t p = 10U;
        memcpy(tag + p, "APIC", 4U); p += 4U;
        put_synchsafe(tag + p, (uint32_t)ap); p += 4U;
        tag[p++] = 0U; tag[p++] = 0x01U; /* data length indicator flag */
        memcpy(tag + p, apic, ap); p += ap;

        memcpy(tag, "ID3", 3U);
        tag[3] = 4U; tag[4] = 0U; tag[5] = 0U;
        put_synchsafe(tag + 6, (uint32_t)(p - 10U));

        struct mem_src src = { .data = tag, .len = p };
        struct iap2_art_reader r = { .context = &src, .read = mem_read, .seek = mem_seek };
        uint8_t out[256];
        size_t out_len = 0U;
        assert(iap2_artwork_extract_jpeg(&r, out, sizeof(out), &out_len));
        assert(out_len == sizeof(jpeg));
        assert(memcmp(out, jpeg, sizeof(jpeg)) == 0);
    }

    /* 3. ID3v2.2 PIC frame */
    {
        uint8_t pic[256];
        size_t pp = 0U;
        pic[pp++] = 0U; /* ISO-8859-1 */
        memcpy(pic + pp, "JPG", 3U); pp += 3U;
        pic[pp++] = 3U;
        pic[pp++] = 'c'; pic[pp++] = 0U;
        memcpy(pic + pp, jpeg, sizeof(jpeg)); pp += sizeof(jpeg);

        uint8_t tag[512];
        size_t p = 10U;
        memcpy(tag + p, "PIC", 3U); p += 3U;
        tag[p++] = (uint8_t)((pp >> 16) & 0xFFU);
        tag[p++] = (uint8_t)((pp >> 8) & 0xFFU);
        tag[p++] = (uint8_t)(pp & 0xFFU);
        memcpy(tag + p, pic, pp); p += pp;

        memcpy(tag, "ID3", 3U);
        tag[3] = 2U; tag[4] = 0U; tag[5] = 0U;
        put_synchsafe(tag + 6, (uint32_t)(p - 10U));

        struct mem_src src = { .data = tag, .len = p };
        struct iap2_art_reader r = { .context = &src, .read = mem_read, .seek = mem_seek };
        uint8_t out[256];
        size_t out_len = 0U;
        assert(iap2_artwork_extract_jpeg(&r, out, sizeof(out), &out_len));
        assert(out_len == sizeof(jpeg));
        assert(memcmp(out, jpeg, sizeof(jpeg)) == 0);
    }

    /* 4. Non-audio / corrupt ID3 rejected safely */
    {
        uint8_t bad[16] = { 'I', 'D', '3', 3, 0, 0, 0x80, 0, 0, 10, 0, 0 };
        struct mem_src src = { .data = bad, .len = sizeof(bad) };
        struct iap2_art_reader r = { .context = &src, .read = mem_read, .seek = mem_seek };
        uint8_t out[64];
        size_t out_len = 1U;
        assert(!iap2_artwork_extract_jpeg(&r, out, sizeof(out), &out_len));
        assert(out_len == 0U);
    }
}

int main(void)
{
    test_mp4_basic_and_largesize();
    test_mp4_reject_png_and_corrupt();
    test_id3_v23_v24_v22();
    printf("iAP2 artwork extractor tests passed\n");
    return 0;
}
