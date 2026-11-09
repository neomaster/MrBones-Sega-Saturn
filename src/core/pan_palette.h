#ifndef MB_PAN_PALETTE_H
#define MB_PAN_PALETTE_H
#include <stddef.h>
#include <stdint.h>
/* Applies RUNWAY _MainPalette stream to caller-provided palette words.
 * Source and destination must not overlap. Destination and consumed remain
 * unchanged on failure. Palette words preserve the original low 24 bits. */
int pan_palette_apply(const uint8_t *src, size_t size, uint32_t *palette,
                      size_t entries, size_t *consumed);
#endif
