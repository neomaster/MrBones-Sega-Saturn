#ifndef MB_RUNWAY_SPRITE_H
#define MB_RUNWAY_SPRITE_H
#include <stddef.h>
#include <stdint.h>
#include "stxtr.h"

/* RUNWAY, USA V1.000: resolve a known descriptor in 03CODE to 04STXT.
 * Address is the SH-2 link address of the descriptor, not a native pointer.
 * Returns 0 on success; the record's data borrows the textures buffer.
 * This only resolves texels and PMOD; placement, palette choice and effects
 * have to come from reconstructed game logic. */
int runway_sprite_resolve(const void *code, size_t code_size, uint32_t address,
                          const void *textures, size_t textures_size,
                          stxtr_record *record, uint16_t *pmod);
#endif
