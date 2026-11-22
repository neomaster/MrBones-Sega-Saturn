#include "core/upscale.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned mb_upscale_mode_factor(mb_upscale_mode mode) {
    switch (mode) {
    case MB_UPSCALE_SCALE2X:
    case MB_UPSCALE_XBR2X:
        return 2u;
    case MB_UPSCALE_SCALE3X:
    case MB_UPSCALE_XBR3X:
        return 3u;
    case MB_UPSCALE_NEAREST:
    default:
        return 1u;
    }
}

const char *mb_upscale_mode_name(mb_upscale_mode mode) {
    switch (mode) {
    case MB_UPSCALE_NEAREST: return "nearest";
    case MB_UPSCALE_SCALE2X: return "scale2x";
    case MB_UPSCALE_SCALE3X: return "scale3x";
    case MB_UPSCALE_XBR2X:   return "xbr2x";
    case MB_UPSCALE_XBR3X:   return "xbr3x";
    default:                 return "xbr3x";
    }
}

static int ci_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

int mb_upscale_mode_parse(const char *name, mb_upscale_mode *out) {
    if (!name || !out) return -1;
    if (ci_eq(name, "nearest") || ci_eq(name, "1x")) {
        *out = MB_UPSCALE_NEAREST;
        return 0;
    }
    if (ci_eq(name, "scale2x") || ci_eq(name, "epx") || ci_eq(name, "2x")) {
        *out = MB_UPSCALE_SCALE2X;
        return 0;
    }
    if (ci_eq(name, "scale3x")) {
        *out = MB_UPSCALE_SCALE3X;
        return 0;
    }
    if (ci_eq(name, "xbr2x")) {
        *out = MB_UPSCALE_XBR2X;
        return 0;
    }
    if (ci_eq(name, "xbr3x") || ci_eq(name, "xbr") || ci_eq(name, "3x") ||
        ci_eq(name, "hd")) {
        *out = MB_UPSCALE_XBR3X;
        return 0;
    }
    return -1;
}

typedef struct {
    uint8_t r, g, b, a;
} rgba_px;

static rgba_px sample_clamp(const uint8_t *src, unsigned w, unsigned h, int x, int y) {
    rgba_px p;
    const uint8_t *q;
    if (x < 0) x = 0;
    else if ((unsigned)x >= w) x = (int)w - 1;
    if (y < 0) y = 0;
    else if ((unsigned)y >= h) y = (int)h - 1;
    q = src + ((size_t)(unsigned)y * w + (size_t)(unsigned)x) * 4u;
    p.r = q[0];
    p.g = q[1];
    p.b = q[2];
    p.a = q[3];
    return p;
}

static void store_px(uint8_t *dst, unsigned dst_w, unsigned x, unsigned y, rgba_px p) {
    uint8_t *q = dst + ((size_t)y * dst_w + x) * 4u;
    q[0] = p.r;
    q[1] = p.g;
    q[2] = p.b;
    q[3] = p.a;
}

static unsigned px_dist(rgba_px a, rgba_px b) {
    int dr, dg, db, y, u, v;
    if (a.a == 0 && b.a == 0) return 0u;
    if ((a.a == 0) != (b.a == 0)) {
        int da = (int)a.a - (int)b.a;
        return 2048u + (unsigned)(da < 0 ? -da : da) * 48u;
    }
    dr = (int)a.r - (int)b.r;
    dg = (int)a.g - (int)b.g;
    db = (int)a.b - (int)b.b;
    if (dr == 0 && dg == 0 && db == 0 && a.a == b.a) return 0u;
    y = (dr + 2 * dg + db) >> 2;
    u = (dr - db) >> 1;
    v = (-dr + 2 * dg - db) >> 2;
    if (y < 0) y = -y;
    if (u < 0) u = -u;
    if (v < 0) v = -v;
    return (unsigned)(48 * y + 7 * u + 6 * v + abs((int)a.a - (int)b.a) * 16);
}

static int px_eq_exact(rgba_px a, rgba_px b) {
    if (a.a == 0 && b.a == 0) return 1;
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static int px_eq_y(rgba_px a, rgba_px b) {
    return px_dist(a, b) < 720u;
}

/* Blend (w_a * a + w_b * b) / (w_a + w_b), avoiding dark RGB fringe at alpha==0 edges. */
static rgba_px px_blend(rgba_px a, rgba_px b, unsigned w_b, unsigned den) {
    rgba_px out;
    unsigned w_a = den - w_b;
    if (a.a == 0 && b.a == 0) {
        out.r = out.g = out.b = out.a = 0;
        return out;
    }
    if (a.a == 0) {
        unsigned na = (b.a * w_b + den / 2u) / den;
        out.r = b.r;
        out.g = b.g;
        out.b = b.b;
        out.a = (uint8_t)na;
        return out;
    }
    if (b.a == 0) {
        unsigned na = (a.a * w_a + den / 2u) / den;
        out.r = a.r;
        out.g = a.g;
        out.b = a.b;
        out.a = (uint8_t)na;
        return out;
    }
    out.r = (uint8_t)((a.r * w_a + b.r * w_b + den / 2u) / den);
    out.g = (uint8_t)((a.g * w_a + b.g * w_b + den / 2u) / den);
    out.b = (uint8_t)((a.b * w_a + b.b * w_b + den / 2u) / den);
    out.a = (uint8_t)((a.a * w_a + b.a * w_b + den / 2u) / den);
    return out;
}

static void upscale_nearest(const uint8_t *src, unsigned w, unsigned h,
                            unsigned factor, uint8_t *dst) {
    unsigned x, y, sx, sy;
    unsigned dst_w = w * factor;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            const uint8_t *p = src + ((size_t)y * w + x) * 4u;
            for (sy = 0; sy < factor; sy++) {
                uint8_t *row = dst + ((size_t)(y * factor + sy) * dst_w + x * factor) * 4u;
                for (sx = 0; sx < factor; sx++) {
                    row[4 * sx + 0] = p[0];
                    row[4 * sx + 1] = p[1];
                    row[4 * sx + 2] = p[2];
                    row[4 * sx + 3] = p[3];
                }
            }
        }
    }
}

