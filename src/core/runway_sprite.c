#include "runway_sprite.h"

#define RUNWAY_CODE_BASE UINT32_C(0x00232000)
#define RUNWAY_STXTR_BASE UINT32_C(0x25c08000)

static uint16_t read16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}
static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

int runway_sprite_resolve(const void *code, size_t code_size, uint32_t address,
                          const void *textures, size_t textures_size,
                          stxtr_record *record, uint16_t *pmod) {
    stxtr_record temp;
    size_t offset, descriptor;
    uint32_t target;
    uint16_t mode, dimensions;
    if (!code || !textures || !record || !pmod || address < RUNWAY_CODE_BASE) return -1;
    descriptor = (size_t)(address - RUNWAY_CODE_BASE);
    if (descriptor > code_size || code_size - descriptor < 16) return -1;
    const uint8_t *p = (const uint8_t *)code + descriptor;
    target = read32(p);
    if (target < RUNWAY_STXTR_BASE) return -1;
    offset = (size_t)(target - RUNWAY_STXTR_BASE);
    if (stxtr_next(textures, textures_size, &offset, &temp) != 1) return -1;
    dimensions = read16(p + 14);
    if ((temp.width & 7) || temp.width > 504 || temp.height > 255 ||
        dimensions != (uint16_t)((temp.width / 8) << 8 | temp.height)) return -1;
    mode = read16(p + 8);
    if (((mode >> 3) & 7) != 4 && ((mode >> 3) & 7) != 5) return -1;
    if (temp.size != (size_t)temp.width * temp.height * ((((mode >> 3) & 7) == 5) ? 2 : 1))
        return -1;
    *record = temp;
    *pmod = mode;
    return 0;
}
