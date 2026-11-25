/* SPDX-License-Identifier: GPL-3.0-only */
#include "embedded.h"

#ifdef __arm__
#include "rv_libc.h"
#else
#include <string.h>
#endif

#define MP4_MAX_DEPTH  8U
#define MP4_MAX_ATOMS  512U
#define ID3_MAX_FRAMES 512U

static uint32_t lyr_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t lyr_be64(const uint8_t *p)
{
    return ((uint64_t)lyr_be32(p) << 32) | (uint64_t)lyr_be32(p + 4);
}

static int tag_eq4(const uint8_t *a, const uint8_t *b)
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static int tag_eq4_c(const uint8_t *a, const char *b)
{
    return tag_eq4(a, (const uint8_t *)b);
}

static int tag_eq3_c(const uint8_t *a, const char *b)
{
    return a[0] == (uint8_t)b[0] && a[1] == (uint8_t)b[1] &&
           a[2] == (uint8_t)b[2];
}

static int read_exact(const struct lyrics_reader *r, void *buf, size_t len)
{
    uint8_t *dst = buf;
    size_t done = 0U;

    while (done < len) {
        size_t got = 0U;
        if (!r->read(r->context, dst + done, len - done, &got) || got == 0U ||
            got > len - done) {
            return 0;
        }
        done += got;
    }
    return 1;
}

static int seek_and_read(const struct lyrics_reader *r, uint64_t off,
                         void *buf, size_t len)
{
    if (!r->seek(r->context, off))
        return 0;
    return read_exact(r, buf, len);
}

static int valid_synchsafe32(const uint8_t *p, uint32_t *out)
{
    if ((p[0] | p[1] | p[2] | p[3]) & 0x80U)
        return 0;
    *out = ((uint32_t)p[0] << 21) | ((uint32_t)p[1] << 14) |
           ((uint32_t)p[2] << 7) | (uint32_t)p[3];
    return 1;
}

struct mp4_frame {
    uint64_t pos;
    uint64_t end;
    uint8_t level;
};

static int extract_mp4_lyrics(const struct lyrics_reader *r,
                              struct lrc_state *st,
                              char *scratch, size_t scratch_cap)
{
    static const uint8_t path_tags[6][4] = {
        {'m', 'o', 'o', 'v'},
        {'u', 'd', 't', 'a'},
        {'m', 'e', 't', 'a'},
        {'i', 'l', 's', 't'},
        {0xA9U, 'l', 'y', 'r'},
        {'d', 'a', 't', 'a'},
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
        uint32_t size32 = lyr_be32(hdr);
        uint64_t hdr_len = 8ULL;
        uint64_t box_end = 0ULL;

        if (size32 == 1U) {
            uint8_t ext[8];
            if (f->end - box_off < 16ULL || !read_exact(r, ext, sizeof(ext)))
                return 0;
            uint64_t size64 = lyr_be64(ext);
            if (size64 < 16ULL || size64 > UINT64_MAX - box_off)
                return 0;
            hdr_len = 16ULL;
            box_end = box_off + size64;
        } else if (size32 == 0U) {
            box_end = f->end;
        } else {
            if (size32 < 8U || (uint64_t)size32 > UINT64_MAX - box_off)
                return 0;
            box_end = box_off + (uint64_t)size32;
        }

        if (f->end != UINT64_MAX && box_end > f->end)
            return 0;
        if (box_end <= box_off + hdr_len && size32 != 0U) {
            f->pos = box_end;
            continue;
        }

        f->pos = box_end;
        uint8_t level = f->level;
        if (!tag_eq4(hdr + 4, path_tags[level]))
            continue;

        uint64_t payload_off = box_off + hdr_len;
        if (level == 5U) {
            if (box_end == UINT64_MAX || box_end <= payload_off + 8ULL)
                continue;
            uint8_t data_hdr[8];
            if (!seek_and_read(r, payload_off, data_hdr, sizeof(data_hdr)))
                return 0;
            uint32_t type_ind = lyr_be32(data_hdr);
            /* MP4 'data' types: 1 = UTF-8 text, 0 = implicit */
            if (type_ind != 1U && type_ind != 0U)
                continue;
            uint64_t txt_off = payload_off + 8ULL;
            uint64_t txt_len = box_end - txt_off;
            if (txt_len == 0ULL)
                continue;
            size_t take = (txt_len > (uint64_t)(scratch_cap - 1U))
                              ? (scratch_cap - 1U)
                              : (size_t)txt_len;
            if (!seek_and_read(r, txt_off, scratch, take))
                return 0;
            scratch[take] = '\0';
            if (lrc_parse(st, scratch, (uint32_t)take) > 0U)
                return 1;
            continue;
        }

        if (level == 2U) {
            if (box_end != UINT64_MAX && box_end < payload_off + 4ULL)
                continue;
            payload_off += 4ULL;
        }

        if (sp < MP4_MAX_DEPTH && payload_off < box_end) {
            stack[sp].pos = payload_off;
            stack[sp].end = box_end;
            stack[sp].level = (uint8_t)(level + 1U);
            sp++;
        }
    }

    return 0;
}