static void upscale_scale2x(const uint8_t *src, unsigned w, unsigned h, uint8_t *dst) {
    unsigned x, y, dst_w = w * 2u;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            rgba_px b = sample_clamp(src, w, h, (int)x, (int)y - 1);
            rgba_px d = sample_clamp(src, w, h, (int)x - 1, (int)y);
            rgba_px e = sample_clamp(src, w, h, (int)x, (int)y);
            rgba_px f = sample_clamp(src, w, h, (int)x + 1, (int)y);
            rgba_px hpx = sample_clamp(src, w, h, (int)x, (int)y + 1);
            rgba_px e0 = e, e1 = e, e2 = e, e3 = e;
            if (!px_eq_exact(b, hpx) && !px_eq_exact(d, f)) {
                e0 = px_eq_exact(d, b) ? d : e;
                e1 = px_eq_exact(b, f) ? f : e;
                e2 = px_eq_exact(d, hpx) ? d : e;
                e3 = px_eq_exact(hpx, f) ? f : e;
            }
            store_px(dst, dst_w, x * 2u + 0u, y * 2u + 0u, e0);
            store_px(dst, dst_w, x * 2u + 1u, y * 2u + 0u, e1);
            store_px(dst, dst_w, x * 2u + 0u, y * 2u + 1u, e2);
            store_px(dst, dst_w, x * 2u + 1u, y * 2u + 1u, e3);
        }
    }
}

static void upscale_scale3x(const uint8_t *src, unsigned w, unsigned h, uint8_t *dst) {
    unsigned x, y, dst_w = w * 3u;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            rgba_px a = sample_clamp(src, w, h, (int)x - 1, (int)y - 1);
            rgba_px b = sample_clamp(src, w, h, (int)x, (int)y - 1);
            rgba_px c = sample_clamp(src, w, h, (int)x + 1, (int)y - 1);
            rgba_px d = sample_clamp(src, w, h, (int)x - 1, (int)y);
            rgba_px e = sample_clamp(src, w, h, (int)x, (int)y);
            rgba_px f = sample_clamp(src, w, h, (int)x + 1, (int)y);
            rgba_px g = sample_clamp(src, w, h, (int)x - 1, (int)y + 1);
            rgba_px hpx = sample_clamp(src, w, h, (int)x, (int)y + 1);
            rgba_px i = sample_clamp(src, w, h, (int)x + 1, (int)y + 1);
            rgba_px out[9];
            unsigned k;
            for (k = 0; k < 9u; k++) out[k] = e;
            if (!px_eq_exact(b, hpx) && !px_eq_exact(d, f)) {
                if (px_eq_exact(d, b)) out[0] = d;
                if ((px_eq_exact(d, b) && !px_eq_exact(e, c)) ||
                    (px_eq_exact(b, f) && !px_eq_exact(e, a)))
                    out[1] = b;
                if (px_eq_exact(b, f)) out[2] = f;
                if ((px_eq_exact(d, b) && !px_eq_exact(e, g)) ||
                    (px_eq_exact(d, hpx) && !px_eq_exact(e, a)))
                    out[3] = d;
                if ((px_eq_exact(b, f) && !px_eq_exact(e, i)) ||
                    (px_eq_exact(hpx, f) && !px_eq_exact(e, c)))
                    out[5] = f;
                if (px_eq_exact(d, hpx)) out[6] = d;
                if ((px_eq_exact(d, hpx) && !px_eq_exact(e, i)) ||
                    (px_eq_exact(hpx, f) && !px_eq_exact(e, g)))
                    out[7] = hpx;
                if (px_eq_exact(hpx, f)) out[8] = f;
            }
            for (k = 0; k < 9u; k++)
                store_px(dst, dst_w, x * 3u + (k % 3u), y * 3u + (k / 3u), out[k]);
        }
    }
}

typedef struct {
    int edr, edr_left, edr_up;
    rgba_px px;
} xbr_corner_decision;

/*
 * Direct (unclamped) sample: the interior fast path reads with pointer/stride
 * arithmetic only. For x in [2..w-3], y in [2..h-3] every xBR offset stays within
 * ±2 of (x,y), so sample_clamp would never clamp there — this returns the exact
 * same bytes with none of the per-sample bound checks.
 */
static rgba_px sample_direct(const uint8_t *src, unsigned w, int x, int y) {
    rgba_px p;
    const uint8_t *q = src + ((size_t)(unsigned)y * w + (size_t)(unsigned)x) * 4u;
    p.r = q[0];
    p.g = q[1];
    p.b = q[2];
    p.a = q[3];
    return p;
}

/*
 * Shared decision core — the only edge logic. Both the clamped border fetch and
 * the direct interior fetch feed identical pixels here, so their outputs are
 * byte-identical by construction.
 */
static xbr_corner_decision xbr_corner_decide(rgba_px pe, rgba_px pf, rgba_px ph,
                                             rgba_px pd, rgba_px pb, rgba_px pi,
                                             rgba_px pc, rgba_px pg, rgba_px pf4,
                                             rgba_px pi4, rgba_px ph5, rgba_px pi5) {
    xbr_corner_decision d;
    unsigned e, i, ke, ki;
    int lv1;

    d.edr = 0;
    d.edr_left = 0;
    d.edr_up = 0;
    d.px = pe;

    e = px_dist(pe, pc) + px_dist(pe, pg) + px_dist(pi, ph5) + px_dist(pi, pf4) +
        4u * px_dist(pf, ph);
    i = px_dist(ph, pd) + px_dist(ph, pi5) + px_dist(pf, pi4) + px_dist(pf, pb) +
        4u * px_dist(pe, pi);
    if (e >= i) return d;

    lv1 = ((!px_eq_y(pf, pb) && !px_eq_y(pf, pc)) ||
           (!px_eq_y(ph, pd) && !px_eq_y(ph, pg)) ||
           (px_eq_y(pe, pi) &&
            ((!px_eq_y(pf, pf4) && !px_eq_y(pf, pi4)) ||
             (!px_eq_y(ph, ph5) && !px_eq_y(ph, pi5)))) ||
           px_eq_y(pe, pg) || px_eq_y(pe, pc));
    if (!lv1) return d;

    ke = px_dist(pf, pg);
    ki = px_dist(ph, pc);
    d.edr = 1;
    d.edr_left = (2u * ke <= ki) && (!px_eq_y(pe, pg) && !px_eq_y(pd, pg));
    d.edr_up   = (2u * ki <= ke) && (!px_eq_y(pe, pc) && !px_eq_y(pb, pc));
    d.px = (px_dist(pe, pf) <= px_dist(pe, ph)) ? pf : ph;
    return d;
}

