#include "pan_collision.h"
#include <string.h>

int pan_collision_decode(const uint8_t *src, size_t size, uint8_t *dst,
                         size_t capacity, size_t *consumed, size_t *written) {
    if (!src || !dst || !consumed || !written || size < 6) return -1;
    size_t frame_size = ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
                        ((uint32_t)src[2] << 8) | src[3];
    if (frame_size < 6 || frame_size > size) return -1;
    size_t pairs_size = ((size_t)src[4] << 8) | src[5];
    if (!pairs_size || (pairs_size & 1) || pairs_size > frame_size - 6) return -1;
    size_t out = 0;
    for (size_t i = 0; i < pairs_size; i += 2) {
        size_t run = (size_t)src[6+i] + 1;
        if (run > capacity - out) return -1;
        out += run;
    }
    out = 0;
    for (size_t i = 0; i < pairs_size; i += 2) {
        size_t run = (size_t)src[6+i] + 1;
        memset(dst + out, src[7+i], run);
        out += run;
    }
    *consumed = 6 + pairs_size;
    *written = out;
    return 0;
}
