#include "sidescrl_phase.h"
#include "core/runway_projection.h"
#include "core/mb_font.h"
#include "core/pan_palette.h"
#include "core/resource.h"
#include "core/runway_compose.h"
#include "core/runway_sfx.h"
#include "core/sprite_decode.h"
#include "core/stage_enemy.h"
#include "core/stxtr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCR_W 320u
#define SCR_H 224u
#define UNITS_PER_PIXEL 2341u
#define START_PX 40
#define START_X ((uint32_t)START_PX * UNITS_PER_PIXEL)
#define FINISH_X (START_X + 120u * UNITS_PER_PIXEL)
#define GROUND_Y (180u * UNITS_PER_PIXEL)
#define SCROLL_STRIP_W 640u
/* The centre column of the actor box and its feet, relative to the box origin. */
#define ACTOR_HALF_W (SIDESCRL_ACTOR_WIDTH / 2)
#define ACTOR_FEET SIDESCRL_STAND_HEIGHT
/* Ground the centre column is followed over per frame; see sidescrl_motion.h. */
#define MAX_CLIMB_STEP 6
/* Whole pixels kept between the actor's centre and the left edge of the world. */
#define EDGE_MARGIN 4
/* Pad bits, as saturn_pad reports them. */
#define PAD_RIGHT 0x8000u
#define PAD_LEFT 0x4000u
#define PAD_DOWN 0x2000u
#define PAD_UP 0x1000u
#define PAD_A 0x0400u
#define PAD_C 0x0200u
#define PAD_JUMP 0x0100u
#define PAD_START 0x0800u
#define PAD_X 0x0040u

static const char *k_layer_names[] = {
    "07ACDB", "08ATIL", "09BCDB", "10BTIL",
    "11CCDB", "12CTIL", "13DCDB", "14DTIL", NULL
};



static void build_palette(const uint8_t *code, size_t code_size,
                          const uint8_t *title, size_t title_size,
                          uint8_t *palette) {
    uint32_t words[1024], tmp[1024];
    size_t off, consumed, best = 0, i;
    const uint8_t *src[2];
    size_t src_sz[2];
    int s;
    memset(words, 0, sizeof words);
    src[0] = code;
    src_sz[0] = code_size;
    src[1] = title;
    src_sz[1] = title_size;
    for (s = 0; s < 2; s++) {
        if (!src[s] || src_sz[s] < 2048) continue;
        for (off = 0; off + 2048 <= src_sz[s] && off < 0x80000; off += 4) {
            if (src[s][off] != 0 || src[s][off + 1] != 0 ||
                src[s][off + 2] >= 4 || src[s][off + 4] != 1)
                continue;
            consumed = 0;
            memset(tmp, 0, sizeof tmp);
            if (pan_palette_apply(src[s] + off, src_sz[s] - off, tmp, 1024, &consumed))
                continue;
            if (consumed >= 2048 && consumed > best) {
                best = consumed;
                memcpy(words, tmp, sizeof words);
                if (best >= 4100) break;
            }
        }
        if (best >= 4100) break;
    }
    if (best < 64) {
        for (s = 0; s < 2; s++) {
            if (!src[s] || src_sz[s] < 16) continue;
            for (off = 0; off + 16 < src_sz[s] && off < 0x80000; off += 4) {
                consumed = 0;
                memset(tmp, 0, sizeof tmp);
                if (pan_palette_apply(src[s] + off, src_sz[s] - off, tmp, 1024, &consumed))
                    continue;
                if (consumed > best && consumed >= 64) {
                    best = consumed;
                    memcpy(words, tmp, sizeof words);
                    if (best >= 200) break;
                }
            }
            if (best >= 200) break;
        }
    }
    if (best < 64) {
        for (i = 0; i < 256; i++)
            words[i] = (uint32_t)((i << 16) | (i << 8) | i);
    }
    for (i = 0; i < 1024; i++) {
        palette[4 * i + 0] = 0;
        palette[4 * i + 1] = (uint8_t)words[i];
        palette[4 * i + 2] = (uint8_t)(words[i] >> 8);
        palette[4 * i + 3] = (uint8_t)(words[i] >> 16);
    }
}

/*
 * 15GLASS Plane B is 100% opaque and reads CRAM[513..638] (bank 2 lower half), which the base
 * 03CODE palette stream at 0x64df0 leaves (0,0,0) — the screen goes pitch black behind sprites.
 * /15GLASS/03CODE stores the glass/nebula sub-palettes at 0x65f18..0x66d34 as a self-indexed
 * _MainPalette stream (same format pan_palette_apply reads); merge it so bank2[1..126] is lit.
 * Conservative: GLASS only, indices 513..638 only, and pan_palette_apply leaves the destination
 * untouched on a bad decode, so a wrong offset is a silent no-op rather than corruption.
 * NEEDS DISC VALIDATION — the 0x65f18 offset/encoding is documented, not verified against the disc.
 */
static void merge_glass_subpalette(sidescrl_phase *phase) {
    static const size_t k_off = 0x65f18u, k_end = 0x66d34u;
    const uint8_t *code = (const uint8_t *)phase->code;
    uint32_t words[1024];
    size_t off, i;
    int merged = 0;
    if (!code || strcmp(phase->stage_name, "GLASS") != 0) return;
    /* Seed from the current palette so an apply only overwrites the indices its stream names. */
    for (i = 0; i < 1024u; i++)
        words[i] = ((uint32_t)phase->palette[4 * i + 3] << 16) |
                   ((uint32_t)phase->palette[4 * i + 2] << 8) |
                   (uint32_t)phase->palette[4 * i + 1];
    for (off = k_off; off + 8u <= phase->code_size && off <= k_end; off += 4u) {
        uint32_t seed[1024];
        size_t consumed = 0;
        size_t start = ((size_t)code[off] << 24) | ((size_t)code[off + 1] << 16) |
                       ((size_t)code[off + 2] << 8) | code[off + 3];
        if (start < 513u || start > 638u) continue; /* stream must target bank 2 lower half */
        memcpy(seed, words, sizeof seed);
        if (pan_palette_apply(code + off, phase->code_size - off, seed, 1024, &consumed) == 0 &&
            consumed >= 8u) {
            memcpy(words, seed, sizeof words);
            merged = 1;
            break;
        }
    }
    if (!merged) return;
    for (i = 513u; i <= 638u; i++) {
        phase->palette[4 * i + 0] = 0;
        phase->palette[4 * i + 1] = (uint8_t)words[i];
        phase->palette[4 * i + 2] = (uint8_t)(words[i] >> 8);
        phase->palette[4 * i + 3] = (uint8_t)(words[i] >> 16);
    }
}

static int decode_stxtr(const void *textures, size_t textures_size, size_t *offset,
                        const uint8_t *palette, uint8_t **out, unsigned *w,
                        unsigned *h) {
    static const uint16_t k_pmods[] = {0x0020, 0x0028, 0x00a0, 0x00a8, 0};
    stxtr_record rec;
    size_t size;
    uint8_t *pixels;
    unsigned i;
    if (!textures || !offset) return -1;
    if (stxtr_next(textures, textures_size, offset, &rec) != 1) return -1;
    size = (size_t)rec.width * rec.height * 4u;
    pixels = malloc(size);
    if (!pixels) return -1;
    for (i = 0; k_pmods[i]; i++) {
        if (mb_sprite_decode(rec.data, rec.size, rec.width, rec.height, k_pmods[i],
                             palette, 1024, pixels, size) == 0) {
            
            *out = pixels;
            *w = rec.width;
            *h = rec.height;
            return 0;
        }
    }
    free(pixels);
    return -1;
}



static const sidescrl_layer *find_layer(const sidescrl_phase *phase, const char *name) {
    unsigned i;
    for (i = 0; i < phase->layer_count; i++)
        if (strcmp(phase->layers[i].name, name) == 0)
            return &phase->layers[i];
    return NULL;
}

static const void *layer_data(const sidescrl_phase *phase, const char *name, size_t *sz) {
    const sidescrl_layer *L = find_layer(phase, name);
    if (!L) {
        *sz = 0;
        return NULL;
    }
    *sz = L->size;
    return L->data;
}

static int plane_slot_from_cdb_name(const char *cdb_name) {
    if (!cdb_name) return -1;
    if (strcmp(cdb_name, "07ACDB") == 0) return 0;
    if (strcmp(cdb_name, "09BCDB") == 0) return 1;
    if (strcmp(cdb_name, "11CCDB") == 0) return 2;
    if (strcmp(cdb_name, "13DCDB") == 0) return 3;
    return -1;
}

/*
 * Horizontal parallax coefficients (16.16 fixed point, MapDef+0x34 in bkgdata) from Disc 1/2
 * stage 03CODE binaries. base_y stays 0 until vertical camera tracking is modeled.
 */
static void apply_stage_parallax(sidescrl_phase *phase) {
    static const struct {
        const char *stage;
        int32_t coeff[SCRL_PLANE_MAX];
        int32_t base_y[SCRL_PLANE_MAX];
    } k_stage_parallax[] = {
        { "MAUSO",   { 0x10000, 0x06000, 0x00000, 0x10000 }, { 0, -90, 0, -100 } },
        { "VALLEY",  { 0x10000, 0x06000, 0x03000, 0x10000 }, { 0, 0, 0, 0 } },
        { "BAT1",    { 0x10000, 0x03000, 0x01000, 0x00000 }, { 0, 0, 0, 0 } },
        { "BAT2",    { 0x10000, 0x03000, 0x01000, 0x00000 }, { 0, 0, 0, 0 } },
        { "BAT3",    { 0x10000, 0x03000, 0x01000, 0x00000 }, { 0, 0, 0, 0 } },
        { "GLASS",   { 0x10000, 0x00000, 0x00000, 0x10000 }, { 0, 0, 0, 0 } },
        { "VORTEX",  { 0x00000, 0x00000, 0x00000, 0x00000 }, { 0, 0, 0, 0 } },
        { "VILLAGE", { 0x10000, 0x0C000, 0x08000, 0x14000 }, { 0, 0, 0, 0 } },
        { "SHADOW",  { 0x10000, 0x10000, 0x10000, 0x10000 }, { 0, 0, 0, 0 } },
        { "LAFF",    { 0x10000, 0x10000, 0x10000, 0x10000 }, { 0, 0, 0, 0 } },
    };
    unsigned s, i;
    if (!phase) return;
    for (s = 0; s < sizeof k_stage_parallax / sizeof k_stage_parallax[0]; s++) {
        if (strcmp(phase->stage_name, k_stage_parallax[s].stage) != 0) continue;
        for (i = 0; i < phase->map.plane_count && i < SCRL_PLANE_MAX; i++) {
            int slot = plane_slot_from_cdb_name(phase->map.planes[i].cdb_name);
            if (slot >= 0) {
                (void)scrl_map_set_plane_parallax(&phase->map, i,
                                                  k_stage_parallax[s].coeff[slot],
                                                  k_stage_parallax[s].base_y[slot]);
                /* Tile vertically only when the plane actually covers the screen height and sits
                 * unshifted; a short plane (pixel_h < SCR_H) or any base_y offset must not wrap
                 * (keeps MAUSO B/D, which carry a negative base_y). */
                phase->map.planes[i].no_wrap_y =
                    (phase->map.planes[i].pixel_h < SCR_H) ||
                    (k_stage_parallax[s].base_y[slot] != 0);
            }
        }
        return;
    }
}

/* Camera_y eases at most this many rows per frame so the vertical anchor never pops. */
#define CAMERA_Y_EASE 3

/*
 * D3: bottom-most populated plane-A pixel row inside world columns [x0, x0+w), scanning from the
 * base of the plane up and stopping at the first row that carries a non-transparent dot. Columns
 * are sampled on a 16 px stride (terrain floor bands are far wider than that) to bound the cost of
 * the empty rows above a sunken strip. Returns -1 when the cache is unusable or the window is bare.
 */
static int plane_a_bottom_py(const scrl_tile_cache *c, unsigned x0, unsigned w) {
    int y;
    if (!c || c->status != SCRL_TC_OK || c->height_px == 0u) return -1;
    for (y = (int)c->height_px - 1; y >= 0; y--) {
        unsigned x;
        for (x = x0; x < x0 + w; x += 16u) {
            if (x >= c->width_px) break;
            if (scrl_tilecache_pixel(c, x, (unsigned)y) > 0)
                return y;
        }
    }
    return -1;
}

/*
 * D3: the camera_y that seats plane A's populated floor band at the base of the screen for the view
 * starting at world column src_x. Short planes (pixel_h <= SCR_H) and plane-less rigs keep 0, which
 * leaves scrl_map_draw byte-identical. A bare window falls back to the full-height anchor
 * (pixel_h - SCR_H). This is a rendering offset only; base_y, floor_py, bones_y and collision are
 * never touched here.
 */
static int camera_y_target(const sidescrl_phase *phase, unsigned src_x) {
    const scrl_plane *A;
    int bottom, target, max_cy;
    if (phase->map.plane_count == 0u) return 0;
    A = &phase->map.planes[0];
    if (A->pixel_h <= SCR_H) return 0;
    max_cy = (int)(A->pixel_h - SCR_H);
    bottom = plane_a_bottom_py(&A->tiles, src_x, SCR_W);
    if (bottom < 0) return max_cy;
    target = bottom - (int)SCR_H; /* VALLEY x0..735: 1247 - 224 = 1023 */
    if (target < 0) target = 0;
    if (target > max_cy) target = max_cy;
    return target;
}

/* SCRLCOLLInitGetFloor scans up from the bottom to the first free hixel.
 * Keep the collision rows in map pixels; only the render subtracts camera_y. */
static void build_hixel_floor(scrl_map *map) {
    const scrl_tile_cache *tiles = &map->planes[0].tiles;
    if (!tiles->hixels || strcmp(map->planes[0].cdb_name, "07ACDB")) return;
    for (unsigned x = 0; x < map->strip_w; x++) {
        int y = (int)tiles->height_px - 1;
        while (y >= 0 && scrl_tilecache_hixel(tiles, x, (unsigned)y) != 0) y -= 2;
        int floor = y + 1;
        int camera = floor - (int)SCR_H / 2;
        int max_camera = (int)tiles->height_px - (int)SCR_H;
        if (camera > max_camera) camera = max_camera;
        if (camera < 0) camera = 0;
        map->vfloor[x] = (int16_t)floor;
        map->floor_py[x] = floor >= (int)tiles->height_px || x >= tiles->width_px
                            ? SCRL_FLOOR_PIT : (uint8_t)(floor - camera);
        map->wall[x] = 0;
    }
    map->vcam_on = 1;
}

