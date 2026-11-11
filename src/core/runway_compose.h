#ifndef MB_RUNWAY_COMPOSE_H
#define MB_RUNWAY_COMPOSE_H
#include <stddef.h>
#include <stdint.h>

/* Diagnostic RGBA8888 composition helpers for RUNWAY previews.
 * Not a VDP1/VDP2 command-list renderer. Alpha 0 source texels are skipped.
 * Destination is row-major RGBA; coordinates may be partially off-screen. */

int runway_compose_clear(uint8_t *dst, unsigned width, unsigned height,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a);

/* Copy a full background of matching size into dst. */
int runway_compose_blit_opaque(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                               const uint8_t *src, unsigned src_w, unsigned src_h,
                               int dst_x, int dst_y);

/* Alpha-test blit: source alpha == 0 skips the texel; any non-zero copies RGB
 * and writes opaque alpha 255. Top-left of the sprite is placed at (dst_x, dst_y). */
int runway_compose_blit_sprite(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                               const uint8_t *src, unsigned src_w, unsigned src_h,
                               int dst_x, int dst_y);

/* Same as blit_sprite; flip_x mirrors about the sprite vertical axis (VDP1 H-flip). */
int runway_compose_blit_sprite_ex(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                                  const uint8_t *src, unsigned src_w, unsigned src_h,
                                  int dst_x, int dst_y, int flip_x);

/* Scaled alpha-test blit for VDP1 RWorldSprite / RGang depth scaling. */
int runway_compose_blit_sprite_scaled(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                                      const uint8_t *src, unsigned src_w, unsigned src_h,
                                      int dst_x, int dst_y,
                                      unsigned out_w, unsigned out_h, int flip_x);

/* §16 HUD: CSkelBar 14 segments (HEALTH.O stand-in) + optional PAUSE.O sprite. */
int runway_compose_hud_skelbar(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                               int segments, int max_segments,
                               int dst_x, int dst_y);
int runway_compose_hud_pause(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                             const uint8_t *pause_rgba, unsigned pw, unsigned ph,
                             int show);

#endif
