#ifndef MB_SCRL_TILECACHE_H
#define MB_SCRL_TILECACHE_H
#include <stddef.h>
#include <stdint.h>

/*
 * Decoded-tile cache of one side-scroll plane: every 32x32 block of its TIL expanded to
 * indexed pixels, so a renderer reads tile art instead of the compressed stream.
 *
 * Inputs, as the project already reads them (menu_phase decode_menu_plane, scrl_map):
 *
 *   CDB   10-byte BE16 header (pix_w, pix_h, a, b, n), then a payload of raw 8x8 characters
 *         of 64 bytes (8 bits per dot, 8 rows of 8). The payload is addressed by byte offset.
 *   TIL   a sequence of block records that tile the file exactly. A record is
 *           +0   BE16 size     bytes in this record, header included (>= 72)
 *           +2   BE16 count    characters carried by this record, 0..16
 *           +4   BE16 id       block number in the plane grid, row by row
 *           +6   BE16          not interpreted
 *           +8   16 x BE32     cell descriptors, a 4x4 grid of 8x8 cells, row by row
 *           +72  payload       packed form (DecompTile when map[+0x20] is NULL)
 *           +136 payload       heap form (DecompTile when map[+0x20] is set); +72..+135 collision hixels
 *         The build votes per file: each compressed record is tried at +72 and, when the
 *         record is long enough, at +136; the offset with more clean decodes wins for the
 *         whole TIL (MAUSO plane A is heap; MENU and MAUSO B/C/D are packed). A descriptor is
 *         a Saturn VDP2-style 2-word pattern name plus one game bit:
 *           bits 15..0     character byte-offset / 32
 *           bits 21..20    palette bank 0..3 (CRAM index = bank<<8 | dot, 256-color cells)
 *           bit  27        1 → character comes from the CDB payload, else from this record
 *           bit  30        H-Flip (left/right invert of the 8×8 cell)
 *           bit  31        V-Flip (upside-down invert of the 8×8 cell)
 *         Any other high bit is OR'd into `ignored_bits`.
 *
 * The grid is ceil(width / 32) by ceil(height / 32) blocks, which for a 1176x608 plane is
 * 37 x 19 = 703. A block without a record is missing (transparent). A block is "empty" when
 * all its dots are 0.
 *
 * Two kinds of problem, treated differently. The record structure (sizes, ids, one record per
 * block, exact tiling of the file) decides whether the TIL is in this format at all: if it does
 * not walk cleanly the build fails and the plane draws nothing, so noise from a file in some
 * other format can never reach the screen. A record with size 0 after at least one accepted
 * record is an end sentinel: the build keeps what it has and ignores the rest (VALLEY A).
 * Inside a sound structure, a record whose characters do not decompress to exactly count * 64
 * bytes, without a match the format forbids, is dropped: its block stays missing (transparent)
 * and is counted in `lz_failed_blocks`. A cell whose descriptor points outside its source is
 * left at 0 (transparent) and counted in `bad_cells`.
 *
 * Dot value 0 is treated as transparent by the renderer (the VDP2 default for a character
 * dot); the palette bank a plane uses is not modelled.
 *
 * Status: written against synthetic fixtures only. It has not been compared with the
 * original routine's output on game data.
 */

#define SCRL_TILE_PX 32u
#define SCRL_TILE_BLOCK_BYTES (SCRL_TILE_PX * SCRL_TILE_PX)
#define SCRL_TILE_CHAR_BYTES 64u
#define SCRL_TILE_CHARS_MAX 16u
#define SCRL_TILE_RECORD_HDR 72u   /* packed payload offset (+0x48), DecompTile no-heap */
#define SCRL_TILE_HEAP_PAYLOAD 136u /* heap payload offset (+0x88), DecompTile heap mode */
#define SCRL_CDB_HEADER_BYTES 10u
#define SCRL_TILE_BLOCKS_MAX 65536u /* what scrl_cdb_parse allows: 16384 x 4096 px, 64 MB */
#define SCRL_DESC_CDB_FLAG 0x08000000u
#define SCRL_DESC_CHAR_MASK 0x0000FFFFu
#define SCRL_DESC_PAL_MASK 0x00300000u /* bits 21..20: palette bank 0..3 */
#define SCRL_DESC_PAL_SHIFT 20u
#define SCRL_DESC_HFLIP 0x40000000u /* bit 30: VDP2 left/right invert */
#define SCRL_DESC_VFLIP 0x80000000u /* bit 31: VDP2 upside-down invert */
#define SCRL_DESC_KNOWN_MASK                                                               \
    (SCRL_DESC_CHAR_MASK | SCRL_DESC_CDB_FLAG | SCRL_DESC_PAL_MASK | SCRL_DESC_HFLIP |       \
     SCRL_DESC_VFLIP)

