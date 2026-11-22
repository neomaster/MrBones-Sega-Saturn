#include "core/cdimage.h"
#include "core/identity.h"
#include "core/mb_font.h"
#include "core/resource.h"
#include "core/scrl_map.h"
#include "tile_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

/*
 * Test palette: CRAM entry i is (i & 255, 255 - (i & 255), (i & 255) / 2), so entry 0, the
 * back colour, is (0, 255, 0). Where nothing is drawn the strip shows the back colour; an
 * opaque dot replaces it with its own palette entry, unblended.
 */
static int strip_px_is(const uint8_t *strip, unsigned w, unsigned x, unsigned y, unsigned dot_value) {
    const uint8_t *p = strip + ((size_t)y * w + x) * 4u;
    const unsigned r = dot_value;
    const unsigned g = 255u - dot_value;
    const unsigned b = dot_value / 2u;
    return p[0] == r && p[1] == g && p[2] == b && p[3] == 255u;
}

/* Disc-less: the decoded tiles of a plane reach the strip, and noise in place of a TIL does not. */
static int test_cache_drawn(void) {
    static uint8_t strip[288u * 64u * 4u];
    uint8_t cdb[SCRL_CDB_HEADER_BYTES + 2 * 64], til[3000], noise[600], pal[4096];
    const size_t cdb_size = make_cdb_sized(cdb, 2, 288, 64);
    const unsigned tag5[1] = {5};
    size_t til_size = 0;
    uint32_t d[16];
    scrl_map map;
    unsigned i, x, y, seed = 99u;

    all_cells(d, OWN(0)); /* block 0: its own character 5 in every cell */
    til_size = add_record(til, til_size, 0, 1, tag5, d);
    all_cells(d, CDB(0)); /* block 1: the CDB's character 50 in every cell */
    til_size = add_record(til, til_size, 1, 0, NULL, d);
    all_cells(d, OWN(0)); /* block 2: points at nothing, so it is transparent */
    til_size = add_record(til, til_size, 2, 0, NULL, d);
    memset(pal, 0, sizeof pal);
    for (i = 0; i < 1024; i++) {
        pal[4 * i + 1] = (uint8_t)(i & 0xffu);
        pal[4 * i + 2] = (uint8_t)(255u - (i & 0xffu));
        pal[4 * i + 3] = (uint8_t)((i & 0xffu) / 2u);
    }

    CHECK(scrl_map_init(&map, cdb, cdb_size, til, til_size, NULL, 0, NULL, 0, NULL, 0, NULL, 0, NULL,
                        0, NULL, 0, NULL, 0) == 0);
    CHECK(map.plane_count == 1u && map.planes[0].pixel_w == 288u && map.planes[0].pixel_h == 64u);
    CHECK(scrl_map_cached_planes(&map) == 1u);
    CHECK(scrl_map_draw(&map, pal, strip, 288, 64, 0) == 0);
    CHECK(strip_px_is(strip, 288, 3, 2, dot(5, 2u * 8u + 3u))); /* block 0, cell 0 */
    CHECK(strip_px_is(strip, 288, 31, 31, dot(5, 7u * 8u + 7u))); /* block 0, cell 15, last dot */
    CHECK(strip_px_is(strip, 288, 32 + 9, 8 + 5, dot(50, 5u * 8u + 1u))); /* block 1, cell 5 */
    CHECK(strip_px_is(strip, 288, 64 + 3, 3, 0)); /* block 2: transparent */
    CHECK(strip_px_is(strip, 288, 100, 0, 0)); /* block 3: no record */
    CHECK(strip_px_is(strip, 288, 5, 40, 0)); /* block row 1 has no records */

    CHECK(scrl_map_draw(&map, pal, strip, 288, 64, 5) == 0); /* the camera moves the view */
    CHECK(strip_px_is(strip, 288, 0, 2, dot(5, 2u * 8u + 5u)));
    CHECK(scrl_map_draw(&map, pal, strip, 288, 64, 287) == 0); /* and the plane wraps */
    CHECK(strip_px_is(strip, 288, 0, 0, 0)); /* plane x 287: block 8 has no record */
    CHECK(strip_px_is(strip, 288, 1, 0, dot(5, 0))); /* plane x 0 again */
    scrl_map_free(&map);
    CHECK(map.plane_count == 0u && scrl_map_cached_planes(&map) == 0u);

    /* The same plane with noise where the TIL should be: nothing is drawn, nothing is noise. */
    for (i = 0; i < sizeof noise; i++) {
        seed = seed * 1664525u + 1013904223u;
        noise[i] = (uint8_t)(seed >> 16);
    }
    CHECK(scrl_map_init(&map, cdb, cdb_size, noise, sizeof noise, NULL, 0, NULL, 0, NULL, 0, NULL, 0,
                        NULL, 0, NULL, 0, NULL, 0) == 0);
    CHECK(map.plane_count == 1u && scrl_map_cached_planes(&map) == 0u);
    CHECK(map.planes[0].tiles.status != SCRL_TC_OK);
    CHECK(scrl_map_draw(&map, pal, strip, 288, 64, 0) == 0);
    for (y = 0; y < 64; y++)
        for (x = 0; x < 288; x++) CHECK(strip_px_is(strip, 288, x, y, 0));
    /* A zero CRAM entry 0 gives a clean black back colour, with no bands down the strip. */
    pal[1] = pal[2] = pal[3] = 0;
    CHECK(scrl_map_draw(&map, pal, strip, 288, 64, 0) == 0);
    for (y = 0; y < 64; y++)
        for (x = 0; x < 288; x++) {
            const uint8_t *p = strip + ((size_t)y * 288u + x) * 4u;
            CHECK(p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 255u);
        }
    scrl_map_free(&map);
    return 0;
}

