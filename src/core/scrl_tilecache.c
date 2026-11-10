#include "scrl_tilecache.h"
#include "scrl_decomp.h"
#include <stdlib.h>
#include <string.h>

static unsigned rd16(const uint8_t *p) {
    return ((unsigned)p[0] << 8) | p[1];
}

static uint32_t rd32(const uint8_t *p) {
    return ((uint32_t)rd16(p) << 16) | rd16(p + 2);
}

/* Values of scrl_tile_cache.present[]. */
#define BLOCK_MISSING 0u
#define BLOCK_USABLE 1u
#define BLOCK_DROPPED 2u /* it had a record, but the characters did not decompress */

static void note(scrl_tile_cache *c, unsigned index, unsigned count, size_t size) {
    c->fail.record = index;
    c->fail.count = count;
    c->fail.size = (unsigned)size;
}

void scrl_tilecache_free(scrl_tile_cache *c) {
    if (!c) return;
    free(c->pixels);
    free(c->present);
    free(c->cell_bank);
    memset(c, 0, sizeof *c);
}

/* Release everything but keep the diagnostics of the failure. */
static scrl_tc_status fail(scrl_tile_cache *c, scrl_tc_status status) {
    const scrl_tc_fail why = c->fail;
    scrl_tilecache_free(c);
    c->fail = why;
    c->status = status;
    return status;
}

const char *scrl_tilecache_status_name(scrl_tc_status status) {
    switch (status) {
    case SCRL_TC_NONE: return "not-built";
    case SCRL_TC_OK: return "ok";
    case SCRL_TC_ARG: return "bad-argument";
    case SCRL_TC_GRID: return "bad-grid";
    case SCRL_TC_RECORD: return "bad-record";
    case SCRL_TC_DUP: return "duplicate-block";
    case SCRL_TC_TRAILING: return "trailing-bytes";
    case SCRL_TC_NOMEM: return "out-of-memory";
    }
    return "unknown";
}

/* Copy one 8x8 character into a block at cell (cx, cy), with optional VDP2 H/V flip. */
static void put_char(uint8_t *block, unsigned cx, unsigned cy, const uint8_t *src, int hflip,
                     int vflip) {
    unsigned row, col;
    uint8_t *dst = block + (cy * 8u) * SCRL_TILE_PX + cx * 8u;
    for (row = 0; row < 8u; row++) {
        const unsigned sr = vflip ? (7u - row) : row;
        for (col = 0; col < 8u; col++) {
            const unsigned sc = hflip ? (7u - col) : col;
            dst[row * SCRL_TILE_PX + col] = src[sr * 8u + sc];
        }
    }
}

static int block_is_empty(const uint8_t *block) {
    unsigned i;
    for (i = 0; i < SCRL_TILE_BLOCK_BYTES; i++)
        if (block[i] != 0u) return 0;
    return 1;
}

/* Expand the 16 cells of the record at `rec` (`count` own characters) into `block`. */
static void expand_record(scrl_tile_cache *c, uint8_t *block, const uint8_t *rec,
                          const uint8_t *chars, unsigned count, const uint8_t *cdb_payload,
                          size_t cdb_payload_size) {
    unsigned cell;
    const size_t block_index = (size_t)(block - c->pixels) / SCRL_TILE_BLOCK_BYTES;
    for (cell = 0; cell < 16u; cell++) {
        const uint32_t desc = rd32(rec + 8u + cell * 4u);
        const size_t off = (size_t)(desc & SCRL_DESC_CHAR_MASK) * 32u;
        const int from_cdb = (desc & SCRL_DESC_CDB_FLAG) != 0u;
        const uint8_t *base = from_cdb ? cdb_payload : chars;
        const size_t avail = from_cdb ? cdb_payload_size : (size_t)count * SCRL_TILE_CHAR_BYTES;
        const unsigned bank = (desc & SCRL_DESC_PAL_MASK) >> SCRL_DESC_PAL_SHIFT;
        c->cell_bank[block_index * 16u + cell] = (uint8_t)(bank & 3u);
        c->ignored_bits |= desc & ~SCRL_DESC_KNOWN_MASK;
        if (off > avail || avail - off < SCRL_TILE_CHAR_BYTES) {
            c->bad_cells++; /* the cell stays 0: transparent, never garbage */
            continue;
        }
        put_char(block, cell % 4u, cell / 4u, base + off, (desc & SCRL_DESC_HFLIP) != 0u,
                 (desc & SCRL_DESC_VFLIP) != 0u);
    }
}

