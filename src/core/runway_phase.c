#include "runway_phase.h"
#include "core/bones_state.h"
#include "core/sidescrl_physics.h"
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
#include "core/scrl_map.h"
#include "core/finale_body.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Screen scale: RWorldSprite uses (cam-world)*7>>14 ≡ *28>>16 (same ratio). */
#define UNITS_PER_PIXEL 2341u
#define START_X (40u * UNITS_PER_PIXEL)
#define FINISH_X (280u * UNITS_PER_PIXEL)
#define WALK_STEP_LEFT 5400u
#define WALK_STEP_LEFT_TALL 2800u
#define WALK_STEP_RIGHT 5400u
#define WALK_STEP_RIGHT_TALL 2800u
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
/* sfx_flags bits ≡ RUNWAY_SFX_FLAG_* (runway_sfx.h). */
#define SFX_WALK RUNWAY_SFX_FLAG_WALK
#define SFX_TALL RUNWAY_SFX_FLAG_TALL
#define SFX_C RUNWAY_SFX_FLAG_C
#define SFX_JUMP RUNWAY_SFX_FLAG_JUMP
#define SFX_LAND RUNWAY_SFX_FLAG_LAND
#define SOUND_EVENT_DOWN 0xcu /* Stand→SoundEvent(0xc); MAP _SoundEvent@0x204128 */

static void phase_sound_event(runway_phase *phase, unsigned id) {
    phase->sound_event_id = id;
    runway_sfx_emit(id);
}

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

