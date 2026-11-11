#include "runway_compose.h"
#include "mb_font.h"

int runway_compose_clear(uint8_t *dst, unsigned width, unsigned height,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!dst || !width || !height) return -1;
    size_t n = (size_t)width * height;
    for (size_t i = 0; i < n; i++) {
        dst[4 * i + 0] = r;
        dst[4 * i + 1] = g;
        dst[4 * i + 2] = b;
        dst[4 * i + 3] = a;
    }
    return 0;
}

static void put_pixel(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                      int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x < 0 || y < 0 || (unsigned)x >= dst_w || (unsigned)y >= dst_h) return;
    size_t i = ((size_t)y * dst_w + (unsigned)x) * 4;
    dst[i + 0] = r;
    dst[i + 1] = g;
    dst[i + 2] = b;
    dst[i + 3] = a;
}

int runway_compose_blit_opaque(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                               const uint8_t *src, unsigned src_w, unsigned src_h,
                               int dst_x, int dst_y) {
    if (!dst || !src || !dst_w || !dst_h || !src_w || !src_h) return -1;
    for (unsigned y = 0; y < src_h; y++) {
        for (unsigned x = 0; x < src_w; x++) {
            size_t s = ((size_t)y * src_w + x) * 4;
            put_pixel(dst, dst_w, dst_h, dst_x + (int)x, dst_y + (int)y,
                      src[s], src[s + 1], src[s + 2], src[s + 3]);
        }
    }
    return 0;
}

int runway_compose_blit_sprite_ex(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                                  const uint8_t *src, unsigned src_w, unsigned src_h,
                                  int dst_x, int dst_y, int flip_x) {
    if (!dst || !src || !dst_w || !dst_h || !src_w || !src_h) return -1;
    for (unsigned y = 0; y < src_h; y++) {
        for (unsigned x = 0; x < src_w; x++) {
            unsigned sx = flip_x ? (src_w - 1u - x) : x;
            size_t s = ((size_t)y * src_w + sx) * 4;
            if (!src[s + 3]) continue;
            put_pixel(dst, dst_w, dst_h, dst_x + (int)x, dst_y + (int)y,
                      src[s], src[s + 1], src[s + 2], 255);
        }
    }
    return 0;
}

int runway_compose_blit_sprite(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                               const uint8_t *src, unsigned src_w, unsigned src_h,
                               int dst_x, int dst_y) {
    return runway_compose_blit_sprite_ex(dst, dst_w, dst_h, src, src_w, src_h,
                                         dst_x, dst_y, 0);
}

int runway_compose_hud_skelbar(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                               int segments, int max_segments,
                               int dst_x, int dst_y) {
    int i, x, y, seg_w = 10, seg_h = 8, gap = 2;
    if (!dst || !dst_w || !dst_h || max_segments <= 0) return -1;
    if (segments < 0) segments = 0;
    if (segments > max_segments) segments = max_segments;
    for (i = 0; i < max_segments; i++) {
        int filled = i < segments;
        uint8_t r = filled ? 40 : 20;
        uint8_t g = filled ? 200 : 40;
        uint8_t b = filled ? 60 : 40;
        int ox = dst_x + i * (seg_w + gap);
        for (y = 0; y < seg_h; y++)
            for (x = 0; x < seg_w; x++)
                put_pixel(dst, dst_w, dst_h, ox + x, dst_y + y, r, g, b, 255);
    }
    return 0;
}

int runway_compose_hud_pause(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                             const uint8_t *pause_rgba, unsigned pw, unsigned ph,
                             int show) {
    int bx, by, x, y;
    if (!dst || !show) return 0;
    if (pause_rgba && pw && ph) {
        int dx = ((int)dst_w - (int)pw) / 2;
        int dy = ((int)dst_h - (int)ph) / 2;
        return runway_compose_blit_sprite(dst, dst_w, dst_h, pause_rgba, pw, ph, dx, dy);
    }
    /* Fallback PAUSE.O plate with centered PAUSED label. */
    bx = (int)dst_w / 2 - 40;
    by = (int)dst_h / 2 - 12;
    for (y = 0; y < 24; y++)
        for (x = 0; x < 80; x++)
            put_pixel(dst, dst_w, dst_h, bx + x, by + y, 200, 160, 40, 255);
    mb_font_draw_text_centered(dst, dst_w, dst_h, by + 2, "PAUSED",
                               255, 255, 240, 40, 24, 8, 1);
    return 0;
}

int runway_compose_blit_sprite_scaled(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                                      const uint8_t *src, unsigned src_w, unsigned src_h,
                                      int dst_x, int dst_y,
                                      unsigned out_w, unsigned out_h, int flip_x) {
    unsigned dx, dy;
    if (!dst || !src || !dst_w || !dst_h || !src_w || !src_h || !out_w || !out_h) return -1;
    if (out_w == src_w && out_h == src_h)
        return runway_compose_blit_sprite_ex(dst, dst_w, dst_h, src, src_w, src_h,
                                             dst_x, dst_y, flip_x);
    for (dy = 0; dy < out_h; dy++) {
        int py = dst_y + (int)dy;
        unsigned sy;
        if (py < 0 || (unsigned)py >= dst_h) continue;
        sy = (dy * src_h) / out_h;
        if (sy >= src_h) sy = src_h - 1;
        for (dx = 0; dx < out_w; dx++) {
            int px = dst_x + (int)dx;
            unsigned sx, rx;
            const uint8_t *sp;
            if (px < 0 || (unsigned)px >= dst_w) continue;
            sx = (dx * src_w) / out_w;
            if (sx >= src_w) sx = src_w - 1;
            rx = flip_x ? (src_w - 1u - sx) : sx;
            sp = src + ((size_t)sy * src_w + rx) * 4u;
            if (sp[3])
                put_pixel(dst, dst_w, dst_h, px, py, sp[0], sp[1], sp[2], 255);
        }
    }
    return 0;
}
