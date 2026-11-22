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
    /* A-style: first words look like pixel plane size (MAUSO 1176×608, ICE1 1864×8192). */
    const unsigned blocks_w = (w0 + 31u) / 32u;
    const unsigned blocks_h = (w1 + 31u) / 32u;
    if (w0 >= 256u && w1 >= 64u && w0 <= 16384u && w1 <= 8192u &&
        (uint64_t)blocks_w * (uint64_t)blocks_h <= SCRL_TILE_BLOCKS_MAX &&
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
    /* A..D follow the camera at 1, 1/2, 1/3 and 1/4 (a third rounded up, so whole thirds round
     * the way a division does), unshifted. */
    p->parallax_coeff = (int32_t)((SCRL_PARALLAX_ONE + (int)m->plane_count) / ((int)m->plane_count + 1));
    p->base_y = 0;
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

int64_t scrl_parallax_offset(unsigned camera_px, int32_t coeff) {
    return ((int64_t)camera_px * (int64_t)coeff) / SCRL_PARALLAX_ONE;
}

int scrl_map_set_plane_parallax(scrl_map *m, unsigned plane, int32_t coeff, int32_t base_y) {
    if (!m || plane >= m->plane_count || plane >= SCRL_PLANE_MAX) return -1;
    m->planes[plane].parallax_coeff = coeff;
    m->planes[plane].base_y = base_y;
    return 0;
}

int scrl_map_set_back_color(scrl_map *m, uint8_t r, uint8_t g, uint8_t b) {
    if (!m) return -1;
    m->back_mode = 1;
    m->back_rgb[0] = r;
    m->back_rgb[1] = g;
    m->back_rgb[2] = b;
    m->back_line_count = 0;
    return 0;
}

int scrl_map_set_back_gradient(scrl_map *m, const uint8_t (*rgb_lines)[3], unsigned lines) {
    if (!m || !rgb_lines || lines == 0 || lines > SCRL_BACK_LINES_MAX) return -1;
    memcpy(m->back_lines, rgb_lines, (size_t)lines * 3u);
    m->back_line_count = lines;
    m->back_mode = 2;
    return 0;
}

void scrl_map_clear_back(scrl_map *m) {
    if (!m) return;
    m->back_mode = 0;
    m->back_line_count = 0;
}

unsigned scrl_map_cached_planes(const scrl_map *m) {
    unsigned i, n = 0;
    if (!m) return 0;
    for (i = 0; i < m->plane_count && i < SCRL_PLANE_MAX; i++)
        if (m->planes[i].tiles.status == SCRL_TC_OK) n++;
    return n;
}

/* Streaming planes carry (relative sector, byte count) pairs for 08ATIL. */
static void parse_amap(scrl_map *m, const uint8_t *amap, size_t size, size_t til_size) {
    size_t sector = 0;
    if (!amap || !size || size % 4u || size / 4u > 65536u) return;
    for (size_t i = 0; i < size; i += 4) {
        unsigned bytes = be16(amap + i + 2u);
        if (be16(amap + i) != sector || !bytes || sector * 2048u + bytes > til_size) return;
        sector += (bytes + 2047u) / 2048u;
    }
    m->amap_n = (unsigned)(size / 4u);
    m->has_amap = 1;
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
    parse_amap(m, (const uint8_t *)amap, amap_sz, atil_sz);
    return m->plane_count > 0 || m->has_amap ? 0 : -1;
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
        if (near_p)
            fy = floor_from_til(near_p, x, strip_w);
        else
            fy = 180;
        m->floor_py[x] = (uint8_t)fy;
        m->wall[x] = 0;
    }
    /* Light smooth — TIL density floors are noisy without AMAP. */
    if (strip_w >= 3) {
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
    if (strip_w >= 2) {
        for (x = 1; x < strip_w; x++) {
            int lo = (int)m->floor_py[x - 1] - SCRL_FLOOR_MAX_SLOPE;
            int hi = (int)m->floor_py[x - 1] + SCRL_FLOOR_MAX_SLOPE;
            int v = m->floor_py[x];
            m->floor_py[x] = (uint8_t)(v < lo ? lo : (v > hi ? hi : v));
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

/*
 * First and last plane column holding any non-zero dot, found through the tile cache. Returns 0
 * when the plane has none. Blocks that cannot widen the range found so far are skipped.
 */
static int populated_cols(const scrl_tile_cache *c, unsigned pw, unsigned *lo_out, unsigned *hi_out) {
    unsigned bx, by, lx, ly, lo = pw, hi = 0;
    int found = 0;
    if (c->status != SCRL_TC_OK || !c->present || !pw) return 0;
    for (by = 0; by < c->rows; by++) {
        for (bx = 0; bx < c->cols; bx++) {
            const unsigned x0 = bx * SCRL_TILE_PX;
            if (c->present[by * c->cols + bx] != 1u || x0 >= pw) continue;
            if (found && x0 > lo && x0 + SCRL_TILE_PX - 1u < hi) continue; /* strictly inside */
            for (ly = 0; ly < SCRL_TILE_PX; ly++) {
                const unsigned y = by * SCRL_TILE_PX + ly;
                if (y >= c->height_px) break;
                for (lx = 0; lx < SCRL_TILE_PX; lx++) {
                    const unsigned x = x0 + lx;
                    if (x >= pw) break;
                    if (scrl_tilecache_pixel(c, x, y) > 0) {
                        if (x < lo) lo = x;
                        if (x > hi) hi = x;
                        found = 1;
                    }
                }
            }
        }
    }
    if (!found) return 0;
    *lo_out = lo;
    *hi_out = hi;
    return 1;
}

/* Alpha 0..255 of an opaque dot at plane column `col` for a populated range [lo, hi]. */
static unsigned feather_alpha(unsigned col, unsigned lo, unsigned hi, unsigned pw) {
    unsigned a = 255u;
    if (lo > 0u && col >= lo && col < lo + SCRL_FEATHER_PX) {
        const unsigned t = 255u * (col - lo + 1u) / (SCRL_FEATHER_PX + 1u);
        if (t < a) a = t;
    }
    if (hi + 1u < pw && col <= hi && col + SCRL_FEATHER_PX > hi) {
        const unsigned t = 255u * (hi - col + 1u) / (SCRL_FEATHER_PX + 1u);
        if (t < a) a = t;
    }
    return a;
}

int scrl_map_draw(const scrl_map *m, const uint8_t *palette_rgba1024,
                  uint8_t *strip_rgba, unsigned strip_w, unsigned strip_h,
                  unsigned camera_px) {
    return scrl_map_draw_with_front_mask(m, palette_rgba1024, strip_rgba, strip_w,
                                         strip_h, camera_px, NULL);
}

int scrl_map_draw_with_front_mask(const scrl_map *m, const uint8_t *palette_rgba1024,
                                  uint8_t *strip_rgba, unsigned strip_w, unsigned strip_h,
                                  unsigned camera_px, uint8_t *front_mask) {
    unsigned x, y, pi;
    if (!m || !palette_rgba1024 || !strip_rgba || strip_w == 0 || strip_h == 0)
        return -1;
    if (front_mask) memset(front_mask, 0, (size_t)strip_w * strip_h);

    /*
     * Back screen: CRAM entry 0 of the stage palette (black when it is zero), or the colour or
     * per-scanline gradient set on the map. No synthetic bands: whatever no plane covers shows
     * this colour.
     */
    for (y = 0; y < strip_h; y++) {
        uint8_t back[3];
        if (m->back_mode == 2 && m->back_line_count) {
            const unsigned line = y < m->back_line_count ? y : m->back_line_count - 1u;
            back[0] = m->back_lines[line][0];
            back[1] = m->back_lines[line][1];
            back[2] = m->back_lines[line][2];
        } else if (m->back_mode == 1) {
            back[0] = m->back_rgb[0];
            back[1] = m->back_rgb[1];
            back[2] = m->back_rgb[2];
        } else {
            back[0] = palette_rgba1024[1];
            back[1] = palette_rgba1024[2];
            back[2] = palette_rgba1024[3];
        }
        for (x = 0; x < strip_w; x++) {
            size_t di = ((size_t)y * strip_w + x) * 4u;
            strip_rgba[di + 0] = back[0];
            strip_rgba[di + 1] = back[1];
            strip_rgba[di + 2] = back[2];
            strip_rgba[di + 3] = 255;
        }
    }

    /* Draw far to near (D..A), each plane at its own parallax and base_y. */
    for (pi = m->plane_count > SCRL_PLANE_MAX ? SCRL_PLANE_MAX : m->plane_count; pi > 0; pi--) {
        const scrl_plane *p = &m->planes[pi - 1];
        const unsigned pw = p->pixel_w ? p->pixel_w : strip_w;
        const unsigned ph = p->pixel_h ? p->pixel_h : strip_h;
        const int64_t cam = scrl_parallax_offset(camera_px, p->parallax_coeff);
        /*
         * Vertical scene camera: the world plane (Plane A, pi == 1) when taller than the view is
         * shown from camera_y down, so its authored floor band meets the screen floor instead of
         * its sky. Background planes B/C/D (pi > 1) and planes that fit the view keep their
         * base_y position untouched (the vertical camera does not apply to them).
         */
        const int64_t vcam = (pi == 1u && (int64_t)ph > (int64_t)strip_h) ? (int64_t)m->camera_y : 0;
        unsigned flo = 0, fhi = 0;
        int feather = 0;
        if (p->tiles.status != SCRL_TC_OK) continue; /* nothing to draw: skip the pixels */
        if (p->blend_mode == 1u) feather = populated_cols(&p->tiles, pw, &flo, &fhi);
        for (y = 0; y < strip_h; y++) {
            unsigned line = y >= p->restart_y ? y - p->restart_y : y;
            int64_t raw_row = (int64_t)line + vcam - (int64_t)p->base_y;
            int64_t row;
            unsigned sy;
            if (p->no_wrap_y && (raw_row < 0 || raw_row >= (int64_t)ph)) continue;
            row = raw_row % (int64_t)ph;
            if (row < 0) row += ph;
            sy = (unsigned)row;
            for (x = 0; x < strip_w; x++) {
                int64_t raw_col = (int64_t)x + cam + (int64_t)p->base_x;
                int64_t col;
                uint8_t px[4];
                size_t di = ((size_t)y * strip_w + x) * 4u;
                if (p->no_wrap_x && (raw_col < 0 || raw_col >= (int64_t)pw)) continue;
                col = raw_col % (int64_t)pw;
                if (col < 0) col += pw;
                sample_plane(p, (unsigned)col, sy, palette_rgba1024, px);
                if (p->blend_mode == 3u) {
                    if (ph >= 1376u && sy >= 1376u &&
                        (px[3] == 0 || (px[0] <= 18u && px[1] <= 18u && px[2] <= 18u)))
                        sample_plane(p, (unsigned)col, 1280u + (sy & 63u), palette_rgba1024, px);
                    if (px[3] == 0 && sy >= 128u && ph >= 160u)
                        sample_plane(p, (unsigned)col, 64u + (sy & 63u), palette_rgba1024, px);
                }
                if (px[3] == 0) continue; /* dot 0 and missing blocks stay transparent */
                if (pi == 1 && front_mask) front_mask[di / 4u] = 1;
                if (p->blend_mode == 2u && !p->no_wrap_x && pw >= 2u * SCRL_WRAP_FEATHER_PX) {
                    unsigned ucol = (unsigned)col;
                    unsigned dist = ucol < SCRL_WRAP_FEATHER_PX
                                        ? ucol
                                        : (ucol >= pw - SCRL_WRAP_FEATHER_PX ? (pw - 1u - ucol)
                                                                             : SCRL_WRAP_FEATHER_PX);
                    if (dist < SCRL_WRAP_FEATHER_PX) {
                        unsigned opp_col = (ucol < SCRL_WRAP_FEATHER_PX) ? (pw - 1u) : 0u;
                        uint8_t opp_px[4];
                        sample_plane(p, opp_col, sy, palette_rgba1024, opp_px);
                        if (opp_px[3] != 0) {
                            unsigned w_opp = ((SCRL_WRAP_FEATHER_PX - dist) * 128u) / SCRL_WRAP_FEATHER_PX;
                            px[0] = (uint8_t)((px[0] * (256u - w_opp) + opp_px[0] * w_opp) / 256u);
                            px[1] = (uint8_t)((px[1] * (256u - w_opp) + opp_px[1] * w_opp) / 256u);
                            px[2] = (uint8_t)((px[2] * (256u - w_opp) + opp_px[2] * w_opp) / 256u);
                        }
                    }
                }
                if (feather) {
                    const unsigned a = feather_alpha((unsigned)col, flo, fhi, pw);
                    if (a < 255u) {
                        /* Feathered edge: blend over what is already drawn behind. */
                        strip_rgba[di + 0] = (uint8_t)((px[0] * a + strip_rgba[di + 0] * (255u - a)) / 255u);
                        strip_rgba[di + 1] = (uint8_t)((px[1] * a + strip_rgba[di + 1] * (255u - a)) / 255u);
                        strip_rgba[di + 2] = (uint8_t)((px[2] * a + strip_rgba[di + 2] * (255u - a)) / 255u);
                        continue;
                    }
                }
                /* An opaque dot replaces what is behind it. */
                strip_rgba[di + 0] = px[0];
                strip_rgba[di + 1] = px[1];
                strip_rgba[di + 2] = px[2];
            }
        }
    }

    return 0;
}

int scrl_map_find_floor(const scrl_map *m, int px) {
    if (!m || m->strip_w == 0) return -1;
    if (px < 0) px = 0;
    px %= (int)m->strip_w;
    if (m->floor_py[px] == SCRL_FLOOR_PIT) return -1;
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
