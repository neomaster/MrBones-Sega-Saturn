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

/* The strip is sky (18, 36, 72) where nothing is drawn; a dot blends 3:1 over it. */
static int strip_px_is(const uint8_t *strip, unsigned w, unsigned x, unsigned y, unsigned dot_value) {
    const uint8_t *p = strip + ((size_t)y * w + x) * 4u;
    const unsigned r = dot_value ? (dot_value * 3u + 18u) / 4u : 18u;
    const unsigned g = dot_value ? ((255u - dot_value) * 3u + 36u) / 4u : 36u;
    const unsigned b = dot_value ? ((dot_value / 2u) * 3u + 72u) / 4u : 72u;
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
    for (i = 0; i < 256; i++) {
        pal[4 * i + 1] = (uint8_t)i;
        pal[4 * i + 2] = (uint8_t)(255u - i);
        pal[4 * i + 3] = (uint8_t)(i / 2u);
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
    scrl_map_free(&map);
    return 0;
}

/* What the cache made of a real plane, as counts only. */
static void print_cache(const char *stage, const scrl_map *m) {
    unsigned i;
    for (i = 0; i < m->plane_count; i++) {
        const scrl_tile_cache *c = &m->planes[i].tiles;
        printf("%s %s: plane=%ux%u cache=%s grid=%ux%u present=%u compressed=%u empty=%u bad_cells=%u "
               "ignored=0x%08X\n",
               stage, m->planes[i].cdb_name, m->planes[i].pixel_w, m->planes[i].pixel_h,
               scrl_tilecache_status_name(c->status), c->cols, c->rows, c->present_blocks,
               c->compressed_blocks, c->empty_blocks, c->bad_cells, (unsigned)c->ignored_bits);
        if (c->status != SCRL_TC_OK || c->lz_failed_blocks)
            printf("%s %s: lz_failed_blocks=%u first problem at record %u (count=%u size=%u) lz got=%u "
                   "want=%u bad_matches=%u terminated=%u til_size=%u\n",
                   stage, m->planes[i].cdb_name, c->lz_failed_blocks, c->fail.record, c->fail.count,
                   c->fail.size, (unsigned)c->fail.lz_got, (unsigned)c->fail.lz_want,
                   c->fail.lz_bad_matches, c->fail.lz_terminated, (unsigned)m->planes[i].til_size);
    }
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
    CHECK(map.has_amap == 1);
    scrl_map_build_collision(&map, 640);
    CHECK(map.floor_py[100] == 100 && map.floor_py[101] == 200); /* authored cliff intact */
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

    /* VALLEY AMAP floor polyline. */
    CHECK(resource_load(&cd, "/06VALLEY/07ACDB", &acdb, &acdb_sz) == 0);
    CHECK(resource_load(&cd, "/06VALLEY/08ATIL", &atil, &atil_sz) == 0);
    CHECK(resource_load(&cd, "/06VALLEY/06AMAP", &amap, &amap_sz) == 0);
    CHECK(amap_sz > 8);
    CHECK(scrl_map_init(&map, acdb, acdb_sz, atil, atil_sz, NULL, 0, NULL, 0, NULL, 0,
                        NULL, 0, NULL, 0, NULL, 0, amap, amap_sz) == 0);
    CHECK(map.has_amap == 1);
    CHECK(map.amap_n > 10);
    scrl_map_build_collision(&map, 640);
    CHECK(scrl_map_find_floor(&map, 80) > 0);
    printf("VALLEY amap_n=%u floor80=%d\n", map.amap_n, scrl_map_find_floor(&map, 80));
    print_cache("VALLEY", &map);

    scrl_map_free(&map);
    free(acdb); free(atil); free(amap);
    cd_close(&cd);

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
