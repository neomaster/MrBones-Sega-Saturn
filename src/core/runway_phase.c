#include "runway_phase.h"
#include "core/bones_state.h"
#include "core/bones_tall.h"
#include "core/mb_font.h"
#include "core/runway_sfx.h"
#include "core/resource.h"
#include "core/runway_assets.h"
#include "core/runway_sprite.h"
#include "core/runway_compose.h"
#include "core/runway_projection.h"
#include "core/runway_input.h"
#include "core/sprite_decode.h"
#include "core/pan_frame.h"
#include "core/pan_palette.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Screen scale: RWorldSprite uses (cam-world)*7>>14 ≡ *28>>16 (same ratio). */
#define UNITS_PER_PIXEL 2341u
#define START_X (40u * UNITS_PER_PIXEL)
#define FINISH_X (280u * UNITS_PER_PIXEL)
/* InputBrain @ 0x0026a614 literals (03CODE base 0x00232000). */
#define WALK_STEP_LEFT 3932u       /* 0x0026a6ac = 0x0F5C */
#define WALK_STEP_LEFT_TALL 1310u  /* 0x0026a6aa */
/* R nontall: InputBrain no-op on 0x2858BC; motion via vel→NearVel (host step below). */
#define WALK_STEP_RIGHT 1966u
#define WALK_STEP_RIGHT_TALL 2621u /* abs(0x0026a6ae = -2621) */
#define MSG_DOWN 0x00010700u
#define MSG_C_A 0x00010600u
#define MSG_C_B 0x00020600u
#define MSG_STAND_BOOT 0x00020A00u
#define MSG_JUMP 0x00020800u /* Stand flag&8 peek → brain switch (rea-runway-session) */
/* Stand peeks 0x11001 / 0x11000 → BonesWalk @0x260370 (rea-runway-session §12e). */
#define MSG_WALK_A 0x00011001u
#define MSG_WALK_B 0x00011000u
#define MAX_STEP_UP 4
#define MAX_STEP_DOWN 4
/* Air: bones_y grows downward (tile row). Jump = negative vel[1]; gravity positive.
 * Impulse scale ~ VelThrust 0x1eb8 family — tuned for playable arc on RUNWAY coll. */
#define JUMP_IMPULSE (-(int32_t)(56u * UNITS_PER_PIXEL))
#define GRAVITY_Y ((int32_t)(3u * UNITS_PER_PIXEL))
/* sfx_flags bits ≡ RUNWAY_SFX_FLAG_* (runway_sfx.h). */
#define SFX_WALK RUNWAY_SFX_FLAG_WALK
#define SFX_TALL RUNWAY_SFX_FLAG_TALL
#define SFX_C RUNWAY_SFX_FLAG_C
#define SFX_JUMP RUNWAY_SFX_FLAG_JUMP
#define SFX_LAND RUNWAY_SFX_FLAG_LAND
#define SOUND_EVENT_DOWN 0xcu /* Stand→SoundEvent(0xc); MAP _SoundEvent@0x204128 */
/* Descriptor banks: morph tumble @0x23204C×13; full body walk @0x253858×20. */
#define BONES_DESC_MORPH UINT32_C(0x0023204c)
#define BONES_DESC_FULL UINT32_C(0x00253858)
#define BONES_DESC_STRIDE UINT32_C(0x38)
#define BONES_MORPH_FRAMES 13u
#define BONES_FULL_FRAMES 20u
/* Hold frame within active bank; walk uses Mplaynext cue duration (default 8). */
#define BONES_HOLD_FRAME 5u
#define START_PX 40u
#define FINISH_PX 280u
/* HitChk config origin is bones center; native bones_y is feet → raise by bot_y. */
#define HITCHK_CENTER_LIFT 12

/*
 * BonesSetConfig @0x25d75c ← bnscfg 0x2873EC +0x50..0x6c.
 * All 8 crouch slots share AABB TR/TL/BR/BL = (±5,−8)/(±5,+12) (verified in 03CODE).
 * Image HitChkScrn* @0x285A6C..88 are zero until this copy runs.
 */
static const uint8_t k_bnscfg_2873ec_aabb[0x70] = {
    /* 0x00..0x4f unused by pan_hit_offsets_from_config */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /* +0x50 TR X/Y, +0x58 TL X/Y, +0x60 BR X/Y, +0x68 BL X/Y (BE32) */
    0x00,0x00,0x00,0x05, 0xFF,0xFF,0xFF,0xF8,
    0xFF,0xFF,0xFF,0xFB, 0xFF,0xFF,0xFF,0xF8,
    0x00,0x00,0x00,0x05, 0x00,0x00,0x00,0x0C,
    0xFF,0xFF,0xFF,0xFB, 0x00,0x00,0x00,0x0C
};

/* BonesSetConfig stand-in: refresh HitChk AABB from crouch bnscfg template. */
static void bones_set_config_hitchk(runway_phase *phase) {
    if (!phase) return;
    if (pan_hit_offsets_from_config(&phase->hit, k_bnscfg_2873ec_aabb,
                                    sizeof k_bnscfg_2873ec_aabb) != 0)
        pan_hit_offsets_default(&phase->hit);
}

/* BrainSwitch_25d534 stand-in: install config slot, BonesSetConfig HitChk, TallSet → *0x28DB8C. */
static void brain_switch(runway_phase *phase, uint32_t slot) {
    phase->brain_slot = slot;
    bones_set_config_hitchk(phase);
    phase->tall = bones_tall_set(slot);
}

typedef struct {
    runway_phase *phase;
} bbs_emit_ctx;

