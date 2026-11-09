#ifndef MB_LZ_TEST_ENC_H
#define MB_LZ_TEST_ENC_H
#include <stddef.h>
#include <stdint.h>

/*
 * Test-only encoder for the stream format of core/scrl_decomp.h. It exists so the tests can
 * build compressed fixtures from plain data without any game file. Greedy matching, window
 * 4095, lengths 2..16, and a terminator at the end. Also usable item by item to hand-craft
 * streams (including ones the format forbids, to exercise the decoder's error reporting).
 */

typedef struct {
    uint8_t *out;
    size_t cap, len;
    unsigned ctrl, items;
    uint8_t body[16];
    unsigned body_len;
    int overflow;
} lz_enc;

static inline void lz_enc_init(lz_enc *e, uint8_t *out, size_t cap) {
    e->out = out;
    e->cap = cap;
    e->len = 0;
    e->ctrl = 0;
    e->items = 0;
    e->body_len = 0;
    e->overflow = 0;
}

static inline void lz_enc_flush(lz_enc *e) {
    unsigned i;
    if (e->items == 0) return;
    if (e->len + 1u + e->body_len > e->cap) {
        e->overflow = 1;
    } else {
        e->out[e->len++] = (uint8_t)e->ctrl;
        for (i = 0; i < e->body_len; i++) e->out[e->len++] = e->body[i];
    }
    e->ctrl = 0;
    e->items = 0;
    e->body_len = 0;
}

static inline void lz_enc_literal(lz_enc *e, uint8_t b) {
    e->body[e->body_len++] = b;
    if (++e->items == 8u) lz_enc_flush(e);
}

/* A match item with an arbitrary 16-bit code: dist << 4 | nibble. */
static inline void lz_enc_code(lz_enc *e, unsigned code) {
    e->ctrl |= 1u << (7u - e->items);
    e->body[e->body_len++] = (uint8_t)(code >> 8);
    e->body[e->body_len++] = (uint8_t)(code & 0xFFu);
    if (++e->items == 8u) lz_enc_flush(e);
}

static inline void lz_enc_match(lz_enc *e, unsigned dist, unsigned len) {
    lz_enc_code(e, (dist << 4) | (len - 1u));
}

static inline void lz_enc_end(lz_enc *e) {
    lz_enc_code(e, 0u); /* nibble 0: the terminator */
    lz_enc_flush(e);
}

/* Encode in[0..n). Returns the stream length, 0 when `cap` is too small. */
static inline size_t lz_encode(const uint8_t *in, size_t n, uint8_t *out, size_t cap) {
    lz_enc e;
    size_t i = 0;
    lz_enc_init(&e, out, cap);
    while (i < n) {
        size_t best_len = 0, best_dist = 0, dist;
        const size_t max_dist = i < 4095u ? i : 4095u;
        for (dist = 1; dist <= max_dist; dist++) {
            size_t k = 0;
            while (k < 16u && i + k < n && in[i + k] == in[i + k - dist]) k++;
            if (k > best_len) {
                best_len = k;
                best_dist = dist;
                if (k == 16u) break;
            }
        }
        if (best_len >= 2u) {
            lz_enc_match(&e, (unsigned)best_dist, (unsigned)best_len);
            i += best_len;
        } else {
            lz_enc_literal(&e, in[i]);
            i++;
        }
    }
    lz_enc_end(&e);
    return e.overflow ? 0u : e.len;
}

#endif