static size_t ununsync_bytes(uint8_t *buf, size_t len)
{
    size_t r = 0U;
    size_t w = 0U;
    while (r < len) {
        uint8_t c = buf[r++];
        buf[w++] = c;
        if (c == 0xFFU && r < len && buf[r] == 0x00U)
            r++;
    }
    return w;
}

static size_t emit_utf8(uint32_t cp, char *out, size_t at, size_t cap)
{
    if (cp == 0U || at + 1U >= cap)
        return at;
    if (cp < 0x80U) {
        out[at++] = (char)cp;
    } else if (cp < 0x800U) {
        if (at + 2U < cap) {
            out[at++] = (char)(0xC0U | (cp >> 6));
            out[at++] = (char)(0x80U | (cp & 0x3FU));
        }
    } else if (cp < 0x10000U) {
        if (at + 3U < cap) {
            out[at++] = (char)(0xE0U | (cp >> 12));
            out[at++] = (char)(0x80U | ((cp >> 6) & 0x3FU));
            out[at++] = (char)(0x80U | (cp & 0x3FU));
        }
    } else if (cp <= 0x10FFFFU) {
        if (at + 4U < cap) {
            out[at++] = (char)(0xF0U | (cp >> 18));
            out[at++] = (char)(0x80U | ((cp >> 12) & 0x3FU));
            out[at++] = (char)(0x80U | ((cp >> 6) & 0x3FU));
            out[at++] = (char)(0x80U | (cp & 0x3FU));
        }
    }
    return at;
}

static size_t decode_id3_string(uint8_t enc, const uint8_t *src, size_t len,
                                char *dst, size_t dst_cap)
{
    size_t at = 0U;
    if (!dst || dst_cap == 0U)
        return 0U;
    if (!src || len == 0U) {
        dst[0] = '\0';
        return 0U;
    }

    if (enc == 0U) {
        for (size_t i = 0U; i < len && src[i] != 0U && at + 1U < dst_cap; i++)
            at = emit_utf8((uint32_t)src[i], dst, at, dst_cap);
    } else if (enc == 1U || enc == 2U) {
        int le = 0;
        size_t i = 0U;
        if (enc == 1U && len >= 2U) {
            if (src[0] == 0xFFU && src[1] == 0xFEU) {
                le = 1;
                i = 2U;
            } else if (src[0] == 0xFEU && src[1] == 0xFFU) {
                le = 0;
                i = 2U;
            }
        }
        while (i + 1U < len && at + 1U < dst_cap) {
            uint16_t u = le ? (uint16_t)((uint16_t)src[i] | ((uint16_t)src[i + 1U] << 8))
                            : (uint16_t)(((uint16_t)src[i] << 8) | src[i + 1U]);
            i += 2U;
            if (u == 0U)
                break;
            if (u >= 0xD800U && u <= 0xDBFFU && i + 1U < len) {
                uint16_t lo = le ? (uint16_t)((uint16_t)src[i] | ((uint16_t)src[i + 1U] << 8))
                                 : (uint16_t)(((uint16_t)src[i] << 8) | src[i + 1U]);
                if (lo >= 0xDC00U && lo <= 0xDFFFU) {
                    i += 2U;
                    uint32_t cp = 0x10000U + (((uint32_t)(u - 0xD800U) << 10) |
                                              (uint32_t)(lo - 0xDC00U));
                    at = emit_utf8(cp, dst, at, dst_cap);
                    continue;
                }
            }
            at = emit_utf8((uint32_t)u, dst, at, dst_cap);
        }
    } else if (enc == 3U) {
        size_t i = 0U;
        while (i < len && src[i] != 0U && at + 1U < dst_cap)
            dst[at++] = (char)src[i++];
    }

    dst[at] = '\0';
    return at;
}