typedef enum {
    SCRL_TC_NONE = 0, /* never built, or freed: a zeroed struct is this */
    SCRL_TC_OK,
    SCRL_TC_ARG,      /* NULL / empty input, or a CDB without its header */
    SCRL_TC_GRID,     /* the plane has no blocks or more than SCRL_TILE_BLOCKS_MAX */
    SCRL_TC_RECORD,   /* a record is truncated, too small, too big, count > 16, id off the grid */
    SCRL_TC_DUP,      /* two records for the same block */
    SCRL_TC_TRAILING, /* bytes left over that do not form a record */
    SCRL_TC_NOMEM
} scrl_tc_status;

/*
 * The first problem a build met: the record that made it fail, or the first record whose
 * characters did not decompress. Zeroed when there was none. Numbers only, for diagnostics.
 */
typedef struct {
    unsigned record;         /* 0-based index of the record */
    unsigned count, size;    /* its count and size fields, once they were read */
    size_t lz_got, lz_want;  /* decompression: bytes the stream produced, bytes it had to */
    unsigned lz_bad_matches; /* decompression: matches the format forbids */
    unsigned lz_terminated;  /* decompression: 1 when the stream ended on its terminator */
} scrl_tc_fail;

typedef struct {
    uint8_t bits[64];
    uint8_t valid;
} scrl_hixel_block;

typedef struct {
    scrl_hixel_block *hixels; /* Heap records only, independent of graphics decode. */
    uint8_t *pixels;    /* cols * rows blocks of 32x32 character dots, row by row; owned */
    uint8_t *present;   /* per block: 1 when it has a usable record (2: dropped); owned */
    uint8_t *cell_bank; /* per cell (blocks * 16): palette bank 0..3 from the descriptor; owned */
    unsigned cols, rows;
    unsigned width_px, height_px;
    unsigned present_blocks;    /* blocks with a record */
    unsigned compressed_blocks; /* of those, with count > 0 */
    unsigned empty_blocks;      /* of those, with every dot 0 */
    unsigned bad_cells;         /* descriptors that pointed outside their source */
    unsigned lz_failed_blocks;  /* records dropped because their characters did not decompress */
    unsigned heap_mode;         /* 1 when the winning payload offset was SCRL_TILE_HEAP_PAYLOAD */
    uint32_t ignored_bits;      /* OR of descriptor bits the cache does not interpret */
    scrl_tc_status status;      /* SCRL_TC_OK when the cache is usable */
    scrl_tc_fail fail;          /* the first problem, see scrl_tc_fail */
} scrl_tile_cache;

/*
 * Build the cache. On any failure the cache is released and zeroed, with `status` set to the
 * reason, and the same value is returned. Safe to call on an uninitialised struct. A
 * previous cache in `c` is NOT released: call scrl_tilecache_free first.
 */
scrl_tc_status scrl_tilecache_build(scrl_tile_cache *c, unsigned width_px, unsigned height_px,
                                    const uint8_t *cdb, size_t cdb_size, const uint8_t *til,
                                    size_t til_size);

/* Release the buffers and zero the struct. Safe on a zeroed or already freed cache. */
void scrl_tilecache_free(scrl_tile_cache *c);

/* Character dot (0..255) at plane pixel (x, y), or -1 when the cache is unusable, the pixel is
 * outside the plane, or its block is missing. 0 is a valid result: a transparent dot. */
int scrl_tilecache_hixel(const scrl_tile_cache *c, unsigned x, unsigned y);
/* Signed displacement returned by SCRLCOLLfindfloor for a map-space probe. */
int scrl_tilecache_floor_offset(const scrl_tile_cache *cache, int x, int y);

int scrl_tilecache_pixel(const scrl_tile_cache *c, unsigned x, unsigned y);

/* CRAM colour index for a 256-colour cell: (palette_bank << 8) | character_dot. Returns -1 when
 * the pixel is missing, 0 when the character dot is transparent. Bank comes from the cell's
 * descriptor (bits 21..20). Index fits in a 1024-entry CRAM (Saturn mode 0/2). */
int scrl_tilecache_cram(const scrl_tile_cache *c, unsigned x, unsigned y);

const char *scrl_tilecache_status_name(scrl_tc_status status);

#endif
