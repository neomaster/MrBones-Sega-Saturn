#ifndef MB_SPRITE_DECODE_H
#define MB_SPRITE_DECODE_H
#include <stddef.h>
#include <stdint.h>

/*
 * Experimental row-major VDP1 modes 4 and 5 conversion into RGBA8888.
 * Supported PMOD: SPD=0, ECD=1; palette is 256 4-byte entries [unused,R,G,B].
 * Mode 4 requires the current VDP2 palette selected by the original draw command.
 * All sizes are byte lengths. Destination must not overlap source or palette.
 * Returns 0 on success, -1 for unsupported mode/flags, invalid size or invalid RGB.
 */
int mb_sprite_decode(const uint8_t *pixels, size_t pixels_size,
                     unsigned width, unsigned height, uint16_t pmod,
                     const uint8_t *palette, size_t palette_size,
                     uint8_t *rgba, size_t rgba_size);
#endif