static void bbs_emit(void *ctx, uint32_t id, uint32_t value) {
    bbs_emit_ctx *e = ctx;
    if (!e || !e->phase) return;
    bones_bbs_post(&e->phase->bbs, id, value);
    e->phase->last_message = id;
}

static uint32_t ground_world_y(const pan_collision_map *map, int tx) {
    int gty = pan_collision_map_ground_ty(map, tx, 0);
    if (gty < 0) gty = 17; /* RUNWAY first-frame fallback: solid from ~row 17 */
    /* Sit 1px above the solid hixel row so origin is air; PushOutOfWalls (HitChk=0)
     * otherwise treats the floor as a wall and slides X every frame (§12k). */
    unsigned py = (unsigned)gty * PAN_COLL_PIXELS_PER_TILE;
    if (py > 0) py -= 1;
    return py * UNITS_PER_PIXEL;
}



static int decode_record(const stxtr_record *record, const uint8_t *palette,
                         uint16_t pmod, uint8_t **out, unsigned *w, unsigned *h) {
    size_t size;
    uint8_t *pixels;
    if (!record || !out || !w || !h) return -1;
    size = (size_t)record->width * record->height * 4;
    pixels = malloc(size);
    if (!pixels) return -1;
    if (mb_sprite_decode(record->data, record->size, record->width, record->height, pmod,
                         palette, 1024, pixels, size)) {
        free(pixels);
        return -1;
    }
    
    *out = pixels;
    *w = record->width;
    *h = record->height;
    return 0;
}

static int load_sprite(runway_assets *assets, const uint8_t *palette,
                       uint32_t address, uint8_t **out, unsigned *w, unsigned *h) {
    stxtr_record record;
    uint16_t pmod;
    if (runway_sprite_resolve(assets->code, assets->code_size, address,
                              assets->textures, assets->textures_size, &record, &pmod))
        return -1;
    return decode_record(&record, palette, pmod, out, w, h);
}

/* Soft STXTR walk: try mode4/mode5 pmods. Always advances *offset on a valid header. */
static int load_sprite_stxtr(const void *textures, size_t textures_size,
                             size_t *offset, const uint8_t *palette,
                             uint8_t **out, unsigned *w, unsigned *h) {
    stxtr_record record;
    static const uint16_t k_pmods[] = {0x0020, 0x0028, 0x00a0, 0x00a8, 0};
    unsigned i;
    if (!textures || !offset) return -1;
    if (stxtr_next(textures, textures_size, offset, &record) != 1) return -1;
    for (i = 0; k_pmods[i]; i++) {
        if (decode_record(&record, palette, k_pmods[i], out, w, h) == 0)
            return 0;
    }
    return -1;
}

static int make_placeholder_sprite(uint8_t **out, unsigned *w, unsigned *h) {
    unsigned x, y;
    uint8_t *p = malloc(16u * 32u * 4u);
    if (!p) return -1;
    for (y = 0; y < 32; y++) {
        for (x = 0; x < 16; x++) {
            size_t i = ((size_t)y * 16u + x) * 4u;
            int edge = (x == 0 || y == 0 || x == 15 || y == 31);
            p[i + 0] = edge ? 255 : 240;
            p[i + 1] = edge ? 255 : 220;
            p[i + 2] = edge ? 255 : 80;
            p[i + 3] = 255;
        }
    }
    *out = p;
    *w = 16;
    *h = 32;
    return 0;
}

/* Scan 03CODE for the genuine 1024-color stage palette stream first; fall back to loose scan. */
static int find_and_apply_palette(const uint8_t *code, size_t code_size,
                                  uint32_t *palette_words, uint8_t *palette) {
    size_t off, consumed, best_off = 0, best_cons = 0;
    uint32_t tmp[1024];
    size_t i;
    if (!code || code_size < 16) return -1;
    /* Pass 1: strict Saturn CRAM palette stream (header index < 1024, 0x01BBGGRR tag, >= 2048 B). */
    for (off = 0; off + 2048 <= code_size && off < 0x100000; off += 4) {
        if (code[off] != 0 || code[off + 1] != 0 || code[off + 2] >= 4 || code[off + 4] != 1)
            continue;
        consumed = 0;
        memset(tmp, 0, sizeof tmp);
        if (pan_palette_apply(code + off, code_size - off, tmp, 1024, &consumed))
            continue;
        if (consumed >= 4096 && consumed <= 4120) {
            best_cons = consumed;
            best_off = off;
            memcpy(palette_words, tmp, sizeof tmp);
            break;
        }
        if (consumed >= 2048 && consumed > best_cons) {
            best_cons = consumed;
            best_off = off;
            memcpy(palette_words, tmp, sizeof tmp);
        }
    }
    /* Pass 2: fallback loose scan for synthetic/short buffers. */
    if (best_cons < 64) {
        for (off = 0; off + 16 < code_size && off < 0x100000; off += 4) {
            consumed = 0;
            memset(tmp, 0, sizeof tmp);
            if (pan_palette_apply(code + off, code_size - off, tmp, 1024, &consumed))
                continue;
            if (consumed > best_cons && consumed >= 64) {
                best_cons = consumed;
                best_off = off;
                memcpy(palette_words, tmp, sizeof tmp);
                if (best_cons >= 200) break;
            }
        }
    }
    if (best_cons < 64) {
        /* Grayscale fallback bank. */
        memset(palette_words, 0, 1024 * sizeof *palette_words);
        for (i = 0; i < 256; i++)
            palette_words[i] = (uint32_t)((i << 16) | (i << 8) | i);
    }
    for (i = 0; i < 1024; i++) {
        palette[4 * i + 1] = (uint8_t)(palette_words[i] >> 16);
        palette[4 * i + 2] = (uint8_t)(palette_words[i] >> 8);
        palette[4 * i + 3] = (uint8_t)palette_words[i];
    }
    (void)best_off;
    return 0;
}

