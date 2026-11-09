#include "core/cdimage.h"
#include "core/identity.h"
#include "core/mb_font.h"
#include "core/resource.h"
#include "core/scrl_map.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

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