static size_t skip_id3_terminated(uint8_t enc, const uint8_t *buf, size_t len, size_t pos)
{
    if (enc == 1U || enc == 2U) {
        while (pos + 1U < len) {
            if (buf[pos] == 0U && buf[pos + 1U] == 0U)
                return pos + 2U;
            pos += 2U;
        }
        return len;
    }
    while (pos < len) {
        if (buf[pos] == 0U)
            return pos + 1U;
        pos++;
    }
    return len;
}

static int parse_sylt_payload(struct lrc_state *st, const uint8_t *buf, size_t len)
{
    /* SYLT: encoding(1) + language(3) + timestamp_format(1) + content_type(1) */
    if (len < 7U)
        return 0;
    uint8_t enc = buf[0];
    uint8_t ts_fmt = buf[4];
    if (enc > 3U || ts_fmt != 2U) /* 2 = milliseconds */
        return 0;

    size_t pos = skip_id3_terminated(enc, buf, len, 6U);
    lrc_state_reset(st);
    st->has_synced_timestamps = 1;

    while (pos < len && st->line_count < LRC_MAX_LINES) {
        size_t text_start = pos;
        size_t after_text = skip_id3_terminated(enc, buf, len, pos);
        if (after_text + 4U > len)
            break;
        size_t raw_len = after_text - text_start;
        uint32_t ts_ms = lyr_be32(buf + after_text);
        pos = after_text + 4U;

        struct lrc_line *line = &st->lines[st->line_count];
        size_t out_len = decode_id3_string(enc, buf + text_start, raw_len,
                                           line->text, sizeof(line->text));
        while (out_len > 0U &&
               (line->text[out_len - 1U] == '\r' || line->text[out_len - 1U] == '\n' ||
                line->text[out_len - 1U] == ' ' || line->text[out_len - 1U] == '\t')) {
            line->text[--out_len] = '\0';
        }
        if (out_len > 0U) {
            line->timestamp_ms = ts_ms;
            st->line_count++;
        }
    }

    if (st->line_count == 0U) {
        lrc_state_reset(st);
        return 0;
    }

    for (uint32_t i = 1U; i < st->line_count; i++) {
        struct lrc_line key = st->lines[i];
        int j = (int)i - 1;
        while (j >= 0 && st->lines[j].timestamp_ms > key.timestamp_ms) {
            st->lines[j + 1] = st->lines[j];
            j--;
        }
        st->lines[j + 1] = key;
    }
    lrc_build_wrap(st, LRC_WRAP_WIDTH_PX);
    return 1;
}

static int parse_uslt_payload(struct lrc_state *st, uint8_t *raw, size_t raw_len,
                              char *out, size_t out_cap)
{
    /* USLT: encoding(1) + language(3) + content_descriptor(NUL-term) + text */
    if (raw_len < 5U)
        return 0;
    uint8_t enc = raw[0];
    if (enc > 3U)
        return 0;
    size_t pos = skip_id3_terminated(enc, raw, raw_len, 4U);
    if (pos >= raw_len)
        return 0;
    size_t n = decode_id3_string(enc, raw + pos, raw_len - pos, out, out_cap);
    if (n == 0U)
        return 0;
    return lrc_parse(st, out, (uint32_t)n) > 0U ? 1 : 0;
}

