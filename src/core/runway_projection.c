#include "runway_projection.h"

static int32_t project(uint32_t camera, uint32_t world) {
    uint32_t high = ((camera - world) * UINT32_C(28)) >> 16;
    return high < 0x8000 ? (int32_t)high : (int32_t)high - 0x10000;
}

int runway_project_center(uint32_t camera_x, uint32_t camera_y,
                          uint32_t world_x, uint32_t world_y,
                          int32_t *screen_x, int32_t *screen_y) {
    if (!screen_x || !screen_y) return -1;
    *screen_x = project(camera_x, world_x);
    *screen_y = project(camera_y, world_y);
    return (uint32_t)(*screen_x + 220) <= 440 &&
           (uint32_t)(*screen_y + 200) <= 400;
}
