#include "scrl_map.h"
#include "scrl_decomp.h"
#include <stdio.h>
#include <string.h>

static unsigned be16(const uint8_t *p) {
    return ((unsigned)p[0] << 8) | p[1];
}

int scrl_cdb_parse(const uint8_t *cdb, size_t sz, scrl_plane *out) {
    unsigned w0, w1, w2, w3, w4;
    if (!cdb || !out || sz < 10) return -1;
    memset(out, 0, sizeof *out);
    out->cdb = cdb;
    out->cdb_size = sz;
    w0 = be16(cdb);
    w1 = be16(cdb + 2);
    w2 = be16(cdb + 4);
    w3 = be16(cdb + 6);
    w4 = be16(cdb + 8);
    out->flags = w0;
    out->tile_count = w4;
    /* A-style: first words look like pixel plane size (MAUSO 1176×608, BAT* thousands). */
    if (w0 >= 256u && w1 >= 64u && w0 <= 16384u && w1 <= 4096u &&
        (w2 <= 512u || w0 > 0x200u)) {
        out->hdr_style = 0;
        out->pixel_w = w0;
        out->pixel_h = w1;
        out->tile_w = w2 ? w2 : 8;
        out->tile_h = w3 ? w3 : 8;
    } else {
        /* B-style: flags + tile cell size (VORTEX/BCDB 0x0200 / tw / th / n). */
        out->hdr_style = 1;
        out->tile_w = (w2 >= 1 && w2 <= 64) ? w2 : 16;
        out->tile_h = (w3 >= 1 && w3 <= 64) ? w3 : 8;
        out->pixel_w = out->tile_w * 8u;
        out->pixel_h = out->tile_h * 8u;
        if (w1 >= 64 && w1 <= 4096)
            out->pixel_w = w1; /* occasional width override */
    }
    if (out->pixel_w < 32) out->pixel_w = 320;
    if (out->pixel_h < 32) out->pixel_h = 224;
    return 0;
}

size_t scrl_decomp_byte(const uint8_t *src, size_t src_cap,
                        uint8_t *dst, size_t dst_cap) {
    return scrl_decomp_byte_ex(src, src_cap, dst, dst_cap, NULL);
}

static int add_plane(scrl_map *m, const char *cdn, const char *tiln,
                     const void *cdb, size_t csz, const void *til, size_t tsz) {
    scrl_plane *p;
    if (!cdb || csz < 10 || m->plane_count >= SCRL_PLANE_MAX) return 0;
    p = &m->planes[m->plane_count];
    if (scrl_cdb_parse((const uint8_t *)cdb, csz, p)) return 0;
    snprintf(p->cdb_name, sizeof p->cdb_name, "%s", cdn);
    snprintf(p->til_name, sizeof p->til_name, "%s", tiln ? tiln : "");
    p->til = (const uint8_t *)til;
    p->til_size = tsz;
    /* All or nothing: a TIL that does not walk cleanly leaves the plane transparent. */
    if (til && tsz)
        (void)scrl_tilecache_build(&p->tiles, p->pixel_w, p->pixel_h, (const uint8_t *)cdb, csz,
                                   (const uint8_t *)til, tsz);
    m->plane_count++;
    m->has_cdb = 1;
    return 1;
}

void scrl_map_free(scrl_map *m) {
    unsigned i;
    if (!m) return;
    for (i = 0; i < SCRL_PLANE_MAX; i++) scrl_tilecache_free(&m->planes[i].tiles);
    memset(m, 0, sizeof *m);
}

unsigned scrl_map_cached_planes(const scrl_map *m) {
    unsigned i, n = 0;
    if (!m) return 0;
    for (i = 0; i < m->plane_count && i < SCRL_PLANE_MAX; i++)
        if (m->planes[i].tiles.status == SCRL_TC_OK) n++;
    return n;
}

