#include "duck_tm1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Built-in skinny/fat deltas (public TM1 constants; overridden by 19DUCK). */
static const int16_t k_ydt[4][8] = {
    {0, -2, 2, -6, 6, -12, 12, -12},
    {0, -2, 4, -6, 8, -12, 12, -12},
    {4, -6, 20, -20, 46, -46, 94, -94},
    {0, -4, 4, -16, 16, -36, 36, -80}
};
static const int16_t k_fat_ydt[4][8] = {
    {0, -15, 50, -50, 115, -115, 235, -235},
    {0, -15, 50, -50, 115, -115, 235, -235},
    {0, -15, 50, -50, 115, -115, 235, -235},
    {0, 40, 80, -76, 160, -154, 236, -236}
};
static const int16_t k_cdt[4][8] = {
    {0, -1, 1, -2, 3, -4, 5, -4},
    {0, -1, 1, -2, 3, -4, 5, -4},
    {0, -4, 3, -16, 20, -32, 36, -32},
    {0, -2, 2, -8, 8, -18, 18, -40}
};
static const int16_t k_fat_cdt[4][8] = {
    {0, -20, 15, -80, 100, -160, 180, -160},
    {0, -20, 15, -80, 100, -160, 180, -160},
    {0, -20, 15, -80, 100, -160, 180, -160},
    {0, 40, 80, -76, 160, -154, 236, -236}
};

/* Minimal sel_vector fallback (identity-ish) when 19DUCK vec walk fails. */
static const uint8_t k_vec_fallback[] = {
    0x8, 0x00, 0x00, 0x00, 0x00, 0x8, 0x00, 0x00, 0x00, 0x00,
    0x8, 0x10, 0x00, 0x00, 0x00, 0x8, 0x01, 0x00, 0x00, 0x00,
    0x8, 0x00, 0x10, 0x00, 0x00, 0x8, 0x00, 0x01, 0x00, 0x00,
    0x8, 0x00, 0x00, 0x10, 0x00, 0x8, 0x00, 0x00, 0x01, 0x00,
    0x8, 0x00, 0x00, 0x00, 0x10, 0x8, 0x00, 0x00, 0x00, 0x01,
    0x6, 0x00, 0x00, 0x00, 0x6, 0x10, 0x00, 0x00, 0x6, 0x01, 0x00, 0x00,
    0x6, 0x00, 0x10, 0x00, 0x6, 0x00, 0x01, 0x00, 0x6, 0x00, 0x00, 0x10,
    0x6, 0x00, 0x00, 0x01, 0x4, 0x00, 0x00, 0x4, 0x10, 0x00, 0x4, 0x01, 0x00,
    0x4, 0x00, 0x10, 0x4, 0x00, 0x01, 0x2, 0x00, 0x2, 0x10, 0x2, 0x01, 0x2, 0x11
};

typedef enum { ALGO_NOP = 0, ALGO_RGB16V, ALGO_RGB16H, ALGO_RGB24H } tm_algo;
typedef enum { BLOCK_2x2 = 0, BLOCK_2x4, BLOCK_4x2, BLOCK_4x4 } tm_block;

static const struct {
    tm_algo algo;
    int block_w, block_h;
    tm_block block_type;
} k_comp[17] = {
    {ALGO_NOP, 0, 0, BLOCK_2x2},
    {ALGO_RGB16V, 4, 4, BLOCK_4x4}, {ALGO_RGB16H, 4, 4, BLOCK_4x4},
    {ALGO_RGB16V, 4, 2, BLOCK_4x2}, {ALGO_RGB16H, 4, 2, BLOCK_4x2},
    {ALGO_RGB16V, 2, 4, BLOCK_2x4}, {ALGO_RGB16H, 2, 4, BLOCK_2x4},
    {ALGO_RGB16V, 2, 2, BLOCK_2x2}, {ALGO_RGB16H, 2, 2, BLOCK_2x2},
    {ALGO_NOP, 0, 0, BLOCK_2x2},
    {ALGO_RGB24H, 4, 4, BLOCK_4x4},
    {ALGO_NOP, 0, 0, BLOCK_2x2},
    {ALGO_RGB24H, 4, 2, BLOCK_4x2},
    {ALGO_NOP, 0, 0, BLOCK_2x2},
    {ALGO_RGB24H, 2, 4, BLOCK_2x4},
    {ALGO_NOP, 0, 0, BLOCK_2x2},
    {ALGO_RGB24H, 2, 2, BLOCK_2x2}
};

