#ifndef MB_PAN_COLLISION_H
#define MB_PAN_COLLISION_H
#include <stddef.h>
#include <stdint.h>
/* Decode the RLE prefix at byte offset 4 in a contiguous PAN frame.
 * Source and destination must not overlap. On success reports consumed source
 * bytes and emitted map bytes. */
int pan_collision_decode(const uint8_t *src, size_t size, uint8_t *dst,
                         size_t capacity, size_t *consumed, size_t *written);
#endif
