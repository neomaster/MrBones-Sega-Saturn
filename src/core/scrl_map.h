#ifndef MB_SCRL_MAP_H
#define MB_SCRL_MAP_H
#include <stddef.h>
#include <stdint.h>

/*
 * §23 Host stand-in for sidescrl.o LoadCDB/LoadMap/DrawMap + scrlcoll.o
 * SCRLCOLLGetHixel3D / findfloor / pFloorHixels over 07ACDB..14DTIL (+06AMAP).
 * MAP refs (MAUSO): LoadCDB@2a1d38 LoadMap@2a1c78 DrawMap@2a19f4
 *   GetHixel3D@2a2ee8 findfloor@2a2f0c InitGetFloor@2a2e6c pFloorHixels COMMON.
 * CDB BE16 header: A-style [pix_w][pix_h][…] or B-style [flags][…][tw][th][n].
 * AMAP: BE16 (x,y) polyline → floor (y>>8 when y>224).
 */

#define SCRL_PLANE_MAX 4u
#define SCRL_STRIP_MAX 2048u
#define SCRL_AMAP_MAX 512u
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
} scrl_plane;

typedef struct {
    scrl_plane planes[SCRL_PLANE_MAX]; /* A,B,C,D */
    unsigned plane_count;
    /* Authored floor polyline (06AMAP) → InitGetFloor stand-in. */
    unsigned amap_n;
    uint16_t amap_x[SCRL_AMAP_MAX];
    uint8_t amap_py[SCRL_AMAP_MAX];
    /* pFloorHixels / wall columns across strip. */
    unsigned strip_w;
    uint8_t floor_py[SCRL_STRIP_MAX];
    uint8_t wall[SCRL_STRIP_MAX]; /* 1 = solid wall column */
    int has_amap;
    int has_cdb;
} scrl_map;

/* Bind layer blobs already loaded by caller (pointers not owned). */
int scrl_map_init(scrl_map *m,
                  const void *acdb, size_t acdb_sz, const void *atil, size_t atil_sz,
                  const void *bcdb, size_t bcdb_sz, const void *btil, size_t btil_sz,
                  const void *ccdb, size_t ccdb_sz, const void *ctil, size_t ctil_sz,
                  const void *dcdb, size_t dcdb_sz, const void *dtil, size_t dtil_sz,
                  const void *amap, size_t amap_sz);

/* Parse CDB header alone (tests / diagnostics). */
int scrl_cdb_parse(const uint8_t *cdb, size_t sz, scrl_plane *out);

/*
 * _DecompByte@0x002a0b4c (compress.o): MSB-first control byte; bit=0 literal,
 * bit=1 BE16 [dist:12 | nibble:4] where nibble=0 terminates and nibble=1..15
 * copies (nibble+1) bytes from dst - dist. Returns bytes written to dst.
 */
size_t scrl_decomp_byte(const uint8_t *src, size_t src_cap,
                        uint8_t *dst, size_t dst_cap);

/* Build floor/wall tables (SCRLCOLLInitGetFloor + AMAP). */
void scrl_map_build_collision(scrl_map *m, unsigned strip_w);

/* DrawMap stand-in: compose planes into RGBA strip (parallax by plane index). */
int scrl_map_draw(const scrl_map *m, const uint8_t *palette_rgba1024,
                  uint8_t *strip_rgba, unsigned strip_w, unsigned strip_h,
                  unsigned camera_px);

/* SCRLCOLLGetHixel3D / findfloor stand-in. */
int scrl_map_get_hixel(const scrl_map *m, int px, int py);
int scrl_map_find_floor(const scrl_map *m, int px); /* pixel Y or -1 */
int scrl_map_wall_at(const scrl_map *m, int px);

#endif