static int16_t be16(const uint8_t *p) {
    return (int16_t)(((unsigned)p[0] << 8) | p[1]);
}

/* RGB565 C/Y predictor entries (FFmpeg-compatible shape). */
static int make_cdt16_entry(int p1, int p2, const int16_t *cdt) {
    int cb = cdt[p1], cr = cdt[p2];
    int r = cr, g = (cb + cr) / 2, b = cb;
    int lo = (b & 0x1F) | ((g & 0x3F) << 5) | ((r & 0x1F) << 11);
    int hi = lo;
    (void)hi;
    return ((lo & 0xFFFF) << 1); /* LSB used as end flag later */
}
static int make_ydt16_entry(int p1, int p2, const int16_t *ydt) {
    int y1 = ydt[p1], y2 = ydt[p2];
    int lo = (y1 & 0x1F) | ((y1 & 0x3F) << 5) | ((y1 & 0x1F) << 11);
    int hi = (y2 & 0x1F) | ((y2 & 0x3F) << 5) | ((y2 & 0x1F) << 11);
    return ((hi << 16) | (lo & 0xFFFF)) & ~1;
}

static void gen_vector_table16(duck_tm1 *tm, const uint8_t *sel, size_t sel_size) {
    size_t pos = 0;
    int i, j;
    for (i = 0; i < 1024; i += 4) {
        unsigned len, b;
        if (pos >= sel_size) break;
        b = sel[pos++];
        len = b / 2u;
        if (len == 0 || pos + len > sel_size) break;
        for (j = 0; j < (int)len && j < 4; j++) {
            unsigned char pair = sel[pos++];
            tm->y_pred[i + j] =
                (uint32_t)make_ydt16_entry(pair >> 4, pair & 0xF, tm->ydt) & ~1u;
            tm->c_pred[i + j] =
                (uint32_t)make_cdt16_entry(pair >> 4, pair & 0xF, tm->cdt) & ~1u;
            tm->fat_y_pred[i + j] =
                (uint32_t)make_ydt16_entry(pair >> 4, pair & 0xF, tm->fat_ydt) & ~1u;
            tm->fat_c_pred[i + j] =
                (uint32_t)make_cdt16_entry(pair >> 4, pair & 0xF, tm->fat_cdt) & ~1u;
        }
        if (j > 0) {
            tm->y_pred[i + (j - 1)] |= 1u;
            tm->c_pred[i + (j - 1)] |= 1u;
            tm->fat_y_pred[i + (j - 1)] |= 1u;
            tm->fat_c_pred[i + (j - 1)] |= 1u;
        }
        for (; j < 4; j++) {
            tm->y_pred[i + j] = tm->c_pred[i + j] = 1;
            tm->fat_y_pred[i + j] = tm->fat_c_pred[i + j] = 1;
        }
    }
}

static void select_deltas(duck_tm1 *tm, int idx) {
    int i;
    if (idx < 0 || idx > 3) idx = 0;
    memcpy(tm->ydt, k_ydt[idx], sizeof tm->ydt);
    memcpy(tm->cdt, k_cdt[idx], sizeof tm->cdt);
    memcpy(tm->fat_ydt, k_fat_ydt[idx], sizeof tm->fat_ydt);
    memcpy(tm->fat_cdt, k_fat_cdt[idx], sizeof tm->fat_cdt);
    for (i = 0; i < 8; i++) {
        tm->ydt[i] &= (int16_t)~1;
        tm->ydt[i] /= 2;
    }
}

