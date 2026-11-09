/*
 * 8x8 bitmap font renderer for Mr. Bones host framebuffer (320x224 RGBA).
 * Used by MENU, HUD (Pause / Stage banner / Rhythm highway), INSERTCD, and CREDITS.
 */
#ifndef MB_FONT_H
#define MB_FONT_H

#include <stddef.h>
#include <stdint.h>

/* Returns an 8-byte glyph row array for ASCII character ch (bit 7 = left pixel). */
const uint8_t *mb_font_glyph(char ch);

/* Width in pixels of a NUL-terminated string at the given integer scale (>= 1). */
unsigned mb_font_text_width(const char *text, unsigned scale);

/* Draw a single character into dst_rgba (width dst_w, height dst_h, 4 bytes/px). */
void mb_font_draw_char(uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                       int x, int y, char ch,
                       uint8_t r, uint8_t g, uint8_t b, unsigned scale);

/* Draw a NUL-terminated string into dst_rgba. */
void mb_font_draw_text(uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                       int x, int y, const char *text,
                       uint8_t r, uint8_t g, uint8_t b, unsigned scale);

/* Draw a NUL-terminated string with a 1px/scale drop shadow at (x+scale, y+scale). */
void mb_font_draw_text_shadow(uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                              int x, int y, const char *text,
                              uint8_t r, uint8_t g, uint8_t b,
                              uint8_t sr, uint8_t sg, uint8_t sb,
                              unsigned scale);

/* Draw centered text horizontally at row y with drop shadow. */
void mb_font_draw_text_centered(uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                                int y, const char *text,
                                uint8_t r, uint8_t g, uint8_t b,
                                uint8_t sr, uint8_t sg, uint8_t sb,
                                unsigned scale);

#endif