/* BrainSwitch_25d534 stand-in: install config slot, BonesSetConfig HitChk, TallSet → *0x28DB8C. */
static void brain_switch(runway_phase *phase, uint32_t slot) {
    phase->brain_slot = slot;
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

static int screen_x_at(const runway_phase *phase, uint32_t world_x) {
    int32_t sx = 0, sy = 0;
    if (!phase) return (int)(PAN_WIDTH / 2);
    runway_project_center(phase->camera_x, phase->camera_y,
                          world_x, phase->bones_y, &sx, &sy);
    return (int)(PAN_WIDTH / 2) - (int)sx;
}

static int viewport_px(const runway_phase *phase, uint32_t world_x) {
    int px = screen_x_at(phase, world_x);
    if (px < 0) px = 0;
    if (px >= (int)PAN_WIDTH) px = (int)PAN_WIDTH - 1;
    return px;
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

static int pan_has_collision(const runway_phase *phase) {
    return phase->pan.count && phase->collision.size == PAN_COLL_SIZE;
}

static int pan_floor_at(const runway_phase *phase, int x) {
    int y = (int)(phase->bones_y / UNITS_PER_PIXEL);
    if (y >= (int)PAN_HEIGHT) return -1;
    return pan_collision_floor(&phase->collision, x, y);
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

static int overlay_word(const runway_assets *assets, uint32_t va, uint32_t *value) {
    const uint32_t base = 0x232000;
    if (va < base || va - base > assets->code_size || assets->code_size - (va-base) < 4) return -1;
    const uint8_t *p = (const uint8_t *)assets->code + (va-base);
    *value = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
    return 0;
}

/* _TriggerList is bounded by the following link symbol, _TypeThrow. */
static int load_runway_events(runway_phase *p, const runway_assets *assets) {
    unsigned previous = 0;
    for (uint32_t va = 0x28dba0; va < 0x28dc50; va += 8) {
        uint32_t frame, mask;
        if (overlay_word(assets, va, &frame) || overlay_word(assets, va+4, &mask)) return -1;
        if (!frame) return !mask && p->runway_win_record && p->gang_start_tick ? 0 : -1;
        if (frame < previous || frame >= p->pan.count) return -1;
        previous = frame;
        p->pan_events[p->pan_event_count].frame = frame;
        p->pan_events[p->pan_event_count++].mask = mask;
        if (mask & 4u) {
            if (p->butterfly_count >= sizeof p->butterfly_start / sizeof p->butterfly_start[0]) return -1;
            p->butterfly_start[p->butterfly_count++] = 4*frame + 1;
        }
        if (mask & 0x800u) p->runway_win_record = frame;
        /* CLord::Gang yields 30 times before creating slots 0..3. */
        if (mask & 0x40u) p->gang_start_tick = 4*frame + 30;
    }
    return -1;
}

static int load_course_events(runway_phase *p, const runway_assets *assets, uint32_t va, unsigned n) {
    unsigned previous = 0;
    for (unsigned i = 0; i <= n; ++i) {
        uint32_t at, mask;
        if (overlay_word(assets, va+8*i, &at) || overlay_word(assets, va+8*i+4, &mask))
            return -1;
        if (i == n) return (at == 0 || at == 0xffffu) && mask == 0 ? 0 : -1;
        if (at < previous || at >= p->pan.count) return -1;
        p->pan_events[i].frame = previous = at;
        p->pan_events[i].mask = mask;
        ++p->pan_event_count;
    }
    return -1;
}

/* CSparky::ManageSounds reads {PAN record, SoundEvent} lists selected by
 * the attack, growl, breakout, huff and shake animation functions. */
static int load_moat_sound_events(runway_phase *p, const runway_assets *assets) {
    static const uint32_t lists[] = {
        0x2dd100, 0x2dd150, 0x2dd180, 0x2dd1e0, 0x2dd258, 0x2dd1f0
    };
    for (unsigned list = 0; list < sizeof lists / sizeof lists[0]; ++list) {
        unsigned previous = 0;
        for (uint32_t va = lists[list]; ; va += 8) {
            uint32_t frame, id;
            if (overlay_word(assets, va, &frame) || overlay_word(assets, va+4, &id)) return -1;
            if (!id) {
                if (frame) return -1;
                break;
            }
            if (frame <= previous || frame > UINT16_MAX || id >= 64 ||
                p->pan_event_count >= sizeof p->pan_events / sizeof p->pan_events[0]) return -1;
            previous = frame;
            p->pan_events[p->pan_event_count].frame = (list << 16) | frame;
            p->pan_events[p->pan_event_count++].mask = id;
        }
    }
    /* The initial PAN has no active CSparky sound-list cursor. */
    p->pan_event_index = p->pan_event_count;
    return 0;
}

static int load_moat_layout(runway_phase *p, const runway_assets *assets) {
    unsigned nuts = 0, targets = 0, sparky = 0;
    for (uint32_t va = 0x2dcb48; va;) {
        uint32_t next, ctor, x, y, side;
        if (overlay_word(assets, va, &next) || overlay_word(assets, va+4, &ctor) ||
            overlay_word(assets, va+12, &x) || overlay_word(assets, va+16, &y) ||
            (next && (next < va+28 || next > 0x2dd0e0))) return -1;
        if (ctor == 0x2b0b70) ++sparky;
        if (ctor == 0x2af7c4 || ctor == 0x2b1250) {
            if (overlay_word(assets, va+28, &side)) return -1;
            /* Both constructors translate layout coordinates by (5, 3.5). */
            x += 0x50000u;
            y += 0x38000u;
            if (ctor == 0x2af7c4) {
                if (side > 1 || (nuts & (1u << side))) return -1;
                nuts |= 1u << side;
                p->nuts[side].x = (int32_t)x;
                p->nuts[side].y = (int32_t)y;
                p->nuts[side].side = side;
                p->nuts[side].turns = 6;
            } else {
                if (targets == 9) return -1;
                p->sparky_targets[targets].x = (int32_t)x;
                p->sparky_targets[targets].y = (int32_t)y;
                p->sparky_targets[targets++].id = side;
            }
        }
        va = next;
    }
    for (unsigned i = 0; i < 15; ++i) {
        if (overlay_word(assets, 0x2de62c + i*4, &p->moat_sectors[i]) ||
            (uint64_t)p->moat_sectors[i]*2048 >= p->pan_blob_size ||
            (i && p->moat_sectors[i] <= p->moat_sectors[i-1])) return -1;
    }
    return nuts == 3 && targets == 9 && sparky == 1 ? 0 : -1;
}

static int load_pan_course(runway_phase *p, runway_assets *a) {
    if (!strcmp(p->stage_name, "LOGS")) {
        uint32_t x, y;
        if (overlay_word(a,0x2dbb4c,&x) || overlay_word(a,0x2dbb50,&y)) return -1;
        p->course_x = (int32_t)x;
        p->course_y = (int32_t)y;
        const uint32_t tracks[3]={0x2dc5b8,0x2dc300,0x2dc048};
        const unsigned lengths[3]={27,29,29};
        for(unsigned k=0;k<3;++k) for(unsigned n=0;n<lengths[k];++n) {
            uint32_t row=tracks[k]+24*n, tx,ty,ticks,action,target;
            if (overlay_word(a,row,&tx) || overlay_word(a,row+4,&ty) || overlay_word(a,row+12,&ticks) ||
                overlay_word(a,row+16,&action) || overlay_word(a,row+20,&target) ||
                !ticks || ticks>100 || (n+1<lengths[k] ? action!=1 :
                    action!=0x10000000 && (action!=2 || target>=n))) return -1;
            p->log_tracks[k][n].x=(int32_t)tx; p->log_tracks[k][n].y=(int32_t)ty;
            p->log_tracks[k][n].ticks=ticks; p->log_tracks[k][n].action=action; p->log_tracks[k][n].target=target;
        }
        p->course = COURSE_LOGS;
        return load_course_events(p, a, 0x2dbb80, 152);
    }
    if (!strcmp(p->stage_name, "STAING")) {
        for (unsigned i=0; i<11; ++i) {
            uint32_t x,y;
            if (overlay_word(a,0x27dd04u+12*i,&x) || overlay_word(a,0x27dd08u+12*i,&y)) return -1;
            p->claw_aim[i].x=(int32_t)x;
            p->claw_aim[i].y=(int32_t)y;
        }
        p->claws.claw_y=p->claw_aim[0].y;
        p->claw_body=stage_body_load_joints(a->code,a->code_size,0x27d59c,0x246978,0x24699c);
        if (!p->claw_body || overlay_word(a,0x239670,&p->claw_tracks[0]) ||
            overlay_word(a,0x239680,&p->claw_tracks[1]) || overlay_word(a,0x239688,&p->claw_tracks[2])) return -1;
        p->course = COURSE_STAING;
        return load_course_events(p, a, 0x27d8d4, 133);
    }
    if (!strcmp(p->stage_name, "MOAT")) {
        uint32_t config,screen,world;
        if (overlay_word(a,0x2e7058,&config) || overlay_word(a,config+0x4c,&screen) ||
            overlay_word(a,config+0x44,&world)) return -1;
        p->moat_floor_screen=(int)screen;
        p->moat_floor_world=(int32_t)world;
        p->airborne=1;
        p->sparky_target=5;
        p->course = COURSE_MOAT;
        return load_moat_layout(p, a) || load_moat_sound_events(p, a) ? -1 : 0;
    }
    if (!strcmp(p->stage_name, "RIDE")) {
        p->course = COURSE_RIDE;
        if (ride_current_init(&p->current, a->code, a->code_size)) return -1;
        p->swim_sprite = calloc(PAN_PLANE_SIZE, 4);
        if (!p->swim_sprite) return -1;
        for(unsigned i=0;i<16;++i) for(unsigned j=0;j<10;++j)
            if(overlay_word(a,0x271d0c+40*i+4*j,&p->swim_order[i][j])) return -1;
        for(unsigned i=0;i<5;++i)
            if(overlay_word(a,0x267830+4*i,&p->swim_burst_frame[i])) return -1;
        p->swim_config=p->swim_order[0][0];
        p->swim_body=stage_body_load_joints(a->code,a->code_size,0x27203c,0x245ea0,0x245ec4);
        if(!p->swim_body)return -1;
        p->swim_pitch = 1024;
        p->swim_yaw = 512;
    }
    return 0;
}

/* CLevelManager::CheckFallHole uses the pair table between _TableYFall and _ColTable. */
static int load_fall_profile(runway_phase *p, const runway_assets *assets) {
    uint32_t initial;
    if (overlay_word(assets, 0x26d3b0, &initial)) return -1;
    p->fall_height = (int32_t)initial;
    for (unsigned i = 0; i < sizeof p->fall_profile / sizeof p->fall_profile[0]; i++) {
        uint32_t x, y;
        if (overlay_word(assets, 0x28dd54 + 8*i, &x) ||
            overlay_word(assets, 0x28dd58 + 8*i, &y)) return -1;
        p->fall_profile[i].x = (int32_t)x;
        p->fall_profile[i].y = (int32_t)y;
        if (i && p->fall_profile[i].x >= p->fall_profile[i-1].x) return -1;
    }
    return 0;
}

static void step_fall_profile(runway_phase *p) {
    if (p->fall_wait) {
        if (p->fall_wait == 11) {
            p->vel[1] = -0x4ccc;
            p->accel[1] = SIDESCRL_PHYS_GRAVITY_ACCEL;
            p->airborne = 1;
            p->state = RUNWAY_ST_AIR;
        }
        p->fall_wait--;
        return;
    }
    int64_t x = (int64_t)p->camera_x - p->bones_x - (int64_t)p->pan.index*0x2c54 - 0x5b6db;
    if (p->fall_index < sizeof p->fall_profile / sizeof p->fall_profile[0] &&
        x < p->fall_profile[p->fall_index].x) {
        p->fall_height = p->fall_profile[p->fall_index++].y;
    }
    /* Native feet are absolute screen pixels; Saturn BonesPos is relative to the centre. */
    int64_t y = ((int)PAN_HEIGHT/2 + p->hit.bot_right_y) * (int64_t)UNITS_PER_PIXEL - p->bones_y;
    if (y < p->fall_height) {
        runway_skel_loose(&p->skel, 0x6400);
        p->hazards.hits_applied++;
        p->hurt_timer = p->fall_wait = 11;
    }
}

static int load_shape(runway_assets *assets, const uint8_t *palette, uint32_t entry, runway_gang_shape *frame) {
    uint32_t shape, primitive, origin;
    if (overlay_word(assets, entry, &shape) || overlay_word(assets, shape+12, &primitive) ||
        overlay_word(assets, primitive, &origin) ||
        load_sprite(assets, palette, primitive+8, &frame->pixels, &frame->width, &frame->height)) return -1;
    frame->anchor_x = (int16_t)(origin >> 16);
    frame->anchor_y = (int16_t)origin;
    return 0;
}

static int load_butterfly(runway_phase *p, runway_assets *assets, const uint8_t *palette) {
    unsigned total = 0;
    for (unsigned i = 0; i < 26; i++) {
        uint32_t entry = (p->course==COURSE_LOGS ? 0x232c30u : 0x232a24u) + 12*i, duration, action;
        if (overlay_word(assets, entry+4, &duration) || duration == 0 || duration > 255 ||
            overlay_word(assets, entry+8, &action) || action != (i==25 ? 0x206f5cU : 0x206eb4U) ||
            load_shape(assets, palette, entry, &p->butterfly_anim[i].shape)) return -1;
        total += duration;
        p->butterfly_anim[i].end_tick = total;
    }
    return 0;
}

/* Stand__7CButFly advances X by 0x666 per yield. CheckAlive removes it at screen X <= -190. */
static int butterfly_position(const runway_phase *p, unsigned i, int *age, int32_t *world_x) {
    if (p->runway_clock < p->butterfly_start[i] || (p->butterfly_collected & (1u<<i))) return 0;
    *age = (int)(p->runway_clock - p->butterfly_start[i]);
    *world_x = -0x66db6 + 0x666 * *age;
    return *world_x < 190 * (int)UNITS_PER_PIXEL;
}

static void collect_butterflies(runway_phase *p) {
    int64_t x = (int64_t)p->camera_x - p->bones_x;
    int64_t y = ((int)PAN_HEIGHT/2 + p->hit.bot_right_y) * (int64_t)UNITS_PER_PIXEL - p->bones_y;
    for (unsigned i=0; i<p->butterfly_count; i++) {
        int age;
        int32_t bx;
        if (!butterfly_position(p,i,&age,&bx) || llabs(x-bx) > 0x9999 || llabs(y-0x16db6) > 0x9999) continue;
        /* shortcut: config contact box until the joint transforms for BonesObjHitChk are available. */
        int dx = (int)((x-bx) / UNITS_PER_PIXEL), dy = (int)((y-0x16db6) / UNITS_PER_PIXEL);
        const int radius = 0x4000 / (int)UNITS_PER_PIXEL;
        if (dx < p->hit.top_left_x-radius || dx > p->hit.top_right_x+radius ||
            dy < p->hit.top_right_y-radius || dy > p->hit.bot_right_y+radius) continue;
        runway_skel_add(&p->skel, 0xf000);
        p->enemies.pickups_collected++;
        p->butterfly_collected |= 1u << i;
    }
}

static int load_gang_recordings(runway_phase *p, runway_assets *assets, const uint8_t *palette) {
    const uint32_t table = 0x2d9684, animations = 0x28db68, code_base = 0x232000;
    p->enemies.count = 4;
    for (unsigned i = 0; i < 4; i++) {
        runway_gang_track *track = &p->gang_tracks[i];
        uint32_t start, end;
        if (overlay_word(assets, table+i*4, &start) || overlay_word(assets, table+(i+1)*4, &end) ||
            start < code_base || end <= start || end-code_base > assets->code_size ||
            (end-start) % 4) return -1;
        const uint8_t *records = (const uint8_t *)assets->code + (start-code_base);
        unsigned limit = (end-start)/4;
        for (; track->count < limit; track->count++) {
            const uint8_t *record = records + track->count*4;
            if (record[0] == 0x27 && record[1] == 0x10) break;
            unsigned pose = record[3];
            runway_gang_shape *frame = &p->gang_shapes[pose];
            if (!frame->pixels) {
                uint32_t animation;
                if (overlay_word(assets, animations + (pose>>5)*4, &animation) ||
                    load_shape(assets, palette, animation + (pose&31)*12, frame)) return -1;
            }
        }
        if (!track->count || track->count == limit) return -1;
        track->entries = malloc((size_t)track->count*4);
        if (!track->entries) return -1;
        memcpy(track->entries, records, (size_t)track->count*4);
        track->last_pan = p->gang_start_tick / 4;
        stage_enemy *actor = &p->enemies.ents[i];
        actor->kind = STAGE_ENEMY_GANG;
        actor->recorded = 1;
        actor->damage = 0x6400; /* CGangMember::CheckForBonesHit, BonesDoDamage(8, 0x6400). */
        actor->hp = 1;
    }
    return 0;
}

static void step_gang_recordings(runway_phase *p) {
    if (p->runway_clock <= p->gang_start_tick) return;
    for (unsigned i = 0; i < 4; i++) {
        runway_gang_track *track = &p->gang_tracks[i];
        stage_enemy *actor = &p->enemies.ents[i];
        unsigned index = track->next;
        if (track->last_pan != p->pan.index) {
            while (index < track->count && !(track->entries[index*4+1]&1)) index++;
        } else if (index < track->count && (track->entries[index*4+1]&1)) continue;
        if (index == track->count) {
            actor->active = 0;
            track->next = index;
            continue;
        }
        const uint8_t *record = track->entries + index*4;
        int16_t packed = (int16_t)((uint16_t)record[0]<<8 | record[1]);
        int x = ((int)packed - (record[1]&1))/2;
        actor->screen_x = (int)PAN_WIDTH/2 + x;
        actor->screen_y = (int)PAN_HEIGHT/2 + (int8_t)record[2];
        int64_t world = (int64_t)p->camera_x + (int64_t)x*UNITS_PER_PIXEL;
        actor->x = world > 0 ? (uint32_t)world : 0;
        actor->y = actor->screen_y > 0 ? (unsigned)actor->screen_y*UNITS_PER_PIXEL : 0;
        actor->active = 1;
        actor->anim = record[3];
        if (record[3] == (7u<<5)) { /* CGangMember::Read -> Shoot on ThrowEnd frame 0. */
            int32_t speed = p->bones_x >= actor->x ? 2*(int32_t)UNITS_PER_PIXEL : -2*(int32_t)UNITS_PER_PIXEL;
            runway_hazards_spawn_thrown_vel(&p->hazards, actor->x, actor->y, speed, RUNWAY_DAMAGE_DEFAULT / 4);
            p->enemies.throws_spawned++;
        }
        track->last_pan = p->pan.index;
        track->next = index+1;
    }
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
        palette[4 * i + 1] = (uint8_t)palette_words[i];
        palette[4 * i + 2] = (uint8_t)(palette_words[i] >> 8);
        palette[4 * i + 3] = (uint8_t)(palette_words[i] >> 16);
    }
    (void)best_off;
    return 0;
}

typedef struct {
    int32_t time, x, y, spin;
} ice_waypoint;

typedef struct {
    int32_t x, y, vx, vy;
    ice_waypoint *route;
    unsigned count, next, remaining, stuck, sink;
} ice_block;

typedef struct {
    int32_t x, y;
    unsigned strength, health, wait, active, shape, path;
} ice_node;

typedef struct {
    int32_t cx,cy,x,y;
    unsigned jump;
    int heading;
} ice_view;

struct runway_ice {
    finale_body *body;
    void *code, *textures;
    int heading;
    int yaw, aim, turn_rate, turning, walking, slow_turn, turn_motion;
    int prior_pad[2], input_dir, motion_weight, walk_blend, last_stride;
    scrl_map map;
    void *map_data[8];
    uint8_t palette[4096];
    ice_block blocks[64];
    ice_node *nodes;
    uint8_t crack_sfx_countdown[43];
    runway_gang_shape shapes[32], floe;
    uint32_t keys[32];
    unsigned shape_count, skate, jump, line_index, line_ticks;
    int walk_stride[32];
    unsigned walk_keys, walk_clock;
    int32_t line, line_step, line_ends[16], line_times[16];
    unsigned line_count;
    int32_t vx, vy, finish;
    uint16_t wall_angle[16];
    int camera_x, camera_y;
    ice_view views[3];
    int32_t camera_world[2];
    uint32_t parallax_x,parallax_y;
    uint8_t *navigation;
    unsigned nav_w, nav_h;
};

static int ice_pixel(int32_t world, unsigned scale) {
    return (int)(-(int64_t)world * scale / 65536);
}
static int ice_view_pixel(int32_t world, unsigned scale) {
    /* Camera map scroll negates the signed high word (hixel probes truncate toward zero). */
    return -(int)(((int64_t)world * scale)>>16);
}

static int ice_records(const runway_assets *assets, uint32_t address, unsigned stride,
                       int32_t end, unsigned *count) {
    for (*count = 0; ; ++*count) {
        uint32_t value;
        if (overlay_word(assets, address, &value)) return -1;
        if ((int32_t)value == end) return 0;
        if (address > UINT32_MAX - stride || (int32_t)value <= 0) return -1;
        address += stride;
    }
}

static int ice_load_block(runway_phase *p, const runway_assets *assets, uint32_t row) {
    uint32_t x, y, route, stuck;
    if (p->ice_block_count >= 64 || overlay_word(assets,row+12,&x) ||
        overlay_word(assets,row+16,&y) || overlay_word(assets,row+28,&route) ||
        overlay_word(assets,row+32,&stuck)) return -1;
    ice_block *b = &p->ice->blocks[p->ice_block_count++];
    b->x = (int32_t)x; b->y = (int32_t)y; b->stuck = stuck;
    if (!route) return 0;
    if (ice_records(assets,route,20,0,&b->count) || !b->count) return -1;
    b->route = calloc(b->count, sizeof *b->route);
    if (!b->route) return -1;
    for (unsigned i=0; i<b->count; ++i,route+=20) {
        uint32_t time, spin;
        if (overlay_word(assets,route,&time) || overlay_word(assets,route+4,&x) ||
            overlay_word(assets,route+8,&y) || overlay_word(assets,route+16,&spin)) return -1;
        b->route[i] = (ice_waypoint){(int32_t)time,(int32_t)x,(int32_t)y,(int32_t)spin};
    }
    return 0;
}

static int ice_load_cracks(runway_phase *p, runway_assets *assets, uint32_t row) {
    struct runway_ice *ice = p->ice;
    uint32_t address;
    unsigned count;
    if (overlay_word(assets,row+28,&address) || address > UINT32_MAX-56u ||
        ice_records(assets,address+56u,56u,-1,&count) || !count ||
        count > assets->code_size/56u - p->ice_node_count) return -1;
    ice_node *nodes = realloc(ice->nodes,(p->ice_node_count+count)*sizeof *nodes);
    if (!nodes) return -1;
    ice->nodes = nodes;
    const unsigned path = p->ice_path_count;
    if (ice->skate) {
        if (path >= sizeof ice->crack_sfx_countdown / sizeof ice->crack_sfx_countdown[0]) return -1;
        /* CCrackPath::CCrackPath, 0x29147E, initializes +0xF4 to 3. */
        ice->crack_sfx_countdown[path] = 3;
    }
    ++p->ice_path_count;
    for (unsigned i=0; i<count; ++i) {
        uint32_t hp, initial, wait, x, y, texture, active;
        address += 56u;
        if (overlay_word(assets,address,&hp) || overlay_word(assets,address+4,&initial) ||
            overlay_word(assets,address+8,&wait) || overlay_word(assets,address+12,&x) ||
            overlay_word(assets,address+16,&y) || overlay_word(assets,address+28,&texture) ||
            overlay_word(assets,address+36,&active)) return -1;
        unsigned id = 0;
        while (id < ice->shape_count && ice->keys[id] != texture) ++id;
        if (id == ice->shape_count) {
            if (id >= sizeof ice->shapes/sizeof ice->shapes[0] ||
                load_shape(assets,ice->palette+1024,texture,&ice->shapes[id])) return -1;
            ice->keys[id] = texture;
            ++ice->shape_count;
        }
        nodes[p->ice_node_count++] = (ice_node){(int32_t)x,(int32_t)y,hp,initial,wait,active,id,path};
    }
    return 0;
}

static int ice_load(runway_phase *p, runway_assets *assets, uint32_t init) {
    struct runway_ice *ice = p->ice;
    uint32_t x,y,next,ctor;
    unsigned skate = !strcmp(p->stage_name,"ICE2");
    uint32_t row = skate ? 0x2c82dcu : 0x2e3c80u;
    p->ice_support = -1;
    ice->skate = skate;
    ice->yaw = ice->aim = skate ? 0 : 768;
    ice->input_dir = ice->prior_pad[0] = ice->prior_pad[1] = -1;
    ice->finish = skate ? -0x8c46 : -0x14000;
    for (unsigned i=0;i<16;i+=2) {
        uint32_t pair;
        if (overlay_word(assets,(skate?0x2c8884u:0x2e58dcu)+2*i,&pair)) return -1;
        ice->wall_angle[i]=(uint16_t)(pair>>16);
        ice->wall_angle[i+1]=(uint16_t)pair;
    }
    ice->body=ice_body_load(assets->code,assets->code_size,assets->textures,
                           assets->textures_size,init,skate,ice->palette);
    if (!ice->body) return -1;
    if (ice->map.plane_count!=2 || strcmp(ice->map.planes[0].cdb_name,"07ACDB") ||
        strcmp(ice->map.planes[1].cdb_name,"11CCDB") || !ice->map.planes[1].pixel_h ||
        !ice->map.planes[1].pixel_w || overlay_word(assets,init+8,&x) ||
        overlay_word(assets,init+12,&y)) return -1;
    p->ice_x = (int32_t)x; p->ice_y = (int32_t)y;
    ice->camera_world[0]=p->ice_x;ice->camera_world[1]=p->ice_y;
    for(unsigned i=0;i<3;++i)
        ice->views[i]=(ice_view){p->ice_x,p->ice_y,p->ice_x,p->ice_y,0,0};
    uint32_t config, stand, motion, tracks, root;
    if (overlay_word(assets,init,&config) || overlay_word(assets,config+4,&stand) ||
        overlay_word(assets,stand+16,&motion) || overlay_word(assets,motion,&tracks) ||
        overlay_word(assets,tracks,&root)) return -1;
    /* ANrel carries local 10:10:10 translations in units of 1/128; BonesBuild
     * shifts them right two bits because the player samples one key in four retraces. */
    for (unsigned i=0;i<32;++i) {
        uint32_t relative, callback;
        if (overlay_word(assets,root+12*i,&relative) || overlay_word(assets,root+12*i+8,&callback)) return -1;
        ice->walk_stride[i]=((int)(relative&1023)-512)*128;
        ++ice->walk_keys;
        if (callback == (skate ? 0x299e4cu : 0x2a78d0u)) break;
        if (callback != (skate ? 0x299e28u : 0x2a78acu) || i+1==32) return -1;
    }
    do {
        if (overlay_word(assets,row,&next) || overlay_word(assets,row+4,&ctor) ||
            (next && next <= row)) return -1;
        if (ctor == (skate ? 0x2912c4u : 0x29e638u) && ice_load_cracks(p,assets,row)) return -1;
        if (!skate && ctor == 0x29ed50u && ice_load_block(p,assets,row)) return -1;
        row = next;
    } while (row);
    if (!skate && load_shape(assets,ice->palette+1024,0x23a4a8u,&ice->floe)) return -1;
    if (skate) {
        if (overlay_word(assets,0x2c8e04u+0x34,&ice->parallax_x) ||
            overlay_word(assets,0x2c8e04u+0x38,&ice->parallax_y)) return -1;
        ice->line = -0x20e070;
        for (unsigned i=0; i<16; ++i) {
            uint32_t duration, target;
            if (overlay_word(assets,0x2c88a4u+8*i,&duration) || overlay_word(assets,0x2c88a8u+8*i,&target)) return -1;
            if (!duration) break;
            ice->line_times[i] = (int32_t)duration;
            ice->line_ends[i] = (int32_t)target;
            ++ice->line_count;
        }
        if (!ice->line_count) return -1;
    }
    return p->ice_path_count == (skate ? 43u : 86u) && p->ice_block_count == (skate ? 0u : 64u) ? 0 : -1;
}

static void ice_free(runway_phase *p) {
    struct runway_ice *ice=p->ice;
    if (!ice) return;
    free(ice->body);
    free(ice->textures);
    free(ice->code);
    for (unsigned i=0; i<p->ice_block_count; ++i) free(ice->blocks[i].route);
    for (unsigned i=0; i<ice->shape_count; ++i) free(ice->shapes[i].pixels);
    free(ice->floe.pixels);
    free(ice->nodes);
    free(ice->navigation);
    scrl_map_free(&ice->map);
    for (unsigned i=0; i<8; ++i) free(ice->map_data[i]);
    free(ice);
}

static int ice_near(int32_t a, int32_t b, int32_t radius) {
    int64_t distance = (int64_t)a - b;
    return distance > -radius && distance < radius;
}

static void ice_move_blocks(runway_phase *p) {
    for (unsigned i=0;i<p->ice_block_count;++i) {
        ice_block *block=&p->ice->blocks[i];
        if (block->sink) --block->sink;
        if (!block->count || block->stuck) continue;
        const ice_waypoint *target=&block->route[block->next];
        if (!block->remaining) {
            block->remaining=(unsigned)target->time;
            block->vx=(target->x-block->x)/target->time;
            block->vy=(target->y-block->y)/target->time;
        }
        int32_t old_x=block->x, old_y=block->y;
        if (--block->remaining) { block->x+=block->vx; block->y+=block->vy; }
        else {
            block->x=target->x; block->y=target->y;
            block->next=(block->next+1u)%block->count;
        }
        if ((int)i==p->ice_support && !p->airborne) {
            int32_t dx=block->x-old_x, dy=block->y-old_y;
            p->ice_x+=dx; p->ice_y+=dy;
            if (dx || dy) ++p->ice_carried;
        }
    }
}

static void ice_crack_step(runway_phase *p, int covered) {
    struct runway_ice *ice=p->ice;
    if (ice->skate && ice->line_index < ice->line_count) {
        if (!ice->line_ticks) {
            unsigned duration=(unsigned)ice->line_times[ice->line_index];
            ice->line_ticks=duration+1;  /* Advance when --count == -1. */
            ice->line_step=(ice->line_ends[ice->line_index]-ice->line)/(int32_t)duration;
        }
        ice->line += ice->line_step;
        if (!--ice->line_ticks) ++ice->line_index;
    }
    for (unsigned i=0;i<p->ice_node_count;++i) {
        ice_node *q=&ice->nodes[i];
        int on_floor = !p->airborne && p->ice_support < 0 && !covered;
        int close=ice_near(p->ice_x,q->x,0x19000) && ice_near(p->ice_y,q->y,0x19000);
        int32_t radius=ice->skate ? 0x6934 : 0xf000;
        int through=(ice->skate || on_floor) && ice_near(p->ice_x,q->x,radius) && ice_near(p->ice_y,q->y,radius);
        if (!q->active && (ice->skate ? q->y<=ice->line : (on_floor && close))) {
            q->active = 1;
            if (ice->skate) {
                /* ICE2 CheckCrackLine (0x291680) counts newly activated nodes per path. */
                uint8_t *countdown = &ice->crack_sfx_countdown[q->path];
                if (*countdown) --*countdown;
                else {
                    phase_sound_event(p, 0x30);
                    *countdown = 5;
                }
            } else {
                /* ICE1 CheckForCracks calls SoundEvent(0x2E) on the inactive edge. */
                phase_sound_event(p, 0x2e);
            }
        }
        if (q->active && ((ice->skate && through) || (!ice->skate && close && on_floor && !q->wait)))
            q->health = q->health>2 ? q->health-2u : 0u;
        if (q->active && q->wait) --q->wait;
        if (through && !q->health && !p->hurt_timer) {
            ++p->ice_falls;
            if (ice->skate || runway_skel_loose(&p->skel,0x11999)) {
                if (!ice->skate) phase_sound_event(p, 0x2c);
                p->state=RUNWAY_ST_LOST;
            } else {
                /* ICE1 FallThroughIce queues 0x31 after a recoverable break. */
                phase_sound_event(p, 0x31);
            }
            p->hurt_timer=30;
            q->health=q->strength;
        }
        if (q->health<q->strength) ++q->health;
    }
}

static int ice_hixel(const struct runway_ice *ice,int x,int y) {
    const scrl_plane *plane=&ice->map.planes[0];
    if (x<0 || y<0 || (unsigned)x>=plane->pixel_w || (unsigned)y>=plane->pixel_h) return 3;
    return scrl_tilecache_hixel(&plane->tiles,(unsigned)x,(unsigned)y);
}

static int ice_coverage(const struct runway_ice *ice,int x,int y,int *covered) {
    int h[4]={ice_hixel(ice,x,y+2),ice_hixel(ice,x+2,y),
              ice_hixel(ice,x,y-2),ice_hixel(ice,x-2,y)};
    if (covered) *covered=h[0] || h[1] || h[2] || h[3];
    return (h[0]==3) | (h[1]==3)<<1 | (h[2]==3)<<2 | (h[3]==3)<<3;
}

static int ice_open(const struct runway_ice *ice,int x,int y) {
    /* GetHixelCoverage: four samples, 256-turn increments, signed sin8 >> 7. */
    return !ice_coverage(ice,x,y,NULL);
}

static const int ice_nav_dx[8]={0,-1,1,0,-1,1,-1,1},ice_nav_dy[8]={-1,0,0,1,-1,-1,1,1};

/* Autoplay steers using the same four pad directions through the map A collision mask. */
static int ice_navigation(runway_phase *p) {
    struct runway_ice *ice=p->ice;
    unsigned w=(ice->map.planes[0].pixel_w+7)/8, h=(ice->map.planes[0].pixel_h+7)/8;
    size_t total=(size_t)w*h;
    unsigned *queue=malloc(total*sizeof *queue),*cost=malloc(total*sizeof *cost);
    uint8_t *clearance=malloc(total),*queued=calloc(total,1),*risk=calloc(total,1);
    ice->navigation=calloc(total,1);
    if (!queue || !ice->navigation || !cost || !clearance || !queued || !risk) {
        free(queue); free(cost); free(clearance); free(queued); free(risk); return -1;
    }
    ice->nav_w=w; ice->nav_h=h;
    for (unsigned j=0;j<h;++j) for(unsigned i=0;i<w;++i) {
        unsigned k=j*w+i;
        cost[k]=UINT32_MAX;
        clearance[k]=i && j && i+1<w && j+1<h && ice_open(ice,(int)i*8+4,(int)j*8+4)?31:0;
    }
    /* Manhattan distance from the banks keeps a sliding character in open water. */
    for(unsigned j=1;j<h;++j) for(unsigned i=1;i<w;++i) {
        unsigned k=j*w+i, a=1u+clearance[k-1],b=1u+clearance[k-w];
        if(clearance[k]>a)clearance[k]=(uint8_t)a;
        if(clearance[k]>b)clearance[k]=(uint8_t)b;
    }
    for(unsigned j=h-1;j>0;--j) for(unsigned i=w-1;i>0;--i) {
        unsigned k=(j-1)*w+i-1,a=1u+clearance[k+1],b=1u+clearance[k+w];
        if(clearance[k]>a)clearance[k]=(uint8_t)a;
        if(clearance[k]>b)clearance[k]=(uint8_t)b;
    }
    if(!ice->skate) for(unsigned n=0;n<p->ice_node_count;++n) {
        int cx=ice_pixel(ice->nodes[n].x,p->projection_px)/8;
        int cy=ice_pixel(ice->nodes[n].y,p->projection_px)/8;
        for(int j=cy-7;j<=cy+7;++j) for(int i=cx-7;i<=cx+7;++i) {
            if ((unsigned)i>=w || (unsigned)j>=h || ice_hixel(ice,i*8+4,j*8+4)) continue;
            int r=7-abs(i-cx)-abs(j-cy);
            if(r>(int)risk[(unsigned)j*w+(unsigned)i])risk[(unsigned)j*w+(unsigned)i]=(uint8_t)r;
        }
    }
    unsigned head=0,tail=0;
    unsigned goal=(unsigned)ice_pixel(ice->finish,p->projection_px)/8;
    if(goal) --goal;
    for (unsigned x=1;x+1<w;++x) {
        if (!ice_open(ice,(int)x*8+4,(int)goal*8+4)) continue;
        unsigned i=goal*w+x;
        ice->navigation[i]=1;
        cost[i]=0;queued[i]=1;
        queue[tail++]=i;
    }
    while(head!=tail) {
        unsigned i=queue[head];
        head=(head+1)%(unsigned)total;
        queued[i]=0;
        int x=(int)(i%w),y=(int)(i/w);
        for (unsigned d=0;d<8;++d) {
            int nx=x-ice_nav_dx[d],ny=y-ice_nav_dy[d];
            if ((unsigned)nx>=w || (unsigned)ny>=h) continue;
            unsigned j=(unsigned)ny*w+(unsigned)nx;
            if (!clearance[j] || (d>=4 && (!clearance[(unsigned)ny*w+(unsigned)x] ||
                                           !clearance[(unsigned)y*w+(unsigned)nx]))) continue;
            unsigned distance=cost[i]+(d>=4?14u:10u)*(4u+14u/clearance[j]+3u*risk[j]);
            if(distance>=cost[j])continue;
            cost[j]=distance;
            ice->navigation[j]=(uint8_t)(d+1);
            if(!queued[j]) {queued[j]=1;queue[tail]=j;tail=(tail+1)%(unsigned)total;}
        }
    }
    free(queue);free(cost);free(queued);free(clearance);free(risk);
    return 0;
}

static unsigned ice_autoplay(const runway_phase *p) {
    const struct runway_ice *ice=p->ice;
    int x=ice_pixel(p->ice_x,p->projection_px),y=ice_pixel(p->ice_y,p->projection_px);
    if (!ice->navigation || (unsigned)x/8>=ice->nav_w || (unsigned)y/8>=ice->nav_h) return 0;
    int gx=x/8, gy=y/8;
    unsigned v=ice->navigation[(unsigned)gy*ice->nav_w+(unsigned)gx];
    int tx=8*gx+4, ty=8*gy+4;
    if (v) {
        /* Look ahead through the authored waterway and correct for sliding momentum. */
        int lead=ice->skate?96:88;
        for (unsigned i=0;i<24 && (tx-x)*(tx-x)+(ty-y)*(ty-y)<lead*lead;++i) {
            gx+=ice_nav_dx[v-1]; gy+=ice_nav_dy[v-1];
            if ((unsigned)gx>=ice->nav_w || (unsigned)gy>=ice->nav_h) break;
            int px=gx*8+4,py=gy*8+4;
            int steps=(abs(px-x)+abs(py-y))/4+1;
            int visible=1;
            for(int j=1;j<=steps;++j)
                if(!ice_open(ice,x+(px-x)*j/steps,y+(py-y)*j/steps)) {visible=0;break;}
            if(!visible && i>0)break;
            tx=px; ty=py;
            v=ice->navigation[(unsigned)gy*ice->nav_w+(unsigned)gx];
            if (!v) break;
        }
    }
    else {
        int best=INT32_MAX;
        for (int j=gy-2;j<=gy+2;++j) for (int i=gx-2;i<=gx+2;++i) {
            if ((unsigned)i>=ice->nav_w || (unsigned)j>=ice->nav_h ||
                !ice->navigation[(unsigned)j*ice->nav_w+(unsigned)i]) continue;
            int dx=8*i+4-x,dy=8*j+4-y,dist=dx*dx+dy*dy;
            if (dist<best) {best=dist;tx=x+dx;ty=y+dy;}
        }
    }
    unsigned btn=0;
    x += ice_pixel(ice->vx*15,p->projection_px);
    y += ice_pixel(ice->vy*15,p->projection_px);
    int ex=tx-x,ey=ty-y;
    if(ex>5 && ex*1000>414*abs(ey))btn|=MB_PAD_RIGHT;
    if(ex< -5 && -ex*1000>414*abs(ey))btn|=MB_PAD_LEFT;
    if(ey>5 && ey*1000>414*abs(ex))btn|=MB_PAD_DOWN;
    if(ey< -5 && -ey*1000>414*abs(ex))btn|=MB_PAD_UP;
    for (unsigned i=0;!ice->skate && i<p->ice_node_count;++i) {
        const ice_node *q=&ice->nodes[i];
        if (q->active && q->health<q->strength && ice_near(p->ice_x,q->x,0x19000) &&
            ice_near(p->ice_y,q->y,0x19000) && !(p->frames&1u)) return btn|MB_PAD_B;
    }
    return btn;
}

static int ice_clamp(int n,int upper) {return n<0 ? 0 : n>upper ? upper : n;}

/* ICE2 CCameraMover::Brain (0x2a521c) tracks the projected BonesPos
 * inside a one-pixel dead zone. Its ctor supplies 256 fixed-point units
 * per excess pixel on each of the four sides. */
static void ice_follow_camera(runway_phase *p) {
    struct runway_ice *ice=p->ice;
    int32_t pos[2]={p->ice_x,p->ice_y};
    double depth=ice->skate ? 3.5 : 8.;
    if(p->frames>4) depth+=(ice->skate?72089.:-72089.)/65536.;
    for(unsigned i=0;i<2;++i) {
        int screen=(int)ceil((pos[i]-ice->camera_world[i])*(256./depth)/65536.);
        if(screen>1)ice->camera_world[i]+=(screen-1)*256;
        if(screen< -1)ice->camera_world[i]+=(screen+1)*256;
    }
}

static void ice_camera(runway_phase *p) {
    struct runway_ice *ice=p->ice;
    int x=ice_view_pixel(p->ice_x,p->projection_px), y=ice_view_pixel(p->ice_y,p->projection_px);
    const ice_view *v=&ice->views[(p->frames+1)%3];
    ice->camera_x=ice_clamp(ice_view_pixel(v->cx,p->projection_px)-160,(int)ice->map.planes[0].pixel_w-320);
    ice->camera_y=ice_clamp(ice_view_pixel(v->cy,p->projection_px)-112,(int)ice->map.planes[0].pixel_h-224);
    p->bones_x=(uint32_t)x*UNITS_PER_PIXEL;
    p->bones_y=(uint32_t)y*UNITS_PER_PIXEL;
    p->camera_x=(uint32_t)ice->camera_x*UNITS_PER_PIXEL;
    p->camera_y=(uint32_t)ice->camera_y*UNITS_PER_PIXEL;
}

static int ice_angle_delta(int to, int from) {
    return ((to-from+511)&1023)-511;
}

/* BonesFaceDir (ICE2 0x299aa0) advances a committed turn before asking
 * BBS for the next direction. BonesWalk (0x29c7cc) calls FaceDir twice
 * on entry to the turning branch and once on each subsequent yield. */
static void ice_walk(runway_phase *p, int horizontal, int vertical, int *dx, int *dy) {
    struct runway_ice *ice=p->ice;
    unsigned tick=p->frames;
    int dir=-1;
    if (horizontal || vertical)
        dir=(int)lround(atan2((double)horizontal,(double)(ice->skate?-vertical:vertical))
                        *1024./6.283185307179586)&1023;
    if (tick&1u) ice->input_dir=ice->prior_pad[0];
    ice->prior_pad[0]=ice->prior_pad[1];
    ice->prior_pad[1]=dir;
    int opening=tick<(ice->skate ? 5u : 4u);
    int moved=0;
    if (!opening) {
        if (!ice->walking && ice->input_dir>=0) ice->aim=ice->input_dir;
        int delta=ice_angle_delta(ice->aim,ice->yaw);
        if (delta) {
            int step=ice->walking ? ice->turn_rate : 64;
            if (!ice->turning) {
                step=ice->walking ? 2*step : 96;
                if (ice->walking && ice->slow_turn) {
                    ice->turn_motion=1;
                    ice->motion_weight=255+32;
                }
            }
            ice->yaw=(ice->yaw+(delta< -step ? -step : delta>step ? step : delta))&1023;
            ice->turning=ice->yaw!=ice->aim;
        }
        if (!ice->turning) {
            if (ice->input_dir<0) {
                ice->walking=0;
                ice->turn_motion=0;
            } else {
                if (!ice->walking || ice->turn_motion) {
                    ice->walk_blend=ice->turn_motion ? 279 : 239;
                    ice->slow_turn=!ice->walking;
                    ice->turn_motion=0;
                    ice->motion_weight=0;
                    ice->walk_clock=0;
                }
                ice->walking=1;
                ice->aim=ice->input_dir;
                ice->turn_rate=abs(ice_angle_delta(ice->aim,ice->yaw)>>4);
            }
        }
        if (ice->walking) {
            if (ice->turn_motion) {
                /* Crossfade the old translation against a stationary turn. */
                ice->motion_weight-=32;
                if (ice->motion_weight>0)
                    moved=ice->last_stride*ice->motion_weight/(255+32);
            } else {
                int stride=ice->walk_stride[(ice->walk_clock++/4)%ice->walk_keys];
                ice->last_stride=stride;
                ice->motion_weight+=24;
                if (ice->motion_weight>ice->walk_blend) ice->motion_weight=ice->walk_blend;
                moved=stride*ice->motion_weight/ice->walk_blend;
            }
        }
    }
    ice->heading=(ice->yaw-(ice->skate?0:768))&1023;
    double radians=ice->yaw*6.283185307179586/1024.;
    int sx=(int)(256.*sin(radians)),cy=(int)(256.*cos(radians));
    int target_x=sx*moved>>8;
    int target_y=(ice->skate ? -cy : cy)*moved>>8;
    /* BonesBuild (0x2972f0) applies the friction gain to motion already
     * rotated by the body. The startup FrictionTimeout bypasses the gain. */
    if (moved && ice->walk_clock==1 && !ice->turn_motion && tick<16) {
        ice->vx=sx*263>>8;
        ice->vy=cy*263>>8;
    }
    if ((ice->skate && moved && tick<16 && ice->walk_clock<=2) ||
        (!ice->skate && moved && tick<=5)) {
        *dx=target_x;*dy=target_y;
    } else {
        ice->vx+=(target_x-ice->vx)>>6;
        ice->vy+=(target_y-ice->vy)>>6;
        *dx=ice->vx;*dy=ice->vy;
    }
    if(ice->skate && tick==5 && ice->input_dir>=0 && ice->turning) {
        /* First InAir move is built after the initial 32-turn step,
         * before the following two 32-turn advances in the same retrace. */
        int turn=ice_angle_delta(ice->aim,0)<0 ? -32 : 32;
        double angle=turn*6.283185307179586/1024.;
        *dx=((int)(256.*sin(angle))*524)>>8;
        *dy=(-(int)(256.*cos(angle))*524)>>8;
    }
}

static int ice_step(runway_phase *p,const saturn_pad *pad) {
    struct runway_ice *ice=p->ice;
    int crossed=p->ice_y>ice->finish; /* IceFloor Brain precedes BonesBuild. */
    ice_follow_camera(p);
    int horizontal=(int)!!(pad->held&0x4000)-(int)!!(pad->held&0x8000);
    int vertical=(int)!!(pad->held&0x1000)-(int)!!(pad->held&0x2000);
    if (horizontal) p->facing=-horizontal;
    if (p->hurt_timer) --p->hurt_timer;
    runway_skel_tick_cooldown(&p->skel);
    ice_move_blocks(p);
    if (!p->airborne && pad->pressed&0x0100) {
        p->airborne=1; ice->jump=0; p->ice_support=-1;
        p->vel[1]=p->jump_impulse; p->sfx_flags|=SFX_JUMP;
    }
    int dx,dy;
    ice_walk(p,horizontal,vertical,&dx,&dy);
    int covered=0;
    int walls=ice_coverage(ice,ice_pixel(p->ice_x+dx,p->projection_px),
                          ice_pixel(p->ice_y+dy,p->projection_px),&covered);
    if (walls) {
        double angle=ice->wall_angle[walls]*6.283185307179586/1024.;
        int nx=(int)(256.*sin(angle)),ny=(int)(256.*cos(angle));
        int normal=((nx*dx)>>8)+((ny*dy)>>8);
        dx-=nx*normal>>8;dy-=ny*normal>>8;
        if (ice_coverage(ice,ice_pixel(p->ice_x+dx,p->projection_px),
                         ice_pixel(p->ice_y+dy,p->projection_px),&covered)) {
            dx=dy=0;
            ice->vx=nx*4;ice->vy=ny*4;
        }
    }
    p->ice_x+=dx;p->ice_y+=dy;
    if (p->airborne) {
        p->vel[1]+=SIDESCRL_PHYS_GRAVITY_ACCEL;
        int64_t height=(int64_t)ice->jump-p->vel[1];
        if (height<=0) { ice->jump=0; p->airborne=0; p->sfx_flags|=SFX_LAND; }
        else ice->jump=(unsigned)height;
    }
    p->ice_support=-1;
    for (unsigned i=0;!p->airborne && i<p->ice_block_count;++i) {
        ice_block *b=&ice->blocks[i];
        if (ice_near(p->ice_x,b->x,0x16800) && ice_near(p->ice_y,b->y,0x16800)) {
            p->ice_support=(int)i;
            if (p->sfx_flags&SFX_LAND) {b->stuck=0; b->sink=15;}
            break;
        }
    }
    ice_crack_step(p,covered);
    if (p->state!=RUNWAY_ST_LOST)
        p->state=crossed ? RUNWAY_ST_FINISHED :
            p->airborne ? RUNWAY_ST_AIR : (ice->vx || ice->vy) ? RUNWAY_ST_WALK : RUNWAY_ST_STAND;
    p->walk_dist+=(unsigned)(abs(ice->vx)+abs(ice->vy));
    ice->views[p->frames%3]=(ice_view){ice->camera_world[0],ice->camera_world[1],
                                       p->ice_x,p->ice_y,ice->jump,ice->heading};
    ice_camera(p);
    snprintf(p->status,sizeof p->status,"%s x=%d y=%d support=%d cracks=%u",
             p->stage_name,p->ice_x,p->ice_y,p->ice_support,p->ice_falls);
    return runway_phase_render(p);
}

/* ICE2's A map uses the VDP2 special-colour mode: the bottom three
 * palette-dot bits select the ice texture, bit 3 selects opaque shoreline.
 * CCRNB=0x0b combines 20 parts of the A dot with 12 parts behind it. */
static void ice_layer(runway_phase *p, int surface) {
    struct runway_ice *ice=p->ice;
    const scrl_plane *plane=&ice->map.planes[surface ? 0 : 1];
    unsigned ox=(unsigned)ice->camera_x,oy=(unsigned)ice->camera_y;
    const ice_view *following=&ice->views[(p->frames+2)%3];
    unsigned last_x=(unsigned)ice_clamp(ice_view_pixel(following->cx,p->projection_px)-160,
                                        (int)ice->map.planes[0].pixel_w-320);
    unsigned last_y=(unsigned)ice_clamp(ice_view_pixel(following->cy,p->projection_px)-112,
                                        (int)ice->map.planes[0].pixel_h-224);
    if (!surface) {
        ox=(ox*ice->parallax_x)>>16;oy=(oy*ice->parallax_y)>>16;
        last_x=(last_x*ice->parallax_x)>>16;last_y=(last_y*ice->parallax_y)>>16;
    }
    uint8_t *front=ice_body_surface(ice->body);
    for (unsigned y=0;y<224;++y) {
        /* Both VDP2 layers latch the next scroll for the last scanline.
         * A also restarts its line table at zero on that scanline. */
        if(y==223){ox=last_x;oy=last_y;}
        unsigned sy=(oy+(surface && y==223 ? 0 : y))%plane->pixel_h;
        for (unsigned x=0;x<320;++x) {
            int i=scrl_tilecache_cram(&plane->tiles,(ox+x)%plane->pixel_w,sy);
            uint8_t *dst=p->frame_rgba+(y*320+x)*4u;
            if (i<=0) {if (!surface) {memset(dst,0,3);dst[3]=255;} continue;}
            const uint8_t *color=ice->palette+(unsigned)i*4u+1u;
            if (surface) memcpy(front+(y*320u+x)*4u,color,3);
            if (surface && !(i&8)) {
                for (unsigned c=0;c<3;++c) dst[c]=(uint8_t)((20u*color[c]+12u*dst[c])>>5);
            } else memcpy(dst,color,3);
            dst[3]=255;
        }
    }
}

static int ice_draw(runway_phase *p) {
    struct runway_ice *ice=p->ice;
    ice_camera(p);
    ice->map.camera_y=ice->camera_y;
    if (ice->skate) ice_layer(p,0);
    else {
        if (scrl_map_draw(&ice->map,ice->palette,p->frame_rgba,320,224,(unsigned)ice->camera_x)) return -1;
        memcpy(p->frame_rgba+223u*320u*4u,p->frame_rgba,320u*4u);
    }
    for (unsigned i=0;i<p->ice_node_count;++i) {
        const ice_node *n=&ice->nodes[i];
        if (!n->active) continue;
        int x=ice_pixel(n->x,p->projection_px)-ice->camera_x;
        int y=ice_pixel(n->y,p->projection_px)-ice->camera_y;
        const runway_gang_shape *shape=&ice->shapes[n->shape];
        if (x>=-64 && x<384 && y>=-64 && y<288)
            runway_compose_blit_sprite(p->frame_rgba,320,224,shape->pixels,shape->width,shape->height,
                                       x-shape->anchor_x,y-shape->anchor_y);
    }
    for (unsigned i=0;i<p->ice_block_count;++i) {
        ice_block *b=&ice->blocks[i];
        int x=ice_pixel(b->x,p->projection_px)-ice->camera_x;
        int y=ice_pixel(b->y,p->projection_px)-ice->camera_y;
        if (x>=-64 && x<384 && y>=-64 && y<288)
            runway_compose_blit_sprite(p->frame_rgba,320,224,ice->floe.pixels,ice->floe.width,ice->floe.height,
                                       x-(int)ice->floe.width/2,y-(int)ice->floe.height/2);
    }
    const ice_view *view=&ice->views[(p->frames+1)%3];
    int sx=ice_view_pixel(view->cx,p->projection_px)-ice->camera_x;
    int sy=ice_view_pixel(view->cy,p->projection_px)-ice->camera_y;
    double z = p->frames < 4 ? 0. : (ice->skate ? 72089. : -72089.)/65536.;
    int emerged=ice->skate && p->ice_y>=-65536;
    if (emerged) ice_layer(p,1);
    ice_body_draw(ice->body,p->frame_rgba,p->frames,ice->turn_motion?2:ice->walking,view->heading,
                  (view->x-view->cx)/65536.+(160-sx)/(double)p->projection_px,
                  (view->y-view->cy)/65536.+(112-sy)/(double)p->projection_px,
                  z - view->jump/65536.);
    if (ice->skate && !emerged) ice_layer(p,1);
    finale_body_draw_health(ice->body,p->frame_rgba,(unsigned)p->skel.segments);
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

typedef struct {
    int32_t drift_vx;
    unsigned finish_px;
    unsigned speed_pct;
} runway_stage_profile;

static const runway_stage_profile *runway_profile_for_stage(const char *s) {
    static const runway_stage_profile k_runway = { 0, 4400u, 100u };
    static const runway_stage_profile k_logs   = { 260, 2400u, 100u };
    static const runway_stage_profile k_ride   = { 1280, 2400u, 130u };
    static const runway_stage_profile k_moat   = { 0, 960u, 100u };
    static const runway_stage_profile k_staing = { 0, 2400u, 100u };
    if (!s || !strcmp(s, "RUNWAY")) return &k_runway;
    if (!strcmp(s, "LOGS")) return &k_logs;
    if (!strcmp(s, "RIDE")) return &k_ride;
    static const runway_stage_profile k_ice = {0, 0, 100u};
    if (!strcmp(s,"ICE1") || !strcmp(s,"ICE2")) return &k_ice;
    if (!strcmp(s, "MOAT")) return &k_moat;
    if (!strcmp(s, "STAING")) return &k_staing;
    return &k_runway;
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

/* The RUNWAY chase uses the full-body _SkeletonRun poses. */
static void anim_update(runway_phase *phase) {
    unsigned bank, idx, n;
    if (!phase) return;
    if (phase->state == RUNWAY_ST_WALK && !phase->tall && !phase->runway_win_record)
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
        int changed = mplay_seq_step(&phase->mplay);
        idx = mplay_seq_frame(&phase->mplay);
        /* _SkeletonRun and _SkeletonCRun tag poses 3 and 13 with 0x00210003. */
        if (phase->runway_win_record && changed && (idx == 3u || idx == 13u))
            phase->sfx_flags |= SFX_WALK;
    } else if (phase->state == RUNWAY_ST_AIR) {
        idx = BONES_HOLD_FRAME < n ? BONES_HOLD_FRAME : 0;
    } else {
        idx = (phase->anim_bank == bank) ? phase->anim_index : 0;
        if (idx >= n) idx = BONES_HOLD_FRAME < n ? BONES_HOLD_FRAME : 0;
    }
    anim_set_current(phase, bank, idx);
}

/*
 * Distant backdrop (scrl_bg_rgba), sized from what the discs actually hold. Measured
 * with scrl_map on the loaded planes (09BCDB/10BTIL are empty on all five stages):
 *   11LOGS  D 320x224                 18RIDE  C 320x224 (one flat colour)
 *   23MOAT  C 512x512 + D 320x224     20ICE1  A 1864x8192 + C 512x512
 *   21ICE2  A 640x2400 + C 360x512
 * No plane is a 512-wide tiling layer that is actually visible, so a wrapping strip
 * shows a vertical seam (LOGS plane D is a full-screen painting whose left and right
 * edges do not match: seam 79 vs 5 per column on average). Therefore:
 *   - a stage with any plane <= PAN_WIDTH wide (a one-screen picture) keeps the
 *     frozen 320x224 render at camera 0 (scrl_bg_w = 0): LOGS, RIDE, MOAT;
 *   - otherwise (ICE1, ICE2) the strip is rendered as wide as the widest plane
 *     (<= PAN_SCRL_BG_MAX_W) and the view slides over it, clamped at the end, so it
 *     never wraps.
 * Limitation: the strip is rendered once at camera 0 and scrolled as a single image,
 * so the whole backdrop moves at ONE parallax rate; the per-plane rates of
 * scrl_map_draw are lost. scrl_map_draw is never called per frame.
 */
static unsigned scrl_bg_strip_width(const scrl_map *m) {
    unsigned i, w = 0;
    for (i = 0; i < m->plane_count && i < SCRL_PLANE_MAX; i++) {
        const scrl_plane *p = &m->planes[i];
        if (p->tiles.status != SCRL_TC_OK) continue; /* drawn as nothing */
        if (p->pixel_w <= PAN_WIDTH) return 0u;      /* one-screen picture: keep frozen */
        if (p->pixel_w > w) w = p->pixel_w;
    }
    if (w > PAN_SCRL_BG_MAX_W) w = PAN_SCRL_BG_MAX_W;
    if (w < PAN_WIDTH + PAN_SCRL_BG_MIN_ROOM) return 0u;
    return w;
}

unsigned runway_phase_bg_column(unsigned x, unsigned camera_px,
                                unsigned pan_index, unsigned strip_w) {
    unsigned dx, room, col;
    if (strip_w == 0u) return 0u;
    dx = camera_px / PAN_SCRL_BG_CAM_DIV + pan_index / PAN_SCRL_BG_IDX_DIV;
    room = strip_w > PAN_WIDTH ? strip_w - PAN_WIDTH : 0u;
    if (dx > room) dx = room;
    col = x + dx;
    return col < strip_w ? col : strip_w - 1u;
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
    phase->coyote_timer = 0;
    phase->jump_buf_timer = 0;
    phase->walk_dist = 0;
    if (!strcmp(phase->stage_name, "RIDE")) phase->fly_y_px = 132;
    else if (!strcmp(phase->stage_name, "STAING")) phase->fly_y_px = 108;
    else phase->fly_y_px = 0;
    pan_hit_offsets_default(&phase->hit);
    phase->audio_track = 2;
    phase->state = RUNWAY_ST_STAND;
    phase->pre_pause_state = RUNWAY_ST_STAND;
    {
        const runway_stage_profile *prof = runway_profile_for_stage(phase->stage_name);
        phase->finish_x = prof->finish_px * UNITS_PER_PIXEL;
        runway_skel_init(&phase->skel);
        /* §22: hazards on all 7 15PAN stages (RUNWAY + LOGS/ICE1/ICE2/MOAT/RIDE/STAING). */
        runway_hazards_init_pan_stage(&phase->hazards, phase->stage_name, UNITS_PER_PIXEL);
        /* §24: gang/lord/poofs/wingnuts/tbones on all 15PAN stages. */
        if (!strcmp(phase->stage_name, "RIDE") || !strcmp(phase->stage_name, "MOAT") ||
            !strcmp(phase->stage_name, "ICE1") || !strcmp(phase->stage_name, "ICE2") ||
            !strcmp(phase->stage_name, "LOGS") || !strcmp(phase->stage_name,"STAING"))
            phase->enemies.units_per_pixel = UNITS_PER_PIXEL;
        else
            stage_enemies_init_pan_stage(&phase->enemies, phase->stage_name, UNITS_PER_PIXEL);
        phase->enemies.stage_name = phase->stage_name;
        stage_enemies_load_disc_sprites(cd);
        if (!runway && strcmp(phase->stage_name,"ICE1") && strcmp(phase->stage_name,"ICE2") &&
            strcmp(phase->stage_name, "RIDE") && strcmp(phase->stage_name, "MOAT") &&
            strcmp(phase->stage_name,"STAING") && strcmp(phase->stage_name,"LOGS")) {
            stage_enemies_add_health_pickup(&phase->enemies, 165u, 150u,
                                            RUNWAY_DAMAGE_DEFAULT);
            if (prof->finish_px > 420u) {
                stage_enemies_add_health_pickup(&phase->enemies, 480u, 150u,
                                                RUNWAY_DAMAGE_DEFAULT);
            }
            if (prof->finish_px > 1200u) {
                stage_enemies_add_health_pickup(&phase->enemies, 960u, 150u,
                                                RUNWAY_DAMAGE_DEFAULT);
                stage_enemies_add_health_pickup(&phase->enemies, 1620u, 150u,
                                                RUNWAY_DAMAGE_DEFAULT);
            }
        }
    }
    bones_bbs_post(&phase->bbs, MSG_STAND_BOOT, 0);

    if (runway_assets_load_dir(cd, iso_dir, &assets)) return -1;
    const bones_projection *projection = bones_projection_for(phase->stage_name);
    bones_stage_init initial;
    if (!projection || bones_projection_init(projection, assets.code, assets.code_size, &initial)) {
        runway_assets_free(&assets);
        return -1;
    }
    if (!strcmp(phase->stage_name,"ICE1") || !strcmp(phase->stage_name,"ICE2")) {
        phase->ice = calloc(1,sizeof *phase->ice);
        if (!phase->ice) {runway_assets_free(&assets); runway_phase_free(phase); return -1;}
    }
    phase->projection_px = projection->pixels_per_unit;
    phase->hit = initial.hit;
    phase->jump_impulse = initial.jump_impulse;
    phase->boost_ticks = initial.boost_ticks;
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
        if (runway) {
            uint32_t start_x, run_cue;
            if (load_runway_events(phase, &assets) || load_fall_profile(phase, &assets) ||
                overlay_word(&assets, projection->init_address + 8, &start_x) ||
                overlay_word(&assets, 0x253cac + 4, &run_cue) || !(run_cue & 0xffffu)) {
                runway_assets_free(&assets);
                runway_phase_free(phase);
                return -1;
            }
            phase->bones_x = phase->camera_x - start_x;
            phase->bones_y = (PAN_HEIGHT / 2 + initial.start_y + phase->hit.bot_right_y) * UNITS_PER_PIXEL;
            phase->input_cell = phase->bones_x;
            phase->facing = initial.facing;
            /* _SkeletonRun has twenty poses with the same duration in the low word. */
            mplay_seq_init(&phase->mplay, BONES_FULL_FRAMES, run_cue & 0xffffu);
            /* UpdateSpeed writes 0x2c54 exactly when CurrentFrame changes. */
            phase->finish_x = START_X + phase->runway_win_record * 0x2c54u;
        }
    }
    {
        /* §16/§22 distant parallax backdrop (scrl_bg_rgba) for pan stages whose
         * 15PAN leaves sky/canyon holes. ICE1/ICE2 also carry a Plane A
         * (07ACDB/08ATIL) ice canyon seen through their idx==255 cutouts. */
        int is_ice = !strcmp(phase->stage_name, "ICE1") ||
                     !strcmp(phase->stage_name, "ICE2");
        int want_scrl = is_ice ||
                        !strcmp(phase->stage_name, "LOGS") ||
                        !strcmp(phase->stage_name, "RIDE") ||
                        !strcmp(phase->stage_name, "MOAT");
        if (want_scrl) {
            static const char *const k_scrl[6] = {
                "09BCDB", "10BTIL", "11CCDB", "12CTIL", "13DCDB", "14DTIL"
            };
            void *blobs[6] = {0};
            size_t sizes[6] = {0};
            void *a_cdb = NULL, *a_til = NULL;
            size_t a_cdb_sz = 0, a_til_sz = 0;
            scrl_map map;
            unsigned k;
            for (k = 0; k < 6u; k++) {
                char spath[96];
                snprintf(spath, sizeof spath, "%s/%s", iso_dir, k_scrl[k]);
                if (resource_load(cd, spath, &blobs[k], &sizes[k]) || sizes[k] == 0) {
                    free(blobs[k]);
                    blobs[k] = NULL;
                    sizes[k] = 0;
                }
            }
            if (is_ice) {
                char spath[96];
                snprintf(spath, sizeof spath, "%s/07ACDB", iso_dir);
                if (resource_load(cd, spath, &a_cdb, &a_cdb_sz) || a_cdb_sz == 0) {
                    free(a_cdb); a_cdb = NULL; a_cdb_sz = 0;
                }
                snprintf(spath, sizeof spath, "%s/08ATIL", iso_dir);
                if (resource_load(cd, spath, &a_til, &a_til_sz) || a_til_sz == 0) {
                    free(a_til); a_til = NULL; a_til_sz = 0;
                }
            }
            memset(&map, 0, sizeof map);
            if (scrl_map_init(&map, a_cdb, a_cdb_sz, a_til, a_til_sz,
                              blobs[0], sizes[0], blobs[1], sizes[1],
                              blobs[2], sizes[2], blobs[3], sizes[3],
                              blobs[4], sizes[4], blobs[5], sizes[5],
                              NULL, 0) == 0 &&
                scrl_map_cached_planes(&map) > 0) {
                /* strip_w == 0: frozen 320x224 render, as before the parallax work. */
                unsigned strip_w = scrl_bg_strip_width(&map);
                unsigned draw_w = strip_w ? strip_w : PAN_WIDTH;
                if (is_ice && map.plane_count && map.planes[0].pixel_h >= PAN_HEIGHT) {
                    int top = initial.start_y - (int)PAN_HEIGHT / 2;
                    int last = (int)(map.planes[0].pixel_h - PAN_HEIGHT);
                    map.camera_y = top < 0 ? 0 : top > last ? last : top;
                    phase->scrl_origin_x = initial.start_x - (int)START_PX;
                    phase->fly_y_px = initial.start_y - map.camera_y + initial.hit.bot_right_y;
                }
                phase->scrl_bg_rgba = malloc((size_t)draw_w * PAN_HEIGHT * 4u);
                if (phase->scrl_bg_rgba &&
                    scrl_map_draw(&map, palette, phase->scrl_bg_rgba, draw_w,
                                  PAN_HEIGHT, 0u)) {
                    free(phase->scrl_bg_rgba);
                    phase->scrl_bg_rgba = NULL;
                }
                phase->scrl_bg_w = phase->scrl_bg_rgba ? strip_w : 0u;
                if (phase->ice) {
                    phase->ice->map=map;
                    memset(&map,0,sizeof map);
                    memcpy(phase->ice->palette,palette,sizeof phase->ice->palette);
                    for (k=0;k<6;++k) { phase->ice->map_data[k]=blobs[k]; blobs[k]=NULL; }
                    phase->ice->map_data[6]=a_cdb; a_cdb=NULL;
                    phase->ice->map_data[7]=a_til; a_til=NULL;
                }
            }
            scrl_map_free(&map);
            for (k = 0; k < 6u; k++) free(blobs[k]);
            free(a_cdb);
            free(a_til);
        }
    }
    phase->frame_rgba = malloc(PAN_PLANE_SIZE * 4);
    if (!phase->frame_rgba) {
        runway_assets_free(&assets);
        runway_phase_free(phase);
        return -1;
    }

    /* Anim banks: RUNWAY descriptors first; then STXTR records. */
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
                fprintf(stderr, "%s/04STXT contains no decodable actor frames\n", iso_dir);
                runway_assets_free(&assets);
                runway_phase_free(phase);
                return -1;
            }
        }
    }
    if (phase->runway_win_record && (load_gang_recordings(phase, &assets, palette + 2*1024) ||
        load_butterfly(phase, &assets, palette + 2*1024))) {
        runway_assets_free(&assets);
        runway_phase_free(phase);
        return -1;
    }
    if (phase->ice && (ice_load(phase,&assets,projection->init_address) || ice_navigation(phase))) {
        runway_assets_free(&assets); runway_phase_free(phase); return -1;
    }
    if (load_pan_course(phase, &assets)) {
        runway_assets_free(&assets);
        runway_phase_free(phase);
        return -1;
    }
    if(phase->course==COURSE_RIDE) {
        for(unsigned i=0;i<16;++i) {
            if(load_shape(&assets,palette+2048,0x232d34+12*i,&phase->swim_bubble_pose[i])) {
                runway_assets_free(&assets);runway_phase_free(phase);return -1;
            }
        }
        phase->course_code=assets.code;
        assets.code=NULL;
    }
    if (phase->course==COURSE_LOGS) {
        int error=load_butterfly(phase,&assets,palette+2048);
        for (unsigned i=0;!error && i<9;++i)
            error=load_shape(&assets,palette+2048,0x232214+12*i,&phase->log_head[i]);
        if (error) {runway_assets_free(&assets);runway_phase_free(phase);return -1;}
    }
    if (phase->course == COURSE_STAING) {
        const uint32_t animation[3] = {0x235df8,0x235d2c,0x23f3f4};
        for (unsigned kind=0; kind<3; ++kind) for (unsigned pose=0; pose<4; ++pose) {
            if (load_shape(&assets,palette+2048,animation[kind]+12*pose,&phase->flight_shapes[kind][pose])) {
                runway_assets_free(&assets);
                runway_phase_free(phase);
                return -1;
            }
        }
    }
    if(phase->course==COURSE_STAING){phase->course_code=assets.code;assets.code=NULL;}
    if (phase->course == COURSE_MOAT) {
        const uint32_t anim[2] = {0x2a3024,0x2a2be4};
        for (unsigned side=0; side<2; ++side) for (unsigned i=0; i<16; ++i) {
            if (load_shape(&assets, palette+2048, anim[side]+12*i, &phase->nut_pose[side][i])) {
                runway_assets_free(&assets);
                runway_phase_free(phase);
                return -1;
            }
        }
    }
    if (phase->ice) {
        phase->ice->code = assets.code; assets.code = NULL;
        phase->ice->textures = assets.textures; assets.textures = NULL;
    }
    runway_assets_free(&assets);
    anim_set_current(phase,
                     phase->anim_bank_count[RUNWAY_ANIM_BANK_FULL]
                         ? RUNWAY_ANIM_BANK_FULL : RUNWAY_ANIM_BANK_MORPH,
                     0);
    if (!phase->runway_win_record)
        mplay_seq_init(&phase->mplay,
                       phase->anim_bank_count[RUNWAY_ANIM_BANK_MORPH]
                           ? phase->anim_bank_count[RUNWAY_ANIM_BANK_MORPH] : 1u,
                       MPLAY_DEFAULT_DURATION);

    {
        if (phase->ice) {
            ice_camera(phase);
        } else if (pan_has_collision(phase)) {
            int y = pan_floor_at(phase, viewport_px(phase, phase->bones_x));
            if (y >= 0) phase->bones_y = (uint32_t)y * UNITS_PER_PIXEL;
            else phase->airborne = 1;
        } else {
            int tx = (int)(phase->bones_x / UNITS_PER_PIXEL) / (int)PAN_COLL_PIXELS_PER_TILE;
            phase->bones_y = ground_world_y(&phase->collision, tx);
        }
        phase->camera_y = phase->bones_y - 40u * UNITS_PER_PIXEL;
    }
    if (phase->runway_win_record)
        phase->state = phase->airborne ? RUNWAY_ST_AIR : RUNWAY_ST_WALK;
    snprintf(phase->status, sizeof phase->status, "%s %s", phase->stage_name,
             phase->state == RUNWAY_ST_WALK ? "walk" : phase->airborne ? "air" : "stand");
    return runway_phase_render(phase);
}

