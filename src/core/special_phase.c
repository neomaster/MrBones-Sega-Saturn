#include "special_phase.h"
#include "core/duck_avi.h"
#include "core/mb_font.h"
#include "core/pan_palette.h"
#include "core/pan_scroll.h"
#include "core/resource.h"
#include "core/rhythm_table.h"
#include "core/runway_compose.h"
#include "core/runway_projection.h"
#include "core/runway_sfx.h"
#include "core/saturn_pad.h"
#include "core/scrl_map.h"
#include "core/sprite_decode.h"
#include "core/stage_enemy.h"
#include "core/stxtr.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCR_W 320u
#define SCR_H 224u
#define UNITS_PER_PIXEL 2341u
#define START_X (40u * UNITS_PER_PIXEL)
#define FINISH_X (START_X + 240u * UNITS_PER_PIXEL)
#define GROUND_Y (180u * UNITS_PER_PIXEL)
#define WALK_STEP (4u * UNITS_PER_PIXEL)
#define JUMP_IMPULSE (-(int32_t)17600)
#define GRAVITY_Y ((int32_t)840)

/*
 * FINALE CRing literals from /27FINALE/03CODE (base 0x00232000).
 * Direction table @0x002b21e0; RingDefinitionTable @0x002b0158 (0x2000 B);
 * phase increments @0x002b2158; ShootRing adds 0x100 before subtracting
 * object+0x704 (docs/finale-ring-analysis.md).
 */
static const int k_finale_dir[4] = {0, 128, 192, 64};
#include "finale_ring_table.inc"

/*
 * ShootRing sound stand-in (Saturn SoundEvent @0x00204128).
 * Deactivate/hit table @0x002b21f0 = {0x2A,0x2A,0x2B,0x29} by dir.
 * Activate table     @0x002b2200 = {0x32,0x32,0x33,0x31} by dir.
 * Event records resolve to /27FINALE/05SND1 span4 samples via word[4].
 * Flags keep the legacy RUNWAY_SFX dispatch path for headless tests.
 */
#define FINALE_SFX_HIT      (RUNWAY_SFX_FLAG_JUMP | RUNWAY_SFX_FLAG_C)
#define FINALE_SFX_ACTIVATE (RUNWAY_SFX_FLAG_C)
static const unsigned k_finale_sfx_hit[4] = {0x2Au, 0x2Au, 0x2Bu, 0x29u};
static const unsigned k_finale_sfx_act[4] = {0x32u, 0x32u, 0x33u, 0x31u};
static void finale_ring_init(special_phase *phase) {
    unsigned i;
    for (i = 0; i < FINALE_RING_SLOTS; i++)
        phase->ring_active[i] = 1; /* CRing constructor @0x28d506 */
    phase->ring_active_count = (int)FINALE_RING_SLOTS;
    phase->ring_phase = 0;
    phase->ring_phase_acc = 0;
    phase->finale_facing = 4u;
    /* /27FINALE/03CODE 29F6AE/B6: two _rand calls per body joint.
     * CRing constructs sixteen randomized animations through RandomRange (20489A). */
    phase->finale_rng = (2u * FINALE_BODY_JOINTS + FINALE_RING_SLOTS) * 0x07050301u;
    phase->finale_cycle_tick = 3u;
    phase->ring_cleared = 0;
    phase->ring_shots = 0;
    phase->ring_last_index = -1;
    phase->ring_last_activated = 0;
    phase->ring_sfx_event = 0;
    /* finale_movie_armed is set from load once 20AVI opens. */
}

/* Disc 2, US: measured CD/DUKMOVIE startup for the arena→film route, in
 * retraces. PlayDuckMovie(3) 20b61c reads the 66-sector 19DUCK overlay,
 * then DKAVIFilePlay seeks to the tail index of 20AVI and back to the data.
 * The common-input Ymir trace records request 2127, MFP_LoadIndex 2172,
 * duckAudioStart 2284, first Start poll 2285 and first scanout 2286.
 * These are observed CD/decoder timings, not SH-2 delay constants. */
enum { FINALE_MOVIE_FIRST_SCANOUT = 159, FINALE_AUDIO_BEFORE_SCANOUT = 2 };

int special_phase_finale_play_ending(special_phase *phase) {
    if (!phase || phase->kind != STAGE_KIND_FINALE || !phase->finale_movie_armed ||
        phase->movie.cur != 1 || phase->movie.decode_error)
        return -1;
    phase->movie_ok = 1;
    phase->finale_movie_preroll = FINALE_MOVIE_FIRST_SCANOUT;
    phase->movie_pcm_pending = 0;
    snprintf(phase->status, sizeof phase->status,
             "%s PlayDuckMovie(3) frames=%u pcm=%zu",
             phase->stage_name, phase->movie.frame_count,
             phase->movie.pcm_samples);
    return 0;
}

int special_phase_finale_defer_ending(special_phase *phase) {
    if (!phase || phase->kind != STAGE_KIND_FINALE || phase->ring_cleared || special_phase_done(phase))
        return -1;
    if (!phase->finale_movie_armed)
        return -1;
    /* The normal stage load already holds frame zero and the decoded PCM.
     * If entering the arena after previewing the movie, rewind at entry so
     * the victory transition never re-indexes a 36 MB AVI on the UI thread. */
    if (phase->movie.cur != 1 || phase->movie.elapsed_us || phase->movie.playback_done) {
        duck_avi_free(&phase->movie);
        if (duck_avi_open_with_duck(&phase->movie, phase->avi, phase->avi_size,
                                    phase->duck, phase->duck_size) ||
            !duck_avi_step(&phase->movie))
            return -1;
    }
    if (phase->movie_ok) phase->frames = 0; /* arena begins at its own tick zero */
    phase->movie_ok = 0;
    phase->movie_pcm_pending = 0;
    phase->finale_movie_preroll = 0;
    snprintf(phase->status, sizeof phase->status,
             "%s arena (ending deferred) slots=%d/%u",
             phase->stage_name, phase->ring_active_count, FINALE_RING_SLOTS);
    return 0;
}

enum { RING_IDLE, RING_OUT, RING_IN, RING_HIDDEN };

/* The two downstream VDP1 lists separate collision state from scanout. */
static void submit_finale_ring(special_phase *p) {
    p->ring_list[0] = p->ring_list[1];
    p->ring_list[1] = p->ring_list[2];
    p->ring_list[2].phase = p->ring_phase;
    memcpy(p->ring_list[2].order, p->ring_order, sizeof p->ring_order);
    for (unsigned i = 0; i < FINALE_RING_SLOTS; ++i) {
        unsigned mode = p->ring_motion[i];
        int age = p->ring_age[i];
        const stxtr_record *frame = NULL;
        if (mode != RING_HIDDEN) {
            const finale_ring_anim *anim = &p->ring_anim[mode];
            unsigned row = age > 0 ? (unsigned)age / anim->ticks : 0;
            frame = &anim->frames[row];
            if (++age == (int)(anim->count * anim->ticks)) {
                p->ring_motion[i] = mode == RING_OUT ? RING_HIDDEN : RING_IDLE;
                age = 0;
            }
            p->ring_age[i] = age;
        }
        p->ring_list[2].slots[i] = frame;
    }
}

/* Brain_CRing: advance phase accumulator and publish high-16 to +0x704. */
static void finale_ring_brain(special_phase *phase) {
    submit_finale_ring(phase);
    phase->ring_phase = phase->ring_phase_acc >> 16;
    phase->ring_phase_acc += k_finale_phase_incr[phase->ring_active_count];
    if (phase->ring_phase_acc > FINALE_RING_PHASE_WRAP)
        phase->ring_phase_acc -= FINALE_RING_PHASE_WRAP;
}

static int finale_ring_select(int phase_field, unsigned dir) {
    int idx;
    dir &= 3u;
    idx = ((k_finale_dir[dir] + 0x100 - phase_field) + 8) >> 4;
    while (idx > 15)
        idx -= 16;
    while (idx < 0)
        idx += 16;
    return idx;
}

static int apply_finale_shot(special_phase *phase, finale_ring_contact contact, unsigned actor_tick) {
    int idx;
    unsigned d;
    if (!phase || phase->kind != STAGE_KIND_FINALE || phase->ring_cleared)
        return 0;
    d = contact.dir & 3u;
    idx = finale_ring_select(phase->ring_phase, d);
    phase->ring_shots++;
    phase->ring_last_index = idx;
    /* SetAnim links a removed CRender at the head (29FBF2). */
    if (phase->ring_motion[idx] == RING_HIDDEN) {
        for (unsigned pos = 0; pos < FINALE_RING_SLOTS; ++pos) {
            if (phase->ring_order[pos] != idx) continue;
            memmove(phase->ring_order + 1, phase->ring_order, pos);
            phase->ring_order[0] = (uint8_t)idx;
            break;
        }
    }
    phase->ring_motion[idx] = phase->ring_active[idx] ? RING_OUT : RING_IN;
    phase->ring_age[idx] = 1; /* SetAnim publishes the first leaf on the hit tick. */
    /* Ending movie carries its own audio — skip ring SFX while movie_ok. */
    if (!phase->ring_active[idx]) {
        /* inactive → activate, increment +0x708; Saturn ID table @0x2b2200. */
        phase->ring_active[idx] = 1;
        phase->ring_active_count++;
        phase->ring_last_activated = 1;
        if (!phase->movie_ok) {
            phase->sfx_flags |= FINALE_SFX_ACTIVATE;
            phase->ring_sfx_event = k_finale_sfx_act[d];
        }
        return 0;
    }
    /* active → hit/destroy, decrement; count==0 → EndLevel(0); IDs @0x2b21f0. */
    phase->ring_active[idx] = 0;
    phase->ring_active_count--;
    phase->ring_last_activated = 0;
    phase->finale_ring_hits[contact.slot] = (finale_ring_impact){d, actor_tick};
    if (!phase->movie_ok) {
        phase->sfx_flags |= FINALE_SFX_HIT;
        phase->ring_sfx_event = k_finale_sfx_hit[d];
    }
    if (phase->ring_active_count < 0)
        phase->ring_active_count = 0;
    if (phase->ring_active_count == 0) {
        phase->ring_cleared = 1;
        phase->finale_clear_tick = phase->frames;
        return 1;
    }
    return 0;
}

int special_phase_finale_shoot(special_phase *phase, unsigned dir) {
    return apply_finale_shot(phase, (finale_ring_contact){(uint8_t)dir, 0}, phase ? phase->frames : 0u);
}

static void publish_finale_shot(special_phase *p, finale_ring_contact contact, unsigned actor_tick) {
    if (apply_finale_shot(p, contact, actor_tick)) p->finale_end_tick = actor_tick;
}


static unsigned finale_dir_from_pad(const saturn_pad *pad) {
    if (pad->held & 0x1000u) return 0; /* N, S, E, W in CheckJoy order */
    if (pad->held & 0x2000u) return 1;
    if (pad->held & 0x8000u) return 2;
    if (pad->held & 0x4000u) return 3;
    return 4;
}

static void paint_finale_rings(const special_phase *p, uint8_t *dst) {
    const finale_ring_list *list = &p->ring_list[0];
    for (unsigned pos = 0; pos < FINALE_RING_SLOTS; ++pos) {
        unsigned i = list->order[pos];
        unsigned rec = ((unsigned)list->phase + 16u * i) & 255u;
        if (list->slots[i])
            finale_body_draw_ring(p->finale_body_mesh, p->bg_rgba, dst,
                                  k_finale_ring_xy[rec], list->slots[i]);
    }
}

