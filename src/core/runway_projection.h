#ifndef RUNWAY_PROJECTION_H
#define RUNWAY_PROJECTION_H
#include <stdint.h>

/* Static reconstruction of RUNWAY _RWorldSprite's center and visibility.
   Coordinates are the original 32-bit fixed-point bit patterns. */
int runway_project_center(uint32_t camera_x, uint32_t camera_y,
                          uint32_t world_x, uint32_t world_y,
                          int32_t *screen_x, int32_t *screen_y);
#endif