/* What the cache made of a real plane, as counts only. */
static void print_cache(const char *stage, const scrl_map *m) {
    unsigned i;
    for (i = 0; i < m->plane_count; i++) {
        const scrl_tile_cache *c = &m->planes[i].tiles;
        printf("%s %s: plane=%ux%u cache=%s grid=%ux%u present=%u compressed=%u empty=%u bad_cells=%u "
               "heap=%u ignored=0x%08X\n",
               stage, m->planes[i].cdb_name, m->planes[i].pixel_w, m->planes[i].pixel_h,
               scrl_tilecache_status_name(c->status), c->cols, c->rows, c->present_blocks,
               c->compressed_blocks, c->empty_blocks, c->bad_cells, c->heap_mode,
               (unsigned)c->ignored_bits);
        if (c->status != SCRL_TC_OK || c->lz_failed_blocks)
            printf("%s %s: lz_failed_blocks=%u first problem at record %u (count=%u size=%u) lz got=%u "
                   "want=%u bad_matches=%u terminated=%u til_size=%u\n",
                   stage, m->planes[i].cdb_name, c->lz_failed_blocks, c->fail.record, c->fail.count,
                   c->fail.size, (unsigned)c->fail.lz_got, (unsigned)c->fail.lz_want,
                   c->fail.lz_bad_matches, c->fail.lz_terminated, (unsigned)m->planes[i].til_size);
    }
}

/* Descriptor palette bank shifts the CRAM index used by scrl_map_draw. */
static int test_palette_bank_draw(void) {
    static uint8_t strip[32u * 32u * 4u];
    uint8_t cdb[SCRL_CDB_HEADER_BYTES + 64], til[2000], pal[4096];
    const size_t cdb_size = make_cdb_sized(cdb, 1, 32, 32);
    const unsigned tags[1] = {7};
    uint32_t d[16];
    size_t til_size;
    scrl_map map;
    unsigned i;
    const uint8_t *px;

    all_cells(d, OWN(0) | (2u << SCRL_DESC_PAL_SHIFT)); /* bank 2 */
    til_size = add_record(til, 0, 0, 1, tags, d);
    memset(pal, 0, sizeof pal);
    for (i = 0; i < 1024; i++) {
        pal[4 * i + 1] = (uint8_t)((i >> 8) * 40u + (i & 0xffu));
        pal[4 * i + 2] = (uint8_t)(i & 0xffu);
        pal[4 * i + 3] = (uint8_t)(255u - (i & 0xffu));
    }
    CHECK(scrl_map_init(&map, cdb, cdb_size, til, til_size, NULL, 0, NULL, 0, NULL, 0, NULL, 0,
                        NULL, 0, NULL, 0, NULL, 0) == 0);
    CHECK(scrl_map_draw(&map, pal, strip, 32, 32, 0) == 0);
    px = strip;
    {
        const unsigned dotv = dot(7, 0);
        const unsigned cram = (2u << 8) | dotv;
        /* The opaque dot is the palette entry itself: nothing of the back colour leaks in. */
        CHECK(px[0] == pal[4 * cram + 1] && px[1] == pal[4 * cram + 2] &&
              px[2] == pal[4 * cram + 3] && px[3] == 255u);
        CHECK(pal[4 * cram + 1] != pal[1] || pal[4 * cram + 3] != pal[3]); /* differs from back */
        CHECK(scrl_tilecache_cram(&map.planes[0].tiles, 0, 0) == (int)cram);
    }
    scrl_map_free(&map);
    return 0;
}

/*
 * Per-plane parallax and base_y, and the back screen. Plane A is nine 32-pixel blocks, block b
 * made of character tag b in every cell, so the dot at plane (c, r) is dot(c / 32, (r % 8) * 8 +
 * c % 8) and its red channel names it.
 */