static int extract_id3_lyrics(const struct lyrics_reader *r, const uint8_t *hdr10,
                              struct lrc_state *st,
                              char *scratch, size_t scratch_cap)
{
    uint8_t ver = hdr10[3];
    uint8_t flags = hdr10[5];
    if (ver != 3U && ver != 4U)
        return 0;

    uint32_t tag_size = 0U;
    if (!valid_synchsafe32(hdr10 + 6, &tag_size))
        return 0;

    int global_unsync = (flags & 0x80U) != 0U;
    uint64_t tag_end = 10ULL + (uint64_t)tag_size;
    uint64_t pos = 10ULL;

    if ((flags & 0x40U) != 0U) {
        uint8_t ext[4];
        if (pos + 4ULL > tag_end || !seek_and_read(r, pos, ext, sizeof(ext)))
            return 0;
        uint32_t ext_size = 0U;
        if (ver == 4U) {
            if (!valid_synchsafe32(ext, &ext_size) || ext_size < 6U ||
                (uint64_t)ext_size > tag_end - pos) {
                return 0;
            }
            pos += (uint64_t)ext_size;
        } else {
            ext_size = lyr_be32(ext);
            if ((uint64_t)ext_size > tag_end - pos - 4ULL)
                return 0;
            pos += 4ULL + (uint64_t)ext_size;
        }
    }

    uint64_t uslt_off = 0ULL;
    size_t uslt_len = 0U;
    int uslt_unsync = 0;

    for (uint32_t i = 0U; i < ID3_MAX_FRAMES && pos + 10ULL <= tag_end; i++) {
        uint8_t fhdr[10];
        if (!seek_and_read(r, pos, fhdr, sizeof(fhdr)))
            break;
        if (fhdr[0] == 0U)
            break;

        uint32_t fsize = 0U;
        int skip = 0;
        uint64_t extra = 0ULL;
        int frame_unsync = global_unsync;

        if (ver == 4U) {
            if (!valid_synchsafe32(fhdr + 4, &fsize))
                break;
            if ((fhdr[9] & 0x0CU) != 0U) /* compressed or encrypted */
                skip = 1;
            if ((fhdr[9] & 0x02U) != 0U)
                frame_unsync = 1;
            if ((fhdr[9] & 0x40U) != 0U)
                extra += 1ULL;
            if ((fhdr[9] & 0x01U) != 0U)
                extra += 4ULL;
        } else {
            fsize = lyr_be32(fhdr + 4);
            if ((fhdr[9] & 0xC0U) != 0U) /* compressed or encrypted */
                skip = 1;
            if ((fhdr[9] & 0x20U) != 0U)
                extra += 1ULL;
        }

        uint64_t payload_off = pos + 10ULL;
        if ((uint64_t)fsize > tag_end - payload_off)
            break;
        uint64_t next_pos = payload_off + (uint64_t)fsize;

        if (!skip && (uint64_t)fsize > extra) {
            uint64_t data_off = payload_off + extra;
            size_t data_len = (size_t)((uint64_t)fsize - extra);

            if (tag_eq4_c(fhdr, "SYLT")) {
                size_t take = (data_len > scratch_cap) ? scratch_cap : data_len;
                uint8_t *raw = (uint8_t *)scratch;
                if (seek_and_read(r, data_off, raw, take)) {
                    if (frame_unsync)
                        take = ununsync_bytes(raw, take);
                    if (parse_sylt_payload(st, raw, take))
                        return 1;
                }
            } else if (tag_eq4_c(fhdr, "USLT") && uslt_len == 0U) {
                uslt_off = data_off;
                uslt_len = data_len;
                uslt_unsync = frame_unsync;
            }
        }

        pos = next_pos;
    }

    if (uslt_len > 0U && scratch_cap >= 128U) {
        size_t half = scratch_cap / 2U;
        size_t take = (uslt_len > scratch_cap - half) ? (scratch_cap - half) : uslt_len;
        uint8_t *raw = (uint8_t *)scratch + half;
        if (seek_and_read(r, uslt_off, raw, take)) {
            if (uslt_unsync)
                take = ununsync_bytes(raw, take);
            if (parse_uslt_payload(st, raw, take, scratch, half))
                return 1;
        }
    }

    return 0;
}

int lyrics_extract_embedded(const struct lyrics_reader *reader,
                            struct lrc_state *st,
                            char *scratch,
                            size_t scratch_cap)
{
    if (!reader || !reader->read || !reader->seek || !st ||
        !scratch || scratch_cap < 256U) {
        return 0;
    }

    uint8_t hdr[12];
    if (!seek_and_read(reader, 0ULL, hdr, sizeof(hdr)))
        return 0;

    if (tag_eq4_c(hdr + 4, "ftyp"))
        return extract_mp4_lyrics(reader, st, scratch, scratch_cap);
    if (tag_eq3_c(hdr, "ID3"))
        return extract_id3_lyrics(reader, hdr, st, scratch, scratch_cap);

    return 0;
}

