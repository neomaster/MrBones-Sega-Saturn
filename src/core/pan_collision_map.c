#include "pan_collision_map.h"
#include <string.h>

/* Called per pandcomp record from pan_scroll_seek (§22 live 15PAN collision). */
int pan_collision_map_load(pan_collision_map *map, const uint8_t *cells, size_t size) {
    if (!map || !cells || size != PAN_COLL_SIZE) return -1;
    memcpy(map->cells, cells, PAN_COLL_SIZE);
    map->size = size;
    return 0;
}

int pan_collision_get_hixel(const pan_collision_map *map, int grid_x, int grid_y) {
    int x, y;
    size_t idx;
    uint8_t b;
    if (!map || map->size != PAN_COLL_SIZE) return -1;
    x = grid_x + PAN_COLL_X_BIAS;
    y = grid_y + PAN_COLL_Y_BIAS;
    if (x < 0 || y < 0 || x > PAN_COLL_X_MAX || y > PAN_COLL_Y_MAX)
        return -1;
    idx = (size_t)(y >> 1) * (size_t)PAN_COLL_TILES_X + (size_t)(x >> 3);
    if (idx >= PAN_COLL_SIZE) return -1;
    b = map->cells[idx];
    /* BitExtract_213FA0(byte, x&6) & 3 — for shifts 0,2,4,6 ≡ byte>>(x&6). */
    return (int)((b >> (x & 6)) & 3u);
}

int pan_collision_hixel_pixel(const pan_collision_map *map, int pixel_x, int pixel_y) {
    return pan_collision_get_hixel(map, pixel_x - PAN_COLL_X_BIAS,
                                   pixel_y - PAN_COLL_Y_BIAS);
}

int pan_collision_map_solid_tile(const pan_collision_map *map, int tx, int ty) {
    int px0, py0, dx, dy;
    if (!map || map->size != PAN_COLL_SIZE) return 0;
    if (tx < 0 || ty < 0 ||
        (unsigned)tx >= PAN_COLL_TILES_X || (unsigned)ty >= PAN_COLL_TILES_Y)
        return 1;
    px0 = tx * (int)PAN_COLL_PIXELS_PER_TILE;
    py0 = ty * (int)PAN_COLL_PIXELS_PER_TILE;
    /* Sample 2×2 pixel grid (hixel resolution) across the 8×8 tile. */
    for (dy = 0; dy < (int)PAN_COLL_PIXELS_PER_TILE; dy += 2) {
        for (dx = 0; dx < (int)PAN_COLL_PIXELS_PER_TILE; dx += 2) {
            if (pan_collision_hixel_pixel(map, px0 + dx, py0 + dy) == PAN_COLL_HIXEL_SOLID)
                return 1;
        }
    }
    return 0;
}

int pan_collision_map_solid_pixel(const pan_collision_map *map, int pixel_x, int pixel_y) {
    return pan_collision_hixel_pixel(map, pixel_x, pixel_y) == PAN_COLL_HIXEL_SOLID;
}

int pan_collision_map_ground_ty(const pan_collision_map *map, int tx, int start_ty) {
    int ty;
    if (!map || map->size != PAN_COLL_SIZE) return -1;
    if (tx < 0 || tx >= (int)PAN_COLL_TILES_X) return -1;
    if (start_ty < 0) start_ty = 0;
    for (ty = start_ty; ty < (int)PAN_COLL_TILES_Y; ty++) {
        if (pan_collision_map_solid_tile(map, tx, ty)) return ty;
    }
    return -1;
}

void pan_hit_offsets_default(pan_hit_offsets *o) {
    if (!o) return;
    /* BonesSetConfig ← bnscfg template 0x2873EC (+0x50..0x6c). */
    o->top_right_x = 5;
    o->top_right_y = -8;
    o->top_left_x = -5;
    o->top_left_y = -8;
    o->bot_right_x = 5;
    o->bot_right_y = 12;
    o->bot_left_x = -5;
    o->bot_left_y = 12;
}