static int is_runway_iso(const char *iso_dir) {
    return iso_dir && strstr(iso_dir, "RUNWAY") != NULL;
}

static void stage_name_from_iso(char *out, size_t n, const char *iso_dir) {
    const char *p = strrchr(iso_dir, '/');
    p = p ? p + 1 : iso_dir;
    if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9')
        p += 2;
    snprintf(out, n, "%s", p);
}

static void anim_set_current(runway_phase *phase, unsigned bank, unsigned index) {
    if (!phase || bank >= RUNWAY_ANIM_BANK_COUNT) return;
    phase->anim_bank = bank;
    phase->anim_count = phase->anim_bank_count[bank];
    if (phase->anim_count == 0) return;
    if (index >= phase->anim_count) index = phase->anim_count - 1;
    phase->anim_index = index;
    phase->sprite_rgba = phase->anim_rgba[bank][index];
    phase->sprite_w = phase->anim_w[bank][index];
    phase->sprite_h = phase->anim_h[bank][index];
}

/*
 * §12m frame select: morph bank while walking; full for stand/air/tall.
 * Walk index from Mplaynext/Mnext cue sequencer (stride 0xc, duration 8).
 */
static void anim_update(runway_phase *phase) {
    unsigned bank, idx, n;
    if (!phase) return;
    if (phase->state == RUNWAY_ST_WALK && !phase->tall)
        bank = RUNWAY_ANIM_BANK_MORPH;
    else
        bank = RUNWAY_ANIM_BANK_FULL;
    n = phase->anim_bank_count[bank];
    if (n == 0) {
        bank = RUNWAY_ANIM_BANK_MORPH;
        n = phase->anim_bank_count[bank];
    }
    if (n == 0) return;
    if (phase->mplay.count != n)
        mplay_seq_init(&phase->mplay, n, MPLAY_DEFAULT_DURATION);
    if (phase->state == RUNWAY_ST_WALK) {
        mplay_seq_step(&phase->mplay);
        idx = mplay_seq_frame(&phase->mplay);
    } else if (phase->state == RUNWAY_ST_AIR) {
        idx = BONES_HOLD_FRAME < n ? BONES_HOLD_FRAME : 0;
    } else {
        idx = (phase->anim_bank == bank) ? phase->anim_index : 0;
        if (idx >= n) idx = BONES_HOLD_FRAME < n ? BONES_HOLD_FRAME : 0;
    }
    anim_set_current(phase, bank, idx);
}

int runway_phase_load(runway_phase *phase, cdimage *cd) {
    return runway_phase_load_dir(phase, cd, "/04RUNWAY");
}

