// SPDX-License-Identifier: GPL-3.0-only
#include "artwork.h"

#define MP4_MAX_DEPTH 8U
#define MP4_MAX_ATOMS 512U
#define ID3_MAX_FRAMES 512U

static uint32_t art_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t art_be64(const uint8_t *p)
{
    return ((uint64_t)art_be32(p) << 32) | (uint64_t)art_be32(p + 4);
}

static bool tag_eq4(const uint8_t *a, const char *b)
{
    return a[0] == (uint8_t)b[0] && a[1] == (uint8_t)b[1] &&
           a[2] == (uint8_t)b[2] && a[3] == (uint8_t)b[3];
}

static bool tag_eq3(const uint8_t *a, const char *b)
{
    return a[0] == (uint8_t)b[0] && a[1] == (uint8_t)b[1] &&
           a[2] == (uint8_t)b[2];
}

static bool read_exact(const struct iap2_art_reader *r, void *buf, size_t len)
{
    uint8_t *dst = buf;
    size_t done = 0U;
    while (done < len) {
        size_t got = 0U;
        if (!r->read(r->context, dst + done, len - done, &got) || got == 0U ||
            got > len - done) {
            return false;
        }
        done += got;
    }
    return true;
}

static bool seek_and_read(const struct iap2_art_reader *r, uint64_t off,
                          void *buf, size_t len)
{
    if (!r->seek(r->context, off)) {
        return false;
    }
    return read_exact(r, buf, len);
}

static bool valid_synchsafe32(const uint8_t *p, uint32_t *out)
{
    if ((p[0] | p[1] | p[2] | p[3]) & 0x80U) {
        return false;
    }
    *out = ((uint32_t)p[0] << 21) | ((uint32_t)p[1] << 14) |
           ((uint32_t)p[2] << 7) | (uint32_t)p[3];
    return true;
}

struct mp4_frame {
    uint64_t pos;
    uint64_t end;
    uint8_t level;
};

static bool extract_mp4(const struct iap2_art_reader *r,
                        uint8_t *out_buf, size_t out_cap, size_t *out_len)
{
    static const char *const path_tags[6] = {
        "moov", "udta", "meta", "ilst", "covr", "data"
    };

    struct mp4_frame stack[MP4_MAX_DEPTH];
    size_t sp = 1U;
    stack[0].pos = 0ULL;
    stack[0].end = UINT64_MAX;
    stack[0].level = 0U;

    uint32_t atoms = 0U;
    while (sp > 0U && atoms < MP4_MAX_ATOMS) {
        struct mp4_frame *f = &stack[sp - 1U];
        if (f->pos >= f->end || f->end - f->pos < 8ULL) {
            sp--;
            continue;
        }

        uint8_t hdr[8];
        if (!seek_and_read(r, f->pos, hdr, sizeof(hdr))) {
            sp--;
            continue;
        }
        atoms++;

        uint64_t box_off = f->pos;
        uint32_t size32 = art_be32(hdr);
        uint64_t hdr_len = 8ULL;
        uint64_t box_end = 0ULL;

        if (size32 == 1U) {
            uint8_t ext[8];
            if (f->end - box_off < 16ULL || !read_exact(r, ext, sizeof(ext))) {
                return false;
            }
            uint64_t size64 = art_be64(ext);
            if (size64 < 16ULL || size64 > UINT64_MAX - box_off) {
                return false;
            }
            hdr_len = 16ULL;
            box_end = box_off + size64;
        } else if (size32 == 0U) {
            box_end = f->end;
        } else {
            if (size32 < 8U || (uint64_t)size32 > UINT64_MAX - box_off) {
                return false;
            }
            box_end = box_off + (uint64_t)size32;
        }

        if (f->end != UINT64_MAX && box_end > f->end) {
            return false;
        }
        if (box_end <= box_off + hdr_len && size32 != 0U) {
            f->pos = box_end;
            continue;
        }

        f->pos = box_end;
        uint8_t level = f->level;
        if (!tag_eq4(hdr + 4, path_tags[level])) {
            continue;
        }

        uint64_t payload_off = box_off + hdr_len;
        if (level == 5U) {
            if (box_end == UINT64_MAX || box_end <= payload_off + 8ULL) {
                continue;
            }
            uint8_t data_hdr[8];
            if (!seek_and_read(r, payload_off, data_hdr, sizeof(data_hdr))) {
                return false;
            }
            uint32_t type_ind = art_be32(data_hdr);
            if (type_ind != 13U && type_ind != 0U) {
                continue;
            }
            uint64_t img_off = payload_off + 8ULL;
            uint64_t img_len = box_end - img_off;
            if (img_len < 2ULL || img_len > (uint64_t)out_cap) {
                continue;
            }
            if (!seek_and_read(r, img_off, out_buf, (size_t)img_len)) {
                return false;
            }
            if (out_buf[0] == 0xFFU && out_buf[1] == 0xD8U) {
                *out_len = (size_t)img_len;
                return true;
            }
            continue;
        }

        if (level == 2U) {
            /* ISO 14496-12 'meta' is a FullBox (4-byte version/flags). */
            if (box_end != UINT64_MAX && box_end < payload_off + 4ULL) {
                continue;
            }
            payload_off += 4ULL;
        }

        if (sp < MP4_MAX_DEPTH && payload_off < box_end) {
            stack[sp].pos = payload_off;
            stack[sp].end = box_end;
            stack[sp].level = (uint8_t)(level + 1U);
            sp++;
        }
    }

    return false;
}