static int load_deltas_from_duck(duck_tm1 *tm, const uint8_t *d, size_t n) {
    size_t i;
    const int16_t want[] = {0, -2, 2, -6, 6, -12, 12, -12};
    for (i = 0; i + 16 <= n; i++) {
        int ok = 1, k;
        for (k = 0; k < 8; k++) {
            if (be16(d + i + (size_t)k * 2) != want[k]) {
                ok = 0;
                break;
            }
        }
        if (!ok) continue;
        /* Layout after ydt1: ydt2, fat_ydt3, ydt4?, fat… — copy first 4 skinny. */
        if (n - i >= 144) {
            for (k = 0; k < 8; k++) {
                tm->ydt[k] = be16(d + i + (size_t)k * 2);
                tm->ydt[k] &= (int16_t)~1;
                tm->ydt[k] /= 2;
            }
            for (k = 0; k < 8; k++)
                tm->cdt[k] = be16(d + i + 96 + (size_t)k * 2); /* approx cdt1 @ +96 */
            /* fat from following */
            for (k = 0; k < 8; k++)
                tm->fat_ydt[k] = be16(d + i + 32 + (size_t)k * 2);
            for (k = 0; k < 8; k++)
                tm->fat_cdt[k] = be16(d + i + 128 + (size_t)k * 2);
            return 1;
        }
    }
    return 0;
}

static size_t find_vec_start(const uint8_t *d, size_t n) {
    size_t best = 0, best_e = 0, i;
    size_t from = n > 4000 ? n - 4000 : 0;
    for (i = from; i + 100 < n; i++) {
        size_t j = i;
        unsigned e = 0;
        while (j + 1 < n && e < 300) {
            unsigned b = d[j], len = b / 2u;
            if (b == 0 || (b & 1u) || len == 0 || len > 20 || j + 1 + len > n)
                break;
            j += 1 + len;
            e++;
        }
        if (e > best_e) {
            best_e = e;
            best = i;
        }
    }
    return best_e >= 64 ? best : 0;
}

int duck_tm1_init(duck_tm1 *tm, const void *duck19, size_t duck_size) {
    const uint8_t *d = (const uint8_t *)duck19;
    if (!tm) return -1;
    memset(tm, 0, sizeof *tm);
    tm->last_deltaset = tm->last_vectable = -1;
    select_deltas(tm, 0);
    if (d && duck_size > 200) {
        if (load_deltas_from_duck(tm, d, duck_size))
            tm->tables_from_duck = 1;
        {
            size_t vs = find_vec_start(d, duck_size);
            if (vs) {
                tm->vec_tbl[0] = d + vs;
                tm->vec_tbl_size[0] = duck_size - vs;
                tm->tables_from_duck = 1;
            }
        }
    }
    if (!tm->vec_tbl[0]) {
        tm->vec_tbl[0] = k_vec_fallback;
        tm->vec_tbl_size[0] = sizeof k_vec_fallback;
    }
    gen_vector_table16(tm, tm->vec_tbl[0], tm->vec_tbl_size[0]);
    return 0;
}

void duck_tm1_free(duck_tm1 *tm) {
    if (!tm) return;
    free(tm->vert_pred);
    free(tm->rgb565);
    free(tm->rgb24);
    memset(tm, 0, sizeof *tm);
}

