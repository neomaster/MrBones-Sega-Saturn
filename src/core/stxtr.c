#include "stxtr.h"
#include <string.h>

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

int stxtr_next(const void *data, size_t size, size_t *offset, stxtr_record *record) {
    static const uint8_t marker[] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
    if (!data || !offset || !record || *offset > size || (*offset & 15)) return -1;
    if (*offset == size) return 0;
    if (size - *offset < 16) return -1;
    const uint8_t *p = (const uint8_t *)data + *offset;
    size_t payload = (size_t)be16(p + 8) * 4;
    size_t padded = (payload + 15) & ~(size_t)15;
    if (!be16(p) || !be16(p + 2) || memcmp(p + 10, marker, sizeof marker) ||
        padded > size - *offset - 16) return -1;
    record->width = be16(p);
    record->height = be16(p + 2);
    record->header_word = be32(p + 4);
    record->data = p + 16;
    record->size = payload;
    *offset += 16 + padded;
    return 1;
}