static void parse_amap(scrl_map *m, const uint8_t *amap, size_t sz) {
    size_t i;
    unsigned max_x = 0;
    if (!amap || sz < 4) return;
    m->amap_n = 0;
    /* BE16 (x,y) pairs — BAT1/VALLEY authored floor polyline. */
    for (i = 0; i + 4 <= sz && m->amap_n < SCRL_AMAP_MAX; i += 4) {
        unsigned x = be16(amap + i);
        unsigned y = be16(amap + i + 2);
        unsigned py;
        if (y > 224u)
            py = y >> 8; /* 16.8-ish fixed → pixel */
        else
            py = y;
        if (py < 8) py = 8;
        if (py > 220) py = 220;
        m->amap_x[m->amap_n] = (uint16_t)x;
        m->amap_py[m->amap_n] = (uint8_t)py;
        if (x > max_x) max_x = x;
        m->amap_n++;
    }
    /* Dense small X → tile columns; scale to pixels. */
    if (m->amap_n > 4 && max_x > 0 && max_x < 400) {
        unsigned k;
        for (k = 0; k < m->amap_n; k++)
            m->amap_x[k] = (uint16_t)(m->amap_x[k] * 8u);
    }
    m->has_amap = m->amap_n > 0;
}

int scrl_map_init(scrl_map *m,
                  const void *acdb, size_t acdb_sz, const void *atil, size_t atil_sz,
                  const void *bcdb, size_t bcdb_sz, const void *btil, size_t btil_sz,
                  const void *ccdb, size_t ccdb_sz, const void *ctil, size_t ctil_sz,
                  const void *dcdb, size_t dcdb_sz, const void *dtil, size_t dtil_sz,
                  const void *amap, size_t amap_sz) {
    if (!m) return -1;
    memset(m, 0, sizeof *m);
    add_plane(m, "07ACDB", "08ATIL", acdb, acdb_sz, atil, atil_sz);
    add_plane(m, "09BCDB", "10BTIL", bcdb, bcdb_sz, btil, btil_sz);
    add_plane(m, "11CCDB", "12CTIL", ccdb, ccdb_sz, ctil, ctil_sz);
    add_plane(m, "13DCDB", "14DTIL", dcdb, dcdb_sz, dtil, dtil_sz);
    parse_amap(m, (const uint8_t *)amap, amap_sz);
    return m->plane_count > 0 || m->has_amap ? 0 : -1;
}

static unsigned floor_from_amap(const scrl_map *m, unsigned px) {
    unsigned i;
    if (!m->amap_n) return 180;
    if (px <= m->amap_x[0]) return m->amap_py[0];
    for (i = 1; i < m->amap_n; i++) {
        if (px <= m->amap_x[i]) {
            unsigned x0 = m->amap_x[i - 1], x1 = m->amap_x[i];
            unsigned y0 = m->amap_py[i - 1], y1 = m->amap_py[i];
            unsigned span = x1 > x0 ? x1 - x0 : 1;
            unsigned t = px - x0;
            return y0 + (int)(y1 - y0) * (int)t / (int)span;
        }
    }
    return m->amap_py[m->amap_n - 1];
}

static unsigned floor_from_til(const scrl_plane *p, unsigned px, unsigned strip_w) {
    unsigned best = 0, yy, fy = 140;
    size_t stride;
    if (!p || !p->til || p->til_size < 64) return 140;
    stride = p->pixel_w >= 64 && p->pixel_w <= 4096 ? p->pixel_w : 256u;
    /*
     * In authored Saturn stages without 06AMAP (MAUSO, GLASS, etc.), p->til is
     * a compressed LZ stream. Probing compressed bytes as raster pixels produces
     * random cliffs and pits (e.g. 60px drops at x=229). For uncompressed synthetic
     * test buffers (til_size == stride * pixel_h), run the estimator; for compressed
     * stage data, return the authored flat chamber floor at y=140.
     */
    if (p->pixel_h > 0 && p->til_size != (size_t)stride * p->pixel_h)
        return 140;
    px %= strip_w ? strip_w : 640u;
    for (yy = 120; yy < 224; yy++) {
        size_t si = ((size_t)yy * stride + (size_t)px) % p->til_size;
        if (p->til[si] > best) {
            best = p->til[si];
            fy = yy;
        }
    }
    if (fy < 140) fy = 140;
    if (fy > 210) fy = 210;
    return fy;
}