/* Rotated Hyllian xBR edge detector across the 4 corners (BR, BL, TL, TR). */
static xbr_corner_decision eval_xbr_corner(const uint8_t *src, unsigned w, unsigned h,
                                           int x, int y,
                                           int dfx, int dfy, int dhx, int dhy) {
    rgba_px pe  = sample_clamp(src, w, h, x, y);
    rgba_px pf  = sample_clamp(src, w, h, x + dfx, y + dfy);
    rgba_px ph  = sample_clamp(src, w, h, x + dhx, y + dhy);
    rgba_px pd  = sample_clamp(src, w, h, x - dfx, y - dfy);
    rgba_px pb  = sample_clamp(src, w, h, x - dhx, y - dhy);
    rgba_px pi  = sample_clamp(src, w, h, x + dfx + dhx, y + dfy + dhy);
    rgba_px pc  = sample_clamp(src, w, h, x + dfx - dhx, y + dfy - dhy);
    rgba_px pg  = sample_clamp(src, w, h, x - dfx + dhx, y - dfy + dhy);
    rgba_px pf4 = sample_clamp(src, w, h, x + 2 * dfx, y + 2 * dfy);
    rgba_px pi4 = sample_clamp(src, w, h, x + 2 * dfx + dhx, y + 2 * dfy + dhy);
    rgba_px ph5 = sample_clamp(src, w, h, x + 2 * dhx, y + 2 * dhy);
    rgba_px pi5 = sample_clamp(src, w, h, x + dfx + 2 * dhx, y + dfy + 2 * dhy);
    return xbr_corner_decide(pe, pf, ph, pd, pb, pi, pc, pg, pf4, pi4, ph5, pi5);
}

/* Interior fast path: identical neighbourhood via direct reads (no clamping). */
static xbr_corner_decision eval_xbr_corner_interior(const uint8_t *src, unsigned w,
                                                    int x, int y,
                                                    int dfx, int dfy,
                                                    int dhx, int dhy) {
    rgba_px pe  = sample_direct(src, w, x, y);
    rgba_px pf  = sample_direct(src, w, x + dfx, y + dfy);
    rgba_px ph  = sample_direct(src, w, x + dhx, y + dhy);
    rgba_px pd  = sample_direct(src, w, x - dfx, y - dfy);
    rgba_px pb  = sample_direct(src, w, x - dhx, y - dhy);
    rgba_px pi  = sample_direct(src, w, x + dfx + dhx, y + dfy + dhy);
    rgba_px pc  = sample_direct(src, w, x + dfx - dhx, y + dfy - dhy);
    rgba_px pg  = sample_direct(src, w, x - dfx + dhx, y - dfy + dhy);
    rgba_px pf4 = sample_direct(src, w, x + 2 * dfx, y + 2 * dfy);
    rgba_px pi4 = sample_direct(src, w, x + 2 * dfx + dhx, y + 2 * dfy + dhy);
    rgba_px ph5 = sample_direct(src, w, x + 2 * dhx, y + 2 * dhy);
    rgba_px pi5 = sample_direct(src, w, x + dfx + 2 * dhx, y + dfy + 2 * dhy);
    return xbr_corner_decide(pe, pf, ph, pd, pb, pi, pc, pg, pf4, pi4, ph5, pi5);
}

/* Interior test: all xBR offsets stay within ±2, so no border clamp applies. */
static int xbr_interior(unsigned x, unsigned y, unsigned w, unsigned h) {
    return x >= 2u && x + 2u < w && y >= 2u && y + 2u < h;
}

/* Exact 4-byte cardinal match: since xbr_corner_decide picks dec.px strictly
 * from {pf, ph} (the 4 cardinal neighbours across all rotations), when all 4
 * cardinals are byte-identical to pe (and alpha>0 or RGB==0), every rotation
 * blends pe with pe = pe, leaving the 2x2 / 3x3 block identically pe. */
static int xbr_flat_cardinals(const uint8_t *src, unsigned w, int x, int y,
                              rgba_px pe) {
    rgba_px pb, pd, pf, ph;
    if (pe.a == 0 && (pe.r != 0 || pe.g != 0 || pe.b != 0)) return 0;
    pb = sample_direct(src, w, x, y - 1);
    if (pb.r != pe.r || pb.g != pe.g || pb.b != pe.b || pb.a != pe.a) return 0;
    pd = sample_direct(src, w, x - 1, y);
    if (pd.r != pe.r || pd.g != pe.g || pd.b != pe.b || pd.a != pe.a) return 0;
    pf = sample_direct(src, w, x + 1, y);
    if (pf.r != pe.r || pf.g != pe.g || pf.b != pe.b || pf.a != pe.a) return 0;
    ph = sample_direct(src, w, x, y + 1);
    return ph.r == pe.r && ph.g == pe.g && ph.b == pe.b && ph.a == pe.a;
}

static void upscale_xbr2x(const uint8_t *src, unsigned w, unsigned h, uint8_t *dst,
                          int allow_fast) {
    /* Corner axes rotated 90 deg CCW cleanly: BR, TR, TL, BL -> subpixel indices */
    static const int k_rot[4][4] = {
        { 1,  0,  0,  1}, /* BR -> sub(1,1) */
        { 0, -1,  1,  0}, /* TR -> sub(1,0) */
        {-1,  0,  0, -1}, /* TL -> sub(0,0) */
        { 0,  1, -1,  0}  /* BL -> sub(0,1) */
    };
    unsigned x, y, r, dst_w = w * 2u;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int inner = allow_fast && xbr_interior(x, y, w, h);
            rgba_px pe = inner ? sample_direct(src, w, (int)x, (int)y)
                               : sample_clamp(src, w, h, (int)x, (int)y);
            rgba_px sub[2][2];
            sub[0][0] = sub[0][1] = sub[1][0] = sub[1][1] = pe;
            if (!(inner && xbr_flat_cardinals(src, w, (int)x, (int)y, pe))) {
                for (r = 0; r < 4u; r++) {
                    int dfx = k_rot[r][0], dfy = k_rot[r][1];
                    int dhx = k_rot[r][2], dhy = k_rot[r][3];
                    xbr_corner_decision dec = inner
                        ? eval_xbr_corner_interior(src, w, (int)x, (int)y,
                                                   dfx, dfy, dhx, dhy)
                        : eval_xbr_corner(src, w, h, (int)x, (int)y,
                                          dfx, dfy, dhx, dhy);
                    int cx, cy, lhx, lhy, ufx, ufy;
                    if (!dec.edr) continue;
                    cx = (1 + dfx + dhx) >> 1;
                    cy = (1 + dfy + dhy) >> 1;
                    lhx = (1 - dfx + dhx) >> 1;
                    lhy = (1 - dfy + dhy) >> 1;
                    ufx = (1 + dfx - dhx) >> 1;
                    ufy = (1 + dfy - dhy) >> 1;
                    if (dec.edr_left && dec.edr_up) {
                        sub[cy][cx] = px_blend(pe, dec.px, 7u, 8u);
                        sub[lhy][lhx] = px_blend(sub[lhy][lhx], dec.px, 1u, 4u);
                        sub[ufy][ufx] = px_blend(sub[ufy][ufx], dec.px, 1u, 4u);
                    } else if (dec.edr_left) {
                        sub[cy][cx] = px_blend(pe, dec.px, 3u, 4u);
                        sub[lhy][lhx] = px_blend(sub[lhy][lhx], dec.px, 1u, 4u);
                    } else if (dec.edr_up) {
                        sub[cy][cx] = px_blend(pe, dec.px, 3u, 4u);
                        sub[ufy][ufx] = px_blend(sub[ufy][ufx], dec.px, 1u, 4u);
                    } else {
                        sub[cy][cx] = px_blend(pe, dec.px, 1u, 2u);
                    }
                }
            }
            store_px(dst, dst_w, x * 2u + 0u, y * 2u + 0u, sub[0][0]);
            store_px(dst, dst_w, x * 2u + 1u, y * 2u + 0u, sub[0][1]);
            store_px(dst, dst_w, x * 2u + 0u, y * 2u + 1u, sub[1][0]);
            store_px(dst, dst_w, x * 2u + 1u, y * 2u + 1u, sub[1][1]);
        }
    }
}