static bool skip_id3_pic_header(const struct iap2_art_reader *r,
                                uint64_t start_off, uint64_t frame_len,
                                bool is_v22, uint64_t *img_off_out,
                                uint64_t *img_len_out)
{
    if (frame_len < (is_v22 ? 7ULL : 5ULL)) {
        return false;
    }
    if (!r->seek(r->context, start_off)) {
        return false;
    }

    uint64_t rem = frame_len;
    uint8_t enc = 0U;
    if (!read_exact(r, &enc, 1U)) {
        return false;
    }
    rem--;
    if (enc > 3U) {
        return false;
    }

    if (is_v22) {
        uint8_t fmt[3];
        if (rem < 3ULL || !read_exact(r, fmt, sizeof(fmt))) {
            return false;
        }
        rem -= 3ULL;
    } else {
        while (rem > 0ULL) {
            uint8_t c = 0U;
            if (!read_exact(r, &c, 1U)) {
                return false;
            }
            rem--;
            if (c == 0U) {
                break;
            }
        }
    }

    if (rem == 0ULL) {
        return false;
    }
    uint8_t pic_type = 0U;
    if (!read_exact(r, &pic_type, 1U)) {
        return false;
    }
    (void)pic_type;
    rem--;

    if (enc == 1U || enc == 2U) {
        bool term = false;
        while (rem >= 2ULL) {
            uint8_t pair[2];
            if (!read_exact(r, pair, sizeof(pair))) {
                return false;
            }
            rem -= 2ULL;
            if (pair[0] == 0U && pair[1] == 0U) {
                term = true;
                break;
            }
        }
        if (!term) {
            return false;
        }
    } else {
        bool term = false;
        while (rem > 0ULL) {
            uint8_t c = 0U;
            if (!read_exact(r, &c, 1U)) {
                return false;
            }
            rem--;
            if (c == 0U) {
                term = true;
                break;
            }
        }
        if (!term) {
            return false;
        }
    }

    if (rem < 2ULL) {
        return false;
    }
    *img_off_out = start_off + (frame_len - rem);
    *img_len_out = rem;
    return true;
}