static int test_parallax_and_back(void) {
    static uint8_t strip[288u * 32u * 4u];
    uint8_t cdb[SCRL_CDB_HEADER_BYTES + 64], til[9u * 400u], pal[4096];
    const size_t cdb_size = make_cdb_sized(cdb, 1, 288, 64);
    size_t til_size = 0;
    uint32_t d[16];
    scrl_map map;
    unsigned i, b;

    for (b = 0; b < 9u; b++) {
        const unsigned tags[1] = {b};
        all_cells(d, OWN(0));
        til_size = add_record(til, til_size, b, 1, tags, d);
    }
    memset(pal, 0, sizeof pal);
    for (i = 0; i < 1024; i++) {
        pal[4 * i + 1] = (uint8_t)(i & 0xffu);
        pal[4 * i + 2] = (uint8_t)(255u - (i & 0xffu));
        pal[4 * i + 3] = (uint8_t)((i & 0xffu) / 2u);
    }
    CHECK(scrl_map_init(&map, cdb, cdb_size, til, til_size, NULL, 0, NULL, 0, NULL, 0, NULL, 0,
                        NULL, 0, NULL, 0, NULL, 0) == 0);
    CHECK(scrl_map_cached_planes(&map) == 1u);

    /* Defaults: plane A follows the camera one to one and is not shifted. */
    CHECK(map.planes[0].parallax_coeff == SCRL_PARALLAX_ONE && map.planes[0].base_y == 0);
    CHECK(scrl_map_draw(&map, pal, strip, 288, 32, 40) == 0);
    CHECK(strip[(0u * 288u + 3u) * 4u] == dot(43u / 32u, 43u % 8u));

    /* coeff 0x3000 is 3/16 of the camera: camera 160 puts plane column 30 at image column 0. */
    CHECK(scrl_parallax_offset(160, 0x3000) == 30);
    CHECK(scrl_parallax_offset(100, -0x8000) == -50);
    CHECK(scrl_parallax_offset(100, 0) == 0);
    CHECK(scrl_map_set_plane_parallax(&map, 0, 0x3000, 0) == 0);
    CHECK(scrl_map_draw(&map, pal, strip, 288, 32, 160) == 0);
    CHECK(strip[(0u * 288u + 0u) * 4u] == dot(30u / 32u, 30u % 8u));
    CHECK(strip[(0u * 288u + 9u) * 4u] == dot(39u / 32u, 39u % 8u));
    /* A negative coefficient scrolls the other way and wraps at the plane width. */
    CHECK(scrl_map_set_plane_parallax(&map, 0, -0x8000, 0) == 0);
    CHECK(scrl_map_draw(&map, pal, strip, 288, 32, 20) == 0); /* offset -10: column 278 at x=0 */
    CHECK(strip[(0u * 288u + 0u) * 4u] == dot(278u / 32u, 278u % 8u));

    /* base_y shifts the plane down: image row 3 shows plane row 0, and rows above wrap to the
     * bottom of the 64-row plane, where this fixture has no blocks (the back colour shows). */
    CHECK(scrl_map_set_plane_parallax(&map, 0, SCRL_PARALLAX_ONE, 3) == 0);
    CHECK(scrl_map_draw(&map, pal, strip, 288, 32, 0) == 0);
    CHECK(strip[(3u * 288u + 3u) * 4u] == dot(0, 0u * 8u + 3u));
    CHECK(strip[(10u * 288u + 3u) * 4u] == dot(0, 7u * 8u + 3u)); /* plane row 7 */
    CHECK(strip[(0u * 288u + 3u) * 4u] == pal[1] && strip[(0u * 288u + 3u) * 4u + 1u] == pal[2]);
    /* A negative base_y shifts it up: image row 0 shows plane row 5. */
    CHECK(scrl_map_set_plane_parallax(&map, 0, SCRL_PARALLAX_ONE, -5) == 0);
    CHECK(scrl_map_draw(&map, pal, strip, 288, 32, 0) == 0);
    CHECK(strip[(0u * 288u + 3u) * 4u] == dot(0, 5u * 8u + 3u));
    CHECK(strip[(26u * 288u + 3u) * 4u] == dot(0, 7u * 8u + 3u)); /* plane row 31, last of the block row */
    CHECK(strip[(27u * 288u + 3u) * 4u] == pal[1]); /* plane row 32: no block there */

    CHECK(scrl_map_set_plane_parallax(&map, 1, 0, 0) == -1); /* only plane A exists */
    CHECK(scrl_map_set_plane_parallax(NULL, 0, 0, 0) == -1);
    scrl_map_free(&map);

    /* Planes B..D default to 1/2, 1/3 and 1/4 of the camera (a third rounded up). */
    {
        uint8_t cdb2[SCRL_CDB_HEADER_BYTES + 64];
        const size_t sz2 = make_cdb_sized(cdb2, 1, 288, 64);
        CHECK(scrl_map_init(&map, cdb2, sz2, til, til_size, cdb2, sz2, til, til_size, cdb2, sz2, til,
                            til_size, cdb2, sz2, til, til_size, NULL, 0) == 0);
        CHECK(map.plane_count == 4u);
        CHECK(map.planes[1].parallax_coeff == 0x8000);
        CHECK(map.planes[2].parallax_coeff == 0x5556);
        CHECK(map.planes[3].parallax_coeff == 0x4000);
        for (i = 1; i < 4; i++) CHECK(scrl_parallax_offset(600, map.planes[i].parallax_coeff) == (int64_t)(600u / (i + 1u)));
        scrl_map_free(&map);
    }

    /* Back screen: CRAM 0 by default, then a colour, then a gradient, then back to CRAM 0. */
    {
        static uint8_t small[8u * 6u * 4u];
        uint8_t grad[3][3] = {{10, 20, 30}, {40, 50, 60}, {70, 80, 90}};
        unsigned y, x;
        memset(&map, 0, sizeof map);
        CHECK(scrl_map_draw(&map, pal, small, 8, 6, 0) == 0);
        for (y = 0; y < 6; y++)
            for (x = 0; x < 8; x++) {
                const uint8_t *p = small + ((size_t)y * 8u + x) * 4u;
                CHECK(p[0] == pal[1] && p[1] == pal[2] && p[2] == pal[3] && p[3] == 255u);
            }
        CHECK(scrl_map_set_back_color(&map, 1, 2, 3) == 0);
        CHECK(scrl_map_draw(&map, pal, small, 8, 6, 0) == 0);
        CHECK(small[0] == 1 && small[1] == 2 && small[2] == 3);
        CHECK(small[(5u * 8u + 7u) * 4u + 2u] == 3);
        CHECK(scrl_map_set_back_gradient(&map, grad, 3) == 0);
        CHECK(scrl_map_draw(&map, pal, small, 8, 6, 0) == 0);
        for (y = 0; y < 6; y++) {
            const unsigned line = y < 3u ? y : 2u; /* the last line holds for the rest */
            for (x = 0; x < 8; x++) {
                const uint8_t *p = small + ((size_t)y * 8u + x) * 4u;
                CHECK(p[0] == grad[line][0] && p[1] == grad[line][1] && p[2] == grad[line][2]);
            }
        }
        CHECK(scrl_map_set_back_gradient(&map, grad, 0) == -1);
        CHECK(scrl_map_set_back_gradient(&map, NULL, 3) == -1);
        CHECK(scrl_map_set_back_gradient(&map, grad, SCRL_BACK_LINES_MAX + 1u) == -1);
        scrl_map_clear_back(&map);
        CHECK(scrl_map_draw(&map, pal, small, 8, 6, 0) == 0);
        CHECK(small[0] == pal[1] && small[1] == pal[2] && small[2] == pal[3]);
    }
    return 0;
}

