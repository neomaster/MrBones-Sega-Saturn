#ifndef MB_SCRL_MAP_H
#define MB_SCRL_MAP_H
#include "scrl_tilecache.h"
#include <stddef.h>
#include <stdint.h>

/*
 * §23 Host stand-in for sidescrl.o LoadCDB/LoadMap/DrawMap + scrlcoll.o
 * SCRLCOLLGetHixel3D / findfloor / pFloorHixels over 07ACDB..14DTIL (+06AMAP).
 * MAP refs (MAUSO): LoadCDB@2a1d38 LoadMap@2a1c78 DrawMap@2a19f4
 *   GetHixel3D@2a2ee8 findfloor@2a2f0c InitGetFloor@2a2e6c pFloorHixels COMMON.
 * CDB BE16 header: A-style [pix_w][pix_h][…] or B-style [flags][…][tw][th][n].
 * AMAP: BE16 chunk sector and byte count in ATIL.
 */

#define SCRL_PLANE_MAX 4u
#define SCRL_STRIP_MAX 16384u
/*
 * Steepest slope, in rows per column, that an estimated floor may have. It is the
 * limit for walking at full speed in sidescrl_motion.h (max_climb_step 6 divided by
 * 3 px/frame). The disk hixel mask is read separately.
 */
#define SCRL_FLOOR_MAX_SLOPE 2
#define SCRL_FLOOR_PIT 0xFFu
#define SCRL_HIXEL_AIR 0
#define SCRL_HIXEL_FLOOR 1
#define SCRL_HIXEL_WALL 2

typedef struct {
    char cdb_name[12];
    char til_name[12];
    const uint8_t *cdb;
    size_t cdb_size;
    const uint8_t *til;
    size_t til_size;
    unsigned pixel_w, pixel_h; /* decoded from CDB header */
    unsigned tile_w, tile_h;   /* cell hints (often 8/16) */
    unsigned tile_count;       /* CDB[+8] n from LoadCDB@2a1d38 */
    unsigned flags;
    unsigned hdr_style; /* 0=A pixel dims, 1=B tile dims */
    scrl_tile_cache tiles; /* decoded TIL blocks; status != OK means "draw nothing" */
    /*
     * Scroll of this plane against the camera. The plane column at image column x is
     * x + (camera_px * parallax_coeff >> 16), so 0x10000 follows the camera one to one, 0x8000
     * half as fast, 0 not at all. base_y shifts the plane down by that many rows (negative: up):
     * the plane row at image row y is y - base_y, wrapped at the plane height. scrl_map_init sets
     * the A..D defaults (1, 1/2, 1/3, 1/4 of the camera, no shift); scrl_map_set_plane_parallax
     * changes them per plane.
     */
    int32_t parallax_coeff;
    int32_t base_x;
    int32_t base_y;
    unsigned restart_y; /* scanline where the VDP2 vertical accumulator resets, 0 if unused */
    uint8_t no_wrap_y;
    uint8_t no_wrap_x; /* 1 = do not tile horizontally (full-width foreground plane) */
    /*
     * 0 = opaque (an opaque dot replaces what is behind it; the default, zeroed by memset).
     * 1 = horizontal feather: opaque dots within SCRL_FEATHER_PX columns of either edge of the
     * plane's populated column range (the columns holding any non-zero dot) are alpha-blended over
     * what is already drawn, with a linear ramp, so a narrow panel (17VORTEX plane A, x 128..383 of
     * 512) fades into the plane behind it instead of ending in a hard cut. An edge that touches the
     * plane border (column 0 / pixel_w - 1) is not feathered.
     * 2 = horizontal wrap-seam crossfade: on a wrapping plane (17VORTEX plane B, 320x320), columns
     * within SCRL_WRAP_FEATHER_PX of column 0 or pixel_w - 1 crossfade with the opposite border so
     * col = pixel_w - 1 -> 0 has no visible vertical step.
     * 3 = vertical lower-band fill: when a background plane has empty blocks in its lower rows
     * (09BAT2 plane C, rows y >= 160), transparent dots at sy >= 128 fall back to rows 64..127.
     */
    uint8_t blend_mode;
} scrl_plane;

#define SCRL_FEATHER_PX 20u
#define SCRL_WRAP_FEATHER_PX 16u

#define SCRL_PARALLAX_ONE 0x10000
#define SCRL_BACK_LINES_MAX 512u

