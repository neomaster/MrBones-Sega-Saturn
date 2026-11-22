#ifndef MB_UPSCALE_H
#define MB_UPSCALE_H
#include <stddef.h>
#include <stdint.h>

/*
 * Edge-directed pixel-art upscalers (Scale2x, Scale3x, xBR2x, xBR3x) and
 * pure-C PNG/PAM/PPM/TGA RGBA image loader for custom HD sprite overrides.
 */

typedef enum {
    MB_UPSCALE_NEAREST = 0,
    MB_UPSCALE_SCALE2X = 1,
    MB_UPSCALE_SCALE3X = 2,
    MB_UPSCALE_XBR2X = 3,
    MB_UPSCALE_XBR3X = 4
} mb_upscale_mode;

unsigned mb_upscale_mode_factor(mb_upscale_mode mode);
const char *mb_upscale_mode_name(mb_upscale_mode mode);
int mb_upscale_mode_parse(const char *name, mb_upscale_mode *out);

/* Upscale row-major RGBA8888 src[w*h*4] -> dst[(w*scale)*(h*scale)*4].
 * For MB_UPSCALE_NEAREST, `scale_override` (1..6, 0 defaults to 3) selects the factor.
 * For fixed-factor modes (2x/3x), `scale_override` is ignored unless set to 4 on
 * 2x modes, which runs a clean 2x -> 2x pass for 4x output.
 * Returns 0 on success, -1 on invalid args. */
int mb_upscale_rgba(const uint8_t *src, unsigned w, unsigned h,
                    mb_upscale_mode mode, unsigned scale_override,
                    uint8_t *dst, size_t dst_size);

/* Straight-alpha sprite compositor onto an upscaled RGBA surface.
 * Uses 1:1 blit when src_w==out_w && src_h==out_h && !flip_x, else crisp bilinear
 * premultiplied-RGB interpolation so transparent boundaries never bleed black halos. */
int mb_upscale_blit_alpha(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                          const uint8_t *src, unsigned src_w, unsigned src_h,
                          int dst_x, int dst_y, unsigned out_w, unsigned out_h,
                          int flip_x);

/* Load a PNG, PAM (P7), PPM (P6), or uncompressed 24/32-bit TGA into newly
 * malloc'd RGBA8888 buffer (*out_rgba). Caller must free(*out_rgba).
 * Returns 0 on success, -1 on error. */
int mb_image_load_rgba(const char *path, uint8_t **out_rgba,
                       unsigned *out_w, unsigned *out_h);

#endif