/* Disc-less: an estimated floor is walkable, an authored one is left alone. */
static int test_floor_slope(void) {
    static uint8_t til[640u * 256u];
    const uint8_t acdb[10] = {0x02, 0x80, 0x01, 0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x00};
    const uint8_t amap[8] = {0x00, 0x64, 0x00, 0x64, 0x00, 0x65, 0x00, 0xC8}; /* (100,100) (101,200) */
    scrl_map map;
    unsigned x, lo = 255, hi = 0, seed = 12345u;
    for (x = 0; x < sizeof til; x++) {
        seed = seed * 1103515245u + 12345u;
        til[x] = (uint8_t)(seed >> 16); /* density noise: neighbouring columns differ wildly */
    }
    CHECK(scrl_map_init(&map, acdb, sizeof acdb, til, sizeof til, NULL, 0, NULL, 0, NULL, 0, NULL,
                        0, NULL, 0, NULL, 0, NULL, 0) == 0);
    CHECK(map.has_amap == 0);
    scrl_map_build_collision(&map, 640);
    CHECK(map.strip_w == 640);
    for (x = 0; x < map.strip_w; x++) {
        if (map.floor_py[x] < lo) lo = map.floor_py[x];
        if (map.floor_py[x] > hi) hi = map.floor_py[x];
        if (x > 0) {
            int d = (int)map.floor_py[x] - (int)map.floor_py[x - 1];
            CHECK(d >= -SCRL_FLOOR_MAX_SLOPE && d <= SCRL_FLOOR_MAX_SLOPE);
        }
    }
    CHECK(lo >= 140u && hi <= 210u); /* still inside the estimator's range */
    CHECK(hi > lo); /* and not flattened to nothing */
    scrl_map_free(&map);

    CHECK(scrl_map_init(&map, acdb, sizeof acdb, til, sizeof til, NULL, 0, NULL, 0, NULL, 0, NULL,
                        0, NULL, 0, NULL, 0, amap, sizeof amap) == 0);
    CHECK(map.has_amap == 0); /* invalid sector chain */
    scrl_map_build_collision(&map, 640);
    CHECK(abs((int)map.floor_py[101] - map.floor_py[100]) <= SCRL_FLOOR_MAX_SLOPE);
    scrl_map_free(&map);
    return 0;
}

static void fill_test_pal(uint8_t *pal) {
    unsigned i;
    memset(pal, 0, 4096);
    for (i = 0; i < 1024; i++) {
        pal[4 * i + 1] = (uint8_t)(i & 0xffu);
        pal[4 * i + 2] = (uint8_t)(255u - (i & 0xffu));
        pal[4 * i + 3] = (uint8_t)((i & 0xffu) / 2u);
    }
}

/*
 * The vertical camera moves only the world plane (Plane A). Planes A and B are both 288x64 (taller
 * than the 32-row strip). Block row 0 and block row 1 of each carry different characters, so a
 * vertical shift of 32 rows is visible: A at x 0..31, B at x 32..63.
 */