void scrl_map_build_collision(scrl_map *m, unsigned strip_w) {
    unsigned x;
    const scrl_plane *near_p = NULL;
    if (!m) return;
    if (strip_w < 320) strip_w = 640;
    if (strip_w > SCRL_STRIP_MAX) strip_w = SCRL_STRIP_MAX;
    m->strip_w = strip_w;
    if (m->plane_count)
        near_p = &m->planes[0];

    for (x = 0; x < strip_w; x++) {
        unsigned fy;
        if (m->has_amap)
            fy = floor_from_amap(m, x);
        else if (near_p)
            fy = floor_from_til(near_p, x, strip_w);
        else
            fy = 180;
        m->floor_py[x] = (uint8_t)fy;
        m->wall[x] = 0;
    }
    /* Light smooth — TIL density floors are noisy without AMAP. */
    if (!m->has_amap && strip_w >= 3) {
        uint8_t tmp[SCRL_STRIP_MAX];
        memcpy(tmp, m->floor_py, strip_w);
        for (x = 1; x + 1 < strip_w; x++) {
            unsigned a = tmp[x - 1], b = tmp[x], c = tmp[x + 1];
            m->floor_py[x] = (uint8_t)((a + b * 2u + c) / 4u);
        }
    }
    /*
     * An estimated floor is not terrain: the density estimate jumps by tens of rows
     * between neighbouring columns, which a physical actor reads as cliffs and pits it
     * can never leave. Limit the slew rate: each column ends within SCRL_FLOOR_MAX_SLOPE
     * of the (already limited) one before it, so every neighbouring pair does. One pass
     * is enough; a pass back would find nothing left to change.
     */
    if (!m->has_amap && strip_w >= 2) {
        for (x = 1; x < strip_w; x++) {
            int lo = (int)m->floor_py[x - 1] - SCRL_FLOOR_MAX_SLOPE;
            int hi = (int)m->floor_py[x - 1] + SCRL_FLOOR_MAX_SLOPE;
            int v = m->floor_py[x];
            m->floor_py[x] = (uint8_t)(v < lo ? lo : (v > hi ? hi : v));
        }
    }
    /* Walls from authored AMAP extreme cliffs only. */
    if (m->has_amap) {
        for (x = 1; x < strip_w; x++) {
            int d = (int)m->floor_py[x] - (int)m->floor_py[x - 1];
            if (d > 80 || d < -80)
                m->wall[x] = 1;
        }
    }
}

/*
 * The CDB and TIL are descriptors and compressed streams, not raster pixels: reading their
 * bytes as dots turns the screen into noise. A plane is sampled only through its decoded tile
 * cache (scrl_tilecache.h), and a plane whose cache could not be built is not sampled at all.
 * Dot 0, a missing block and a point outside the plane are transparent.
 */
static void sample_plane(const scrl_plane *p, unsigned sx, unsigned sy,
                         const uint8_t *pal, uint8_t *rgba) {
    /* CRAM index = (descriptor palette bank << 8) | character dot; pal holds 1024 entries. */
    const int cram = scrl_tilecache_cram(&p->tiles, sx, sy);
    if (cram <= 0 || cram >= 1024) {
        rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0;
        return;
    }
    rgba[0] = pal[4 * cram + 1]; /* palette entries are {unused, R, G, B} */
    rgba[1] = pal[4 * cram + 2];
    rgba[2] = pal[4 * cram + 3];
    rgba[3] = 255;
}