typedef struct {
    scrl_plane planes[SCRL_PLANE_MAX]; /* A,B,C,D */
    unsigned plane_count;
    /* Number of validated ATIL chunk descriptors. */
    unsigned amap_n;
    /* pFloorHixels / wall columns across strip. */
    unsigned strip_w;
    uint8_t floor_py[SCRL_STRIP_MAX];
    uint8_t wall[SCRL_STRIP_MAX]; /* 1 = solid wall column */
    int has_amap;
    int has_cdb;
    /*
     * VDP2 back screen, what shows where no plane has a dot. back_mode 0: CRAM entry 0 of the
     * palette given to scrl_map_draw. 1: one colour. 2: a colour per scanline (a gradient), image
     * row y using back_lines[min(y, back_line_count - 1)].
     */
    int back_mode;
    uint8_t back_rgb[3];
    unsigned back_line_count;
    uint8_t back_lines[SCRL_BACK_LINES_MAX][3];
    /*
     * Vertical scene camera (runtime only; 0 by default). A plane taller than the view is drawn
     * from this row down so its authored floor band meets the screen floor instead of its sky
     * (VALLEY/BAT1-3 plane A is 1344..2768 px tall; MAUSO/VILLAGE streets sit low). It is a
     * SEPARATE offset from each plane's base_y (which stays as loaded) and applies in
     * scrl_map_draw only to the world plane (Plane A, index 0) when it is taller than the strip;
     * background planes B/C/D keep vcam 0 whatever their height. 0 leaves the draw byte-identical.
     */
    int camera_y;
    /* Authored bottom surface in plane pixels. Collision uses the full hixel mask. */
    int vcam_on;
    int16_t vfloor[SCRL_STRIP_MAX];
} scrl_map;

/*
 * Bind layer blobs already loaded by caller (pointers not owned) and decode each plane's TIL
 * into its tile cache. The map owns the caches: release them with scrl_map_free. Calling init
 * again on a map that was already initialised leaks them, so free it first.
 */
int scrl_map_init(scrl_map *m,
                  const void *acdb, size_t acdb_sz, const void *atil, size_t atil_sz,
                  const void *bcdb, size_t bcdb_sz, const void *btil, size_t btil_sz,
                  const void *ccdb, size_t ccdb_sz, const void *ctil, size_t ctil_sz,
                  const void *dcdb, size_t dcdb_sz, const void *dtil, size_t dtil_sz,
                  const void *amap, size_t amap_sz);

/* Horizontal scroll of a plane for `camera_px` at `coeff` (16.16): camera_px * coeff >> 16,
 * signed (a negative coeff scrolls the other way). */
int64_t scrl_parallax_offset(unsigned camera_px, int32_t coeff);

/* Per-plane scroll, see scrl_plane. Returns -1 when `plane` is not a plane of the map. */
int scrl_map_set_plane_parallax(scrl_map *m, unsigned plane, int32_t coeff, int32_t base_y);

/* Back screen: one colour, a colour per scanline (`lines` triplets R,G,B, at most
 * SCRL_BACK_LINES_MAX), or back to CRAM entry 0. -1 on bad arguments. */
int scrl_map_set_back_color(scrl_map *m, uint8_t r, uint8_t g, uint8_t b);
int scrl_map_set_back_gradient(scrl_map *m, const uint8_t (*rgb_lines)[3], unsigned lines);
void scrl_map_clear_back(scrl_map *m);

/* Release the tile caches and zero the map. The map must be zeroed or initialised. */
void scrl_map_free(scrl_map *m);

/* Planes whose tile cache was built (the rest are drawn as nothing). */
unsigned scrl_map_cached_planes(const scrl_map *m);

/* Parse CDB header alone (tests / diagnostics). */
int scrl_cdb_parse(const uint8_t *cdb, size_t sz, scrl_plane *out);

/*
 * _DecompByte@0x002a0b4c (compress.o): MSB-first control byte; bit=0 literal,
 * bit=1 BE16 [dist:12 | nibble:4] where nibble=0 terminates and nibble=1..15
 * copies (nibble+1) bytes from dst - dist. Returns bytes written to dst. This is
 * scrl_decomp_byte_ex (core/scrl_decomp.h) without its report.
 */
size_t scrl_decomp_byte(const uint8_t *src, size_t src_cap,
                        uint8_t *dst, size_t dst_cap);

/* Build floor/wall tables (fallback surface profile). */
void scrl_map_build_collision(scrl_map *m, unsigned strip_w);

/* DrawMap stand-in: compose planes into an RGBA image (parallax by plane index).
 * `palette_rgba1024` is 1024 CRAM entries × 4 bytes {unused,R,G,B} (4096 bytes): cell
 * descriptors select bank 0..3 so the colour index is (bank<<8)|dot. The image shows the world
 * from column `camera_px` on: plane N at image column x is plane column x plus
 * scrl_parallax_offset of the camera at the plane's parallax_coeff (plane A follows the camera
 * one to one by default), shifted down by its base_y. The back screen is CRAM entry 0 unless a
 * colour or gradient was set (black when entry 0 is zero); an opaque dot replaces what is behind
 * it, dot 0 and missing blocks do not. */
int scrl_map_draw(const scrl_map *m, const uint8_t *palette_rgba1024,
                  uint8_t *strip_rgba, unsigned strip_w, unsigned strip_h,
                  unsigned camera_px);
/* Optional strip_w * strip_h output. A nonzero byte denotes a nontransparent
 * map A dot at that screen pixel, including dots whose palette entry is black. */
int scrl_map_draw_with_front_mask(const scrl_map *m, const uint8_t *palette_rgba1024,
                                  uint8_t *strip_rgba, unsigned strip_w, unsigned strip_h,
                                  unsigned camera_px, uint8_t *front_mask);

/* SCRLCOLLGetHixel3D / findfloor stand-in. */
int scrl_map_get_hixel(const scrl_map *m, int px, int py);
int scrl_map_find_floor(const scrl_map *m, int px); /* pixel Y or -1 */
int scrl_map_wall_at(const scrl_map *m, int px);

#endif