static int ensure_bufs(duck_tm1 *tm, unsigned w, unsigned h) {
    if (w < 8 || h < 8 || w > 512 || h > 512) return -1;
    if (tm->width != w || tm->height != h || !tm->vert_pred) {
        free(tm->vert_pred);
        free(tm->rgb565);
        free(tm->rgb24);
        tm->vert_pred = (uint32_t *)calloc(w / 2u + 2u, sizeof(uint32_t));
        tm->rgb565 = (uint16_t *)calloc((size_t)w * h, sizeof(uint16_t));
        tm->rgb24 = (uint8_t *)calloc((size_t)w * h * 3u, 1);
        tm->width = w;
        tm->height = h;
        tm->vert_pred_w = w / 2u;
        if (!tm->vert_pred || !tm->rgb565 || !tm->rgb24) return -1;
    }
    memset(tm->vert_pred, 0, (w / 2u + 2u) * sizeof(uint32_t));
    return 0;
}

static void rgb565_to_rgba320(const uint16_t *src, unsigned sw, unsigned sh,
                              uint8_t *dst) {
    unsigned x, y, ox = 0, oy = 0;
    if (sw < 320) ox = (320 - sw) / 2;
    if (sh < 224) oy = (224 - sh) / 2;
    memset(dst, 0, 320u * 224u * 4u);
    for (y = 0; y < sh && y + oy < 224; y++) {
        for (x = 0; x < sw && x + ox < 320; x++) {
            uint16_t p = src[y * sw + x];
            uint8_t *o = dst + ((size_t)(y + oy) * 320u + (x + ox)) * 4u;
            o[0] = (uint8_t)(((p >> 11) & 31) * 255 / 31);
            o[1] = (uint8_t)(((p >> 5) & 63) * 255 / 63);
            o[2] = (uint8_t)((p & 31) * 255 / 31);
            o[3] = 255;
        }
    }
}

/* Simplified 16-bit TM1 row decoder (C/Y predictors + change bits). */
static void decode_16(duck_tm1 *tm, const uint8_t *mb_change, unsigned mb_size,
                      const uint8_t *index_stream, unsigned index_size,
                      int block_w, int block_h, tm_block btype) {
    unsigned y, w = tm->width, h = tm->height;
    unsigned index_pos = 0, mb_idx = 0;
    uint32_t horiz_pred;
    unsigned pixels_per_line = w / 2u;
    (void)block_h;
    (void)btype;

    for (y = 0; y < h; y++) {
        uint32_t *vert = tm->vert_pred;
        uint16_t *out = tm->rgb565 + (size_t)y * w;
        unsigned px_left = pixels_per_line;
        unsigned col = 0;
        horiz_pred = 0;
        if ((y & 3u) == 0)
            mb_idx = (y / 4u) * ((w + 3u) / 4u);

        while (px_left) {
            int apply = 1;
            if (mb_change && mb_size) {
                unsigned bit = mb_idx / 8u, shift = mb_idx & 7u;
                if (bit < mb_size)
                    apply = (mb_change[bit] >> shift) & 1;
                mb_idx++;
            }
            if (apply && index_pos < index_size) {
                unsigned index = index_stream[index_pos++];
                uint32_t pred;
                /* Chroma transmitted on 2-tall blocks or even lines of 4-tall blocks. */
                if (block_h == 2 || (y & 1u) == 0) {
                    pred = tm->c_pred[index & 1023u];
                    horiz_pred += pred >> 1;
                    if (pred & 1u) {
                        if (index_pos < index_size)
                            index = index_stream[index_pos++];
                        else
                            index = 0;
                    }
                }
                pred = tm->y_pred[index & 1023u];
                horiz_pred += pred >> 1;
                if (pred & 1u) {
                    if (index_pos < index_size)
                        index = index_stream[index_pos++];
                }
            }
            {
                uint32_t pix = *vert + horiz_pred;
                *vert++ = pix;
                out[col * 2] = (uint16_t)(pix & 0xFFFF);
                out[col * 2 + 1] = (uint16_t)((pix >> 16) & 0xFFFF);
            }
            col++;
            px_left--;
            if (block_w == 4 && px_left && apply) {
                /* second pair in 4-wide block: Y only often */
                if (index_pos < index_size) {
                    unsigned index = index_stream[index_pos++];
                    uint32_t pred = tm->y_pred[index & 1023u];
                    horiz_pred += pred >> 1;
                }
                {
                    uint32_t pix = *vert + horiz_pred;
                    *vert++ = pix;
                    out[col * 2] = (uint16_t)(pix & 0xFFFF);
                    out[col * 2 + 1] = (uint16_t)((pix >> 16) & 0xFFFF);
                }
                col++;
                px_left--;
            }
        }
    }
}

