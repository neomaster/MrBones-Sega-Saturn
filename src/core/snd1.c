#include "snd1.h"

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

int snd1_parse(const uint8_t *data, size_t size, snd1_span out[SND1_SPAN_COUNT]) {
    snd1_span spans[SND1_SPAN_COUNT];
    if (!data || !out || size < 96) return -1;
    size_t previous = 96;
    for (size_t i = 0; i < SND1_SPAN_COUNT; i++) {
        size_t offset = be32(data + i * 8);
        size_t length = be32(data + i * 8 + 4);
        if ((offset & 3) || offset < previous || offset > size || length > size - offset)
            return -1;
        spans[i].data = data + offset;
        spans[i].size = length;
        previous = offset + length;
    }
    for (size_t i = 0; i < SND1_SPAN_COUNT; i++) out[i] = spans[i];
    return 0;
}
