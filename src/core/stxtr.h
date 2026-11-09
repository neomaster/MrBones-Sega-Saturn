#ifndef MB_STXTR_H
#define MB_STXTR_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t width, height;
    uint32_t header_word;
    const uint8_t *data;
    size_t size;
} stxtr_record;

/* Returns 1 for a record, 0 at end, -1 for malformed input. */
int stxtr_next(const void *data, size_t size, size_t *offset, stxtr_record *record);
#endif