int runway_phase_load_dir(runway_phase *phase, cdimage *cd, const char *iso_dir) {
    char pan_path[96];
    runway_assets assets;
    uint32_t palette_words[1024];
    uint8_t palette[4096];
    int runway;
    if (!phase || !cd || !iso_dir) return -1;
    memset(phase, 0, sizeof *phase);
    runway = is_runway_iso(iso_dir);
    phase->generic_pan = !runway;
    snprintf(phase->iso_dir, sizeof phase->iso_dir, "%s", iso_dir);
    stage_name_from_iso(phase->stage_name, sizeof phase->stage_name, iso_dir);
    bones_bbs_init(&phase->bbs);
    phase->facing = 1;
    phase->tall = 0;
    phase->brain_slot = BONES_BRAIN_STAND;
    phase->crouch_index = 0;
    phase->sched_enqueued_0c = 0;
    phase->sound_event_id = 0;
    phase->bones_x = START_X;
    phase->input_cell = START_X;
    phase->obj_flags = 0x80u;
    phase->bones_y = 17u * PAN_COLL_PIXELS_PER_TILE * UNITS_PER_PIXEL;
    phase->camera_x = START_X;
    phase->camera_y = phase->bones_y - 40u * UNITS_PER_PIXEL;
    memset(phase->vel, 0, sizeof phase->vel);
    memset(phase->accel, 0, sizeof phase->accel);
    phase->vel_a = 0;
    phase->vel_b = 0;
    phase->airborne = 0;
    /* BonesSetConfig ← bnscfg 0x2873EC HitChk AABB (same as crouch8). */
    bones_set_config_hitchk(phase);
    phase->audio_track = 2;
    phase->state = RUNWAY_ST_STAND;
    phase->pre_pause_state = RUNWAY_ST_STAND;
    phase->finish_x = runway ? FINISH_X : (START_X + 120u * UNITS_PER_PIXEL);
    runway_skel_init(&phase->skel);
    /* §22: hazards on all 7 15PAN stages (RUNWAY + LOGS/ICE1/ICE2/MOAT/RIDE/STAING). */
    runway_hazards_init_pan_stage(&phase->hazards, phase->stage_name, UNITS_PER_PIXEL);
    /* §24: gang/lord/poofs/tbones on all 15PAN stages. */
    if (runway) stage_enemies_init_runway_rom(&phase->enemies, UNITS_PER_PIXEL); else stage_enemies_init_runway(&phase->enemies, UNITS_PER_PIXEL);
    phase->enemies.stage_name = phase->stage_name;
    stage_enemies_load_disc_sprites(cd);
    bones_bbs_post(&phase->bbs, MSG_STAND_BOOT, 0);

    if (runway_assets_load_dir(cd, iso_dir, &assets)) return -1;
    memset(palette_words, 0, sizeof palette_words);
    memset(palette, 0, sizeof palette);
    (void)find_and_apply_palette((const uint8_t *)assets.code, assets.code_size,
                                 palette_words, palette);

    /* §12m/§16: 15PAN required for pan playable stages (VDP2 CRAM +0x200 = Bank 1). */
    {
        void *frame = NULL;
        size_t frame_size = 0;
        const uint8_t *bank = palette + 1 * 1024;
        snprintf(pan_path, sizeof pan_path, "%s/15PAN", iso_dir);
        if (resource_load(cd, pan_path, &frame, &frame_size) || frame_size == 0 ||
            pan_scroll_init(&phase->pan, (const uint8_t *)frame, frame_size, bank)) {
            free(frame);
            runway_assets_free(&assets);
            return -1;
        }
        phase->pan_blob = (uint8_t *)frame;
        phase->pan_blob_size = frame_size;
        phase->bg_rgba = phase->pan.bg_rgba;
        phase->collision = phase->pan.collision0;
    }
    phase->frame_rgba = malloc(PAN_PLANE_SIZE * 4);
    if (!phase->frame_rgba) {
        runway_assets_free(&assets);
        runway_phase_free(phase);
        return -1;
    }

    /* Anim banks: RUNWAY descriptors first; else STXTR walk; else placeholder. */
    {
        const uint32_t bases[RUNWAY_ANIM_BANK_COUNT] = { BONES_DESC_MORPH, BONES_DESC_FULL };
        const unsigned counts[RUNWAY_ANIM_BANK_COUNT] = { BONES_MORPH_FRAMES, BONES_FULL_FRAMES };
        unsigned b, i;
        int desc_ok = 1;
        for (b = 0; b < RUNWAY_ANIM_BANK_COUNT; b++) {
            phase->anim_bank_count[b] = 0;
            for (i = 0; i < counts[b] && i < RUNWAY_ANIM_MAX_FRAMES; i++) {
                uint32_t addr = bases[b] + i * BONES_DESC_STRIDE;
                const uint8_t *pal_bank = palette + (b == 0 ? 1024 : 2048);
                if (load_sprite(&assets, pal_bank, addr,
                                &phase->anim_rgba[b][i],
                                &phase->anim_w[b][i], &phase->anim_h[b][i])) {
                    desc_ok = 0;
                    break;
                }
                phase->anim_bank_count[b]++;
            }
            if (!desc_ok) break;
        }
        if (!desc_ok || phase->anim_bank_count[0] == 0) {
            size_t off = 0;
            unsigned loaded = 0;
            for (b = 0; b < RUNWAY_ANIM_BANK_COUNT; b++) {
                for (i = 0; i < phase->anim_bank_count[b] && i < RUNWAY_ANIM_MAX_FRAMES; i++)
                    free(phase->anim_rgba[b][i]);
                phase->anim_bank_count[b] = 0;
            }
            for (b = 0; b < RUNWAY_ANIM_BANK_COUNT; b++) {
                unsigned want = counts[b];
                unsigned guard = 0;
                while (phase->anim_bank_count[b] < want &&
                       phase->anim_bank_count[b] < RUNWAY_ANIM_MAX_FRAMES &&
                       guard++ < 64) {
                    unsigned slot = phase->anim_bank_count[b];
                    size_t before = off;
                    if (load_sprite_stxtr(assets.textures, assets.textures_size, &off,
                                          palette, &phase->anim_rgba[b][slot],
                                          &phase->anim_w[b][slot],
                                          &phase->anim_h[b][slot])) {
                        if (off == before) break;
                        continue;
                    }
                    phase->anim_bank_count[b]++;
                    loaded++;
                }
            }
            if (loaded == 0) {
                if (make_placeholder_sprite(&phase->anim_rgba[0][0],
                                            &phase->anim_w[0][0],
                                            &phase->anim_h[0][0])) {
                    runway_assets_free(&assets);
                    runway_phase_free(phase);
                    return -1;
                }
                phase->anim_bank_count[0] = 1;
                if (make_placeholder_sprite(&phase->anim_rgba[1][0],
                                            &phase->anim_w[1][0],
                                            &phase->anim_h[1][0])) {
                    runway_assets_free(&assets);
                    runway_phase_free(phase);
                    return -1;
                }
                phase->anim_bank_count[1] = 1;
            }
        }
    }
    runway_assets_free(&assets);
    anim_set_current(phase,
                     phase->anim_bank_count[RUNWAY_ANIM_BANK_FULL]
                         ? RUNWAY_ANIM_BANK_FULL : RUNWAY_ANIM_BANK_MORPH,
                     0);
    mplay_seq_init(&phase->mplay,
                   phase->anim_bank_count[RUNWAY_ANIM_BANK_MORPH]
                       ? phase->anim_bank_count[RUNWAY_ANIM_BANK_MORPH] : 1u,
                   MPLAY_DEFAULT_DURATION);

    {
        int tx = (int)(phase->bones_x / UNITS_PER_PIXEL) / (int)PAN_COLL_PIXELS_PER_TILE;
        phase->bones_y = ground_world_y(&phase->collision, tx);
        phase->camera_y = phase->bones_y - 40u * UNITS_PER_PIXEL;
    }
    snprintf(phase->status, sizeof phase->status, "%s stand", phase->stage_name);
    return runway_phase_render(phase);
}