static void upscale_xbr3x(const uint8_t *src, unsigned w, unsigned h, uint8_t *dst,
                          int allow_fast) {
    static const int k_rot[4][4] = {
        { 1,  0,  0,  1}, /* BR */
        { 0, -1,  1,  0}, /* TR */
        {-1,  0,  0, -1}, /* TL */
        { 0,  1, -1,  0}  /* BL */
    };
    unsigned x, y, r, dst_w = w * 3u;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int inner = allow_fast && xbr_interior(x, y, w, h);
            rgba_px pe = inner ? sample_direct(src, w, (int)x, (int)y)
                               : sample_clamp(src, w, h, (int)x, (int)y);
            rgba_px sub[3][3];
            unsigned sy, sx;
            for (sy = 0; sy < 3u; sy++)
                for (sx = 0; sx < 3u; sx++)
                    sub[sy][sx] = pe;
            if (!(inner && xbr_flat_cardinals(src, w, (int)x, (int)y, pe))) {
                for (r = 0; r < 4u; r++) {
                    int dfx = k_rot[r][0], dfy = k_rot[r][1];
                    int dhx = k_rot[r][2], dhy = k_rot[r][3];
                    xbr_corner_decision dec = inner
                        ? eval_xbr_corner_interior(src, w, (int)x, (int)y,
                                                   dfx, dfy, dhx, dhy)
                        : eval_xbr_corner(src, w, h, (int)x, (int)y,
                                          dfx, dfy, dhx, dhy);
                    int cx, cy, rx, ry, dx, dy, lhx, lhy, ufx, ufy;
                    if (!dec.edr) continue;
                    cx = 1 + dfx + dhx; cy = 1 + dfy + dhy;
                    rx = 1 + dfx;       ry = 1 + dfy;
                    dx = 1 + dhx;       dy = 1 + dhy;
                    lhx = 1 - dfx + dhx; lhy = 1 - dfy + dhy;
                    ufx = 1 + dfx - dhx; ufy = 1 + dfy - dhy;
                    if (dec.edr_left && dec.edr_up) {
                        sub[cy][cx] = dec.px;
                        sub[ry][rx] = px_blend(sub[ry][rx], dec.px, 3u, 4u);
                        sub[dy][dx] = px_blend(sub[dy][dx], dec.px, 3u, 4u);
                        sub[lhy][lhx] = px_blend(sub[lhy][lhx], dec.px, 1u, 4u);
                        sub[ufy][ufx] = px_blend(sub[ufy][ufx], dec.px, 1u, 4u);
                    } else if (dec.edr_left) {
                        sub[cy][cx] = dec.px;
                        sub[dy][dx] = px_blend(sub[dy][dx], dec.px, 3u, 4u);
                        sub[lhy][lhx] = px_blend(sub[lhy][lhx], dec.px, 1u, 4u);
                        sub[ry][rx] = px_blend(sub[ry][rx], dec.px, 1u, 4u);
                    } else if (dec.edr_up) {
                        sub[cy][cx] = dec.px;
                        sub[ry][rx] = px_blend(sub[ry][rx], dec.px, 3u, 4u);
                        sub[ufy][ufx] = px_blend(sub[ufy][ufx], dec.px, 1u, 4u);
                        sub[dy][dx] = px_blend(sub[dy][dx], dec.px, 1u, 4u);
                    } else {
                        sub[cy][cx] = px_blend(pe, dec.px, 3u, 4u);
                        sub[ry][rx] = px_blend(sub[ry][rx], dec.px, 1u, 4u);
                        sub[dy][dx] = px_blend(sub[dy][dx], dec.px, 1u, 4u);
                    }
                }
            }
            for (sy = 0; sy < 3u; sy++)
                for (sx = 0; sx < 3u; sx++)
                    store_px(dst, dst_w, x * 3u + sx, y * 3u + sy, sub[sy][sx]);
        }
    }
}

/*
 * Test-only reference: runs the xBR scaler with the interior fast path disabled,
 * so every pixel takes the scalar (clamped) path. The equivalence test prototypes
 * this directly (it is intentionally absent from upscale.h) to prove the fast path
 * is byte-identical to the all-scalar result. factor must be 2 or 3.
 */
void mb_upscale_xbr_scalar_ref(const uint8_t *src, unsigned w, unsigned h,
                               unsigned factor, uint8_t *dst) {
    if (factor == 2u)
        upscale_xbr2x(src, w, h, dst, 0);
    else
        upscale_xbr3x(src, w, h, dst, 0);
}

int mb_upscale_rgba(const uint8_t *src, unsigned w, unsigned h,
                    mb_upscale_mode mode, unsigned scale_override,
                    uint8_t *dst, size_t dst_size) {
    unsigned factor;
    size_t need;
    if (!src || !dst || w == 0 || h == 0) return -1;
    if ((mode == MB_UPSCALE_SCALE2X || mode == MB_UPSCALE_XBR2X) &&
        scale_override == 4u) {
        size_t mid_w = (size_t)w * 2u, mid_h = (size_t)h * 2u;
        size_t mid_sz = mid_w * mid_h * 4u;
        uint8_t *mid;
        need = (size_t)w * 4u * (size_t)h * 4u * 4u;
        if (dst_size < need) return -1;
        mid = (uint8_t *)malloc(mid_sz);
        if (!mid) return -1;
        if (mode == MB_UPSCALE_SCALE2X) {
            upscale_scale2x(src, w, h, mid);
            upscale_scale2x(mid, (unsigned)mid_w, (unsigned)mid_h, dst);
        } else {
            upscale_xbr2x(src, w, h, mid, 1);
            upscale_xbr2x(mid, (unsigned)mid_w, (unsigned)mid_h, dst, 1);
        }
        free(mid);
        return 0;
    }
    factor = mb_upscale_mode_factor(mode);
    if (mode == MB_UPSCALE_NEAREST) {
        factor = (scale_override >= 1u && scale_override <= 6u) ? scale_override : 3u;
    }
    if ((size_t)w > 8192u || (size_t)h > 8192u) return -1;
    need = (size_t)w * factor * (size_t)h * factor * 4u;
    if (dst_size < need) return -1;
    switch (mode) {
    case MB_UPSCALE_NEAREST:
        upscale_nearest(src, w, h, factor, dst);
        return 0;
    case MB_UPSCALE_SCALE2X:
        upscale_scale2x(src, w, h, dst);
        return 0;
    case MB_UPSCALE_SCALE3X:
        upscale_scale3x(src, w, h, dst);
        return 0;
    case MB_UPSCALE_XBR2X:
        upscale_xbr2x(src, w, h, dst, 1);
        return 0;
    case MB_UPSCALE_XBR3X:
        upscale_xbr3x(src, w, h, dst, 1);
        return 0;
    default:
        return -1;
    }
}

