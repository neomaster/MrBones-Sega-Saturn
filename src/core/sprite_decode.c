#include "sprite_decode.h"
#include <stdint.h>

int mb_sprite_decode(const uint8_t *pixels, size_t pixels_size,
                     unsigned width, unsigned height, uint16_t pmod,
                     const uint8_t *palette, size_t palette_size,
                     uint8_t *rgba, size_t rgba_size) {
    unsigned mode = (pmod >> 3) & 7;
    size_t count;
    if (!pixels || !rgba || !width || !height || (pmod & 0x40) || !(pmod & 0x80)) return -1;
    if (mode != 4 && mode != 5) return -1;
    if (mode == 4 && (!palette || palette_size < 1024)) return -1;
    if ((size_t)height > SIZE_MAX / width) return -1;
    count = (size_t)width * height;
    if (count > SIZE_MAX / 4 || rgba_size < count * 4 || pixels_size < count * (mode == 5 ? 2 : 1)) return -1;
    /* Reject unsupported non-RGB mode-5 pixels before changing the destination. */
    if (mode == 5) for (size_t i = 0; i < count; i++) {
        unsigned word = ((unsigned)pixels[2*i] << 8) | pixels[2*i+1];
        if (word && !(word & 0x8000)) return -1;
    }
    for (size_t i = 0; i < count; i++) {
        unsigned r, g, b, opaque;
        if (mode == 4) {
            unsigned index = pixels[i];
            r = palette[index*4+1]; g = palette[index*4+2]; b = palette[index*4+3];
            opaque = index != 0;
        } else {
            unsigned word = ((unsigned)pixels[2*i] << 8) | pixels[2*i+1];
            r = word & 31; g = (word >> 5) & 31; b = (word >> 10) & 31;
            r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
            opaque = word != 0;
        }
        rgba[4*i] = (uint8_t)r; rgba[4*i+1] = (uint8_t)g;
        rgba[4*i+2] = (uint8_t)b; rgba[4*i+3] = opaque ? 255 : 0;
    }
    return 0;
}