void runway_phase_free(runway_phase *phase) {
    unsigned b, i;
    if (!phase) return;
    /* bg_rgba aliases pan.bg_rgba — freed by pan_scroll_free. */
    phase->bg_rgba = NULL;
    pan_scroll_free(&phase->pan);
    free(phase->pan_blob);
    free(phase->frame_rgba);
    for (b = 0; b < RUNWAY_ANIM_BANK_COUNT; b++)
        for (i = 0; i < phase->anim_bank_count[b] && i < RUNWAY_ANIM_MAX_FRAMES; i++)
            free(phase->anim_rgba[b][i]);
    free(phase->hud_health_rgba);
    free(phase->hud_pause_rgba);
    /* sprite_rgba aliases anim_rgba[bank][index] — do not free separately. */
    memset(phase, 0, sizeof *phase);
}

/*
 * §22: record-0 collision is world-baked (absolute px). Later pandcomp records
 * are scrolling viewports — probe bones relative to camera, anchored at START_PX
 * so spawn (bones==camera) hits the same column as absolute START.
 */
static int coll_probe_px(const runway_phase *phase, uint32_t world_x) {
    int wpx = (int)(world_x / UNITS_PER_PIXEL);
    int cpx, spx;
    if (!phase || phase->pan.index == 0) return wpx;
    cpx = (int)(phase->camera_x / UNITS_PER_PIXEL);
    spx = wpx - cpx + (int)START_PX;
    if (spx < 0) spx = 0;
    if (spx >= (int)PAN_WIDTH) spx = (int)PAN_WIDTH - 1;
    return spx;
}

static void world_to_grid_at(const runway_phase *phase, uint32_t bones_x,
                             uint32_t bones_y, int *gx, int *gy) {
    *gx = coll_probe_px(phase, bones_x) - PAN_COLL_X_BIAS;
    *gy = (int)(bones_y / UNITS_PER_PIXEL) - PAN_COLL_Y_BIAS;
}

static void grid_to_world_x_at(const runway_phase *phase, int gx, uint32_t *bones_x) {
    int spx = gx + PAN_COLL_X_BIAS;
    int wpx;
    if (spx < 0) spx = 0;
    if (!phase || phase->pan.index == 0) {
        *bones_x = (uint32_t)spx * UNITS_PER_PIXEL;
        return;
    }
    wpx = spx - (int)START_PX + (int)(phase->camera_x / UNITS_PER_PIXEL);
    if (wpx < 0) wpx = 0;
    *bones_x = (uint32_t)wpx * UNITS_PER_PIXEL;
}

/*
 * Walk blocked on screen bounds, missing ground, or step taller than MAX_STEP_*.
 * Fine wall penetration is resolved after integrate by _BonesPushOutOfWalls (§12k).
 */
static int move_blocked(const runway_phase *phase, uint32_t world_x) {
    int abs_px = (int)(world_x / UNITS_PER_PIXEL);
    int px, tx, dest_gty, foot_ty, climb;
    if (abs_px < 4 || abs_px >= (int)PAN_WIDTH - 4) return 1;
    px = coll_probe_px(phase, world_x);
    tx = px / (int)PAN_COLL_PIXELS_PER_TILE;
    dest_gty = pan_collision_map_ground_ty(&phase->collision, tx, 0);
    /* §16/§22: empty coll cells (some RIDE/ICE/STAING frames) → soft floor on
     * generic_pan; live pandcomp may also briefly lose ground mid-seek. */
    if (dest_gty < 0)
        return phase->generic_pan ? 0 : 1;
    foot_ty = (int)(phase->bones_y / UNITS_PER_PIXEL) /
              (int)PAN_COLL_PIXELS_PER_TILE;
    climb = foot_ty - dest_gty; /* >0 = destination ground is higher */
    if (climb > MAX_STEP_UP) return 1;
    if (-climb > MAX_STEP_DOWN) return 1;
    return 0;
}

/* §12l: _BonesPushOutOfWalls after integrate — HitChk origin = center (feet−12). */
static void apply_push_out(runway_phase *phase) {
    int gx, gy;
    world_to_grid_at(phase, phase->bones_x, phase->bones_y, &gx, &gy);
    gy -= HITCHK_CENTER_LIFT;
    if (pan_bones_push_out_of_walls(&phase->collision, &gx, &gy, &phase->hit))
        grid_to_world_x_at(phase, gx, &phase->bones_x);
}