int pan_hit_offsets_from_config(pan_hit_offsets *o, const void *config, size_t config_size) {
    const uint8_t *p;
    if (!o || !config || config_size < 0x70) return -1;
    p = (const uint8_t *)config;
    /* BE32 fields at the BonesSetConfig source offsets. */
#define BE32_AT(off) ((int32_t)(((uint32_t)p[(off)] << 24) | ((uint32_t)p[(off)+1] << 16) | \
                                ((uint32_t)p[(off)+2] << 8) | (uint32_t)p[(off)+3]))
    o->top_right_x = BE32_AT(0x50);
    o->top_right_y = BE32_AT(0x54);
    o->top_left_x = BE32_AT(0x58);
    o->top_left_y = BE32_AT(0x5c);
    o->bot_right_x = BE32_AT(0x60);
    o->bot_right_y = BE32_AT(0x64);
    o->bot_left_x = BE32_AT(0x68);
    o->bot_left_y = BE32_AT(0x6c);
#undef BE32_AT
    return 0;
}

/* Solid with air 2px above → floor surface; do not X-push (avoids sliding on ground). */
static int corner_is_wall(const pan_collision_map *map, int gx, int gy) {
    if (pan_collision_get_hixel(map, gx, gy) != PAN_COLL_HIXEL_SOLID) return 0;
    if (pan_collision_get_hixel(map, gx, gy - 2) != PAN_COLL_HIXEL_SOLID) return 0;
    return 1;
}

int pan_bones_push_out_of_walls(const pan_collision_map *map, int *grid_x, int *grid_y,
                                const pan_hit_offsets *o) {
    int hit = 0;
    int guard;
    if (!map || !grid_x || !grid_y || !o) return 0;

    /* Right: BotRight then TopRight → decrement X while wall (not floor). */
    for (guard = 0; guard < 64; guard++) {
        if (!corner_is_wall(map, *grid_x + o->bot_right_x, *grid_y + o->bot_right_y))
            break;
        (*grid_x)--;
        hit = 1;
    }
    for (guard = 0; guard < 64; guard++) {
        if (!corner_is_wall(map, *grid_x + o->top_right_x, *grid_y + o->top_right_y))
            break;
        (*grid_x)--;
        hit = 1;
    }
    /* Left: BotLeft then TopLeft → increment X. */
    for (guard = 0; guard < 64; guard++) {
        if (!corner_is_wall(map, *grid_x + o->bot_left_x, *grid_y + o->bot_left_y))
            break;
        (*grid_x)++;
        hit = 1;
    }
    for (guard = 0; guard < 64; guard++) {
        if (!corner_is_wall(map, *grid_x + o->top_left_x, *grid_y + o->top_left_y))
            break;
        (*grid_x)++;
        hit = 1;
    }
    return hit;
}

int pan_bones_wall_above(const pan_collision_map *map, int grid_x, int grid_y,
                         const pan_hit_offsets *o) {
    int top_r, bot_r, top_l, bot_l;
    if (!map || !o) return 0;
    top_r = pan_collision_get_hixel(map, grid_x + o->top_right_x, grid_y + o->top_right_y);
    bot_r = pan_collision_get_hixel(map, grid_x + o->bot_right_x, grid_y + o->bot_right_y);
    top_l = pan_collision_get_hixel(map, grid_x + o->top_left_x, grid_y + o->top_left_y);
    bot_l = pan_collision_get_hixel(map, grid_x + o->bot_left_x, grid_y + o->bot_left_y);
    /*
     * Saturn returns 1 when the early-exit path is not taken: roughly when a
     * top corner is solid while the matching bottom is not both-solid. Native
     * uses: top solid and bottom free on either side.
     */
    if (top_r == PAN_COLL_HIXEL_SOLID && bot_r != PAN_COLL_HIXEL_SOLID) return 1;
    if (top_l == PAN_COLL_HIXEL_SOLID && bot_l != PAN_COLL_HIXEL_SOLID) return 1;
    return 0;
}