void runway_phase_free(runway_phase *phase) {
    unsigned b, i;
    if (!phase) return;
    ice_free(phase);
    /* bg_rgba aliases pan.bg_rgba — freed by pan_scroll_free. */
    phase->bg_rgba = NULL;
    pan_scroll_free(&phase->pan);
    for (unsigned i = 0; i < 4; i++) free(phase->gang_tracks[i].entries);
    for (unsigned i = 0; i < 256; i++) free(phase->gang_shapes[i].pixels);
    for (unsigned i = 0; i < 26; i++) free(phase->butterfly_anim[i].shape.pixels);
    for (unsigned side=0; side<2; ++side) for (unsigned i=0; i<16; ++i)
        free(phase->nut_pose[side][i].pixels);
    for (unsigned kind=0;kind<3;++kind) for (unsigned pose=0;pose<4;++pose)
        free(phase->flight_shapes[kind][pose].pixels);
    for(unsigned i=0;i<9;++i) free(phase->log_head[i].pixels);
    free(phase->swim_sprite);
    free(phase->swim_body);
    free(phase->claw_body);
    for(unsigned i=0;i<16;++i)free(phase->swim_bubble_pose[i].pixels);
    free(phase->course_code);
    free(phase->pan_blob);
    free(phase->scrl_bg_rgba);
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
    int x = phase->runway_win_record ? screen_x_at(phase, bones_x) : coll_probe_px(phase, bones_x);
    *gx = x - PAN_COLL_X_BIAS;
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

/* Return the displacement to the near face of a solid PAN wall. A fence post
 * can be narrower than one record's scroll. Sweep the crossed columns. */
static int runway_wall_shift(const runway_phase *phase, int first, int last, int direction) {
    int feet = (int)(phase->bones_y / UNITS_PER_PIXEL);
    int head = feet - phase->hit.bot_right_y + phase->hit.top_right_y;
    for (int x = first; direction > 0 ? x <= last : x >= last; x += direction) {
        if (x < 0 || x >= (int)PAN_WIDTH) continue;
        /* A two-pixel terrain step has just one exposed hixel. */
        int exposed = 0;
        for (int y = head; y <= feet; y += 2) {
            int neighbor = pan_collision_hixel_pixel(&phase->collision, x - direction, y);
            int face = neighbor >= 0 && neighbor != PAN_COLL_HIXEL_SOLID &&
                       pan_collision_hixel_pixel(&phase->collision, x, y) == PAN_COLL_HIXEL_SOLID;
            if (face && exposed) return x - direction - last;
            exposed = face;
        }
    }
    return 0;
}

static int move_blocked(const runway_phase *phase, uint32_t world_x) {
    int abs_px = (int)(world_x / UNITS_PER_PIXEL);
    int max_px = (int)(phase->finish_x / UNITS_PER_PIXEL) + 40;
    if (max_px < (int)PAN_WIDTH - 4) max_px = (int)PAN_WIDTH - 4;
    if (abs_px < 4 || abs_px >= max_px) return 1;
    if (phase->runway_win_record && world_x > phase->camera_x &&
        world_x - phase->camera_x >= (PAN_WIDTH / 2u - (unsigned)phase->hit.top_right_x) * UNITS_PER_PIXEL)
        return 1;
    if (phase->runway_win_record && world_x != phase->bones_x) {
        int direction = world_x < phase->bones_x ? -1 : 1;
        int side = direction < 0 ? phase->hit.top_left_x : phase->hit.top_right_x;
        int old_x = screen_x_at(phase, phase->bones_x) + side;
        int new_x = screen_x_at(phase, world_x) + side;
        if (runway_wall_shift(phase, old_x + direction, new_x, direction)) return 1;
    }
    if (phase->pan.count == 0u) {
        int px = coll_probe_px(phase, world_x);
        int tx = px / (int)PAN_COLL_PIXELS_PER_TILE;
        int dest_gty = pan_collision_map_ground_ty(&phase->collision, tx, 0);
        if (dest_gty < 0)
            return phase->generic_pan ? 0 : 1;
    }
    return 0;
}

/* §12l: _BonesPushOutOfWalls after integrate — HitChk origin = center (feet−12). */
static void apply_push_out(runway_phase *phase) {
    int gx, gy;
    world_to_grid_at(phase, phase->bones_x, phase->bones_y, &gx, &gy);
    gy -= phase->hit.bot_right_y;
    if (pan_bones_push_out_of_walls(&phase->collision, &gx, &gy, &phase->hit))
        grid_to_world_x_at(phase, gx, &phase->bones_x);
}

static int sat_sin(int angle) {
    /* The loader's 1024-entry sine table truncates to signed 8.8. */
    return (int)(256.0 * sin(6.2831853071795864769 * (angle & 1023) / 1024.0));
}

static int course_seek(runway_phase *p) {
    ++p->runway_clock;
    /* RIDE begins two blanks before the first PAN buffer swap. */
    unsigned frame = (p->runway_clock + (p->course==COURSE_RIDE ? 2u : 0u)) / 4u;
    if (frame >= p->pan.count) frame = p->pan.count - 1u;
    if (frame != p->pan.index && pan_scroll_seek(&p->pan, frame)) return -1;
    p->bg_rgba = p->pan.bg_rgba;
    p->collision = p->pan.collision;
    return 0;
}

static void course_screen_pos(runway_phase *p) {
    int x = 160 - (int)((int64_t)p->course_x*p->projection_px/65536);
    int y = 112 - (int)((int64_t)p->course_y*p->projection_px/65536);
    /* Keep a positive origin in the legacy compositor's unsigned projection. */
    p->camera_x = 320u * UNITS_PER_PIXEL;
    p->bones_x = (uint32_t)(x + 160) * UNITS_PER_PIXEL;
    p->bones_y = (uint32_t)(y + p->hit.bot_right_y) * UNITS_PER_PIXEL;
    p->fly_y_px = y;
}

/* _BoneOrder is an array of 40-byte configurations, each with the next
 * damage state and two independent reattachment destinations. */
static const uint32_t *ride_order(const runway_phase *p) {
    for(unsigned i=0;i<16;++i)if(p->swim_order[i][0]==p->swim_config)return p->swim_order[i];
    return NULL;
}

static int ride_distance(const int xyz[3],int x,int y,int z) {
    int dx=(xyz[0]-x)>>8,dy=(xyz[1]-y)>>8,dz=(xyz[2]-z)>>8;
    return dx*dx+dy*dy+dz*dz;
}

static void ride_joints(runway_phase *p,int32_t points[FINALE_BODY_JOINTS][3]) {
    uint32_t tracks=(p->swim_input&0xf000u)?0x236cd4u:0x237160u;
    if(tracks!=p->swim_tracks){p->swim_tracks=tracks;p->swim_motion_tick=0;}
    stage_body_pose pose={{p->course_x,p->course_y,0},{p->swim_pitch,p->swim_yaw,p->swim_roll},
                          {0,0,0},256,p->swim_motion_tick++,p->swim_config,tracks};
    stage_body_joints(p->swim_body,&pose,points);
}

static int course_obj_hit(const int32_t points[FINALE_BODY_JOINTS][3],int x,int y,int z,int radius) {
    /* BonesObjHitChk 2446f0, shifted squared distances. Order matters because
     * the unconditional root test precedes the configured arm/leg samples. */
    if(ride_distance(points[0],x,y,z)<0xbb8) return 1;
    static const struct {unsigned joint,radius;} probes[]={
        {5,0x7d0},{2,0x7d0},{12,0xbb8},{19,0xbb8},{13,0x7d0},{20,0x7d0},{9,0x7d0},{16,0x7d0}};
    for(unsigned i=0;i<sizeof probes/sizeof probes[0];++i){
        unsigned j=probes[i].joint;
        if((points[j][0]||points[j][1]||points[j][2]) &&
           ride_distance(points[j],x,y,z)<(int)probes[i].radius+radius) return 1;
    }
    return 0;
}

static void ride_eject(runway_phase *p,const int32_t points[FINALE_BODY_JOINTS][3]) {
    int count=(RUNWAY_SKEL_MAX-p->skel.skeletism)>>16;
    while(count-- && p->swim_detached<8) {
        const uint32_t *row=ride_order(p);
        if(!row || !row[2]) break;
        unsigned n=p->swim_detached++,joint=(row[2]-0x270e9c)/168;
        p->swim_bones[n].joint=joint;
        p->swim_bones[n].age=0;
        p->swim_bones[n].attract=0;
        p->swim_bones[n].x=points[joint][0];p->swim_bones[n].y=points[joint][1];p->swim_bones[n].z=points[joint][2];
        p->swim_bones[n].vx=-p->swim_bones[n].x/60;
        p->swim_bones[n].vy=-p->swim_bones[n].y/60;
        p->swim_bones[n].vz=-p->swim_bones[n].z/60;
        p->swim_config=row[1];
        p->swim_keyhole=20;
    }
}

static int ride_attract_step(int delta) {
    int v=delta/8;
    return v< -6553 ? -6553 : v>6553 ? 6553 : v;
}

static void ride_parts(runway_phase *p,const saturn_pad *pad,const int32_t points[FINALE_BODY_JOINTS][3],int cx,int cy) {
    if(p->swim_keyhole)--p->swim_keyhole;
    if(pad->pressed&0x0400u){
        p->swim_pump+=0x30a3;
        if(p->swim_pump>65536)p->swim_pump=65536;
    } else {
        p->swim_pump-=131+(p->swim_pump>>6);
        if(p->swim_pump<0)p->swim_pump=0;
    }
    int range=p->swim_pump>>8; range=range*range*9*76/256;
    for(unsigned i=0;i<p->swim_detached;){
        unsigned joint=p->swim_bones[i].joint;
        int age=(int)++p->swim_bones[i].age;
        const uint32_t *row=ride_order(p);
        uint32_t body_part=0x270e9c+168*joint;
        unsigned receptor=row && row[5]==body_part ? 4 : row && row[8]==body_part ? 7 : 0;
        unsigned target=receptor ? (row[receptor+1]-0x270e9c)/168 : joint;
        int dist=ride_distance(points[target],p->swim_bones[i].x,p->swim_bones[i].y,p->swim_bones[i].z);
        if(receptor && dist<range)p->swim_bones[i].attract=1;
        if(p->swim_bones[i].attract && receptor) {
            if(dist<0x28a4 && !p->swim_keyhole){
                p->swim_config=row[receptor+2];p->swim_keyhole=20;
                p->swim_bones[i]=p->swim_bones[--p->swim_detached];++p->swim_recovered;continue;
            }
            p->swim_bones[i].x+=ride_attract_step(points[target][0]-p->swim_bones[i].x);
            p->swim_bones[i].y+=ride_attract_step(points[target][1]-p->swim_bones[i].y);
            p->swim_bones[i].z+=ride_attract_step(points[target][2]-p->swim_bones[i].z);
        } else {
            if(age<=60) {
                p->swim_bones[i].x+=p->swim_bones[i].vx;p->swim_bones[i].y+=p->swim_bones[i].vy;
                p->swim_bones[i].z+=p->swim_bones[i].vz;
            }
            p->swim_bones[i].x+=cx;p->swim_bones[i].y+=cy;
        }
        ++i;
    }
}

static void ride_bubbles(runway_phase *p,const int32_t points[FINALE_BODY_JOINTS][3]) {
    if(p->swim_burst<5 && !p->swim_bubbles_left && p->pan.index>p->swim_burst_frame[p->swim_burst]){
        ++p->swim_burst;p->swim_bubbles_left=5;p->swim_bubble_wait=0;
    }
    if(p->swim_bubble_wait)--p->swim_bubble_wait;
    if(p->swim_bubbles_left && !p->swim_bubble_wait && p->swim_bubble_count<6){
        unsigned i=p->swim_bubble_count++;
        int target=-(sat_sin(320*(int)++p->swim_spawned)*0xf3cf)>>8;
        p->swim_bubbles[i].x=0;p->swim_bubbles[i].y=-156038;p->swim_bubbles[i].z=0;
        p->swim_bubbles[i].vx=target/100;p->swim_bubbles[i].vy=156038/100;
        p->swim_bubbles[i].vz=19600;p->swim_bubbles[i].age=0;
        --p->swim_bubbles_left;p->swim_bubble_wait=21;
    }
    for(unsigned i=0;i<p->swim_bubble_count;){
        int x=(p->swim_bubbles[i].x+=p->swim_bubbles[i].vx);
        int y=(p->swim_bubbles[i].y+=p->swim_bubbles[i].vy);
        int z=(p->swim_bubbles[i].z+=p->swim_bubbles[i].vz);
        p->swim_bubbles[i].vz-=393;++p->swim_bubbles[i].age;
        if(course_obj_hit(points,x,y,z,0xf3cf)){
            ++p->swim_pickups;runway_skel_add(&p->skel,0x3200);
            phase_sound_event(p, 0x17); /* RIDE CBluePoof::Brain pickup. */
        } else if(z>=-0x60000){++i;continue;}
        p->swim_bubbles[i]=p->swim_bubbles[--p->swim_bubble_count];
    }
}

static int step_ride(runway_phase *p, const saturn_pad *pad) {
    if (course_seek(p)) return -1;
    int current_x,current_y;
    /* The framebuffer swap runs ahead of the render-list/object scheduler.
     * CChannel::UpdateValue sees the preceding task's CurrentFrame. */
    ride_current_step(&p->current,(p->runway_clock+1)/4,&current_x,&current_y);
    int32_t joints[FINALE_BODY_JOINTS][3]={{0}};
    ride_joints(p,joints);
    ride_parts(p,pad,joints,current_x,current_y);
    ride_bubbles(p,joints);
    int x = 160 - (int)((int64_t)p->course_x*42/65536);
    int y = 112 - (int)((int64_t)p->course_y*42/65536);
    int thrust_x=(-5242*sat_sin(p->swim_yaw))>>8;
    int thrust_y=(-5242*sat_sin(p->swim_pitch))>>8;
    int dx=current_x+thrust_x,dy=current_y+thrust_y;
    p->swim_input=pad->held;
    if (!p->wall_tick) {
        p->swim_wall_mask = (uint16_t)ride_wall_coverage(&p->collision, x, y);
        if (p->swim_wall_mask) {
            p->wall_tick=27;
            phase_sound_event(p, 0x2a); /* CCurrent::Brain. */
            runway_sfx_emit(0x1b); /* BkgHitWhileSwimming follows in the same VBlank. */
            ++p->hazards.hits_applied;
            runway_skel_hit(&p->skel,0x3200);
            ride_eject(p,joints);
            p->swim_dx=-p->course_x/20;p->swim_dy=-p->course_y/20;
        }
    }
    if (p->wall_tick) {
        unsigned blend=p->wall_tick>16 ? 16u : p->wall_tick-1;
        /* CCurrent yields ten unblended rebounds, sixteen interpolations
         * (256 down to 16) then yields the last vector one more time. */
        if (p->wall_tick==1)blend=1;
        dx=thrust_x+(((p->swim_dx-thrust_x)*(int)blend)>>4);
        dy=thrust_y+(((p->swim_dy-thrust_y)*(int)blend)>>4);
        if(p->wall_tick>17){
            unsigned angle=p->current.wall_angle[p->swim_wall_mask];
            p->swim_roll-=24*sat_sin((int)angle+256)/256;
        }
    }
    /* Bones::BkgHit restores its normal state after 11 yields although the
     * independently scheduled CCurrent keeps blending the rebound. */
    if(p->wall_tick<=16) {
        if (pad->held & 0xf000u) {
            if ((pad->held & 0x1000u) && p->swim_pitch < 1088) p->swim_pitch += 4;
            if ((pad->held & 0x2000u) && p->swim_pitch > 960) p->swim_pitch -= 4;
            if ((pad->held & 0x4000u) && p->swim_yaw < 608) p->swim_yaw += 4;
            if ((pad->held & 0x8000u) && p->swim_yaw > 416) p->swim_yaw -= 4;
        } else {
            p->swim_pitch += p->swim_pitch < 1024 ? 1 : p->swim_pitch > 1024 ? -1 : 0;
            p->swim_yaw += p->swim_yaw < 512 ? 1 : p->swim_yaw > 512 ? -1 : 0;
        }
        p->swim_roll=(p->swim_yaw-512)*2;
    }
    if(p->wall_tick)--p->wall_tick;
    p->course_x += dx;
    p->course_y += dy;
    course_screen_pos(p);
    p->state = RUNWAY_ST_WALK;
    if (p->skel.died) p->state = RUNWAY_ST_LOST;
    else if (p->pan.index > 0x95bu && p->stage_exit_wait++)
        p->state = p->swim_keyhole || p->swim_config!=p->swim_order[0][0] ? RUNWAY_ST_LOST : RUNWAY_ST_FINISHED;
    snprintf(p->status, sizeof p->status, "RIDE PAN=%u wall=%x hits=%u", p->pan.index,
             (unsigned)p->swim_wall_mask, p->hazards.hits_applied);
    return runway_phase_render(p);
}

static int moat_start_clip(runway_phase *p, unsigned clip) {
    size_t start = (size_t)p->moat_sectors[clip]*2048u;
    size_t end = clip == 14 ? p->pan_blob_size : (size_t)p->moat_sectors[clip+1]*2048u;
    pan_scroll next;
    if (pan_scroll_init(&next, p->pan_blob+start, end-start, p->pan.palette4)) return -1;
    pan_scroll_free(&p->pan);
    p->pan = next;
    p->moat_clip = clip;
    p->moat_time = 0;
    const unsigned list = clip < 10 ? 0 : clip-9;
    unsigned i = 0;
    while (i < p->pan_event_count && (p->pan_events[i].frame >> 16) < list) ++i;
    p->pan_event_index = i;
    return 0;
}

static int moat_pan_tick(runway_phase *p) {
    /* CSparky::Brain waits for the current animation before selecting its next action. */
    unsigned record = ++p->moat_time / 4u;
    if (record >= p->pan.count) {
        /* Slot 0 is filled with clip 12 by PreloadPanAnim(0,12).
         * CSparky waits for the final shake and two huffs before Breakout. */
        unsigned next=12;
        if (p->moat_escape) {
            if (++p->moat_escape==4) next=11;
        } else if (p->nuts_released==3) {
            next=p->moat_shake==13 ? 13u : 14u;
            p->moat_escape=1;
        } else if (p->moat_shake) {
            next=p->moat_shake;
            p->moat_shake=0;
            p->moat_huffs=0;
            p->moat_growls=0;
        } else if (p->moat_clip==12 && ++p->moat_huffs==2) {
            next=p->moat_growls++==5 ? 10u : p->sparky_target;
            if (next==10) p->moat_growls=0;
            p->moat_huffs=0;
        }
        if (moat_start_clip(p, next)) return -1;
    } else if (record != p->pan.index) {
        if (pan_scroll_seek(&p->pan,record)) return -1;
        const unsigned list = p->moat_clip < 10 ? 0 : p->moat_clip-9;
        const unsigned key = (list << 16) | record;
        unsigned i = p->pan_event_index;
        if (i < p->pan_event_count && p->pan_events[i].frame == key) {
            phase_sound_event(p, p->pan_events[i].mask);
            ++p->pan_event_index;
        }
    }
    p->bg_rgba = p->pan.bg_rgba;
    p->collision = p->pan.collision;
    return 0;
}

static unsigned next_course_event(runway_phase *p) {
    unsigned i = p->pan_event_index;
    if (i >= p->pan_event_count || p->pan_events[i].frame != p->pan.index) return 0;
    ++p->pan_event_index;
    return p->pan_events[i].mask;
}

static void flight_spawn(runway_phase *p,int x,int y,int z,unsigned delay,int red) {
    if (p->flight_count >= sizeof p->flight_objects/sizeof p->flight_objects[0]) return;
    unsigned n=p->flight_count++;
    p->flight_objects[n].x=x;
    p->flight_objects[n].y=y;
    p->flight_objects[n].z=z;
    p->flight_objects[n].birth=p->runway_clock+delay;
    p->flight_objects[n].red=red;
}

/* CBflyManager and CRedThingManager schedule CObject::Delay in VBlanks. */
static void flight_patterns(runway_phase *p, unsigned flags) {
    for (unsigned bit=2; bit<=0x10000; bit*=2) {
        if (!(flags & bit)) continue;
        if (bit==2 || bit==4 || bit==8 || bit==0x2000 || bit==0x4000) {
            unsigned step=bit==8?128u:bit>=0x2000?64u:96u;
            for(unsigned k=0;k*step<1024u;++k) {
                int a=(int)(k*step)*(bit==4?-1:1);
                int x=sat_sin(a)*256;
                unsigned delay=k*(bit==8?1u:31u);
                if (bit==0x4000) {
                    flight_spawn(p,x+65536,0x1999,0x50000,delay,1);
                    flight_spawn(p,x-65536,0x1999,0x50000,delay,1);
                } else {
                    int y=bit==0x2000 ? -0x1999 : sat_sin(a+256)*(bit==8?65536:0x9999)/256;
                    flight_spawn(p,x,y,bit==0x2000?0x50000:0x60000,delay,0);
                }
            }
        } else if (bit==0x100 || bit==0x400) {
            for(unsigned i=0;i<5;++i) flight_spawn(p,bit==0x100?0xcccc:-0xcccc,0x1999,0x60000,21*i,0);
        }
    }
    if (flags&0x20000) flight_spawn(p,0x1cccc,0x3333,0xcccc,0,0);
    if (flags&0x40000) flight_spawn(p,-0x1cccc,0x3333,0xcccc,0,0);
    if (flags&0x800000) flight_spawn(p,0xe666,0x1999,0x60000,0,1);
    if (flags&0x1000000) flight_spawn(p,0,-0x1999,0x60000,0,1);
    if (flags&0x2000000) flight_spawn(p,-0xe666,0x6666,0x60000,0,1);
}

static int flight_limit(int x,int lo,int hi) { return x<lo ? lo : x>hi ? hi : x; }

static void move_claws(const runway_phase *p,runway_claws *m,unsigned aim) {
    if (aim!=m->aim) {
        m->aim=aim; m->aim_ticks=0;
        m->claw_dip=-flight_limit(abs(p->claw_aim[aim].x-m->claw_x)/4,0,0xcccc);
    } else if (m->claw_dip<0) m->claw_dip+=0x147;
    ++m->aim_ticks;
    int64_t vx=(int64_t)(p->claw_aim[aim].x-m->claw_x)*(16+m->aim_ticks);
    int64_t vy=(int64_t)(p->claw_aim[aim].y+m->claw_dip-m->claw_y)*(56+m->aim_ticks);
    m->claw_ax=(int)(vx>>16);
    m->claw_vx=flight_limit(((m->claw_vx+m->claw_ax)*250)>>8,-6553,6553);
    m->claw_vy=flight_limit(((m->claw_vy+(int)(vy>>16))*240)>>8,-6553,6553);
    m->claw_x=flight_limit(m->claw_x+m->claw_vx,-0x29999,0x29999);
    m->claw_y=flight_limit(m->claw_y+m->claw_vy,0xb333,0x28000);
}

/* CClaws::MoveBones @261a00 attaches the arithmetic mean of the two
 * hand joints to the talons. CSGBfly and CRedThing query the CRender screen
 * origins of BneHips (27c858) and BneLF/BneRF (27ccf0/27d188). */
static int flight_project(int xy,int z) {
    /* The projection of the original hips, feet and talons gives a
     * 256-pixel focal length at four world units from the origin. */
    int depth=4*65536+z;
    if(depth<=0)return 0;
    int64_t numerator=-(int64_t)xy*256;
    return (int)(numerator<0 ? (numerator-depth+1)/depth : numerator/depth);
}

static void flight_joint_positions(runway_phase *p,int32_t points[FINALE_BODY_JOINTS][3]) {
    uint32_t tracks=p->claw_tracks[p->claws.claw_ax>=79 ? 2u : p->claws.claw_ax< -78 ? 1u : 0u];
    if(tracks!=p->claw_motion){p->claw_motion=tracks;p->claw_tick=0;}
    int difference=p->claws.claw_x-p->claw_aim[p->claws.aim].x;
    int turn=difference>0 ? 5*p->claws.claw_dip : difference<0 ? -5*p->claws.claw_dip : 0;
    p->claws.claw_roll=((p->claws.claw_roll-(turn>>8))*252)>>8;
    stage_body_pose pose={{0,0,0},{0,0,0},{0,0,p->claws.claw_roll>>8},256,p->claw_tick++,0,tracks};
    stage_body_joints(p->claw_body,&pose,points);
    int offset[3];
    int claw[3]={p->claws.claw_x+sat_sin((int)p->runway_clock*19)*8,
                 p->claws.claw_y+sat_sin((int)p->runway_clock*25)*16,0};
    for(unsigned k=0;k<3;++k)offset[k]=claw[k]-(points[10][k]+points[17][k])/2;
    p->course_x=offset[0];p->course_y=offset[1];
    for(unsigned i=0;i<21;++i)for(unsigned k=0;k<3;++k)points[i][k]+=offset[k];
    const unsigned ids[3]={6,13,20};
    for(unsigned i=0;i<3;++i)for(unsigned k=0;k<2;++k)
        p->flight_joints[i][k]=flight_project(points[ids[i]][k],points[ids[i]][2]);
}

static int step_staing(runway_phase *p,const saturn_pad *pad) {
    if (course_seek(p)) return -1;
    unsigned flags = p->stage_exit_wait ? 0 : next_course_event(p);
    if (flags&1u) { ++p->section; ++p->glass_hits; phase_sound_event(p, 0x2a); }
    unsigned loss=(flags>>26)&15u;
    static const unsigned damage[4]={0x4000,0x8000,0xc000,0x10000};
    for(unsigned i=0;i<4;++i) if(loss&(1u<<i)) runway_skel_loose(&p->skel,(int32_t)damage[i]);
    flight_patterns(p,flags);
    unsigned aim=(pad->held&0x4000u)?8u:(pad->held&0x8000u)?4u:0u;
    aim |= (pad->held&0x1000u)?2u:(pad->held&0x2000u)?1u:0u;
    move_claws(p,&p->claws,aim);
    int32_t joints[FINALE_BODY_JOINTS][3]={{0}};
    flight_joint_positions(p,joints);
    for (unsigned i=0;i<p->flight_count;) {
        if(p->runway_clock<p->flight_objects[i].birth){++i;continue;}
        int z=(p->flight_objects[i].z-=1638);
        int band=(z+0x28000)>>14;
        int red=p->flight_objects[i].red;
        unsigned age=p->runway_clock-p->flight_objects[i].birth;
        int sx=flight_project(p->flight_objects[i].x,z);
        int sy=flight_project(p->flight_objects[i].y,z);
        int hip_dx=abs(sx-p->flight_joints[0][0]),hip_dy=abs(sy-p->flight_joints[0][1]);
        int feet_dx=abs(sx-(p->flight_joints[1][0]+p->flight_joints[2][0])/2);
        int feet_dy=abs(sy-(p->flight_joints[1][1]+p->flight_joints[2][1])/2);
        int contact=red ? band==4 && (age&3u)==2u && hip_dx<28 && hip_dy<64 :
            (band==7 || band==8) && (((age&3u)==1u && hip_dx<28 && hip_dy<64) ||
                                      ((age&3u)==3u && feet_dx<28 && feet_dy<28));
        if(!contact && !red && (band==7 || band==8) && !(age&3u))
            contact=course_obj_hit(joints,p->flight_objects[i].x,p->flight_objects[i].y,joints[0][2],0xbb8);
        if (contact) {
            if (p->flight_objects[i].red) {
                ++p->red_hits;
                phase_sound_event(p, 0x2b);
                runway_skel_loose(&p->skel,0x2300);
            } else {
                ++p->rewards;
                phase_sound_event(p, 0x17);
                runway_skel_add(&p->skel,0xa00);
            }
        } else if (band>=(red?4:7)) {++i;continue;}
        p->flight_objects[i]=p->flight_objects[--p->flight_count];
    }
    if (flags&0x80000000u) p->stage_exit_wait=31;
    course_screen_pos(p);
    p->state=RUNWAY_ST_WALK;
    if (p->skel.died) p->state=RUNWAY_ST_LOST;
    else if (p->stage_exit_wait && !--p->stage_exit_wait) p->state=RUNWAY_ST_FINISHED;
    snprintf(p->status,sizeof p->status,"STAING PAN=%u glass=%u red=%u blue=%u",p->pan.index,p->glass_hits,p->red_hits,p->rewards);
    return runway_phase_render(p);
}

static void draw_flight_objects(runway_phase *p) {
    for (unsigned i=0;i<p->flight_count;++i) {
        if (p->runway_clock<p->flight_objects[i].birth) continue;
        int z=p->flight_objects[i].z;
        int x=160+flight_project(p->flight_objects[i].x,z);
        int y=112+flight_project(p->flight_objects[i].y,z);
        unsigned type=p->flight_objects[i].red ? 2u : (z+0x28000)/16384 <= 8 ? 1u : 0u;
        const runway_gang_shape *f=&p->flight_shapes[type][((p->runway_clock-p->flight_objects[i].birth)/6)%4];
        runway_compose_blit_sprite(p->frame_rgba,PAN_WIDTH,PAN_HEIGHT,f->pixels,f->width,f->height,x-f->anchor_x,y-f->anchor_y);
    }
}

static void log_spawn(runway_phase *p,unsigned kind) {
    static const int init[4][2]={{0x68000,0x68000},{-0x90000,0},{-0x80000,0x20000},{-0x80000,0x20000}};
    ++p->log_spawned[kind];
    if (p->log_object_count==sizeof p->log_actors/sizeof p->log_actors[0]) return;
    unsigned n=p->log_object_count++;
    memset(&p->log_actors[n],0,sizeof p->log_actors[n]);
    p->log_actors[n].ox=p->log_actors[n].x=p->log_actors[n].from_x=init[kind][0];
    p->log_actors[n].oy=p->log_actors[n].y=p->log_actors[n].from_y=init[kind][1];
    p->log_actors[n].type=kind;
    p->log_actors[n].knot=1; /* CTraj::SetS calls NextMove before its first GuideMe. */
}

static void log_update(runway_phase *p) {
    for (unsigned i=0;i<p->log_object_count;) {
        unsigned k=p->log_actors[i].type;
        unsigned age=++p->log_actors[i].age;
        if (k==3) {
            p->log_actors[i].x+=0x170a;
            p->log_actors[i].y+=0x20;
        } else {
            unsigned n=p->log_actors[i].knot, duration=p->log_tracks[k][n].ticks;
            int tx=p->log_actors[i].ox+p->log_tracks[k][n].x;
            int ty=p->log_actors[i].oy+p->log_tracks[k][n].y;
            unsigned elapsed=++p->log_actors[i].elapsed;
            p->log_actors[i].x=p->log_actors[i].from_x+(int)((int64_t)(tx-p->log_actors[i].from_x)*elapsed/duration);
            p->log_actors[i].y=p->log_actors[i].from_y+(int)((int64_t)(ty-p->log_actors[i].from_y)*elapsed/duration);
            if (elapsed==duration) {
                p->log_actors[i].from_x=tx;
                p->log_actors[i].from_y=ty;
                p->log_actors[i].elapsed=0;
                switch (p->log_tracks[k][n].action) {
                case 1: p->log_actors[i].knot=n+1; break;
                case 2: p->log_actors[i].knot=p->log_tracks[k][n].target+1; break;
                default:
                    p->log_actors[i].knot=1;
                    p->log_actors[i].ox=tx;
                    p->log_actors[i].oy=ty;
                    break;
                }
            }
        }
        int dx=(p->course_x-p->log_actors[i].x)/256;
        int dy=(p->course_y-p->log_actors[i].y)/256;
        if ((k==3 || age%3==0) && (int64_t)dx*dx+(int64_t)dy*dy<(k==3?0xfa0:0x2710)+0xbb8) {
            runway_skel_add(&p->skel,k==3?0x20000:0x6400);
            ++p->rewards;
            p->sound_event_id=k==3?23:43;
            if (k == 3) runway_sfx_emit(0x17); /* LOGS CBlueHead::Brain. */
        } else if (k==3 ? p->log_actors[i].x<=0xa0000 :
                   age<=718 && llabs((int64_t)p->log_actors[i].x*21/65536)<=200) {
            ++i;
            continue;
        }
        p->log_actors[i]=p->log_actors[--p->log_object_count];
    }
}

static void draw_log_objects(runway_phase *p) {
    for (unsigned i=0;i<p->log_object_count;++i) {
        const runway_gang_shape *f;
        if (p->log_actors[i].type==3) f=&p->log_head[p->log_actors[i].age/6%9];
        else {
            unsigned elapsed=p->log_actors[i].age%p->butterfly_anim[25].end_tick;
            unsigned j=0;
            while(elapsed>=p->butterfly_anim[j].end_tick) ++j;
            f=&p->butterfly_anim[j].shape;
        }
        int x=160-(int)((int64_t)p->log_actors[i].x*21/65536);
        int y=112-(int)((int64_t)p->log_actors[i].y*21/65536);
        runway_compose_blit_sprite(p->frame_rgba,PAN_WIDTH,PAN_HEIGHT,f->pixels,f->width,f->height,
                                   x-f->anchor_x,y-f->anchor_y);
    }
}

static int step_logs(runway_phase *p, const saturn_pad *pad) {
    if (course_seek(p)) return -1;
    unsigned flags = next_course_event(p);
    static const unsigned sounds[5] = {41,47,42,48,50};
    for (unsigned i = 0; i < 5; ++i)
        if (flags & (4u << i)) phase_sound_event(p, sounds[i]);
    for (unsigned k=0;k<3;++k) if (flags&(0x80u<<k)) log_spawn(p,k);
    if (flags&0x1000u) log_spawn(p,3);
    if (flags & 1u) p->section = 1;
    int dir = !!(pad->held & 0x4000u) - !!(pad->held & 0x8000u);
    int scale = (int)p->projection_px;
    int32_t nx = p->course_x + dir*(int32_t)WALK_STEP_RIGHT;
    if (p->pan.index < 120 && nx > -65536) nx = -65536;
    int x = 160-(int)((int64_t)nx*scale/65536);
    int foot = 112-(int)((int64_t)p->course_y*scale/65536)+p->hit.bot_right_y;
    if (x >= p->hit.top_right_x && x < 320+p->hit.top_left_x) p->course_x = nx;
    else x = 160-(int)((int64_t)p->course_x*scale/65536);
    if (dir) p->facing = -dir;
    if (!p->airborne && (pad->pressed & 0x0100u)) {
        p->moat_vy = -p->jump_impulse;
        p->spring_ticks = (uint8_t)p->boost_ticks;
        p->sfx_flags |= SFX_JUMP;
    }
    p->moat_vy -= SIDESCRL_PHYS_GRAVITY_ACCEL;
    if (p->spring_ticks && p->moat_vy > 0 && (pad->held & 0x0100u)) {
        p->moat_vy += SIDESCRL_PHYS_GRAVITY_ACCEL/2;
        --p->spring_ticks;
    } else p->spring_ticks = 0;
    int floor = pan_collision_floor(&p->collision,x,foot > 221 ? 221 : foot < 0 ? 0 : foot);
    p->course_y += p->moat_vy;
    if (p->moat_vy <= 0 && floor >= 0 &&
        112-(int)((int64_t)p->course_y*scale/65536)+p->hit.bot_right_y >= floor) {
        p->course_y = (112+p->hit.bot_right_y-floor)*65536/scale;
        p->moat_vy = 0;
        p->airborne = 0;
    } else p->airborne = 1;
    log_update(p);
    course_screen_pos(p);
    p->state = p->airborne ? RUNWAY_ST_AIR : dir ? RUNWAY_ST_WALK : RUNWAY_ST_STAND;
    if (p->fly_y_px > 252) p->state = RUNWAY_ST_LOST;
    if (p->pan.index > 0xb28u) {
        p->section = 2;
        /* EvalSuccess tests the junction with the body at SH coordinates
         * (-232652,150077,0). Keep the screen-space foot/torso separated. */
        int dx = (p->course_x + 232652) / 256;
        int dy = (p->course_y - 150077) / 256;
        if (p->skel.skeletism == RUNWAY_SKEL_MAX && dx*dx+dy*dy < 0x7d0 + 0xbb8)
            p->state = RUNWAY_ST_FINISHED;
    }
    snprintf(p->status,sizeof p->status,"LOGS PAN=%u event=%u x=%d y=%d",p->pan.index,p->pan_event_index,x,p->fly_y_px);
    return runway_phase_render(p);
}

static int step_moat(runway_phase *p, const saturn_pad *pad) {
    if (moat_pan_tick(p)) return -1;
    if (p->frames<=5) return runway_phase_render(p);
    if (p->moat_clip == 11 && p->pan.index >= 120) {
        phase_sound_event(p, 0x18); /* EndLevel(0), success branch at 0x2AE82C. */
        p->state = RUNWAY_ST_FINISHED;
        snprintf(p->status,sizeof p->status,"MOAT Sparky released PAN=%u",p->pan.index);
        return runway_phase_render(p);
    }
    int move = (pad->held & 0x4000u) ? 1 : (pad->held & 0x8000u) ? -1 : 0;
    p->tall = (pad->held & 0x2000u) != 0;
    if (move) p->facing = -move;
    /* InAir modifier accelerates by 0x20c to a maximum of 0xf5c. */
    if (p->airborne) p->moat_vx=flight_limit(p->moat_vx+move*524,-3932,3932);
    else p->moat_vx=move*(int32_t)WALK_STEP_RIGHT;
    int32_t next_x = p->course_x+p->moat_vx;
    int x = 160 - next_x/2048, foot = 112 - p->course_y/2048 + p->moat_floor_screen;
    if (pan_collision_hixel_pixel(&p->collision,x+move*-p->hit.top_right_x,foot-5) != 3)
        p->course_x = next_x;
    else x = 160-p->course_x/2048;
    if (!p->airborne && (pad->pressed & 0x0200u)) {
        p->moat_vy = -p->jump_impulse;
        p->spring_ticks = (uint8_t)p->boost_ticks;
        p->airborne = 1;
        p->sfx_flags |= SFX_JUMP;
    }
    int32_t previous_y = p->course_y;
    p->moat_vy -= SIDESCRL_PHYS_GRAVITY_ACCEL;
    if (p->moat_vy < -13107) p->moat_vy=-13107;
    if (p->spring_ticks && p->moat_vy > 0 && (pad->held & 0x0200u)) {
        p->moat_vy += SIDESCRL_PHYS_GRAVITY_ACCEL/2;
        --p->spring_ticks;
    } else p->spring_ticks = 0;
    p->course_y += p->moat_vy;
    int floor = pan_collision_floor(&p->collision,x,foot-1);
    if (p->moat_vy <= 0 && floor >= 0 && 112 - p->course_y/2048 + p->moat_floor_screen >= floor) {
        p->course_y = (112 + p->moat_floor_screen-floor)*2048;
        p->moat_vy = 0;
        p->airborne = 0;
    } else p->airborne = 1;
    for (unsigned i=0; i<9; ++i) {
        int64_t dx = (int64_t)p->course_x-p->sparky_targets[i].x;
        int64_t dy = (int64_t)p->course_y-p->sparky_targets[i].y;
        if (dx*dx+dy*dy < (int64_t)0xf000*0xf000)
            p->sparky_target = p->sparky_targets[i].id;
    }
    for (unsigned side=0; side<2; ++side) {
        if (p->nut_busy[side]) --p->nut_busy[side];
        if (!p->nuts[side].turns) continue;
        int32_t nx=p->nuts[side].x, ny=p->nuts[side].y;
        int dx=p->course_x-nx;
        if (dx <= -0xf000 || dx >= 0xf000 || p->moat_vy > 0) continue;
        /* CPlatSvrxxYy top face, platform +0x80 and Bones foot offset. */
        int32_t top = ny+0x5000-p->moat_floor_world;
        if (previous_y < top || p->course_y > top) continue;
        int landed = p->moat_vy < -SIDESCRL_PHYS_GRAVITY_ACCEL;
        p->course_y=top; p->airborne=0; p->moat_vy=0;
        if (!p->nut_busy[side] && (landed || p->tall)) {
            p->nut_busy[side] = 16*6;
            p->moat_shake = side ? 14 : 13;
            if (--p->nuts[side].turns == 0) p->nuts_released |= 1u<<side;
        }
    }
    course_screen_pos(p);
    p->bones_y=(uint32_t)(p->fly_y_px+p->moat_floor_screen)*UNITS_PER_PIXEL;
    p->state = p->airborne ? RUNWAY_ST_AIR : move ? RUNWAY_ST_WALK : RUNWAY_ST_STAND;
    snprintf(p->status,sizeof p->status,"MOAT Sparky=%u PAN=%u nuts=%u/%u",
             p->moat_clip,p->pan.index,p->nuts[0].turns,p->nuts[1].turns);
    return runway_phase_render(p);
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
     * RUNWAY calls EvalSuccess from the final PAN trigger.
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
    if (phase->ice) return ice_step(phase,pad);

    runway_skel_tick_cooldown(&phase->skel);
    if (phase->course == COURSE_STAING) return step_staing(phase,pad);
    if (phase->course == COURSE_LOGS) return step_logs(phase, pad);
    if (phase->course == COURSE_RIDE) return step_ride(phase, pad);
    if (phase->course == COURSE_MOAT) return step_moat(phase, pad);

    int32_t input_dx = 0;
    /*
     * RUNWAY InputBrain writes BonesPos.x in Saturn coordinates. The host
     * projects positive world X to the right, so apply the opposite sign.
     * §12m tall: MSG_DOWN → 0x204128(0xc) + BrainSwitch_25d534 → TallSet_26D784.
     */
    {
        uint8_t input0 = 0, input4 = 0;
        if (pad->held & 0x4000) input0 |= 0x40; /* L */
        if (pad->held & 0x8000) input0 |= 0x80; /* R */
        if (pad->held & 0x2000) input0 |= 0x20; /* Down */
        if (pad->pressed & 0x0200) input4 |= 0x02; /* C edge */
        bbs_emit_ctx emit_ctx = { phase };
        uint32_t delta = 0;
        runway_input_step(&delta, (uint32_t)phase->tall, input0, input4, bbs_emit, &emit_ctx);
        input_dx = -(int32_t)delta;
    }

    if (bones_bbs_consume(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_STAND_BOOT, NULL) == 1) {
        phase->obj_flags |= 0x01u;
        phase->last_message = MSG_STAND_BOOT;
    }
    phase->sched_enqueued_0c = 0;
    if (bones_bbs_peek(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_DOWN, NULL) == 1) {
        /* Stand flag&2 path: _SoundEvent(0xc) @0x204128 then BrainSwitch_25d534. */
        if (!was_tall) {
            phase->sched_enqueued_0c = 1; /* legacy name: SoundEvent index 0xc fired */
            phase_sound_event(phase, SOUND_EVENT_DOWN);
        }
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
        phase->sfx_flags |= SFX_C;
        runway_sfx_emit(0x23);
        stage_enemies_fire_blues(&phase->enemies, phase->bones_x,
                                 phase->bones_y, phase->facing, 0);
        bones_bbs_consume(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_C_A, NULL);
    }
    if (bones_bbs_peek(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_C_B, NULL) == 1) {
        phase->last_message = MSG_C_B;
        phase->sfx_flags |= SFX_C;
        runway_sfx_emit(0x23); /* Saturn C edge, Spring_Brain at 0x26305E. */
        stage_enemies_fire_blues(&phase->enemies, phase->bones_x,
                                 phase->bones_y, phase->facing, 0);
        bones_bbs_consume(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_C_B, NULL);
    }
    if (pad->pressed & 0x0400u) {
        stage_enemies_fire_blues(&phase->enemies, phase->bones_x,
                                 phase->bones_y, phase->facing, 1);
        phase->sfx_flags |= SFX_C;
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
        int is_ride = (strcmp(phase->stage_name, "RIDE") == 0);
        int is_staing = (strcmp(phase->stage_name, "STAING") == 0);
        uint32_t prev_bx = phase->bones_x;
        uint32_t step_r = (phase->tall && !is_ride && !is_staing) ? WALK_STEP_RIGHT_TALL : WALK_STEP_RIGHT;
        uint32_t step_l = (phase->tall && !is_ride && !is_staing) ? WALK_STEP_LEFT_TALL : WALK_STEP_LEFT;

        const runway_stage_profile *prof = runway_profile_for_stage(phase->stage_name);
        uint32_t target_r = (step_r * prof->speed_pct) / 100u;
        uint32_t target_l = (step_l * prof->speed_pct) / 100u;

        if (is_ride || is_staing) {
            int y_min = is_ride ? 56 : 48;
            int y_max = is_ride ? 176 : 172;
            phase->tall = 0;
            if (held & 0x1000u) phase->fly_y_px -= 2;
            if (held & 0x2000u) phase->fly_y_px += 2;
            if (phase->fly_y_px < y_min) phase->fly_y_px = y_min;
            if (phase->fly_y_px > y_max) phase->fly_y_px = y_max;
        }

        /* Horizontal physics: smooth ground acceleration/braking, preserved air momentum, knockback. */
        if (phase->hurt_timer > 0u) phase->hurt_timer--;
        if (phase->hurt_timer > 10u && phase->knockback_vx != 0) {
            phase->vel[0] = phase->knockback_vx;
            phase->knockback_vx = (phase->knockback_vx * 3) / 4;
            phase->accel[0] = 0;
            phase->vel_a = phase->vel[0] < 0 ? -phase->vel[0] : phase->vel[0];
            phase->vel_b = phase->vel_a;
        } else if (phase->runway_win_record) {
            if (held & 0x8000) phase->facing = 1;
            else if (held & 0x4000) phase->facing = -1;
            phase->vel[0] = input_dx;
            phase->accel[0] = 0;
            phase->vel_a = phase->vel_b = abs(input_dx);
        } else if (prof->drift_vx == 0 && prof->speed_pct == 100u) {
            int32_t desired = 0;
            if (held & 0x8000) {
                phase->facing = 1;
                desired = (int32_t)step_r;
            } else if (held & 0x4000) {
                phase->facing = -1;
                desired = -(int32_t)step_l;
            }
            phase->accel[0] = 0;
            if (!phase->airborne) {
                if (desired != 0) {
                    int32_t ramp = (int32_t)(step_r / 2u);
                    int32_t diff = desired - phase->vel[0];
                    if (ramp < 1) ramp = (int32_t)step_r;
                    if (diff > ramp) phase->vel[0] += ramp;
                    else if (diff < -ramp) phase->vel[0] -= ramp;
                    else phase->vel[0] = desired;
                } else {
                    phase->vel[0] = (phase->vel[0] * 3) / 5;
                    if (phase->vel[0] > -(int32_t)(UNITS_PER_PIXEL / 4u) &&
                        phase->vel[0] < (int32_t)(UNITS_PER_PIXEL / 4u))
                        phase->vel[0] = 0;
                }
            } else if (desired != 0) {
                int32_t air_step = (int32_t)(step_r / 6u);
                int32_t diff = desired - phase->vel[0];
                if (air_step < 1) air_step = 1;
                if (diff > air_step) phase->vel[0] += air_step;
                else if (diff < -air_step) phase->vel[0] -= air_step;
                else phase->vel[0] = desired;
            } else {
                phase->vel[0] = (phase->vel[0] * 63) / 64;
            }
            phase->vel_a = phase->vel[0] < 0 ? -phase->vel[0] : phase->vel[0];
            phase->vel_b = phase->vel_a;
        } else {
            phase->accel[0] = 0;
            if (held & 0x8000) {
                phase->facing = 1;
                phase->vel[0] = (int32_t)target_r;
            } else if (held & 0x4000) {
                phase->facing = -1;
                phase->vel[0] = (prof->drift_vx >= 1000)
                                    ? -(int32_t)(target_l / 3u)
                                    : -(int32_t)target_l;
            } else {
                phase->vel[0] = prof->drift_vx;
                if (prof->drift_vx > 0) phase->facing = 1;
            }
            phase->vel_a = phase->vel[0] < 0 ? -phase->vel[0] : phase->vel[0];
            phase->vel_b = phase->vel_a;
        }

        /* 60Hz coyote time + 5f jump buffer; short-hop cut on early release. */
        phase->coyote_timer = phase->airborne
                                  ? (phase->coyote_timer > 0u ? (uint8_t)(phase->coyote_timer - 1u) : 0u)
                                  : 5u;
        if (pad->pressed & 0x0100u) phase->jump_buf_timer = 5u;
        else if (phase->jump_buf_timer > 0u) phase->jump_buf_timer--;

        if (phase->jump_buf_timer > 0u && (!phase->airborne || phase->coyote_timer > 0u)) {
            int gx, gy;
            world_to_grid_at(phase, phase->bones_x, phase->bones_y, &gx, &gy);
            gy -= phase->hit.bot_right_y;
            if (!pan_bones_wall_above(&phase->collision, gx, gy, &phase->hit)) {
                bones_bbs_post(&phase->bbs, MSG_JUMP, 1);
                phase->last_message = MSG_JUMP;
                phase->obj_flags |= 0x08u;
                phase->vel[1] = phase->jump_impulse;
                phase->spring_ticks = (uint8_t)phase->boost_ticks;
                phase->accel[1] = SIDESCRL_PHYS_GRAVITY_ACCEL;
                phase->airborne = 1;
                phase->coyote_timer = 0u;
                phase->jump_buf_timer = 0u;
                phase->state = RUNWAY_ST_AIR;
                phase->sfx_flags |= SFX_JUMP;
            }
        }

        int has_terrain = pan_has_collision(phase);
        if (phase->airborne) {
            phase->accel[1] = SIDESCRL_PHYS_GRAVITY_ACCEL;
            if (phase->spring_ticks) {
                if (pad->held & 0x0100u) {
                    phase->accel[1] -= BONES_SPRING_BOOST;
                    phase->spring_ticks--;
                } else phase->spring_ticks = 0;
            }
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
            if (has_terrain) {
                int vpx = viewport_px(phase, phase->bones_x);
                int floor = (is_ride || is_staing) ? (int)phase->fly_y_px : pan_floor_at(phase, vpx);
                if (floor >= 0 && phase->vel[1] >= 0 && next_y64 >= (int64_t)floor * UNITS_PER_PIXEL) {
                    phase->bones_y = (uint32_t)floor * UNITS_PER_PIXEL;
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
                    if (is_ride || is_staing) {
                        phase->bones_y = (uint32_t)phase->fly_y_px * UNITS_PER_PIXEL;
                        phase->state = RUNWAY_ST_WALK;
                        phase->obj_flags |= 0x02u;

                    } else if (has_terrain) {
                        int vpx_new = viewport_px(phase, phase->bones_x);
                        int floor = pan_floor_at(phase, vpx_new);
                        if (floor < 0 || floor > (int)(phase->bones_y / UNITS_PER_PIXEL) + 2) {
                            phase->airborne = 1;
                            phase->vel[1] = 0;
                            phase->state = RUNWAY_ST_AIR;
                        } else {
                            phase->bones_y = (uint32_t)floor * UNITS_PER_PIXEL;
                            phase->state = RUNWAY_ST_WALK;
                            phase->obj_flags |= 0x02u;
                        }
                    } else {
                        int tx = coll_probe_px(phase, phase->bones_x) /
                                 (int)PAN_COLL_PIXELS_PER_TILE;
                        phase->bones_y = ground_world_y(&phase->collision, tx);
                        phase->state = RUNWAY_ST_WALK;
                        phase->obj_flags |= 0x02u;
                    }
                } else {
                    bones_kill_vel_accel_vec(phase->vel, phase->accel);
                    phase->vel_a = phase->vel_b = 0;
                    phase->state = RUNWAY_ST_STAND;
                }
            } else if (is_ride || is_staing) {
                phase->bones_y = (uint32_t)phase->fly_y_px * UNITS_PER_PIXEL;
                phase->state = (held & 0x3000u) ? RUNWAY_ST_WALK : RUNWAY_ST_STAND;
            } else if (has_terrain && pan_floor_at(phase, viewport_px(phase, phase->bones_x)) < 0) {
                phase->airborne = 1;
                phase->vel[1] = 0;
                phase->accel[1] = SIDESCRL_PHYS_GRAVITY_ACCEL;
                phase->state = RUNWAY_ST_AIR;
            } else {
                phase->state = RUNWAY_ST_STAND;
            }
        }
        if (phase->runway_win_record && !phase->airborne) {
            phase->state = RUNWAY_ST_WALK;
            phase->obj_flags |= 0x02u;
        }
        phase->walk_dist += (phase->bones_x >= prev_bx)
                                ? (phase->bones_x - prev_bx)
                                : (prev_bx - phase->bones_x);

        if (bones_bbs_consume(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_JUMP, NULL) == 1)
            phase->last_message = MSG_JUMP;

        /* Stand → BonesWalk message edge (peek 0x11001 / 0x11000 in Stand_Brain). */
        if (was_state == RUNWAY_ST_STAND && phase->state == RUNWAY_ST_WALK) {
            bones_bbs_post(&phase->bbs, MSG_WALK_A, 1);
            bones_bbs_post(&phase->bbs, MSG_WALK_B, 1);
            if (!phase->runway_win_record) phase->sfx_flags |= SFX_WALK;
        }
        if (!phase->runway_win_record && phase->state == RUNWAY_ST_WALK && !phase->airborne &&
            (phase->frames % 30u) == 6u) {
            phase->sfx_flags |= SFX_WALK;
        }
        if (bones_bbs_peek(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_WALK_A, NULL) == 1) {
            phase->last_message = MSG_WALK_A;
            bones_bbs_consume(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_WALK_A, NULL);
        }
        if (bones_bbs_peek(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_WALK_B, NULL) == 1)
            bones_bbs_consume(&phase->bbs, BONES_BBS_MASK_EXACT, MSG_WALK_B, NULL);

        if (!has_terrain)
            apply_push_out(phase);
        phase->input_cell = phase->bones_x; /* ≡ world X @0x2858BC after NearVel */
    }

    /* Camera follows bones with light lag. */
    int32_t dx = (int32_t)(phase->bones_x - phase->camera_x) / 8;
    if (!phase->runway_win_record) phase->camera_x += (uint32_t)dx;
    int32_t dy = (int32_t)(phase->bones_y - phase->camera_y) / 8;
    phase->camera_y += (uint32_t)dy;
    if (!strcmp(phase->stage_name, "MOAT") && ((phase->frames % 40u) < 3u))
        phase->camera_y += UNITS_PER_PIXEL;

    /* §12m/§22: CPanScroll / PlayPanAnim — seek 15PAN visuals + live collision
     * from current pandcomp record during camera advance. */
    if (phase->pan.count > 0) {
        unsigned cam_px = phase->camera_x / UNITS_PER_PIXEL;
        unsigned bones_px = phase->bones_x / UNITS_PER_PIXEL;
        unsigned prev_idx = phase->pan.index;
        unsigned fin_px = (unsigned)(phase->finish_x / UNITS_PER_PIXEL);
        int free_y = (!strcmp(phase->stage_name, "RIDE") ||
                      !strcmp(phase->stage_name, "STAING"));
        int pan_res;
        if (fin_px <= START_PX) fin_px = FINISH_PX;
        if (phase->runway_win_record) {
            unsigned record = ++phase->runway_clock / 4u;
            /* 15 PAN records/s in the 60 Hz loop, verified against the capture. */
            pan_res = record == prev_idx ? 0 : pan_scroll_seek(&phase->pan, record);
            if (pan_res) return -1;
            /* Keep the host's scrolling coordinate basis out of InputBrain.
             * The original recordings and BonesScPos are centered on the viewport. */
            uint32_t basis = START_X + record * 0x2c54u;
            if (phase->state == RUNWAY_ST_WALK && !phase->airborne)
                phase->walk_dist += basis - phase->camera_x;
            phase->bones_x += basis - phase->camera_x;
            phase->camera_x = basis;
            while (phase->pan_event_index < phase->pan_event_count &&
                   phase->pan_events[phase->pan_event_index].frame == record) {
                unsigned mask = phase->pan_events[phase->pan_event_index++].mask;
                if (mask & 1u) {
                    /* PlayTriggerList sets BonesPos to (0xfffd4ccd, 0x4000). */
                    phase->bones_x = basis + 0x2b333u;
                    phase->bones_y = (PAN_HEIGHT/2 - 7 + phase->hit.bot_right_y) * UNITS_PER_PIXEL;
                }
            }
            phase->pan.scroll_x = (int)(phase->camera_x / UNITS_PER_PIXEL);
            /* CLevelManager::Brain yields for 0x2d0 ticks before the left-edge
             * death test. BonesScPos is signed and centered on the screen. */
            int64_t relative = (int64_t)phase->bones_x - phase->camera_x;
            if (phase->runway_clock > 0x2d0u && relative < -(int64_t)150 * UNITS_PER_PIXEL) {
                (void)runway_skel_loose(&phase->skel, RUNWAY_SKEL_MAX);
                phase->state = RUNWAY_ST_LOST;
                snprintf(phase->status, sizeof phase->status, "RUNWAY lost (left edge) PAN=%u", record);
                return runway_phase_render(phase);
            }
        }
        else if (!strcmp(phase->stage_name, "LOGS") ||
                 !strcmp(phase->stage_name, "RIDE") ||
                 !strcmp(phase->stage_name, "STAING"))
            pan_res = pan_scroll_update_ratio(&phase->pan, cam_px, bones_px, 1u, 4u);
        else
            pan_res = pan_scroll_update(&phase->pan, cam_px, bones_px, START_PX, fin_px);
        if (pan_res == 0) {
            phase->bg_rgba = phase->pan.bg_rgba;
            phase->collision = phase->pan.collision;
            /* A moving PAN wall can reach stationary Bones between player steps. */
            if (phase->runway_win_record && phase->pan.index != prev_idx) {
                int x = screen_x_at(phase, phase->bones_x);
                int shift = runway_wall_shift(phase, x + phase->hit.top_left_x,
                                              x + phase->hit.top_right_x, 1);
                int64_t corrected = (int64_t)phase->bones_x + (int64_t)shift * UNITS_PER_PIXEL;
                phase->bones_x = corrected > 0 ? (uint32_t)corrected : 0;
            }
            if (!phase->airborne && !free_y && phase->pan.index != prev_idx) {
                if (pan_has_collision(phase)) {
                    int floor = pan_floor_at(phase, viewport_px(phase, phase->bones_x));
                    if (floor >= 0 && floor <= (int)(phase->bones_y / UNITS_PER_PIXEL) + 2)
                        phase->bones_y = (uint32_t)floor * UNITS_PER_PIXEL;
                    else {
                        phase->airborne = 1;
                        phase->state = RUNWAY_ST_AIR;
                    }
                } else {
                    int tx = coll_probe_px(phase, phase->bones_x) /
                             (int)PAN_COLL_PIXELS_PER_TILE;
                    phase->bones_y = ground_world_y(&phase->collision, tx);
                }
            }
        }
    }

    if (phase->runway_win_record) {
        collect_butterflies(phase);
        if (phase->runway_clock > 0x2d0u) step_fall_profile(phase);
        if (phase->skel.died) {
            phase->state = RUNWAY_ST_LOST;
            snprintf(phase->status, sizeof phase->status, "RUNWAY lost (CheckFallHole) hits=%u", phase->hazards.hits_applied);
            return runway_phase_render(phase);
        }
    }
    anim_update(phase);

    /* §24: Gang/Lord/Poofs + CThrown + Blues shots + HEALTH pickups + ice cracks. */
    {
        uint32_t ground_cur;
        int high_jump;
        int prev_skel = phase->skel.skeletism;
        if (pan_has_collision(phase)) {
            int vpx = viewport_px(phase, phase->bones_x);
            int floor = pan_floor_at(phase, vpx);
            ground_cur = (floor >= 0 ? (unsigned)floor : PAN_HEIGHT) * UNITS_PER_PIXEL;
            if (phase->airborne && phase->bones_y / UNITS_PER_PIXEL >= PAN_HEIGHT) {
                phase->state = RUNWAY_ST_LOST;
                snprintf(phase->status, sizeof phase->status, "%s fell below PAN", phase->stage_name);
                return runway_phase_render(phase);
            }
        } else {
            int gtx = coll_probe_px(phase, phase->bones_x) /
                      (int)PAN_COLL_PIXELS_PER_TILE;
            ground_cur = ground_world_y(&phase->collision, gtx);
        }
        high_jump = (phase->airborne &&
                     (phase->vel[1] < 0 ||
                      phase->bones_y + 6u * UNITS_PER_PIXEL <= ground_cur))
                        ? 1
                        : 0;
        if (phase->runway_win_record) step_gang_recordings(phase);
        unsigned enemy_hits = phase->enemies.hits_applied;
        int enemy_loss = stage_enemies_step_player(&phase->enemies, &phase->skel,
                                                   phase->bones_x, phase->bones_y,
                                                   ground_cur, phase->tall, phase->airborne,
                                                   &phase->hazards, 1);
        if (phase->runway_win_record && phase->enemies.hits_applied > enemy_hits)
            phase_sound_event(phase, 0x1b); /* RUNWAY _Hit_Generic. */
        if (enemy_loss || phase->skel.died) {
            phase->state = RUNWAY_ST_LOST;
            snprintf(phase->status, sizeof phase->status,
                     "%s lost (enemy skel=0) hits=%u", phase->stage_name,
                     phase->enemies.hits_applied);
            return runway_phase_render(phase);
        }

        /* §12o/§22 hazards on all pan stages → BonesLoose → EndLevel(1). */
        if (phase->hazards.zone_count || phase->hazards.thrown_count) {
            unsigned hazard_hits = phase->hazards.hits_applied;
            int hazard_loss = runway_hazards_step_ex(&phase->hazards, &phase->skel,
                                                    phase->bones_x, high_jump);
            if (phase->runway_win_record && phase->hazards.hits_applied > hazard_hits)
                phase_sound_event(phase, 0x1b);
            if (hazard_loss || phase->skel.died) {
                phase->state = RUNWAY_ST_LOST;
                snprintf(phase->status, sizeof phase->status,
                         "%s lost (EndLevel fail) skel=0 hits=%u",
                         phase->stage_name, phase->hazards.hits_applied);
                return runway_phase_render(phase);
            }
        }
        if (!phase->skel.died && phase->skel.skeletism < prev_skel) {
            phase->hurt_timer = 18u;
            phase->knockback_vx = -phase->facing * (int32_t)(2u * UNITS_PER_PIXEL);
            if (!phase->airborne) {
                phase->vel[1] = BONES_SPRING_IMPULSE / 3;
                phase->accel[1] = SIDESCRL_PHYS_GRAVITY_ACCEL;
                phase->airborne = 1;
                phase->state = RUNWAY_ST_AIR;
            }
            stage_enemies_spawn_bone_burst(&phase->enemies, phase->bones_x,
                                           phase->bones_y, 5);
        }
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

    if (phase->runway_win_record ? phase->pan.index >= phase->runway_win_record
                                : phase->bones_x >= phase->finish_x) {
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

static void rw_blend_px(uint8_t *dst, unsigned w, unsigned h, int x, int y,
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

static void rw_draw_disc(uint8_t *dst, unsigned w, unsigned h, int cx, int cy,
                         int rad, uint8_t r, uint8_t g, uint8_t b, int a) {
    int dx, dy, r2 = rad * rad;
    for (dy = -rad; dy <= rad; dy++) {
        for (dx = -rad; dx <= rad; dx++) {
            if (dx * dx + dy * dy <= r2)
                rw_blend_px(dst, w, h, cx + dx, cy + dy, r, g, b, a);
        }
    }
}

static void rw_draw_stage_fx(const runway_phase *phase,
                             const uint8_t rw_road_py[PAN_WIDTH]) {
    uint8_t *dst = phase->frame_rgba;
    unsigned f = phase->frames;
    if (!strcmp(phase->stage_name, "RIDE")) {
        int k;
        for (k = 0; k < 8; k++) {
            int bx = (38 * k + (int)((f >> 1) % 32u)) % (int)PAN_WIDTH;
            int by = 188 - (int)((f * 2u + (unsigned)k * 23u) % 148u);
            int br = 2 + (k & 1);
            rw_draw_disc(dst, PAN_WIDTH, PAN_HEIGHT, bx, by, br, 165, 235, 255, 130);
            rw_blend_px(dst, PAN_WIDTH, PAN_HEIGHT, bx - 1, by - 1, 240, 252, 255, 210);
        }
    } else if (!strcmp(phase->stage_name, "RUNWAY")) {
        unsigned cam_px = phase->camera_x / UNITS_PER_PIXEL;
        int k;
        for (k = 0; k < 3; k++) {
            int gx = (int)(460u + (unsigned)k * 1240u) - (int)cam_px + (int)(PAN_WIDTH / 2u);
            if (gx >= 16 && gx <= (int)PAN_WIDTH - 16) {
                int gy = (rw_road_py && gx < (int)PAN_WIDTH && rw_road_py[gx] > 48u)
                             ? (int)rw_road_py[gx] - 6
                             : 146;
                stage_enemies_draw_runway_ghost(dst, PAN_WIDTH, PAN_HEIGHT, gx, gy, f + (unsigned)k * 9u);
            }
        }
    }
}

static void draw_swimmer(runway_phase *p, int x, int foot_y, unsigned pose) {
    uint8_t *src = p->swim_sprite;
    memset(src, 0, PAN_PLANE_SIZE*4);
    stage_enemies_draw_bones_ex(src, PAN_WIDTH, PAN_HEIGHT, 160, 112 + p->hit.bot_right_y,
                                p->facing, 0, 1, pose, p->projection_px, 28);
    int co = sat_sin(p->swim_roll + 256), si = sat_sin(p->swim_roll);
    int cy = foot_y - p->hit.bot_right_y;
    for (int v = -112; v < 112; ++v) {
        int y = cy + v;
        if ((unsigned)y >= PAN_HEIGHT) continue;
        for (int u = -112; u < 112; ++u) {
            int dst_x = x+u;
            if ((unsigned)dst_x >= PAN_WIDTH) continue;
            int sx = 160 + ((u*co + v*si) >> 8);
            int sy = 112 + ((v*co - u*si) >> 8);
            if ((unsigned)sx >= PAN_WIDTH || (unsigned)sy >= PAN_HEIGHT) continue;
            const uint8_t *pixel = src + 4u*((unsigned)sy*PAN_WIDTH + (unsigned)sx);
            if (pixel[3]) memcpy(p->frame_rgba + 4u*((unsigned)y*PAN_WIDTH + (unsigned)dst_x), pixel, 4);
        }
    }
}

int runway_phase_render(runway_phase *phase) {
    int foot_x, foot_y;
    if (phase && phase->ice) return ice_draw(phase);
    if (!phase || !phase->bg_rgba || !phase->frame_rgba || !phase->sprite_rgba) return -1;
    stage_enemies_reset_bones_pose();
    if (phase->scrl_bg_rgba && phase->pan.planes) {
        /* §16 mask the ACTIVE ring plane (pan.index & 3), not stale Plane 0:
         * bg_rgba is regenerated from planes + (index&3)*PAN_PLANE_SIZE.
         * scrl_bg_w == 0: frozen 320-wide backdrop sampled at the same pixel;
         * otherwise a wide strip sampled at a camera/ring-driven column offset. */
        size_t p;
        size_t plane_base = (size_t)(phase->pan.index & 3u) * PAN_PLANE_SIZE;
        unsigned camera_px = phase->camera_x / UNITS_PER_PIXEL;
        unsigned bg_stride = phase->scrl_bg_w ? phase->scrl_bg_w : PAN_WIDTH;
        for (p = 0; p < PAN_PLANE_SIZE; p++) {
            uint8_t pv = phase->pan.planes[plane_base + p];
            int opaque = pv != 0u;
            uint8_t *dst = &phase->frame_rgba[4u * p];
            const uint8_t *src;
            if (opaque) {
                src = &phase->bg_rgba[4u * p];
            } else {
                unsigned col = (unsigned)(p % PAN_WIDTH);
                if (phase->scrl_bg_w)
                    col = runway_phase_bg_column(col, camera_px, phase->pan.index,
                                                 phase->scrl_bg_w);
                src = &phase->scrl_bg_rgba[4u * ((p / PAN_WIDTH) * bg_stride + col)];
            }
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = 255u;
        }
    } else if (runway_compose_blit_opaque(phase->frame_rgba, PAN_WIDTH, PAN_HEIGHT,
                                          phase->bg_rgba, PAN_WIDTH, PAN_HEIGHT, 0, 0))
        return -1;
    foot_x = screen_x_at(phase, phase->bones_x);
    if (foot_x < 16) foot_x = 16;
    if (foot_x > (int)PAN_WIDTH - 16) foot_x = (int)PAN_WIDTH - 16;
    foot_y = (int)(phase->bones_y / UNITS_PER_PIXEL);
    if (foot_y < 20) foot_y = 20;
    if (foot_y > (int)PAN_HEIGHT - 8) foot_y = (int)PAN_HEIGHT - 8;

    {
        uint8_t rw_road_py[PAN_WIDTH];
        int has_floor = pan_has_collision(phase);
        for (unsigned x = 0; x < PAN_WIDTH; x += 2) {
            int floor = has_floor ? pan_floor_at(phase, (int)x) : -1;
            rw_road_py[x] = rw_road_py[x + 1] = (floor < 0) ? UINT8_MAX : (uint8_t)floor;
        }
        int is_ride = (strcmp(phase->stage_name, "RIDE") == 0);
        int is_staing = (strcmp(phase->stage_name, "STAING") == 0);
        int ground_py = foot_y;
        int shadow_py = foot_y;
        unsigned bones_anim = 0u;
        if (has_floor && foot_x >= 0 && foot_x < (int)PAN_WIDTH &&
            rw_road_py[foot_x] > 0u && rw_road_py[foot_x] < 224u) {
            ground_py = (int)rw_road_py[foot_x];
            if (!phase->airborne && !is_ride && !is_staing &&
                foot_y > ground_py)
                foot_y = ground_py;
            if (foot_y < 20) foot_y = 20;
            shadow_py = ground_py;
        }
        stage_enemies_render_ex(&phase->enemies, &phase->hazards,
                                phase->frame_rgba, PAN_WIDTH, PAN_HEIGHT,
                                phase->camera_x / UNITS_PER_PIXEL,
                                (int)(PAN_WIDTH / 2), ground_py,
                                has_floor ? rw_road_py : NULL,
                                has_floor ? PAN_WIDTH : 0u, 1);
        if (phase->runway_win_record) {
            for (unsigned i=0; i<phase->butterfly_count; i++) {
                int age;
                int32_t world_x;
                if (!butterfly_position(phase,i,&age,&world_x)) continue;
                unsigned tick = (unsigned)age % phase->butterfly_anim[25].end_tick;
                unsigned index = 0;
                while (tick >= phase->butterfly_anim[index].end_tick) index++;
                const runway_gang_shape *f = &phase->butterfly_anim[index].shape;
                runway_compose_blit_sprite(phase->frame_rgba, PAN_WIDTH, PAN_HEIGHT, f->pixels, f->width, f->height,
                    (int)PAN_WIDTH/2 - world_x/(int)UNITS_PER_PIXEL - f->anchor_x,
                    (int)PAN_HEIGHT/2 - 0x16db6/(int)UNITS_PER_PIXEL - f->anchor_y);
            }
            for (unsigned i = 0; i < 4; i++) {
                const stage_enemy *actor = &phase->enemies.ents[i];
                if (!actor->active) continue;
                const runway_gang_shape *shape = &phase->gang_shapes[actor->anim];
                runway_compose_blit_sprite(phase->frame_rgba, PAN_WIDTH, PAN_HEIGHT,
                    shape->pixels, shape->width, shape->height,
                    actor->screen_x - shape->anchor_x, actor->screen_y - shape->anchor_y);
            }
        }
        stage_enemies_set_bones_ground_y(shadow_py, phase->stage_name);
        stage_enemies_set_bones_fx((int)phase->hurt_timer,
                                   phase->skel.hit_cooldown,
                                   phase->skel.died);
        if (phase->course == COURSE_MOAT) {
            for (unsigned side=0; side<2; ++side) {
                if (!phase->nuts[side].turns) continue;
                unsigned pose=phase->nut_busy[side] ? (96-phase->nut_busy[side])/6 : 0;
                const runway_gang_shape *f=&phase->nut_pose[side][pose];
                runway_compose_blit_sprite(phase->frame_rgba,PAN_WIDTH,PAN_HEIGHT,
                    f->pixels,f->width,f->height,160-phase->nuts[side].x/2048-f->anchor_x,
                    112-phase->nuts[side].y/2048-f->anchor_y);
            }
        }
        if (is_ride) for(unsigned i=0;i<phase->swim_bubble_count;++i) {
            const runway_gang_shape *f=&phase->swim_bubble_pose[(phase->swim_bubbles[i].age/4)%16];
            int depth=6*65536+phase->swim_bubbles[i].z;
            if(depth>0){
                int x=160-(int)((int64_t)phase->swim_bubbles[i].x*252/depth);
                int y=112-(int)((int64_t)phase->swim_bubbles[i].y*252/depth);
                runway_compose_blit_sprite(phase->frame_rgba,PAN_WIDTH,PAN_HEIGHT,f->pixels,f->width,f->height,x-f->anchor_x,y-f->anchor_y);
            }
        }
        if (phase->course==COURSE_LOGS) draw_log_objects(phase);
        if (is_staing) draw_flight_objects(phase);
        if (is_staing)
            stage_enemies_draw_staing_arms(phase->frame_rgba, PAN_WIDTH, PAN_HEIGHT,
                                           foot_x, foot_y - 44, phase->frames);
        {
            int draw_airborne = phase->airborne;
            if (is_ride) {
                draw_airborne = 1;
                bones_anim = (phase->frames / 6u) & 3u;
                if (!phase->airborne)
                    foot_y -= 4 + (int)((phase->frames / 5u) & 3u);
            } else if (is_staing) {
                draw_airborne = 1;
                bones_anim = (phase->frames / 8u) & 1u;
            } else if (phase->airborne) {
                int32_t vy = phase->vel[1];
                int32_t u = (int32_t)UNITS_PER_PIXEL;
                bones_anim = (vy < -6 * u) ? 0u : (vy < 0) ? 1u : (vy < 6 * u) ? 2u : 3u;
            } else if (phase->tall) {
                bones_anim = (phase->frames / 6u) & 1u;
            } else if (phase->state == RUNWAY_ST_WALK ||
                       (phase->state == RUNWAY_ST_PAUSED && phase->pre_pause_state == RUNWAY_ST_WALK)) {
                bones_anim = phase->runway_win_record ? phase->anim_index + 1u :
                             ((phase->walk_dist / (5u * UNITS_PER_PIXEL)) % 20u) + 1u;
            } else {
                bones_anim = 0u;
            }
            if (is_ride) draw_swimmer(phase, foot_x, foot_y, bones_anim);
            else stage_enemies_draw_bones_ex(
                phase->frame_rgba, PAN_WIDTH, PAN_HEIGHT,
                foot_x, foot_y, phase->facing, phase->tall,
                draw_airborne, bones_anim,
                phase->projection_px, 28u);
            if (has_floor)
                rw_draw_stage_fx(phase, rw_road_py);
        }
    }
    /* §16 HUD: top-right BONES 14-seg skelbar + top-left stage label; PAUSE.O when paused. */
    if (phase->hud_health_rgba)
        runway_compose_blit_sprite(phase->frame_rgba, PAN_WIDTH, PAN_HEIGHT,
                                   phase->hud_health_rgba, phase->hud_health_w,
                                   phase->hud_health_h, 214, 6);
    stage_enemies_draw_hud_skelbar(phase->frame_rgba, PAN_WIDTH, PAN_HEIGHT,
                               phase->skel.segments, RUNWAY_SKEL_SEGMENTS, 214, 6);
    if (phase->stage_name[0])
        mb_font_draw_text_shadow(phase->frame_rgba, PAN_WIDTH, PAN_HEIGHT,
                                 8, 8, phase->stage_name,
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

/* Look ahead through visible object trajectories using the same claw controller as
 * a player. Each candidate is an ordinary D-pad aim held across two time windows. */
static unsigned flight_inputs(const runway_phase *p) {
    static const unsigned aim_to_pad[11]={0,MB_PAD_DOWN,MB_PAD_UP,0,MB_PAD_RIGHT,
        MB_PAD_RIGHT|MB_PAD_DOWN,MB_PAD_RIGHT|MB_PAD_UP,0,MB_PAD_LEFT,
        MB_PAD_LEFT|MB_PAD_DOWN,MB_PAD_LEFT|MB_PAD_UP};
    static const unsigned choices[]={0,1,2,4,5,6,8,9,10};
    if (p->runway_clock % 8u) return aim_to_pad[p->claws.aim];
    struct prediction {int at,end,x,y,z,delay,value;} near[128];
    unsigned count=0;
    for (unsigned i=0;i<p->flight_count;++i) {
        int delay=p->flight_objects[i].birth>p->runway_clock ?
            (int)(p->flight_objects[i].birth-p->runway_clock) : 0;
        int red=p->flight_objects[i].red;
        int begin=(p->flight_objects[i].z+(red?0x14000:0x4000))/1638 + delay;
        int end=(p->flight_objects[i].z+(red?0x18000:0xc000))/1638 + delay;
        if (begin>144 || end<0) continue;
        near[count].at=begin; near[count].end=end;
        near[count].x=p->flight_objects[i].x; near[count].y=p->flight_objects[i].y;
        near[count].z=p->flight_objects[i].z;near[count].delay=delay;
        near[count++].value=red ? -10 * 0xa00 : 0xa00;
    }
    if (!count) return 0;
    int rel[37][2][3];
    for (unsigned t=4;t<=144;t+=4) {
        int32_t pts[FINALE_BODY_JOINTS][3]={{0}};
        stage_body_pose pose={{0,0,0},{0,0,0},{0,0,p->claws.claw_roll>>8},256,
                              p->claw_tick+t,0,p->claw_motion};
        stage_body_joints(p->claw_body,&pose,pts);
        for(int k=0;k<3;++k){
            int hands=(pts[10][k]+pts[17][k])/2;
            rel[t/4][0][k]=pts[6][k]-hands;
            rel[t/4][1][k]=(pts[13][k]+pts[20][k])/2-hands;
        }
    }
    int best=-2147483647;
    unsigned button=0;
    for (unsigned a=0;a<9;++a) for (unsigned b=0;b<9;++b) {
        unsigned char touched[128]={0};
        runway_claws m=p->claws;
        int score=0;
        for (int t=1;t<=144;++t) {
            move_claws(p,&m,choices[t<=48 ? a : b]);
            if ((t&3)!=0) continue;
            int x=m.claw_x+sat_sin((int)(p->runway_clock+t)*19)*8;
            int y=m.claw_y+sat_sin((int)(p->runway_clock+t)*25)*16;
            int hip_x=flight_project(x+rel[t/4][0][0],rel[t/4][0][2]);
            int hip_y=flight_project(y+rel[t/4][0][1],rel[t/4][0][2]);
            int feet_x=flight_project(x+rel[t/4][1][0],rel[t/4][1][2]);
            int feet_y=flight_project(y+rel[t/4][1][1],rel[t/4][1][2]);
            for (unsigned i=0;i<count;++i) {
                if (t<near[i].at || t>near[i].end || touched[i]) continue;
                int z=near[i].z-(t-near[i].delay)*1638;
                int sx=flight_project(near[i].x,z),sy=flight_project(near[i].y,z);
                /* Leave clearance for pose changes that this forecast cannot track. */
                int hip_width = near[i].value < 0 ? 44 : 28;
                int hip_height = near[i].value < 0 ? 79 : 64;
                if ((abs(hip_x-sx)<hip_width && abs(hip_y-sy)<hip_height) ||
                    (near[i].value>0 && abs(feet_x-sx)<28 && abs(feet_y-sy)<28)) {
                    score+=near[i].value*(200-t/2);
                    touched[i]=1;
                }
            }
        }
        if (choices[a]==p->claws.aim) score+=1;
        if (score>best) {best=score;button=aim_to_pad[choices[a]];}
    }
    return button;
}

static unsigned logs_inputs(const runway_phase *p,unsigned frame) {
    int x=160-(int)((int64_t)p->course_x*p->projection_px/65536);
    int foot=p->fly_y_px+p->hit.bot_right_y;
    int best=234, cost=100000;
    for (int cx=18;cx<302;cx+=4) {
        int floor=pan_collision_floor(&p->collision,cx,foot>212?212:foot<0?0:foot);
        if (floor<0) continue;
        int s=abs(x-cx)+abs(cx-234)/3;
        if(p->pan.index<2825) s+=floor*2;
        else s+=abs(cx-234)*5;
        if(s<cost){cost=s;best=cx;}
    }
    unsigned b=best>x+2 ? MB_PAD_RIGHT : best<x-2 ? MB_PAD_LEFT : 0;
    if (p->airborne || (!(frame&1u) &&
        (foot>167 || p->pan.index>=2840 || abs(x-best)>10))) b|=MB_PAD_B;
    return b;
}

static unsigned ride_inputs(const runway_phase *p) {
    int x = 160 - (int)((int64_t)p->course_x*42/65536);
    int y = 112 - (int)((int64_t)p->course_y*42/65536);
    int best = -100000, dest_x = x, dest_y = y;
    /* Steer toward open water using the same four probes as the swimmer. */
    for (int cy = 40; cy <= 184; cy += 8) {
        for (int cx = 40; cx <= 280; cx += 8) {
            if (ride_wall_coverage(&p->collision, cx, cy)) continue;
            int room = 0;
            for (int r = 8; r <= 56; r += 8) {
                if (ride_wall_coverage(&p->collision,cx-r,cy) || ride_wall_coverage(&p->collision,cx+r,cy) ||
                    ride_wall_coverage(&p->collision,cx,cy-r) || ride_wall_coverage(&p->collision,cx,cy+r))
                    break;
                room += 8;
            }
            int score = 5*room - (abs(cx-160)+abs(cy-112))/2 - abs(cx-x)/4 - abs(cy-y)/4;
            if (score > best) { best=score; dest_x=cx; dest_y=cy; }
        }
    }
    int hx = x-dest_x + (512-p->swim_yaw)/5;
    int hy = y-dest_y + (p->swim_pitch-1024)/5;
    unsigned btn = MB_PAD_C;
    if (hx > 2) btn |= MB_PAD_LEFT;
    if (hx < -2) btn |= MB_PAD_RIGHT;
    if (hy > 2) btn |= MB_PAD_DOWN;
    if (hy < -2) btn |= MB_PAD_UP;
    return btn;
}

unsigned runway_phase_autoplay_buttons(const runway_phase *phase,
                                       unsigned frame) {
    unsigned btn = MB_PAD_RIGHT;
    unsigned cur_px, i;
    int want_jump = 0, want_c = 0;
    if (!phase) return MB_PAD_RIGHT;
    if (phase->ice) return ice_autoplay(phase);
    if (phase->course == COURSE_LOGS) return logs_inputs(phase,frame);
    if (phase->course == COURSE_STAING) return flight_inputs(phase);
    if (phase->course == COURSE_RIDE) return ride_inputs(phase);
    if (phase->course == COURSE_MOAT) {
        unsigned side = phase->nuts[0].turns ? 0u : 1u;
        int dx = phase->nuts[side].x-phase->course_x;
        btn = dx > 5400 ? MB_PAD_LEFT : dx < -5400 ? MB_PAD_RIGHT : MB_PAD_DOWN;
        if (phase->airborne || ((frame & 1u)==0 && !phase->nut_busy[side])) btn |= MB_PAD_C;
        return btn;
    }
    cur_px = phase->bones_x / UNITS_PER_PIXEL;
    if (phase->runway_win_record) {
        frame = phase->runway_clock;
        /* Upright right has no lateral thrust. Tall right recovers after a hit. */
        if ((int64_t)phase->bones_x - phase->camera_x < 0x2b333 - 2621) btn |= MB_PAD_DOWN;
    }
    for (i = 0; i < RUNWAY_HAZARD_ZONES && i < phase->hazards.zone_count; i++) {
        uint32_t x0 = phase->hazards.zones[i].x0 / UNITS_PER_PIXEL;
        uint32_t x1 = phase->hazards.zones[i].x1 / UNITS_PER_PIXEL;
        if (cur_px + 12u >= x0 && cur_px <= x1) want_jump = 1;
    }
    if (phase->pan.count > 0 && phase->collision.size > 0u) {
        int vpx = viewport_px(phase, phase->bones_x);
        int feet = (int)(phase->bones_y / UNITS_PER_PIXEL);
        for (int d = 2; d <= 26 && vpx + d < (int)PAN_WIDTH; d += 2) {
            int floor = pan_floor_at(phase, vpx + d);
            if (floor < 0 || floor < feet - 2 || floor > feet + 2) {
                want_jump = 1;
                break;
            }
        }
    }
    for (i = 0; i < RUNWAY_THROWN_MAX; i++) {
        int32_t dx;
        if (!phase->hazards.thrown[i].active) continue;
        dx = (int32_t)(phase->hazards.thrown[i].x / UNITS_PER_PIXEL) - (int32_t)cur_px;
        if (dx >= -6 && dx <= 38) { want_jump = 1; want_c = 1; }
    }
    for (i = 0; i < phase->enemies.count; i++) {
        const stage_enemy *ent = &phase->enemies.ents[i];
        int32_t dx;
        if (!ent->active) continue;
        dx = (int32_t)(ent->x / UNITS_PER_PIXEL) - (int32_t)cur_px;
        if (dx >= -6 && dx <= 96) {
            want_c = 1;
            if (ent->kind != STAGE_ENEMY_WINGNUT && ent->kind != STAGE_ENEMY_BAT &&
                dx >= 0 && dx <= 28)
                want_jump = 1;
        }
    }
    if (phase->runway_win_record && phase->airborne && phase->spring_ticks)
        btn |= MB_PAD_B;
    if (want_jump && !phase->airborne && (frame & 1u) == 0u)
        btn |= MB_PAD_B;
    if ((want_c && (frame % 6u) == 0u) || (frame % 12u) == 0u)
        btn |= MB_PAD_C;
    return btn;
}