static int test_vcam_world_plane_only(void) {
    static uint8_t strip[288u * 32u * 4u];
    uint8_t acdb[SCRL_CDB_HEADER_BYTES + 64], bcdb[SCRL_CDB_HEADER_BYTES + 64];
    uint8_t atil[1500], btil[1500], pal[4096];
    const size_t acdb_sz = make_cdb_sized(acdb, 1, 288, 64);
    const size_t bcdb_sz = make_cdb_sized(bcdb, 1, 288, 64);
    const unsigned t5[1] = {5}, t6[1] = {6}, t7[1] = {7}, t8[1] = {8};
    uint32_t d[16];
    size_t asz = 0, bsz = 0;
    scrl_map map;

    all_cells(d, OWN(0));
    asz = add_record(atil, asz, 0, 1, t5, d);  /* A row 0, x 0..31 */
    asz = add_record(atil, asz, 9, 1, t6, d);  /* A row 1 (y 32..63), x 0..31 */
    bsz = add_record(btil, bsz, 1, 1, t7, d);  /* B row 0, x 32..63 */
    bsz = add_record(btil, bsz, 10, 1, t8, d); /* B row 1, x 32..63 */
    fill_test_pal(pal);
    CHECK(scrl_map_init(&map, acdb, acdb_sz, atil, asz, bcdb, bcdb_sz, btil, bsz, NULL, 0, NULL, 0,
                        NULL, 0, NULL, 0, NULL, 0) == 0);
    CHECK(map.plane_count == 2u && scrl_map_cached_planes(&map) == 2u);
    CHECK(map.planes[0].blend_mode == 0u && map.planes[1].blend_mode == 0u);

    CHECK(scrl_map_draw(&map, pal, strip, 288, 32, 0) == 0);
    CHECK(strip_px_is(strip, 288, 3, 2, dot(5, 2u * 8u + 3u)));
    CHECK(strip_px_is(strip, 288, 40, 2, dot(7, 2u * 8u + 0u)));

    map.camera_y = 32;
    CHECK(scrl_map_draw(&map, pal, strip, 288, 32, 0) == 0);
    CHECK(strip_px_is(strip, 288, 3, 2, dot(6, 2u * 8u + 3u))); /* A: displaced to block row 1 */
    CHECK(strip_px_is(strip, 288, 40, 2, dot(7, 2u * 8u + 0u))); /* B: still row 0, not displaced */
    scrl_map_free(&map);
    return 0;
}

/* blend_mode 1 feathers the edges of the populated column range; 0 stays opaque. */
static int test_blend_feather(void) {
    static uint8_t s0[288u * 32u * 4u], s1[288u * 32u * 4u];
    uint8_t cdb[SCRL_CDB_HEADER_BYTES + 64], til[2000], pal[4096];
    const size_t cdb_sz = make_cdb_sized(cdb, 1, 288, 64);
    const unsigned t5[1] = {5};
    uint32_t d[16];
    size_t tsz = 0;
    unsigned id, x, y;
    scrl_map map;

    all_cells(d, OWN(0));
    for (id = 2; id <= 5; id++) tsz = add_record(til, tsz, id, 1, t5, d); /* columns 64..191 */
    fill_test_pal(pal);
    CHECK(scrl_map_init(&map, cdb, cdb_sz, til, tsz, NULL, 0, NULL, 0, NULL, 0, NULL, 0, NULL, 0,
                        NULL, 0, NULL, 0) == 0);
    CHECK(scrl_map_draw(&map, pal, s0, 288, 32, 0) == 0);
    map.planes[0].blend_mode = 1;
    CHECK(scrl_map_draw(&map, pal, s1, 288, 32, 0) == 0);

    /* Opaque mode: hard edges, byte for byte what the plane always drew. */
    CHECK(strip_px_is(s0, 288, 64, 2, dot(5, 2u * 8u + 0u)));
    CHECK(strip_px_is(s0, 288, 191, 2, dot(5, 2u * 8u + 7u)));
    CHECK(strip_px_is(s0, 288, 63, 2, 0));
    CHECK(strip_px_is(s0, 288, 192, 2, 0));

    /* Feather: edge dots are between the back colour (0,255,0) and the dot, interior exact. */
    {
        const unsigned xs[2] = {64, 191};
        unsigned k;
        for (k = 0; k < 2; k++) {
            const uint8_t *e = s1 + ((size_t)2 * 288u + xs[k]) * 4u;
            const uint8_t *o = s0 + ((size_t)2 * 288u + xs[k]) * 4u;
            CHECK(e[3] == 255u);
            CHECK(e[0] > 0u && e[0] < o[0]); /* red: back 0, dot o[0] */
            CHECK(e[1] < 255u && e[1] > o[1]); /* green: back 255, dot o[1] */
        }
    }
    CHECK(strip_px_is(s1, 288, 63, 2, 0));
    CHECK(strip_px_is(s1, 288, 192, 2, 0));
    CHECK(strip_px_is(s1, 288, 64 + SCRL_FEATHER_PX + 4u, 2, dot(5, 2u * 8u + ((64u + SCRL_FEATHER_PX + 4u) % 8u))));
    CHECK(strip_px_is(s1, 288, 191 - SCRL_FEATHER_PX - 4u, 2, dot(5, 2u * 8u + ((191u - SCRL_FEATHER_PX - 4u) % 8u))));
    /* Everything outside the two feather bands is identical in both modes. */
    for (y = 0; y < 32; y++)
        for (x = 0; x < 288; x++) {
            const int in_band = (x >= 64u && x < 64u + SCRL_FEATHER_PX) ||
                                (x <= 191u && x + SCRL_FEATHER_PX > 191u);
            if (!in_band)
                CHECK(memcmp(s0 + ((size_t)y * 288u + x) * 4u, s1 + ((size_t)y * 288u + x) * 4u, 4) == 0);
        }
    scrl_map_free(&map);
    return 0;
}