/* Clean decode at payload `off`: exact count*64 bytes and no forbidden matches. */
static int decomp_clean(const uint8_t *rec, size_t size, unsigned count, unsigned off,
                        uint8_t *chars, size_t want, scrl_decomp_info *info) {
    size_t got;
    if (size <= (size_t)off) return 0;
    got = scrl_decomp_byte_ex(rec + off, size - (size_t)off, chars, want, info);
    return got == want && info->bad_matches == 0u;
}

/*
 * Walk the TIL once for structure / voting, then decode with the winning payload offset.
 * Packed (+0x48) wins ties and the empty-vote case; heap (+0x88) wins when it has more
 * clean decodes (MAUSO plane A). size==0 after a prior record ends the walk (VALLEY A).
 */
scrl_tc_status scrl_tilecache_build(scrl_tile_cache *c, unsigned width_px, unsigned height_px,
                                    const uint8_t *cdb, size_t cdb_size, const uint8_t *til,
                                    size_t til_size) {
    uint8_t chars[SCRL_TILE_CHARS_MAX * SCRL_TILE_CHAR_BYTES];
    size_t pos = 0, blocks, limit = til_size;
    unsigned cols, rows, index, votes48 = 0u, votes88 = 0u, payload_off;
    if (!c) return SCRL_TC_ARG;
    memset(c, 0, sizeof *c);
    if (!cdb || cdb_size < SCRL_CDB_HEADER_BYTES || !til || til_size == 0u || width_px == 0u ||
        height_px == 0u)
        return fail(c, SCRL_TC_ARG);
    cols = width_px / SCRL_TILE_PX + (width_px % SCRL_TILE_PX != 0u);
    rows = height_px / SCRL_TILE_PX + (height_px % SCRL_TILE_PX != 0u);
    if (cols > SCRL_TILE_BLOCKS_MAX || rows > SCRL_TILE_BLOCKS_MAX ||
        (size_t)cols * rows > SCRL_TILE_BLOCKS_MAX)
        return fail(c, SCRL_TC_GRID);
    blocks = (size_t)cols * rows;
    c->pixels = (uint8_t *)calloc(blocks, SCRL_TILE_BLOCK_BYTES);
    c->present = (uint8_t *)calloc(blocks, 1u);
    c->cell_bank = (uint8_t *)calloc(blocks, 16u);
    if (!c->pixels || !c->present || !c->cell_bank) return fail(c, SCRL_TC_NOMEM);
    c->cols = cols;
    c->rows = rows;
    c->width_px = width_px;
    c->height_px = height_px;

    /* Pass 1: validate structure, vote packed vs heap, stop at size-0 sentinel. */
    for (index = 0; pos < til_size; index++) {
        const uint8_t *rec = til + pos;
        size_t size;
        unsigned count, id;
        if (til_size - pos < SCRL_TILE_RECORD_HDR) {
            note(c, index, 0u, til_size - pos);
            return fail(c, SCRL_TC_TRAILING);
        }
        size = rd16(rec);
        count = rd16(rec + 2);
        id = rd16(rec + 4);
        if (size == 0u && index > 0u) {
            limit = pos; /* end sentinel: keep prior records, ignore the rest */
            break;
        }
        if (size < SCRL_TILE_RECORD_HDR || size > til_size - pos || count > SCRL_TILE_CHARS_MAX ||
            id >= blocks) {
            note(c, index, count, size);
            return fail(c, SCRL_TC_RECORD);
        }
        if (c->present[id] != BLOCK_MISSING) {
            note(c, index, count, size);
            return fail(c, SCRL_TC_DUP);
        }
        c->present[id] = BLOCK_DROPPED; /* reserve id for the duplicate check; decode resets it */
        if (count > 0u) {
            scrl_decomp_info info;
            const size_t want = (size_t)count * SCRL_TILE_CHAR_BYTES;
            if (decomp_clean(rec, size, count, SCRL_TILE_RECORD_HDR, chars, want, &info))
                votes48++;
            if (decomp_clean(rec, size, count, SCRL_TILE_HEAP_PAYLOAD, chars, want, &info))
                votes88++;
        }
        pos += size;
    }
    payload_off = (votes88 > votes48) ? SCRL_TILE_HEAP_PAYLOAD : SCRL_TILE_RECORD_HDR;
    c->heap_mode = (payload_off == SCRL_TILE_HEAP_PAYLOAD) ? 1u : 0u;
    memset(c->present, 0, blocks); /* clear the pass-1 reservations */

    /* Pass 2: decode with the winning offset up to `limit`. */
    for (index = 0, pos = 0; pos < limit; index++) {
        const uint8_t *rec = til + pos;
        size_t size = rd16(rec);
        unsigned count = rd16(rec + 2);
        unsigned id = rd16(rec + 4);
        uint8_t *block;
        if (count > 0u) {
            scrl_decomp_info info;
            const size_t want = (size_t)count * SCRL_TILE_CHAR_BYTES;
            size_t got;
            if (size > (size_t)payload_off)
                got = scrl_decomp_byte_ex(rec + payload_off, size - (size_t)payload_off, chars, want,
                                          &info);
            else {
                memset(&info, 0, sizeof info);
                got = 0u;
            }
            if (got != want || info.bad_matches != 0u) {
                if (c->lz_failed_blocks++ == 0u) {
                    note(c, index, count, size);
                    c->fail.lz_got = got;
                    c->fail.lz_want = want;
                    c->fail.lz_bad_matches = info.bad_matches;
                    c->fail.lz_terminated = info.terminated;
                }
                c->present[id] = BLOCK_DROPPED;
                pos += size;
                continue;
            }
            c->compressed_blocks++;
        }
        block = c->pixels + (size_t)id * SCRL_TILE_BLOCK_BYTES;
        expand_record(c, block, rec, chars, count, cdb + SCRL_CDB_HEADER_BYTES,
                      cdb_size - SCRL_CDB_HEADER_BYTES);
        c->present[id] = BLOCK_USABLE;
        c->present_blocks++;
        if (block_is_empty(block)) c->empty_blocks++;
        pos += size;
    }
    c->status = SCRL_TC_OK;
    return SCRL_TC_OK;
}

