#include "runway_projection.h"
#include <string.h>

/* USA loaded addresses, cross-checked against each disc's full-body Spring constructor. */
static const bones_projection projections[] = {
    {"RUNWAY", 28, 0x28db0c}, {"MAUSO", 42, 0x2d113c}, {"VALLEY", 42, 0x2fafcc},
    {"GUITAR", 64, 0x28d854}, {"BAT1", 39, 0x2da134}, {"BAT2", 39, 0x2dba70},
    {"BAT3", 39, 0x2dde80}, {"LOGS", 21, 0x2dbb44}, {"DRUM", 64, 0x2790a0},
    {"GLASS", 25, 0x2dd15c}, {"BIGBNS", 42, 0x270a60}, {"VORTEX", 25, 0x28ffc8},
    {"RIDE", 42, 0x27203c}, {"VILLAGE", 42, 0x2c92b8}, {"ICE1", 32, 0x2eeb88},
    {"ICE2", 73, 0x2d1b88}, {"SHADOW", 18, 0x2cb5bc}, {"MOAT", 32, 0x2e7058},
    {"STAING", 64, 0x27d59c}, {"GENRM", 32, 0x2703a4}, {"LAFF", 64, 0x2c2450},
    {"FINALE", 42, 0x2bc35c}
};

const bones_projection *bones_projection_for(const char *stage) {
    for (size_t i = 0; i < sizeof projections / sizeof projections[0]; i++)
        if (stage && !strcmp(projections[i].stage, stage)) return &projections[i];
    return NULL;
}

static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int layout_pixel(uint32_t word_value, unsigned scale) {
    int64_t n = -(int64_t)(int32_t)word_value * scale;
    return (int)((n + (n < 0 ? -32768 : 32768)) / 65536);
}

int bones_projection_init(const bones_projection *projection, const void *code, size_t size,
                          bones_stage_init *out) {
    if (!out || !projection || !code || projection->init_address < 0x232000u) return -1;
    size_t offset = projection->init_address - 0x232000u;
    if (offset > size || size - offset < 24u) return -1;
    const uint8_t *init = (const uint8_t *)code + offset;
    uint32_t config = word(init);
    if (config < 0x232000u || config - 0x232000u > size) return -1;
    out->start_x = layout_pixel(word(init + 8), projection->pixels_per_unit);
    out->start_y = layout_pixel(word(init + 12), projection->pixels_per_unit);
    out->facing = (word(init + 20) & 1023u) == 256u ? -1 : 1;
    return bones_read_config(code, size, config, out);
}

int bones_read_config(const void *code, size_t size, uint32_t config, bones_stage_init *out) {
    if (!code || !out || config < 0x232000u || config - 0x232000u >= size) return -1;
    size_t offset = config - 0x232000u;
    out->config = config;
    const uint8_t *cfg = (const uint8_t *)code + offset;
    if (pan_hit_offsets_from_config(&out->hit, cfg, size - offset)) return -1;
    int64_t width = (int64_t)out->hit.top_right_x - out->hit.top_left_x;
    int64_t height = (int64_t)out->hit.bot_right_y - out->hit.top_right_y;
    if (width < 0 || width > 320 || height < 0 || height > 224 ||
        out->hit.top_left_x < -320 || out->hit.top_right_x > 320 ||
        out->hit.top_right_y < -224 || out->hit.bot_right_y > 224) return -1;
    uint32_t spring = word(cfg + 16);
    if (spring < 0x232000u || size < 28 || spring - 0x232000u > size - 28) return -1;
    const uint8_t *params = (const uint8_t *)code + (spring - 0x232000u);
    uint32_t impulse = word(params + 20), ticks = word(params + 24);
    if (impulse > INT32_MAX || ticks > UINT8_MAX) return -1;
    out->jump_impulse = -(int32_t)impulse;
    out->boost_ticks = ticks;
    return 0;
}

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