/* blend_mode 2 crossfades col 0 and pw-1; blend_mode 3 fills sy>=128 gaps from rows 64..127. */
static int test_blend_wrap_and_lower_fill(void) {
    static uint8_t s0[288u * 160u * 4u], s2[288u * 32u * 4u], s3[288u * 160u * 4u];
    uint8_t cdb2[SCRL_CDB_HEADER_BYTES + 64], cdb3[SCRL_CDB_HEADER_BYTES + 64];
    uint8_t til2[2000], til3[1500], pal[4096];
    const size_t cdb2_sz = make_cdb_sized(cdb2, 1, 288, 64);
    const size_t cdb3_sz = make_cdb_sized(cdb3, 1, 288, 160);
    const unsigned t5[1] = {5}, t6[1] = {6};
    uint32_t d[16];
    size_t t2sz = 0, t3sz = 0;
    scrl_map map;

    all_cells(d, OWN(0));
    t2sz = add_record(til2, t2sz, 0, 1, t5, d); /* col 0..31 */
    t2sz = add_record(til2, t2sz, 8, 1, t6, d); /* col 256..287 */
    fill_test_pal(pal);
    CHECK(scrl_map_init(&map, cdb2, cdb2_sz, til2, t2sz, NULL, 0, NULL, 0, NULL, 0, NULL, 0,
                        NULL, 0, NULL, 0, NULL, 0) == 0);
    CHECK(scrl_map_draw(&map, pal, s0, 288, 32, 0) == 0);
    map.planes[0].blend_mode = 2;
    CHECK(scrl_map_draw(&map, pal, s2, 288, 32, 0) == 0);
    CHECK(memcmp(s2 + ((size_t)2 * 288u + 0u) * 4u, s2 + ((size_t)2 * 288u + 287u) * 4u, 4) == 0);
    CHECK(memcmp(s2 + ((size_t)2 * 288u + 0u) * 4u, s0 + ((size_t)2 * 288u + 0u) * 4u, 4) != 0);
    scrl_map_free(&map);

    t3sz = add_record(til3, t3sz, 18, 1, t5, d); /* block row 2 (y 64..95), col 0..31; row 4 empty */
    CHECK(scrl_map_init(&map, cdb3, cdb3_sz, til3, t3sz, NULL, 0, NULL, 0, NULL, 0, NULL, 0,
                        NULL, 0, NULL, 0, NULL, 0) == 0);
    CHECK(scrl_map_draw(&map, pal, s0, 288, 160, 0) == 0);
    CHECK(strip_px_is(s0, 288, 3, 130, 0)); /* empty before blend_mode 3 */
    map.planes[0].blend_mode = 3;
    CHECK(scrl_map_draw(&map, pal, s3, 288, 160, 0) == 0);
    CHECK(strip_px_is(s3, 288, 3, 130, dot(5, (66u % 8u) * 8u + 3u)));
    scrl_map_free(&map);
    return 0;
}