static bool extract_id3(const struct iap2_art_reader *r, const uint8_t *hdr10,
                        uint8_t *out_buf, size_t out_cap, size_t *out_len)
{
    uint8_t ver = hdr10[3];
    uint8_t flags = hdr10[5];
    if (ver < 2U || ver > 4U) {
        return false;
    }
    /* Global unsynchronisation or v2.2 tag-level compression alters image bytes. */
    if ((flags & 0x80U) != 0U || (ver == 2U && (flags & 0x40U) != 0U)) {
        return false;
    }

    uint32_t tag_size = 0U;
    if (!valid_synchsafe32(hdr10 + 6, &tag_size)) {
        return false;
    }
    uint64_t tag_end = 10ULL + (uint64_t)tag_size;
    uint64_t pos = 10ULL;

    if ((ver == 3U || ver == 4U) && (flags & 0x40U) != 0U) {
        uint8_t ext[4];
        if (pos + 4ULL > tag_end || !seek_and_read(r, pos, ext, sizeof(ext))) {
            return false;
        }
        uint32_t ext_size = 0U;
        if (ver == 4U) {
            if (!valid_synchsafe32(ext, &ext_size) || ext_size < 6U) {
                return false;
            }
            if ((uint64_t)ext_size > tag_end - pos) {
                return false;
            }
            pos += (uint64_t)ext_size;
        } else {
            ext_size = art_be32(ext);
            if ((uint64_t)ext_size > tag_end - pos - 4ULL) {
                return false;
            }
            pos += 4ULL + (uint64_t)ext_size;
        }
    }

    uint64_t fhdr_len = (ver == 2U) ? 6ULL : 10ULL;
    for (uint32_t i = 0U; i < ID3_MAX_FRAMES && pos + fhdr_len <= tag_end; i++) {
        uint8_t fhdr[10];
        if (!seek_and_read(r, pos, fhdr, (size_t)fhdr_len)) {
            return false;
        }
        if (fhdr[0] == 0U) {
            break;
        }

        uint32_t fsize = 0U;
        bool skip = false;
        bool is_pic = false;
        uint64_t extra = 0ULL;

        if (ver == 2U) {
            fsize = ((uint32_t)fhdr[3] << 16) | ((uint32_t)fhdr[4] << 8) |
                    (uint32_t)fhdr[5];
            is_pic = tag_eq3(fhdr, "PIC");
        } else {
            if (ver == 4U) {
                if (!valid_synchsafe32(fhdr + 4, &fsize)) {
                    break;
                }
                if ((fhdr[9] & 0x0EU) != 0U) {
                    skip = true;
                }
                if ((fhdr[9] & 0x40U) != 0U) {
                    extra += 1ULL;
                }
                if ((fhdr[9] & 0x01U) != 0U) {
                    extra += 4ULL;
                }
            } else {
                fsize = art_be32(fhdr + 4);
                if ((fhdr[9] & 0xC0U) != 0U) {
                    skip = true;
                }
                if ((fhdr[9] & 0x20U) != 0U) {
                    extra += 1ULL;
                }
            }
            is_pic = tag_eq4(fhdr, "APIC");
        }

        uint64_t payload_off = pos + fhdr_len;
        if ((uint64_t)fsize > tag_end - payload_off) {
            break;
        }
        uint64_t next_pos = payload_off + (uint64_t)fsize;

        if (is_pic && !skip && (uint64_t)fsize > extra) {
            uint64_t img_off = 0ULL;
            uint64_t img_len = 0ULL;
            if (skip_id3_pic_header(r, payload_off + extra,
                                    (uint64_t)fsize - extra, ver == 2U,
                                    &img_off, &img_len)) {
                if (img_len >= 2ULL && img_len <= (uint64_t)out_cap &&
                    seek_and_read(r, img_off, out_buf, (size_t)img_len) &&
                    out_buf[0] == 0xFFU && out_buf[1] == 0xD8U) {
                    *out_len = (size_t)img_len;
                    return true;
                }
            }
        }

        pos = next_pos;
    }

    return false;
}

bool iap2_artwork_extract_jpeg(const struct iap2_art_reader *reader,
                               uint8_t *out_buf,
                               size_t out_cap,
                               size_t *out_len)
{
    if (out_len != NULL) {
        *out_len = 0U;
    }
    if (reader == NULL || reader->read == NULL || reader->seek == NULL ||
        out_buf == NULL || out_cap < 2U || out_len == NULL) {
        return false;
    }

    uint8_t hdr[12];
    if (!seek_and_read(reader, 0ULL, hdr, sizeof(hdr))) {
        return false;
    }

    if (tag_eq4(hdr + 4, "ftyp")) {
        return extract_mp4(reader, out_buf, out_cap, out_len);
    }
    if (tag_eq3(hdr, "ID3")) {
        return extract_id3(reader, hdr, out_buf, out_cap, out_len);
    }
    return false;
}