int runway_phase_step(runway_phase *phase, const saturn_pad *pad) {
    if (!phase || !pad || runway_phase_done(phase)) return 0;
    phase->frames++;
    phase->sfx_flags = 0;
    phase->sound_event_id = 0;
    int was_tall = phase->tall;
    runway_phase_state was_state = phase->state;

    /*
     * §12o PauseBrain_CSkelBar@2685dc: Start toggles bPaused.
     * Win is EvalSuccess→EndLevel(0) at goal X (not Start).
     */
    if (pad->pressed & 0x0800u) {
        if (phase->state == RUNWAY_ST_PAUSED) {
            runway_skel_set_paused(&phase->skel, 0);
            phase->state = phase->pre_pause_state;
            snprintf(phase->status, sizeof phase->status, "RUNWAY resumed");
            return runway_phase_render(phase);
        }
        phase->pre_pause_state = phase->state;
        runway_skel_set_paused(&phase->skel, 1);
        phase->state = RUNWAY_ST_PAUSED;
        snprintf(phase->status, sizeof phase->status,
                 "RUNWAY paused skel=%d/%d seg=%d",
                 phase->skel.skeletism, RUNWAY_SKEL_MAX, phase->skel.segments);
        return runway_phase_render(phase);
    }
    if (phase->state == RUNWAY_ST_PAUSED)
        return runway_phase_render(phase);

    runway_skel_tick_cooldown(&phase->skel);

    /*
     * InputBrain (0x0026a614): posts Down/C. Position side-effects go to a scratch
     * cell — world X is driven by vel→NearVel below (R nontall is no-op in InputBrain).
     * §12m tall: MSG_DOWN → 0x204128(0xc) + BrainSwitch_25d534 → TallSet_26D784.
     */
    {
        uint8_t input0 = 0, input4 = 0;
        if (pad->held & 0x4000) input0 |= 0x40; /* L */
        if (pad->held & 0x8000) input0 |= 0x80; /* R */
        if (pad->held & 0x2000) input0 |= 0x20; /* Down */
        if (pad->pressed & 0x0200) input4 |= 0x02; /* C edge */
        bbs_emit_ctx emit_ctx = { phase };
        uint32_t input_scratch = phase->bones_x;
        runway_input_step(&input_scratch, (uint32_t)phase->tall, input0, input4,
                          bbs_emit, &emit_ctx);
    }

    if (bones_bbs_consume(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_STAND_BOOT, NULL) == 1) {
        phase->obj_flags |= 0x01u;
        phase->last_message = MSG_STAND_BOOT;
    }
    phase->sched_enqueued_0c = 0;
    if (bones_bbs_peek(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_DOWN, NULL) == 1) {
        /* Stand flag&2 path: _SoundEvent(0xc) @0x204128 then BrainSwitch_25d534. */
        phase->sched_enqueued_0c = 1; /* legacy name: SoundEvent index 0xc fired */
        phase->sound_event_id = SOUND_EVENT_DOWN;
        phase->obj_flags |= 0x20u;
        phase->last_message = MSG_DOWN;
        if (!bones_is_crouch_slot(phase->brain_slot)) {
            phase->crouch_index = 0;
            brain_switch(phase, bones_crouch_slot_at(0));
        }
        bones_bbs_consume(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_DOWN, NULL);
    } else if (pad->held & 0x2000) {
        /* TallBrain stand-in: stay on crouch set while Down held. */
        if (!bones_is_crouch_slot(phase->brain_slot))
            brain_switch(phase, bones_crouch_slot_at(phase->crouch_index));
        else
            phase->tall = bones_tall_set(phase->brain_slot);
    } else {
        /* Release → leave crouch set; TallSet clears *0x28DB8C. */
        if (bones_is_crouch_slot(phase->brain_slot))
            brain_switch(phase, BONES_BRAIN_STAND);
        else
            phase->tall = bones_tall_set(phase->brain_slot);
    }
    if (bones_bbs_peek(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_C_A, NULL) == 1) {
        phase->last_message = MSG_C_A;
        bones_bbs_consume(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_C_A, NULL);
    }
    if (bones_bbs_peek(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_C_B, NULL) == 1) {
        phase->last_message = MSG_C_B;
        phase->sfx_flags |= SFX_C;
        bones_bbs_consume(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_C_B, NULL);
    }
    if (!was_tall && phase->tall)
        phase->sfx_flags |= SFX_TALL;

    /*
     * §12h/§12i NearVel + air:
     *  - pad L/R → vel[0] / vel_a / vel_b (R nontall via vel, not InputBrain)
     *  - B edge → MSG_JUMP 0x20800 + upward vel[1] (Stand flag&8 path)
     *  - NearVel_25B1C8: vel+=accel then world @0x2858BC += vel (collision-gated)
     *  - grounded walk snaps Y to ground column; HitChk push-out after integrate
     */
    {
        uint32_t held = pad->held;
        uint32_t step_r = phase->tall ? WALK_STEP_RIGHT_TALL : WALK_STEP_RIGHT;
        uint32_t step_l = phase->tall ? WALK_STEP_LEFT_TALL : WALK_STEP_LEFT;

        /* Horizontal thrust each frame; keep vertical vel while airborne. */
        phase->vel[0] = 0;
        phase->accel[0] = 0;
        phase->vel_a = 0;
        phase->vel_b = 0;
        if (held & 0x8000) {
            phase->facing = 1;
            phase->vel[0] = (int32_t)step_r;
            phase->vel_b = (int32_t)step_r;
            phase->vel_a = (int32_t)step_r;
        } else if (held & 0x4000) {
            phase->facing = -1;
            phase->vel[0] = -(int32_t)step_l;
            phase->vel_b = (int32_t)step_l;
            phase->vel_a = (int32_t)step_l;
        }

        /* Jump: Saturn B (host X) — Stand peeks 0x20800 when obj flag & 8.
         * §12l: skip if _BonesWallAboveCheck (HitChk center origin). */
        if ((pad->pressed & 0x0100u) && !phase->airborne) {
            int gx, gy;
            world_to_grid_at(phase, phase->bones_x, phase->bones_y, &gx, &gy);
            gy -= HITCHK_CENTER_LIFT;
            if (!pan_bones_wall_above(&phase->collision, gx, gy, &phase->hit)) {
                bones_bbs_post(&phase->bbs, MSG_JUMP, 1);
                phase->last_message = MSG_JUMP;
                phase->obj_flags |= 0x08u;
                phase->vel[1] = JUMP_IMPULSE;
                phase->accel[1] = GRAVITY_Y;
                phase->airborne = 1;
                phase->state = RUNWAY_ST_AIR;
                phase->sfx_flags |= SFX_JUMP;
            }
        }

        if (phase->airborne) {
            phase->accel[1] = GRAVITY_Y;
            phase->accel[2] = 0;
            /* NearVel integrate first (vel+=accel); apply gated by coll. */
            bones_near_vel_integrate(phase->vel, phase->accel);

            int64_t next_x64 = (int64_t)phase->bones_x + (int64_t)phase->vel[0];
            if (next_x64 < 0) next_x64 = 0;
            uint32_t next_x = (uint32_t)next_x64;
            if (!move_blocked(phase, next_x))
                phase->bones_x = next_x;
            else
                phase->vel[0] = 0;

            int64_t next_y64 = (int64_t)phase->bones_y + (int64_t)phase->vel[1];
            if (next_y64 < 0) {
                next_y64 = 0;
                phase->vel[1] = 0;
            }
            int tx = coll_probe_px(phase, phase->bones_x) /
                     (int)PAN_COLL_PIXELS_PER_TILE;
            uint32_t ground = ground_world_y(&phase->collision, tx);
            if (phase->vel[1] >= 0 && (uint32_t)next_y64 >= ground) {
                phase->bones_y = ground;
                phase->vel[1] = 0;
                phase->accel[1] = 0;
                phase->airborne = 0;
                phase->sfx_flags |= SFX_LAND;
                phase->state = (phase->vel[0] != 0) ? RUNWAY_ST_WALK : RUNWAY_ST_STAND;
            } else {
                phase->bones_y = (uint32_t)next_y64;
                phase->state = RUNWAY_ST_AIR;
            }
        } else {
            phase->vel[1] = 0;
            phase->accel[1] = 0;
            bones_near_vel_integrate(phase->vel, phase->accel);

            if (phase->vel[0] != 0) {
                int64_t next64 = (int64_t)phase->bones_x + (int64_t)phase->vel[0];
                if (next64 < 0) next64 = 0;
                uint32_t next_x = (uint32_t)next64;
                if (!move_blocked(phase, next_x)) {
                    phase->bones_x = next_x;
                    int tx = coll_probe_px(phase, phase->bones_x) /
                             (int)PAN_COLL_PIXELS_PER_TILE;
                    phase->bones_y = ground_world_y(&phase->collision, tx);
                    phase->state = RUNWAY_ST_WALK;
                    phase->obj_flags |= 0x02u;
                } else {
                    bones_kill_vel_accel_vec(phase->vel, phase->accel);
                    phase->vel_a = phase->vel_b = 0;
                    phase->state = RUNWAY_ST_STAND;
                }
            } else {
                phase->state = RUNWAY_ST_STAND;
            }
        }

        if (bones_bbs_consume(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_JUMP, NULL) == 1)
            phase->last_message = MSG_JUMP;

        /* Stand → BonesWalk message edge (peek 0x11001 / 0x11000 in Stand_Brain). */
        if (was_state == RUNWAY_ST_STAND && phase->state == RUNWAY_ST_WALK) {
            bones_bbs_post(&phase->bbs, MSG_WALK_A, 1);
            bones_bbs_post(&phase->bbs, MSG_WALK_B, 1);
            phase->sfx_flags |= SFX_WALK;
        }
        if (bones_bbs_peek(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_WALK_A, NULL) == 1) {
            phase->last_message = MSG_WALK_A;
            bones_bbs_consume(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_WALK_A, NULL);
        }
        if (bones_bbs_peek(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_WALK_B, NULL) == 1)
            bones_bbs_consume(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_WALK_B, NULL);

        apply_push_out(phase);
        phase->input_cell = phase->bones_x; /* ≡ world X @0x2858BC after NearVel */
    }

    /* Camera follows bones with light lag. */
    int32_t dx = (int32_t)(phase->bones_x - phase->camera_x) / 8;
    phase->camera_x += (uint32_t)dx;
    int32_t dy = (int32_t)(phase->bones_y - phase->camera_y) / 8;
    phase->camera_y += (uint32_t)dy;

    /* §12m/§22: CPanScroll / PlayPanAnim — seek 15PAN visuals + live collision
     * from current pandcomp record during camera advance. Record-0 stays
     * world-absolute; later frames are viewport-probed via coll_probe_px. */
    if (phase->pan.count > 0) {
        unsigned cam_px = phase->camera_x / UNITS_PER_PIXEL;
        unsigned bones_px = phase->bones_x / UNITS_PER_PIXEL;
        unsigned prev_idx = phase->pan.index;
        if (pan_scroll_update(&phase->pan, cam_px, bones_px, START_PX, FINISH_PX) == 0) {
            phase->bg_rgba = phase->pan.bg_rgba;
            phase->collision = phase->pan.collision;
            if (!phase->airborne && phase->pan.index != prev_idx) {
                int tx = coll_probe_px(phase, phase->bones_x) /
                         (int)PAN_COLL_PIXELS_PER_TILE;
                phase->bones_y = ground_world_y(&phase->collision, tx);
            }
        }
    }

    anim_update(phase);

    /* §24: Gang/Lord/Poofs patrol + throw counters. Throws are tracked on the
     * enemy brain (throws_spawned); zone pads stay the sole proximity damage
     * so mbrunway/--check walk is not stacked with CThrown. */
    if (phase->enemies.count > 0)
        (void)stage_enemies_step(&phase->enemies, &phase->skel, phase->bones_x,
                                 0, 0);

    /* §12o/§22 hazards on all pan stages → BonesLoose → EndLevel(1). */
    if (phase->hazards.zone_count > 0 &&
        (runway_hazards_step(&phase->hazards, &phase->skel, phase->bones_x) ||
         phase->skel.died)) {
        phase->state = RUNWAY_ST_LOST;
        snprintf(phase->status, sizeof phase->status,
                 "%s lost (EndLevel fail) skel=0 hits=%u",
                 phase->stage_name, phase->hazards.hits_applied);
        return runway_phase_render(phase);
    }
    if (phase->skel.died) {
        phase->state = RUNWAY_ST_LOST;
        snprintf(phase->status, sizeof phase->status, "%s lost (skel)", phase->stage_name);
        return runway_phase_render(phase);
    }

    /* Fall off map ≈ CheckFallHole_CLevelManager host stand-in. */
    if ((int)(phase->bones_y / UNITS_PER_PIXEL) > (int)PAN_HEIGHT + 32) {
        phase->skel.died = 1;
        phase->state = RUNWAY_ST_LOST;
        snprintf(phase->status, sizeof phase->status, "%s lost (fall hole)",
                 phase->stage_name);
        return runway_phase_render(phase);
    }

    if (phase->bones_x >= phase->finish_x) {
        /* EvalSuccess_CLevelManager@26d574 → EndLevel@2563d4(0). */
        phase->state = RUNWAY_ST_FINISHED;
        snprintf(phase->status, sizeof phase->status,
                 "%s finished (EndLevel win) skel=%d seg=%d hits=%u enemies=%u "
                 "throws=%u",
                 phase->stage_name, phase->skel.skeletism, phase->skel.segments,
                 phase->hazards.hits_applied, phase->enemies.count,
                 phase->enemies.throws_spawned);
    } else {
        const char *st =
            phase->state == RUNWAY_ST_AIR ? "air" :
            phase->state == RUNWAY_ST_WALK ? "walk" : "stand";
        snprintf(phase->status, sizeof phase->status,
                 "%s %s x=%u y=%u tall=%d skel=%d/%d seg=%d hits=%u enemies=%u "
                 "msg=%08X",
                 phase->stage_name, st, phase->bones_x / UNITS_PER_PIXEL,
                 phase->bones_y / UNITS_PER_PIXEL, phase->tall,
                 phase->skel.skeletism, RUNWAY_SKEL_MAX, phase->skel.segments,
                 phase->hazards.hits_applied, phase->enemies.count,
                 phase->last_message);
    }
    return runway_phase_render(phase);
}

