#ifndef RUNWAY_PROJECTION_H
#define RUNWAY_PROJECTION_H
#include <stdint.h>

#include <stddef.h>
#include "pan_collision_map.h"

typedef struct {
    const char *stage;
    unsigned pixels_per_unit;
    uint32_t init_address;
} bones_projection;

const bones_projection *bones_projection_for(const char *stage);
typedef struct {
    pan_hit_offsets hit;
    int start_x, start_y, facing;
    uint32_t config;
    int32_t jump_impulse;
    unsigned boost_ticks;
} bones_stage_init;

int bones_read_config(const void *code, size_t size, uint32_t config, bones_stage_init *out);
int bones_projection_init(const bones_projection *projection, const void *code, size_t size,
                          bones_stage_init *out);

/* Static reconstruction of RUNWAY _RWorldSprite's center and visibility.
   Coordinates are the original 32-bit fixed-point bit patterns. */
int runway_project_center(uint32_t camera_x, uint32_t camera_y,
                          uint32_t world_x, uint32_t world_y,
                          int32_t *screen_x, int32_t *screen_y);
#endif
