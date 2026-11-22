#include "core/cdimage.h"
#include "core/stage_enemy.h"
#include "core/upscale.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check_solid_block(mb_upscale_mode mode, unsigned scale_arg,
                             unsigned expected_factor) {
    uint8_t src[4 * 4 * 4];
    uint8_t *dst;
    size_t i, dw = 4u * expected_factor, dh = 4u * expected_factor;
    size_t sz = dw * dh * 4u;
    for (i = 0; i < 16u; i++) {
        src[4u * i + 0u] = 210u;
        src[4u * i + 1u] = 190u;
        src[4u * i + 2u] = 150u;
        src[4u * i + 3u] = 255u;
    }
    dst = (uint8_t *)malloc(sz);
    if (!dst) return 1;
    if (mb_upscale_rgba(src, 4u, 4u, mode, scale_arg, dst, sz) != 0) {
        free(dst);
        return 1;
    }
    for (i = 0; i < dw * dh; i++) {
        if (dst[4u * i + 0u] != 210u || dst[4u * i + 1u] != 190u ||
            dst[4u * i + 2u] != 150u || dst[4u * i + 3u] != 255u) {
            free(dst);
            return 1;
        }
    }
    free(dst);
    return 0;
}

static int check_diagonal_smoothing_and_alpha(void) {
    /* 4x4 diagonal white bone corner over transparent background */
    static const uint8_t pat[4][4] = {
        {1, 1, 0, 0},
        {1, 1, 1, 0},
        {0, 1, 1, 0},
        {0, 0, 0, 0}
    };
    uint8_t src[4 * 4 * 4];
    uint8_t dst3[12 * 12 * 4];
    uint8_t dst2[8 * 8 * 4];
    unsigned x, y;
    for (y = 0; y < 4u; y++) {
        for (x = 0; x < 4u; x++) {
            uint8_t *p = src + (y * 4u + x) * 4u;
            if (pat[y][x]) {
                p[0] = 240u; p[1] = 232u; p[2] = 216u; p[3] = 255u;
            } else {
                p[0] = 0u; p[1] = 0u; p[2] = 0u; p[3] = 0u;
            }
        }
    }
    if (mb_upscale_rgba(src, 4u, 4u, MB_UPSCALE_XBR3X, 0u, dst3, sizeof dst3) != 0)
        return 1;
    /* Transparent bottom-right must stay transparent (alpha == 0) */
    if (dst3[(11u * 12u + 11u) * 4u + 3u] != 0u) return 1;
    /* Interior opaque pixel at (1,1) -> center (4,4) must remain full ivory */
    if (dst3[(4u * 12u + 4u) * 4u + 3u] != 255u ||
        dst3[(4u * 12u + 4u) * 4u + 0u] != 240u)
        return 1;
    /* Diagonal corner (x=2,y=0 -> subblock (6..8, 0..2)) BL corner should bridge diagonal */
    if (dst3[(2u * 12u + 6u) * 4u + 3u] == 0u) return 1;
    /* Any pixel with alpha > 0 must NOT get dark-fringe darkened RGB (must retain ivory R >= 220) */
    for (y = 0; y < 12u; y++) {
        for (x = 0; x < 12u; x++) {
            const uint8_t *p = dst3 + (y * 12u + x) * 4u;
            if (p[3] > 0u && p[0] < 220u) return 1;
        }
    }
    if (mb_upscale_rgba(src, 4u, 4u, MB_UPSCALE_SCALE2X, 0u, dst2, sizeof dst2) != 0)
        return 1;
    if (mb_upscale_rgba(src, 4u, 4u, MB_UPSCALE_SCALE3X, 0u, dst3, sizeof dst3) != 0)
        return 1;
    if (mb_upscale_rgba(src, 4u, 4u, MB_UPSCALE_XBR2X, 0u, dst2, sizeof dst2) != 0)
        return 1;
    return 0;
}