int scrl_tilecache_pixel(const scrl_tile_cache *c, unsigned x, unsigned y) {
    size_t block;
    if (!c || c->status != SCRL_TC_OK || x >= c->width_px || y >= c->height_px) return -1;
    block = (size_t)(y / SCRL_TILE_PX) * c->cols + (x / SCRL_TILE_PX);
    if (c->present[block] != BLOCK_USABLE) return -1;
    return c->pixels[block * SCRL_TILE_BLOCK_BYTES + (y % SCRL_TILE_PX) * SCRL_TILE_PX +
                     (x % SCRL_TILE_PX)];
}

int scrl_tilecache_cram(const scrl_tile_cache *c, unsigned x, unsigned y) {
    size_t block, cell;
    unsigned bank;
    const int dot = scrl_tilecache_pixel(c, x, y);
    if (dot < 0) return -1;
    if (dot == 0 || !c->cell_bank) return 0;
    block = (size_t)(y / SCRL_TILE_PX) * c->cols + (x / SCRL_TILE_PX);
    cell = (size_t)((y % SCRL_TILE_PX) / 8u) * 4u + ((x % SCRL_TILE_PX) / 8u);
    bank = c->cell_bank[block * 16u + cell] & 3u;
    return (int)((bank << 8) | (unsigned)dot);
}
