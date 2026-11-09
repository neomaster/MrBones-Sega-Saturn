#ifndef MB_TILE_FIXTURE_H
#define MB_TILE_FIXTURE_H
#include "core/scrl_tilecache.h"
#include "lz_test_enc.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Synthetic planes for the tile cache tests: a CDB and a TIL built in memory from a few
 * made-up characters, in the layout documented in core/scrl_tilecache.h. No game data.
 */

#define OWN(n) ((uint32_t)(n) * 2u) /* character n of the record: byte offset n * 64 = desc * 32 */
#define CDB(n) (SCRL_DESC_CDB_FLAG | (uint32_t)(n) * 2u)

static inline void put16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static inline void put32(uint8_t *p, uint32_t v) {
    put16(p, (unsigned)(v >> 16));
    put16(p + 2, (unsigned)(v & 0xFFFFu));
}

/* Dot i of the character made with `tag`: never 0, so it cannot pass for transparent. */
static inline uint8_t dot(unsigned tag, unsigned i) {
    return (uint8_t)((tag * 37u + i) % 255u + 1u);
}

static inline void make_char(uint8_t *p, unsigned tag) {
    unsigned i;
    for (i = 0; i < SCRL_TILE_CHAR_BYTES; i++) p[i] = dot(tag, i);
}

/* A CDB of `n` characters tagged 50, 51, ... Its header gets plane size w x h (0 = unset). */
static inline size_t make_cdb_sized(uint8_t *cdb, unsigned n, unsigned w, unsigned h) {
    unsigned i;
    memset(cdb, 0, SCRL_CDB_HEADER_BYTES);
    put16(cdb, w);
    put16(cdb + 2, h);
    put16(cdb + 4, w ? 8u : 0u);
    put16(cdb + 6, h ? 8u : 0u);
    put16(cdb + 8, n);
    for (i = 0; i < n; i++) make_char(cdb + SCRL_CDB_HEADER_BYTES + i * SCRL_TILE_CHAR_BYTES, 50u + i);
    return SCRL_CDB_HEADER_BYTES + (size_t)n * SCRL_TILE_CHAR_BYTES;
}

static inline size_t make_cdb(uint8_t *cdb, unsigned n) {
    return make_cdb_sized(cdb, n, 0, 0);
}

/* Append a record carrying `count` characters made from `tags`; returns the new end. */
static inline size_t add_record(uint8_t *til, size_t pos, unsigned id, unsigned count,
                                const unsigned *tags, const uint32_t desc[16]) {
    uint8_t raw[SCRL_TILE_CHARS_MAX * SCRL_TILE_CHAR_BYTES], lz[2400];
    size_t lz_len = 0, size, i;
    for (i = 0; i < count; i++) make_char(raw + i * SCRL_TILE_CHAR_BYTES, tags[i]);
    if (count) lz_len = lz_encode(raw, (size_t)count * SCRL_TILE_CHAR_BYTES, lz, sizeof lz);
    size = SCRL_TILE_RECORD_HDR + lz_len;
    memset(til + pos, 0, SCRL_TILE_RECORD_HDR);
    put16(til + pos, (unsigned)size);
    put16(til + pos + 2, count);
    put16(til + pos + 4, id);
    for (i = 0; i < 16; i++) put32(til + pos + 8 + i * 4, desc[i]);
    if (lz_len) memcpy(til + pos + SCRL_TILE_RECORD_HDR, lz, lz_len);
    return pos + size;
}

static inline void all_cells(uint32_t desc[16], uint32_t value) {
    unsigned i;
    for (i = 0; i < 16; i++) desc[i] = value;
}

#endif