int mb_upscale_blit_alpha(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                          const uint8_t *src, unsigned src_w, unsigned src_h,
                          int dst_x, int dst_y, unsigned out_w, unsigned out_h,
                          int flip_x) {
    unsigned oy, ox;
    if (!dst || !src || !dst_w || !dst_h || !src_w || !src_h || !out_w || !out_h)
        return -1;
    if (src_w == out_w && src_h == out_h) {
        for (oy = 0; oy < out_h; oy++) {
            int dy = dst_y + (int)oy;
            unsigned sy = oy;
            if (dy < 0 || dy >= (int)dst_h) continue;
            for (ox = 0; ox < out_w; ox++) {
                int dx = dst_x + (int)ox;
                unsigned sx = flip_x ? (src_w - 1u - ox) : ox;
                const uint8_t *sp;
                uint8_t *dp;
                unsigned a;
                if (dx < 0 || dx >= (int)dst_w) continue;
                sp = src + ((size_t)sy * src_w + sx) * 4u;
                a = sp[3];
                if (a == 0u) continue;
                dp = dst + ((size_t)(unsigned)dy * dst_w + (unsigned)dx) * 4u;
                if (a == 255u) {
                    dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = 255u;
                } else {
                    unsigned inv = 255u - a;
                    dp[0] = (uint8_t)((sp[0] * a + dp[0] * inv + 127u) / 255u);
                    dp[1] = (uint8_t)((sp[1] * a + dp[1] * inv + 127u) / 255u);
                    dp[2] = (uint8_t)((sp[2] * a + dp[2] * inv + 127u) / 255u);
                    dp[3] = 255u;
                }
            }
        }
        return 0;
    }

    for (oy = 0; oy < out_h; oy++) {
        int dy = dst_y + (int)oy;
        uint32_t fy_fp, sy0, sy1, wy0, wy1;
        if (dy < 0 || dy >= (int)dst_h) continue;
        fy_fp = (uint32_t)(((uint64_t)(2u * oy + 1u) * src_h * 256u) / (2u * out_h));
        if (fy_fp >= 128u) fy_fp -= 128u;
        else fy_fp = 0u;
        sy0 = fy_fp >> 8;
        if (sy0 >= src_h) sy0 = src_h - 1u;
        sy1 = (sy0 + 1u < src_h) ? (sy0 + 1u) : sy0;
        wy1 = fy_fp & 255u;
        wy0 = 256u - wy1;
        for (ox = 0; ox < out_w; ox++) {
            int dx = dst_x + (int)ox;
            uint32_t fx_fp, sx0, sx1, wx0, wx1;
            const uint8_t *p00, *p10, *p01, *p11;
            uint32_t w00, w10, w01, w11;
            uint32_t a00, a10, a01, a11, alpha_sum, r_sum, g_sum, b_sum;
            uint8_t *dp;
            if (dx < 0 || dx >= (int)dst_w) continue;
            fx_fp = (uint32_t)(((uint64_t)(2u * ox + 1u) * src_w * 256u) / (2u * out_w));
            if (fx_fp >= 128u) fx_fp -= 128u;
            else fx_fp = 0u;
            sx0 = fx_fp >> 8;
            if (sx0 >= src_w) sx0 = src_w - 1u;
            sx1 = (sx0 + 1u < src_w) ? (sx0 + 1u) : sx0;
            wx1 = fx_fp & 255u;
            wx0 = 256u - wx1;
            if (flip_x) {
                sx0 = src_w - 1u - sx0;
                sx1 = src_w - 1u - sx1;
            }
            p00 = src + ((size_t)sy0 * src_w + sx0) * 4u;
            p10 = src + ((size_t)sy0 * src_w + sx1) * 4u;
            p01 = src + ((size_t)sy1 * src_w + sx0) * 4u;
            p11 = src + ((size_t)sy1 * src_w + sx1) * 4u;
            w00 = wx0 * wy0; w10 = wx1 * wy0;
            w01 = wx0 * wy1; w11 = wx1 * wy1;
            a00 = (uint32_t)p00[3] * w00; a10 = (uint32_t)p10[3] * w10;
            a01 = (uint32_t)p01[3] * w01; a11 = (uint32_t)p11[3] * w11;
            alpha_sum = a00 + a10 + a01 + a11;
            if (alpha_sum < 16u * 65536u / 255u) continue;
            r_sum = (uint32_t)p00[0] * a00 + (uint32_t)p10[0] * a10 +
                    (uint32_t)p01[0] * a01 + (uint32_t)p11[0] * a11;
            g_sum = (uint32_t)p00[1] * a00 + (uint32_t)p10[1] * a10 +
                    (uint32_t)p01[1] * a01 + (uint32_t)p11[1] * a11;
            b_sum = (uint32_t)p00[2] * a00 + (uint32_t)p10[2] * a10 +
                    (uint32_t)p01[2] * a01 + (uint32_t)p11[2] * a11;
            {
                unsigned sr = (unsigned)(r_sum / alpha_sum);
                unsigned sg = (unsigned)(g_sum / alpha_sum);
                unsigned sb = (unsigned)(b_sum / alpha_sum);
                unsigned sa = (unsigned)(alpha_sum >> 16);
                if (sa > 255u) sa = 255u;
                dp = dst + ((size_t)(unsigned)dy * dst_w + (unsigned)dx) * 4u;
                if (sa == 255u) {
                    dp[0] = (uint8_t)sr; dp[1] = (uint8_t)sg; dp[2] = (uint8_t)sb;
                } else {
                    unsigned inv = 255u - sa;
                    dp[0] = (uint8_t)((sr * sa + dp[0] * inv + 127u) / 255u);
                    dp[1] = (uint8_t)((sg * sa + dp[1] * inv + 127u) / 255u);
                    dp[2] = (uint8_t)((sb * sa + dp[2] * inv + 127u) / 255u);
                }
                dp[3] = 255u;
            }
        }
    }
    return 0;
}

/* ---------- Pure-C Image Loader (PNG + PAM P7 + PPM P6 + TGA) ---------- */