static uint32_t layout_word(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static const uint8_t *overlay_at(const sidescrl_phase *p, uint32_t address, size_t n) {
    if (address < 0x232000u || n > p->code_size || address - 0x232000u > p->code_size - n)
        return NULL;
    return (const uint8_t *)p->code + (address - 0x232000u);
}

/* Stand_Brain (0x2ccc70) passes this clip to BonesWalk. Mnext and Mloop
 * advance the twelve root records on every fourth update. */
#define VALLEY_WALK_POSES 12u
#define VALLEY_WALK_TICKS (4u * VALLEY_WALK_POSES)
static int load_valley_walk(sidescrl_phase *p) {
    const uint8_t *cfg = overlay_at(p, p->initial.config, 8);
    const uint8_t *stand = cfg ? overlay_at(p, layout_word(cfg + 4), 20) : NULL;
    const uint8_t *motion = stand ? overlay_at(p, layout_word(stand + 16), 4) : NULL;
    const uint8_t *tracks = motion ? overlay_at(p, layout_word(motion), 4) : NULL;
    const uint8_t *root = tracks ? overlay_at(p, layout_word(tracks), 12 * VALLEY_WALK_POSES) : NULL;
    if (!root) return -1;
    for (unsigned i = 0; i < VALLEY_WALK_POSES; i++) {
        uint32_t callback = layout_word(root + 12 * i + 8);
        if (callback != (i + 1 == VALLEY_WALK_POSES ? 0x2c9d08u : 0x2c9ce4u)) return -1;
    }
    p->walk_root = root;
    return 0;
}

/* BonesSetConfig, including the floor probe and the latch constructor's hand offset. */
static int bat_configure(sidescrl_phase *p, uint32_t address) {
    bones_stage_init body = p->initial;
    const uint8_t *cfg = overlay_at(p, address, 112);
    if (!cfg || bones_read_config(p->code, p->code_size, address, &body)) return -1;
    const uint8_t *latch = overlay_at(p, layout_word(cfg + 12), 64);
    if (!latch) return -1;
    int floor = (int32_t)layout_word(cfg + 76);
    int64_t hand = (int64_t)(int32_t)layout_word(cfg + 72) + (int32_t)layout_word(latch + 60);
    if (floor < -224 || floor > 224 || hand < -224 || hand > 224) return -1;
    p->body_config = address;
    p->floor_delta = floor;
    p->hand_delta = (int)hand;
    p->actor.hit_width = body.hit.top_right_x - body.hit.top_left_x;
    p->actor.hit_height = body.hit.bot_right_y - body.hit.top_right_y;
    p->actor.jump_impulse = body.jump_impulse;
    p->actor.boost_duration = body.boost_ticks;
    return 0;
}

uint32_t batwood_piece(const sidescrl_phase *p, unsigned mask) {
    const uint8_t *types = overlay_at(p, p->piece_table, 48);
    if (!types || !mask) return 0;
    unsigned count = 0;
    for (unsigned i = 0; i < 6; i++) count += (mask & layout_word(types + 8*i + 4)) != 0;
    if (!count) return 0;
    unsigned index = (p->frames / 100u) % count;
    for (unsigned i = 0; i < 6; i++)
        if ((mask & layout_word(types + 8*i + 4)) && index-- == 0) return layout_word(types + 8*i);
    return 0;
}

static void bat_collect(sidescrl_phase *p, const saturn_pad *pad) {
    if (pad->pressed & PAD_A) {
        p->skel.pump += 0x30a3;
        if (p->skel.pump > 65536) p->skel.pump = 65536;
    } else if (p->skel.pump > 0) {
        p->skel.pump -= 131 + p->skel.pump / 64;
        if (p->skel.pump < 0) p->skel.pump = 0;
    }
    int x = (p->actor.x + ACTOR_HALF_W) * 256;
    int y = (p->actor.y + ACTOR_FEET - p->floor_delta) * 256;
    int scale = (int)p->projection_px;
    int range = 3 * (p->skel.pump >> 8);
    const uint8_t *row = NULL;
    for (unsigned j = 0; j < 32; j++) {
        row = overlay_at(p, p->config_table + j*40, 40);
        if (!row || !layout_word(row)) return;
        if (layout_word(row) == p->body_config) break;
        row = NULL;
    }
    if (!row) return;
    for (unsigned i = 0; i < p->body_piece_count; i++) {
        if (p->collected & (1u << i)) continue;
        struct batwood_pickup *bone = &p->body_pieces[i];
        if (bone->falling) {
            const scrl_tile_cache *tiles = &p->map.planes[0].tiles;
            int row = bone->y / 256;
            if (row >= (int)tiles->height_px) continue;
            bone->rise -= 0x44d;
            if (bone->rise < -0x3333) bone->rise = -0x3333;
            int next_y = bone->y - bone->rise * scale / 256;
            int floor = row + scrl_tilecache_floor_offset(tiles, bone->x / 256, row);
            if (bone->rise < 0 && next_y / 256 >= floor) {
                next_y = floor * 256;
                bone->falling = 0;
            }
            bone->y = next_y;
        }
        int dx = x - bone->x, dy = y - bone->y;
        int64_t ax = dx / scale, ay = dy / scale, dist = ax*ax + ay*ay;
        uint32_t piece = bone->attracted ? bone->attracted : batwood_piece(p, bone->mask);
        unsigned edge = 16;
        while (edge <= 28 && layout_word(row + edge) != piece) edge += 12;
        if (!piece || edge > 28) {
            if (bone->attracted) bone->falling = 1;
            bone->attracted = 0;
            continue;
        }
        if (!bone->attracted) {
            if (p->latch != BAT_FREE || dist >= range*range) continue;
            bone->attracted = piece;
            bone->falling = 0;
            bone->rise = 0x3333;
            bone->grace = 20;
        }
        if (bone->rise > -0x28f) {
            bone->rise -= 0x44d;
            bone->y -= bone->rise * scale / 256;
            int vx = dx / 32, limit = 0x3333 * scale / 256;
            bone->x += vx < -limit ? -limit : vx > limit ? limit : vx;
            continue;
        }
        if (bone->grace) bone->grace--;
        else if (dist > range*range + 0x4000) {
            bone->attracted = 0;
            bone->falling = 1;
            continue;
        }
        if (dist > 0x28a4 || p->latch != BAT_FREE) {
            int vx = dx / 8, vy = dy / 8, limit = 0x1999 * scale / 256;
            bone->x += vx < -limit ? -limit : vx > limit ? limit : vx;
            bone->y += vy < -limit ? -limit : vy > limit ? limit : vy;
            continue;
        }
        int old_floor = p->floor_delta;
        if (bat_configure(p, layout_word(row + edge + 8))) return;
        if (!p->actor.on_ground) p->actor.y += p->floor_delta - old_floor;
        p->collected |= 1u << i;
        return;
    }
}

/* Retail _Hit_Generic / _ShatterDemBonesWayWayDown piece detachment via config_table+4..12. */
static void bat_detach_on_hit(sidescrl_phase *p) {
    const uint8_t *row = NULL;
    uint32_t loss1, loss2, nloss;
    unsigned k, slot = 24u;
    int old_floor;
    if (!p || !p->config_table || !p->body_config) return;
    for (unsigned j = 0; j < 32u; j++) {
        const uint8_t *cand = overlay_at(p, p->config_table + j * 40u, 40u);
        if (!cand || !layout_word(cand)) return;
        if (layout_word(cand) == p->body_config) {
            row = cand;
            break;
        }
    }
    if (!row) return;
    loss1 = layout_word(row + 4);
    loss2 = layout_word(row + 8);
    nloss = layout_word(row + 12);
    if (!loss1 || !loss2 || !nloss || loss1 == p->body_config) return;
    for (k = 0; k < p->body_piece_count; k++) {
        if (p->collected & (1u << k)) {
            slot = k;
            break;
        }
    }
    if (slot == 24u && p->body_piece_count < sizeof p->body_pieces / sizeof p->body_pieces[0])
        slot = p->body_piece_count++;
    if (slot < 24u) {
        p->collected &= ~(1u << slot);
        p->body_pieces[slot].x = (p->actor.x + ACTOR_HALF_W) * 256;
        p->body_pieces[slot].y = (p->actor.y + ACTOR_FEET - p->floor_delta - 16) * 256;
        p->body_pieces[slot].rise = 0x2800;
        p->body_pieces[slot].grace = 16;
        p->body_pieces[slot].falling = 1;
        p->body_pieces[slot].mask = nloss & 63u;
        p->body_pieces[slot].attracted = 0u;
    }
    old_floor = p->floor_delta;
    if (bat_configure(p, loss1) == 0 && !p->actor.on_ground)
        p->actor.y += p->floor_delta - old_floor;
}

static bool bat_climb(sidescrl_phase *p, const saturn_pad *pad) {
    SidescrlActor *a = &p->actor;
    int x = a->x + ACTOR_HALF_W, feet = a->y + ACTOR_FEET;
    const scrl_tile_cache *tiles = &p->map.planes[0].tiles;
    if (!(pad->held & PAD_C)) {
        p->latch = BAT_FREE;
        return false;
    }
    if (p->latch == BAT_FREE) {
        if (a->on_ground) return false;
        int y = feet - p->floor_delta + p->hand_delta;
        int type = scrl_tilecache_hixel(tiles, (unsigned)x, (unsigned)y);
        if (type != 1 && type != 2) return false;
        do { y -= 2; } while (y >= 0 && scrl_tilecache_hixel(tiles, (unsigned)x, (unsigned)y) == type);
        p->latch_y = y;
        p->latch_type = type;
        p->latch_frame = p->frames;
        p->latch = BAT_HANG;
        p->latch_wait = 8;
        a->y = y - p->hand_delta + p->floor_delta - ACTOR_FEET;
        a->vx = a->vy = a->rem_x = a->rem_y = 0;
        a->jumping = false;
        a->spring_ticks = 0;
        p->on_plat_idx = -1;
    } else if (p->latch_wait) {
        p->latch_wait--;
    } else if (p->latch == BAT_HANG) {
        if ((pad->held & PAD_UP) &&
            scrl_tilecache_hixel(tiles, (unsigned)x, (unsigned)(p->latch_y - 4)) != 3) {
            p->latch = BAT_PULL;
            p->latch_frame = p->frames;
            p->latch_wait = 4;
        } else if (pad->held & (PAD_LEFT | PAD_RIGHT)) {
            int step = pad->held & PAD_RIGHT ? 2 : -2;
            int nx = x + step, ny = p->latch_y;
            for (int row = ny - 2; row <= ny + 4; row += 2) {
                int type = scrl_tilecache_hixel(tiles, (unsigned)nx, (unsigned)row);
                if (type == 3) break;
                if (type != 1 && type != 2) continue;
                a->x += step;
                a->facing_left = step < 0;
                a->y += row - 2 - ny;
                p->latch_y = row - 2;
                break;
            }
        }
    } else {
        int remaining = feet - p->latch_y;
        if (remaining > 0) a->y -= (remaining + 7) / 8;
        if (remaining <= 1) {
            a->y = p->latch_y - ACTOR_FEET;
            a->on_ground = true;
            p->latch = BAT_FREE;
        }
    }
    return true;
}

/* ___9CSpringBrPv at VALLEY 0x2bc830. Formats are Saturn signed 16.16. */
static const struct {
    int reach, low, high, delay;
} spring_types[] = {
    {0xe666, 0x170a, 0x3d70, 4}, {0x15999, 0x1eb8, 0x4ccc, 5},
    {0x19999, 0x3333, 0x6666, 6}, {0x19999, 0x3333, 0x6666, 6}
};

/* CPlatSvrxxYy::Search compares the actor origin with the branch's signed
 * world-space bounds. CSpringBr::PlatformCheck requires downward motion. */
static bool touches_platform(const sidescrl_phase *phase, const sidescrl_plat *pl,
                             int x, int previous_foot, int foot) {
    if (pl->kind != 1)
        return x >= pl->x0 && x <= pl->x1 && previous_foot <= pl->y + 4 && foot >= pl->y;
    if (x <= pl->x0 || x >= pl->x1) return false;
    int origin = foot - (phase->body_config ? phase->floor_delta : phase->initial.hit.bot_right_y);
    int64_t offset = (int64_t)(origin - pl->y) * 65536;
    int scale = (int)phase->projection_px;
    return offset > -0x2666 * scale && offset < 0x8000 * scale;
}

/* Primary layout list in the retail overlay, after link-map relocation. */
static int load_stage_layout(sidescrl_phase *phase) {
    static const struct { const char *stage; uint32_t first, spring, win; } lists[] = {
        {"VALLEY", 0x2eed7c, 0x2bc7e4, 0x2be91c}, {"BAT1", 0x2d3bb0, 0x2c7bc8, 0x2c909c},
        {"BAT2", 0x2d56b0, 0x2c9464, 0x2caba4}, {"BAT3", 0x2d6ed0, 0x2ca7b4, 0x2cc3c0},
        {"VILLAGE", 0x2bfbd0, 0, 0}, {"MAUSO", 0x2c8250, 0, 0},
        {"LAFF", 0x2b89cc, 0, 0}, {"GLASS", 0x2dd18c, 0, 0},
        {"VORTEX", 0x290044, 0, 0}, {"SHADOW", 0x2cb648, 0, 0}
    };
    struct bat_def {
        const char *stage;
        uint32_t config_table, piece_table, bone_ctor, goals_addr;
    };
    static const struct bat_def bat_defs[] = {
        {"BAT1", 0x2d9e04u, 0x2dcf48u, 0x2adba4u, 0x2c91c8u},
        {"BAT2", 0x2db740u, 0x2de884u, 0x2af440u, 0x2cacd0u},
        {"BAT3", 0x2dd9a8u, 0x2e0c94u, 0x2b0524u, 0x2cc4ecu}
    };
    const uint32_t base = 0x232000u;
    for (size_t i = 0; i < sizeof lists / sizeof lists[0]; i++) {
        if (strcmp(phase->stage_name, lists[i].stage)) continue;
        uint32_t address = lists[i].first;
        const struct bat_def *bat_cfg = NULL;
        for (size_t bi = 0; bi < sizeof bat_defs / sizeof bat_defs[0]; bi++) {
            if (!strcmp(phase->stage_name, bat_defs[bi].stage)) {
                bat_cfg = &bat_defs[bi];
                break;
            }
        }
        int is_valley = !strcmp(phase->stage_name, "VALLEY");
        int is_mauso = !strcmp(phase->stage_name, "MAUSO");
        if (bat_cfg) {
            phase->config_table = bat_cfg->config_table;
            phase->piece_table = bat_cfg->piece_table;
            if (!overlay_at(phase, phase->config_table, 16*40) || !overlay_at(phase, phase->piece_table, 48)) return -1;
        }
        while (address) {
            if (address < base || phase->code_size < 28 || address - base > phase->code_size - 28)
                return -1;
            const uint8_t *rec = (const uint8_t *)phase->code + (address - base);
            uint32_t next = layout_word(rec), ctor = layout_word(rec + 4);
            if (next && ((next & 3u) || next < address + 28u)) return -1;
            int64_t x_raw = -(int64_t)(int32_t)layout_word(rec + 12) * phase->projection_px;
            int64_t y_raw = -(int64_t)(int32_t)layout_word(rec + 16) * phase->projection_px;
            int x = (int)((x_raw + 32768) / 65536), y = (int)((y_raw + 32768) / 65536);
            if (bat_cfg && ctor == bat_cfg->bone_ctor) {
                if (!overlay_at(phase, address, 36) || (next && next-address < 36)) return -1;
                unsigned flags = layout_word(rec + 28);
                if ((flags & 0x100) && (flags & 63)) {
                    unsigned j = phase->body_piece_count;
                    if (j >= sizeof phase->body_pieces / sizeof phase->body_pieces[0]) return -1;
                    phase->body_pieces[j].x = x * 256;
                    phase->body_pieces[j].y = y * 256;
                    phase->body_pieces[j].mask = flags & 63;
                    phase->body_piece_count++;
                }
            }
            if (ctor == lists[i].win && x >= 0 && (unsigned)x < phase->scroll_w) {
                phase->finish_x = (unsigned)x * UNITS_PER_PIXEL;
                if (bat_cfg) {
                    if (!overlay_at(phase, address, 32) || (next && next-address < 32)) return -1;
                    unsigned selector = layout_word(rec + 28);
                    const uint8_t *goals = overlay_at(phase, bat_cfg->goals_addr, 12);
                    if (!goals || selector < 1 || selector > 3) return -1;
                    phase->exit_config = layout_word(goals + 4*(selector-1));
                    phase->exit_world_x = (int32_t)layout_word(rec+12);
                    phase->exit_world_y = (int32_t)layout_word(rec+16);
                    phase->finish_y = y;
                }
                if (ctor == 0x2be91cu) { /* CWinBox constructor converts pixel dimensions to world units. */
                    if (phase->code_size - (address - base) < 36u || (next && next - address < 36u))
                        return -1;
                    uint32_t w = layout_word(rec + 28), h = layout_word(rec + 32);
                    if (!w || !h || w > INT16_MAX || h > INT16_MAX) return -1;
                    phase->finish_y = y;
                    phase->finish_width = (int)w;
                    phase->finish_height = (int)h;
                }
            }
            if (is_valley) {
                if (ctor == 0x2c22c0u && phase->fence_count < 4u) {
                    if (!overlay_at(phase, address, 32) || (next && next - address < 32)) return -1;
                    unsigned k = phase->fence_count++;
                    phase->fences[k].x = x;
                    phase->fences[k].y = y;
                    phase->fences[k].id = layout_word(rec + 28);
                } else if (ctor == 0x2c24f8u && phase->fence_key_count < 4u) {
                    if (!overlay_at(phase, address, 32) || (next && next - address < 32)) return -1;
                    unsigned k = phase->fence_key_count++;
                    phase->fence_keys[k].x = x;
                    phase->fence_keys[k].y = y;
                    phase->fence_keys[k].id = layout_word(rec + 28);
                } else if (ctor == 0x2c14ecu && phase->blue_tomb_count < 4u) {
                    unsigned k = phase->blue_tomb_count++;
                    phase->blue_tombs[k].x = x;
                    phase->blue_tombs[k].y = y;
                }
            }
            if (is_mauso) {
                if (ctor == 0x2b2fb4u) { /* CMaestro */
                    if (!overlay_at(phase, address, 40) || (next && next - address < 40)) return -1;
                    int aw = (int)layout_word(rec + 28);
                    int st = (int)layout_word(rec + 36);
                    phase->mauso.enabled = 1;
                    phase->mauso.phase = 1;
                    phase->mauso.arena_x = x;
                    phase->mauso.arena_w = aw > 0 ? aw : 590;
                    phase->mauso.arena_y = y;
                    phase->mauso.left_edge = x;
                    phase->mauso.right_edge = x + phase->mauso.arena_w;
                    phase->mauso.skels_total = st > 0 ? st : 8;
                    phase->mauso.wave_timer = 1u;
                } else if (ctor == 0x2b2728u) { /* CBlueHead */
                    phase->mauso.bh_x = x;
                    phase->mauso.bh_y = y;
                    phase->mauso.bh_active = 1;
                } else if (ctor == 0x2b7a30u) { /* CButFly */
                    phase->mauso.bf_x = x;
                    phase->mauso.bf_y = y;
                    if (x > 0 && (unsigned)x < phase->scroll_w)
                        phase->finish_x = (unsigned)x * UNITS_PER_PIXEL;
                }
            }
            if (!strcmp(phase->stage_name, "VILLAGE")) {
                if (ctor == 0x29c38cu) {
                    if (phase->code_size - (address - base) < 32u || (next && next - address < 32u))
                        return -1;
                    unsigned type = layout_word(rec + 28);
                    /* CHouse::Brain selects HouseStand[type][3] in the initial state. */
                    if (type > 2u || phase->enemies.prop_count == STAGE_PROP_MAX) return -1;
                    const uint32_t origins[] = {0x235234, 0x235344, 0x235454};
                    if (origins[type] - base > phase->code_size - 4) return -1;
                    uint32_t origin = layout_word((const uint8_t *)phase->code + (origins[type] - base));
                    stage_prop *house = &phase->enemies.props[phase->enemies.prop_count++];
                    house->x = x;
                    house->y = y;
                    house->anchor_x = (int16_t)(origin >> 16);
                    house->anchor_y = (int16_t)origin;
                    house->anim = 4u * type + 3u;
                } else if (ctor == 0x29c6f8u && phase->village.spider_count < 12) { /* CDrop spider anchor */
                    if (!overlay_at(phase, address, 32) || (next && next - address < 32)) return -1;
                    int k = phase->village.spider_count++;
                    int drop_raw = (int)layout_word(rec + 28);
                    int drop_px = (drop_raw > 0 && drop_raw <= 220) ? drop_raw : 160;
                    phase->village.enabled = 1;
                    phase->village.spiders[k].x = x;
                    phase->village.spiders[k].top_y = y;
                    phase->village.spiders[k].cur_y = y;
                    phase->village.spiders[k].drop_dist = drop_px;
                    phase->village.spiders[k].state = 0;
                    phase->village.spiders[k].timer = k * 14;
                    phase->village.spiders[k].has_guy = 0;
                } else if (ctor == 0x29cadcu && phase->village.mushroom_count < 8) { /* CMushroom */
                    if (!overlay_at(phase, address, 36) || (next && next - address < 36)) return -1;
                    int mk = phase->village.mushroom_count++;
                    phase->village.mushrooms[mk].x = x;
                    phase->village.mushrooms[mk].y = y;
                    phase->village.mushrooms[mk].taken = 0;
                    phase->village.mushrooms[mk].kind = (int)layout_word(rec + 32);
                } else if (ctor == 0x29a098u) { /* CBlueHead */
                    phase->village.bh_x = x;
                    phase->village.bh_y = y;
                    phase->village.bh_active = 1;
                }
            }
            if (!strcmp(phase->stage_name, "LAFF")) {
                if (ctor == 0x2943fcu) { /* CJunior */
                    phase->laff.enabled = 1;
                    phase->laff.junior_x = x;
                    phase->laff.junior_y = y;
                    phase->laff.last_boss_hp = STAGE_LAFF_BOSS_HP;
                } else if (ctor == 0x294800u && phase->laff.skel_count < 80u) { /* 80 CLaffSkel */
                    unsigned lk = phase->laff.skel_count++;
                    phase->laff.skel_x[lk] = (int16_t)x;
                    phase->laff.skel_y[lk] = (int16_t)y;
                }
            }
            if (!strcmp(phase->stage_name, "GLASS")) {
                phase->glass.enabled = 1;
                phase->glass.total_records++;
                if (ctor == 0x2cf77cu || ctor == 0x2cf5d0u || ctor == 0x2cf30cu ||
                    ctor == 0x2cef38u || ctor == 0x2cfc80u) {
                    uint8_t skind = (ctor == 0x2cf77cu) ? 0u :
                                    (ctor == 0x2cf5d0u) ? 1u :
                                    (ctor == 0x2cf30cu) ? 2u :
                                    (ctor == 0x2cef38u) ? 3u : 4u;
                    if (skind == 0u) phase->glass.safe_count++;
                    else if (skind == 1u) phase->glass.turn_count++;
                    else if (skind == 2u) phase->glass.trap_count++;
                    else if (skind == 3u) phase->glass.shoot_count++;
                    else phase->glass.blue_count++;
                    if (phase->glass.shard_count < 180u) {
                        unsigned si = phase->glass.shard_count++;
                        unsigned sx = (unsigned)((x < 0) ? 0 : (x % 1220));
                        phase->glass.shards[si].x0 = (uint16_t)sx;
                        phase->glass.shards[si].x1 = (uint16_t)(sx + 48u);
                        phase->glass.shards[si].top_y = (uint8_t)(128u + ((unsigned)(y < 0 ? -y : y) % 30u));
                        phase->glass.shards[si].kind = skind;
                    }
                } else if (ctor == 0x2d0024u) { /* CKeyHole */
                    phase->glass.keyhole_x = x;
                    phase->glass.keyhole_y = y;
                    phase->glass.keyhole_unlocked = 0;
                } else if (ctor == 0x2d085cu) { /* CFog */
                    phase->glass.fog_count++;
                }
            }
            if (!strcmp(phase->stage_name, "VORTEX")) {
                if (ctor == 0x261620u) { /* CRotScroller */
                    phase->vortex.enabled = 1;
                    phase->vortex.rot_x = x;
                    phase->vortex.rot_y = y;
                } else if (ctor == 0x283314u && phase->vortex.ring_count < 20u) { /* CGlass ring wave */
                    unsigned ri = phase->vortex.ring_count++;
                    phase->vortex.enabled = 1;
                    phase->vortex.rings[ri].x = x;
                    phase->vortex.rings[ri].y = y;
                    phase->vortex.rings[ri].kind = (next && next - address >= 36)
                                                       ? layout_word(rec + 32) : (ri & 3u);
                    phase->vortex.rings[ri].spawn_frame = 6u + ri * 10u;
                    phase->vortex.rings[ri].cleared = 0;
                }
            }
            if (!strcmp(phase->stage_name, "SHADOW")) {
                if (ctor == 0x2be684u) { /* CSpirit */
                    phase->shadow.enabled = 1;
                    phase->shadow.config_ok = 1;
                } else if (ctor == 0x2bb860u) { /* CBlueHead */
                    phase->shadow.bh_x = x;
                    phase->shadow.bh_y = y;
                    phase->shadow.bh_active = 1;
                }
            }
            if (ctor == lists[i].spring) {
                if (address - base > phase->code_size - 36 || (next && next - address < 36))
                    return -1;
                uint32_t rotation = layout_word(rec + 28), variant = layout_word(rec + 32);
                if (variant > 3u || phase->plat_count >= sizeof phase->plats / sizeof phase->plats[0])
                    return -1;
                /* CSpringBr constructor (+0x78/+0x7c): one-sided active extent. */
                int width = (spring_types[variant].reach * (int)phase->projection_px + 32768) / 65536;
                int left = rotation == 0x200u ? x - width : x;
                if (left < 0 || left + width > (int)phase->scroll_w || y < 0 || y > UINT16_MAX)
                    return -1;
                sidescrl_plat *pl = &phase->plats[phase->plat_count++];
                pl->x0 = (uint16_t)left;
                pl->x1 = (uint16_t)(left + width);
                pl->y = (uint16_t)y;
                pl->anchor_x = (uint16_t)x;
                pl->charge = spring_types[variant].low;
                pl->kind = 1;
                pl->variant = (uint8_t)variant;
            }
            address = next;
        }
        if (bat_cfg && (!phase->exit_config || !phase->body_piece_count ||
            !overlay_at(phase, phase->exit_config, 208))) return -1;
        break;
    }
    return 0;
}

static int build_scroll_from_map(sidescrl_phase *phase) {
    size_t acdb_sz = 0, atil_sz = 0, bcdb_sz = 0, btil_sz = 0;
    size_t ccdb_sz = 0, ctil_sz = 0, dcdb_sz = 0, dtil_sz = 0;
    const void *acdb = layer_data(phase, "07ACDB", &acdb_sz);
    const void *atil = layer_data(phase, "08ATIL", &atil_sz);
    const void *bcdb = layer_data(phase, "09BCDB", &bcdb_sz);
    const void *btil = layer_data(phase, "10BTIL", &btil_sz);
    const void *ccdb = layer_data(phase, "11CCDB", &ccdb_sz);
    const void *ctil = layer_data(phase, "12CTIL", &ctil_sz);
    const void *dcdb = layer_data(phase, "13DCDB", &dcdb_sz);
    const void *dtil = layer_data(phase, "14DTIL", &dtil_sz);
    unsigned strip_w = SCROLL_STRIP_W;
    uint8_t *strip;

    if (scrl_map_init(&phase->map, acdb, acdb_sz, atil, atil_sz, bcdb, bcdb_sz,
                      btil, btil_sz, ccdb, ccdb_sz, ctil, ctil_sz, dcdb, dcdb_sz,
                      dtil, dtil_sz, phase->amap, phase->amap_size) &&
        !phase->map.has_amap) {
        /* Soft path still OK with empty planes if STXT present. */
    }
    apply_stage_parallax(phase);

    /* Center VORTEX concentric tunnel planes without horizontal scroll tearing. */
    if (strcmp(phase->stage_name, "VORTEX") == 0) {
        unsigned pi;
        for (pi = 0; pi < phase->map.plane_count && pi < SCRL_PLANE_MAX; pi++) {
            scrl_plane *vp = &phase->map.planes[pi];
            vp->blend_mode = (pi == 0u) ? 1u : 0u;
            vp->no_wrap_x = 0u;
            vp->no_wrap_y = 0u;
            vp->base_x = (vp->pixel_w > SCR_W) ? (int32_t)((vp->pixel_w - SCR_W) / 2u) : 0;
            vp->base_y = (vp->pixel_h > SCR_H) ? -(int32_t)((vp->pixel_h - SCR_H) / 2u) : 0;
        }
        if (phase->map.plane_count > 0 && phase->map.planes[0].pixel_h > SCR_H)
            phase->map.camera_y = (int)(phase->map.planes[0].pixel_h - SCR_H) / 2;
    }
    if (strcmp(phase->stage_name, "BAT2") == 0 && phase->map.plane_count > 0)
        phase->map.planes[0].blend_mode = 3;

    if (phase->map.plane_count > 0 && phase->map.planes[0].pixel_w > strip_w) {
        strip_w = phase->map.planes[0].pixel_w;
        if (strip_w > SCRL_STRIP_MAX) strip_w = SCRL_STRIP_MAX;
    }

    if (strcmp(phase->stage_name, "VORTEX") != 0 &&
        phase->map.plane_count > 0 && phase->map.planes[0].pixel_w >= strip_w)
        phase->map.planes[0].no_wrap_x = 1u;

    if (strcmp(phase->stage_name, "VORTEX") != 0)
        phase->map.camera_y = camera_y_target(phase, 0u);

    scrl_map_build_collision(&phase->map, strip_w);
    {
        unsigned cx;
        for (cx = 0; cx < strip_w && cx < SCRL_STRIP_MAX; cx++) {
            if (phase->map.floor_py[cx] != SCRL_FLOOR_PIT &&
                phase->map.floor_py[cx] < (uint8_t)(ACTOR_FEET + 8u))
                phase->map.floor_py[cx] = (uint8_t)(ACTOR_FEET + 8u);
        }
    }

    if (phase->map.plane_count && strcmp(phase->stage_name, "VORTEX") != 0)
        build_hixel_floor(&phase->map);

    strip = malloc((size_t)strip_w * SCR_H * 4u);
    if (!strip) return -1;
    if (scrl_map_draw(&phase->map, phase->palette, strip, strip_w, SCR_H, 0)) {
        free(strip);
        return -1;
    }
    phase->scroll_rgba = strip;
    phase->scroll_w = strip_w;
    phase->scroll_h = SCR_H;
    return 0;
}

/* ---- terrain seen by sidescrl_motion: the scrl_map floor profile -------------- */

static bool is_pit_col(const sidescrl_phase *phase, int32_t column) {
    return phase && column >= 0 && (unsigned)column < phase->map.strip_w &&
           phase->map.floor_py[column] == SCRL_FLOOR_PIT;
}

static int32_t floor_px_at(const sidescrl_phase *phase, int32_t column) {
    int fy = scrl_map_find_floor(&phase->map, (int)column);
    return fy < 0 ? 180 : fy;
}

/* First column the actor box may not enter on the right: the strip is the world. */
static int32_t world_right_col(const sidescrl_phase *phase) {
    return phase->scroll_w ? (int32_t)phase->scroll_w : (int32_t)SCR_W;
}

/* y of the highest solid pixel in `column`, or SIDESCRL_SLOPE_NO_GROUND in a pit. */
static int32_t terrain_ground(const void *ctx, int32_t column) {
    const sidescrl_phase *phase = (const sidescrl_phase *)ctx;
    int32_t clamped = column;
    if (phase && phase->plat_count > 0u && phase->on_plat_idx >= 0 &&
        phase->on_plat_idx < (int)phase->plat_count) {
        const sidescrl_plat *pl = &phase->plats[phase->on_plat_idx];
        if (pl->state != 2u && column >= (int32_t)pl->x0 && column <= (int32_t)pl->x1)
            return (int32_t)pl->y;
    }
    if (phase->map.vcam_on && column >= 0 && column < world_right_col(phase)) {
        const scrl_tile_cache *tiles = &phase->map.planes[0].tiles;
        int row = phase->actor.y + ACTOR_FEET;
        if (row < 0) row = 0;
        if ((unsigned)row >= tiles->height_px) return SIDESCRL_SLOPE_NO_GROUND;
        return row + scrl_tilecache_floor_offset(tiles, column, row);
    }
    if (is_pit_col(phase, column)) return SIDESCRL_SLOPE_NO_GROUND;
    if (clamped < 0) clamped = 0;
    if (clamped >= world_right_col(phase)) clamped = world_right_col(phase) - 1;
    if (is_pit_col(phase, clamped)) return SIDESCRL_SLOPE_NO_GROUND;
    return floor_px_at(phase, clamped);
}

/* Solid: everything at or below the floor (except open pits), closed fences, and world edges. */
static bool terrain_wall(void *ctx, int32_t column, int32_t row) {
    const sidescrl_phase *phase = (const sidescrl_phase *)ctx;
    if (column < EDGE_MARGIN - ACTOR_HALF_W || column >= world_right_col(phase))
        return true;
    for (unsigned fi = 0; fi < phase->fence_count; fi++) {
        if (!phase->fences[fi].open &&
            column >= phase->fences[fi].x - 6 && column <= phase->fences[fi].x + 6 &&
            row >= phase->fences[fi].y - 96 && row <= phase->fences[fi].y + 96)
            return true;
    }
    /* _BonesWallAheadCheck branches on hixel 3 in both facing directions. */
    if (phase->map.vcam_on)
        return scrl_tilecache_hixel(&phase->map.planes[0].tiles, (unsigned)column, (unsigned)row) == 3;
    if (is_pit_col(phase, column))
        return false;
    return row >= floor_px_at(phase, column);
}

/* Refresh the fields that mirror the actor (see the header). */
static void sync_from_actor(sidescrl_phase *phase) {
    const SidescrlActor *a = &phase->actor;
    const int64_t cx = (int64_t)a->x + ACTOR_HALF_W;
    int64_t feet = (int64_t)a->y + ACTOR_FEET;
    if (phase->map.vcam_on) {
        int camera = (int)feet - (int)SCR_H / 2;
        int limit = (int)phase->map.planes[0].tiles.height_px - (int)SCR_H;
        if (camera > limit) camera = limit;
        phase->map.camera_y = camera > 0 ? camera : 0;
        feet -= phase->map.camera_y;
    }
    const int32_t ground = floor_px_at(phase, (int32_t)cx);
    phase->bones_x = (uint32_t)(cx * UNITS_PER_PIXEL + a->rem_x);
    phase->bones_y = (uint32_t)(feet * UNITS_PER_PIXEL + a->rem_y);
    phase->ground_y = (uint32_t)ground * UNITS_PER_PIXEL;
    phase->vel_x = a->vx;
    phase->vel_y = a->vy;
    phase->airborne = a->on_ground ? 0 : 1;
    phase->facing = a->facing_left ? -1 : 1;
}

void sidescrl_phase_spawn(sidescrl_phase *phase, int world_x_px) {
    int32_t floor;
    if (!phase) return;
    floor = phase->map.vcam_on && world_x_px >= 0 && (unsigned)world_x_px < phase->map.strip_w
                ? phase->map.vfloor[world_x_px] : floor_px_at(phase, world_x_px);
    sidescrl_actor_init(&phase->actor, world_x_px - ACTOR_HALF_W, floor - ACTOR_FEET);
    phase->walk_tick = 0;
    if (phase->projection_px) {
        phase->actor.projection_px = phase->projection_px;
        phase->actor.jump_impulse = phase->initial.jump_impulse;
        phase->actor.boost_duration = phase->initial.boost_ticks;
        phase->actor.hit_width = phase->initial.hit.top_right_x - phase->initial.hit.top_left_x;
        phase->actor.hit_height = phase->initial.hit.bot_right_y - phase->initial.hit.top_right_y;
    }
    phase->actor.on_ground = true; /* flush on the floor: the first step keeps it there */
    sync_from_actor(phase);
}

static void stage_name_from_iso(char *out, size_t n, const char *iso_dir) {
    const char *p = strrchr(iso_dir, '/');
    p = p ? p + 1 : iso_dir;
    if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9')
        p += 2;
    snprintf(out, n, "%s", p);
}

static int load_opt(cdimage *cd, const char *iso_dir, const char *file,
                    void **out, size_t *sz) {
    char path[96];
    snprintf(path, sizeof path, "%s/%s", iso_dir, file);
    if (resource_load(cd, path, out, sz)) {
        *out = NULL;
        *sz = 0;
        return -1;
    }
    return 0;
}

int sidescrl_phase_load(sidescrl_phase *phase, cdimage *cd, const char *iso_dir) {
    unsigned i;
    size_t off;
    char path[96];
    if (!phase || !cd || !iso_dir) return -1;
    memset(phase, 0, sizeof *phase);
    snprintf(phase->iso_dir, sizeof phase->iso_dir, "%s", iso_dir);
    stage_name_from_iso(phase->stage_name, sizeof phase->stage_name, iso_dir);
    phase->facing = 1;
    phase->bones_x = START_X;
    phase->bones_y = GROUND_Y;
    phase->ground_y = GROUND_Y;
    phase->camera_x = START_X;
    phase->finish_x = FINISH_X;
    phase->state = SIDESCRL_ST_STAND;
    phase->audio_track = 3;
    runway_skel_init(&phase->skel);
    /* §24: poofs on all sidescrl; denser on BAT*. */
    stage_enemies_init_sidescrl(&phase->enemies, phase->stage_name,
                                UNITS_PER_PIXEL);
    stage_enemies_load_disc_sprites(cd);

    snprintf(path, sizeof path, "%s/03CODE", iso_dir);
    if (resource_load(cd, path, &phase->code, &phase->code_size) ||
        phase->code_size <= 20000)
        goto fail;
    snprintf(path, sizeof path, "%s/04STXT", iso_dir);
    if (resource_load(cd, path, &phase->textures, &phase->textures_size) ||
        phase->textures_size == 0)
        goto fail;
    (void)load_opt(cd, iso_dir, "02TITL", &phase->title, &phase->title_size);
    (void)load_opt(cd, iso_dir, "06AMAP", &phase->amap, &phase->amap_size);

    for (i = 0; k_layer_names[i] && phase->layer_count < SIDESCRL_LAYER_MAX; i++) {
        void *data = NULL;
        size_t sz = 0;
        if (load_opt(cd, iso_dir, k_layer_names[i], &data, &sz) || sz == 0)
            continue;
        snprintf(phase->layers[phase->layer_count].name,
                 sizeof phase->layers[phase->layer_count].name, "%s",
                 k_layer_names[i]);
        phase->layers[phase->layer_count].data = data;
        phase->layers[phase->layer_count].size = sz;
        phase->layer_count++;
    }

    const bones_projection *projection = bones_projection_for(phase->stage_name);
    if (!projection || bones_projection_init(projection, phase->code, phase->code_size, &phase->initial))
        goto fail;
    phase->projection_px = projection->pixels_per_unit;
    if (!strcmp(phase->stage_name, "VALLEY") && load_valley_walk(phase)) goto fail;

    build_palette((const uint8_t *)phase->code, phase->code_size,
                  (const uint8_t *)phase->title, phase->title_size, phase->palette);
    merge_glass_subpalette(phase); /* 15GLASS bank2[1..126] — see helper (needs disc validation) */
    if (build_scroll_from_map(phase)) goto fail;

    {
        unsigned goal_px = (phase->scroll_w > 360u) ? (phase->scroll_w - 60u) : 260u;
        phase->finish_x = goal_px * UNITS_PER_PIXEL;
    }
    if (load_stage_layout(phase)) goto fail;
    if (phase->glass.enabled) {
        unsigned pi, lit = 0u;
        for (pi = 513u; pi <= 638u; pi++) {
            if (phase->palette[4u * pi + 1u] | phase->palette[4u * pi + 2u] |
                phase->palette[4u * pi + 3u])
                lit++;
        }
        phase->glass.subpalette_lit = lit;
    }
    stage_enemies_populate_sidescrl_strip(&phase->enemies, phase->scroll_w);
    {
        unsigned hpx = (phase->scroll_w > 480u) ? 320u : 220u;
        int32_t hpy;
        while (is_pit_col(phase, (int32_t)hpx) && hpx + 16u < phase->scroll_w) hpx += 16u;
        hpy = floor_px_at(phase, (int32_t)hpx);
        stage_enemies_add_health_pickup(&phase->enemies, hpx,
                                        hpy > 0 ? (unsigned)hpy : 180u,
                                        RUNWAY_DAMAGE_DEFAULT);
        if (phase->scroll_w >= 1100u) {
            unsigned hpx2 = phase->scroll_w / 2u + 80u;
            int32_t hpy2;
            while (is_pit_col(phase, (int32_t)hpx2) && hpx2 + 24u < phase->scroll_w) hpx2 += 24u;
            hpy2 = floor_px_at(phase, (int32_t)hpx2);
            stage_enemies_add_health_pickup(&phase->enemies, hpx2,
                                            hpy2 > 0 ? (unsigned)hpy2 : 180u,
                                            RUNWAY_DAMAGE_DEFAULT);
        }
        if (!strcmp(phase->stage_name, "LAFF") && phase->scroll_w >= 640u) {
            unsigned hpx3 = phase->scroll_w - 356u;
            int32_t hpy3 = floor_px_at(phase, (int32_t)hpx3);
            stage_enemies_add_health_pickup(&phase->enemies, hpx3,
                                            hpy3 > 0 ? (unsigned)hpy3 : 140u,
                                            RUNWAY_DAMAGE_DEFAULT);
        }
    }

    int start_x = phase->initial.start_x, start_y = phase->initial.start_y;
    if (phase->map.vcam_on && start_x >= 0 && (unsigned)start_x < phase->scroll_w &&
        start_y >= 0 && (unsigned)start_y < phase->map.planes[0].pixel_h) {
        sidescrl_phase_spawn(phase, start_x);
        if (phase->config_table && bat_configure(phase, phase->initial.config)) goto fail;
        phase->actor.y = start_y + (phase->body_config ? phase->floor_delta : phase->initial.hit.bot_right_y) - ACTOR_FEET;
        phase->actor.facing_left = phase->initial.facing < 0;
        phase->actor.on_ground = false;
        sync_from_actor(phase);
    } else {
        sidescrl_phase_spawn(phase, START_PX);
    }
    phase->camera_x = phase->bones_x;
    phase->finish_left = phase->bones_x > phase->finish_x;
    phase->on_plat_idx = -1;

    off = 0;
    while (phase->anim_count < SIDESCRL_ANIM_MAX) {
        size_t before = off;
        uint8_t *px = NULL;
        unsigned w = 0, h = 0;
        if (decode_stxtr(phase->textures, phase->textures_size, &off, phase->palette,
                         &px, &w, &h)) {
            if (off == before) break;
            continue;
        }
        if (w >= 8 && w <= 96 && h >= 12 && h <= 96) {
            phase->anim_rgba[phase->anim_count] = px;
            phase->anim_w[phase->anim_count] = w;
            phase->anim_h[phase->anim_count] = h;
            phase->anim_count++;
        } else {
            free(px);
        }
    }
    if (phase->anim_count == 0) {
        fprintf(stderr, "%s/04STXT contains no decodable actor frames\n", iso_dir);
        goto fail;
    }
    phase->anim_index = 0;
    phase->sprite_rgba = phase->anim_rgba[0];
    phase->sprite_w = phase->anim_w[0];
    phase->sprite_h = phase->anim_h[0];

    phase->frame_rgba = calloc((size_t)SCR_W * SCR_H * 4u, 1);
    if (!phase->frame_rgba) goto fail;

    snprintf(phase->status, sizeof phase->status,
             "%s stand layers=%u planes=%u amap=%u", phase->stage_name,
             phase->layer_count, phase->map.plane_count, phase->map.amap_n);
    return sidescrl_phase_render(phase);

fail:
    sidescrl_phase_free(phase);
    return -1;
}

void sidescrl_phase_free(sidescrl_phase *phase) {
    unsigned i;
    if (!phase) return;
    scrl_map_free(&phase->map); /* the tile caches own their memory; the layers below are not theirs */
    for (i = 0; i < phase->anim_count && i < SIDESCRL_ANIM_MAX; i++)
        free(phase->anim_rgba[i]);
    for (i = 0; i < phase->layer_count && i < SIDESCRL_LAYER_MAX; i++)
        free(phase->layers[i].data);
    free(phase->title);
    free(phase->textures);
    free(phase->code);
    free(phase->amap);
    free(phase->scroll_rgba);
    free(phase->frame_rgba);
    memset(phase, 0, sizeof *phase);
}

/*
 * Left edge of the view, in strip (world) columns: the camera trails Bones, the view keeps him a
 * third of the screen in from the left and stays inside the strip. The strip is the world frame
 * of the collision and of the actors, so this is also the offset that turns a world column into
 * a screen column.
 */
static unsigned view_left(const sidescrl_phase *phase) {
    const unsigned cam_px = phase->camera_x / UNITS_PER_PIXEL;
    int target;
    if (phase->scroll_w <= SCR_W) return 0;
    target = (int)cam_px - (int)(SCR_W / 3);
    if (target < 0) target = 0;
    if ((unsigned)target > phase->scroll_w - SCR_W)
        target = (int)(phase->scroll_w - SCR_W);
    return (unsigned)target;
}

static void valley_triggers_step(sidescrl_phase *phase, int32_t prev_vy) {
    const SidescrlActor *a = &phase->actor;
    int cx = a->x + ACTOR_HALF_W;
    int feet = a->y + ACTOR_FEET;
    unsigned i, j;
    for (i = 0; i < phase->fence_count; i++) {
        if (phase->fences[i].dissolve_timer > 0)
            phase->fences[i].dissolve_timer--;
    }
    for (i = 0; i < phase->fence_key_count; i++) {
        if (phase->fence_keys[i].anim_timer > 0)
            phase->fence_keys[i].anim_timer--;
        /* CFenceKey activates strictly on stomp/downward drop, not walk or Blues zap. */
        if (!phase->fence_keys[i].triggered && (prev_vy > 0 || a->vy > 0) &&
            abs(cx - phase->fence_keys[i].x) <= 28 &&
            abs(feet - phase->fence_keys[i].y) <= 64) {
            phase->fence_keys[i].triggered = 1;
            phase->fence_keys[i].anim_timer = 18;
            runway_sfx_emit(0x38u); /* VALLEY CFenceKey::Brain at 0x2C2656. */
            for (j = 0; j < phase->fence_count; j++) {
                if (phase->fences[j].id == phase->fence_keys[i].id) {
                    phase->fences[j].open = 1;
                    phase->fences[j].dissolve_timer = 24;
                }
            }
        }
    }
    for (i = 0; i < phase->blue_tomb_count; i++) {
        if (phase->blue_tombs[i].drain_timer > 0)
            phase->blue_tombs[i].drain_timer--;
        if (!phase->blue_tombs[i].used && a->on_ground && a->vy == 0 &&
            abs(cx - phase->blue_tombs[i].x) <= 24 &&
            abs(feet - phase->blue_tombs[i].y) <= 40) {
            phase->blue_tombs[i].used = 1;
            phase->blue_tombs[i].drain_timer = 24;
            runway_skel_add(&phase->skel, 0x12c00);
            phase->sfx_flags |= RUNWAY_SFX_FLAG_C;
        }
    }
}

static void mauso_controller_step(sidescrl_phase *phase) {
    SidescrlActor *a;
    int cx, feet;
    unsigned k;
    if (!phase->mauso.enabled) return;
    a = &phase->actor;
    cx = a->x + ACTOR_HALF_W;
    feet = a->y + ACTOR_FEET;
    for (k = 0; k < 16u; k++) {
        if (phase->mauso.key_timer[k] > 0u) phase->mauso.key_timer[k]--;
    }
    if (phase->mauso.bh_buzz > 0) phase->mauso.bh_buzz--;
    if (phase->mauso.bh_active &&
        abs(cx - phase->mauso.bh_x) <= 28 &&
        abs(feet - phase->mauso.bh_y) <= 56) {
        phase->mauso.bh_active = 0;
        phase->mauso.bh_buzz = 16;
        runway_skel_add(&phase->skel, 0x20000);
        phase->sfx_flags |= RUNWAY_SFX_FLAG_C;
        runway_sfx_emit(0x17u);
    }
    if (phase->mauso.phase == 1 || phase->mauso.phase == 2) {
        if (!phase->mauso.lock_edges &&
            cx >= phase->mauso.left_edge + 14 &&
            cx <= phase->mauso.right_edge - 14) {
            phase->mauso.lock_edges = 1;
        }
        if (phase->mauso.lock_edges) {
            int min_cx = phase->mauso.left_edge + 8;
            int max_cx = phase->mauso.right_edge - 8;
            if (cx < min_cx) {
                a->x = min_cx - ACTOR_HALF_W;
                a->rem_x = 0;
                if (a->vx < 0) a->vx = 0;
                cx = min_cx;
            } else if (cx > max_cx) {
                a->x = max_cx - ACTOR_HALF_W;
                a->rem_x = 0;
                if (a->vx > 0) a->vx = 0;
                cx = max_cx;
            }
        }
    }
    if (phase->mauso.phase == 1) {
        if (!phase->mauso.lock_edges) return;
        phase->mauso.wave_timer++;
        k = (phase->mauso.wave_timer / 6u + phase->mauso.wave_stage * 3u) & 15u;
        phase->mauso.key_timer[k] = 8u;
        if ((phase->mauso.wave_timer % 36u) == 0u) {
            int span = phase->mauso.arena_w > 80 ? (phase->mauso.arena_w - 80) : 480;
            int key_x = phase->mauso.arena_x + 40 + (int)(k * (unsigned)span / 16u);
            runway_sfx_emit(0x2fu); /* CTopSkelKey::SlapSlap, including misses. */
            if (a->on_ground && !a->jumping && abs(cx - key_x) <= 16) {
                int32_t before = phase->skel.skeletism;
                if (runway_skel_hit(&phase->skel, RUNWAY_DAMAGE_DEFAULT / 2))
                    phase->skel.died = 1;
                if (phase->skel.died || phase->skel.skeletism < before)
                    runway_sfx_emit(0x1bu);
            }
        }
        if (phase->mauso.wave_timer >= 90u) {
            phase->mauso.wave_timer = 0u;
            phase->mauso.wave_stage++;
            if (phase->mauso.wave_stage >= 3u) {
                phase->mauso.phase = 2;
                phase->mauso.wave_timer = 0u;
            }
        }
    } else if (phase->mauso.phase == 2) {
        unsigned alive = stage_enemies_mauso_alive(&phase->enemies);
        while (phase->mauso.skels_spawned < phase->mauso.skels_total &&
               alive < 2u &&
               (phase->mauso.wave_timer == 0u || (phase->mauso.wave_timer % 20u) == 0u)) {
            int left_s = phase->mauso.left_edge + 64;
            int right_s = phase->mauso.right_edge - 64;
            int sx_px = (phase->mauso.skels_spawned & 1) ? left_s : right_s;
            if (abs(sx_px - cx) < 96)
                sx_px = (sx_px == left_s) ? right_s : left_s;
            if (sx_px < 16) sx_px = 16;
            if (stage_enemies_spawn_mauso_assembler(
                    &phase->enemies, (unsigned)sx_px,
                    phase->mauso.left_edge + 20, phase->mauso.right_edge - 20,
                    RUNWAY_DAMAGE_DEFAULT) != 0)
                break;
            phase->mauso.skels_spawned++;
            alive++;
        }
        phase->mauso.wave_timer++;
        if (phase->mauso.skels_spawned >= phase->mauso.skels_total && alive == 0u) {
            phase->mauso.phase = 3;
            phase->mauso.lock_edges = 0;
            phase->mauso.bf_wait = 0;
        }
    } else if (phase->mauso.phase == 3) {
        if (!phase->mauso.bf_taken &&
            abs(cx - phase->mauso.bf_x) <= 28 &&
            abs(feet - phase->mauso.bf_y) <= 96) {
            phase->mauso.bf_taken = 1;
            runway_skel_add(&phase->skel, 0x3200);
            phase->sfx_flags |= RUNWAY_SFX_FLAG_C;
            runway_sfx_emit(0x29u); /* CButFly::CheckForBonesHit at 0x2B7BF0. */
        }
    }
}

/* CSpider::CheckThreadHit (0x29b3ac): sever spider silk thread -> DropLittleGuy & _LittleGuysSaved++. */
int sidescrl_village_cut_thread(sidescrl_phase *phase, int hit_x, int hit_y) {
    int si;
    if (!phase || !phase->village.enabled) return 0;
    for (si = 0; si < phase->village.spider_count; si++) {
        int sx = phase->village.spiders[si].x;
        int top_y = phase->village.spiders[si].top_y;
        int cur_y = phase->village.spiders[si].cur_y;
        if (phase->village.spiders[si].state > 1) continue;
        if (cur_y <= top_y + 4) continue;
        if (abs(hit_x - sx) <= 18 && hit_y >= top_y - 12 && hit_y <= cur_y + 24) {
            if (phase->village.spiders[si].has_guy) {
                phase->village.little_guys_saved++;
                phase->village.spider_meter += 10;
                if (phase->village.spider_meter > 100) phase->village.spider_meter = 100;
                runway_skel_add(&phase->skel, 0x2000);
            }
            phase->village.spiders[si].has_guy = 0;
            phase->village.spiders[si].state = 2;
            phase->village.spiders[si].timer = 45;
            phase->sfx_flags |= RUNWAY_SFX_FLAG_C;
            runway_sfx_emit(0x35u); /* CSpider::Fall after the thread is hit. */
            return 1;
        }
    }
    return 0;
}

static void village_controller_step(sidescrl_phase *phase, int pulse_active) {
    int si, mi, cx, feet;
    unsigned s;
    if (!phase || !phase->village.enabled) return;
    cx = phase->actor.x + ACTOR_HALF_W;
    feet = phase->actor.y + ACTOR_FEET;
    if (phase->village.bh_active &&
        abs(cx - phase->village.bh_x) <= 28 &&
        abs(feet - phase->village.bh_y) <= 64) {
        phase->village.bh_active = 0;
        runway_skel_add(&phase->skel, 0x20000);
        phase->sfx_flags |= RUNWAY_SFX_FLAG_C;
        runway_sfx_emit(0x17u);
    }
    for (mi = 0; mi < phase->village.mushroom_count; mi++) {
        if (!phase->village.mushrooms[mi].taken &&
            abs(cx - phase->village.mushrooms[mi].x) <= 24 &&
            abs(feet - phase->village.mushrooms[mi].y) <= 44) {
            phase->village.mushrooms[mi].taken = 1;
            runway_skel_add(&phase->skel, 0x4000);
            phase->sfx_flags |= RUNWAY_SFX_FLAG_C;
        }
    }
    for (s = 0; s < STAGE_SHOT_MAX; s++) {
        if (!phase->enemies.shots[s].active) continue;
        int sx = (int)(phase->enemies.shots[s].x / UNITS_PER_PIXEL);
        int sy = (int)(phase->enemies.shots[s].y / UNITS_PER_PIXEL);
        if (sidescrl_village_cut_thread(phase, sx, sy))
            phase->enemies.shots[s].active = 0;
    }
    if (pulse_active) {
        for (si = 0; si < phase->village.spider_count; si++) {
            if (phase->village.spiders[si].state <= 1 &&
                abs(cx - phase->village.spiders[si].x) <= 72 &&
                feet >= phase->village.spiders[si].top_y - 16)
                (void)sidescrl_village_cut_thread(phase, phase->village.spiders[si].x,
                                                  phase->village.spiders[si].cur_y);
        }
    }
    for (si = 0; si < phase->village.spider_count; si++) {
        int max_y = phase->village.spiders[si].top_y + phase->village.spiders[si].drop_dist;
        if (max_y > 276) max_y = 276;
        if (phase->village.spiders[si].state == 0) {
            if (phase->village.spiders[si].timer > 0) {
                phase->village.spiders[si].timer--;
                continue;
            }
            phase->village.spiders[si].cur_y += 2;
            if (phase->village.spiders[si].cur_y >= max_y) {
                phase->village.spiders[si].cur_y = max_y;
                phase->village.spiders[si].has_guy = 1; /* GrabLittleGuy */
                phase->village.spiders[si].state = 1;
            }
        } else if (phase->village.spiders[si].state == 1) {
            phase->village.spiders[si].cur_y -= 2;
            if (phase->village.spiders[si].cur_y <= phase->village.spiders[si].top_y) {
                phase->village.spiders[si].cur_y = phase->village.spiders[si].top_y;
                if (phase->village.spiders[si].has_guy) {
                    /* EatLittleGuy (0x4020011): uncut thread consumes Little Guy and drops meter. */
                    phase->village.little_guys_eaten++;
                    if (phase->village.little_guys_saved > 0u)
                        phase->village.little_guys_saved--;
                    phase->village.spider_meter -= 8;
                    if (phase->village.spider_meter < 0) phase->village.spider_meter = 0;
                }
                phase->village.spiders[si].has_guy = 0;
                phase->village.spiders[si].state = 3;
                phase->village.spiders[si].timer = 90;
            }
        } else {
            phase->village.spiders[si].cur_y = phase->village.spiders[si].top_y;
            if (phase->village.spiders[si].timer > 0) {
                phase->village.spiders[si].timer--;
            } else {
                phase->village.spiders[si].state = 0;
            }
        }
    }
}

/* CJunior::ToldGoodJoke (0x294780 -> CringeBrain) vs ToldBadJoke (0x2947a8 -> LaffBrain). */
int sidescrl_laff_tell_joke(sidescrl_phase *phase, int is_bad_joke) {
    if (!phase || !phase->laff.enabled) return -1;
    phase->laff.joke_timer = 32u;
    if (!is_bad_joke) {
        phase->laff.last_joke_kind = 1;
        phase->laff.good_jokes++;
        /* JuniorLCringe/JuniorRCringe both cue 0x29 at macro entry. */
        runway_sfx_emit(0x29u);
        return 0;
    }
    unsigned idx = phase->laff.bad_jokes;
    phase->laff.last_joke_kind = 2;
    phase->laff.bad_jokes++;
    phase->laff.laff_meter += 16;
    if (phase->laff.laff_meter > 100) phase->laff.laff_meter = 100;
    runway_skel_add(&phase->skel, 0x1000);
    /* _PickJoke (0x2B6820): 3-part spoken joke (sequences 1..3 in 17SND2/18SND3). */
    runway_sfx_emit_joke(22u + idx / 3u, 1u + (idx % 3u));
    return 1;
}

static void laff_controller_step(sidescrl_phase *phase) {
    int cur_hp;
    if (!phase || !phase->laff.enabled) return;
    if (phase->frames == 30u && phase->laff.bad_jokes == 0u)
        runway_sfx_emit_joke(22u, 1u);
    if (phase->laff.joke_timer > 0u) {
        phase->laff.joke_timer--;
        if (phase->laff.joke_timer == 1u && phase->laff.last_joke_kind == 2)
            runway_sfx_emit(0x2au + (phase->laff.bad_jokes % 3u));
    }
    cur_hp = stage_enemies_laff_boss_hp(&phase->enemies, NULL);
    while (cur_hp < phase->laff.last_boss_hp) {
        phase->laff.last_boss_hp--;
        (void)sidescrl_laff_tell_joke(phase, 1);
    }
}

int sidescrl_glass_unlock_keyhole(sidescrl_phase *phase) {
    if (!phase || !phase->glass.enabled) return -1;
    phase->glass.keyhole_unlocked = 1;
    phase->sfx_flags |= RUNWAY_SFX_FLAG_C;
    return 0;
}

static void glass_check_shots(sidescrl_phase *phase) {
    unsigned s, si;
    if (!phase || !phase->glass.enabled) return;
    for (s = 0; s < STAGE_SHOT_MAX; s++) {
        int sx;
        if (!phase->enemies.shots[s].active) continue;
        sx = (int)(phase->enemies.shots[s].x / UNITS_PER_PIXEL);
        for (si = 0; si < phase->glass.shard_count; si++) {
            if (!phase->glass.shards[si].broken &&
                (phase->glass.shards[si].kind == 2u || phase->glass.shards[si].kind == 3u) &&
                sx >= (int)phase->glass.shards[si].x0 &&
                sx <= (int)phase->glass.shards[si].x1) {
                phase->glass.shards[si].broken = 1;
            }
        }
    }
}

static void glass_controller_step(sidescrl_phase *phase) {
    int cx;
    if (!phase || !phase->glass.enabled) return;
    cx = phase->actor.x + ACTOR_HALF_W;
    glass_check_shots(phase);
    if (!phase->glass.keyhole_unlocked && abs(cx - phase->glass.keyhole_x) <= 48)
        (void)sidescrl_glass_unlock_keyhole(phase);
}

static void vortex_controller_step(sidescrl_phase *phase) {
    unsigned ri;
    if (!phase || !phase->vortex.enabled) return;
    for (ri = 0; ri < phase->vortex.ring_count; ri++) {
        if (phase->vortex.rings[ri].cleared) continue;
        if (phase->frames >= phase->vortex.rings[ri].spawn_frame + 12u) {
            phase->vortex.rings[ri].cleared = 1;
            phase->vortex.rings_done++;
        }
    }
    if (phase->vortex.rings_done >= phase->vortex.ring_count && phase->vortex.ring_count > 0u)
        phase->vortex.exit_unlocked = 1;
}

int sidescrl_shadow_complete_trial(sidescrl_phase *phase, int trial_idx) {
    int bit = (trial_idx >= 0 && trial_idx < 6) ? trial_idx : (phase ? (phase->shadow.trials % 6) : 0);
    if (!phase || !phase->shadow.enabled) return -1;
    phase->shadow.trial_mask |= (uint8_t)(1u << bit);
    phase->shadow.trials++;
    return phase->shadow.trials;
}

static void shadow_controller_step(sidescrl_phase *phase, int crouch, int32_t prev_ax) {
    int cx;
    if (!phase || !phase->shadow.enabled) return;
    cx = phase->actor.x + ACTOR_HALF_W;
    if (phase->shadow.bh_active && abs(cx - phase->shadow.bh_x) <= 28) {
        phase->shadow.bh_active = 0;
        runway_skel_add(&phase->skel, 0x20000);
        phase->sfx_flags |= RUNWAY_SFX_FLAG_C;
        runway_sfx_emit(0x17u);
    }
    phase->shadow_eye_phase = (phase->frames % 180u);
    if (phase->shadow_eye_phase >= 140u) {
        if (!crouch && (phase->actor.x != prev_ax || phase->actor.jumping)) {
            phase->shadow.gaze_penalties++;
            if (phase->shadow.gaze_penalties == 1u || (phase->frames % 24u) == 0u) {
                if (runway_skel_loose(&phase->skel, RUNWAY_DAMAGE_DEFAULT / 4))
                    phase->skel.died = 1;
            }
        } else if (crouch) {
            phase->shadow.crouch_dodges++;
        }
    }
    if (phase->shadow_eye_phase == 179u)
        (void)sidescrl_shadow_complete_trial(phase, phase->shadow.trials);
}

static void sidescrl_end_sound(const sidescrl_phase *phase, unsigned lost) {
    if (!phase->stage_name[0]) return;
    if (phase->laff.enabled) {
        runway_sfx_emit(lost ? 0x29u : 0x2cu);
        return;
    }
    runway_sfx_emit((phase->body_config ? 0x17u : 0x18u) + lost);
}

int sidescrl_phase_step(sidescrl_phase *phase, const saturn_pad *pad) {
    uint32_t held;
    int32_t prev_ax;
    if (!phase || !pad || sidescrl_phase_done(phase)) return 0;
    if (!phase->body_config || (!(pad->pressed & PAD_START) && phase->state != SIDESCRL_ST_PAUSED))
        phase->frames++;
    phase->sfx_flags = 0;
    held = pad->held;

    if (pad->pressed & PAD_START) {
        if (phase->state == SIDESCRL_ST_PAUSED) {
            runway_skel_set_paused(&phase->skel, 0);
            phase->state = phase->pre_pause_state;
        } else {
            phase->pre_pause_state = phase->state;
            runway_skel_set_paused(&phase->skel, 1);
            phase->state = SIDESCRL_ST_PAUSED;
        }
        return sidescrl_phase_render(phase);
    }
    if (phase->state == SIDESCRL_ST_PAUSED)
        return sidescrl_phase_render(phase);

    runway_skel_tick_cooldown(&phase->skel);
    prev_ax = phase->actor.x;

    {
        /* One unified step: walk, wall clamp, jump, slope, crouch and gravity. */
        const bool live_stage = (phase->stage_name[0] != '\0');
        const bool is_vortex = (strcmp(phase->stage_name, "VORTEX") == 0);
        const bool was_on_ground = phase->actor.on_ground;
        SidescrlActor *a = &phase->actor;
        int32_t prev_foot_y = a->y + ACTOR_FEET;
        int32_t prev_vy = a->vy;
        int crouch = (!is_vortex && a->on_ground && (held & PAD_DOWN)) ? 1 : 0;
        int32_t walk_dir = crouch ? 0 : ((held & PAD_RIGHT) ? 1 : ((held & PAD_LEFT) ? -1 : 0));
        if (phase->walk_root) {
            if (a->on_ground && !walk_dir) phase->walk_tick = 0;
            uint32_t root = layout_word(phase->walk_root + 12 * (phase->walk_tick / 4));
            /* BonesBuild: signed ten-bit forward travel * 512, split over four ticks. */
            a->walk_speed = ((int)(root & 1023u) - 512) * 128;
        }
        unsigned jump_button = phase->body_config ? PAD_C : PAD_JUMP;
        bool jump_edge = (!crouch && !is_vortex && (pad->pressed & jump_button) != 0);
        int valley_vault = (!phase->body_config && !strcmp(phase->stage_name, "VALLEY") &&
                            (held & PAD_X) != 0);
        int was_tall = phase->tall;
        if (phase->hurt_timer > 0u) phase->hurt_timer--;
        phase->tall = crouch;

        if (phase->plat_count > 0u) {
            unsigned pi;
            for (pi = 0; pi < phase->plat_count; pi++) {
                sidescrl_plat *pl = &phase->plats[pi];
                if (pl->kind == 1u) {
                    if ((int)pi != phase->on_plat_idx && pl->charge > INT32_MIN + 26)
                        pl->charge -= 26;
                    continue;
                }
                if (pl->state == 1u) {
                    if (pl->timer > 0u) pl->timer--;
                    if (pl->timer == 0u) {
                        pl->state = 2u;
                        pl->timer = 110u;
                        if (phase->on_plat_idx == (int)pi) {
                            phase->on_plat_idx = -1;
                            a->on_ground = false;
                        }
                    }
                } else if (pl->state == 2u) {
                    if (pl->timer > 0u) pl->timer--;
                    if (pl->timer == 0u) pl->state = 0u;
                }
            }
            if (phase->on_plat_idx >= 0 && phase->on_plat_idx < (int)phase->plat_count) {
                const sidescrl_plat *cur_pl = &phase->plats[phase->on_plat_idx];
                int32_t cur_cx = a->x + ACTOR_HALF_W;
                if (cur_pl->state == 2u || cur_cx < (int32_t)cur_pl->x0 ||
                    cur_cx > (int32_t)cur_pl->x1) {
                    phase->on_plat_idx = -1;
                    a->on_ground = false;
                } else if ((held & PAD_DOWN) && (pad->pressed & jump_button)) {
                    phase->on_plat_idx = -1;
                    a->on_ground = false;
                    a->jumping = false;
                    a->y += 3;
                    jump_edge = false;
                    crouch = 0;
                    phase->tall = 0;
                } else {
                    a->y = (int32_t)cur_pl->y - ACTOR_FEET;
                    a->vy = 0;
                    a->rem_y = 0;
                    a->on_ground = true;
                }
            }
        }
        if (phase->map.vcam_on && phase->on_plat_idx < 0 && a->on_ground &&
            (held & PAD_DOWN) && (pad->pressed & jump_button)) {
            const scrl_tile_cache *tiles = &phase->map.planes[0].tiles;
            int hx = a->x + ACTOR_HALF_W;
            int hf = a->y + ACTOR_FEET;
            int hcode = 0;
            for (int hd = 0; hd <= 4; hd++) {
                int yq = hf + hd;
                if (yq >= 0 && (unsigned)yq < tiles->height_px) {
                    hcode = scrl_tilecache_hixel(tiles, (unsigned)hx, (unsigned)yq);
                    if (hcode) break;
                }
            }
            if (hcode == 1 || hcode == 2) {
                a->y += 14;
                a->on_ground = false;
                a->jumping = false;
                jump_edge = false;
                crouch = 0;
                phase->tall = 0;
            }
        }

        if (phase->body_config && bat_climb(phase, pad)) {
            /* The latch owns motion until release or completion of the pull-up. */
        } else if (is_vortex) {
            int nx;
            if (phase->fly_y_px == 0) phase->fly_y_px = 112;
            if (walk_dir != 0) a->facing_left = (walk_dir < 0);
            a->vx = walk_dir * sidescrl_project_impulse(a, SIDESCRL_MOTION_WALK_MAX);
            nx = a->x + walk_dir * 3;
            if (nx < EDGE_MARGIN) nx = EDGE_MARGIN;
            if (phase->scroll_w > (unsigned)EDGE_MARGIN &&
                nx > (int)phase->scroll_w - EDGE_MARGIN)
                nx = (int)phase->scroll_w - EDGE_MARGIN;
            a->x = nx;
            a->rem_x = 0;
            if (held & PAD_UP) phase->fly_y_px -= 2;
            if (held & PAD_DOWN) phase->fly_y_px += 2;
            if (phase->fly_y_px < 48) phase->fly_y_px = 48;
            if (phase->fly_y_px > 172) phase->fly_y_px = 172;
            a->y = phase->fly_y_px - ACTOR_FEET;
            a->vy = 0;
            a->rem_y = 0;
            a->on_ground = true;
            a->jumping = false;
            a->pose = SIDESCRL_POSE_STAND;
        } else {
            if (live_stage) {
                if (a->on_ground) {
                    phase->coyote_timer = 5u;
                } else if (phase->coyote_timer > 0u) {
                    phase->coyote_timer--;
                }
                if (jump_edge) {
                    phase->jump_buf_timer = 5u;
                } else if (phase->jump_buf_timer > 0u) {
                    phase->jump_buf_timer--;
                }
                if (!a->on_ground && phase->coyote_timer > 0u && jump_edge && !a->jumping) {
                    a->on_ground = true;
                    phase->coyote_timer = 0u;
                    phase->jump_buf_timer = 0u;
                } else if (a->on_ground && phase->jump_buf_timer > 0u && !crouch) {
                    jump_edge = true;
                    phase->jump_buf_timer = 0u;
                    phase->coyote_timer = 0u;
                }
                if (!a->on_ground && walk_dir != 0) {
                    int32_t target_vx = walk_dir * sidescrl_project_impulse(a, a->walk_speed);
                    int32_t diff = target_vx - a->vx;
                    int32_t steer = 340;
                    if (diff > steer) diff = steer;
                    else if (diff < -steer) diff = -steer;
                    a->vx += diff;
                    a->facing_left = (walk_dir < 0);
                }
                if (a->on_ground) phase->branch_launch = 0;
                a->spring_held = (held & (phase->branch_launch ? PAD_C : (valley_vault ? PAD_X : jump_button))) != 0;

            }
            if (valley_vault && a->on_ground && (pad->pressed & PAD_X)) {
                a->vy = sidescrl_project_impulse(a, -22200);
                a->on_ground = false;
                a->jumping = true;
                a->spring_ticks = 34u;
                a->spring_held = true;
                jump_edge = false;
                phase->sfx_flags |= RUNWAY_SFX_FLAG_JUMP;
            }

            if (live_stage && phase->hurt_timer > 11u && phase->knockback_vx != 0) {
                a->vx = phase->knockback_vx;
                phase->knockback_vx = (phase->knockback_vx * 3) / 4;
            }
            if (phase->on_plat_idx >= 0) {
                sidescrl_plat *pl = &phase->plats[phase->on_plat_idx];
                if (pl->kind == 1 && pl->timer) {
                    /* CBnsExtState::SetSpring holds the actor for 4..6 yields, then
                     * sets a new upward impulse and the optional 20-tick boost. */
                    int type = pl->variant, low = spring_types[type].low, high = spring_types[type].high;
                    walk_dir = 0;
                    jump_edge = false;
                    a->vx = 0;
                    phase->jump_buf_timer = 0;
                    if (--pl->timer == 0) {
                        int distance = abs(a->x + ACTOR_HALF_W - (int)pl->anchor_x);
                        int32_t arm = (int32_t)((int64_t)distance * 65536 / a->projection_px) / 4;
                        int boost = (held & PAD_C) != 0;
                        if (boost) pl->charge += (high - low) / 4;
                        if (pl->charge < low) pl->charge = low;
                        if (pl->charge > high) pl->charge = high;
                        int impulse = low + (int)((int64_t)pl->charge * arm / 65536);
                        a->vy = sidescrl_project_impulse(a, -(impulse > high ? high : impulse));
                        phase->branch_launch = 1;
                        a->spring_ticks = boost ? 20u : 0u;
                        a->spring_held = boost;
                        a->on_ground = false;
                        a->jumping = true;
                        phase->on_plat_idx = -1;
                    }
                }
            }
            /* BonesLandOnFloorChk applies a negative penetration correction even
             * when it exceeds the walk step height (BAT1 starts inside a slope). */
            if (phase->map.vcam_on && !a->on_ground && a->vy >= 0) {
                const scrl_tile_cache *tiles = &phase->map.planes[0].tiles;
                int cx = a->x + ACTOR_HALF_W;
                int foot = a->y + ACTOR_FEET;
                if (scrl_tilecache_hixel(tiles, (unsigned)cx, (unsigned)foot) > 0) {
                    a->y += scrl_tilecache_floor_offset(tiles, cx, foot);
                    a->vy = a->rem_y = 0;
                    a->on_ground = true;
                    a->jumping = false;
                }
            }
            sidescrl_motion_step(a, walk_dir, jump_edge, MAX_CLIMB_STEP, terrain_wall,
                                 terrain_ground, phase);
            if (phase->plat_count > 0u) {
                if (a->jumping && phase->on_plat_idx >= 0) {
                    phase->on_plat_idx = -1;
                } else if (phase->on_plat_idx < 0 && a->vy >= 0 && !(held & PAD_DOWN)) {
                    int32_t cx_now = a->x + ACTOR_HALF_W;
                    int32_t new_foot_y = a->y + ACTOR_FEET;
                    unsigned pi;
                    for (pi = 0; pi < phase->plat_count; pi++) {
                        sidescrl_plat *pl = &phase->plats[pi];
                        if (pl->state == 2u) continue;
                        if (touches_platform(phase, pl, cx_now, prev_foot_y, new_foot_y)) {
                            phase->on_plat_idx = (int)pi;
                            a->y = (int32_t)pl->y - ACTOR_FEET;
                            a->vy = 0;
                            a->rem_y = 0;
                            a->on_ground = true;
                            a->jumping = false;
                            if (pl->kind == 1u) pl->timer = (uint8_t)spring_types[pl->variant].delay;
                            if (pl->kind == 2u && pl->state == 0u) {
                                pl->state = 1u;
                                pl->timer = 45u;
                            }
                            break;
                        }
                    }
                }
            }
            if (a->jumping) phase->coyote_timer = 0u;
            if (a->y < 0) { /* the top of the screen is a ceiling */
                a->y = 0;
                a->rem_y = 0;
                if (a->vy < 0) a->vy = 0;
            }
            if (was_on_ground && !a->on_ground && a->jumping)
                phase->sfx_flags |= RUNWAY_SFX_FLAG_JUMP;
            if (!was_on_ground && a->on_ground)
                phase->sfx_flags |= RUNWAY_SFX_FLAG_LAND;
        }
        if (!was_tall && phase->tall) {
            phase->sfx_flags |= RUNWAY_SFX_FLAG_TALL;
            if (live_stage)
                runway_sfx_emit(phase->laff.enabled ? 0x2eu : 0x0cu);
        }
        if ((phase->sfx_flags & RUNWAY_SFX_FLAG_JUMP) && live_stage) {
            /* Spring_Brain loads the same note at different IDs in these banks. */
            unsigned event = phase->laff.enabled ? 0x30u :
                             phase->body_config ? 0x22u :
                             !strcmp(phase->stage_name, "VALLEY") ? 0x2au : 0x23u;
            runway_sfx_emit(event);
        }
        if (phase->walk_root && was_on_ground && walk_dir && a->x != prev_ax)
            phase->walk_tick = (phase->walk_tick + 1) % VALLEY_WALK_TICKS;
        phase->walk_dist += (uint32_t)(a->x >= prev_ax ? (a->x - prev_ax) : (prev_ax - a->x)) *
                            UNITS_PER_PIXEL;
        phase->state = is_vortex
                           ? (a->vx != 0 || (held & (PAD_UP | PAD_DOWN)) ? SIDESCRL_ST_WALK : SIDESCRL_ST_STAND)
                           : (!a->on_ground ? SIDESCRL_ST_AIR
                                            : (a->vx != 0 ? SIDESCRL_ST_WALK : SIDESCRL_ST_STAND));
        if (phase->body_config) bat_collect(phase, pad);
        valley_triggers_step(phase, prev_vy);
        mauso_controller_step(phase);
        sync_from_actor(phase);
        if (is_vortex)
            phase->ground_y = (uint32_t)phase->fly_y_px * UNITS_PER_PIXEL;
        glass_controller_step(phase);
        vortex_controller_step(phase);
        shadow_controller_step(phase, crouch, prev_ax);
        if (phase->skel.died) {
            phase->state = SIDESCRL_ST_LOST;
            sidescrl_end_sound(phase, 1u);
            snprintf(phase->status, sizeof phase->status,
                     "%s lost (skel) hits=%u", phase->stage_name,
                     phase->hazard_hits);
            return sidescrl_phase_render(phase);
        }
    }

    if ((pad->pressed & (phase->body_config ? PAD_JUMP : PAD_C)) &&
        stage_enemies_fire_blues(&phase->enemies, phase->bones_x,
                                 phase->bones_y, phase->facing, 0)) {
        phase->sfx_flags |= RUNWAY_SFX_FLAG_C;
    }
    if (!phase->body_config && (pad->pressed & PAD_A) &&
        stage_enemies_fire_blues(&phase->enemies, phase->bones_x,
                                 phase->bones_y, phase->facing, 1)) {
        phase->sfx_flags |= RUNWAY_SFX_FLAG_C;
    }
    glass_check_shots(phase);
    village_controller_step(phase, !phase->body_config && (pad->pressed & PAD_A) != 0);

    {
        int32_t dx = (int32_t)(phase->bones_x - phase->camera_x) / 8;
        phase->camera_x += (uint32_t)dx;
    }

    if (phase->anim_count > 0) {
        if (phase->state == SIDESCRL_ST_WALK && (phase->frames & 3u) == 0) {
            phase->anim_index = (phase->anim_index + 1) % phase->anim_count;
            if (phase->anim_index == 0 && phase->actor.on_ground && phase->actor.x != prev_ax) {
                phase->sfx_flags |= RUNWAY_SFX_FLAG_WALK;
                if (phase->laff.enabled && phase->stage_name[0])
                    runway_sfx_emit(0x2du);
            }
        }
        phase->sprite_rgba = phase->anim_rgba[phase->anim_index];
        phase->sprite_w = phase->anim_w[phase->anim_index];
        phase->sprite_h = phase->anim_h[phase->anim_index];
    }

    /* §24: poofs / sparkles / pickups / Blues shots → skel hit or heal. */
    if ((int32_t)phase->actor.y + ACTOR_FEET > (phase->map.vcam_on ? (int32_t)phase->map.planes[0].tiles.height_px : (int32_t)SCR_H) + 24) {
        (void)runway_skel_loose(&phase->skel, RUNWAY_SKEL_MAX + 1);
        phase->skel.died = 1;
        phase->state = SIDESCRL_ST_LOST;
        sidescrl_end_sound(phase, 1u);
        snprintf(phase->status, sizeof phase->status,
                 "%s lost (pit) x=%u", phase->stage_name,
                 phase->bones_x / UNITS_PER_PIXEL);
        return sidescrl_phase_render(phase);
    }
    {
        int prev_skel = phase->skel.skeletism;
        unsigned prev_hits = phase->enemies.hits_applied;
        int could_take_hit = phase->skel.hit_cooldown == 0;
        int bat_hp[STAGE_ENEMY_MAX] = {0};
        stage_batwood_contact contact;
        const stage_batwood_contact *bounds = NULL;
        if (phase->body_config) {
            for (unsigned i = 0; i < phase->enemies.count; i++) {
                const stage_enemy *enemy = &phase->enemies.ents[i];
                if (enemy->active && enemy->kind == STAGE_ENEMY_BAT)
                    bat_hp[i] = enemy->hp;
            }
        }
        if (!strcmp(phase->stage_name, "BAT2") || !strcmp(phase->stage_name, "BAT3")) {
            int feet = phase->actor.y + ACTOR_FEET - phase->floor_delta;
            contact.player = (BoundingBox){phase->actor.x + ACTOR_HALF_W - phase->actor.hit_width / 2,
                                          feet - phase->actor.hit_height,
                                          phase->actor.hit_width, phase->actor.hit_height};
            contact.floor = phase->map.vfloor;
            contact.floor_width = phase->map.strip_w;
            contact.camera_y = phase->map.camera_y;
            bounds = &contact;
        }
        int died = stage_enemies_step_player_bounded(&phase->enemies, &phase->skel, phase->bones_x,
                                                    phase->bones_y, phase->ground_y, phase->tall,
                                                    phase->airborne, NULL, 1, bounds);
        if (phase->body_config) {
            /* Player shots are resolved before contact damage, as in the BAT3 trace. */
            for (unsigned i = 0; i < phase->enemies.count; i++) {
                const stage_enemy *enemy = &phase->enemies.ents[i];
                if (enemy->kind == STAGE_ENEMY_BAT && enemy->hp < bat_hp[i])
                    runway_sfx_emit(0x29u); /* CBat::Hurt. */
            }
        }
        if (could_take_hit && phase->enemies.hits_applied != prev_hits && phase->stage_name[0]) {
            /* Hit_Generic differs by overlay; VORTEX and LAFF override it. */
            unsigned event = phase->laff.enabled ? 0x29u :
                             phase->body_config ? 0x1au :
                             phase->vortex.enabled ? 0x2cu : 0x1bu;
            runway_sfx_emit(event);
        }
        if (died) {
            phase->hazard_hits += phase->enemies.hits_applied;
            phase->state = SIDESCRL_ST_LOST;
            sidescrl_end_sound(phase, 1u);
            snprintf(phase->status, sizeof phase->status,
                     "%s lost (poof/skel) hits=%u enemies=%u", phase->stage_name,
                     phase->hazard_hits, phase->enemies.count);
            return sidescrl_phase_render(phase);
        }
        laff_controller_step(phase);
        if (phase->stage_name[0] != '\0' && !phase->skel.died &&
            phase->skel.skeletism < prev_skel) {
            if (phase->body_config) bat_detach_on_hit(phase);
            int32_t cur_cx = phase->actor.x + ACTOR_HALF_W;
            int32_t kb_col = cur_cx - phase->facing * 18;
            int pit_close = 0;
            int d_pit;
            for (d_pit = -16; d_pit <= 48; d_pit += 4) {
                if (is_pit_col(phase, cur_cx + d_pit)) {
                    pit_close = 1;
                    break;
                }
            }
            phase->hurt_timer = 18u;
            if (!pit_close && phase->actor.on_ground &&
                strcmp(phase->stage_name, "VORTEX") != 0 &&
                !is_pit_col(phase, kb_col)) {
                phase->knockback_vx = -phase->facing * (SIDESCRL_MOTION_WALK_MAX / 2);
                phase->actor.vy = -8800;
                phase->actor.on_ground = false;
                phase->on_plat_idx = -1;
            } else {
                phase->knockback_vx = 0;
            }
            stage_enemies_spawn_bone_burst(&phase->enemies, phase->bones_x,
                                           phase->bones_y, 5);
        }
    }
    phase->hazard_hits = phase->enemies.hits_applied;

    {
        unsigned boss_rem = sidescrl_phase_boss_remaining(phase);
        int at_exit = phase->finish_left ? phase->bones_x <= phase->finish_x
                                         : phase->bones_x >= phase->finish_x;
        if (phase->exit_config) at_exit = batwood_exit_contact(phase);
        else if (phase->mauso.enabled)
            at_exit = (phase->mauso.phase == 3 &&
                       (phase->mauso.bf_taken || phase->bones_x >= phase->finish_x));
        else if (phase->village.enabled && phase->village.little_guys_saved > 20u)
            at_exit = 1;
        else if (phase->finish_width) {
            int x = phase->actor.x + ACTOR_HALF_W - (int)(phase->finish_x / UNITS_PER_PIXEL);
            int y = phase->actor.y + ACTOR_FEET - phase->initial.hit.bot_right_y - phase->finish_y;
            at_exit = x > 0 && x < phase->finish_width && y > 0 && y < phase->finish_height;
        }
        if (boss_rem > 0) {
            if (phase->bones_x >= phase->finish_x) {
                uint32_t lock_px = phase->finish_x / UNITS_PER_PIXEL;
                phase->actor.x = (int32_t)lock_px - 1 - ACTOR_HALF_W;
                phase->actor.rem_x = 0;
                if (phase->actor.vx > 0) phase->actor.vx = 0;
                sync_from_actor(phase);
            }
        } else if (at_exit) {
            phase->state = SIDESCRL_ST_FINISHED;
            sidescrl_end_sound(phase, 0u);
            snprintf(phase->status, sizeof phase->status,
                     "%s finished (EndLevel win) skel=%d seg=%d hits=%u layers=%u "
                     "planes=%u amap=%u enemies=%u",
                     phase->stage_name, phase->skel.skeletism, phase->skel.segments,
                     phase->hazard_hits, phase->layer_count, phase->map.plane_count,
                     phase->map.amap_n, phase->enemies.count);
            return sidescrl_phase_render(phase);
        }
        {
            const char *st =
                phase->state == SIDESCRL_ST_AIR ? "air" :
                phase->state == SIDESCRL_ST_WALK ? "walk" : "stand";
            snprintf(phase->status, sizeof phase->status,
                     "%s %s x=%u y=%u skel=%d/%d seg=%d layers=%u planes=%u "
                     "enemies=%u boss=%u",
                     phase->stage_name, st, phase->bones_x / UNITS_PER_PIXEL,
                     phase->bones_y / UNITS_PER_PIXEL, phase->skel.skeletism,
                     RUNWAY_SKEL_MAX, phase->skel.segments, phase->layer_count,
                     phase->map.plane_count, phase->enemies.count, boss_rem);
        }
    }
    return sidescrl_phase_render(phase);
}

static void ss_blend_px(uint8_t *dst, unsigned w, unsigned h, int x, int y,
                        uint8_t r, uint8_t g, uint8_t b, int a) {
    uint8_t *p;
    if (a <= 0 || x < 0 || y < 0 || (unsigned)x >= w || (unsigned)y >= h) return;
    if (a > 255) a = 255;
    p = dst + ((size_t)(unsigned)y * w + (unsigned)x) * 4u;
    p[0] = (uint8_t)((p[0] * (255 - a) + r * a) / 255);
    p[1] = (uint8_t)((p[1] * (255 - a) + g * a) / 255);
    p[2] = (uint8_t)((p[2] * (255 - a) + b * a) / 255);
    p[3] = 255u;
}

static void ss_draw_disc(uint8_t *dst, unsigned w, unsigned h, int cx, int cy,
                         int rad, uint8_t r, uint8_t g, uint8_t b, int a) {
    int dx, dy, r2 = rad * rad;
    for (dy = -rad; dy <= rad; dy++) {
        for (dx = -rad; dx <= rad; dx++) {
            if (dx * dx + dy * dy <= r2)
                ss_blend_px(dst, w, h, cx + dx, cy + dy, r, g, b, a);
        }
    }
}

static void ss_draw_bat_bird(uint8_t *dst, int dx, unsigned frames) {
    int bx = 56 + (dx > 80 ? dx - 40 : 40) + (int)((frames / 2u) % 64u);
    int flap = ((frames >> 2) & 1u) ? -3 : 3;
    int by = 40 + (((frames >> 3) & 3u) * 2);
    if (bx > (int)SCR_W - 24) bx = (bx % ((int)SCR_W - 60)) + 24;
    ss_draw_disc(dst, SCR_W, SCR_H, bx, by, 3, 60, 195, 255, 235);
    ss_blend_px(dst, SCR_W, SCR_H, bx + 4, by - 1, 245, 210, 90, 245);
    ss_draw_disc(dst, SCR_W, SCR_H, bx - 4, by + flap, 2, 120, 225, 255, 220);
}

static void ss_draw_vortex_rings(uint8_t *dst, unsigned frames) {
    int k;
    for (k = 0; k < 4; k++) {
        int p = (int)((frames * 2u + (unsigned)k * 16u) % 64u);
        int rad = 12 + p;
        int a = 95 - (p * 75) / 64;
        int sx;
        for (sx = -rad; sx <= rad; sx += 2) {
            int y2 = rad * rad - sx * sx;
            int sry = 0;
            int sy;
            while ((sry + 1) * (sry + 1) <= y2) sry++;
            sy = (sry * 3) / 4;
            ss_blend_px(dst, SCR_W, SCR_H, 160 + sx, 108 - sy, 90, 220, 255, a);
            ss_blend_px(dst, SCR_W, SCR_H, 160 + sx, 108 + sy, 185, 130, 255, a);
        }
    }
}

static void ss_draw_shadow_eyes(uint8_t *dst, unsigned eye_phase) {
    uint8_t cr = 75, cg = 225, cb = 110;
    int r = 4;
    if (eye_phase >= 140u) {
        cr = 255; cg = 42; cb = 52; r = 5;
        /* Crimson gaze beam sweeping left across the corridor wall. */
        {
            int bx, spread;
            for (bx = 40; bx < 276; bx += 2) {
                spread = ((276 - bx) / 8) + 2;
                int y0 = 40 + ((276 - bx) / 3);
                int sy;
                for (sy = -spread; sy <= spread; sy += 2)
                    ss_blend_px(dst, SCR_W, SCR_H, bx, y0 + sy, 235, 35, 45, 52);
            }
        }
    } else if (eye_phase >= 105u) {
        cr = 255; cg = 195; cb = 55;
    }
    ss_draw_disc(dst, SCR_W, SCR_H, 268, 36, r, cr, cg, cb, 235);
    ss_draw_disc(dst, SCR_W, SCR_H, 282, 36, r, cr, cg, cb, 235);
}

int sidescrl_phase_render(sidescrl_phase *phase) {
    unsigned src_x;
    int dx, dy;
    char hud_txt[64];
    int boss_max = 0;
    int boss_hp;
    unsigned boss_rem;
    if (!phase || !phase->frame_rgba || !phase->scroll_rgba || !phase->sprite_rgba)
        return -1;
    stage_enemies_reset_bones_pose();
    boss_rem = sidescrl_phase_boss_remaining(phase);
    boss_hp = stage_enemies_laff_boss_hp(&phase->enemies, &boss_max);
    /*
     * DrawMap/CheckScroll stand-in. The map is drawn straight for this view: screen column x
     * shows plane A at column `src_x + x`, which is the world column the floor and the actors
     * use, and the slower planes lag behind it (scrl_map_draw). The view is applied once, here;
     * drawing the whole strip at the camera and then cropping it from `src_x` as well moved the
     * picture twice as far as the actors and cut plane A off against the floor. Without planes
     * (the synthetic rig) the strip is cropped as it is.
     */
    uint8_t view_floor[SCRL_STRIP_MAX];
    const uint8_t *floor_for_view = phase->map.floor_py;
    src_x = view_left(phase);
    if (phase->map.plane_count > 0) {
        int is_vortex_map = (strcmp(phase->stage_name, "VORTEX") == 0);
        if (is_vortex_map) {
            unsigned pi;
            int ph0 = (int)((phase->frames >> 2) & 15u);
            int sway = (ph0 < 8 ? ph0 - 4 : 12 - ph0);
            for (pi = 0; pi < phase->map.plane_count && pi < SCRL_PLANE_MAX; pi++) {
                scrl_plane *vp = &phase->map.planes[pi];
                int32_t cx = (vp->pixel_w > SCR_W) ? (int32_t)((vp->pixel_w - SCR_W) / 2u) : 0;
                vp->base_x = cx + ((pi == 0u) ? sway : (sway / 2));
            }
            scrl_map_draw(&phase->map, phase->palette, phase->frame_rgba, SCR_W, SCR_H, 0u);
        } else {
            if (phase->map.vcam_on) {
                int x;
                for (x = 0; x < (int)phase->map.strip_w; x++) {
                    int fy = (int)phase->map.vfloor[x] - phase->map.camera_y;
                    if (phase->map.floor_py[x] == SCRL_FLOOR_PIT) fy = SCRL_FLOOR_PIT;
                    else if (fy < 1) fy = 1;
                    else if (fy > (int)SCR_H - 1) fy = (int)SCR_H - 1;
                    view_floor[x] = (uint8_t)fy;
                }
                floor_for_view = view_floor;
            } else {
                int cy_tgt = camera_y_target(phase, src_x);
                int cy_d = cy_tgt - phase->map.camera_y;
                if (cy_d > CAMERA_Y_EASE) cy_d = CAMERA_Y_EASE;
                if (cy_d < -CAMERA_Y_EASE) cy_d = -CAMERA_Y_EASE;
                phase->map.camera_y += cy_d;
            }
            scrl_map_draw(&phase->map, phase->palette, phase->frame_rgba, SCR_W, SCR_H, src_x);
        }
    } else {
        unsigned y, x;
        for (y = 0; y < SCR_H; y++) {
            for (x = 0; x < SCR_W; x++) {
                size_t si = ((size_t)y * phase->scroll_w + src_x + x) * 4u;
                size_t di = ((size_t)y * SCR_W + x) * 4u;
                phase->frame_rgba[di + 0] = phase->scroll_rgba[si + 0];
                phase->frame_rgba[di + 1] = phase->scroll_rgba[si + 1];
                phase->frame_rgba[di + 2] = phase->scroll_rgba[si + 2];
                phase->frame_rgba[di + 3] = 255;
            }
        }
    }
    dx = (int)(phase->bones_x / UNITS_PER_PIXEL) - (int)src_x;
    if (dx < 12) dx = 12;
    if (dx > (int)SCR_W - 12) dx = (int)SCR_W - 12;
    dy = (int)(phase->bones_y / UNITS_PER_PIXEL);
    {
        int bpx = (int)(phase->bones_x / UNITS_PER_PIXEL);
        int gy = (bpx >= 0 && (unsigned)bpx < phase->map.strip_w &&
                  floor_for_view[bpx] > 0 &&
                  floor_for_view[bpx] < 224u)
                     ? (int)floor_for_view[bpx]
                     : (int)(phase->ground_y / UNITS_PER_PIXEL);
        int is_vortex = !strcmp(phase->stage_name, "VORTEX");
        int is_village = !strcmp(phase->stage_name, "VILLAGE");
        int is_bat = !strcmp(phase->stage_name, "BAT1") ||
                     !strcmp(phase->stage_name, "BAT2") ||
                     !strcmp(phase->stage_name, "BAT3");
        int draw_airborne = (phase->state == SIDESCRL_ST_AIR) || is_vortex;
        unsigned bones_anim;
        if (is_vortex) {
            bones_anim = (phase->frames / 6u) & 3u;
        } else if (phase->state == SIDESCRL_ST_AIR) {
            int32_t u = (int32_t)UNITS_PER_PIXEL;
            bones_anim = (phase->vel_y < -3 * u) ? 0u
                       : (phase->vel_y < 0)      ? 1u
                       : (phase->vel_y < 3 * u)  ? 2u
                                                 : 3u;
        } else if (phase->tall) {
            bones_anim = (phase->frames / 6u) & 1u;
        } else if (phase->state == SIDESCRL_ST_WALK) {
            bones_anim = phase->walk_root ? 1u + phase->walk_tick * 20u / VALLEY_WALK_TICKS :
                         1u + (phase->walk_dist / (5u * UNITS_PER_PIXEL)) % 20u;
        } else {
            bones_anim = 0u;
        }
        if (is_vortex)
            ss_draw_vortex_rings(phase->frame_rgba, phase->frames);
        if (is_bat)
            ss_draw_bat_bird(phase->frame_rgba, dx, phase->frames);
        if (!strcmp(phase->stage_name, "GLASS")) {
            stage_enemies_draw_glass_platforms(phase->frame_rgba, SCR_W, SCR_H,
                                               src_x, phase->map.strip_w,
                                               phase->finish_x / UNITS_PER_PIXEL,
                                               phase->frames);
        }
        if (phase->glass.enabled) {
            unsigned si;
            int kx = phase->glass.keyhole_x - (int)src_x;
            int ky = gy - 26;
            if (ky < 28) ky = 28;
            if (ky > (int)SCR_H - 24) ky = (int)SCR_H - 24;
            for (si = 0; si < phase->glass.shard_count; si++) {
                int sx = (int)phase->glass.shards[si].x0 - (int)src_x;
                int sw = (int)phase->glass.shards[si].x1 - (int)phase->glass.shards[si].x0;
                int sy = (int)phase->glass.shards[si].top_y - phase->map.camera_y;
                uint8_t cr = 95u, cg = 215u, cb = 225u;
                int broken = phase->glass.shards[si].broken;
                if (sx + sw < 0 || sx >= (int)SCR_W) continue;
                if (phase->glass.shards[si].kind == 1u) { cr = 240u; cg = 200u; cb = 95u; }
                else if (phase->glass.shards[si].kind == 2u) { cr = 235u; cg = 85u; cb = 95u; }
                else if (phase->glass.shards[si].kind == 3u) { cr = 130u; cg = 245u; cb = 255u; }
                else if (phase->glass.shards[si].kind == 4u) { cr = 85u; cg = 165u; cb = 255u; }
                for (int px = 0; px < sw; px += 3)
                    ss_blend_px(phase->frame_rgba, SCR_W, SCR_H, sx + px, sy,
                                cr, cg, cb, broken ? 45 : 155);
            }
            if (kx >= -20 && kx < (int)SCR_W + 20) {
                int unlocked = phase->glass.keyhole_unlocked;
                ss_draw_disc(phase->frame_rgba, SCR_W, SCR_H, kx, ky, 7,
                             unlocked ? 85u : 245u, unlocked ? 245u : 195u,
                             unlocked ? 155u : 75u, 225);
                ss_draw_disc(phase->frame_rgba, SCR_W, SCR_H, kx, ky, 3,
                             15, 20, 35, unlocked ? 90 : 235);
            }
        }
        if (phase->vortex.enabled) {
            unsigned ri;
            for (ri = 0; ri < phase->vortex.ring_count; ri++) {
                int rx, ry, age, rad;
                if (phase->vortex.rings[ri].cleared ||
                    phase->frames < phase->vortex.rings[ri].spawn_frame)
                    continue;
                age = (int)(phase->frames - phase->vortex.rings[ri].spawn_frame);
                rad = 8 + age * 4;
                rx = 160 + (phase->vortex.rings[ri].x - phase->vortex.rot_x) / 3;
                ry = 112 + (phase->vortex.rings[ri].y - phase->vortex.rot_y) / 3;
                ss_draw_disc(phase->frame_rgba, SCR_W, SCR_H, rx, ry, rad,
                             115, 225, 255, age < 6 ? 120 : 75);
            }
            if (phase->vortex.ring_count > 0u) {
                int fill_w = (int)((phase->vortex.rings_done * 64u) / phase->vortex.ring_count);
                for (int px = 0; px < fill_w; px++)
                    for (int py = 0; py < 4; py++)
                        ss_blend_px(phase->frame_rgba, SCR_W, SCR_H, 16 + px, 16 + py, 100, 225, 255, 215);
            }
        }
        if (phase->plat_count > 0u) {
            unsigned pi;
            for (pi = 0; pi < phase->plat_count; pi++) {
                const sidescrl_plat *pl = &phase->plats[pi];
                int sx0 = (int)pl->x0 - (int)src_x;
                int pw = (int)(pl->x1 - pl->x0);
                int py = (int)pl->y - (phase->map.vcam_on ? phase->map.camera_y : 0);
                int px_i, py_i;
                if (sx0 + pw < -16 || sx0 >= (int)SCR_W + 16) continue;
                if (pl->kind == 1u) {
                    stage_enemies_draw_bat_branch(phase->frame_rgba, SCR_W, SCR_H,
                                                  sx0, py, pw, pl->variant);
                } else if (pl->kind == 2u) {
                    if (pl->state == 2u) {
                        for (px_i = 0; px_i < pw; px_i += 6)
                            ss_blend_px(phase->frame_rgba, SCR_W, SCR_H, sx0 + px_i, py,
                                        110, 230, 210, 85);
                    } else {
                        int flash = (pl->state == 1u && ((phase->frames >> 1) & 1u));
                        for (py_i = 0; py_i < 6; py_i++) {
                            uint8_t r = (py_i < 2 || flash) ? 155u : 42u;
                            uint8_t g = (py_i < 2 || flash) ? 250u : 195u;
                            uint8_t b = (py_i < 2 || flash) ? 235u : 165u;
                            for (px_i = py_i / 2; px_i < pw - py_i / 2; px_i++)
                                ss_blend_px(phase->frame_rgba, SCR_W, SCR_H,
                                            sx0 + px_i, py + py_i, r, g, b, 215);
                        }
                    }
                } else {
                    for (py_i = 0; py_i < 7; py_i++) {
                        uint8_t r = (py_i == 0) ? 94u : (py_i < 4 ? 48u : 22u);
                        uint8_t g = (py_i == 0) ? 104u : (py_i < 4 ? 54u : 26u);
                        uint8_t b = (py_i == 0) ? 124u : (py_i < 4 ? 68u : 36u);
                        for (px_i = 0; px_i < pw; px_i++)
                            ss_blend_px(phase->frame_rgba, SCR_W, SCR_H,
                                        sx0 + px_i, py + py_i, r, g, b, 235);
                    }
                }
            }
        }
        if (!strcmp(phase->stage_name, "SHADOW")) {
            ss_draw_shadow_eyes(phase->frame_rgba, phase->shadow_eye_phase);
            if (phase->shadow.enabled) {
                int ti;
                if (phase->shadow.bh_active) {
                    int hx = phase->shadow.bh_x - (int)src_x;
                    int hy = gy - 18;
                    if (hx >= -16 && hx < (int)SCR_W + 16)
                        ss_draw_disc(phase->frame_rgba, SCR_W, SCR_H, hx, hy, 7, 70, 185, 255, 225);
                }
                for (ti = 0; ti < 7; ti++) {
                    int lit = (ti < 6) ? ((phase->shadow.trial_mask & (1u << ti)) != 0)
                                       : (phase->shadow.trials > 6);
                    ss_draw_disc(phase->frame_rgba, SCR_W, SCR_H, 248 + ti * 8, 48, 2,
                                 lit ? 90u : 55u, lit ? 235u : 60u, lit ? 215u : 75u, 220);
                }
            }
            stage_enemies_draw_wall_shadow(phase->frame_rgba, SCR_W, SCR_H,
                                           dx, dy, phase->facing, phase->tall,
                                           draw_airborne, bones_anim);
        }
        if (phase->fence_count > 0u || phase->fence_key_count > 0u || phase->blue_tomb_count > 0u) {
            int cam_y = phase->map.vcam_on ? phase->map.camera_y : 0;
            unsigned vi;
            for (vi = 0; vi < phase->blue_tomb_count; vi++) {
                int tx = phase->blue_tombs[vi].x - (int)src_x;
                int ty = phase->blue_tombs[vi].y - cam_y;
                if (tx < -24 || tx >= (int)SCR_W + 24 || ty < -32 || ty >= (int)SCR_H + 32) continue;
                int used = phase->blue_tombs[vi].used;
                uint8_t tr = used ? 65u : 80u, tg = used ? 85u : 175u, tb = used ? 120u : 255u;
                for (int py = -22; py <= 0; py++) {
                    for (int px = -9; px <= 9; px++)
                        ss_blend_px(phase->frame_rgba, SCR_W, SCR_H, tx + px, ty + py, tr, tg, tb, used ? 135 : 225);
                }
                if (!used) ss_draw_disc(phase->frame_rgba, SCR_W, SCR_H, tx, ty - 14, 4, 150, 235, 255, 190);
            }
            for (vi = 0; vi < phase->fence_key_count; vi++) {
                int kx = phase->fence_keys[vi].x - (int)src_x;
                int ky = phase->fence_keys[vi].y - cam_y;
                if (kx < -20 || kx >= (int)SCR_W + 20 || ky < -20 || ky >= (int)SCR_H + 20) continue;
                int trig = phase->fence_keys[vi].triggered;
                ss_draw_disc(phase->frame_rgba, SCR_W, SCR_H, kx, ky - 6, trig ? 4 : 6,
                             trig ? 75u : 60u, trig ? 165u : 245u, trig ? 130u : 255u, 225);
            }
            for (vi = 0; vi < phase->fence_count; vi++) {
                int fx = phase->fences[vi].x - (int)src_x;
                int fy = phase->fences[vi].y - cam_y;
                if (fx < -16 || fx >= (int)SCR_W + 16) continue;
                if (phase->fences[vi].open && phase->fences[vi].dissolve_timer <= 0) continue;
                int alpha = phase->fences[vi].open ? (phase->fences[vi].dissolve_timer * 8) : 210;
                for (int py = -48; py <= 8; py++) {
                    uint8_t g = ((py + (int)phase->frames) & 4) ? 235u : 155u;
                    for (int px = -4; px <= 4; px++)
                        ss_blend_px(phase->frame_rgba, SCR_W, SCR_H, fx + px, fy + py, 60, g, 255, alpha);
                }
            }
        }
        if (phase->mauso.enabled) {
            int cam_y = phase->map.vcam_on ? phase->map.camera_y : 0;
            int span = phase->mauso.arena_w > 80 ? (phase->mauso.arena_w - 80) : 480;
            for (unsigned ki = 0; ki < 16u; ki++) {
                if (!phase->mauso.key_timer[ki]) continue;
                int kx = phase->mauso.arena_x + 40 + (int)(ki * (unsigned)span / 16u) - (int)src_x;
                int ky = phase->mauso.arena_y - cam_y;
                if (kx < -16 || kx >= (int)SCR_W + 16) continue;
                ss_draw_disc(phase->frame_rgba, SCR_W, SCR_H, kx, ky - 8, 5,
                             95, 215, 255, 40 + (int)phase->mauso.key_timer[ki] * 22);
            }
            if (phase->mauso.bh_active || phase->mauso.bh_buzz > 0) {
                int hx = phase->mauso.bh_x - (int)src_x;
                int hy = phase->mauso.bh_y - cam_y - 16;
                ss_draw_disc(phase->frame_rgba, SCR_W, SCR_H, hx, hy,
                             phase->mauso.bh_active ? 8 : 11, 70, 175, 255,
                             phase->mauso.bh_active ? 225 : phase->mauso.bh_buzz * 12);
            }
            if (!phase->mauso.bf_taken) {
                int bx = phase->mauso.bf_x - (int)src_x;
                int hover = ((phase->frames >> 2) & 1u) ? -2 : 2;
                int by = phase->mauso.bf_y - cam_y - 24 + hover;
                ss_draw_disc(phase->frame_rgba, SCR_W, SCR_H, bx - 3, by, 3, 250, 210, 80, 235);
                ss_draw_disc(phase->frame_rgba, SCR_W, SCR_H, bx + 3, by, 3, 250, 210, 80, 235);
            }
        }
        stage_enemies_draw_houses(&phase->enemies, phase->frame_rgba, SCR_W, SCR_H,
                                  (int)src_x, phase->map.camera_y);
        if (phase->village.enabled) {
            int si;
            for (si = 0; si < phase->village.spider_count; si++) {
                int sx = phase->village.spiders[si].x - (int)src_x;
                int top_y = phase->village.spiders[si].top_y - phase->map.camera_y;
                int cur_y = phase->village.spiders[si].cur_y - phase->map.camera_y;
                int y;
                if (sx < -24 || sx >= (int)SCR_W + 24) continue;
                if (phase->village.spiders[si].state <= 1 && cur_y > top_y) {
                    for (y = top_y; y <= cur_y; y++)
                        ss_blend_px(phase->frame_rgba, SCR_W, SCR_H, sx, y, 225, 240, 255, 210);
                    ss_draw_disc(phase->frame_rgba, SCR_W, SCR_H, sx, cur_y, 5, 195, 70, 55, 235);
                    if (phase->village.spiders[si].has_guy)
                        ss_draw_disc(phase->frame_rgba, SCR_W, SCR_H, sx, cur_y + 7, 3, 245, 210, 120, 240);
                }
            }
            for (si = 0; si < phase->village.mushroom_count; si++) {
                int mx = phase->village.mushrooms[si].x - (int)src_x;
                int my = phase->village.mushrooms[si].y - phase->map.camera_y;
                if (phase->village.mushrooms[si].taken) continue;
                ss_draw_disc(phase->frame_rgba, SCR_W, SCR_H, mx, my - 5, 4, 90, 235, 195, 225);
            }
        }
        if (phase->laff.enabled) {
            unsigned lk;
            int laugh_bob = (phase->laff.joke_timer > 0u && phase->laff.last_joke_kind == 2)
                                ? ((phase->frames & 4u) ? -3 : 1)
                                : 0;
            for (lk = 0; lk < phase->laff.skel_count; lk++) {
                int lx = (int)phase->laff.skel_x[lk] - (int)src_x;
                int ly = (int)phase->laff.skel_y[lk] - phase->map.camera_y + laugh_bob;
                if (lx < -12 || lx >= (int)SCR_W + 12) continue;
                ss_draw_disc(phase->frame_rgba, SCR_W, SCR_H, lx, ly - 8, 3, 210, 220, 235, 175);
            }
            for (int px = 0; px < (phase->laff.laff_meter * 64) / 100; px++)
                for (int py = 0; py < 4; py++)
                    ss_blend_px(phase->frame_rgba, SCR_W, SCR_H, 232 + px, 16 + py, 85, 235, 120, 220);
        }
        stage_enemies_render_ex(&phase->enemies, NULL, phase->frame_rgba, SCR_W, SCR_H,
                                src_x, 0, (int)(phase->ground_y / UNITS_PER_PIXEL),
                                floor_for_view, phase->map.strip_w, 0);
        if (!strcmp(phase->stage_name, "LAFF") ||
            (phase->glass.enabled && !phase->glass.keyhole_unlocked)) {
            int gx = (int)(phase->finish_x / UNITS_PER_PIXEL) - (int)src_x;
            if (gx >= 2 && gx + 4 <= (int)SCR_W) {
                stage_enemies_draw_barrier(phase->frame_rgba, SCR_W, SCR_H,
                                           gx, (boss_rem > 0) ? 52 : 124, 192,
                                           boss_rem > 0, phase->frames);
            }
        }
        stage_enemies_set_bones_ground_y(gy, phase->stage_name);
        stage_enemies_set_bones_fx((int)phase->hurt_timer,
                                   phase->skel.hit_cooldown,
                                   phase->skel.died);
        if (phase->body_config) batwood_draw(phase,dx,dy,(int)src_x);
        else stage_enemies_draw_bones_ex(phase->frame_rgba, SCR_W, SCR_H, dx, dy,
                                    phase->facing, phase->tall, draw_airborne, bones_anim,
                                    phase->projection_px ? phase->projection_px : 28u, 28u);
        if (!strcmp(phase->stage_name, "MAUSO"))
            stage_enemies_draw_mauso_fg(phase->frame_rgba, SCR_W, SCR_H, src_x,
                                        phase->frames);
        if (is_village) {
            unsigned saved = phase->enemies.defeated_count +
                             (unsigned)((phase->bones_x / UNITS_PER_PIXEL) / 260u);
            if (saved > 6u) saved = 6u;
            stage_enemies_draw_village_hud(phase->frame_rgba, SCR_W, SCR_H,
                                           saved, 6u, phase->frames);
        }
        if (!strcmp(phase->stage_name, "LAFF")) {
            stage_enemies_draw_laff_meter(phase->frame_rgba, SCR_W, SCR_H,
                                          boss_hp, boss_max > 0 ? boss_max : 1,
                                          phase->frames);
        }
    }
    stage_enemies_draw_hud_skelbar(phase->frame_rgba, SCR_W, SCR_H,
                                   phase->skel.segments, RUNWAY_SKEL_SEGMENTS,
                                   214, 6);
    if (phase->stage_name[0]) {
        if (!strcmp(phase->stage_name, "LAFF")) {
            snprintf(hud_txt, sizeof hud_txt,
                     boss_rem > 0 ? "DA LAFF %d/%d" : "LAFF  GO ->",
                     boss_hp, boss_max);
            mb_font_draw_text_shadow(phase->frame_rgba, SCR_W, SCR_H, 8, 8,
                                     hud_txt,
                                     boss_rem > 0 ? 255 : 120,
                                     boss_rem > 0 ? 130 : 245,
                                     boss_rem > 0 ? 110 : 140,
                                     16, 12, 8, 1);
            if (boss_rem > 0 && boss_max > 0) {
                int seg, bx, by;
                for (seg = 0; seg < boss_max; seg++) {
                    int fill = (seg < boss_hp);
                    for (by = 20; by < 25; by++) {
                        for (bx = 8 + seg * 12; bx < 8 + seg * 12 + 10; bx++) {
                            uint8_t *p = phase->frame_rgba +
                                         ((size_t)by * SCR_W + (size_t)bx) * 4u;
                            p[0] = fill ? 240 : 48;
                            p[1] = fill ? 60 : 30;
                            p[2] = fill ? 95 : 45;
                            p[3] = 255;
                        }
                    }
                }
            }
        } else {
            snprintf(hud_txt, sizeof hud_txt, "%s", phase->stage_name);
            mb_font_draw_text_shadow(phase->frame_rgba, SCR_W, SCR_H, 8, 8,
                                     hud_txt, 245, 225, 130, 16, 12, 8, 1);
        }
    }
    runway_compose_hud_pause(phase->frame_rgba, SCR_W, SCR_H, NULL, 0, 0,
                             phase->state == SIDESCRL_ST_PAUSED);
    return 0;
}

int sidescrl_phase_done(const sidescrl_phase *phase) {
    return phase && (phase->state == SIDESCRL_ST_FINISHED ||
                     phase->state == SIDESCRL_ST_ABORTED ||
                     phase->state == SIDESCRL_ST_LOST);
}

unsigned sidescrl_phase_boss_remaining(const sidescrl_phase *phase) {
    if (!phase) return 0;
    if (phase->mauso.enabled)
        return (phase->mauso.phase != 3) ? 1u : 0u;
    if (phase->glass.enabled && !phase->glass.keyhole_unlocked)
        return 1u;
    if (phase->vortex.enabled && !phase->vortex.exit_unlocked)
        return 1u;
    if (phase->shadow.enabled && (!phase->shadow.config_ok || phase->shadow.trials <= 6))
        return 1u;
    return stage_enemies_laff_boss_alive(&phase->enemies) ? 1u : 0u;
}

/* Predict landing for the automated controller using the same boost and 2-bit terrain as play. */
static int jump_landing(const sidescrl_phase *phase, bool hold, int limit, bool platforms) {
    SidescrlActor arc = phase->actor;
    const scrl_tile_cache *tiles = &phase->map.planes[0].tiles;
    if (arc.on_ground) {
        arc.vy = sidescrl_project_impulse(&arc, arc.jump_impulse);
        arc.spring_ticks = arc.boost_duration;
        arc.jumping = true;
    }
    arc.on_ground = false;
    arc.spring_held = hold;
    do {
        int old_y = arc.y + ACTOR_FEET;
        sidescrl_physics_step(&arc, NULL, 0);
        int x = arc.x + ACTOR_HALF_W, y = arc.y + ACTOR_FEET;
        if ((unsigned)x >= tiles->width_px || y >= (int)tiles->height_px) break;
        if (arc.vy <= 0) continue;
        for (int row = old_y; row <= y && row <= limit; row++) {
            if (row > 0 && scrl_tilecache_hixel(tiles, (unsigned)x, (unsigned)row) > 0 &&
                scrl_tilecache_hixel(tiles, (unsigned)x, (unsigned)(row - 1)) <= 0) return row;
        }
        if (platforms) {
            for (unsigned i = 0; i < phase->plat_count; i++) {
                const sidescrl_plat *pl = &phase->plats[i];
                if (pl->state != 2 && touches_platform(phase, pl, x, old_y, y)) return pl->y;
            }
        }
    } while (arc.vy <= 0 || arc.y + ACTOR_FEET <= limit);
    return (int)tiles->height_px;
}

/* Approach the outer arm from below before jumping at a wall. A spring can
 * be behind the actor after a failed jump, in either direction of travel. */
static unsigned approach_branch(const sidescrl_phase *phase) {
    const SidescrlActor *a = &phase->actor;
    int x = a->x + ACTOR_HALF_W, feet = a->y + ACTOR_FEET;
    const scrl_tile_cache *tiles = &phase->map.planes[0].tiles;
    if (!phase->map.vcam_on || phase->on_plat_idx >= 0 || phase->branch_launch) return 0;
    for (unsigned i = 0; i < phase->plat_count; i++) {
        const sidescrl_plat *pl = &phase->plats[i];
        if (pl->kind != 1 || feet <= pl->y) continue;
        int length = pl->x1 - pl->x0;
        int target = pl->anchor_x == pl->x0 ? pl->x1 - a->hit_width : pl->x0 + a->hit_width;
        if (abs(x - target) > 2*length || feet - pl->y > 145) continue;
        int step = phase->finish_left ? -1 : 1;
        int ledge = target;
        for (; abs(ledge - target) <= 2*length; ledge += step)
            if (scrl_tilecache_hixel(tiles, (unsigned)ledge, (unsigned)(pl->y - 1)) == 3) break;
        if (abs(ledge - target) > 2*length) continue;
        int drift = a->on_ground ? 0 : (int)((int64_t)a->vx * abs(a->vx) /
                                             (2 * 340 * SIDESCRL_PHYS_UNITS_PER_PIXEL));
        int dx = target - (x + drift);
        unsigned pad = dx > 1 ? MB_PAD_RIGHT : dx < -1 ? MB_PAD_LEFT : 0;
        if (abs(target-x) < a->hit_width || (!a->on_ground && a->vy < 0)) pad |= MB_PAD_B;
        if (!pad) pad = MB_PAD_C;
        return pad;
    }
    return 0;
}

/* Muted campaign playback uses a recorded controller route through the retail hixels.
 * Frame boundaries are exclusive. Interactive input always uses the regular pad. */
static unsigned batwood_autoplay(const sidescrl_phase *p, unsigned frame) {
    enum { L = MB_PAD_LEFT, R = MB_PAD_RIGHT, U = MB_PAD_UP, D = MB_PAD_DOWN, C = MB_PAD_C };
    struct input { unsigned short until; unsigned char buttons; };
    static const struct input bat2[] = {
        {8,R}, {32,R|U|C}, {40,U|C}, {48,0}, {72,R|U|C}, {80,R},
        {112,R|U|C}, {120,R}, {136,R|U|C}, {144,L|U|C}, {152,U|C}, {160,R|U|C},
        {184,L|U|C}, {216,R|U|C}, {217,R|U}, {224,R|U|C}, {258,R}, {259,R|U},
        {285,R|U|C}, {330,R}, {339,R|U|C}, {375,R}, {391,R|U|C}, {399,R|C},
        {407,R}, {408,R|U}, {415,R|U|C}, {447,R}, {463,R|U|C}, {471,R},
        {479,R|U|C}, {495,R}, {519,R|U|C}, {535,R}, {543,R|U|C}, {559,R},
        {583,R|U|C}, {639,R}, {647,0}, {743,R}, {751,R|U|C}, {759,L|U|C},
        {767,0}, {775,L|U|C}, {783,R|C}, {784,R|U}, {799,R|U|C}, {807,R},
        {808,L|U}, {815,L|U|C}, {823,R|U|C}, {824,L|U}, {831,L|U|C}, {839,R},
        {847,L|U|C}, {855,R|C}, {871,R|U|C}, {872,R|U}, {879,R|U|C}, {895,L|U|C},
        {903,L}, {935,R|U|C}, {936,R|U}, {943,R|U|C}, {951,L|U|C}, {959,L},
        {967,0}, {1007,R|U|C}, {1033,R}
    };
    /* Native BAT3 replay with the climb inputs at the stepped ledges. */
    static const struct input bat3[] = {
        {8,L}, {12,R}, {20,L}, {24,0}, {28,R}, {32,R|C}, {56,R},
        {60,C}, {64,R}, {68,0}, {72,R}, {76,L}, {80,0}, {96,R},
        {104,R|C}, {112,R}, {124,L}, {128,0}, {132,R}, {136,D|C}, {140,R},
        {144,L}, {152,L|C}, {168,R|C}, {192,R}, {208,R|C}, {217,R}, {221,R|C},
        {249,R}, {253,R|C}, {289,R}, {293,R|C}, {333,R}, {341,R|C}, {349,L},
        {353,0}, {357,R|C}, {361,C}, {369,R}, {373,D|C}, {377,R}, {389,R|C},
        {409,R}, {413,C}, {421,R|C}, {429,R}, {433,L}, {442,R}, {454,R|C},
        {466,R}, {470,R|C}, {502,R}, {510,R|C}, {534,R}, {538,R|C}, {558,R},
        {566,R|C}, {604,R}, {612,R|C}, {628,R}, {632,R|C}, {720,R}, {724,0},
        {736,R|C}, {740,C}, {744,0}, {752,R|C}, {756,R|U|C}, {772,R|C}, {827,R},
        {839,R|C}, {859,R}, {879,R|C}, {905,R}, {909,R|C}, {957,R}, {961,R|C},
        {1007,R}, {1019,R|C}, {1027,R}, {1031,R|C}, {1093,R}, {1101,R|C}, {1152,R},
        {1164,R|C}, {1172,R}, {1176,0}, {1190,R}, {1194,0}, {1198,L|C}, {1206,R|C},
        {1210,R}, {1214,0}, {1222,R|C}, {1250,R}, {1262,R|C}, {1297,R}, {1301,R|C},
        {1333,R}, {1341,R|C}, {1417,R}, {1421,R|C}, {1447,R}, {1451,R|C}, {1516,R},
        {1520,R|C}, {1528,R}, {1536,L}, {1544,0}, {1552,R|C}, {1564,R}, {1572,L},
        {1584,C}, {1592,R|C}, {1596,L|C}, {1616,R}, {1644,R|C}, {1664,R}, {1668,R|C},
        {1705,R}, {1709,L|C}, {1713,R|C}, {1717,L|C}, {1729,R}, {1733,R|C}, {1757,R},
        {1761,0}, {1765,R}, {1769,R|C}, {1781,L|C}, {1785,L}, {1793,0}, {1797,L},
        {1801,L|D|C}, {1805,R|C}, {1809,L|C}, {1813,R|C}, {1817,L|C}, {1827,R}, {1831,R|C},
        {1848,R},
    };
    int first = !strcmp(p->stage_name, "BAT1");
    int second = !strcmp(p->stage_name, "BAT2");
    if (first) {
        int cur_px = p->actor.x + ACTOR_HALF_W;
        int feet = p->actor.y + ACTOR_FEET;
        const scrl_tile_cache *tiles = &p->map.planes[0].tiles;
        unsigned btn = (cur_px > (int)(p->finish_x / UNITS_PER_PIXEL) + 8) ? MB_PAD_LEFT : MB_PAD_RIGHT;
        unsigned bp;
        int enemy_near = 0;
        for (unsigned ei = 0; ei < p->enemies.count; ei++) {
            const stage_enemy *ent = &p->enemies.ents[ei];
            if (!ent->active) continue;
            if (abs((int)(ent->x / UNITS_PER_PIXEL) - cur_px) <= 130) {
                enemy_near = 1;
                break;
            }
        }
        if (p->body_config == p->exit_config) {
            if (p->latch != BAT_FREE) {
                if (p->frames > p->latch_frame + 55u) return btn;
                if (p->latch == BAT_HANG &&
                    scrl_tilecache_hixel(tiles, (unsigned)cur_px, (unsigned)(p->latch_y - 4)) != 3)
                    return U | C | btn;
                return C | btn;
            }
            if (p->on_plat_idx >= 0 && p->plats[p->on_plat_idx].kind == 1)
                return C | (btn & MB_PAD_LEFT);
            bp = approach_branch(p);
            if (bp) {
                if (bp & MB_PAD_B) {
                    bp &= ~MB_PAD_B;
                    if (!p->actor.on_ground || (frame & 1u) == 0u) bp |= C;
                }
                if (enemy_near && (frame % 4u) == 0u) bp |= MB_PAD_B;
                return bp;
            }
            if (enemy_near && (frame % 4u) == 0u) btn |= MB_PAD_B;
            if (p->actor.on_ground) {
                int step_x = (btn & MB_PAD_LEFT) ? -10 : 10;
                if ((cur_px <= 340 && feet > p->finish_y + 18) ||
                    (cur_px + step_x >= 0 && (unsigned)(cur_px + step_x) < tiles->width_px &&
                     terrain_wall((void *)p, cur_px + step_x, feet - MAX_CLIMB_STEP - 1)) ||
                    (p->actor.vx == 0 && frame > 4u))
                    if ((frame & 1u) == 0u) btn |= C;
            } else {
                btn |= C;
            }
            return btn;
        }
        /* Stay healthy once excess limbs are shed (0x2d754c SS / 0x2d716c SSR). */
        if (p->body_config == 0x2d754cu || p->body_config == 0x2d716cu) {
            unsigned want_mask = (p->body_config == 0x2d754cu) ? 0x0au : 0x02u;
            int best_d = 0x7fffffff, best_x = cur_px;
            if (enemy_near && (frame % 4u) == 0u) btn |= MB_PAD_B;
            for (unsigned bi = 0; bi < p->body_piece_count; bi++) {
                int px, py, d;
                if (p->collected & (1u << bi)) continue;
                if (!(p->body_pieces[bi].mask & want_mask)) continue;
                px = p->body_pieces[bi].x / 256;
                py = p->body_pieces[bi].y / 256;
                if (px > cur_px + 80 || abs(py - feet) > 150) continue;
                d = abs(px - cur_px) + abs(py - feet);
                if (d < best_d) { best_d = d; best_x = px; }
            }
            if (best_d < 220) {
                btn = (best_x < cur_px - 6) ? MB_PAD_LEFT : ((best_x > cur_px + 6) ? MB_PAD_RIGHT : 0u);
                if ((frame % 4u) == 1u) btn |= MB_PAD_A;
                if (enemy_near && (frame % 4u) == 0u) btn |= MB_PAD_B;
                if (p->actor.on_ground && (frame % 24u) == 0u) btn |= C;
                return btn;
            }
        }
        bp = approach_branch(p);
        if (bp) {
            if (bp & MB_PAD_B) {
                bp &= ~MB_PAD_B;
                if (!p->actor.on_ground || (frame & 1u) == 0u) bp |= C;
            }
            return bp;
        }
        if (p->latch != BAT_FREE) {
            if (p->frames > p->latch_frame + 55u) return btn;
            if (p->latch == BAT_HANG &&
                scrl_tilecache_hixel(tiles, (unsigned)cur_px, (unsigned)(p->latch_y - 4)) != 3)
                return U | C | btn;
            return C | btn;
        }
        if (p->actor.on_ground) {
            int ahead = cur_px + ((btn & MB_PAD_LEFT) ? -10 : 10);
            if ((ahead >= 0 && (unsigned)ahead < tiles->width_px &&
                 terrain_wall((void *)p, ahead, feet - MAX_CLIMB_STEP - 1)) ||
                (p->actor.vx == 0 && frame > 4u))
                if ((frame & 1u) == 0u) btn |= C;
        } else if (p->vel_y < 0) {
            btn |= C;
        }
        return btn;
    }
    const struct input *route = second ? bat2 : bat3;
    size_t count = second ? sizeof bat2 / sizeof *bat2 : sizeof bat3 / sizeof *bat3;
    for (size_t i = 0; i < count; i++) {
        if (frame >= route[i].until) continue;
        unsigned buttons = route[i].buttons;
        if (second) {
            if (frame % 6 == 0) buttons |= MB_PAD_B;
            if (frame % 4 == 0 && ((frame >= 536 && frame <= 644) ||
                                   (frame >= 744 && frame <= 836)))
                buttons |= MB_PAD_A;
        } else {
            if (frame % 8 == 0) buttons |= MB_PAD_B;
            if (frame < 280 && frame % 4 == 0) buttons |= MB_PAD_A;
        }
        return buttons;
    }
    return 0;
}

unsigned sidescrl_phase_autoplay_buttons(const sidescrl_phase *phase,
                                         unsigned frame) {
    unsigned btn = MB_PAD_RIGHT;
    unsigned cur_px, cur_y, scan;
    int want_jump = 0, pit_ahead = 0, want_c = 0, want_a = 0;
    if (!phase) return MB_PAD_RIGHT;
    if (phase->exit_config) return batwood_autoplay(phase, frame);
    cur_px = phase->bones_x / UNITS_PER_PIXEL;
    cur_y = phase->bones_y / UNITS_PER_PIXEL;
    if (phase->finish_left) btn = MB_PAD_LEFT;
    if (phase->branch_launch && !phase->actor.on_ground) return btn | MB_PAD_C;
    unsigned branch_pad = approach_branch(phase);
    if (branch_pad) return branch_pad;
    if (phase->fence_key_count > 0u) {
        int cur_feet = phase->actor.y + ACTOR_FEET;
        if (phase->fence_key_count >= 2u && !phase->fence_keys[1].triggered) {
            /* Key 2 (6055,819) sits down shaft x=6280 through 1-way ledges [686]/[748]/[822]. */
            if (cur_feet < 840 && cur_px >= 6220u && cur_px <= 6380u) {
                if (cur_feet >= 670 && cur_px >= 6260u && cur_px <= 6300u)
                    return MB_PAD_DOWN | ((phase->actor.on_ground && (frame & 1u) == 0u) ? MB_PAD_B : 0u);
                return (cur_px < 6275u) ? MB_PAD_RIGHT : ((cur_px > 6290u) ? MB_PAD_LEFT : 0u);
            }
            if (cur_feet >= 730 && cur_px >= 5980u && cur_px <= 6360u) {
                int dx2 = phase->fence_keys[1].x - (int)cur_px;
                btn = (dx2 > 3) ? MB_PAD_RIGHT : ((dx2 < -3) ? MB_PAD_LEFT : 0u);
                if (abs(dx2) <= 18 && ((!phase->airborne && (frame & 1u) == 0u) || phase->vel_y < 0))
                    btn |= MB_PAD_B;
                return btn;
            }
        } else if (phase->fence_key_count >= 2u && phase->fence_keys[1].triggered &&
                   cur_feet >= 670 && cur_px >= 5980u && cur_px <= 6360u) {
            btn = (cur_px < 6274u) ? MB_PAD_RIGHT : ((cur_px > 6292u) ? MB_PAD_LEFT : 0u);
            if ((!phase->airborne && (frame & 1u) == 0u) || phase->vel_y < 0)
                btn |= MB_PAD_B;
            return btn;
        }
        if (phase->fence_key_count >= 3u && !phase->fence_keys[2].triggered &&
            cur_px >= 10530u && cur_px <= 10780u) {
            int dx3 = phase->fence_keys[2].x - (int)cur_px;
            if (cur_feet > 525) {
                btn = (dx3 > 12) ? MB_PAD_RIGHT : ((dx3 < -12) ? MB_PAD_LEFT : 0u);
                if ((!phase->airborne && (frame & 1u) == 0u) || phase->vel_y < 0)
                    btn |= MB_PAD_X;
                return btn;
            }
            if (!phase->airborne && abs(dx3) <= 18 && abs(phase->actor.vx) > 2500)
                return 0u;
            btn = (dx3 > 8) ? MB_PAD_RIGHT : ((dx3 < -8) ? MB_PAD_LEFT : 0u);
            if ((abs(dx3) <= 14 && !phase->airborne && abs(phase->actor.vx) <= 2500 &&
                 (frame & 1u) == 0u) ||
                (phase->airborne && phase->vel_y < 0 && abs(dx3) <= 20))
                btn |= MB_PAD_B;
            return btn;
        }
        if (phase->fence_key_count >= 3u && phase->fence_keys[2].triggered &&
            cur_px >= 10985u && cur_px <= 11055u && cur_feet >= 715) {
            if (phase->actor.vx == 0 && !phase->airborne)
                return MB_PAD_RIGHT | MB_PAD_B;
            return MB_PAD_RIGHT;
        }
        for (unsigned fi = 0; fi < phase->fence_key_count; fi++) {
            if (phase->fence_keys[fi].triggered) continue;
            int dx = phase->fence_keys[fi].x - (int)cur_px;
            int dy = abs(phase->fence_keys[fi].y - cur_feet);
            if (dx >= -12 && dx <= 20 && dy <= 70) {
                btn = (dx > 3) ? MB_PAD_RIGHT : ((dx < -3) ? MB_PAD_LEFT : 0u);
                if ((!phase->airborne && (frame & 1u) == 0u) || phase->vel_y < 0)
                    btn |= MB_PAD_B;
                return btn;
            }
        }
    }
    if (phase->mauso.enabled) {
        if (phase->mauso.phase == 1) {
            if ((int)cur_px < phase->mauso.left_edge + 90)
                return MB_PAD_RIGHT;
            if (phase->mauso.bh_active && abs((int)cur_px - phase->mauso.bh_x) > 10)
                return ((int)cur_px < phase->mauso.bh_x ? MB_PAD_RIGHT : MB_PAD_LEFT);
            return ((frame % 24u) < 4u && !phase->airborne) ? (MB_PAD_RIGHT | MB_PAD_B) : MB_PAD_RIGHT;
        }
        if (phase->mauso.phase == 2) {
            const stage_enemy *target = NULL;
            int best_d = 0x7fffffff;
            for (scan = 0; scan < phase->enemies.count; scan++) {
                const stage_enemy *ent = &phase->enemies.ents[scan];
                int d;
                if (!ent->active || ent->kind != STAGE_ENEMY_LORD) continue;
                d = abs((int)(ent->x / UNITS_PER_PIXEL) - (int)cur_px);
                if (d < best_d) { best_d = d; target = ent; }
            }
            if (target) {
                int dx_t = (int)(target->x / UNITS_PER_PIXEL) - (int)cur_px;
                btn = (dx_t >= 0) ? MB_PAD_RIGHT : MB_PAD_LEFT;
                if (abs(dx_t) < 28) {
                    btn = ((int)cur_px > phase->mauso.left_edge + 120) ? MB_PAD_LEFT : MB_PAD_RIGHT;
                    if (!phase->airborne && (frame & 1u) == 0u) btn |= MB_PAD_B;
                }
                if ((frame % 4u) == 0u) btn |= MB_PAD_C;
                return btn;
            }
        }
    }
    if (phase->map.vcam_on && phase->actor.on_ground) {
        const BoundingBox box = sidescrl_actor_box(&phase->actor);
        int feet = box.y + box.h;
        for (int look = 0; look <= 24; look += 2) {
            int x = phase->finish_left ? box.x - look - 1 : box.x + box.w + look;
            if (terrain_wall((void *)phase, x, feet - MAX_CLIMB_STEP - 1) ||
                terrain_ground(phase, x) > feet + phase->actor.hit_height) {
                want_jump = 1;
                break;
            }
        }
        if (phase->frames && phase->actor.vx == 0) want_jump = 1;
        if (!want_jump && phase->actor.vx && jump_landing(phase, true, feet, true) < feet - MAX_CLIMB_STEP)
            want_jump = 1;
    }

    if (!strcmp(phase->stage_name, "LAFF") &&
        sidescrl_phase_boss_remaining(phase) > 0) {
        const stage_enemy *boss = NULL;
        for (scan = 0; scan < phase->enemies.count; scan++) {
            if (phase->enemies.ents[scan].active &&
                phase->enemies.ents[scan].kind == STAGE_ENEMY_LORD) {
                boss = &phase->enemies.ents[scan];
                break;
            }
        }
        if (boss) {
            unsigned boss_px = boss->x / UNITS_PER_PIXEL;
            unsigned u = phase->enemies.units_per_pixel ? phase->enemies.units_per_pixel
                                                        : UNITS_PER_PIXEL;
            unsigned boss_y = boss->y / u;
            unsigned finish_px = phase->finish_x / UNITS_PER_PIXEL;
            unsigned arena_left = finish_px > 300u ? finish_px - 300u : 80u;
            int dx_boss = (int)boss_px - (int)cur_px;
            int abs_dx = dx_boss >= 0 ? dx_boss : -dx_boss;
            if (cur_px + 130u >= boss_px || cur_px + 220u >= finish_px) {
                btn = 0u;
                if (abs_dx > 84) {
                    btn = (dx_boss >= 0) ? MB_PAD_RIGHT : MB_PAD_LEFT;
                } else if (abs_dx < 36) {
                    if (dx_boss >= 0 && cur_px > arena_left)
                        btn = MB_PAD_LEFT;
                    else if (dx_boss < 0 && cur_px + 40u < finish_px)
                        btn = MB_PAD_RIGHT;
                } else if (dx_boss >= 0 && phase->facing < 0) {
                    btn = MB_PAD_RIGHT;
                } else if (dx_boss < 0 && phase->facing > 0) {
                    btn = MB_PAD_LEFT;
                }
                for (scan = 0; scan < phase->enemies.count; scan++) {
                    const stage_enemy *t = &phase->enemies.ents[scan];
                    int td;
                    if (!t->active || t->kind != STAGE_ENEMY_THROWN) continue;
                    td = (int)(t->x / u) - (int)cur_px;
                    if (td >= -36 && td <= 36) {
                        want_jump = 1;
                        break;
                    }
                }
                if (abs_dx < 26 && boss_y + 18u >= cur_y)
                    want_jump = 1;
                if (want_jump && !phase->airborne && (frame & 1u) == 0u)
                    btn |= MB_PAD_B;
                if (abs_dx <= 40 && (frame % 16u) == 2u)
                    btn |= MB_PAD_A;
                else if (abs_dx <= 140 && (frame % 4u) == 0u)
                    btn |= MB_PAD_C;
                return btn;
            }
        }
    }

    if (phase->plat_count > 0u && phase->on_plat_idx >= 0 &&
        phase->on_plat_idx < (int)phase->plat_count) {
        const sidescrl_plat *cur_pl = &phase->plats[phase->on_plat_idx];
        if (cur_pl->kind == 1u) btn |= MB_PAD_C;
        if (cur_pl->state == 1u && cur_pl->timer <= 16u) want_jump = 1;
        if (!phase->airborne && phase->actor.vx == 0) want_jump = 1;
        if (cur_px + 14u >= cur_pl->x1) {
            for (scan = 1u; scan <= 28u; scan++) {
                unsigned ahead = cur_px + scan;
                if (ahead < phase->scroll_w && phase->map.floor_py[ahead] == SCRL_FLOOR_PIT) {
                    want_jump = 1;
                    break;
                }
            }
        }
    }
    for (scan = 2u; scan <= 16u; scan++) {
        unsigned col = cur_px + scan;
        if (col >= phase->scroll_w) break;
        if ((scan <= 6u && phase->map.floor_py[col] == SCRL_FLOOR_PIT) ||
            ((scan <= 8u || (!phase->airborne && phase->actor.vx == 0 && scan <= ACTOR_HALF_W + 2u)) &&
             phase->map.wall[col]) ||
            ((scan <= 8u || (!phase->airborne && phase->actor.vx == 0 && scan <= ACTOR_HALF_W + 2u)) &&
             col > 0u &&
             phase->map.floor_py[col] != SCRL_FLOOR_PIT &&
             phase->map.floor_py[col - 1u] != SCRL_FLOOR_PIT &&
             (int)phase->map.floor_py[col - 1u] - (int)phase->map.floor_py[col] > MAX_CLIMB_STEP) ||
            (phase->actor.vx > 0 && phase->map.floor_py[col] != SCRL_FLOOR_PIT &&
             scan >= 4u &&
             (int)cur_y - (int)phase->map.floor_py[col] >= (int)(scan * 4u))) {
            want_jump = 1;
            break;
        }
        /* Jump before dropping into a narrow V-gorge whose far side rises steeply
         * (e.g. BAT1 [688..714], BAT3 [333..376]), while walking smoothly down open slopes. */
        if (phase->map.floor_py[col] != SCRL_FLOOR_PIT &&
            phase->map.floor_py[col] > cur_y + 24u) {
            unsigned col2;
            unsigned end2 = col + 28u;
            if (end2 > phase->scroll_w) end2 = phase->scroll_w;
            for (col2 = col + 1u; col2 < end2; col2++) {
                if (phase->map.floor_py[col2] != SCRL_FLOOR_PIT &&
                    ((int)phase->map.floor_py[col] - (int)phase->map.floor_py[col2] >= 28 ||
                     phase->map.wall[col2])) {
                    want_jump = 1;
                    break;
                }
            }
            if (want_jump) break;
        }
    }
    for (scan = 14u; scan <= 150u; scan++) {
        unsigned col = cur_px + scan;
        if (col >= phase->scroll_w) break;
        if (phase->map.floor_py[col] == SCRL_FLOOR_PIT) {
            pit_ahead = 1;
            break;
        }
    }
    for (scan = 0; scan < phase->enemies.count; scan++) {
        const stage_enemy *ent = &phase->enemies.ents[scan];
        int32_t dx;
        if (!ent->active) continue;
        dx = (int32_t)(ent->x / UNITS_PER_PIXEL) - (int32_t)cur_px;
        if (dx >= -8 && dx <= 110) {
            want_c = 1;
            if (!pit_ahead && strcmp(phase->stage_name, "VORTEX") != 0 &&
                (ent->kind == STAGE_ENEMY_HAND ||
                 ent->kind == STAGE_ENEMY_LILGUYS ||
                 ent->kind == STAGE_ENEMY_ZOMBIE ||
                 ent->kind == STAGE_ENEMY_POOF_RED ||
                 ent->kind == STAGE_ENEMY_THROWN)) {
                if (dx >= 0 && dx <= 32) want_jump = 1;
            }
            if (!strcmp(phase->stage_name, "VORTEX") && dx >= 0 && dx <= 72) {
                int32_t ey = (int32_t)(ent->y / UNITS_PER_PIXEL);
                int32_t dy_e = ey - (int32_t)cur_y;
                if (dy_e >= -22 && dy_e <= 22)
                    btn |= (cur_y >= 112u) ? MB_PAD_UP : MB_PAD_DOWN;
            }
            if (ent->kind == STAGE_ENEMY_SHADOW && dx >= -28 && dx <= 48)
                want_a = 1;
        }
    }
    if (phase->vortex.enabled && !phase->vortex.exit_unlocked &&
        cur_px + 32u >= phase->finish_x / UNITS_PER_PIXEL) {
        btn |= ((frame / 18u) & 1u) ? MB_PAD_UP : MB_PAD_DOWN;
        want_c = 1;
    }
    if (phase->village.enabled) {
        int si;
        for (si = 0; si < phase->village.spider_count; si++) {
            int sdx = phase->village.spiders[si].x - (int)cur_px;
            if (phase->village.spiders[si].state <= 1 && sdx >= -8 && sdx <= 100) {
                want_c = 1;
                break;
            }
        }
    }
    if (!strcmp(phase->stage_name, "SHADOW")) {
        if (phase->shadow_eye_phase >= 126u)
            return MB_PAD_DOWN | ((want_a && (frame % 16u) == 0u) ? MB_PAD_A : 0u);
        if (phase->shadow.enabled && phase->shadow.trials <= 6) {
            int target_x = 340;
            if (phase->shadow.bh_active && phase->skel.skeletism < 0x16000)
                target_x = phase->shadow.bh_x;
            else if (phase->shadow_eye_phase >= 118u)
                target_x = (int)cur_px;
            if (target_x < (int)cur_px - 16)
                btn = MB_PAD_LEFT;
            else if (target_x > (int)cur_px + 16)
                btn = MB_PAD_RIGHT;
            else
                btn = 0u;
            for (scan = 0; scan < phase->enemies.count; scan++) {
                const stage_enemy *ent = &phase->enemies.ents[scan];
                int32_t edx;
                if (!ent->active) continue;
                edx = (int32_t)(ent->x / UNITS_PER_PIXEL) - (int32_t)cur_px;
                if (edx < -8 && edx >= -96 && abs(edx) <= 72) {
                    btn = MB_PAD_LEFT;
                    want_c = 1;
                    break;
                }
            }
            want_jump = 0;
        }
    }
    {
        int near_pit = 0;
        for (scan = 0u; scan <= 28u; scan++) {
            unsigned col = cur_px + scan;
            if (col < phase->scroll_w && phase->map.floor_py[col] == SCRL_FLOOR_PIT) {
                near_pit = 1;
                break;
            }
        }
        if ((want_jump && (!phase->airborne || phase->coyote_timer > 0u) &&
             (near_pit || (frame & 1u) == 0u)) ||
            (phase->airborne && (near_pit || phase->map.vcam_on) && phase->vel_y < 0))
            btn |= MB_PAD_B;
    }
    if (want_a && (frame % 16u) == 0u)
        btn |= MB_PAD_A;
    else if ((want_c && (frame % 6u) == 0u) || (frame % 14u) == 0u)
        btn |= MB_PAD_C;
    if (phase->walk_root && (btn & MB_PAD_B) && !phase->branch_launch) {
        const SidescrlActor *a = &phase->actor;
        int feet = a->y + ACTOR_FEET, bottom = (int)phase->map.planes[0].tiles.height_px;
        int margin = a->hit_width / 2;
        /* Release jump to catch a narrow shelf; stay on a ledge if both arcs miss it. */
        if ((!a->on_ground && a->vy < 0 && a->spring_ticks) ||
            (a->on_ground && a->vx > 0 && phase->on_plat_idx < 0 && !(frame & 1u))) {
            int tap = jump_landing(phase, false, bottom, false);
            int hold = jump_landing(phase, true, bottom, false);
            if (a->on_ground ? tap > feet + margin && hold > feet + margin : tap + margin < hold)
                btn &= ~MB_PAD_B;
        }
    }
    return btn;
}