static uint32_t crc32_calc(const uint8_t *buf, size_t len) {
    uint32_t c = 0xFFFFFFFFu;
    size_t i, k;
    for (i = 0; i < len; i++) {
        c ^= buf[i];
        for (k = 0; k < 8u; k++)
            c = (c & 1u) ? ((c >> 1) ^ 0xEDB88320u) : (c >> 1);
    }
    return c ^ 0xFFFFFFFFu;
}

static void wr_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static size_t append_png_chunk(uint8_t *out, size_t pos, const char *tag,
                               const uint8_t *data, size_t len) {
    wr_be32(out + pos, (uint32_t)len);
    memcpy(out + pos + 4u, tag, 4u);
    if (len && data) memcpy(out + pos + 8u, data, len);
    wr_be32(out + pos + 8u + len, crc32_calc(out + pos + 4u, 4u + len));
    return pos + 12u + len;
}

static int write_test_png_2x2(const char *path) {
    /* 2x2 RGBA PNG: row0=(F=1 Sub filter): (10,20,30,255), (+40,+50,+60,0)->(50,70,90,255)
     *               row1=(F=0 None filter): (1,2,3,0), (200,210,220,128) */
    uint8_t raw[18] = {
        1, 10, 20, 30, 255, 40, 50, 60, 0,
        0, 1, 2, 3, 0, 200, 210, 220, 128
    };
    uint8_t z[64];
    uint8_t ihdr[13] = {0,0,0,2, 0,0,0,2, 8,6,0,0,0};
    uint8_t png[256];
    uint32_t s1 = 1u, s2 = 0u;
    size_t i, zpos = 0, ppos = 0;
    FILE *fp;
    for (i = 0; i < sizeof raw; i++) {
        s1 = (s1 + raw[i]) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    z[zpos++] = 0x78u; z[zpos++] = 0x01u;
    z[zpos++] = 0x01u; /* BFINAL=1, BTYPE=0 stored */
    z[zpos++] = (uint8_t)(sizeof raw & 0xFFu); z[zpos++] = 0u;
    z[zpos++] = (uint8_t)((~sizeof raw) & 0xFFu); z[zpos++] = 0xFFu;
    memcpy(z + zpos, raw, sizeof raw); zpos += sizeof raw;
    wr_be32(z + zpos, (s2 << 16) | s1); zpos += 4u;

    memcpy(png, "\x89PNG\r\n\x1a\n", 8u); ppos = 8u;
    ppos = append_png_chunk(png, ppos, "IHDR", ihdr, 13u);
    ppos = append_png_chunk(png, ppos, "IDAT", z, zpos);
    ppos = append_png_chunk(png, ppos, "IEND", NULL, 0u);
    fp = fopen(path, "wb");
    if (!fp) return -1;
    fwrite(png, 1, ppos, fp);
    fclose(fp);
    return 0;
}

static int check_image_loaders(void) {
    uint8_t *px = NULL;
    unsigned w = 0, h = 0;
    FILE *fp;
    static const uint8_t k_tga[18 + 8] = {
        0, 0, 2, 0,0,0,0,0, 0,0,0,0, 2,0, 1,0, 32, 0x20,
        30, 20, 10, 255,  90, 80, 70, 128
    };
    if (write_test_png_2x2("test_tmp_up.png") != 0) return 1;
    if (mb_image_load_rgba("test_tmp_up.png", &px, &w, &h) != 0 ||
        w != 2u || h != 2u ||
        px[0] != 10u || px[1] != 20u || px[2] != 30u || px[3] != 255u ||
        px[4] != 50u || px[5] != 70u || px[6] != 90u || px[7] != 255u ||
        px[8] != 1u || px[11] != 0u ||
        px[12] != 200u || px[13] != 210u || px[14] != 220u || px[15] != 128u) {
        free(px);
        remove("test_tmp_up.png");
        return 1;
    }
    free(px); px = NULL;
    remove("test_tmp_up.png");

    fp = fopen("test_tmp_up.pam", "wb");
    if (!fp) return 1;
    fprintf(fp, "P7\nWIDTH 2\nHEIGHT 1\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n");
    fwrite("\x11\x22\x33\xff\x44\x55\x66\x00", 1, 8, fp);
    fclose(fp);
    if (mb_image_load_rgba("test_tmp_up.pam", &px, &w, &h) != 0 ||
        w != 2u || h != 1u || px[0] != 0x11u || px[7] != 0u) {
        free(px);
        remove("test_tmp_up.pam");
        return 1;
    }
    free(px); px = NULL;
    remove("test_tmp_up.pam");

    fp = fopen("test_tmp_up.tga", "wb");
    if (!fp) return 1;
    fwrite(k_tga, 1, sizeof k_tga, fp);
    fclose(fp);
    if (mb_image_load_rgba("test_tmp_up.tga", &px, &w, &h) != 0 ||
        w != 2u || h != 1u ||
        px[0] != 10u || px[1] != 20u || px[2] != 30u || px[3] != 255u ||
        px[4] != 70u || px[5] != 80u || px[6] != 90u || px[7] != 128u) {
        free(px);
        remove("test_tmp_up.tga");
        return 1;
    }
    free(px);
    remove("test_tmp_up.tga");
    return 0;
}

/* Defined in upscale.c, intentionally not in upscale.h: forces the scalar
 * (clamped) path for every pixel so we can prove the interior fast path matches. */
extern void mb_upscale_xbr_scalar_ref(const uint8_t *src, unsigned w, unsigned h,
                                       unsigned factor, uint8_t *dst);

/* Mixed edge+interior pattern: xBR2x/xBR3x fast path must be byte-identical to a
 * reference scalar pass (zero regression). Needs w,h >= 5 so an interior exists. */
#define FP_W 13u
#define FP_H 11u
static int check_xbr_fastpath_equivalence(void) {
    uint8_t src[FP_W * FP_H * 4u];
    uint8_t *fast = NULL, *ref = NULL;
    unsigned x, y, pass, rc = 0u;
    static const unsigned factors[2] = {2u, 3u};
    /* Diagonals, color gradients and transparent holes so the edge detector
     * fires across interior and border alike (exercises every edr branch). */
    for (y = 0; y < FP_H; y++) {
        for (x = 0; x < FP_W; x++) {
            uint8_t *p = src + ((size_t)y * FP_W + x) * 4u;
            int diag = ((int)x - (int)y + 4) % 5 == 0;
            int hole = (x >= 5u && x <= 7u && y >= 4u && y <= 6u);
            if (hole) {
                p[0] = p[1] = p[2] = p[3] = 0u; /* transparent core */
            } else if (diag) {
                p[0] = 240u; p[1] = 232u; p[2] = 216u; p[3] = 255u; /* ivory bone */
            } else {
                p[0] = (uint8_t)(20u + x * 15u);
                p[1] = (uint8_t)(16u + y * 18u);
                p[2] = (uint8_t)(200u - x * 8u);
                p[3] = 255u;
            }
        }
    }
    for (pass = 0u; pass < 2u && rc == 0u; pass++) {
        unsigned f = factors[pass];
        size_t dw = (size_t)FP_W * f, dh = (size_t)FP_H * f;
        size_t sz = dw * dh * 4u;
        mb_upscale_mode mode = (f == 2u) ? MB_UPSCALE_XBR2X : MB_UPSCALE_XBR3X;
        fast = (uint8_t *)malloc(sz);
        ref = (uint8_t *)malloc(sz);
        if (!fast || !ref) { rc = 1; }
        else if (mb_upscale_rgba(src, FP_W, FP_H, mode, 0u, fast, sz) != 0) { rc = 1; }
        else {
            mb_upscale_xbr_scalar_ref(src, FP_W, FP_H, f, ref);
            if (memcmp(fast, ref, sz) != 0) rc = 1;
        }
        free(fast); fast = NULL;
        free(ref); ref = NULL;
    }
    return (int)rc;
}

static int check_hd_pose_overlay(void) {
    uint8_t *f1x = (uint8_t *)calloc(320u * 224u * 4u, 1u);
    uint8_t *f3x = (uint8_t *)calloc(960u * 672u * 4u, 1u);
    stage_bones_pose pose;
    if (!f1x || !f3x) { free(f1x); free(f3x); return 1; }
    stage_enemies_reset_bones_pose();
    if (stage_enemies_last_bones_pose(&pose) == 0) { free(f1x); free(f3x); return 1; }

    stage_enemies_set_hd_overlay(1);
    stage_enemies_draw_bones_ex(f1x, 320u, 224u, 160, 180, -1, 0, 0, 2u, 256u, 256u);
    if (stage_enemies_last_bones_pose(&pose) != 0 || !pose.valid ||
        pose.foot_x != 160 || pose.foot_y != 180 || pose.facing != -1) {
        stage_enemies_set_hd_overlay(0);
        free(f1x); free(f3x);
        return 1;
    }
    /* Suppression mode must leave 1x framebuffer untouched */
    if (f1x[(170u * 320u + 160u) * 4u + 3u] != 0u) {
        stage_enemies_set_hd_overlay(0);
        free(f1x); free(f3x);
        return 1;
    }
    stage_enemies_set_hd_overlay(0);
    stage_enemies_draw_bones_ex(f1x, 320u, 224u, 160, 180, 1, 0, 0, 0u, 256u, 256u);
    /* Normal mode draws onto 1x framebuffer (fallback or disc sprite) */
    if (f1x[(170u * 320u + 160u) * 4u + 3u] != 255u) {
        free(f1x); free(f3x);
        return 1;
    }

    /* Test HD blit alpha compositor directly into 3x canvas */
    {
        uint8_t sp[4] = {200, 180, 160, 255};
        if (mb_upscale_blit_alpha(f3x, 960u, 672u, sp, 1u, 1u,
                                  480, 500, 12u, 12u, 0) != 0 ||
            f3x[(505u * 960u + 485u) * 4u + 0u] != 200u) {
            free(f1x); free(f3x);
            return 1;
        }
    }
    free(f1x);
    free(f3x);
    return 0;
}

int main(void) {
    mb_upscale_mode m;
    if (mb_upscale_mode_parse("xbr3x", &m) != 0 || m != MB_UPSCALE_XBR3X) return 1;
    if (mb_upscale_mode_parse("XBR2X", &m) != 0 || m != MB_UPSCALE_XBR2X) return 1;
    if (mb_upscale_mode_parse("scale3x", &m) != 0 || m != MB_UPSCALE_SCALE3X) return 1;
    if (mb_upscale_mode_parse("scale2x", &m) != 0 || m != MB_UPSCALE_SCALE2X) return 1;
    if (mb_upscale_mode_parse("nearest", &m) != 0 || m != MB_UPSCALE_NEAREST) return 1;
    if (strcmp(mb_upscale_mode_name(MB_UPSCALE_XBR3X), "xbr3x") != 0) return 1;

    if (check_solid_block(MB_UPSCALE_NEAREST, 3u, 3u)) return 1;
    if (check_solid_block(MB_UPSCALE_SCALE2X, 0u, 2u)) return 1;
    if (check_solid_block(MB_UPSCALE_SCALE3X, 0u, 3u)) return 1;
    if (check_solid_block(MB_UPSCALE_XBR2X, 0u, 2u)) return 1;
    if (check_solid_block(MB_UPSCALE_XBR3X, 0u, 3u)) return 1;
    if (check_solid_block(MB_UPSCALE_XBR2X, 4u, 4u)) return 1;

    if (check_diagonal_smoothing_and_alpha()) { fprintf(stderr, "fail diag\n"); return 1; }
    if (check_xbr_fastpath_equivalence()) { fprintf(stderr, "fail xbr fastpath\n"); return 1; }
    if (check_image_loaders()) { fprintf(stderr, "fail img\n"); return 1; }
    if (check_hd_pose_overlay()) { fprintf(stderr, "fail hd\n"); return 1; }

    printf("test_upscale OK\n");
    return 0;
}