int runway_phase_render(runway_phase *phase) {
    int foot_x, foot_y;
    int32_t sx, sy;
    if (!phase || !phase->bg_rgba || !phase->frame_rgba || !phase->sprite_rgba) return -1;
    if (runway_compose_blit_opaque(phase->frame_rgba, PAN_WIDTH, PAN_HEIGHT,
                                   phase->bg_rgba, PAN_WIDTH, PAN_HEIGHT, 0, 0))
        return -1;
    runway_project_center(phase->camera_x, phase->camera_y,
                          phase->bones_x, phase->bones_y, &sx, &sy);
    foot_x = (int)(PAN_WIDTH / 2) - (int)sx;
    if (foot_x < 16) foot_x = 16;
    if (foot_x > (int)PAN_WIDTH - 16) foot_x = (int)PAN_WIDTH - 16;
    foot_y = (int)(phase->bones_y / UNITS_PER_PIXEL);
    if (foot_y < 48) foot_y = 135;
    if (foot_y > (int)PAN_HEIGHT - 8) foot_y = (int)PAN_HEIGHT - 8;

    stage_enemies_render_ex(&phase->enemies, &phase->hazards,
                         phase->frame_rgba, PAN_WIDTH, PAN_HEIGHT,
                         phase->camera_x / UNITS_PER_PIXEL,
                         (int)(PAN_WIDTH / 2),
                         foot_y, NULL, 0u, 1);
    stage_enemies_draw_bones_ex(phase->frame_rgba, PAN_WIDTH, PAN_HEIGHT,
                             foot_x, foot_y, phase->facing, phase->tall,
                             phase->airborne,
                             (phase->state == RUNWAY_ST_WALK)
                                 ? (phase->anim_index + 1u)
                                 : 0u, (unsigned)(foot_y < 116 ? 176 : (foot_y > 164 ? 272 : 224 + (foot_y - 140) * 2)), 256u);
    /* §16 HUD: 14-seg skelbar + stage label; PAUSE.O when paused. */
    if (phase->hud_health_rgba)
        runway_compose_blit_sprite(phase->frame_rgba, PAN_WIDTH, PAN_HEIGHT,
                                   phase->hud_health_rgba, phase->hud_health_w,
                                   phase->hud_health_h, 8, 8);
    stage_enemies_draw_hud_skelbar(phase->frame_rgba, PAN_WIDTH, PAN_HEIGHT,
                               phase->skel.segments, RUNWAY_SKEL_SEGMENTS, 8, 8);
    if (phase->stage_name[0])
        mb_font_draw_text_shadow(phase->frame_rgba, PAN_WIDTH, PAN_HEIGHT,
                                 200, 8, phase->stage_name,
                                 245, 225, 130, 16, 12, 8, 1);
    runway_compose_hud_pause(phase->frame_rgba, PAN_WIDTH, PAN_HEIGHT,
                             phase->hud_pause_rgba, phase->hud_pause_w,
                             phase->hud_pause_h, phase->state == RUNWAY_ST_PAUSED);
    return 0;
}

int runway_phase_done(const runway_phase *phase) {
    return phase && (phase->state == RUNWAY_ST_FINISHED ||
                     phase->state == RUNWAY_ST_ABORTED ||
                     phase->state == RUNWAY_ST_LOST);
}