int scrl_map_draw(const scrl_map *m, const uint8_t *palette_rgba1024,
                  uint8_t *strip_rgba, unsigned strip_w, unsigned strip_h,
                  unsigned camera_px) {
    unsigned x, y, pi;
    static const unsigned k_div[SCRL_PLANE_MAX] = {1u, 2u, 3u, 4u};
    if (!m || !palette_rgba1024 || !strip_rgba || strip_w == 0 || strip_h == 0)
        return -1;

    /* Sky base. */
    for (y = 0; y < strip_h; y++) {
        for (x = 0; x < strip_w; x++) {
            size_t di = ((size_t)y * strip_w + x) * 4u;
            if (y < 90) {
                strip_rgba[di + 0] = 18;
                strip_rgba[di + 1] = 36;
                strip_rgba[di + 2] = 72;
            } else if (y < 150) {
                strip_rgba[di + 0] = 28;
                strip_rgba[di + 1] = 60;
                strip_rgba[di + 2] = 40;
            } else {
                strip_rgba[di + 0] = 36;
                strip_rgba[di + 1] = 44;
                strip_rgba[di + 2] = 28;
            }
            strip_rgba[di + 3] = 255;
        }
    }

    /* Draw far→near (D..A): plane_count-1 .. 0 with parallax. */
    if (m->plane_count) {
        for (pi = m->plane_count; pi > 0; pi--) {
            const scrl_plane *p = &m->planes[pi - 1];
            unsigned div = k_div[(pi - 1) < SCRL_PLANE_MAX ? (pi - 1) : 3];
            unsigned cam = camera_px / div;
            if (p->tiles.status != SCRL_TC_OK) continue; /* nothing to draw: skip the pixels */
            for (y = 0; y < strip_h; y++) {
                for (x = 0; x < strip_w; x++) {
                    uint8_t px[4];
                    unsigned sx = (x + cam) % (p->pixel_w ? p->pixel_w : strip_w);
                    unsigned sy = y % (p->pixel_h ? p->pixel_h : strip_h);
                    size_t di = ((size_t)y * strip_w + x) * 4u;
                    sample_plane(p, sx, sy, palette_rgba1024, px);
                    if (px[3] == 0) continue;
                    /* Alpha blend over existing. */
                    strip_rgba[di + 0] = (uint8_t)((px[0] * 3u + strip_rgba[di + 0]) / 4u);
                    strip_rgba[di + 1] = (uint8_t)((px[1] * 3u + strip_rgba[di + 1]) / 4u);
                    strip_rgba[di + 2] = (uint8_t)((px[2] * 3u + strip_rgba[di + 2]) / 4u);
                }
            }
        }
    }

    /* Floor tick from collision table. */
    if (m->strip_w) {
        for (x = 0; x < strip_w && x < m->strip_w; x++) {
            unsigned fy = m->floor_py[x];
            size_t di;
            if (fy >= strip_h) continue;
            di = ((size_t)fy * strip_w + x) * 4u;
            strip_rgba[di + 0] = m->wall[x] ? 180 : 110;
            strip_rgba[di + 1] = m->wall[x] ? 60 : 90;
            strip_rgba[di + 2] = 40;
            strip_rgba[di + 3] = 255;
        }
    }
    return 0;
}

int scrl_map_find_floor(const scrl_map *m, int px) {
    if (!m || m->strip_w == 0) return -1;
    if (px < 0) px = 0;
    px %= (int)m->strip_w;
    return m->floor_py[px];
}

int scrl_map_wall_at(const scrl_map *m, int px) {
    if (!m || m->strip_w == 0) return 0;
    if (px < 0) px = 0;
    px %= (int)m->strip_w;
    return m->wall[px];
}

int scrl_map_get_hixel(const scrl_map *m, int px, int py) {
    int fy;
    if (!m || m->strip_w == 0) return SCRL_HIXEL_AIR;
    if (px < 0) px = 0;
    px %= (int)m->strip_w;
    if (m->wall[px] && py < (int)m->floor_py[px] && py > (int)m->floor_py[px] - 40)
        return SCRL_HIXEL_WALL;
    fy = m->floor_py[px];
    if (py >= fy) return SCRL_HIXEL_FLOOR;
    return SCRL_HIXEL_AIR;
}
