#include "core/pan_collision_map.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

/* Fill one coarse tile (4 fine rows × 1 column byte) with all-solid hixels. */
static void set_solid(uint8_t *cells, int tx, int ty) {
    int sub;
    for (sub = 0; sub < 4; sub++) {
        int fine_row = ty * 4 + sub;
        cells[(size_t)fine_row * PAN_COLL_TILES_X + (unsigned)tx] = 0xff;
    }
}

int main(void) {
    pan_collision_map map;
    uint8_t cells[PAN_COLL_SIZE];
    pan_hit_offsets hit;
    int gx, gy;

    memset(cells, 0, sizeof cells);
    /* Ground fill from row 17; tall wall slab tx=9..11, ty=12..16 (fits AABB ±5). */
    for (unsigned ty = 17; ty < PAN_COLL_TILES_Y; ty++)
        for (unsigned tx = 0; tx < PAN_COLL_TILES_X; tx++)
            set_solid(cells, (int)tx, (int)ty);
    for (unsigned ty = 12; ty <= 16; ty++)
        for (unsigned tx = 9; tx <= 11; tx++)
            set_solid(cells, (int)tx, (int)ty);

    CHECK(pan_collision_map_load(&map, cells, PAN_COLL_SIZE) == 0);
    CHECK(pan_collision_map_solid_tile(&map, 5, 17) != 0);
    CHECK(pan_collision_map_solid_tile(&map, 5, 10) == 0);
    CHECK(pan_collision_map_solid_tile(&map, -1, 0) != 0);
    CHECK(pan_collision_map_solid_tile(&map, 10, 15) != 0);
    CHECK(pan_collision_map_solid_pixel(&map, 40, 136) != 0); /* 40/8=5, 136/8=17 */
    CHECK(pan_collision_map_solid_pixel(&map, 40, 80) == 0);
    CHECK(pan_collision_map_ground_ty(&map, 5, 0) == 17);
    CHECK(pan_collision_map_ground_ty(&map, 10, 0) == 12); /* wall column top */
    CHECK(pan_collision_map_ground_ty(&map, 5, 18) == 18);
    CHECK(pan_collision_floor(&map, 40, 80) == 134);
    CHECK(pan_collision_floor(&map, 40, 150) == 134);
    CHECK(pan_collision_floor(&map, -1, 100) == -1);
    map.cells[54 * 40 + 5] = 1u;
    CHECK(pan_collision_floor(&map, 40, 88) == 106);
    map.cells[54 * 40 + 5] = 2u;
    CHECK(pan_collision_floor(&map, 40, 88) == 134);
    map.cells[54 * 40 + 5] = 0;
    CHECK(pan_collision_map_load(&map, cells, 100) == -1);

    /* §12k GetHixel: pixel (40,136) → grid (40-0xA0, 136-0x70) → hixel 3. */
    CHECK(pan_collision_hixel_pixel(&map, 40, 136) == PAN_COLL_HIXEL_SOLID);
    CHECK(pan_collision_get_hixel(&map, 40 - PAN_COLL_X_BIAS, 136 - PAN_COLL_Y_BIAS)
          == PAN_COLL_HIXEL_SOLID);
    CHECK(pan_collision_hixel_pixel(&map, 40, 80) != PAN_COLL_HIXEL_SOLID);
    CHECK(pan_collision_get_hixel(&map, -999, 0) == -1);

    /* Push-out: wall column with solid above corner (not floor surface). */
    pan_hit_offsets_default(&hit);
    CHECK(hit.top_right_x == 5 && hit.top_right_y == -8);
    CHECK(hit.top_left_x == -5 && hit.bot_right_y == 12);
    CHECK(hit.bot_left_x == -5 && hit.bot_left_y == 12);
    /* BonesSetConfig BE32 fields at config+0x50..0x6c (bnscfg 0x2873EC). */
    {
        uint8_t cfg[0x70];
        pan_hit_offsets from;
        memset(cfg, 0, sizeof cfg);
#define WBE(off, v) do { int32_t _v = (int32_t)(v); \
            cfg[(off)] = (uint8_t)((uint32_t)_v >> 24); \
            cfg[(off)+1] = (uint8_t)((uint32_t)_v >> 16); \
            cfg[(off)+2] = (uint8_t)((uint32_t)_v >> 8); \
            cfg[(off)+3] = (uint8_t)(uint32_t)_v; } while (0)
        WBE(0x50, 5); WBE(0x54, -8); WBE(0x58, -5); WBE(0x5c, -8);
        WBE(0x60, 5); WBE(0x64, 12); WBE(0x68, -5); WBE(0x6c, 12);
#undef WBE
        CHECK(pan_hit_offsets_from_config(&from, cfg, sizeof cfg) == 0);
        CHECK(from.top_right_x == 5 && from.top_right_y == -8);
        CHECK(from.top_left_x == -5 && from.bot_left_y == 12);
        CHECK(pan_hit_offsets_from_config(&from, cfg, 0x40) == -1);
    }
    /* Origin mid wall (ty~14); AABB ±5 / Y -8..+12 stays inside solid stack. */
    gx = 10 * 8 + 4 - PAN_COLL_X_BIAS;
    gy = 14 * 8 + 4 - PAN_COLL_Y_BIAS;
    CHECK(pan_collision_get_hixel(&map, gx + hit.bot_right_x, gy + hit.bot_right_y)
          == PAN_COLL_HIXEL_SOLID);
    CHECK(pan_bones_push_out_of_walls(&map, &gx, &gy, &hit) == 1);
    CHECK(pan_collision_get_hixel(&map, gx + hit.bot_right_x, gy + hit.bot_right_y)
          != PAN_COLL_HIXEL_SOLID);

    /* Wall above: top offset in solid, bottom in air. */
    {
        pan_hit_offsets box = {0, -24, 0, -24, 0, 0, 0, 0};
        /* Feet in air at pixel y=100; ceiling solid at y=76 (ty=9). */
        set_solid(cells, 5, 9);
        CHECK(pan_collision_map_load(&map, cells, PAN_COLL_SIZE) == 0);
        int gxf = 40 - PAN_COLL_X_BIAS;
        int gyf = 100 - PAN_COLL_Y_BIAS;
        CHECK(pan_collision_hixel_pixel(&map, 40, 100) != PAN_COLL_HIXEL_SOLID);
        CHECK(pan_collision_hixel_pixel(&map, 40, 76) == PAN_COLL_HIXEL_SOLID);
        CHECK(pan_bones_wall_above(&map, gxf, gyf, &box) == 1);
    }
    return 0;
}
