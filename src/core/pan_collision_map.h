#ifndef MB_PAN_COLLISION_MAP_H
#define MB_PAN_COLLISION_MAP_H
#include <stddef.h>
#include <stdint.h>

/*
 * RUNWAY pan collision buffer: 4480 bytes = 40 columns × 112 fine rows.
 * _PANCOLLGetHixel @0x00256a68 indexes:
 *   x' = grid_x + 0xA0,  y' = grid_y + 0x70
 *   byte = map[(y'>>1)*40 + (x'>>3)]
 *   return (BitExtract(byte, x'&6) & 3)   — 2-bit hixel; 3 = solid
 * BitExtract is loader 0x00213FA0 (arithmetic >> for shift in {0,2,4,6}).
 * Pixel space: grid = pixel - bias, so hixel_pixel probes screen 0..319 × 0..223.
 * Coarse tile view: 40×28 tiles of 8×8 pixels (4 fine rows × 1 column byte).
 */
#define PAN_COLL_TILES_X 40u
#define PAN_COLL_TILES_Y 28u
#define PAN_COLL_FINE_ROWS 112u
#define PAN_COLL_CELL_BYTES 4u /* legacy name: bytes per coarse tile column strip */
#define PAN_COLL_SIZE (PAN_COLL_TILES_X * PAN_COLL_FINE_ROWS)
#define PAN_COLL_PIXELS_PER_TILE 8u
#define PAN_COLL_X_BIAS 0xA0
#define PAN_COLL_Y_BIAS 0x70
#define PAN_COLL_X_MAX 0x13F
#define PAN_COLL_Y_MAX 0xDF
#define PAN_COLL_HIXEL_SOLID 3

typedef struct {
    uint8_t cells[PAN_COLL_SIZE];
    size_t size;
} pan_collision_map;

/* Foot/box deltas in grid space (= pixel deltas). MAP symbols HitChkScrn*.
 * Image .data is zero; runtime fills them — native defaults are playable AABB. */
typedef struct {
    int top_left_x, top_left_y;
    int top_right_x, top_right_y;
    int bot_left_x, bot_left_y;
    int bot_right_x, bot_right_y;
} pan_hit_offsets;

int pan_collision_map_load(pan_collision_map *map, const uint8_t *cells, size_t size);

/* _PANCOLLGetHixel: 0..3 hixel, or -1 if out of bounds (Saturn 0xffffffff). */
int pan_collision_get_hixel(const pan_collision_map *map, int grid_x, int grid_y);
/* Same probe with pixel coordinates (applies inverse bias). */
int pan_collision_hixel_pixel(const pan_collision_map *map, int pixel_x, int pixel_y);

/* Coarse helpers on the fine map. Solid = any hixel == 3 in the 8×8 tile. */
int pan_collision_map_solid_tile(const pan_collision_map *map, int tx, int ty);
int pan_collision_map_solid_pixel(const pan_collision_map *map, int pixel_x, int pixel_y);
/* Topmost solid coarse tile row at column tx at or below start_ty; -1 if none. */
int pan_collision_map_ground_ty(const pan_collision_map *map, int tx, int start_ty);

/*
 * Default HitChk from BonesSetConfig template at bnscfg 0x002873EC
 * (Top/Bot × Left/Right = ±5, top Y=-8, bot Y=+12). Image .data is 0 until
 * BonesSetConfig copies config+0x50..0x6c into HitChkScrn*.
 */
void pan_hit_offsets_default(pan_hit_offsets *o);
/* Copy from a config object base (big-endian ints at +0x50..+0x6c). */
int pan_hit_offsets_from_config(pan_hit_offsets *o, const void *config, size_t config_size);

/*
 * _BonesPushOutOfWalls @0x0025c694: while corner hixel==3, nudge grid X.
 * Returns 1 if any push happened. Does not call WorldXform (caller maps grid→world).
 */
int pan_bones_push_out_of_walls(const pan_collision_map *map, int *grid_x, int *grid_y,
                                const pan_hit_offsets *o);

/*
 * _BonesWallAboveCheck @0x0025c900 (simplified): 1 if a top corner is solid
 * while the matching bottom is free — ceiling / overhang above feet.
 */
int pan_bones_wall_above(const pan_collision_map *map, int grid_x, int grid_y,
                         const pan_hit_offsets *o);

#endif