int main(void) {
    const char *d1 = getenv("MB_DISC1");
    const char *man = getenv("MB_MANIFEST");
    char err[512];
    cdimage cd;
    id_manifest manifest;
    id_result result;
    void *acdb = NULL, *atil = NULL, *bcdb = NULL, *btil = NULL;
    void *ccdb = NULL, *ctil = NULL, *dcdb = NULL, *dtil = NULL, *amap = NULL;
    size_t acdb_sz = 0, atil_sz = 0, bcdb_sz = 0, btil_sz = 0;
    size_t ccdb_sz = 0, ctil_sz = 0, dcdb_sz = 0, dtil_sz = 0, amap_sz = 0;
    scrl_map map;
    scrl_plane plane;
    uint8_t pal[4096];
    uint8_t *strip;
    unsigned i;

    if (test_floor_slope()) return 1;
    printf("OK scrl_map floor slope (synthetic)\n");
    if (test_cache_drawn()) return 1;
    printf("OK scrl_map tile cache drawn (synthetic)\n");
    if (test_palette_bank_draw()) return 1;
    printf("OK scrl_map palette bank draw (synthetic)\n");
    if (test_parallax_and_back()) return 1;
    printf("OK scrl_map parallax, base_y and back screen (synthetic)\n");
    if (test_vcam_world_plane_only()) return 1;
    printf("OK scrl_map vcam applies to Plane A only (synthetic)\n");
    if (test_blend_feather()) return 1;
    printf("OK scrl_map blend_mode feather (synthetic)\n");
    if (test_blend_wrap_and_lower_fill()) return 1;
    printf("OK scrl_map blend_mode wrap and lower fill (synthetic)\n");
    {
        /* ICE1-sized tall CDB (1864×8192, 59×256 = 15104 blocks <= SCRL_TILE_BLOCKS_MAX)
         * parses as A-style and accepts block id=30; 16384×8192 (131072 blocks) falls out. */
        uint8_t cdb_tall[SCRL_CDB_HEADER_BYTES + 64];
        uint8_t til30[512];
        uint32_t cells[16];
        const unsigned tag[1] = {3};
        const size_t tall_sz = make_cdb_sized(cdb_tall, 1, 1864, 8192);
        const size_t tsz = add_record(til30, 0, 30, 1, tag, (all_cells(cells, OWN(0)), cells));
        scrl_plane p_tall;
        scrl_map m_tall;
        CHECK(scrl_cdb_parse(cdb_tall, tall_sz, &p_tall) == 0);
        CHECK(p_tall.hdr_style == 0 && p_tall.pixel_w == 1864u && p_tall.pixel_h == 8192u);
        CHECK(scrl_map_init(&m_tall, cdb_tall, tall_sz, til30, tsz, NULL, 0, NULL, 0, NULL, 0,
                            NULL, 0, NULL, 0, NULL, 0, NULL, 0) == 0);
        CHECK(m_tall.planes[0].tiles.status == SCRL_TC_OK);
        scrl_map_free(&m_tall);

        cdb_tall[0] = 0x40; cdb_tall[1] = 0x00; /* 16384×8192 → 131072 blocks > 65536 */
        CHECK(scrl_cdb_parse(cdb_tall, tall_sz, &p_tall) == 0);
        CHECK(p_tall.hdr_style == 1);
    }
    if (!d1 || !man) {
        printf("SKIP scrl_map\n");
        return 0;
    }
    CHECK(manifest_load(&manifest, man, err, sizeof err) == 0);
    CHECK(cd_open(&cd, d1, err, sizeof err) == 0);
    CHECK(identity_check(&manifest, &cd, 1, ID_QUICK, &result));
    manifest_free(&manifest);

    /* MAUSO ACDB A-style dims 1176×608. */
    CHECK(resource_load(&cd, "/05MAUSO/07ACDB", &acdb, &acdb_sz) == 0);
    CHECK(scrl_cdb_parse(acdb, acdb_sz, &plane) == 0);
    CHECK(plane.hdr_style == 0);
    CHECK(plane.pixel_w == 1176u);
    CHECK(plane.pixel_h == 608u);
    printf("MAUSO ACDB: %ux%u style=%u\n", plane.pixel_w, plane.pixel_h, plane.hdr_style);
    free(acdb);
    acdb = NULL;

    CHECK(resource_load(&cd, "/05MAUSO/07ACDB", &acdb, &acdb_sz) == 0);
    CHECK(resource_load(&cd, "/05MAUSO/08ATIL", &atil, &atil_sz) == 0);
    (void)resource_load(&cd, "/05MAUSO/09BCDB", &bcdb, &bcdb_sz);
    (void)resource_load(&cd, "/05MAUSO/10BTIL", &btil, &btil_sz);
    (void)resource_load(&cd, "/05MAUSO/11CCDB", &ccdb, &ccdb_sz);
    (void)resource_load(&cd, "/05MAUSO/12CTIL", &ctil, &ctil_sz);
    (void)resource_load(&cd, "/05MAUSO/13DCDB", &dcdb, &dcdb_sz);
    (void)resource_load(&cd, "/05MAUSO/14DTIL", &dtil, &dtil_sz);

    CHECK(scrl_map_init(&map, acdb, acdb_sz, atil, atil_sz, bcdb, bcdb_sz, btil,
                        btil_sz, ccdb, ccdb_sz, ctil, ctil_sz, dcdb, dcdb_sz, dtil,
                        dtil_sz, NULL, 0) == 0);
    CHECK(map.plane_count >= 1);
    print_cache("MAUSO", &map);
    scrl_map_build_collision(&map, 640);
    CHECK(map.strip_w == 640);
    CHECK(scrl_map_find_floor(&map, 40) > 0);
    CHECK(scrl_map_get_hixel(&map, 40, 220) == SCRL_HIXEL_FLOOR);
    CHECK(scrl_map_get_hixel(&map, 40, 20) == SCRL_HIXEL_AIR);

    memset(pal, 0, sizeof pal);
    for (i = 0; i < 256; i++) {
        pal[i * 4 + 1] = (uint8_t)i;
        pal[i * 4 + 2] = (uint8_t)(i / 2);
        pal[i * 4 + 3] = (uint8_t)(i / 3);
        pal[i * 4 + 0] = 0;
    }
    strip = malloc(640u * 224u * 4u);
    CHECK(strip != NULL);
    CHECK(scrl_map_draw(&map, pal, strip, 640, 224, 0) == 0);
    free(strip);
    printf("MAUSO planes=%u floor40=%d\n", map.plane_count, scrl_map_find_floor(&map, 40));

    scrl_map_free(&map);
    free(acdb); free(atil); free(bcdb); free(btil);
    free(ccdb); free(ctil); free(dcdb); free(dtil);

    /* Full VALLEY sector index and independently decoded authored hixels. */
    CHECK(resource_load(&cd, "/06VALLEY/07ACDB", &acdb, &acdb_sz) == 0);
    CHECK(resource_load(&cd, "/06VALLEY/08ATIL", &atil, &atil_sz) == 0);
    CHECK(resource_load(&cd, "/06VALLEY/06AMAP", &amap, &amap_sz) == 0);
    CHECK(amap_sz > 8);
    CHECK(scrl_map_init(&map, acdb, acdb_sz, atil, atil_sz, NULL, 0, NULL, 0, NULL, 0,
                        NULL, 0, NULL, 0, NULL, 0, amap, amap_sz) == 0);
    CHECK(map.has_amap == 1);
    CHECK(map.amap_n == 494);
    CHECK(scrl_tilecache_hixel(&map.planes[0].tiles, 80, 733) == 0);
    CHECK(scrl_tilecache_hixel(&map.planes[0].tiles, 80, 734) > 0);
    scrl_map_build_collision(&map, 640);
    CHECK(scrl_map_find_floor(&map, 80) > 0);
    printf("VALLEY amap_n=%u floor80=%d\n", map.amap_n, scrl_map_find_floor(&map, 80));
    print_cache("VALLEY", &map);

    scrl_map_free(&map);
    free(acdb); free(atil); free(amap);
    cd_close(&cd);

    /* Disc 2 ICE1 07ACDB (1864×8192) + 08ATIL (rec=30): A-style ceiling >= 8192. */
    {
        const char *d2 = getenv("MB_DISC2");
        if (d2) {
            cdimage cd2;
            void *icdb = NULL, *itil = NULL;
            size_t icdb_sz = 0, itil_sz = 0;
            CHECK(manifest_load(&manifest, man, err, sizeof err) == 0);
            CHECK(cd_open(&cd2, d2, err, sizeof err) == 0);
            CHECK(identity_check(&manifest, &cd2, 2, ID_QUICK, &result));
            manifest_free(&manifest);
            CHECK(resource_load(&cd2, "/20ICE1/07ACDB", &icdb, &icdb_sz) == 0);
            CHECK(resource_load(&cd2, "/20ICE1/08ATIL", &itil, &itil_sz) == 0);
            CHECK(scrl_map_init(&map, icdb, icdb_sz, itil, itil_sz, NULL, 0, NULL, 0, NULL, 0,
                                NULL, 0, NULL, 0, NULL, 0, NULL, 0) == 0);
            CHECK(map.plane_count == 1u);
            CHECK(map.planes[0].hdr_style == 0u);
            CHECK(map.planes[0].pixel_w == 1864u && map.planes[0].pixel_h == 8192u);
            CHECK(map.planes[0].tiles.status == SCRL_TC_OK);
            CHECK(map.planes[0].tiles.bad_cells == 0u);
            CHECK(map.planes[0].tiles.lz_failed_blocks == 0u);
            print_cache("ICE1", &map);
            scrl_map_free(&map);
            free(icdb);
            free(itil);
            cd_close(&cd2);
        }
    }

    /* _DecompByte@0x002a0b4c synthetic test: 4 literals + nibble=2 (copy 3 from dist=4) + nibble=0 (EOF). */
    {
        const uint8_t stream[] = {
            0x0C,             /* bits: 0,0,0,0 (4 literals), 1 (match), 1 (EOF), 0, 0 */
            'B', 'O', 'N', 'E',
            0x00, 0x42,       /* dist = 4 (0x004), nibble = 2 (copy 3 bytes: "BON") */
            0x00, 0x00        /* nibble = 0 -> EOF */
        };
        uint8_t out[16] = {0};
        size_t n = scrl_decomp_byte(stream, sizeof stream, out, sizeof out);
        CHECK(n == 7u);
        CHECK(memcmp(out, "BONEBON", 7) == 0);
    }

    /* mb_font 8x8 glyph + text rendering test. */
    {
        uint8_t fb[64 * 16 * 4];
        memset(fb, 0, sizeof fb);
        CHECK(mb_font_text_width("BONES", 1) == 40u);
        mb_font_draw_text_shadow(fb, 64, 16, 4, 4, "BONES", 255, 220, 80, 10, 10, 10, 1);
        /* Verify non-zero rendered glyph pixels inside bounding box. */
        {
            unsigned k, lit = 0;
            for (k = 0; k < sizeof fb; k += 4)
                if (fb[k + 3] == 255u) lit++;
            CHECK(lit >= 20u);
        }
    }

    printf("OK scrl_map\n");
    return 0;
}
