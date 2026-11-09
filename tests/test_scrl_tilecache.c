#include "tile_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

static unsigned g_seed = 7919u;
static unsigned rnd(void) {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

/* Does the cell of a block show character `tag`, dot for dot? */
static int cell_matches(const scrl_tile_cache *c, unsigned block_x, unsigned block_y, unsigned cell,
                        unsigned tag) {
    unsigned row, col;
    for (row = 0; row < 8; row++)
        for (col = 0; col < 8; col++) {
            const unsigned x = block_x * 32u + (cell % 4u) * 8u + col;
            const unsigned y = block_y * 32u + (cell / 4u) * 8u + row;
            if (scrl_tilecache_pixel(c, x, y) != (int)dot(tag, row * 8u + col)) return 0;
        }
    return 1;
}

/* The 70x40 plane of test_layout: 3 x 2 blocks, blocks 3 and 5 without a record. */
static size_t layout_fixture(uint8_t *cdb, size_t *cdb_size, uint8_t *til) {
    uint32_t d[16];
    size_t pos = 0;
    const unsigned own_tags[2] = {10, 11};
    const unsigned flip_tag[1] = {12};
    *cdb_size = make_cdb(cdb, 3);

    all_cells(d, OWN(0)); /* block 4 first: the order of records is not the order of blocks */
    d[0] = OWN(0) | 0x40000000u; /* flip bit: ignored */
    d[1] = OWN(0) | 0x00100000u; /* palette bits: ignored */
    pos = add_record(til, pos, 4, 1, flip_tag, d);

    all_cells(d, OWN(0));
    d[1] = OWN(1);
    d[2] = CDB(0);
    d[3] = CDB(1);
    pos = add_record(til, pos, 0, 2, own_tags, d);

    all_cells(d, CDB(2)); /* block 1: nothing of its own, all from the CDB */
    pos = add_record(til, pos, 1, 0, NULL, d);

    all_cells(d, OWN(0)); /* block 2: points at characters it does not have */
    pos = add_record(til, pos, 2, 0, NULL, d);
    return pos;
}

static int test_layout(void) {
    uint8_t cdb[SCRL_CDB_HEADER_BYTES + 3 * 64], til[6000];
    size_t cdb_size, til_size;
    scrl_tile_cache c;
    unsigned cell;
    til_size = layout_fixture(cdb, &cdb_size, til);

    CHECK(scrl_tilecache_build(&c, 70, 40, cdb, cdb_size, til, til_size) == SCRL_TC_OK);
    CHECK(c.status == SCRL_TC_OK && c.cols == 3u && c.rows == 2u);
    CHECK(c.present_blocks == 4u && c.compressed_blocks == 2u && c.empty_blocks == 1u);
    CHECK(c.bad_cells == 16u && c.lz_failed_blocks == 0u);
    CHECK(c.ignored_bits == 0x40100000u);

    CHECK(cell_matches(&c, 0, 0, 0, 10)); /* own char 0 */
    CHECK(cell_matches(&c, 0, 0, 1, 11)); /* own char 1 */
    CHECK(cell_matches(&c, 0, 0, 2, 50)); /* CDB char 0 */
    CHECK(cell_matches(&c, 0, 0, 3, 51)); /* CDB char 1 */
    for (cell = 4; cell < 16; cell++) CHECK(cell_matches(&c, 0, 0, cell, 10));
    for (cell = 0; cell < 16; cell++) CHECK(cell_matches(&c, 1, 0, cell, 52)); /* block 1 */
    CHECK(cell_matches(&c, 1, 1, 0, 12)); /* block 4: the flips changed nothing */
    CHECK(cell_matches(&c, 1, 1, 3, 12)); /* rows 32..39 are the last ones inside the plane */
    CHECK(scrl_tilecache_pixel(&c, 56, 56) == -1); /* cell 15 of block 4 is below the plane */

    CHECK(scrl_tilecache_pixel(&c, 64, 0) == 0 && scrl_tilecache_pixel(&c, 69, 31) == 0); /* block 2 */
    CHECK(scrl_tilecache_pixel(&c, 0, 32) == -1 && scrl_tilecache_pixel(&c, 31, 39) == -1); /* 3 */
    CHECK(scrl_tilecache_pixel(&c, 64, 32) == -1); /* block 5 */
    CHECK(scrl_tilecache_pixel(&c, 70, 0) == -1 && scrl_tilecache_pixel(&c, 69, 40) == -1); /* edge */
    CHECK(scrl_tilecache_pixel(&c, 69, 39) == -1); /* block 5 is missing, even at the edge */
    CHECK(scrl_tilecache_pixel(&c, 50, 39) > 0); /* block 4: inside the plane */
    scrl_tilecache_free(&c);
    scrl_tilecache_free(&c);
    CHECK(c.status == SCRL_TC_NONE && c.pixels == NULL && scrl_tilecache_pixel(&c, 0, 0) == -1);
    return 0;
}

/* The largest record: 16 characters, every cell a different one. */
static int test_full_record(void) {
    uint8_t cdb[SCRL_CDB_HEADER_BYTES + 64], til[2400];
    unsigned tags[16], cell;
    uint32_t d[16];
    scrl_tile_cache c;
    size_t pos;
    for (cell = 0; cell < 16; cell++) {
        tags[cell] = 100u + cell;
        d[cell] = OWN(15u - cell); /* reversed, so a cell cannot get its own index by luck */
    }
    pos = add_record(til, 0, 0, 16, tags, d);
    CHECK(scrl_tilecache_build(&c, 32, 32, cdb, make_cdb(cdb, 1), til, pos) == SCRL_TC_OK);
    CHECK(c.cols == 1u && c.rows == 1u && c.compressed_blocks == 1u && c.bad_cells == 0u);
    for (cell = 0; cell < 16; cell++) CHECK(cell_matches(&c, 0, 0, cell, tags[15u - cell]));
    scrl_tilecache_free(&c);
    return 0;
}

/* Build must fail with `want`, leave nothing allocated, and not care what `c` held before. */
static int expect_fail(const uint8_t *cdb, size_t cdb_size, const uint8_t *til, size_t til_size,
                       unsigned w, unsigned h, scrl_tc_status want, int line) {
    scrl_tile_cache c;
    memset(&c, 0xAB, sizeof c);
    if (scrl_tilecache_build(&c, w, h, cdb, cdb_size, til, til_size) != want || c.status != want ||
        c.pixels || c.present || c.width_px || c.present_blocks || scrl_tilecache_pixel(&c, 0, 0) != -1) {
        printf("FAIL %d: expected %s, got %s\n", line, scrl_tilecache_status_name(want),
               scrl_tilecache_status_name(c.status));
        scrl_tilecache_free(&c);
        return 1;
    }
    scrl_tilecache_free(&c);
    return 0;
}
#define EXPECT(cdb_, cdb_size_, til_, til_size_, w_, h_, want_) \
    do { \
        if (expect_fail((cdb_), (cdb_size_), (til_), (til_size_), (w_), (h_), (want_), __LINE__)) \
            return 1; \
    } while (0)

static int test_rejects(void) {
    uint8_t cdb[SCRL_CDB_HEADER_BYTES + 64], til[4000], bad[4000];
    const size_t cdb_size = make_cdb(cdb, 1);
    uint32_t d[16];
    const unsigned tags[3] = {1, 2, 3};
    size_t good, end;
    scrl_tile_cache c;

    all_cells(d, OWN(0));
    good = add_record(til, 0, 0, 2, tags, d); /* one good record: block 0 of a 2 x 1 grid */
    CHECK(scrl_tilecache_build(NULL, 64, 32, cdb, cdb_size, til, good) == SCRL_TC_ARG);
    CHECK(scrl_tilecache_build(&c, 64, 32, cdb, cdb_size, til, good) == SCRL_TC_OK);
    scrl_tilecache_free(&c);

    EXPECT(NULL, 0, til, good, 64, 32, SCRL_TC_ARG);
    EXPECT(cdb, SCRL_CDB_HEADER_BYTES - 1u, til, good, 64, 32, SCRL_TC_ARG);
    EXPECT(cdb, cdb_size, NULL, good, 64, 32, SCRL_TC_ARG);
    EXPECT(cdb, cdb_size, til, 0, 64, 32, SCRL_TC_ARG);
    EXPECT(cdb, cdb_size, til, good, 0, 32, SCRL_TC_ARG);
    EXPECT(cdb, cdb_size, til, good, 64, 0, SCRL_TC_ARG);

    /* The grid: 8192 blocks is the limit, and no size may overflow the arithmetic. */
    EXPECT(cdb, cdb_size, til, good, 32u * 300u, 32u * 300u, SCRL_TC_GRID); /* 90000 blocks */
    EXPECT(cdb, cdb_size, til, good, 32u * 256u, 32u * 257u, SCRL_TC_GRID); /* 65792 */
    EXPECT(cdb, cdb_size, til, good, 32u * 65537u, 32u, SCRL_TC_GRID);
    EXPECT(cdb, cdb_size, til, good, 0xFFFFFFFFu, 32u, SCRL_TC_GRID);
    EXPECT(cdb, cdb_size, til, good, 0xFFFFFFFFu, 0xFFFFFFFFu, SCRL_TC_GRID);
    CHECK(scrl_tilecache_build(&c, 32u * 8192u, 32u, cdb, cdb_size, til, good) == SCRL_TC_OK);
    CHECK(c.cols == 8192u && c.present_blocks == 1u);
    scrl_tilecache_free(&c);
    /* The largest plane scrl_cdb_parse accepts, 16384 x 4096 px, is exactly the limit. */
    CHECK(scrl_tilecache_build(&c, 16384u, 4096u, cdb, cdb_size, til, good) == SCRL_TC_OK);
    CHECK(c.cols == 512u && c.rows == 128u && c.present_blocks == 1u);
    scrl_tilecache_free(&c);

    /* Records. */
    memcpy(bad, til, good);
    put16(bad, SCRL_TILE_RECORD_HDR - 1u); /* smaller than its own header */
    EXPECT(cdb, cdb_size, bad, good, 64, 32, SCRL_TC_RECORD);
    memcpy(bad, til, good);
    put16(bad, (unsigned)good + 1u); /* longer than the file */
    EXPECT(cdb, cdb_size, bad, good, 64, 32, SCRL_TC_RECORD);
    memcpy(bad, til, good);
    put16(bad + 2, SCRL_TILE_CHARS_MAX + 1u); /* 17 characters */
    EXPECT(cdb, cdb_size, bad, good, 64, 32, SCRL_TC_RECORD);
    memcpy(bad, til, good);
    put16(bad + 4, 2); /* the grid has blocks 0 and 1 */
    EXPECT(cdb, cdb_size, bad, good, 64, 32, SCRL_TC_RECORD);

    /* Bytes after the last record that cannot be a record. */
    memcpy(bad, til, good);
    memset(bad + good, 0, SCRL_TILE_RECORD_HDR - 1u);
    EXPECT(cdb, cdb_size, bad, good + SCRL_TILE_RECORD_HDR - 1u, 64, 32, SCRL_TC_TRAILING);
    EXPECT(cdb, cdb_size, bad, good + 1u, 64, 32, SCRL_TC_TRAILING);

    /* The same block twice. */
    memcpy(bad, til, good);
    end = add_record(bad, good, 0, 2, tags, d);
    EXPECT(cdb, cdb_size, bad, end, 64, 32, SCRL_TC_DUP);

    /* The characters. A record whose characters do not decompress is dropped, not fatal. */
    memcpy(bad, til, good);
    put16(bad + 2, 3); /* claims 3 characters, the stream holds 2 */
    CHECK(scrl_tilecache_build(&c, 64, 32, cdb, cdb_size, bad, good) == SCRL_TC_OK);
    CHECK(c.lz_failed_blocks == 1u && c.present_blocks == 0u && c.compressed_blocks == 0u);
    CHECK(c.fail.record == 0u && c.fail.count == 3u && c.fail.size == good);
    CHECK(c.fail.lz_got == 128u && c.fail.lz_want == 192u && c.fail.lz_terminated == 1u);
    CHECK(scrl_tilecache_pixel(&c, 0, 0) == -1 && scrl_tilecache_pixel(&c, 40, 0) == -1);
    scrl_tilecache_free(&c);

    memcpy(bad, til, good);
    put16(bad, SCRL_TILE_RECORD_HDR + 5u); /* the stream is cut after 5 bytes */
    CHECK(scrl_tilecache_build(&c, 64, 32, cdb, cdb_size, bad, SCRL_TILE_RECORD_HDR + 5u) ==
          SCRL_TC_OK);
    CHECK(c.lz_failed_blocks == 1u && c.present_blocks == 0u);
    CHECK(c.fail.lz_got < c.fail.lz_want && c.fail.lz_terminated == 0u);
    scrl_tilecache_free(&c);
    {
        lz_enc e; /* a match the format forbids: distance 0 */
        unsigned i;
        memcpy(bad, til, SCRL_TILE_RECORD_HDR);
        put16(bad + 2, 1);
        lz_enc_init(&e, bad + SCRL_TILE_RECORD_HDR, 64);
        lz_enc_literal(&e, 1);
        for (i = 0; i < 4; i++) lz_enc_code(&e, (0u << 4) | 15u);
        lz_enc_end(&e);
        put16(bad, (unsigned)(SCRL_TILE_RECORD_HDR + e.len));
        CHECK(scrl_tilecache_build(&c, 64, 32, cdb, cdb_size, bad, SCRL_TILE_RECORD_HDR + e.len) ==
              SCRL_TC_OK);
        CHECK(c.lz_failed_blocks == 1u && c.present_blocks == 0u);
        CHECK(c.fail.lz_got == 64u && c.fail.lz_want == 64u && c.fail.lz_bad_matches == 4u);
        scrl_tilecache_free(&c);
    }

    /* One dropped record leaves the others alone, and its block still counts as seen. */
    all_cells(d, OWN(0));
    end = add_record(bad, 0, 0, 2, tags, d); /* block 0: will be damaged */
    put16(bad + 2, 3);
    end = add_record(bad, end, 1, 2, tags, d); /* block 1: fine */
    CHECK(scrl_tilecache_build(&c, 64, 32, cdb, cdb_size, bad, end) == SCRL_TC_OK);
    CHECK(c.lz_failed_blocks == 1u && c.present_blocks == 1u && c.compressed_blocks == 1u);
    CHECK(scrl_tilecache_pixel(&c, 0, 0) == -1 && scrl_tilecache_pixel(&c, 33, 0) == (int)dot(1, 1));
    scrl_tilecache_free(&c);
    end = add_record(bad, end, 0, 2, tags, d); /* a second record for the dropped block 0 */
    EXPECT(cdb, cdb_size, bad, end, 64, 32, SCRL_TC_DUP);

    /* A record with no characters ignores whatever follows its header. */
    memset(bad, 0, sizeof bad);
    all_cells(d, CDB(0));
    end = add_record(bad, 0, 1, 0, NULL, d);
    memset(bad + end, 0x5A, 28);
    put16(bad, (unsigned)end + 28u);
    CHECK(scrl_tilecache_build(&c, 64, 32, cdb, cdb_size, bad, end + 28u) == SCRL_TC_OK);
    CHECK(c.present_blocks == 1u && c.compressed_blocks == 0u && c.bad_cells == 0u);
    CHECK(cell_matches(&c, 1, 0, 5, 50));
    scrl_tilecache_free(&c);
    return 0;
}

/* A failed build says which record it stopped at and what it found there. */
static int test_fail_diag(void) {
    uint8_t cdb[SCRL_CDB_HEADER_BYTES + 64], til[4000];
    const size_t cdb_size = make_cdb(cdb, 1);
    const unsigned tags[3] = {1, 2, 3};
    uint32_t d[16];
    size_t pos = 0, bad_at, second_at;
    scrl_tile_cache c;

    all_cells(d, OWN(0));
    pos = add_record(til, pos, 0, 2, tags, d); /* records 0 and 1 are good */
    pos = add_record(til, pos, 1, 2, tags, d);
    bad_at = pos;
    pos = add_record(til, pos, 2, 2, tags, d);
    put16(til + bad_at + 2, 3); /* record 2 claims 3 characters; its stream holds 2 */
    CHECK(scrl_tilecache_build(&c, 96, 32, cdb, cdb_size, til, pos) == SCRL_TC_OK);
    CHECK(c.lz_failed_blocks == 1u && c.present_blocks == 2u); /* records 0 and 1 are in */
    CHECK(c.fail.record == 2u && c.fail.count == 3u && c.fail.size == pos - bad_at);
    CHECK(c.fail.lz_got == 128u && c.fail.lz_want == 192u);
    CHECK(c.fail.lz_terminated == 1u && c.fail.lz_bad_matches == 0u);
    CHECK(scrl_tilecache_pixel(&c, 70, 0) == -1); /* block 2 is the dropped one */
    scrl_tilecache_free(&c);
    CHECK(c.fail.record == 0u && c.fail.lz_want == 0u && c.fail.size == 0u); /* free clears them */

    second_at = pos; /* a second damaged record: the numbers kept are those of the first */
    pos = add_record(til, pos, 3, 2, tags, d);
    put16(til + second_at + 2, 4);
    CHECK(scrl_tilecache_build(&c, 128, 32, cdb, cdb_size, til, pos) == SCRL_TC_OK);
    CHECK(c.lz_failed_blocks == 2u && c.present_blocks == 2u);
    CHECK(c.fail.record == 2u && c.fail.count == 3u && c.fail.lz_want == 192u);
    scrl_tilecache_free(&c);

    pos = add_record(til, bad_at, 0, 2, tags, d); /* record 2 again, now the id of record 0 */
    CHECK(scrl_tilecache_build(&c, 96, 32, cdb, cdb_size, til, pos) == SCRL_TC_DUP);
    CHECK(c.fail.record == 2u && c.fail.count == 2u && c.fail.size == pos - bad_at);
    CHECK(c.fail.lz_want == 0u); /* it was not a compression failure */

    CHECK(scrl_tilecache_build(&c, 96, 32, cdb, cdb_size, til, bad_at) == SCRL_TC_OK);
    CHECK(c.fail.record == 0u && c.fail.count == 0u && c.fail.size == 0u); /* nothing to report */
    CHECK(c.fail.lz_want == 0u && c.fail.lz_got == 0u);
    scrl_tilecache_free(&c);
    return 0;
}

/* Whatever the bytes are, the build returns, and whatever it accepts can be read everywhere. */
static int read_everything(const scrl_tile_cache *c) {
    unsigned x, y;
    for (y = 0; y < c->height_px; y += 3)
        for (x = 0; x < c->width_px; x += 3) {
            const int v = scrl_tilecache_pixel(c, x, y);
            if (v < -1 || v > 255) return 0;
        }
    return 1;
}

static int test_fuzz(void) {
    uint8_t cdb[SCRL_CDB_HEADER_BYTES + 3 * 64], til[6000], mut[6000], noise[900], cnoise[400];
    size_t cdb_size, til_size, i;
    unsigned trial, accepted = 0;
    scrl_tile_cache c;
    til_size = layout_fixture(cdb, &cdb_size, til);

    for (trial = 0; trial < 4000; trial++) { /* the valid fixture with a few bytes damaged */
        const unsigned hits = 1u + rnd() % 3u;
        unsigned h;
        memcpy(mut, til, til_size);
        for (h = 0; h < hits; h++) mut[rnd() % til_size] = (uint8_t)rnd();
        if (scrl_tilecache_build(&c, 70, 40, cdb, cdb_size, mut, til_size) == SCRL_TC_OK) {
            accepted++;
            CHECK(read_everything(&c));
            CHECK(c.present_blocks <= c.cols * c.rows && c.empty_blocks <= c.present_blocks);
        }
        scrl_tilecache_free(&c);
    }
    CHECK(accepted > 0u && accepted < 4000u); /* damage is sometimes harmless, sometimes caught */

    for (trial = 0; trial < 4000; trial++) { /* and pure noise */
        const size_t n = rnd() % sizeof noise, cn = SCRL_CDB_HEADER_BYTES + rnd() % 200u;
        for (i = 0; i < n; i++) noise[i] = (uint8_t)rnd();
        for (i = 0; i < cn; i++) cnoise[i] = (uint8_t)rnd();
        if (scrl_tilecache_build(&c, 1u + rnd() % 300u, 1u + rnd() % 300u, cnoise, cn, noise, n) ==
            SCRL_TC_OK)
            CHECK(read_everything(&c));
        scrl_tilecache_free(&c);
    }
    return 0;
}

static int test_names(void) {
    int s;
    for (s = SCRL_TC_NONE; s <= SCRL_TC_NOMEM; s++) {
        const char *name = scrl_tilecache_status_name((scrl_tc_status)s);
        CHECK(name != NULL && name[0] != '\0' && strcmp(name, "unknown") != 0);
    }
    CHECK(strcmp(scrl_tilecache_status_name((scrl_tc_status)99), "unknown") == 0);
    return 0;
}

int main(void) {
    if (test_layout() || test_full_record() || test_rejects() || test_fail_diag() || test_fuzz() ||
        test_names())
        return 1;
    printf("OK scrl_tilecache\n");
    return 0;
}