int duck_tm1_decode_frame(duck_tm1 *tm, const uint8_t *frame, unsigned frame_size,
                          uint8_t *out_rgba_320x224) {
    unsigned L, method, delta_set, vec_set, fh, fw, header_type, flags;
    unsigned char hdr[64];
    unsigned k;
    const uint8_t *body;
    unsigned body_size;
    unsigned mb_size, index_off;
    tm_algo algo;

    if (!tm || !frame || frame_size < 4) return -1;
    L = (unsigned)(((frame[0] >> 5) | (frame[0] << 3)) & 0x7f);
    if (L < 10 || L > 40 || L >= frame_size) return -1;
    for (k = 0; k + 1 < L && k < 63; k++)
        hdr[k] = (unsigned char)(frame[k + 1] ^ frame[k + 2]);

    method = hdr[0];
    delta_set = hdr[1];
    vec_set = hdr[2];
    fh = (unsigned)hdr[3] | ((unsigned)hdr[4] << 8);
    fw = (unsigned)hdr[5] | ((unsigned)hdr[6] << 8);
    header_type = hdr[10];
    flags = hdr[11];
    (void)header_type;
    (void)flags;

    if (method > 16) return -1;
    algo = k_comp[method].algo;
    if (algo == ALGO_NOP) {
        /* Keep previous frame in out buffer. */
        return 0;
    }
    if (fw < 8 || fh < 8 || fw > 512 || fh > 512) {
        fw = tm->width ? tm->width : 320;
        fh = tm->height ? tm->height : 208;
    }
    if (ensure_bufs(tm, fw, fh)) return -1;

    if ((int)delta_set != tm->last_deltaset || (int)vec_set != tm->last_vectable) {
        if (!tm->tables_from_duck || delta_set <= 3)
            select_deltas(tm, (int)delta_set <= 3 ? (int)delta_set : 0);
        {
            const uint8_t *sel = tm->vec_tbl[0];
            size_t ssz = tm->vec_tbl_size[0];
            if (vec_set > 0 && vec_set < 4 && tm->vec_tbl[vec_set]) {
                sel = tm->vec_tbl[vec_set];
                ssz = tm->vec_tbl_size[vec_set];
            }
            gen_vector_table16(tm, sel, ssz);
        }
        tm->last_deltaset = (int)delta_set;
        tm->last_vectable = (int)vec_set;
    }

    body = frame + L;
    body_size = frame_size - L;
    /* Change bits present only when flags bit 0 is set; else full index stream. */
    if (flags & 1u)
        mb_size = (((fw / 4u) * (fh / 4u)) + 7u) / 8u;
    else
        mb_size = 0u;
    if (mb_size > body_size) mb_size = 0;
    index_off = mb_size;

    if (algo == ALGO_RGB24H) {
        /* Soft 24-bit: reuse 16-bit path into rgb565 as stand-in. */
        decode_16(tm, body, mb_size, body + index_off,
                  body_size > index_off ? body_size - index_off : 0,
                  k_comp[method].block_w, k_comp[method].block_h,
                  k_comp[method].block_type);
    } else {
        decode_16(tm, body, mb_size, body + index_off,
                  body_size > index_off ? body_size - index_off : 0,
                  k_comp[method].block_w, k_comp[method].block_h,
                  k_comp[method].block_type);
    }

    if (out_rgba_320x224)
        rgb565_to_rgba320(tm->rgb565, fw, fh, out_rgba_320x224);
    return 0;
}