static int read_entire_file(const char *path, uint8_t **out, size_t *out_sz) {
    FILE *fp;
    long len;
    uint8_t *buf;
    if (!path || !out || !out_sz) return -1;
    fp = fopen(path, "rb");
    if (!fp) return -1;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
    len = ftell(fp);
    if (len <= 0 || len > 64 * 1024 * 1024) { fclose(fp); return -1; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return -1; }
    buf = (uint8_t *)malloc((size_t)len);
    if (!buf) { fclose(fp); return -1; }
    if (fread(buf, 1, (size_t)len, fp) != (size_t)len) {
        free(buf);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    *out = buf;
    *out_sz = (size_t)len;
    return 0;
}

static uint32_t rd_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

typedef struct {
    const uint8_t *in;
    size_t in_sz;
    size_t pos;
    uint32_t bitbuf;
    unsigned bitcnt;
} mb_bitstream;

static int bs_read_bit(mb_bitstream *bs) {
    int b;
    if (bs->bitcnt == 0u) {
        if (bs->pos >= bs->in_sz) return -1;
        bs->bitbuf = bs->in[bs->pos++];
        bs->bitcnt = 8u;
    }
    b = (int)(bs->bitbuf & 1u);
    bs->bitbuf >>= 1;
    bs->bitcnt--;
    return b;
}

static int bs_read_bits(mb_bitstream *bs, unsigned n) {
    unsigned i;
    int val = 0;
    for (i = 0; i < n; i++) {
        int b = bs_read_bit(bs);
        if (b < 0) return -1;
        val |= (b << i);
    }
    return val;
}

#define MB_HUFF_MAX_SYMS 320u
typedef struct {
    uint16_t counts[16];
    uint16_t syms[MB_HUFF_MAX_SYMS];
} mb_huff;

static int huff_build(mb_huff *h, const uint8_t *lengths, unsigned n_syms) {
    unsigned i;
    uint16_t offs[16];
    int left = 1;
    if (n_syms > MB_HUFF_MAX_SYMS) return -1;
    memset(h, 0, sizeof *h);
    for (i = 0; i < n_syms; i++) {
        if (lengths[i] > 15u) return -1;
        h->counts[lengths[i]]++;
    }
    h->counts[0] = 0;
    for (i = 1; i <= 15u; i++) {
        left = (left << 1) - (int)h->counts[i];
        if (left < 0) return -1;
    }
    offs[1] = 0;
    for (i = 1; i < 15u; i++)
        offs[i + 1] = (uint16_t)(offs[i] + h->counts[i]);
    for (i = 0; i < n_syms; i++) {
        if (lengths[i] != 0u)
            h->syms[offs[lengths[i]]++] = (uint16_t)i;
    }
    return 0;
}

static int huff_decode(mb_bitstream *bs, const mb_huff *h) {
    unsigned len;
    int code = 0, first = 0, index = 0;
    for (len = 1u; len <= 15u; len++) {
        int bit = bs_read_bit(bs);
        int count;
        if (bit < 0) return -1;
        code |= bit;
        count = (int)h->counts[len];
        if (code - count < first)
            return (int)h->syms[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static int zlib_inflate(const uint8_t *z, size_t z_sz,
                        uint8_t *out, size_t out_cap, size_t *out_len) {
    static const uint16_t k_len_base[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
        35,43,51,59,67,83,99,115,131,163,195,227,258
    };
    static const uint8_t k_len_extra[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
        3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    static const uint16_t k_dist_base[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
        257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    static const uint8_t k_dist_extra[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
        7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };
    static const uint8_t k_cl_order[19] = {
        16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
    };
    mb_bitstream bs;
    size_t opos = 0;
    int bfinal = 0;
    if (!z || z_sz < 6u) return -1;
    if ((z[0] & 0x0Fu) != 8u) return -1;
    if ((((unsigned)z[0] << 8) | z[1]) % 31u != 0u) return -1;
    if (z[1] & 0x20u) return -1;
    memset(&bs, 0, sizeof bs);
    bs.in = z + 2u;
    bs.in_sz = z_sz - 2u;
    while (!bfinal) {
        int btype;
        mb_huff lit_h, dist_h;
        bfinal = bs_read_bit(&bs);
        btype = bs_read_bits(&bs, 2u);
        if (bfinal < 0 || btype < 0) return -1;
        if (btype == 0) {
            uint16_t len, nlen;
            bs.bitbuf = 0; bs.bitcnt = 0;
            if (bs.pos + 4u > bs.in_sz) return -1;
            len = (uint16_t)(bs.in[bs.pos] | ((uint16_t)bs.in[bs.pos + 1] << 8));
            nlen = (uint16_t)(bs.in[bs.pos + 2] | ((uint16_t)bs.in[bs.pos + 3] << 8));
            bs.pos += 4u;
            if ((uint16_t)(len ^ nlen) != 0xFFFFu) return -1;
            if (bs.pos + len > bs.in_sz || opos + len > out_cap) return -1;
            memcpy(out + opos, bs.in + bs.pos, len);
            bs.pos += len;
            opos += len;
            continue;
        }
        if (btype == 1) {
            uint8_t llens[288], dlens[32];
            unsigned i;
            for (i = 0; i < 144u; i++) llens[i] = 8u;
            for (; i < 256u; i++) llens[i] = 9u;
            for (; i < 280u; i++) llens[i] = 7u;
            for (; i < 288u; i++) llens[i] = 8u;
            for (i = 0; i < 32u; i++) dlens[i] = 5u;
            if (huff_build(&lit_h, llens, 288u) || huff_build(&dist_h, dlens, 32u))
                return -1;
        } else if (btype == 2) {
            int hlit = bs_read_bits(&bs, 5u);
            int hdist = bs_read_bits(&bs, 5u);
            int hclen = bs_read_bits(&bs, 4u);
            uint8_t clens[19], lengths[320];
            mb_huff cl_h;
            unsigned total, idx = 0;
            int k;
            if (hlit < 0 || hdist < 0 || hclen < 0) return -1;
            hlit += 257; hdist += 1; hclen += 4;
            total = (unsigned)(hlit + hdist);
            if (total > sizeof lengths) return -1;
            memset(clens, 0, sizeof clens);
            for (k = 0; k < hclen; k++) {
                int v = bs_read_bits(&bs, 3u);
                if (v < 0) return -1;
                clens[k_cl_order[k]] = (uint8_t)v;
            }
            if (huff_build(&cl_h, clens, 19u)) return -1;
            while (idx < total) {
                int sym = huff_decode(&bs, &cl_h);
                unsigned rep = 1u;
                uint8_t val = 0u;
                if (sym < 0) return -1;
                if (sym < 16) {
                    lengths[idx++] = (uint8_t)sym;
                    continue;
                }
                if (sym == 16) {
                    int ex = bs_read_bits(&bs, 2u);
                    if (ex < 0 || idx == 0u) return -1;
                    val = lengths[idx - 1u];
                    rep = 3u + (unsigned)ex;
                } else if (sym == 17) {
                    int ex = bs_read_bits(&bs, 3u);
                    if (ex < 0) return -1;
                    rep = 3u + (unsigned)ex;
                } else {
                    int ex = bs_read_bits(&bs, 7u);
                    if (ex < 0) return -1;
                    rep = 11u + (unsigned)ex;
                }
                if (idx + rep > total) return -1;
                while (rep--) lengths[idx++] = val;
            }
            if (huff_build(&lit_h, lengths, (unsigned)hlit) ||
                huff_build(&dist_h, lengths + hlit, (unsigned)hdist))
                return -1;
        } else {
            return -1;
        }

        for (;;) {
            int sym = huff_decode(&bs, &lit_h);
            if (sym < 0) return -1;
            if (sym < 256) {
                if (opos >= out_cap) return -1;
                out[opos++] = (uint8_t)sym;
            } else if (sym == 256) {
                break;
            } else {
                int li = sym - 257, lex, dsym, dex;
                unsigned length, dist, k;
                if (li < 0 || li >= 29) return -1;
                lex = k_len_extra[li] ? bs_read_bits(&bs, k_len_extra[li]) : 0;
                if (lex < 0) return -1;
                length = k_len_base[li] + (unsigned)lex;
                dsym = huff_decode(&bs, &dist_h);
                if (dsym < 0 || dsym >= 30) return -1;
                dex = k_dist_extra[dsym] ? bs_read_bits(&bs, k_dist_extra[dsym]) : 0;
                if (dex < 0) return -1;
                dist = k_dist_base[dsym] + (unsigned)dex;
                if (dist == 0u || dist > opos || opos + length > out_cap) return -1;
                for (k = 0; k < length; k++)
                    out[opos + k] = out[opos + k - dist];
                opos += length;
            }
        }
    }
    if (out_len) *out_len = opos;
    return 0;
}

static uint8_t paeth_pred(int a, int b, int c) {
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return (uint8_t)a;
    if (pb <= pc) return (uint8_t)b;
    return (uint8_t)c;
}

static int decode_png_buf(const uint8_t *buf, size_t sz, uint8_t **out_rgba,
                          unsigned *out_w, unsigned *out_h) {
    static const uint8_t k_sig[8] = {137,80,78,71,13,10,26,10};
    size_t pos = 8u, idat_sz = 0, idat_cap = 0;
    uint8_t *idat = NULL, *raw = NULL, *rgba = NULL;
    uint8_t plte[256 * 3], trns[256];
    unsigned plte_n = 0, trns_n = 0;
    uint32_t w = 0, h = 0;
    uint8_t bit_depth = 0, color_type = 0;
    unsigned channels = 0, bpp = 1;
    size_t row_bytes, raw_sz, out_inflated = 0;
    uint32_t y;

    if (sz < 33u || memcmp(buf, k_sig, 8u) != 0) return -1;
    memset(plte, 0, sizeof plte);
    memset(trns, 255, sizeof trns);

    while (pos + 12u <= sz) {
        uint32_t clen = rd_be32(buf + pos);
        const uint8_t *tag = buf + pos + 4u;
        const uint8_t *cdata = buf + pos + 8u;
        if (pos + 12u + clen > sz) { free(idat); return -1; }
        if (memcmp(tag, "IHDR", 4u) == 0) {
            if (clen != 13u) { free(idat); return -1; }
            w = rd_be32(cdata);
            h = rd_be32(cdata + 4u);
            bit_depth = cdata[8];
            color_type = cdata[9];
            if (cdata[10] != 0 || cdata[11] != 0 || cdata[12] != 0) {
                free(idat); return -1;
            }
            if (w == 0 || h == 0 || w > 4096u || h > 4096u) {
                free(idat); return -1;
            }
            if (bit_depth != 8u && !(color_type == 3u && (bit_depth == 1u || bit_depth == 2u || bit_depth == 4u))) {
                free(idat); return -1;
            }
            channels = (color_type == 6u) ? 4u :
                       (color_type == 2u) ? 3u :
                       (color_type == 4u) ? 2u :
                       (color_type == 0u || color_type == 3u) ? 1u : 0u;
            if (channels == 0u) { free(idat); return -1; }
        } else if (memcmp(tag, "PLTE", 4u) == 0) {
            if (clen % 3u != 0u || clen > 768u) { free(idat); return -1; }
            plte_n = clen / 3u;
            memcpy(plte, cdata, clen);
        } else if (memcmp(tag, "tRNS", 4u) == 0) {
            trns_n = clen > 256u ? 256u : clen;
            memcpy(trns, cdata, trns_n);
        } else if (memcmp(tag, "IDAT", 4u) == 0) {
            if (idat_sz + clen > idat_cap) {
                size_t nc = idat_cap ? idat_cap * 2u : 65536u;
                uint8_t *np;
                while (nc < idat_sz + clen) nc *= 2u;
                np = (uint8_t *)realloc(idat, nc);
                if (!np) { free(idat); return -1; }
                idat = np;
                idat_cap = nc;
            }
            memcpy(idat + idat_sz, cdata, clen);
            idat_sz += clen;
        } else if (memcmp(tag, "IEND", 4u) == 0) {
            break;
        }
        pos += 12u + clen;
    }

    if (!w || !h || !idat || !idat_sz) { free(idat); return -1; }
    row_bytes = ((size_t)w * bit_depth * channels + 7u) / 8u;
    bpp = (bit_depth * channels + 7u) / 8u;
    if (bpp == 0u) bpp = 1u;
    raw_sz = (1u + row_bytes) * h;
    raw = (uint8_t *)malloc(raw_sz);
    rgba = (uint8_t *)malloc((size_t)w * h * 4u);
    if (!raw || !rgba || zlib_inflate(idat, idat_sz, raw, raw_sz, &out_inflated) ||
        out_inflated != raw_sz) {
        free(idat); free(raw); free(rgba); return -1;
    }
    free(idat);

    for (y = 0; y < h; y++) {
        uint8_t *row = raw + (size_t)y * (1u + row_bytes);
        uint8_t *cur = row + 1u;
        const uint8_t *prev = (y > 0) ? (cur - (1u + row_bytes)) : NULL;
        uint8_t filt = row[0];
        size_t x;
        for (x = 0; x < row_bytes; x++) {
            uint8_t a = (x >= bpp) ? cur[x - bpp] : 0u;
            uint8_t b = prev ? prev[x] : 0u;
            uint8_t c = (prev && x >= bpp) ? prev[x - bpp] : 0u;
            if (filt == 1u) cur[x] = (uint8_t)(cur[x] + a);
            else if (filt == 2u) cur[x] = (uint8_t)(cur[x] + b);
            else if (filt == 3u) cur[x] = (uint8_t)(cur[x] + ((unsigned)a + b) / 2u);
            else if (filt == 4u) cur[x] = (uint8_t)(cur[x] + paeth_pred(a, b, c));
            else if (filt != 0u) { free(raw); free(rgba); return -1; }
        }
        for (x = 0; x < w; x++) {
            uint8_t *dp = rgba + ((size_t)y * w + x) * 4u;
            if (color_type == 6u) {
                memcpy(dp, cur + 4u * x, 4u);
            } else if (color_type == 2u) {
                dp[0] = cur[3u * x + 0u];
                dp[1] = cur[3u * x + 1u];
                dp[2] = cur[3u * x + 2u];
                dp[3] = (trns_n >= 6u && dp[0] == trns[1] && dp[1] == trns[3] &&
                         dp[2] == trns[5]) ? 0u : 255u;
            } else if (color_type == 4u) {
                uint8_t g = cur[2u * x];
                dp[0] = g; dp[1] = g; dp[2] = g; dp[3] = cur[2u * x + 1u];
            } else if (color_type == 0u) {
                uint8_t g = cur[x];
                dp[0] = g; dp[1] = g; dp[2] = g;
                dp[3] = (trns_n >= 2u && g == trns[1]) ? 0u : 255u;
            } else if (color_type == 3u) {
                unsigned idx = 0u;
                if (bit_depth == 8u) idx = cur[x];
                else {
                    unsigned ppb = 8u / bit_depth;
                    unsigned shift = (ppb - 1u - (unsigned)(x % ppb)) * bit_depth;
                    idx = (cur[x / ppb] >> shift) & ((1u << bit_depth) - 1u);
                }
                if (idx >= plte_n) idx = 0u;
                dp[0] = plte[3u * idx + 0u];
                dp[1] = plte[3u * idx + 1u];
                dp[2] = plte[3u * idx + 2u];
                dp[3] = (idx < trns_n) ? trns[idx] : 255u;
            }
        }
    }
    free(raw);
    *out_rgba = rgba;
    *out_w = w;
    *out_h = h;
    return 0;
}

static int decode_pam_buf(const uint8_t *buf, size_t sz, uint8_t **out_rgba,
                          unsigned *out_w, unsigned *out_h) {
    unsigned w = 0, h = 0, depth = 0, maxval = 0;
    size_t pos = 0, need, i;
    uint8_t *rgba;
    if (sz < 16u || memcmp(buf, "P7\n", 3u) != 0) return -1;
    pos = 3u;
    while (pos < sz) {
        size_t eol = pos;
        char line[128];
        size_t llen;
        while (eol < sz && buf[eol] != '\n') eol++;
        llen = eol - pos;
        if (llen >= sizeof line) return -1;
        memcpy(line, buf + pos, llen);
        line[llen] = 0;
        pos = (eol < sz) ? (eol + 1u) : eol;
        if (strcmp(line, "ENDHDR") == 0) break;
        (void)sscanf(line, "WIDTH %u", &w);
        (void)sscanf(line, "HEIGHT %u", &h);
        (void)sscanf(line, "DEPTH %u", &depth);
        (void)sscanf(line, "MAXVAL %u", &maxval);
    }
    if (!w || !h || w > 4096u || h > 4096u || maxval != 255u ||
        (depth != 3u && depth != 4u))
        return -1;
    need = (size_t)w * h * depth;
    if (pos + need > sz) return -1;
    rgba = (uint8_t *)malloc((size_t)w * h * 4u);
    if (!rgba) return -1;
    if (depth == 4u) {
        memcpy(rgba, buf + pos, need);
    } else {
        for (i = 0; i < (size_t)w * h; i++) {
            rgba[4u * i + 0u] = buf[pos + 3u * i + 0u];
            rgba[4u * i + 1u] = buf[pos + 3u * i + 1u];
            rgba[4u * i + 2u] = buf[pos + 3u * i + 2u];
            rgba[4u * i + 3u] = 255u;
        }
    }
    *out_rgba = rgba;
    *out_w = w;
    *out_h = h;
    return 0;
}

static int decode_tga_buf(const uint8_t *buf, size_t sz, uint8_t **out_rgba,
                          unsigned *out_w, unsigned *out_h) {
    unsigned id_len, img_type, w, h, bpp, top_down;
    size_t off, need, y, x;
    uint8_t *rgba;
    if (sz < 18u) return -1;
    id_len = buf[0];
    img_type = buf[2];
    w = (unsigned)buf[12] | ((unsigned)buf[13] << 8);
    h = (unsigned)buf[14] | ((unsigned)buf[15] << 8);
    bpp = buf[16];
    top_down = (buf[17] & 0x20u) != 0u;
    if (buf[1] != 0 || img_type != 2u || !w || !h || w > 4096u || h > 4096u ||
        (bpp != 24u && bpp != 32u))
        return -1;
    off = 18u + id_len;
    need = (size_t)w * h * (bpp / 8u);
    if (off + need > sz) return -1;
    rgba = (uint8_t *)malloc((size_t)w * h * 4u);
    if (!rgba) return -1;
    for (y = 0; y < h; y++) {
        size_t sy = top_down ? y : (h - 1u - y);
        const uint8_t *sr = buf + off + sy * w * (bpp / 8u);
        uint8_t *dr = rgba + y * w * 4u;
        for (x = 0; x < w; x++) {
            dr[4u * x + 0u] = sr[(bpp / 8u) * x + 2u];
            dr[4u * x + 1u] = sr[(bpp / 8u) * x + 1u];
            dr[4u * x + 2u] = sr[(bpp / 8u) * x + 0u];
            dr[4u * x + 3u] = (bpp == 32u) ? sr[4u * x + 3u] : 255u;
        }
    }
    *out_rgba = rgba;
    *out_w = w;
    *out_h = h;
    return 0;
}

int mb_image_load_rgba(const char *path, uint8_t **out_rgba,
                       unsigned *out_w, unsigned *out_h) {
    uint8_t *buf = NULL;
    size_t sz = 0;
    int rc = -1;
    if (!path || !out_rgba || !out_w || !out_h) return -1;
    *out_rgba = NULL;
    *out_w = 0;
    *out_h = 0;
    if (read_entire_file(path, &buf, &sz) != 0) return -1;
    if (sz >= 8u && buf[0] == 137u && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G')
        rc = decode_png_buf(buf, sz, out_rgba, out_w, out_h);
    else if (sz >= 3u && memcmp(buf, "P7\n", 3u) == 0)
        rc = decode_pam_buf(buf, sz, out_rgba, out_w, out_h);
    else
        rc = decode_tga_buf(buf, sz, out_rgba, out_w, out_h);
    free(buf);
    return rc;
}