static uint32_t finale_word(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static const uint8_t *finale_code(const special_phase *p, uint32_t addr, size_t need) {
    if (addr < 0x232000u || addr - 0x232000u > p->code_size ||
        need > p->code_size - (addr - 0x232000u)) return NULL;
    return (const uint8_t *)p->code + (addr - 0x232000u);
}

/* FireBolt changes individual bank-3 CRAM entries on the VDP2 backdrop.
 * The screen-space texel list is built once from the stage's tile maps. */
typedef struct {
    uint8_t rgb[3], target[3], scanout[2][3], used;
    unsigned fade_tick;
} finale_light_color;

typedef struct { uint32_t pixel; uint8_t index; } finale_light_texel;

struct finale_lights {
    finale_light_color colors[256];
    finale_light_texel *texels;
    size_t count;
    uint32_t sequences[4];
    unsigned first_tick, fade_cycles;
};

static int finale_light_list(special_phase *p, uint32_t address, int fade, int discover) {
    if (!address) return 0;
    const uint8_t *op = finale_code(p, address, 4);
    if (!op) return -1;
    unsigned index = finale_word(op);
    for (;;) {
        address += 4;
        op = finale_code(p, address, 4);
        if (!op) return -1;
        uint32_t value = finale_word(op);
        if (value & 0x80000000u) {
            if (value & 0x40000000u) return 0;
            index = value & 0xffffu;
            continue;
        }
        if (index < 768 || index >= 1024) return -1;
        if (value) {
            finale_light_color *color = &p->finale_lights->colors[index - 768];
            color->used = 1;
            if (!discover) {
                uint8_t *dst = fade ? color->target : color->rgb;
                for (unsigned channel = 0; channel < 3; ++channel)
                    dst[channel] = (uint8_t)(value >> (8u * channel));
                /* 28D970..28D9AA: two yields and a two-tick wait before
                 * PalFade(32). CRAM shows its first step six retraces after red. */
                color->fade_tick = fade ? p->frames + 6u : 0u;
            }
        }
        ++index;
    }
}

static int cache_finale_light_texels(special_phase *p, const scrl_map *map,
                                    const uint8_t *palette) {
    struct finale_lights *lights = p->finale_lights;
    uint8_t indexed[4096] = {0};
    for (unsigned i = 0; i < 256; ++i) {
        if (!lights->colors[i].used) continue;
        memcpy(lights->colors[i].rgb, palette + 4u*(768u+i) + 1u, 3);
        memcpy(lights->colors[i].scanout[0], lights->colors[i].rgb, 3);
        memcpy(lights->colors[i].scanout[1], lights->colors[i].rgb, 3);
        indexed[4u*(768u+i) + 1u] = 255;
        indexed[4u*(768u+i) + 2u] = (uint8_t)i;
    }
    if (scrl_map_draw(map, indexed, p->bg_rgba, SCR_W, SCR_H, 0u)) return -1;
    for (size_t i = 0; i < SCR_W * SCR_H; ++i)
        if (p->bg_rgba[4*i] == 255) ++lights->count;
    if (!lights->count) return -1;
    lights->texels = malloc(lights->count * sizeof *lights->texels);
    if (!lights->texels) return -1;
    size_t pos = 0;
    for (uint32_t i = 0; i < SCR_W * SCR_H; ++i)
        if (p->bg_rgba[4*i] == 255)
            lights->texels[pos++] = (finale_light_texel){i, p->bg_rgba[4*i+1]};
    return 0;
}

static void step_finale_lights(special_phase *p) {
    struct finale_lights *lights = p->finale_lights;
    unsigned dir = p->finale_next_dir;
    if (lights->first_tick && p->frames >= lights->first_tick) {
        unsigned age = p->frames - lights->first_tick;
        if (!(age % 4u) && age / 4u < p->finale_rows[dir]) {
            const uint8_t *row = finale_code(p, lights->sequences[dir] + 28u*(age/4u), 20);
            /* The immutable streams were bounds-checked when loading 03CODE. */
            for (unsigned i = 0; i < 4; ++i)
                finale_light_list(p, finale_word(row + 4*i), i & 1u, 0);
            /* FireBolt invokes the row's factory with the shot manager as parent. */
            if (finale_word(row + 16) == 0x28db00u) p->finale_face_tick = p->frames;
        }
    }
    lights->fade_cycles = 0;
    for (unsigned i = 0; i < 256; ++i) {
        finale_light_color *color = &lights->colors[i];
        if (!color->fade_tick || p->frames < color->fade_tick) continue;
        /* 205D4A iterates 32 times for each of the three channels, even
         * when a channel has already reached its target. */
        lights->fade_cycles += 3u * 32u * 14u;
        for (unsigned c = 0; c < 3; ++c) {
            int delta = (int)color->target[c] - color->rgb[c];
            int step = delta > 32 ? 32 : delta < -32 ? -32 : delta;
            lights->fade_cycles += (unsigned)abs(step) * 4u;
            color->rgb[c] = (uint8_t)(color->rgb[c] + step);
        }
        if (!memcmp(color->rgb, color->target, 3)) color->fade_tick = 0;
    }
    for (size_t i = 0; i < lights->count; ++i) {
        finale_light_texel texel = lights->texels[i];
        memcpy(p->bg_rgba + 4u*texel.pixel, lights->colors[texel.index].scanout[p->frames & 1u], 3);
    }
    /* CPU shadow palette -> CRAM at VBlank -> VDP2 scanout. */
    for (unsigned i = 0; i < 256; ++i)
        memcpy(lights->colors[i].scanout[p->frames & 1u], lights->colors[i].rgb, 3);
}

/* CRing constructor, ShootRing and Brain select these CRender sequences. */
static int load_finale_rings(special_phase *p) {
    const uint32_t sequences[] = {0x28b4c4u, 0x28b60cu, 0x28b648u};
    for (unsigned mode = 0; mode < 3; ++mode) {
        finale_ring_anim *anim = &p->ring_anim[mode];
        anim->count = mode == RING_IDLE ? 4u : 5u;
        for (unsigned i = 0; i < anim->count; ++i) {
            const uint8_t *row = finale_code(p, sequences[mode] + 12 * i, 12);
            if (!row) return -1;
            unsigned ticks = finale_word(row + 4);
            if (!ticks || ticks > 255 || (i && ticks != anim->ticks)) return -1;
            anim->ticks = ticks;
            const uint8_t *obj = finale_code(p, finale_word(row), 16);
            const uint8_t *leaf = obj ? finale_code(p, finale_word(obj + 12), 28) : NULL;
            if (!leaf || finale_word(leaf + 12) != 2 || finale_word(leaf + 16) != 0x04a00000u ||
                finale_word(leaf + 20) != 0x0e1eu || finale_word(leaf + 8) < 0x25c08000u) return -1;
            size_t off = finale_word(leaf + 8) - 0x25c08000u;
            stxtr_record *frame = &anim->frames[i];
            if (stxtr_next(p->textures, p->textures_size, &off, frame) != 1 ||
                frame->width != 112 || frame->height != 30 || frame->size != 112u * 30u) return -1;
        }
    }
    uint32_t seed = 2u * FINALE_BODY_JOINTS * 0x07050301u;
    for (unsigned i = 0; i < FINALE_RING_SLOTS; ++i) {
        seed += 0x07050301u;
        unsigned sample = (unsigned)p->finale_random_table[(seed >> 16) & 255u] << 8 |
                          p->finale_random_table[seed >> 24];
        p->ring_age[i] = 6 - (int)((24u * sample) >> 16);
        p->ring_order[i] = (uint8_t)(FINALE_RING_SLOTS - 1u - i);
    }
    return 0;
}

/* Loader _VDP1TxtrToVram (0x207230): low bit selects literals, even
 * packets insert transparent pixels. A zero control byte ends the texture. */
static int finale_texture(finale_bolt_frame *f, const uint8_t *rec, size_t available) {
    size_t count = (size_t)f->width * f->height;
    if (available < 16 || !count || finale_word(rec) != (f->width << 16 | f->height) ||
        (finale_word(rec + 8) >> 16) * 4u != count ||
        finale_word(rec + 12) != 0x56789abcu) return -1;
    const uint8_t *src = rec + 16;
    available -= 16;
    if (rec[10] || rec[11]) {
        if (available < count) return -1;
        f->pixels = src;
        return 0;
    }
    f->unpacked = malloc(count);
    if (!f->unpacked) return -1;
    size_t used = 0, written = 0;
    while (used < available) {
        unsigned op = src[used++], n = op >> 1;
        if (!op) {
            f->pixels = f->unpacked;
            return written == count ? 0 : -1;
        }
        if (!n || n > count - written) return -1;
        if (op & 1u) {
            if (n > available - used) return -1;
            memcpy(f->unpacked + written, src + used, n);
            used += n;
        } else {
            memset(f->unpacked + written, 0, n);
        }
        written += n;
    }
    return -1;
}

/* CRender descriptors use SH-2 addresses in 03CODE / 16DTXT or VDP1
 * addresses for the textures uploaded from 04STXT. */
static const uint8_t *finale_asset(const special_phase *p, uint32_t addr,
                                   size_t need, size_t *available) {
    const uint8_t *data;
    size_t size, offset;
    if (addr >= 0x25c08000u) {
        data = p->textures;
        size = p->textures_size;
        offset = addr - 0x25c08000u;
    } else if (addr >= 0x06040000u) {
        data = p->finale_dtxt;
        size = p->finale_dtxt_size;
        offset = addr - 0x06040000u;
    } else {
        if (addr < 0x232000u) return NULL;
        data = p->code;
        size = p->code_size;
        offset = addr - 0x232000u;
    }
    if (!data || offset > size || need > size - offset) return NULL;
    if (available) *available = size - offset;
    return data + offset;
}

static int load_finale_anim(const special_phase *p, finale_bolt_anim *anim, uint32_t addr,
                            unsigned ticks_per_record) {
    for (unsigned i = 0; i < sizeof anim->frames / sizeof anim->frames[0]; ++i) {
        const uint8_t *row = finale_asset(p, addr + 12u*i, 12, NULL);
        if (!row) return -1;
        unsigned ticks = finale_word(row + 4) & 0xffffu;
        if (!ticks || (ticks_per_record && ticks != ticks_per_record)) return -1;
        const uint8_t *obj = finale_asset(p, finale_word(row), 16, NULL);
        const uint8_t *leaf = obj ? finale_asset(p, finale_word(obj + 12), 28, NULL) : NULL;
        if (!leaf || finale_word(leaf + 12) != 1u ||
            finale_word(leaf + 16) != 0x04a00000u) return -1;
        uint32_t xy = finale_word(leaf), size_word = finale_word(leaf + 20);
        size_t available;
        const uint8_t *texture = finale_asset(p, finale_word(leaf + 8), 16, &available);
        finale_bolt_frame *f = &anim->frames[i];
        f->ticks = ticks;
        f->x_origin = (int16_t)(xy >> 16);
        f->y_origin = (int16_t)xy;
        f->width = ((size_word >> 8) & 63u) * 8u;
        f->height = size_word & 255u;
        if (!texture || finale_texture(f, texture, available)) return -1;
        uint32_t fn = finale_word(row + 8);
        if (fn == 0x2071f0u || (fn == 0x207214u && !anim->impact))
            anim->impact = i;
        if (fn == 0x207214u) {
            anim->count = i + 1;
            return anim->impact ? 0 : -1;
        }
        if (fn != 0x206eb4u && fn != 0x2071f0u) return -1;
    }
    return -1;
}

static int load_finale_arena(special_phase *p, cdimage *cd) {
    const uint8_t *tables = finale_code(p, 0x2b2ee0u, 84);
    const uint8_t *hit = finale_code(p, 0x2b2f74u, 8u * 16u);
    const uint8_t *ring_hit = finale_code(p, 0x2b2f34u, 4u * 16u);
    const uint8_t *pos = finale_code(p, 0x28d896u, 4);
    const bones_projection *projection = bones_projection_for(p->stage_name);
    size_t size;
    void *loader = NULL;
    if (!tables || !hit || !ring_hit || !pos || !projection) return -1;
    p->finale_lights = calloc(1, sizeof *p->finale_lights);
    if (!p->finale_lights) return -1;
    /* RWorldSprite uses the signed high word of (camera - world) * scale.
     * Ymir arena WRAM: both camera axes zero, VDP1 local origin (159,111). */
    uint32_t xy = finale_word(pos);
    uint32_t dx = (uint32_t)(-(int16_t)(xy >> 16)) * projection->pixels_per_unit;
    uint32_t dy = (uint32_t)(-(int16_t)xy) * projection->pixels_per_unit;
    p->finale_origin_x = (int)(SCR_W - 1u) / 2 + (int16_t)(dx >> 16);
    p->finale_origin_y = (int)(SCR_H - 1u) / 2 + (int16_t)(dy >> 16);
    if (resource_load(cd, "/27FINALE/16DTXT", &p->finale_dtxt, &p->finale_dtxt_size)) return -1;
    for (unsigned type = 0; type < 8; ++type) {
        const uint8_t *entry = hit + 16u*type;
        p->finale_hit_z[type] = (int32_t)finale_word(entry + 4);
        uint32_t offset_x = finale_word(entry + 8) * projection->pixels_per_unit;
        uint32_t offset_y = finale_word(entry + 12) * projection->pixels_per_unit;
        p->finale_hit_x[type] = (int)(SCR_W - 1u) / 2 + (int16_t)((dx - offset_x) >> 16);
        p->finale_hit_y[type] = (int)(SCR_H - 1u) / 2 + (int16_t)((dy - offset_y) >> 16);
        if (type <= 4u) {
            if (load_finale_anim(p, &p->finale_hit[type], finale_word(entry), type < 4u ? 2u : 1u)) return -1;
        } else if (finale_word(entry) != finale_word(hit + 4u*16u)) return -1;
    }
    for (unsigned d = 0; d < 4; ++d) {
        uint32_t sequence = finale_word(tables + 4u*d);
        const uint8_t *entry = ring_hit + 16u*d;
        if (finale_word(entry) != finale_word(ring_hit)) return -1;
        uint32_t offset_x = finale_word(entry + 8) * projection->pixels_per_unit;
        uint32_t offset_y = finale_word(entry + 12) * projection->pixels_per_unit;
        p->finale_ring_hit_x[d] = (int)(SCR_W - 1u)/2 + (int16_t)((dx - offset_x) >> 16);
        p->finale_ring_hit_y[d] = (int)(SCR_H - 1u)/2 + (int16_t)((dy - offset_y) >> 16);
        p->finale_red[d].z_bias = (int32_t)finale_word(tables + 24u + d*8u);
        p->finale_blue[d].z_bias = (int32_t)finale_word(tables + 56u + d*8u);
        p->finale_lights->sequences[d] = sequence;
        /* FireBolt creates the red bolt in the final row before the sentinel. */
        for (unsigned n = 0; n < 32; ++n) {
            const uint8_t *row = finale_code(p, sequence + 28u*n, 32);
            if (!row) return -1;
            for (unsigned i = 0; i < 4; ++i)
                if (finale_light_list(p, finale_word(row + 4*i), i & 1u, 1)) return -1;
            if (finale_word(row + 16) == 0x28dc34u && finale_word(row + 28) == UINT32_MAX) {
                p->finale_rows[d] = n + 1;
                break;
            }
        }
        if (!p->finale_rows[d] ||
            load_finale_anim(p, &p->finale_red[d], finale_word(tables + 20u + d*8u), 1u) ||
            load_finale_anim(p, &p->finale_blue[d], finale_word(tables + 52u + d*8u), 1u)) return -1;
    }
    if (load_finale_anim(p, &p->finale_hit[5], finale_word(ring_hit), 1u)) return -1;
    const uint8_t *face_anim = finale_code(p, 0x28db98u, 4);
    if (!face_anim || load_finale_anim(p, &p->finale_face, finale_word(face_anim), 0))
        return -1;
    if (resource_load(cd, "/02", &loader, &size)) return -1;
    int ok = size >= 0x8528u + sizeof p->finale_random_table;
    if (ok) memcpy(p->finale_random_table, (uint8_t *)loader + 0x8528u, sizeof p->finale_random_table);
    free(loader);
    if (!ok) return -1;
    p->finale_body_mesh = finale_body_load(p->code, p->code_size, p->textures, p->textures_size,
                                          p->finale_random_table);
    return p->finale_body_mesh ? load_finale_rings(p) : -1;
}

static finale_sprite finale_screen_sprite(const finale_bolt_frame *frame, int x, int y, int32_t z) {
    return (finale_sprite){frame->pixels, frame->width, frame->height,
                           x - frame->x_origin, y - frame->y_origin, z / 16777216.0};
}

static void finale_visible_sprites(const special_phase *p, finale_sprite sprites[FINALE_SPRITES_MAX]) {
    if (p->finale_face_tick && p->frames - p->finale_face_tick >= 3u) {
        /* CFace::Brain yields until the end callback. SetAnim submits the
         * first leaf after advancing it and keeps the terminal leaf one extra tick. */
        unsigned elapsed = p->frames - p->finale_face_tick - 2u;
        for (unsigned i = 0; i < p->finale_face.count; ++i) {
            const finale_bolt_frame *frame = &p->finale_face.frames[i];
            if (elapsed < frame->ticks ||
                (i + 1u == p->finale_face.count && elapsed == frame->ticks)) {
                sprites[FINALE_FACE_SPRITE] = finale_screen_sprite(frame, p->finale_origin_x,
                                                                  p->finale_origin_y, 0);
                break;
            }
            elapsed -= frame->ticks;
        }
    }
    for (unsigned i = 0; i < 3; ++i) {
        const finale_bolt *bolt = &p->finale_display_bolts[0][i];
        /* CBolt and CBlueShot enter Rendeng after the first animation step.
         * Publishing the initial record would also advance its depth sort. */
        if (bolt->age < 2u) continue;
        const finale_bolt_anim *anim = &(i ? p->finale_blue : p->finale_red)[bolt->dir];
        unsigned age = bolt->age > anim->count ? anim->count : bolt->age;
        sprites[i] = finale_screen_sprite(&anim->frames[age-1], p->finale_origin_x,
                                                p->finale_origin_y, anim->z_bias);
    }
    /* CShotHit runs SetAnim in its own Brain (28E098). Its first visible
     * list follows the first animation step. The final leaf remains linked
     * until its destructor (28E038) removes the CRender on the next pass. */
    unsigned age = p->frames - p->finale_hit_tick;
    if (p->finale_hit_tick && age >= 3u) {
        unsigned type = p->finale_hit_type;
        const finale_bolt_anim *anim = &p->finale_hit[type < 4 ? type : 4];
        unsigned ticks_per_frame = type < 4u ? 2u : 1u;
        if (age - 2u <= anim->count * ticks_per_frame) {
            unsigned frame = (age - 2u) / ticks_per_frame;
            if (frame == anim->count) --frame;
            sprites[3] = finale_screen_sprite(&anim->frames[frame], p->finale_hit_x[type],
                                              p->finale_hit_y[type], p->finale_hit_z[type]);
        }
    }
    const finale_bolt_anim *burst = &p->finale_hit[5];
    for (unsigned slot = 0; slot < 2; ++slot) {
        const finale_ring_impact *hit = &p->finale_ring_hits[slot];
        age = p->frames - hit->tick;
        if (!hit->tick || age < 3u || age - 3u >= burst->count) continue;
        /* 28DF30: SetAnim replaces the flight on CBlueShot's existing
         * CRender. The first submitted list retains the terminal flight
         * leaf, and the burst retains its final leaf until removal. */
        unsigned frame = age - 2u;
        if (frame >= burst->count) frame = burst->count - 1u;
        unsigned dir = hit->dir;
        sprites[1 + slot] = finale_screen_sprite(&burst->frames[frame],
            p->finale_ring_hit_x[dir], p->finale_ring_hit_y[dir], p->finale_blue[dir].z_bias);
    }
}

static void build_palette(const uint8_t *code, size_t code_size, uint8_t *palette) {
    uint32_t words[1024], tmp[1024];
    size_t off, consumed, best = 0, i;
    memset(words, 0, sizeof words);
    if (code && code_size > 2048) {
        for (off = 0; off + 2048 <= code_size && off < 0x80000; off += 4) {
            if (code[off] != 0 || code[off + 1] != 0 || code[off + 2] >= 4 || code[off + 4] != 1)
                continue;
            consumed = 0;
            memset(tmp, 0, sizeof tmp);
            if (pan_palette_apply(code + off, code_size - off, tmp, 1024, &consumed))
                continue;
            if (consumed >= 2048 && consumed > best) {
                best = consumed;
                memcpy(words, tmp, sizeof words);
                if (best >= 4100) break;
            }
        }
    }
    if (best < 64 && code && code_size > 16) {
        for (off = 0; off + 16 < code_size && off < 0x80000; off += 4) {
            consumed = 0;
            memset(tmp, 0, sizeof tmp);
            if (pan_palette_apply(code + off, code_size - off, tmp, 1024, &consumed))
                continue;
            if (consumed > best && consumed >= 64) {
                best = consumed;
                memcpy(words, tmp, sizeof words);
                if (best >= 200) break;
            }
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

static void stage_name_from_iso(char *out, size_t n, const char *iso_dir) {
    const char *p = strrchr(iso_dir, '/');
    p = p ? p + 1 : iso_dir;
    if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9')
        p += 2;
    snprintf(out, n, "%s", p);
}

static special_mode mode_from_kind(stage_kind kind) {
    switch (kind) {
    case STAGE_KIND_RHYTHM: return SPECIAL_MODE_RHYTHM;
    case STAGE_KIND_BIGBONES:
    case STAGE_KIND_GENROOM: return SPECIAL_MODE_ACTION;
    case STAGE_KIND_FINALE:
    case STAGE_KIND_LOSE:
    case STAGE_KIND_MOVIE:
    case STAGE_KIND_INTRO: return SPECIAL_MODE_PRESENT;
    default: return SPECIAL_MODE_PRESENT;
    }
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

static void fill_bg(uint8_t *bg, uint8_t r, uint8_t g, uint8_t b) {
    size_t i, n = (size_t)SCR_W * SCR_H * 4u;
    for (i = 0; i < n; i += 4) {
        bg[i + 0] = r;
        bg[i + 1] = g;
        bg[i + 2] = b;
        bg[i + 3] = 255;
    }
}

static uint8_t bigbns_dome_y_px(int px) {
    int d = px - 168;
    int y = 164 + (d * d) / 380;
    if (y < 164) y = 164;
    if (y > 208) y = 208;
    return (uint8_t)y;
}

static int is_bigbns_stage(const special_phase *phase) {
    return phase && (!strcmp(phase->stage_name, "BIGBNS") ||
                     !strcmp(phase->stage_name, "BIGBONES"));
}

static void paint_beat_lane(const special_phase *phase, uint8_t *dst,
                            int in_window, unsigned hits) {
    static const char *const drum_labels[] = {"UP", "DN", "R", "L"};
    static const char *const fret_labels[] = {"A", "B", "C", "X", "Y", "Z"};
    static const uint8_t k_btn_col[4][3] = {
        {245,  96,  96}, /* A: red */
        {245, 210,  80}, /* B: gold */
        { 95, 210, 255}, /* C: cyan */
        {115, 235, 145}  /* RIGHT: emerald */
    };
    int x, y, i;
    uint8_t *px;
    char hud[64];

    /* Sleek dark-translucent ribbon at bottom (y = 200..218, x = 28..292). */
    for (y = 200; y < 218; y++) {
        int border = (y == 200 || y == 217);
        for (x = 28; x < 292; x++) {
            px = dst + ((size_t)y * SCR_W + (size_t)x) * 4u;
            if (border) {
                px[0] = (uint8_t)((px[0] + 95u) / 2u);
                px[1] = (uint8_t)((px[1] + 150u) / 2u);
                px[2] = (uint8_t)((px[2] + 185u) / 2u);
                px[3] = 255u;
            } else {
                px[0] = (uint8_t)((px[0] * 100u + 8u * 155u) / 255u);
                px[1] = (uint8_t)((px[1] * 100u + 12u * 155u) / 255u);
                px[2] = (uint8_t)((px[2] * 100u + 22u * 155u) / 255u);
                px[3] = 255u;
            }
        }
    }
    /* Crisp cyan/gold hit-zone ring around (56, 209). */
    {
        uint8_t hr = in_window ? 120u : 75u;
        uint8_t hg = in_window ? 245u : 190u;
        uint8_t hb = in_window ? 255u : 235u;
        if (phase && phase->grade_timer > 0) {
            if (phase->last_hit_grade > 0) {
                hr = 255u; hg = 225u; hb = 110u;
            } else if (phase->last_hit_grade < 0) {
                hr = 240u; hg = 70u; hb = 75u;
            }
        }
        for (y = 201; y <= 217; y++) {
            for (x = 48; x <= 64; x++) {
                int dx = x - 56, dy = y - 209;
                int d2 = dx * dx + dy * dy;
                if (d2 >= 42 && d2 <= 68) {
                    px = dst + ((size_t)y * SCR_W + (size_t)x) * 4u;
                    px[0] = hr; px[1] = hg; px[2] = hb; px[3] = 255u;
                } else if (d2 < 42 && (in_window || (phase && phase->grade_timer > 0))) {
                    px = dst + ((size_t)y * SCR_W + (size_t)x) * 4u;
                    px[0] = (uint8_t)((px[0] + hr) / 2u);
                    px[1] = (uint8_t)((px[1] + hg) / 2u);
                    px[2] = (uint8_t)((px[2] + hb) / 2u);
                    px[3] = 255u;
                }
            }
        }
    }
    if (phase) {
        unsigned ek = phase->expected_kind & 3u;
        mb_font_draw_text_shadow(dst, SCR_W, SCR_H, 54, 205,
                                 phase->drum_notes ? drum_labels[ek] : fret_labels[phase->expected_kind % 6u],
                                 k_btn_col[ek][0], k_btn_col[ek][1], k_btn_col[ek][2],
                                 8, 12, 20, 1);
    }

    /* Compact note pills scrolling smoothly along the ribbon toward x=56. */
    if (phase && phase->rhythm && phase->rhythm->notes) {
        const rhythm_table *rt = phase->rhythm;
        unsigned k;
        for (k = phase->note_cursor; k < rt->count; k++) {
            unsigned ht = rhythm_note_host_frame(rt, k);
            int dt = (int)ht - (int)phase->frames;
            int nx;
            unsigned nk = rt->notes[k].kind & 3u;
            if (dt > 108) break;
            if (dt < -4) continue;
            nx = 56 + dt * 2;
            if (nx < 36 || nx > 282) continue;
            for (y = 203; y <= 215; y++) {
                for (x = nx - 6; x <= nx + 6; x++) {
                    int dx = x - nx, dy = y - 209;
                    if (x < 30 || x >= 290 || dx * dx + dy * dy > 36) continue;
                    px = dst + ((size_t)y * SCR_W + (size_t)x) * 4u;
                    px[0] = k_btn_col[nk][0];
                    px[1] = k_btn_col[nk][1];
                    px[2] = k_btn_col[nk][2];
                    px[3] = 255u;
                }
            }
            mb_font_draw_text_shadow(dst, SCR_W, SCR_H, nx - 2, 205,
                                     drum_labels[nk], 255, 255, 255, 8, 12, 18, 1);
        }
    }

    /* Slim hit & streak progress pips just above the ribbon (y = 195..197). */
    {
        unsigned hits_need = (phase && phase->rhythm && phase->rhythm->hits_to_win)
                                 ? phase->rhythm->hits_to_win
                                 : SPECIAL_HITS_TO_WIN;
        unsigned combo = phase ? phase->combo : 0u;
        unsigned lit = (combo > 0u && (combo % 4u) == 0u) ? 4u : (combo % 4u);
        int step = hits_need ? (int)(240u / hits_need) : 24;
        int pip_w;
        if (step < 5) step = 5;
        pip_w = step > 3 ? step - 3 : 3;
        for (i = 0; i < (int)hits_need; i++) {
            int filled = i < (int)hits;
            int ox = 36 + i * step;
            for (y = 195; y < 198; y++) {
                for (x = ox; x < ox + pip_w; x++) {
                    px = dst + ((size_t)y * SCR_W + (size_t)x) * 4u;
                    px[0] = filled ? 245u : (uint8_t)(px[0] / 2u + 12u);
                    px[1] = filled ? 215u : (uint8_t)(px[1] / 2u + 16u);
                    px[2] = filled ? 110u : (uint8_t)(px[2] / 2u + 26u);
                    px[3] = 255u;
                }
            }
        }
        (void)lit;
        if (phase && phase->stage_name[0]) {
            snprintf(hud, sizeof hud, "%s  %u/%u  x%u",
                     phase->stage_name, hits, hits_need, phase->combo);
            mb_font_draw_text_shadow(dst, SCR_W, SCR_H, 8, 8, hud,
                                     phase->combo >= 4u ? 120 : 255,
                                     phase->combo >= 4u ? 245 : 225,
                                     phase->combo >= 4u ? 255 : 90,
                                     16, 12, 8, 1);
        }
    }
}

static int load_action_backdrop(special_phase *phase, cdimage *cd, const char *iso_dir,
                                 const char *stage_name, const void *pan,
                                 size_t pan_size, const uint8_t *palette,
                                 uint8_t *bg_rgba) {
    static const char *const k_scrl_files[8] = {
        "07ACDB", "08ATIL", "09BCDB", "10BTIL",
        "11CCDB", "12CTIL", "13DCDB", "14DTIL"
    };
    void *blobs[8] = {0};
    size_t sizes[8] = {0};
    scrl_map map;
    unsigned i;
    int light_error = 0;

    for (i = 0; i < 8u; i++) {
        (void)load_opt(cd, iso_dir, k_scrl_files[i], &blobs[i], &sizes[i]);
        if (sizes[i] == 0) {
            free(blobs[i]);
            blobs[i] = NULL;
        }
    }
    memset(&map, 0, sizeof map);
    if (scrl_map_init(&map,
                      blobs[0], sizes[0], blobs[1], sizes[1],
                      blobs[2], sizes[2], blobs[3], sizes[3],
                      blobs[4], sizes[4], blobs[5], sizes[5],
                      blobs[6], sizes[6], blobs[7], sizes[7],
                      NULL, 0) == 0 &&
        scrl_map_cached_planes(&map) > 0) {
        /* In the FINALE scanout, row zero is the black back screen and
         * the first authored tile row is visible at y=1. */
        if (!strcmp(stage_name, "FINALE")) {
            map.back_mode = 1;
            for (unsigned plane = 0; plane < map.plane_count; ++plane) {
                map.planes[plane].base_y = 1;
                map.planes[plane].no_wrap_y = 1;
            }
            /* The US loader rewrites SCYN2 after drawing scanline 222.
             * NBG2 (map A) restarts from scroll -1 on the last scanline;
             * NBG1 continues reading row 222 of the rear plane. */
            map.planes[0].restart_y = SCR_H - 1u;
        }
        if (phase->finale_lights) light_error = cache_finale_light_texels(phase, &map, palette);
        (void)scrl_map_draw_with_front_mask(&map, palette, bg_rgba, SCR_W, SCR_H,
                                            0u, finale_body_front_mask(phase->finale_body_mesh));
    }
    scrl_map_free(&map);
    for (i = 0; i < 8u; i++) free(blobs[i]);

    if (pan && pan_size > 0 &&
        (!strcmp(stage_name, "BIGBNS") || !strcmp(stage_name, "BIGBONES"))) {
        pan_scroll ps;
        const uint8_t *bank1 = palette + 1024u;
        if (pan_scroll_init(&ps, (const uint8_t *)pan, pan_size, bank1) == 0) {
            size_t p;
            for (p = 0; p < PAN_PLANE_SIZE; p++) {
                uint8_t idx = ps.planes[p];
                if (idx != 0u) {
                    const uint8_t *c = bank1 + (size_t)idx * 4u;
                    bg_rgba[4u * p + 0u] = c[1];
                    bg_rgba[4u * p + 1u] = c[2];
                    bg_rgba[4u * p + 2u] = c[3];
                    bg_rgba[4u * p + 3u] = 255u;
                }
            }
            pan_scroll_free(&ps);
        }
    }
    return light_error;
}

static uint32_t big_word(const special_phase *p, uint32_t va) {
    const uint8_t *code = p->code;
    const uint8_t *s = code + (va - 0x232000u);
    return ((uint32_t)s[0]<<24) | ((uint32_t)s[1]<<16) | ((uint32_t)s[2]<<8) | s[3];
}

/* Loader /02: GetAngle(207f78) reduces each signed difference until
 * max(abs(x),abs(y)) <= 31. Its signed word grid is indexed at 21999a
 * with a 256-byte row stride. The two 1024-entry trig tables are BE32. */
static unsigned action_bearing(const special_phase *p,int x,int y) {
    unsigned maximum=(unsigned)(abs(x)>abs(y)?abs(x):abs(y));
    while (maximum>31) { maximum>>=1; x>>=1; y>>=1; }
    const uint8_t *loader=p->angle_loader;
    const uint8_t *a=loader+0x1999a+2*x-256*y;
    return (((unsigned)a[0]<<8)|a[1])&1023u;
}

static int action_trig(const special_phase *p,unsigned angle,int cosine) {
    const uint8_t *loader=p->angle_loader;
    const uint8_t *a=loader+(cosine?0x14a1c:0x1461c)+4*(angle&1023u);
    return (int)(int16_t)(((unsigned)a[2]<<8)|a[3]);
}

static int big_wave(special_phase *p, unsigned wave) {
    if (wave >= 6 || p->code_size < 0x2716a4u-0x232000u) return -1;
    uint32_t node = big_word(p, 0x27168cu + 4u*wave);
    unsigned parts = 0;
    p->big_count = p->wave_bounces = p->bumper_impulse = 0;
    while (node) {
        if (node < 0x232000u || node-0x232000u > p->code_size-36u) return -1;
        uint32_t next = big_word(p,node), create = big_word(p,node+4);
        int32_t x = (int32_t)big_word(p,node+12), y = (int32_t)big_word(p,node+16);
        if (create == 0x25f318u) {
            if (p->big_count >= sizeof p->big_bumpers/sizeof p->big_bumpers[0] ||
                node-0x232000u > p->code_size-40u) return -1;
            p->big_bumpers[p->big_count++] = (big_bumper){x,y,big_word(p,node+28),big_word(p,node+32),0};
        } else if (create == 0x25e550u) {
            p->part_x = x; p->part_y = y;
            p->part_type = big_word(p,node+28);
            ++parts;
        }
        if (next && next <= node) return -1;
        node = next;
    }
    if (!p->big_count || parts != 1) return -1;
    /* Bonebits2Piece (24e870) chooses both the loose piece and its magnet
     * joint. CanIConnect (24ea3c) checks the two triples in this config's
     * BoneOrder row before permitting Connect to install the next config. */
    uint32_t form;
    switch (p->part_type & 15u) {
    case 2: form=big_word(p,0x24e958); p->part_joint=3; break;
    case 1: form=big_word(p,0x24e95c); p->part_joint=1; break;
    case 8: form=big_word(p,0x24e960); p->part_joint=3; break;
    case 4: form=big_word(p,0x24e964); p->part_joint=1; break;
    default: return -1;
    }
    if (!p->big_config) p->big_config=big_word(p,0x270a60);
    p->big_next_config=0;
    for (unsigned i=0;i<16;++i) {
        uint32_t row=0x270730+40*i;
        if (big_word(p,row)!=p->big_config) continue;
        for (unsigned n=16;n<=28;n+=12)
            if (big_word(p,row+n)==form) p->big_next_config=big_word(p,row+n+8);
    }
    if (!p->big_next_config) return -1;
    p->big_wave = wave;
    p->big_x = p->paddle_x = -0x300000;
    p->big_y = -0x3cf3c;
    p->big_vx = p->big_vy = p->paddle_vx = 0;
    p->boost_left = 0;
    p->big_preroll = 13;
    if (!p->frames) p->big_y=0;
    return 0;
}

static int bounded(int x, int low, int high) {
    return x < low ? low : x > high ? high : x;
}

static int big_distance(int x,int y,int cx,int cy) {
    int dx=(x-cx)>>8,dy=(y-cy)>>8;
    return dx*dx+dy*dy;
}

static int big_bumper_hit(const special_phase *p,const big_bumper *e,int x,int y) {
    /* CBumper::HitCheck 25f6b4; all seven tests use world joint centres. */
    static const unsigned joints[]={5,2,19,13,20,9,16};
    if (big_distance(x,y,e->x,e->y)>0x22939) return 0;
    for(unsigned i=0;i<sizeof joints/sizeof joints[0];++i) {
        unsigned j=joints[i];
        if (i>1 && !big_word(p,p->big_config+124+4*j)) continue;
        if(big_distance(p->collision_joints[j][0],p->collision_joints[j][1],e->x,e->y)
           <=(i==2 ? 0x6031 : 0x5c49)) return 1;
    }
    return 0;
}

static void big_actor_tick(special_phase *p, const saturn_pad *pad,int catchup) {
    if (p->live_pan_ok && p->live_pan.count)
        (void)pan_scroll_seek(&p->live_pan, (p->frames / 2u) % p->live_pan.count);
    /* RunWaveBrain 25ec38 moves the paddle. CheckTilt 25f170 acts on the airborne body. */
    uint16_t pressed=0;
    /* StartWave changes the scheduling phase for the first sixty yields
     * after streaming the PAN. Once the pending display catches up the
     * regular odd-retrace controller acquisition resumes. */
    if(p->frames&1u) p->big_latched=pad->previous;
    unsigned pad_phase=1;
    if ((p->big_transition && p->big_transition>=500) ||
        (p->big_resume_tick && p->frames-p->big_resume_tick<60 && !catchup))
        pad_phase=0;
    if ((p->frames&1u)==pad_phase) {
        pressed=(uint16_t)(p->big_latched & ~p->big_pad);
        p->big_pad=p->big_latched;
    }
    if (p->big_transition) {
        /* NextWaveBrain is scheduled two physical retraces after Connect:
         * message 0a020000 restarts the 15Hz movie, wait for frame >121,
         * then yield eight ticks before applying the wave's health. */
        unsigned age=p->big_transition++;
        if (age==2 && p->big_wave==5) { p->state=SPECIAL_ST_FINISHED; return; }
        if (age==3) ++p->big_wave;
        /* EndBrain (25eef4) stops one publication after BonesPos.y
         * crosses camera.y - 234057. Its first pair of yields overlaps
         * actor removal and the new PAN frame. */
        int stop_y=(int32_t)big_word(p,0x24f440)+(int32_t)big_word(p,0x25ef48);
        unsigned moves=age==3 ? 2u : age==2 || age==4 ? 0u : 1u;
        while(moves-- && !p->big_end_rest) {
            p->big_end_rest=p->big_y<stop_y;
            p->big_vy-=524;
            p->big_x+=bounded(p->big_vx,-6553,6553);
            p->big_y+=bounded(p->big_vy,-13107,13107);
        }
        if (age==500) p->paddle_x=-0x300000;
        if (age<501) return;
        int old_vx=p->paddle_vx;
        runway_skel_add(&p->skel,0x6666);
        if(big_wave(p,p->big_wave)) { p->state=SPECIAL_ST_ABORTED; return; }
        p->paddle_vx=old_vx;
        p->big_preroll=1;
        p->big_transition=0;
        p->big_resume_tick=p->frames;
        p->pose_ticks+=2;
    }
    if (p->big_drop) {
        /* EndWave schedules EndBrain concurrently with the paddle's
         * PrevWaveBrain. Its 4/8/8 yields keep RunWave suspended. */
        unsigned age=p->big_drop++;
        if (age==1 && runway_skel_loose(&p->skel,0x9999)) {
            p->state=SPECIAL_ST_LOST;
            return;
        }
        if(age<=4) {
            p->big_x+=bounded(p->big_vx,-6553,6553);
            p->big_y+=bounded(p->big_vy-524,-13107,13107);
        }
        if(age==13) p->paddle_x=-0x300000;
        if(age<22) return;
        int old_vx=p->paddle_vx;
        if(p->big_wave) {
            --p->parts_collected;
            for(unsigned j=0;j<16;++j) {
                uint32_t row=0x270730+40*j;
                if(big_word(p,row)==p->big_config) { p->big_config=big_word(p,row+4); break; }
            }
        }
        if(big_wave(p,p->big_wave?p->big_wave-1:0)) { p->state=SPECIAL_ST_ABORTED;return; }
        p->paddle_vx=old_vx;
        p->big_preroll=1;
        p->big_drop=0;
    }
    if (p->big_pad & 0x8000) p->paddle_vx -= 0x2d0;
    else if (p->big_pad & 0x4000) p->paddle_vx += 0x2d0;
    else if (p->paddle_vx > 0) p->paddle_vx = bounded(p->paddle_vx - 0x2d0,0,0x1c28);
    else p->paddle_vx = bounded(p->paddle_vx + 0x2d0,-0x1c28,0);
    p->paddle_vx = bounded(p->paddle_vx,-0x1c28,0x1c28);
    p->paddle_x = bounded(p->paddle_x + p->paddle_vx, (int32_t)0xffa32340u,(int32_t)0xfffcd420u);
    /* The paddle brain is already interactive while the start brain
     * attaches CMover to BonesExtPos. The publication first becomes visible
     * on retrace 13 relative to 25ec5e on the US original. */
    if (p->big_preroll) {
        if (!--p->big_preroll) p->big_y=-0x3cf3c;
        return;
    }
    int bounced=0;
    int paddle_offset = p->big_x - p->paddle_x + 0x3cf3c;
    if (p->big_vy<=0 && paddle_offset>0 && paddle_offset<0x79e79) {
        uint32_t at = 0x270a8cu + 12u*((unsigned)paddle_offset*256u/0x79e79u);
        if (p->big_y < (int32_t)big_word(p,at)) {
            p->big_vx = (int32_t)big_word(p,at+4);
            p->big_vy = (int32_t)big_word(p,at+8) + (p->boost_left ? 0xa3d : 0);
            bounced=1;
        }
    } else if (p->big_vy<=0 && p->big_y < (int32_t)big_word(p,0x270a8cu)) {
        p->big_drop=1;
    }
    /* After CheckTilt yields, GoDown compares the last published BonesPos
     * against the new paddle position. CMover publishes its launch velocity
     * before advancing the acceleration. The first launch is -2156,+12884. */
    int previous_x=p->big_x, previous_y=p->big_y;
    if (!bounced && !p->bumper_impulse) p->big_vy-=524;
    p->bumper_impulse=0;
    p->big_x+=bounded(p->big_vx,-6553,6553);
    p->big_y+=bounded(p->big_vy,-13107,13107);
    if (pressed & 0x0008) p->big_vx += 0x666;
    else if (pressed & 0x0080) p->big_vx -= 0x666;
    if (pressed & 0x0200) p->boost_left = 20;
    else if (p->boost_left) --p->boost_left;
    if (p->big_x < (int32_t)0xffa32340u) p->big_vx = 0x1c28;
    if (p->big_x > (int32_t)0xfffcd420u) p->big_vx = -0x1c28;
    ++p->pose_ticks;
    int turn=(int)(-1441*(int)p->pose_ticks)>>8;
    stage_body_pose pose={{previous_x,previous_y,0},{256,512,0},{turn,0,turn},
                           256,p->pose_ticks,p->big_config,big_word(p,p->big_wave>3u ? 0x237400 : 0x2373e0)};
    (void)stage_body_joints(p->action_rig,&pose,p->collision_joints);
    for (unsigned i=0; i<p->big_count; ++i) {
        big_bumper *e = &p->big_bumpers[i];
        if (e->touch) { --e->touch; continue; }
        if (!big_bumper_hit(p,e,previous_x,previous_y)) continue;
        /* FadeHit waits for its ten authored animation keys to finish,
         * schedules respawn at GameFrame+300 and sleeps 36 more retraces
         * after FadeRecreate. RotateHit simply yields ten. */
        e->touch=10;
        if(e->type==2) {
            uint32_t anim=big_word(p,0x2716c0);
            e->touch=300+36;
            for(unsigned k=0;k<10;++k) e->touch+=(int)(big_word(p,anim+12*k+4)&0xffff);
        }
        ++p->bumper_bounces;
        ++p->wave_bounces;
        if (previous_x!=e->x || previous_y!=e->y) {
            unsigned angle=action_bearing(p,previous_x-e->x,previous_y-e->y);
            p->big_vx=action_trig(p,angle,0)*(int)e->power;
            p->big_vy=action_trig(p,angle,1)*(int)e->power/2;
            p->bumper_impulse=1;
        }
    }
    const int32_t *joint=p->collision_joints[p->part_joint];
    if (p->wave_bounces && abs(joint[0]-p->part_x)+abs(joint[1]-p->part_y)<=0xb331) {
        ++p->parts_collected;
        p->big_config=p->big_next_config;
        p->big_transition=1;
        p->big_end_rest=0;
    }
    int px = 160 - (int)(((int64_t)p->big_x-p->paddle_x)*42/65536);
    int py = 32 - p->big_y*42/65536;
    p->bones_x = (unsigned)bounded(px,0,319)*UNITS_PER_PIXEL;
    p->bones_y = (unsigned)bounded(py,0,223)*UNITS_PER_PIXEL;
    p->airborne = 1;
    p->vel_x = -p->big_vx; p->vel_y = -p->big_vy;
    snprintf(p->status,sizeof p->status,"BIGBNS wave=%u bumper=%u parts=%u",p->big_wave+1,p->bumper_bounces,p->parts_collected);
}

static void step_big(special_phase *p,const saturn_pad *pad) {
    big_actor_tick(p,pad,0);
    /* Streaming restarts the two actor lists a tick apart. RunWaveBrain
     * and CMover each run twice on the sixtieth retrace after StartWave. */
    if (p->big_resume_tick && p->frames-p->big_resume_tick==59 && p->state==SPECIAL_ST_PLAY)
        big_actor_tick(p,pad,1);
}

static uint32_t big_autoplay(const special_phase *p, unsigned tick) {
    int target_x = p->part_x;
    if (!p->wave_bounces) {
        int best = INT_MAX;
        for (unsigned i = 0; i < p->big_count; ++i) {
            const big_bumper *b = &p->big_bumpers[i];
            int dist = abs(b->x-p->big_x) + abs(b->y-p->big_y);
            if (dist < best) { best=dist; target_x=b->x; }
        }
    }
    int dx = target_x-p->big_x;
    int vx = bounded(dx/10,-6553,6553);
    int paddle_goal=p->big_x+3*bounded(p->big_vx,-6553,6553)-bounded(vx*12,-85000,85000);
    int stopping=p->paddle_x+p->paddle_vx*abs(p->paddle_vx)/(2*720);
    int offset=paddle_goal-stopping;
    uint32_t btn=0;
    if (offset > 4000) btn |= MB_PAD_LEFT;
    else if (offset < -4000) btn |= MB_PAD_RIGHT;
    if (!p->big_preroll && (tick&3u)<2u) {
        if (p->big_vx < vx - 500) btn |= MB_PAD_L;
        else if (p->big_vx > vx + 500) btn |= MB_PAD_R;
        btn |= MB_PAD_C;
    }
    return btn;
}


int special_phase_load(special_phase *phase, cdimage *cd, const char *iso_dir,
                       stage_kind kind) {
    uint8_t palette[4096];
    size_t off;
    char path[96];
    unsigned guard;
    if (!phase || !cd || !iso_dir) return -1;
    memset(phase, 0, sizeof *phase);
    snprintf(phase->iso_dir, sizeof phase->iso_dir, "%s", iso_dir);
    stage_name_from_iso(phase->stage_name, sizeof phase->stage_name, iso_dir);
    phase->kind = kind;
    phase->mode = mode_from_kind(kind);
    phase->state = SPECIAL_ST_PLAY;
    phase->facing = 1;
    phase->arena_y_px = (int32_t)(GROUND_Y / UNITS_PER_PIXEL);
    phase->walk_dist = 0;
    phase->bones_x = (kind == STAGE_KIND_FINALE) ? (SCR_W / 2 * UNITS_PER_PIXEL) : START_X;
    phase->bones_y = is_bigbns_stage(phase)
                         ? ((uint32_t)bigbns_dome_y_px((int)(phase->bones_x / UNITS_PER_PIXEL)) *
                            UNITS_PER_PIXEL)
                         : GROUND_Y;
    if (kind == STAGE_KIND_FINALE) phase->bones_y = 144u * UNITS_PER_PIXEL;
    phase->finish_x = FINISH_X;
    phase->pages_needed = SPECIAL_PRESENT_PAGES;
    phase->beat_period = SPECIAL_BEAT_PERIOD;
    runway_skel_init(&phase->skel);
    /* GENRM and BIGBNS use the shared action-stage enemy manager. */
    if (kind != STAGE_KIND_FINALE)
        stage_enemies_init_special(&phase->enemies, phase->stage_name, UNITS_PER_PIXEL);
    stage_enemies_load_disc_sprites(cd);
    if (kind == STAGE_KIND_GENROOM) {
        phase->enemies.count = 0;
        phase->bones_x = 160u * UNITS_PER_PIXEL;
        phase->bones_y = 112u * UNITS_PER_PIXEL;
        phase->arena_y_px = 112;
        runway_skel_loose(&phase->skel, 0x18000);
        phase->hurt_timer=22;
        phase->zap_clock=1;
    }
    if (phase->mode == SPECIAL_MODE_ACTION && kind != STAGE_KIND_GENROOM) {
        unsigned hpy = is_bigbns_stage(phase) ? (unsigned)(bigbns_dome_y_px(165) - 6) : 180u;
        stage_enemies_add_health_pickup(&phase->enemies, 165u, hpy,
                                        RUNWAY_DAMAGE_DEFAULT);
    }
    if (kind == STAGE_KIND_FINALE)
        finale_ring_init(phase);

    /* LOSE / movie: AVI(+DUCK) required; CODE optional. */
    if (kind == STAGE_KIND_LOSE || kind == STAGE_KIND_MOVIE ||
        kind == STAGE_KIND_INTRO) {
        (void)load_opt(cd, iso_dir, "20AVI", &phase->avi, &phase->avi_size);
        (void)load_opt(cd, iso_dir, "19DUCK", &phase->duck, &phase->duck_size);
        if (phase->avi_size == 0) {
            snprintf(phase->status, sizeof phase->status, "missing %s/20AVI", iso_dir);
            goto fail;
        }
        phase->mode = SPECIAL_MODE_PRESENT;
        phase->audio_track = 4;
        phase->pages_needed = 4;
    } else {
        snprintf(path, sizeof path, "%s/03CODE", iso_dir);
        if (resource_load(cd, path, &phase->code, &phase->code_size) ||
            phase->code_size <= 20000)
            goto fail;
        (void)load_opt(cd, iso_dir, "04STXT", &phase->textures, &phase->textures_size);
        (void)load_opt(cd, iso_dir, "02TITL", &phase->title, &phase->title_size);
        (void)load_opt(cd, iso_dir, "05SND1", &phase->sound, &phase->sound_size);
        (void)load_opt(cd, iso_dir, "15PAN", &phase->pan, &phase->pan_size);
        /* These AVIs are transitions, not their playable scenes. */
        if (strcmp(phase->stage_name, "GUITAR") != 0 &&
            strcmp(phase->stage_name, "DRUM") != 0 &&
            strcmp(phase->stage_name, "BIGBNS") != 0 &&
            strcmp(phase->stage_name, "GENRM") != 0) {
            (void)load_opt(cd, iso_dir, "19DUCK", &phase->duck, &phase->duck_size);
            (void)load_opt(cd, iso_dir, "20AVI", &phase->avi, &phase->avi_size);
        }
        if (phase->mode == SPECIAL_MODE_RHYTHM)
            phase->audio_track = 5;
        else if (phase->mode == SPECIAL_MODE_ACTION)
            phase->audio_track = 6;
        else
            phase->audio_track = 7;
        if (phase->textures_size == 0 && phase->avi_size == 0) goto fail;
    }

    if (kind==STAGE_KIND_GENROOM || kind==STAGE_KIND_BIGBONES) {
        int big=kind==STAGE_KIND_BIGBONES;
        phase->action_rig=stage_body_load_joints(phase->code,phase->code_size,
            big?0x270a60u:0x2703a4u,big?0x244810u:0x243f18u,big?0x244834u:0x243f3cu);
        if (!phase->action_rig) goto fail;
        size_t loader_size=0;
        if (resource_load(cd,"/02",&phase->angle_loader,&loader_size) || loader_size<0x1d928u)
            goto fail;
    }
    if (kind == STAGE_KIND_GENROOM) {
        if (phase->code_size < 0x26f1f0u-0x232000u) goto fail;
        phase->gen_rot_vel=(int)big_word(phase,0x26f1e0u);
        phase->gen_rot_acc=(int)big_word(phase,0x26f1e4u);
        phase->gen_rot_limit=(int)big_word(phase,0x26f1ecu);
        /* RedToBlue waits for the frame-list completion callback. */
        for (unsigned a=0x232f40u; a<0x233700u; a+=12u) {
            uint32_t callback=big_word(phase,a+8);
            if (callback!=0x206eb4u && callback!=0x207214u) goto fail;
            phase->change_frames+=big_word(phase,a+4)&0xffffu;
            if (callback==0x207214u) break;
        }
    }
    if (kind == STAGE_KIND_BIGBONES) {
        phase->enemies.count = phase->enemies.pickup_count = 0;
        if (big_wave(phase,0)) goto fail;
    }
    if (phase->mode == SPECIAL_MODE_RHYTHM && special_phase_read_music(phase)) goto fail;

    if (kind == STAGE_KIND_FINALE && load_finale_arena(phase, cd)) {
        snprintf(phase->status, sizeof phase->status, "invalid FINALE bolt animation or loader");
        goto fail;
    }

    /* §20/§22: open DUCK AVI with 19DUCK tables when present.
     * FINALE: armed for PlayDuckMovie(3); movie_ok starts the film immediately
     * (headless/tests). Interactive host may defer via finale_defer_ending. */
    if (phase->avi_size > 0 &&
        duck_avi_open_with_duck(&phase->movie, phase->avi, phase->avi_size,
                                phase->duck, phase->duck_size) == 0) {
        phase->movie_ok = 1;
        if (kind == STAGE_KIND_FINALE)
            phase->finale_movie_armed = 1;
        if (phase->mode == SPECIAL_MODE_PRESENT) {
            unsigned need = phase->movie.frame_count;
            if (need < 4) need = 4;
            phase->pages_needed = need;
        }
        if (!duck_avi_step(&phase->movie)) goto fail;
        if (phase->movie.pcm_samples > 0)
            phase->movie_pcm_pending = 1;
    }

    if (phase->mode == SPECIAL_MODE_PRESENT && !phase->movie_ok) {
        if (!phase->avi_size)
            snprintf(phase->status, sizeof phase->status, "missing %s/20AVI", iso_dir);
        goto fail;
    }

    phase->bg_rgba = malloc((size_t)SCR_W * SCR_H * 4u);
    phase->frame_rgba = malloc((size_t)SCR_W * SCR_H * 4u);
    if (!phase->bg_rgba || !phase->frame_rgba) goto fail;

    if (phase->mode == SPECIAL_MODE_RHYTHM)
        fill_bg(phase->bg_rgba, 16, 8, 32);
    else if (phase->mode == SPECIAL_MODE_ACTION)
        fill_bg(phase->bg_rgba, 24, 40, 24);
    else if (kind == STAGE_KIND_LOSE)
        fill_bg(phase->bg_rgba, 48, 8, 8);
    else
        fill_bg(phase->bg_rgba, 8, 16, 40);

    build_palette((const uint8_t *)phase->code, phase->code_size, palette);
    off = 0;
    guard = 0;
    while (phase->gfx_count < SPECIAL_GFX_MAX && guard++ < 128 &&
           phase->textures_size > 0) {
        uint8_t *px = NULL;
        unsigned w = 0, h = 0;
        size_t before = off;
        if (decode_stxtr(phase->textures, phase->textures_size, &off, palette,
                         &px, &w, &h)) {
            if (off == before) break;
            continue;
        }
        if (w >= 8 && h >= 8 && w <= SCR_W && h <= SCR_H) {
            phase->gfx_rgba[phase->gfx_count] = px;
            phase->gfx_w[phase->gfx_count] = w;
            phase->gfx_h[phase->gfx_count] = h;
            phase->gfx_count++;
        } else {
            free(px);
        }
    }

    if (phase->mode == SPECIAL_MODE_RHYTHM ||
        phase->mode == SPECIAL_MODE_ACTION || kind == STAGE_KIND_FINALE) {
        if (load_action_backdrop(phase, cd, iso_dir, phase->stage_name, phase->pan,
                                 phase->pan_size, palette, phase->bg_rgba)) goto fail;
        if (phase->pan && phase->pan_size > 0 && is_bigbns_stage(phase)) {
            if (pan_scroll_init(&phase->live_pan, (const uint8_t *)phase->pan,
                                phase->pan_size, palette + 1024u) == 0) {
                phase->live_pan_ok = 1;
            }
        }
    } else if (phase->gfx_count > 0) {
        /* Backdrop plate: largest gfx centered. */
        unsigned best = 0, i, area, best_area = 0;
        for (i = 0; i < phase->gfx_count; i++) {
            area = phase->gfx_w[i] * phase->gfx_h[i];
            if (area > best_area) {
                best_area = area;
                best = i;
            }
        }
        runway_compose_blit_sprite(
            phase->bg_rgba, SCR_W, SCR_H, phase->gfx_rgba[best],
            phase->gfx_w[best], phase->gfx_h[best],
            ((int)SCR_W - (int)phase->gfx_w[best]) / 2,
            ((int)SCR_H - (int)phase->gfx_h[best]) / 2);
        phase->gfx_index = best;
    }

    snprintf(phase->status, sizeof phase->status,
             "%s special mode=%d gfx=%u avi=%zu snd=%zu",
             phase->stage_name, (int)phase->mode, phase->gfx_count,
             phase->avi_size, phase->sound_size);
    return special_phase_render(phase);

fail:
    {
        char error[sizeof phase->status];
        snprintf(error, sizeof error, "%s", phase->movie.decode_error || !phase->movie_ok
                 ? (phase->movie.status[0] ? phase->movie.status : phase->status)
                 : "special phase allocation failed");
        special_phase_free(phase);
        snprintf(phase->status, sizeof phase->status, "%s", error);
    }
    return -1;
}

void special_phase_free(special_phase *phase) {
    unsigned i;
    if (!phase) return;
    if (phase->finale_lights) {
        free(phase->finale_lights->texels);
        free(phase->finale_lights);
    }
    if (phase->live_pan_ok) {
        pan_scroll_free(&phase->live_pan);
        phase->live_pan_ok = 0;
    }
    for (i = 0; i < phase->gfx_count && i < SPECIAL_GFX_MAX; i++)
        free(phase->gfx_rgba[i]);
    for (unsigned d = 0; d < 4; ++d) {
        for (size_t n = 0; n < sizeof phase->finale_red[d].frames / sizeof(finale_bolt_frame); ++n) {
            free(phase->finale_red[d].frames[n].unpacked);
            free(phase->finale_blue[d].frames[n].unpacked);
        }
    }
    for (size_t n = 0; n < sizeof phase->finale_face.frames / sizeof(finale_bolt_frame); ++n)
        free(phase->finale_face.frames[n].unpacked);
    for (unsigned type = 0; type < sizeof phase->finale_hit / sizeof phase->finale_hit[0]; ++type)
        for (size_t n = 0; n < sizeof phase->finale_hit[type].frames / sizeof(finale_bolt_frame); ++n)
            free(phase->finale_hit[type].frames[n].unpacked);
    free(phase->finale_body_mesh);
    free(phase->finale_dtxt);
    duck_avi_free(&phase->movie);
    free(phase->title);
    free(phase->textures);
    free(phase->code);
    free(phase->sound);
    free(phase->duck);
    free(phase->avi);
    free(phase->pan);
    free(phase->action_rig);
    free(phase->angle_loader);
    free(phase->drum_notes);
    free(phase->bg_rgba);
    free(phase->frame_rgba);
    memset(phase, 0, sizeof *phase);
}

static void score_note(special_phase *p, int good, int amount) {
    p->last_hit_grade = good ? 1 : -1;
    p->grade_timer = 12;
    if (good) {
        ++p->hits;
        if (++p->combo > p->max_combo) p->max_combo = p->combo;
        runway_skel_add(&p->skel, amount);
        ++p->rhythm_heals;
        p->sfx_flags |= RUNWAY_SFX_FLAG_C;
    } else {
        ++p->misses;
        p->combo = 0;
        runway_skel_loose(&p->skel, amount);
        p->sfx_flags |= RUNWAY_SFX_FLAG_TALL;
    }
}

/* The CD status is in 75-Hz sectors. CGBones uses the upper byte of the
 * accumulating product (loader multiply at 0x213edc) to schedule its latch. */
static unsigned guitar_beat(unsigned tick) {
    /* From the first 0x2613aa iteration, the CD is already advancing.
     * Regression against all 62 edges of the positive US-disc capture:
     * ~59.766 retraces/s, 1.71 sectors ahead of this thread's epoch. */
    uint64_t sectors = ((uint64_t)tick * 750000u + 1020000u) / 597656u;
    return (unsigned)((sectors * 0x85cd8u) >> 24);
}

static void step_rhythm(special_phase *p, const saturn_pad *pad) {
    ++p->music_tick;
    if (p->grade_timer) --p->grade_timer;
    if (p->music_ending) {
        /* ObjSetBrain is observed on the iteration following the threshold.
         * Its replacement runs one retrace later, yielding 180 times. */
        if (p->music_ending <= 181 && p->music_ending > 1) runway_skel_add(&p->skel,0x51e);
        if (!--p->music_ending) {
            p->state = SPECIAL_ST_FINISHED;
            snprintf(p->status,sizeof p->status,"%s rhythm end hits=%u misses=%u skel=%d",
                     p->stage_name,p->hits,p->misses,p->skel.skeletism);
        }
        return;
    }
    if (p->drum_notes) {
        const rhythm_table *s = p->rhythm;
        unsigned t = p->music_tick;
        if (p->note_cursor < s->count &&
            rhythm_note_host_frame(s, p->note_cursor) == t) {
            unsigned group = s->notes[p->note_cursor++].kind;
            p->drum_judged_mask = p->music_masks[group];
            score_note(p, p->music_latch != 0, p->music_latch ? 0x83 : 0x7ae);
            ++p->beat_index;
            if (p->note_cursor == s->count && !p->skel.died)
                p->state = SPECIAL_ST_FINISHED;
        }
        if (p->drum_prompt < s->count && rhythm_note_host_frame(s, p->drum_prompt) == t + 10u) {
            p->expected_kind = s->notes[p->drum_prompt++].kind;
            p->drum_preview_mask = p->music_masks[p->expected_kind];
        }
        if (p->music_latch) --p->music_latch;
        /* Stack locals r14+0 and r14+4 survive ObjYield until the next group. */
        if (pad->held & (p->drum_preview_mask | p->drum_judged_mask)) p->music_latch = 10;
        p->in_window = p->music_latch != 0;
        if (p->skel.skeletism <= 0x1998) p->state = SPECIAL_ST_LOST;
    } else {
        unsigned beat = guitar_beat(p->music_tick);
        /* 0x2903b4 / 0x2903b2 are the edges of the previously sampled
         * pad. All 62 StartLick calls in score2 observe even samples on
         * the following odd VBlank. A tap between two polls is missed. */
        uint16_t pressed=0, released=0;
        if (p->music_tick & 1u) {
            pressed = (uint16_t)(pad->previous & ~p->guitar_sample);
            released = (uint16_t)(p->guitar_sample & ~pad->previous);
            p->guitar_sample = pad->previous;
        }
        if (p->lick_left && !--p->lick_left) {
            p->lick_valid = 0;
            p->music_wait = 0x140;
        }
        if (p->music_latch) --p->music_latch;
        if (p->beat_index != beat) {
            p->beat_index = beat;
            p->arm_tick = p->music_tick + 19u;
            if (p->lick_valid) runway_skel_add(&p->skel, 0x147);
        }
        if (p->arm_tick == p->music_tick || (!p->beat_index && p->music_tick == 100u))
            p->music_latch = 9;
        if (p->music_wait && !--p->music_wait) {
            score_note(p, 0, 0x3333);
            p->music_wait = 0x140;
        }
        for (unsigned i = 0; i < 6; ++i) {
            if (pressed & p->music_masks[i]) {
                int good = p->music_latch != 0;
                int changed = (int)i != p->active_lick;
                p->active_lick = (int)i;
                p->expected_kind = i;
                p->lick_left = p->lick_durations[i];
                p->lick_valid = good;
                p->music_wait = 0xc80;
                if (good) p->music_latch = 0;
                score_note(p, good, good ? 0x147 + (changed ? 0x28f : 0) : 0x7ae);
                break;
            }
            if ((int)i == p->active_lick && (released & p->music_masks[i])) {
                p->lick_valid = 0;
                p->lick_left = 0;
                p->music_wait = 0x140;
            }
        }
        p->in_window = p->music_latch != 0;
        if (p->skel.died) p->state = SPECIAL_ST_LOST;
        else if (p->skel.skeletism > 0x1e666 || (uint64_t)p->music_tick * 7500u / 5976u > 0x38a4u)
            p->music_ending = 182;
    }
    snprintf(p->status, sizeof p->status, "%s rhythm beat=%u hits=%u misses=%u skel=%d",
             p->stage_name, p->beat_index, p->hits, p->misses, p->skel.skeletism);
}

static uint32_t music_autoplay(const special_phase *p) {
    static const uint32_t drums[] = {MB_PAD_UP, MB_PAD_DOWN, MB_PAD_RIGHT, MB_PAD_LEFT};
    static const uint32_t frets[] = {MB_PAD_A, MB_PAD_B, MB_PAD_C, MB_PAD_X, MB_PAD_Y, MB_PAD_Z};
    unsigned tick = p->music_tick + 1u;
    if (!p->drum_notes) {
        if (p->arm_tick && !p->music_ending && tick >= p->arm_tick+1 && tick < p->arm_tick+7)
            return frets[p->beat_index % 6u];
        return 0;
    }
    unsigned preview = p->drum_prompt;
    if (preview < p->score.count && rhythm_note_host_frame(&p->score, preview) <= tick + 10u) ++preview;
    if (preview) return drums[p->score.notes[preview-1u].kind];
    return 0;
}


/* CBigRed::Brain (23d0e0) emits one CRed every sixty updates. */
static void generator_spawn(special_phase *p) {
    stage_enemies *e = &p->enemies;
    unsigned i = 0;
    while (i < e->count && e->ents[i].active) ++i;
    if (i >= 20) return;
    if (i == e->count) ++e->count;
    int x = p->gen_x, y = p->gen_y;
    memset(&p->plasma[i],0,sizeof p->plasma[i]);
    p->plasma[i].birth=p->frames;
    unsigned sequence = p->plasma_emitted++;
    unsigned turn = (sequence+1u)%3u;
    if (turn == 1) x = -x;
    if (turn == 2) y = -y;
    int ax = abs(x), ay = abs(y);
    int ex = ax > ay ? abs(0x55000-ax) : 0x55000;
    int ey = ax > ay ? 0x55000 : abs(0x55000-ay);
    unsigned dir = (x > 0 ? 2u : 3u) - (y > 0 ? 0u : 2u);
    int speed = (int)big_word(p,0x267388u+(sequence%4u)*4u)*2;
    p->plasma[i].x = x>0 ? -ex : ex;
    p->plasma[i].y = y>0 ? -ey : ey;
    p->plasma[i].vx = (dir & 1u) ? -speed : speed;
    p->plasma[i].vy = (dir & 2u) ? speed : -speed;
    stage_enemy *ent = &e->ents[i];
    memset(ent, 0, sizeof *ent);
    ent->kind = STAGE_ENEMY_RED;
    ent->active = ent->hp = 1;
    ent->damage = 0xa00;
    ent->cooldown = 600;
    ent->patrol_hi = 320u * UNITS_PER_PIXEL;
    ent->patrol_lo = 0;
    ent->facing = 1;
}

static void step_generator_plasma(special_phase *p) {
    stage_enemies *e = &p->enemies;
    const unsigned u = UNITS_PER_PIXEL;

    int previous_zap_x[8]={0}, previous_zap_y[8]={0};
    for (unsigned j = 0; j < 8; ++j) {
        if (!p->zap_ttl[j]) continue;
        previous_zap_x[j]=p->zap_x[j];
        previous_zap_y[j]=p->zap_y[j];
        if (--p->zap_ttl[j]==60) {
            previous_zap_x[j]=p->zap_origin_x;
            previous_zap_y[j]=p->zap_origin_y;
            continue;
        }
        p->zap_x[j] += p->zap_vx[j];
        p->zap_y[j] += p->zap_vy[j];
    }
    unsigned order[20], count=0;
    for (unsigned i=0;i<e->count;++i) {
        if (!e->ents[i].active) continue;
        unsigned n=count++;
        while(n && p->plasma[order[n-1]].birth>p->plasma[i].birth) {
            order[n]=order[n-1]; --n;
        }
        order[n]=i;
    }
    int recoil=0, hit_x=0,hit_y=0;
    for (unsigned n=0;n<count;++n) {
        unsigned i=order[n];
        stage_enemy *ent=&e->ents[i];
        ++ent->anim;
        int dx=p->gen_x-p->plasma[i].x, dy=p->gen_y-p->plasma[i].y;
        /* BonesObjHitChk 2427f8: fixed coarse bound and guaranteed inner
         * sphere followed by the live joint centres and per-joint radii. */
        int64_t d2 = (int64_t)(dx>>8)*(dx>>8)+(int64_t)(dy>>8)*(dy>>8);
        if (p->plasma[i].change) {
            if (--p->plasma[i].change) continue;
            ent->kind=STAGE_ENEMY_BLUE;
            ent->cooldown=180u+(512u-128u+2u)/3u;
            if (!p->blue_head && p->skel.skeletism>0x16666) {
                p->blue_head=i+1;
                p->plasma[i].heading=512;
            }
        }
        if (ent->kind==STAGE_ENEMY_RED && ent->cooldown && !--ent->cooldown) {
            p->plasma[i].shrink=77;
        }
        if(p->plasma[i].shrink && !--p->plasma[i].shrink) { ent->active=0; continue; }
        if (ent->kind == STAGE_ENEMY_RED) {
            /* CheckForZap 23d448 checks each axis against 0x4ccc (32 px/unit). */
            for (unsigned j = 0; j < 8; ++j) {
                /* Actors execute in creation order. An older CRed sees
                 * the projectile before its Move callback this retrace. */
                int after=p->zap_birth[j]<=p->plasma[i].birth;
                int zx=after ? p->zap_x[j] : previous_zap_x[j];
                int zy=after ? p->zap_y[j] : previous_zap_y[j];
                if (!p->zap_ttl[j] || abs(zx-p->plasma[i].x)>0x4ccc ||
                    abs(zy-p->plasma[i].y)>0x4ccc) continue;
                p->zap_ttl[j] = 0;
                p->plasma[i].shrink=0;
                /* End-of-animation dispatch creates CBlue on the next yield. */
                p->plasma[i].change=p->change_frames+1;
                break;
            }
        }
        int hit=d2<0xbb8;
        static const unsigned checked[]={5,2,12,19,13,20,9,16};
        int radius=p->blue_head==i+1 ? 0xfa0 : 0x2000;
        /* BonesObjHitChk writes _Hit on passing the broad phase, before
         * the individual joints are tested. ObjHitGenerator consumes the
         * last such position after this actor list has completed. */
        if (!p->plasma[i].change && !p->plasma[i].shrink && d2<220000+radius) {
            hit_x=p->plasma[i].x;
            hit_y=p->plasma[i].y;
        }
        uint32_t config=big_word(p,0x2703a4);
        for(unsigned n=0; !hit && d2<220000+radius && n<sizeof checked/sizeof checked[0]; ++n) {
            unsigned joint=checked[n];
            if (n>1 && !big_word(p,config+124u+4u*joint)) continue;
            int64_t d=0;
            for(unsigned k=0;k<3;++k) {
                int32_t at=k==0?p->plasma[i].x:k==1?p->plasma[i].y:0;
                int32_t delta=(p->collision_joints[joint][k]-at)>>8;
                d+=(int64_t)delta*delta;
            }
            hit=d<radius+((joint==12 || joint==19) ? 0xbb8 : 0x7d0);
        }
        if (ent->kind == STAGE_ENEMY_RED && !p->plasma[i].change && !p->plasma[i].shrink && hit) {
            if (!p->hurt_timer) {
                runway_skel_loose(&p->skel, 0xa00);
                ++p->hazard_hits;
                recoil=1;
                p->gen_rot_vel=160*p->gen_rot_acc;
                p->gen_rot_limit=0;
                p->hurt_timer = 30;
            }
            p->plasma[i].shrink=77;
        } else if (ent->kind == STAGE_ENEMY_BLUE && hit) {
            if (p->blue_head == i + 1) {
                p->state = SPECIAL_ST_FINISHED; /* CBluMov::Brain -> EndLevel(0) */
                p->blue_head = 0;
            } else {
                runway_skel_add(&p->skel, 0x0f00);
                ++p->plasma_collected;
            }
            ent->active = 0;
        }
        if (ent->active && p->blue_head==i+1) {
            /* CBluMov::Fly homes for 240 ticks, then FlyFree continues at
             * its last heading for 300 ticks. Both test for contact. */
            unsigned age=p->plasma[i].age++;
            unsigned angle=p->plasma[i].heading;
            if (age<=240) {
                int cosine=action_trig(p,angle,1),sine=action_trig(p,angle,0);
                int turn=(int64_t)cosine*dy-(int64_t)sine*dx>0?11:-11;
                angle=(angle+(unsigned)turn)&1023u;
                p->plasma[i].heading=angle;
            }
            int speed=age<=240?1310:1966;
            p->plasma[i].x+=(action_trig(p,angle,1)*speed)>>8;
            p->plasma[i].y+=(action_trig(p,angle,0)*speed)>>8;
            if (age>=540) { ent->active=0;p->blue_head=0; }
        }
        if (ent->kind == STAGE_ENEMY_BLUE && p->blue_head!=i+1 && ent->cooldown && !--ent->cooldown)
            ent->active=0;
        if (ent->kind == STAGE_ENEMY_RED && !p->plasma[i].change) {
            p->plasma[i].x += p->plasma[i].vx;
            p->plasma[i].y += p->plasma[i].vy;
            if (p->plasma[i].x < -0x54ccc) p->plasma[i].x=0x54ccc;
            else if (p->plasma[i].x > 0x54ccc) p->plasma[i].x=-0x54ccc;
            if (p->plasma[i].y < -0x3cccc) p->plasma[i].y=0x3cccc;
            else if (p->plasma[i].y > 0x3cccc) p->plasma[i].y=-0x3cccc;
        }
        ent->x=(unsigned)bounded(160-p->plasma[i].x/2048,0,319)*u;
        ent->y=(unsigned)bounded(112-p->plasma[i].y/2048,0,223)*u;
    }
    if (recoil) {
        unsigned angle=action_bearing(p,hit_x-p->gen_x,hit_y-p->gen_y);
        p->gen_vx=-16*action_trig(p,angle,0);
        p->gen_vy=-16*action_trig(p,angle,1);
    }
    if (p->frames%60u==59u) generator_spawn(p);
    if (p->skel.died) p->state = SPECIAL_ST_LOST;
    snprintf(p->status, sizeof p->status, "GENRM blue=%u head=%u skel=%d", p->plasma_collected, p->blue_head, p->skel.skeletism);
}

/* Input only. Spin and keep firing while collecting the closest available blue. */
static uint32_t generator_autoplay(const special_phase *p) {
    int gx=0,gy=0,nearest=INT_MAX;
    for (unsigned i=0;i<p->enemies.count;++i) {
        if (!p->enemies.ents[i].active || (p->blue_head && p->blue_head!=i+1)) continue;
        int dx=p->plasma[i].x-p->gen_x,dy=p->plasma[i].y-p->gen_y;
        int cost=abs(dx)+abs(dy);
        if (p->enemies.ents[i].kind!=STAGE_ENEMY_BLUE && !p->plasma[i].change) continue;
        if (cost<nearest) { nearest=cost;gx=p->plasma[i].x;gy=p->plasma[i].y; }
    }
    int dx=gx-p->gen_x-5*p->gen_vx,dy=gy-p->gen_y-5*p->gen_vy;
    uint32_t pad=MB_PAD_B|MB_PAD_R;
    if (dx>2048) pad|=MB_PAD_LEFT;
    else if(dx< -2048) pad|=MB_PAD_RIGHT;
    if(dy>2048) pad|=MB_PAD_UP;
    else if(dy< -2048) pad|=MB_PAD_DOWN;
    return pad;
}

unsigned special_phase_targets_remaining(const special_phase *phase) {
    if (!phase || phase->mode != SPECIAL_MODE_ACTION) return 0;
    if (phase->kind == STAGE_KIND_BIGBONES) return phase->state == SPECIAL_ST_FINISHED ? 0 : 6 - phase->big_wave;
    if (phase->kind == STAGE_KIND_GENROOM) return phase->state != SPECIAL_ST_FINISHED;
    return stage_enemies_targets_remaining(&phase->enemies);
}

uint32_t special_phase_autoplay_buttons(const special_phase *phase,
                                          unsigned frame) {
    if (!phase) return MB_PAD_RIGHT;
    if (phase->kind == STAGE_KIND_FINALE && !phase->movie_ok) {
        static const uint32_t directions[] = {MB_PAD_UP, MB_PAD_DOWN, MB_PAD_RIGHT, MB_PAD_LEFT};
        if (phase->finale_end_tick) return 0;
        if (phase->finale_charged) {
            for (unsigned d = 0; d < 4; ++d) {
                /* From an acquired input: publish next tick, then fly until
                 * age == impact + 2. The accumulator already holds the next phase. */
                int impact_phase = (phase->ring_phase_acc +
                    (int)(phase->finale_blue[d].impact + 2u) * k_finale_phase_incr[phase->ring_active_count]) >> 16;
                if (phase->ring_active[finale_ring_select(impact_phase, d)])
                    return directions[d] | MB_PAD_B;
            }
        }
        return phase->red_bolt.age ? directions[phase->red_bolt.dir] : 0;
    }
    if (phase->mode == SPECIAL_MODE_RHYTHM) return music_autoplay(phase);
    if (phase->kind == STAGE_KIND_BIGBONES) return big_autoplay(phase, frame);
    if (phase->kind == STAGE_KIND_GENROOM) return generator_autoplay(phase);
    return MB_PAD_RIGHT;
}

static void generator_pose(special_phase *p,uint16_t held) {
    if (p->frames<5) return;
    /* InputTranslate gates the firing motion on HitTimeout. BonesBuild
     * samples the global four-tick pose clock independently of button edges. */
    uint32_t idle=big_word(p,big_word(p,0x236458u)+4u);
    uint32_t tracks=p->pose_tracks ? p->pose_tracks : idle;
    if(p->gen_motion_timer) --p->gen_motion_timer;
    if (p->hurt_timer==28) tracks=idle;
    if (!p->hurt_timer && !p->gen_motion_timer)
        tracks=(held&0x100) ? big_word(p,0x2364bc) : idle;
    if(tracks!=p->pose_tracks) {
        p->pose_tracks=tracks;
        p->pose_ticks=(p->frames-1)&3u;
        p->gen_motion_timer=10;
    }
    stage_body_pose pose={{p->gen_x,p->gen_y,0},{0,0,p->gen_rot},{0,0,0},150, p->pose_ticks++,0,tracks};
    (void)stage_body_joints(p->action_rig,&pose,p->collision_joints);
}

static void generator_axis(int *pos,int *vel,int accel,int limit) {
    /* InputTranslate at 250252 deliberately performs both signed tests. */
    if (*vel>0) *vel-=52;
    if (*vel<0) *vel+=52;
    *vel=bounded(*vel+accel,-0xf5c,0xf5c);
    *pos+=*vel;
    if (*pos< -limit || *pos>limit) {
        *vel=-*vel/2;
        *pos=(*pos<0 ? -limit:limit)+*vel;
    }
}

static void generator_fire(special_phase *p) {
    static const int sine[]={0,48,97,143,179,212,235,250,256};
    unsigned j=0;
    while(j<8 && p->zap_ttl[j]) ++j;
    if(j==8) return;
    unsigned dir=((unsigned)(p->zap_angle+15)>>5)&31u;
    unsigned q=dir>>3, k=dir&7u;
    int x=(q&1u)?sine[8u-k]:sine[k], y=(q&1u)?sine[k]:sine[8u-k];
    if(q<2) x=-x;
    if(q==1||q==2) y=-y;
    /* CZap::Brain 23df30 moves one full unit on creation. Fly uses
     * 0x28f5 � the 32-direction coefficients for sixty iterations. */
    p->zap_x[j]=p->zap_origin_x+x*256;
    p->zap_y[j]=p->zap_origin_y+y*256;
    p->zap_vx[j]=0x28f5*x/256;
    p->zap_vy[j]=0x28f5*y/256;
    p->zap_ttl[j]=61;
    p->zap_birth[j]=p->frames;
    ++p->enemies.zaps_fired;
    p->sfx_flags|=RUNWAY_SFX_FLAG_C;
}

static void step_action(special_phase *p,const saturn_pad *pad) {
    if (p->hurt_timer) --p->hurt_timer;
    if (p->live_pan_ok && p->live_pan.count>1)
        (void)pan_scroll_seek(&p->live_pan,(p->frames/2u)%p->live_pan.count);
    if (!(p->frames&1u)) p->gen_sample=pad->previous;
    uint16_t held=p->gen_sample;
    int previous_rotation=p->gen_rot;
    generator_pose(p,held);
    if (held & 0x80) { p->gen_rot_acc=1; p->gen_rot_limit=80; }
    else if (held & 8) { p->gen_rot_acc=-1; p->gen_rot_limit=80; }
    if (p->frames>=4) {
        p->gen_rot=(p->gen_rot+p->gen_rot_vel/4)&1023;
        int spin=p->gen_rot_acc*p->gen_rot_vel;
        if (spin<p->gen_rot_limit) {
            p->gen_rot_vel+=5*p->gen_rot_acc;
            if (p->gen_rot_acc*p->gen_rot_vel>=p->gen_rot_limit) p->gen_rot_limit=0;
        } else if(spin>32) p->gen_rot_vel-=p->gen_rot_acc;
    }
    /* CZap receives the position/angle message on the following actor
     * pass. It moves one full unit on creation, then 0x28f5 per yield. */
    if (p->zap_pending) { generator_fire(p); p->zap_pending=0; }
    unsigned blues=stage_enemies_count_kind(&p->enemies,STAGE_ENEMY_BLUE);
    unsigned interval=blues>=6 ? 4u : 16u-2*blues;
    if(p->zap_clock<=16) ++p->zap_clock;
    if(p->zap_clock>interval && (held&0x100)) {
        p->zap_origin_x=p->gen_x;
        p->zap_origin_y=p->gen_y;
        p->zap_angle=previous_rotation;
        p->zap_pending=1;
        p->zap_clock=0;
    }
    generator_axis(&p->gen_x,&p->gen_vx,183*((held&0x4000?1:0)-(held&0x8000?1:0)),0x50000);
    generator_axis(&p->gen_y,&p->gen_vy,183*((held&0x1000?1:0)-(held&0x2000?1:0)),0x38000);
    p->bones_x=(unsigned)bounded(160-p->gen_x/2048,0,319)*UNITS_PER_PIXEL;
    p->arena_y_px=bounded(112-p->gen_y/2048,0,223);
    p->bones_y=(unsigned)p->arena_y_px*UNITS_PER_PIXEL;
    step_generator_plasma(p);
    if (p->hurt_timer==30) p->gen_rot=previous_rotation;
}

static void step_finale_arena(special_phase *p, const saturn_pad *physical) {
    /* US disc 2: CheckJoy (0x28d20c) saw only odd physical VBlanks
     * relative to arena entry. SMPCgetPad (0x20bf08) derives edges from
     * successive reported levels. The native fixed tick publishes on the
     * following retrace; the original actor can run one retrace later. */
    uint16_t raw = (uint16_t)~p->finale_pad_pending;
    saturn_pad_update(&p->finale_pad, (uint8_t)(raw >> 8), (uint8_t)raw);
    const saturn_pad *pad = &p->finale_pad;
    if (p->frames & 1u) p->finale_pad_pending = physical->held;
    if (p->hurt_timer) --p->hurt_timer;
    /* Two VBlanks from command submission to scanout. The renderer holds
     * the final leaf until the actor's deletion reaches the next list. */
    for (unsigned i = 0; i < 3; ++i) {
        finale_bolt previous = p->finale_display_bolts[1][i];
        finale_bolt current = i ? p->blue_bolts[i-1] : p->red_bolt;
        const finale_bolt_anim *anim = &(i ? p->finale_blue : p->finale_red)[previous.dir];
        if (!current.age && previous.age && previous.age == anim->count)
            current = (finale_bolt){previous.dir, previous.age + 1u};
        p->finale_display_bolts[0][i] = previous;
        p->finale_display_bolts[1][i] = current;
    }
    /* The previous actor pass may finish during the vertical blank of this
     * host retrace. Its angular phase remains latched until CRing runs. */
    for (unsigned i = 0; i < p->finale_pending_count; ++i)
        publish_finale_shot(p, p->finale_pending_shots[i], p->frames - 1u);
    p->finale_pending_count = 0;
    finale_ring_brain(p);
    /* CEndLevel::Brain starts independently. The other actors keep running:
     * on Saturn an airborne bolt can still hit Bones during the palette fade. */
    if (p->finale_end_tick && p->frames - p->finale_end_tick >= 50u + 12u + 1u) {
        if (!p->ring_cleared) {
            p->state = SPECIAL_ST_LOST;
            snprintf(p->status, sizeof p->status, "FINALE lost: %u bolt hits", p->hazard_hits);
        } else if (special_phase_finale_play_ending(p)) {
            p->state = SPECIAL_ST_ABORTED;
            snprintf(p->status, sizeof p->status, "FINALE: failed to open ending movie");
        }
        return;
    }

    /* CheckJoy runs after CFBones::Brain's 60 yields. Releasing all
     * directions selects pose 4; firing from that pose uses south. */
    if (p->frames > 60u) {
        if ((pad->pressed | pad->released) & 0xf000u) {
            p->finale_facing = finale_dir_from_pad(pad);
            finale_body_command(p->finale_body_mesh, p->finale_facing);
            p->finale_recenter = 0;
        } else if (p->finale_recenter && !--p->finale_recenter) {
            p->finale_facing = 4;
            finale_body_command(p->finale_body_mesh, 4);
        }
    }
    finale_sprite sprites[FINALE_SPRITES_MAX] = {0};
    finale_visible_sprites(p, sprites);
    finale_body_tick(p->finale_body_mesh, p->finale_charged, sprites);
    /* Startup effect @29547A and its RandomRange call also share the loader RNG. */
    if (p->frames == 6u) p->finale_rng += 2u * 0x07050301u;
    if (p->frames == p->finale_cycle_tick) {
        p->finale_rng += 0x07050301u;
        p->finale_next_dir = p->finale_random_table[p->finale_rng >> 24] & 3u;
        unsigned rows = p->finale_rows[p->finale_next_dir];
        p->finale_spawn_tick = p->frames + 4u*(rows - 1u);
        p->finale_cycle_tick += 4u*rows + 2u;
        p->finale_lights->first_tick = p->frames;
    }
    step_finale_lights(p);
    for (unsigned i = 0; i < 2; ++i) {
        finale_bolt *bolt = &p->blue_bolts[i];
        if (!bolt->age) continue;
        const finale_bolt_anim *anim = &p->finale_blue[bolt->dir];
        if (++bolt->age == anim->impact + 2u) {
            finale_ring_contact contact = {(uint8_t)bolt->dir, (uint8_t)i};
            /* After ObjProcess starts on line 222 and runs its prologue, the
             * actors have about eight NTSC lines until the line-232 boundary. */
            if (p->finale_lights->fade_cycles >= 8u * 1708u)
                p->finale_pending_shots[p->finale_pending_count++] = contact;
            else
                publish_finale_shot(p, contact, p->frames);
        }

        if (bolt->age > anim->count) bolt->age = 0;
    }
    if (p->finale_charged && (pad->pressed & 0x0100u)) {
        for (unsigned i = 0; i < 2; ++i) {
            if (p->blue_bolts[i].age) continue;
            finale_body_command(p->finale_body_mesh, 6);
            if (p->finale_facing == 4) {
                p->finale_facing = 1;
                p->finale_recenter = 15;
            }
            p->blue_bolts[i] = (finale_bolt){p->finale_facing, 1};
            p->finale_charged = 0;
            break;
        }
    }

    finale_bolt *red = &p->red_bolt;
    if (p->frames == p->finale_spawn_tick) {
        *red = (finale_bolt){p->finale_next_dir, 1};
        ++p->enemies.bolts_fired;
    } else if (red->age) {
        const finale_bolt_anim *anim = &p->finale_red[red->dir];
        if (++red->age == anim->impact + 2u) {
            int blocked = !p->finale_charged && p->finale_facing == red->dir;
            p->finale_hit_type = red->dir + (blocked ? 4u : 0u);
            p->finale_hit_tick = p->frames;
            if (blocked) {
                finale_body_command(p->finale_body_mesh, 5);
                p->finale_charged = 1;
                p->ring_sfx_event = 0x2fu;
            } else {
                p->finale_charged = 0;
                p->ring_sfx_event = 0x30u;
                ++p->hazard_hits;
                p->finale_body_hit_tick = p->frames;
                p->finale_health_before_hit = (unsigned)p->skel.segments;
                p->hurt_timer = 2;
                if (runway_skel_loose(&p->skel, 0x9999) && !p->finale_end_tick)
                    p->finale_end_tick = p->frames;
            }
        }
        if (red->age > anim->count) red->age = 0;
    }
    if (p->finale_facing == 2) p->facing = 1;
    if (p->finale_facing == 3) p->facing = -1;
}

static void step_present(special_phase *phase, const saturn_pad *pad) {
    int advance = 0;
    int interactive_finale = phase->kind == STAGE_KIND_FINALE && !phase->movie_ok;
    if (phase->movie_ok) {
        if (phase->finale_movie_preroll && !phase->movie.playback_done) {
            --phase->finale_movie_preroll;
            if (phase->finale_movie_preroll == FINALE_AUDIO_BEFORE_SCANOUT)
                phase->movie_pcm_pending = phase->movie.pcm_samples != 0;
            /* The first elapsed media interval starts at duckAudioStart. */
            if (phase->finale_movie_preroll >= FINALE_AUDIO_BEFORE_SCANOUT) return;
        }
        advance = duck_avi_tick(&phase->movie,
            phase->movie.tick_external ? phase->movie.tick_us : 16000);
        if (advance < 0) {
            phase->movie_pcm_pending = 0;
            phase->state = SPECIAL_ST_ABORTED;
            snprintf(phase->status, sizeof phase->status, "%s", phase->movie.status);
            return;
        }
    } else if (interactive_finale) {
        step_finale_arena(phase, pad);
        if (phase->state != SPECIAL_ST_PLAY || phase->movie_ok) return;
    } else {
        if (pad->pressed & (0x0800u | 0x0400u | 0x0100u | 0x0200u | 0x8000u))
            advance = 1;
        if ((pad->held & 0x8000u) && (phase->frames % 8u) == 0)
            advance = 1;
    }

    if (advance > 0) {
        phase->page += (unsigned)advance;
        if (!phase->movie_ok) phase->sfx_flags |= RUNWAY_SFX_FLAG_C;
        if (phase->gfx_count)
            phase->gfx_index = phase->page % phase->gfx_count;
    }
    if (!interactive_finale &&
        ((phase->movie_ok && duck_avi_playback_done(&phase->movie)) ||
         (!phase->movie_ok && phase->page >= phase->pages_needed))) {
        if (phase->kind == STAGE_KIND_LOSE) {
            phase->state = SPECIAL_ST_LOST;
            snprintf(phase->status, sizeof phase->status,
                     "%s finished (DUKMOVIE/LOSE) avi=%zu frames=%u duck=%zu",
                     phase->stage_name, phase->avi_size, phase->movie.frames_drawn,
                     phase->duck_size);
        } else {
            phase->state = SPECIAL_ST_FINISHED;
            snprintf(phase->status, sizeof phase->status,
                     "%s finished (present win) pages=%u avi_frames=%u gfx=%u "
                     "rings=%u bolts=%u ring_slots=%d/%u shots=%u",
                     phase->stage_name, phase->page, phase->movie.frames_drawn,
                     phase->gfx_count, phase->enemies.rings_fired,
                     phase->enemies.bolts_fired, phase->ring_active_count,
                     FINALE_RING_SLOTS, phase->ring_shots);
        }
        return;
    }
    snprintf(phase->status, sizeof phase->status,
             "%s present %u/%u duck=%s avi=%zu gfx=%u rings=%u bolts=%u "
             "arena=%d/%u shot=%d",
             phase->stage_name, phase->page, phase->pages_needed,
             phase->movie_ok ? phase->movie.status : "off", phase->avi_size,
             phase->gfx_count, phase->enemies.rings_fired,
             phase->enemies.bolts_fired, phase->ring_active_count,
             FINALE_RING_SLOTS, phase->ring_last_index);
}

int special_phase_step(special_phase *phase, const saturn_pad *pad) {
    if (!phase || !pad || special_phase_done(phase)) return 0;
    phase->frames++;
    phase->sfx_flags = 0;

    if (pad->pressed & 0x0800u) {
        if (phase->mode != SPECIAL_MODE_PRESENT) {
            if (phase->state == SPECIAL_ST_PAUSED) {
                runway_skel_set_paused(&phase->skel, 0);
                phase->state = phase->pre_pause_state;
            } else {
                phase->pre_pause_state = phase->state;
                runway_skel_set_paused(&phase->skel, 1);
                phase->state = SPECIAL_ST_PAUSED;
            }
            return special_phase_render(phase);
        }
    }
    if (phase->state == SPECIAL_ST_PAUSED)
        return special_phase_render(phase);

    runway_skel_tick_cooldown(&phase->skel);

    if (phase->mode == SPECIAL_MODE_PRESENT && phase->movie_ok &&
        phase->finale_movie_preroll <= FINALE_AUDIO_BEFORE_SCANOUT && (pad->held & 0x0800u)) {
        phase->movie.playback_done = 1;
        phase->movie_pcm_pending = 0;
    }

    if (phase->mode == SPECIAL_MODE_RHYTHM)
        step_rhythm(phase, pad);
    else if (phase->kind == STAGE_KIND_BIGBONES)
        step_big(phase,pad);
    else if (phase->mode == SPECIAL_MODE_ACTION)
        step_action(phase, pad);
    else
        step_present(phase, pad);

    return special_phase_render(phase);
}

/* Intensity subtracted by COAR/COAG/COAB during arena scanout. */
static unsigned finale_palette_darkness(const special_phase *phase, unsigned tick) {
    if (phase->finale_end_tick) {
        unsigned age = tick - phase->finale_end_tick;
        return age < 3u ? 0u : age >= 52u ? 255u : 5u * (age - 1u);
    }
    /* US 03CODE 29fa40 calls PalMasterFade(+6) on arena retrace 2.
     * The first changed offset (-244) appears on the main image at 4. */
    if (tick < 4u) return 256u;
    return tick < 45u ? 256u - 6u * (tick - 2u) : 0u;
}

int special_phase_render(special_phase *phase) {
    if (!phase || !phase->frame_rgba || !phase->bg_rgba) return -1;
    if (phase->finale_movie_preroll) {
        return runway_compose_clear(phase->frame_rgba, SCR_W, SCR_H, 0, 0, 0, 255);
    }
    stage_enemies_reset_bones_pose();
    memcpy(phase->frame_rgba, phase->bg_rgba, (size_t)SCR_W * SCR_H * 4u);

    if (phase->mode == SPECIAL_MODE_RHYTHM) {
        unsigned hits_need =
            (phase->rhythm && phase->rhythm->hits_to_win) ? phase->rhythm->hits_to_win
                                                          : SPECIAL_HITS_TO_WIN;
        if (phase->movie_ok && duck_avi_frame_rgba(&phase->movie)) {
            memcpy(phase->frame_rgba, duck_avi_frame_rgba(&phase->movie),
                    (size_t)SCR_W * SCR_H * 4u);
        }
        stage_enemies_draw_rhythm_stage_ex(phase->frame_rgba, SCR_W, SCR_H,
                                           phase->stage_name, phase->frames,
                                           phase->combo, phase->last_hit_grade,
                                           phase->grade_timer, phase->expected_kind,
                                           phase->hits, hits_need);
        paint_beat_lane(phase, phase->frame_rgba, phase->in_window, phase->hits);
        stage_enemies_draw_hud_skelbar(phase->frame_rgba, SCR_W, SCR_H,
                                       phase->skel.segments, RUNWAY_SKEL_SEGMENTS,
                                       214, 6);
    } else if (phase->mode == SPECIAL_MODE_ACTION) {
        int dx, dy, gy = (int)(GROUND_Y / UNITS_PER_PIXEL);
        int use_dome = is_bigbns_stage(phase);
        int is_genrm = (!strcmp(phase->stage_name, "GENRM") ||
                        !strcmp(phase->stage_name, "GENROOM"));
        uint8_t dome_floor[SCR_W];
        unsigned rem = special_phase_targets_remaining(phase);

        if (phase->live_pan_ok && phase->live_pan.planes && phase->live_pan.bg_rgba) {
            size_t plane_base = (size_t)(phase->live_pan.index & 3u) * PAN_PLANE_SIZE;
            size_t p;
            for (p = 0; p < PAN_PLANE_SIZE; p++) {
                if (phase->live_pan.planes[plane_base + p] != 0u) {
                    phase->frame_rgba[4u * p + 0u] = phase->live_pan.bg_rgba[4u * p + 0u];
                    phase->frame_rgba[4u * p + 1u] = phase->live_pan.bg_rgba[4u * p + 1u];
                    phase->frame_rgba[4u * p + 2u] = phase->live_pan.bg_rgba[4u * p + 2u];
                    phase->frame_rgba[4u * p + 3u] = 255u;
                }
            }
        }
        if (phase->movie_ok && duck_avi_frame_rgba(&phase->movie)) {
            memcpy(phase->frame_rgba, duck_avi_frame_rgba(&phase->movie),
                    (size_t)SCR_W * SCR_H * 4u);
        }
        if (use_dome) {
            unsigned x;
            for (x = 0; x < SCR_W; x++)
                dome_floor[x] = bigbns_dome_y_px((int)x);
        } else if (is_genrm && phase->arena_y_px >= 116 && phase->arena_y_px <= 188) {
            gy = (int)phase->arena_y_px;
        }
        if (use_dome) {
            stage_enemies *view = &phase->enemies;
            view->count = 0;
            for (unsigned i=0; i<=phase->big_count && view->count<STAGE_ENEMY_MAX; ++i) {
                int piece = i==phase->big_count;
                int wx = piece ? phase->part_x : phase->big_bumpers[i].x;
                int wy = piece ? phase->part_y : phase->big_bumpers[i].y;
                int sx = 160 - (int)(((int64_t)wx-phase->paddle_x)*42/65536);
                int sy = 32 - wy*42/65536;
                if (sx<0 || sx>=320 || sy<0 || sy>=224) continue;
                stage_enemy *e=&view->ents[view->count++];
                e->active=1;
                e->kind = piece ? STAGE_ENEMY_POOF_BLUE : STAGE_ENEMY_GANG;
                e->x=(unsigned)sx*UNITS_PER_PIXEL; e->y=(unsigned)sy*UNITS_PER_PIXEL;
                e->anim=phase->frames;
            }
        }
        stage_enemies_render_ex(&phase->enemies, NULL, phase->frame_rgba,
                                SCR_W, SCR_H, 0, 0, gy,
                                use_dome ? dome_floor : NULL,
                                use_dome ? SCR_W : 0u, 0);
        dx = (int)(phase->bones_x / UNITS_PER_PIXEL);
        dy = (int)(phase->bones_y / UNITS_PER_PIXEL);
        if (use_dome)
            gy = (int)bigbns_dome_y_px(dx);
        stage_enemies_set_bones_ground_y(gy, phase->stage_name);
        stage_enemies_set_bones_fx((int)phase->hurt_timer,
                                   phase->skel.hit_cooldown,
                                   phase->skel.died);
        {
            unsigned bones_anim;
            if (phase->airborne) {
                int32_t u = (int32_t)UNITS_PER_PIXEL;
                bones_anim = (phase->vel_y < -6 * u) ? 0u
                           : (phase->vel_y < 0)      ? 1u
                           : (phase->vel_y < 6 * u)  ? 2u
                                                     : 3u;
            } else if (phase->tall) {
                bones_anim = (phase->frames / 6u) & 1u;
            } else if (phase->vel_x != 0) {
                bones_anim = ((phase->walk_dist / (5u * UNITS_PER_PIXEL)) % 20u) + 1u;
            } else {
                bones_anim = 0u;
            }
            stage_enemies_draw_bones(phase->frame_rgba, SCR_W, SCR_H, dx, dy,
                                     phase->facing, phase->tall,
                                     phase->airborne, bones_anim);
        }
        stage_enemies_draw_hud_skelbar(phase->frame_rgba, SCR_W, SCR_H,
                                   phase->skel.segments, RUNWAY_SKEL_SEGMENTS,
                                   214, 6);
        if (phase->stage_name[0]) {
            char action_hud[48];
            if (rem > 0)
                snprintf(action_hud, sizeof action_hud, "%s  CLR %u",
                         phase->stage_name, rem);
            else
                snprintf(action_hud, sizeof action_hud, "%s  GO ->",
                         phase->stage_name);
            mb_font_draw_text_shadow(phase->frame_rgba, SCR_W, SCR_H, 8, 8,
                                     action_hud,
                                     rem > 0 ? 255 : 120,
                                     rem > 0 ? 185 : 245,
                                     rem > 0 ? 90 : 140, 16, 12, 8, 1);
        }
    } else {
        /* Present: DUCK AVI frame, else STXT plates. */
        if (phase->movie_ok && duck_avi_frame_rgba(&phase->movie)) {
            memcpy(phase->frame_rgba, duck_avi_frame_rgba(&phase->movie),
                   (size_t)SCR_W * SCR_H * 4u);
        } else if (phase->kind != STAGE_KIND_FINALE && phase->gfx_count) {
            unsigned i = phase->gfx_index % phase->gfx_count;
            runway_compose_blit_sprite(
                phase->frame_rgba, SCR_W, SCR_H, phase->gfx_rgba[i],
                phase->gfx_w[i], phase->gfx_h[i],
                ((int)SCR_W - (int)phase->gfx_w[i]) / 2,
                ((int)SCR_H - (int)phase->gfx_h[i]) / 2);
        }
        if (phase->kind == STAGE_KIND_FINALE && !phase->movie_ok) {
            unsigned hit_age = phase->frames - phase->finale_body_hit_tick;
            unsigned segments = phase->finale_body_hit_tick && hit_age < 3u
                              ? phase->finale_health_before_hit : (unsigned)phase->skel.segments;
            /* The command list draws the duel, then the ring, then the bar.
             * All three sample the same VDP2 backdrop for color calculation. */
            finale_body_draw(phase->finale_body_mesh, phase->frame_rgba,
                             phase->finale_body_hit_tick && hit_age > 1u ? hit_age - 1u : 0, 0);
            paint_finale_rings(phase, phase->frame_rgba);
            finale_body_draw_health(phase->finale_body_mesh, phase->frame_rgba, segments);
        } else if (!phase->movie_ok && phase->stage_name[0]) {
            mb_font_draw_text_shadow(phase->frame_rgba, SCR_W, SCR_H, 16, 10,
                                     phase->stage_name,
                                     235, 225, 180, 16, 12, 8, 1);
        }
        /* Progress bar for pages / movie (non-FINALE present fallback). */
        if (!phase->movie_ok && phase->kind != STAGE_KIND_FINALE) {
            int x, y, fill = (int)((phase->page * 240u) /
                                   (phase->pages_needed ? phase->pages_needed : 1u));
            if (fill > 240) fill = 240;
            for (y = 204; y < 212; y++)
                for (x = 40; x < 40 + fill; x++) {
                    uint8_t *px = phase->frame_rgba + ((size_t)y * SCR_W + (size_t)x) * 4u;
                    px[0] = 200; px[1] = 160; px[2] = 40; px[3] = 255;
                }
        }
    }
    if (phase->kind == STAGE_KIND_FINALE && !phase->movie_ok &&
        (phase->frames < 45u || phase->finale_end_tick)) {
        unsigned offset = finale_palette_darkness(phase, phase->frames);
        /* The display register transfer occurs after line 222. Line 223
         * already uses the next offset, on both the +6 and -5 fades. */
        for (unsigned y = 0; y < SCR_H; ++y) {
            if (y == SCR_H - 1u) offset = finale_palette_darkness(phase, phase->frames + 1u);
            if (!offset) continue;
            uint8_t *row = phase->frame_rgba + 4u * SCR_W * y;
            for (unsigned x = 0; x < SCR_W; ++x)
                for (unsigned c = 0; c < 3; ++c) {
                    uint8_t *value = row + 4u*x + c;
                    *value = *value > offset ? (uint8_t)(*value - offset) : 0;
                }
        }
    }
    runway_compose_hud_pause(phase->frame_rgba, SCR_W, SCR_H, NULL, 0, 0,
                             phase->state == SPECIAL_ST_PAUSED);
    return 0;
}

int special_phase_done(const special_phase *phase) {
    return phase && (phase->state == SPECIAL_ST_FINISHED ||
                     phase->state == SPECIAL_ST_LOST ||
                     phase->state == SPECIAL_ST_ABORTED);
}
