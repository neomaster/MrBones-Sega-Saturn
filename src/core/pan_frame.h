#ifndef MB_PAN_FRAME_H
#define MB_PAN_FRAME_H
#include <stddef.h>
#include <stdint.h>
#define PAN_WIDTH 320u
#define PAN_HEIGHT 224u
#define PAN_PLANE_SIZE (PAN_WIDTH * PAN_HEIGHT)
#define PAN_BUFFER_SIZE (4u * PAN_PLANE_SIZE)
/* Frame is contiguous; pixels contains four 320x224 8-bit planes. The initial
 * pixels carry the previous frame state. Source and outputs must not overlap.
 * No palette is applied. Outputs remain unchanged on any error. */
int pan_frame_decode(const uint8_t *frame, size_t size, uint8_t *pixels,
                     size_t pixels_size, unsigned plane, uint8_t *collision,
                     size_t collision_capacity, size_t *collision_size,
                     size_t *consumed);

/* BE32 size prefix of one pandcomp record; 0 if invalid. */
uint32_t pan_frame_byte_size(const uint8_t *frame, size_t size);
/* Count chained records in a 15PAN blob (advance by BE32 size). */
int pan_stream_count(const uint8_t *blob, size_t size, unsigned *out_count);
/* Byte offset + size of record `index` (0-based). */
int pan_stream_record(const uint8_t *blob, size_t size, unsigned index,
                      size_t *out_off, uint32_t *out_sz);
#endif
