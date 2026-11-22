#include "stage_enemy.h"
#include "mb_font.h"
#include "pan_palette.h"
#include "resource.h"
#include "runway_compose.h"
#include "sprite_decode.h"
#include "stxtr.h"
#include "upscale.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * RingDefinitionTable@0x002b0158 first 24 BE32 words (signed offsets / radii).
 * Used as ring spawn offset table for FINALE CRing ShootRing stand-in.
 */
static const uint32_t k_ring_def[24] = {
    0xFFFFFFE5u, 0xFFFFFF9Fu, 0x0000001Bu, 0xFFFFFF9Fu, 0x00000018u,
    0xFFFFFFA9u, 0xFFFFFFE8u, 0xFFFFFFA9u, 0xFFFFFFE2u, 0xFFFFFFA0u,
    0x00000017u, 0xFFFFFF9Fu, 0x00000015u, 0xFFFFFFA9u, 0xFFFFFFE6u,
    0xFFFFFFA9u, 0xFFFFFFDFu, 0xFFFFFFA0u, 0x00000014u, 0xFFFFFF9Fu,
    0x00000012u, 0xFFFFFFA9u, 0xFFFFFFE3u, 0xFFFFFFAAu,
};

#define TIMING_ZAP_MAX 0x10u /* _TimingZapMax @002673d8 */

static void clear_bone_shards(void);
static void tick_bone_shards(unsigned u);
static void draw_filled_disc(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                             int cx, int cy, int r, uint8_t cr, uint8_t cg,
                             uint8_t cb, int alpha);
static void draw_hd_ellipse_shadow(uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                                   int cx, int cy, int rx, int ry, int max_alpha);

static int32_t ring_offset_signed(unsigned i) {
    uint32_t u = k_ring_def[i % 24u];
    return (int32_t)u;
}

static stage_enemy *spawn(stage_enemies *e, stage_enemy_kind kind, unsigned px,
                          int32_t damage, unsigned upp) {
    stage_enemy *ent;
    if (!e || e->count >= STAGE_ENEMY_MAX) return 0;
    ent = &e->ents[e->count++];
    memset(ent, 0, sizeof *ent);
    ent->kind = kind;
    ent->x = px * upp;
    ent->y = 180u * upp;
    ent->damage = damage > 0 ? damage : RUNWAY_DAMAGE_DEFAULT;
    ent->active = 1;
    ent->hp = (kind == STAGE_ENEMY_LORD || kind == STAGE_ENEMY_RED ||
               kind == STAGE_ENEMY_ZOMBIE || kind == STAGE_ENEMY_SHADOW) ? 3 : 1;
    if (kind == STAGE_ENEMY_BAT || kind == STAGE_ENEMY_WINGNUT)
        ent->y = 136u * upp;
    ent->patrol_lo = (px > 38u ? px - 38u : 4u) * upp;
    ent->patrol_hi = (px + 38u) * upp;
    ent->vx = (int32_t)(2u * upp);
    if (kind == STAGE_ENEMY_LILGUYS)
        ent->vx = -(int32_t)(3u * upp);
    else if (kind == STAGE_ENEMY_HAND)
        ent->vx = 0;
    else if (kind == STAGE_ENEMY_POOF_BLUE || kind == STAGE_ENEMY_BLUE ||
             kind == STAGE_ENEMY_ZOMBIE || kind == STAGE_ENEMY_BAT ||
             kind == STAGE_ENEMY_WINGNUT || kind == STAGE_ENEMY_SHADOW)
        ent->vx = -(int32_t)((kind == STAGE_ENEMY_ZOMBIE ? 1u : 2u) * upp);
    return ent;
}

void stage_enemies_init_runway(stage_enemies *e, unsigned units_per_pixel) {
    unsigned u = units_per_pixel ? units_per_pixel : 2341u;
    int32_t d = RUNWAY_DAMAGE_DEFAULT;
    int32_t d2 = RUNWAY_DAMAGE_DEFAULT / 2;
    if (!e) return;
    clear_bone_shards();
    memset(e, 0, sizeof *e);
    e->units_per_pixel = u;
    e->stage_name = "RUNWAY";
    /* Gang members at classic hazard pads (CheckObstacle / Throw). */
    spawn(e, STAGE_ENEMY_GANG, 128u, d, u);
    spawn(e, STAGE_ENEMY_GANG, 182u, d, u);
    spawn(e, STAGE_ENEMY_LORD, 238u, d, u);
    spawn(e, STAGE_ENEMY_POOF_RED, 150u, d2, u);
    spawn(e, STAGE_ENEMY_POOF_BLUE, 210u, d2, u);
    e->ents[0].cooldown = 40u;
    e->ents[1].cooldown = 55u;
    e->ents[2].cooldown = 70u;
}

void stage_enemies_init_sidescrl(stage_enemies *e, const char *stage_name,
                                 unsigned units_per_pixel) {
    unsigned u = units_per_pixel ? units_per_pixel : 2341u;
    int32_t d = RUNWAY_DAMAGE_DEFAULT;
    int32_t d2 = RUNWAY_DAMAGE_DEFAULT / 2;
    int bat = 0;
    if (!e) return;
    clear_bone_shards();
    memset(e, 0, sizeof *e);
    e->units_per_pixel = u;
    e->stage_name = stage_name ? stage_name : "";
    if (stage_name && (!strcmp(stage_name, "BAT1") || !strcmp(stage_name, "BAT2") ||
                       !strcmp(stage_name, "BAT3")))
        bat = 1;
    if (bat) {
        /* Authentic 08BAT1 swooping bats + graveyard zombies (4+ entries). */
        spawn(e, STAGE_ENEMY_BAT, 74u, d2, u);
        spawn(e, STAGE_ENEMY_ZOMBIE, 100u, d, u);
        spawn(e, STAGE_ENEMY_BAT, 134u, d2, u);
        spawn(e, STAGE_ENEMY_ZOMBIE, 165u, d, u);
        spawn(e, STAGE_ENEMY_BAT, 198u, d2, u);
        e->ents[0].anim = 0u;
        e->ents[2].anim = 11u;
        e->ents[4].anim = 22u;
    } else if (stage_name && !strcmp(stage_name, "MAUSO")) {
        /* 05MAUSO is driven by CMaestro's CPartition + CAssembler 8-skeleton wave. */
    } else if (stage_name && !strcmp(stage_name, "LAFF")) {
        /* 26LAFF Boss: Da Laff (ents[0]) + court escort. */
        stage_enemy *boss = spawn(e, STAGE_ENEMY_LORD, 240u, d, u);
        if (boss) {
            boss->hp = STAGE_LAFF_BOSS_HP;
            boss->patrol_lo = 140u * u;
            boss->patrol_hi = 300u * u;
            boss->vx = -(int32_t)(3u * u);
            boss->cooldown = 36u;
        }
        spawn(e, STAGE_ENEMY_GANG, 140u, d2, u);
    } else {
        /* Keep exact 2-entry count: LORD at px=95 and GANG clearly separated down-corridor at 185. */
        spawn(e, STAGE_ENEMY_LORD, 95u, d, u);
        spawn(e, STAGE_ENEMY_GANG, 185u, d2, u);
    }
}

void stage_enemies_init_pan_stage(stage_enemies *e, const char *stage_name,
                                  unsigned units_per_pixel) {
    unsigned u = units_per_pixel ? units_per_pixel : 2341u;
    int32_t d = RUNWAY_DAMAGE_DEFAULT;
    int32_t d2 = RUNWAY_DAMAGE_DEFAULT / 2;
    if (!e) return;
    clear_bone_shards();
    memset(e, 0, sizeof *e);
    e->units_per_pixel = u;
    e->stage_name = stage_name ? stage_name : "RUNWAY";
    if (!stage_name || !strcmp(stage_name, "RUNWAY")) return; /* phase loads TableRead */

    if (!strcmp(stage_name, "MOAT")) {
        spawn(e, STAGE_ENEMY_WINGNUT, 118u, d2, u);
        spawn(e, STAGE_ENEMY_POOF_RED, 190u, d2, u);
        spawn(e, STAGE_ENEMY_GANG, 250u, d2, u);
        spawn(e, STAGE_ENEMY_LORD, 340u, d, u);
        spawn(e, STAGE_ENEMY_WINGNUT, 430u, d2, u);
        spawn(e, STAGE_ENEMY_POOF_RED, 520u, d2, u);
        spawn(e, STAGE_ENEMY_GANG, 620u, d2, u);
        spawn(e, STAGE_ENEMY_LORD, 740u, d, u);
        spawn(e, STAGE_ENEMY_WINGNUT, 860u, d2, u);
    } else if (!strcmp(stage_name, "STAING")) {
        static const unsigned k_st_gang[8] = {
            120u, 360u, 640u, 940u, 1260u, 1580u, 1920u, 2220u
        };
        static const unsigned k_st_blue[4] = { 240u, 780u, 1420u, 2060u };
        static const unsigned k_st_red[3]  = { 500u, 1120u, 1760u };
        unsigned k;
        for (k = 0; k < 8u; k++) {
            stage_enemy *g = spawn(e, STAGE_ENEMY_GANG, k_st_gang[k], d, u);
            if (g) {
                g->patrol_lo = (k_st_gang[k] > 55u ? k_st_gang[k] - 55u : 8u) * u;
                g->patrol_hi = (k_st_gang[k] + 55u) * u;
                g->cooldown = 24u + (k & 3u) * 6u;
            }
        }
        for (k = 0; k < 4u; k++) {
            stage_enemy *pb = spawn(e, STAGE_ENEMY_POOF_BLUE, k_st_blue[k], d2, u);
            if (pb) pb->y = (116u + (k & 1u) * 20u) * u;
        }
        for (k = 0; k < 3u; k++) {
            stage_enemy *pr = spawn(e, STAGE_ENEMY_POOF_RED, k_st_red[k], d2, u);
            if (pr) pr->y = (128u - (k & 1u) * 18u) * u;
        }
        spawn(e, STAGE_ENEMY_LORD, 860u, d, u);
        spawn(e, STAGE_ENEMY_LORD, 1660u, d, u);
    } else if (!strcmp(stage_name, "LOGS")) {
        static const unsigned k_lg_blue[6] = { 120u, 420u, 820u, 1280u, 1740u, 2140u };
        static const unsigned k_lg_gang[5] = { 220u, 620u, 1060u, 1520u, 1960u };
        static const unsigned k_lg_lord[4] = { 480u, 960u, 1440u, 2240u };
        unsigned k;
        for (k = 0; k < 6u; k++) spawn(e, STAGE_ENEMY_POOF_BLUE, k_lg_blue[k], d2, u);
        for (k = 0; k < 5u; k++) {
            stage_enemy *g = spawn(e, STAGE_ENEMY_GANG, k_lg_gang[k], d, u);
            if (g) g->cooldown = 36u + k * 5u;
        }
        for (k = 0; k < 4u; k++) spawn(e, STAGE_ENEMY_LORD, k_lg_lord[k], d, u);
    } else if (!strcmp(stage_name, "ICE1") || !strcmp(stage_name, "ICE2")) {
        int ice2 = (!strcmp(stage_name, "ICE2"));
        static const unsigned k_ic_blue[4] = { 142u, 340u, 620u, 880u };
        static const unsigned k_ic_gang[3] = { 196u, 450u, 760u };
        static const unsigned k_ic_lord[3] = { 270u, 560u, 920u };
        unsigned k;
        for (k = 0; k < 4u; k++) spawn(e, STAGE_ENEMY_POOF_BLUE, k_ic_blue[k], d2, u);
        for (k = 0; k < 3u; k++) {
            stage_enemy *g = spawn(e, STAGE_ENEMY_GANG, k_ic_gang[k], d, u);
            if (g) g->cooldown = ice2 ? (28u + k * 4u) : (42u + k * 4u);
        }
        for (k = 0; k < 3u; k++) spawn(e, STAGE_ENEMY_LORD, k_ic_lord[k], d, u);
        if (ice2) {
            spawn(e, STAGE_ENEMY_POOF_RED, 210u, d, u);
            spawn(e, STAGE_ENEMY_POOF_RED, 490u, d, u);
            spawn(e, STAGE_ENEMY_POOF_RED, 820u, d, u);
        }
    } else if (!strcmp(stage_name, "RIDE")) {
        static const unsigned k_rd_gang[6] = { 126u, 380u, 740u, 1140u, 1560u, 1980u };
        static const unsigned k_rd_red[6]  = { 260u, 620u, 1020u, 1420u, 1840u, 2200u };
        static const unsigned k_rd_lord[5] = { 520u, 920u, 1340u, 1740u, 2140u };
        unsigned k;
        for (k = 0; k < 6u; k++) {
            stage_enemy *g = spawn(e, STAGE_ENEMY_GANG, k_rd_gang[k], d, u);
            if (g) g->cooldown = 34u + k * 4u;
        }
        for (k = 0; k < 6u; k++) spawn(e, STAGE_ENEMY_POOF_RED, k_rd_red[k], d2, u);
        for (k = 0; k < 5u; k++) spawn(e, STAGE_ENEMY_LORD, k_rd_lord[k], d, u);
    } else {
        stage_enemies_init_runway(e, u);
        e->stage_name = stage_name;
    }
}

void stage_enemies_init_special(stage_enemies *e, const char *stage_name,
                                unsigned units_per_pixel) {
    unsigned u = units_per_pixel ? units_per_pixel : 2341u;
    int32_t d = RUNWAY_DAMAGE_DEFAULT;
    int32_t d2 = RUNWAY_DAMAGE_DEFAULT / 2;
    if (!e) return;
    clear_bone_shards();
    memset(e, 0, sizeof *e);
    e->units_per_pixel = u;
    e->stage_name = stage_name ? stage_name : "";
    if (!stage_name) return;

    if (!strcmp(stage_name, "GENRM") || !strcmp(stage_name, "GENROOM")) {
        spawn(e, STAGE_ENEMY_RED, 100u, d, u);
        spawn(e, STAGE_ENEMY_BLUE, 180u, d, u);
        spawn(e, STAGE_ENEMY_ZAP, 140u, d2, u);
        spawn(e, STAGE_ENEMY_POOF_RED, 225u, d2, u);
        e->ents[2].cooldown = TIMING_ZAP_MAX;
    } else if (!strcmp(stage_name, "BIGBNS") || !strcmp(stage_name, "BIGBONES")) {
        spawn(e, STAGE_ENEMY_LORD, 160u, d, u);
        spawn(e, STAGE_ENEMY_GANG, 172u, d, u);
        spawn(e, STAGE_ENEMY_POOF_RED, 100u, d2, u);
        e->ents[1].hp = 2;
        e->ents[1].cooldown = 45u;
        e->ents[1].vx = -(int32_t)u;
        e->ents[1].patrol_lo = 156u * u;
        e->ents[1].patrol_hi = 196u * u;
    } else if (!strcmp(stage_name, "FINALE")) {
        /* Boss: CFBones hovering above altar + ring/bolt attacks from RingDefinitionTable. */
        stage_enemy *fb = spawn(e, STAGE_ENEMY_LORD, 160u, d, u);
        if (fb) {
            fb->y = 124u * u;
            fb->patrol_lo = 112u * u;
            fb->patrol_hi = 208u * u;
            fb->hp = 5;
        }
        spawn(e, STAGE_ENEMY_RING, 180u, d2, u);
        spawn(e, STAGE_ENEMY_BOLT, 220u, d, u);
        e->ents[1].cooldown = 24u;
        e->ents[2].cooldown = 32u;
    }
}

static int in_range(uint32_t a, uint32_t b, uint32_t range) {
    uint32_t d = (a > b) ? (a - b) : (b - a);
    return d <= range;
}

static int hit_bones(stage_enemies *e, runway_skel *skel, stage_enemy *ent) {
    if (!ent->active || !skel || skel->died || skel->paused) return 0;
    if (runway_skel_hit(skel, ent->damage)) {
        e->hits_applied++;
        return 1;
    }
    if (skel->hit_cooldown == RUNWAY_HIT_TIMEOUT)
        e->hits_applied++;
    return 0;
}

static void patrol(stage_enemy *ent) {
    int64_t nx = (int64_t)ent->x + (int64_t)ent->vx;
    if (nx < (int64_t)ent->patrol_lo) {
        nx = (int64_t)ent->patrol_lo;
        ent->vx = -ent->vx;
    } else if (nx > (int64_t)ent->patrol_hi) {
        nx = (int64_t)ent->patrol_hi;
        ent->vx = -ent->vx;
    }
    ent->x = (uint32_t)nx;
    ent->anim++;
}

static void spawn_pop(stage_enemies *e, uint32_t x, uint32_t y) {
    unsigned i, slot = 0;
    if (!e) return;
    for (i = 0; i < STAGE_POP_MAX; i++) {
        if (!e->pops[i].active) {
            slot = i;
            break;
        }
        if (e->pops[i].ttl < e->pops[slot].ttl) slot = i;
    }
    e->pops[slot].x = x;
    e->pops[slot].y = y;
    e->pops[slot].ttl = 14u;
    e->pops[slot].active = 1;
}

static int is_stage_bigbns(const stage_enemies *e) {
    return e && e->stage_name &&
           (!strcmp(e->stage_name, "BIGBNS") ||
            !strcmp(e->stage_name, "BIGBONES"));
}

static int is_stage_genrm(const stage_enemies *e) {
    return e && e->stage_name &&
           (!strcmp(e->stage_name, "GENRM") ||
            !strcmp(e->stage_name, "GENROOM"));
}

static int is_stage_vortex(const stage_enemies *e) {
    return e && e->stage_name && !strcmp(e->stage_name, "VORTEX");
}

static int is_stage_glass(const stage_enemies *e) {
    return e && e->stage_name && !strcmp(e->stage_name, "GLASS");
}

static int is_stage_staing(const stage_enemies *e) {
    return e && e->stage_name && !strcmp(e->stage_name, "STAING");
}

static int is_stage_laff(const stage_enemies *e) {
    return e && e->stage_name && !strcmp(e->stage_name, "LAFF");
}

static int is_stage_finale(const stage_enemies *e) {
    return e && e->stage_name && !strcmp(e->stage_name, "FINALE");
}

static int is_stage_logs(const stage_enemies *e) {
    return e && e->stage_name && !strcmp(e->stage_name, "LOGS");
}

static int is_stage_moat(const stage_enemies *e) {
    return e && e->stage_name && !strcmp(e->stage_name, "MOAT");
}

static int is_stage_runway(const stage_enemies *e) {
    return e && e->stage_name && !strcmp(e->stage_name, "RUNWAY");
}

/* 06VALLEY: valley_skel GANG/LORD look toward Bones (render-only facing). */
static int is_stage_valley(const stage_enemies *e) {
    return e && e->stage_name && !strcmp(e->stage_name, "VALLEY");
}

/* Floating-enemy stages whose gang/lord/poof hover off the floor line. */
static int is_floater_stage(const stage_enemies *e) {
    return is_stage_glass(e) || is_stage_vortex(e) ||
           is_stage_logs(e) || is_stage_moat(e);
}

/*
 * Per-kind horizontal half-width for MELEE contact. Tightens oversized small
 * kinds to their silhouettes; GANG/LORD(generic)/THROWN/RING/BOLT keep the raw
 * STAGE_ENEMY_HIT_RANGE so their asserted hit/defeat counts are unchanged.
 */
static uint32_t enemy_contact_range(const stage_enemy *ent, unsigned u) {
    switch (ent->kind) {
    case STAGE_ENEMY_LILGUYS: return 9u * u;
    case STAGE_ENEMY_ZAP:     return 12u * u;
    case STAGE_ENEMY_WINGNUT: return 14u * u;
    case STAGE_ENEMY_BAT:     return 22u * u;
    case STAGE_ENEMY_SHADOW:  return 12u * u;
    default:                  return STAGE_ENEMY_HIT_RANGE;
    }
}

static void spawn_laff_thrown(stage_enemies *e, uint32_t x, uint32_t y,
                              int32_t vx, int32_t damage) {
    unsigned k;
    stage_enemy *proj = NULL;
    if (!e) return;
    for (k = 1u; k < e->count; k++) {
        if (!e->ents[k].active && e->ents[k].kind == STAGE_ENEMY_THROWN) {
            proj = &e->ents[k];
            break;
        }
    }
    if (!proj) {
        if (e->count >= STAGE_ENEMY_MAX) return;
        proj = &e->ents[e->count++];
    }
    memset(proj, 0, sizeof *proj);
    proj->kind = STAGE_ENEMY_THROWN;
    proj->x = x;
    proj->y = y;
    proj->vx = vx;
    proj->damage = damage > 0 ? damage : RUNWAY_DAMAGE_DEFAULT / 2;
    proj->active = 1;
    proj->hp = 1;
    proj->cooldown = 75u;
}

static int swoop_params(const stage_enemies *e, int *range_px, int *dive_rate_px) {
    if (e && e->stage_name && (!strcmp(e->stage_name, "BAT3") || !strcmp(e->stage_name, "BAT2"))) {
        *range_px = 124;
        *dive_rate_px = !strcmp(e->stage_name, "BAT3") ? 3 : 2;
    } else {
        *range_px = 96;
        *dive_rate_px = 2;
    }
    return 0;
}

static void step_swoop_flyer(const stage_enemies *e, stage_enemy *ent,
                             uint32_t bones_x, uint32_t ground_y, unsigned u) {
    uint32_t hover_y = (ground_y > 44u * u) ? (ground_y - 44u * u) : (16u * u);
    uint32_t dive_y = (ground_y > 14u * u) ? (ground_y - 14u * u) : ground_y;
    uint32_t dx = (bones_x > ent->x) ? (bones_x - ent->x) : (ent->x - bones_x);
    int range_px = 96, dive_rate = 2;
    (void)swoop_params(e, &range_px, &dive_rate);
    if (ent->anim == 0u && (ent->y > ground_y || ent->y + 64u * u < hover_y))
        ent->y = hover_y;
    patrol(ent);
    if (ent->cooldown > 0) {
        ent->cooldown--;
        if (ent->y > hover_y) {
            uint32_t step_up = (uint32_t)dive_rate * u;
            ent->y = (ent->y - hover_y > step_up) ? (ent->y - step_up) : hover_y;
        }
        return;
    }
    if (dx <= STAGE_ENEMY_HIT_RANGE) {
        /* Immediate dive-through on strike column */
        uint32_t plunge = 18u * u;
        if (ent->y + plunge < dive_y) ent->y += plunge;
        else ent->y = dive_y;
    } else if (dx <= (uint32_t)range_px * u) {
        uint32_t dy = (uint32_t)dive_rate * u;
        if (ent->y + dy < dive_y) {
            ent->y += dy;
        } else {
            ent->y = dive_y;
            ent->cooldown = 36u;
        }
    } else if (ent->y > hover_y) {
        uint32_t step_up = 1u * u;
        ent->y = (ent->y - hover_y > step_up) ? (ent->y - step_up) : hover_y;
    }
}

static void damage_enemy(stage_enemies *e, stage_enemy *ent, int dmg) {
    if (!e || !ent || !ent->active) return;
    ent->hp -= dmg > 0 ? dmg : 1;
    if (ent->hp <= 0) {
        if (ent->kind == STAGE_ENEMY_RED && is_stage_genrm(e)) {
            uint32_t u = e->units_per_pixel ? e->units_per_pixel : 2341u;
            ent->kind = STAGE_ENEMY_BLUE;
            ent->hp = 1;
            ent->cooldown = 60u;
            ent->vx = -(int32_t)(2u * u);
            e->defeated_count++;
            spawn_pop(e, ent->x, ent->y);
            return;
        }
        ent->hp = 0;
        ent->active = 0;
        e->defeated_count++;
        spawn_pop(e, ent->x, ent->y);
    }
}

static void step_player_shots_and_pickups(stage_enemies *e, runway_skel *skel,
                                          uint32_t bones_x, uint32_t bones_y,
                                          uint32_t ground_y, unsigned u,
                                          runway_hazards *haz) {
    unsigned i, j;
    if (e->shot_cooldown > 0) e->shot_cooldown--;
    if (e->pulse_cooldown > 0) e->pulse_cooldown--;

    for (i = 0; i < STAGE_POP_MAX; i++) {
        if (!e->pops[i].active) continue;
        if (e->pops[i].ttl > 0)
            e->pops[i].ttl--;
        if (e->pops[i].ttl == 0)
            e->pops[i].active = 0;
    }

    for (i = 0; i < e->pickup_count && i < STAGE_PICKUP_MAX; i++) {
        stage_pickup *p = &e->pickups[i];
        if (!p->active) continue;
        p->anim++;
        if (in_range(bones_x, p->x, 12u * u) &&
            (in_range(bones_y, p->y, 48u * u) ||
             in_range(bones_y, ground_y, 32u * u))) {
            p->active = 0;
            e->pickups_collected++;
            if (skel)
                (void)runway_skel_add(
                    skel, p->heal > 0 ? p->heal : RUNWAY_DAMAGE_DEFAULT);
        }
    }

    for (i = 0; i < STAGE_SHOT_MAX; i++) {
        stage_blues_shot *s = &e->shots[i];
        uint32_t rng;
        if (!s->active) continue;
        s->anim++;
        if (s->ttl == 0) {
            s->active = 0;
            continue;
        }
        s->ttl--;
        if (s->is_radial) {
            s->x = bones_x;
            s->y = bones_y;
            rng = 42u * u;
        } else {
            int64_t nx = (int64_t)s->x + (int64_t)s->vx;
            if (nx < 0) {
                s->active = 0;
                continue;
            }
            s->x = (uint32_t)nx;
            rng = 15u * u;
        }

        if (haz && (!s->is_radial || s->ttl >= 9u)) {
            for (j = 0; j < RUNWAY_THROWN_MAX; j++) {
                if (!haz->thrown[j].active) continue;
                if (in_range(s->x, haz->thrown[j].x, rng)) {
                    haz->thrown[j].active = 0;
                    spawn_pop(e, haz->thrown[j].x, haz->thrown[j].y);
                    if (!s->is_radial) {
                        s->active = 0;
                        break;
                    }
                }
            }
        }
        if (!s->active) continue;
        if (!s->is_radial || s->ttl == 11u) {
            int genrm = is_stage_genrm(e);
            for (j = 0; j < e->count; j++) {
                stage_enemy *ent = &e->ents[j];
                if (!ent->active) continue;
                if (genrm && ent->kind == STAGE_ENEMY_BLUE) continue;
                if (in_range(s->x, ent->x, rng)) {
                    if (ent->kind == STAGE_ENEMY_THROWN) {
                        ent->active = 0;
                        spawn_pop(e, ent->x, ent->y);
                        if (!s->is_radial) {
                            s->active = 0;
                            break;
                        }
                        continue;
                    }
                    if (genrm && ent->kind == STAGE_ENEMY_ZAP) {
                        spawn_pop(e, ent->x, ent->y);
                        if (!s->is_radial) {
                            s->active = 0;
                            break;
                        }
                        continue;
                    }
                    if (ent->kind == STAGE_ENEMY_SHADOW) {
                        ent->cooldown = s->is_radial ? 90u : 60u;
                        damage_enemy(e, ent, s->is_radial ? 2 : s->power);
                    } else {
                        int was_active = ent->active;
                        int dmg = (s->is_radial && ent->kind == STAGE_ENEMY_LORD &&
                                   is_stage_laff(e))
                                      ? 2
                                      : s->power;
                        damage_enemy(e, ent, dmg);
                        if (was_active && !ent->active &&
                            ent->kind == STAGE_ENEMY_POOF_BLUE &&
                            is_stage_vortex(e) && skel) {
                            runway_skel_add(skel, RUNWAY_DAMAGE_DEFAULT / 2);
                        }
                    }
                    if (ent->active) {
                        int kb_dir = s->is_radial ? ((ent->x >= bones_x) ? 1 : -1)
                                                  : ((s->vx >= 0) ? 1 : -1);
                        int64_t kx = (int64_t)ent->x + (int64_t)kb_dir * (int64_t)(8u * u);
                        if (kx < (int64_t)ent->patrol_lo) kx = (int64_t)ent->patrol_lo;
                        if (kx > (int64_t)ent->patrol_hi) kx = (int64_t)ent->patrol_hi;
                        ent->x = (uint32_t)kx;
                        ent->throw_hold = 5u;
                    }
                    spawn_pop(e, ent->x, ent->y);
                    if (!s->is_radial) {
                        s->active = 0;
                        break;
                    }
                }
            }
        }
        if (s->active && s->ttl == 0) s->active = 0;
    }
}

int stage_enemies_fire_blues(stage_enemies *e, uint32_t bones_x,
                             uint32_t bones_y, int facing, int is_radial) {
    unsigned i, u;
    if (!e) return 0;
    u = e->units_per_pixel ? e->units_per_pixel : 2341u;
    if (is_radial) {
        if (e->pulse_cooldown > 0) return 0;
        for (i = 0; i < STAGE_SHOT_MAX; i++)
            if (!e->shots[i].active) break;
        if (i >= STAGE_SHOT_MAX) i = 0u;
        e->shots[i].x = bones_x;
        e->shots[i].y = bones_y;
        e->shots[i].vx = 0;
        e->shots[i].anim = 0;
        e->shots[i].power = 1;
        e->shots[i].ttl = 12u;
        e->shots[i].is_radial = 1;
        e->shots[i].active = 1;
        e->pulse_cooldown = 16u;
        e->bolts_fired++;
        return 1;
    }
    if (e->shot_cooldown > 0) return 0;
    for (i = 0; i < STAGE_SHOT_MAX; i++)
        if (!e->shots[i].active) break;
    if (i >= STAGE_SHOT_MAX) return 0;
    e->shots[i].x = bones_x;
    e->shots[i].y = bones_y;
    e->shots[i].vx = (facing < 0 ? -1 : 1) * (int32_t)(7u * u);
    e->shots[i].anim = 0;
    e->shots[i].power = 1;
    e->shots[i].ttl = 32u;
    e->shots[i].is_radial = 0;
    e->shots[i].active = 1;
    e->shot_cooldown = 10u;
    e->bolts_fired++;
    return 1;
}

void stage_enemies_add_health_pickup(stage_enemies *e, unsigned px,
                                     unsigned py_px, int32_t heal) {
    unsigned u;
    stage_pickup *p;
    if (!e || e->pickup_count >= STAGE_PICKUP_MAX) return;
    u = e->units_per_pixel ? e->units_per_pixel : 2341u;
    p = &e->pickups[e->pickup_count++];
    p->x = px * u;
    p->y = py_px * u;
    p->heal = heal > 0 ? heal : RUNWAY_DAMAGE_DEFAULT;
    p->anim = 0;
    p->active = 1;
}

static void enemy_melee_recoil(stage_enemies *e, stage_enemy *ent,
                               uint32_t bones_x, unsigned u) {
    int push_right = (ent->x >= bones_x);
    uint32_t mid = (bones_x + ent->x) / 2u;
    if (push_right) {
        ent->x += 16u * u;
        if (ent->vx < 0) ent->vx = -ent->vx;
        if (ent->vx == 0) ent->vx = (int32_t)(1u * u);
    } else {
        ent->x = (ent->x > 16u * u) ? (ent->x - 16u * u) : 0u;
        if (ent->vx > 0) ent->vx = -ent->vx;
        if (ent->vx == 0) ent->vx = -(int32_t)(1u * u);
    }
    if (ent->cooldown < 28u) ent->cooldown = 28u;
    spawn_pop(e, mid, ent->y);
}

static int batwood_enemy_overlap(const stage_enemy *enemy, unsigned units_per_pixel,
                                 const stage_batwood_contact *contact);

int stage_enemies_step_player_bounded(stage_enemies *e, runway_skel *skel,
                                     uint32_t bones_x, uint32_t bones_y,
                                     uint32_t ground_y, int tall, int airborne,
                                     runway_hazards *haz, int apply_hits,
                                     const stage_batwood_contact *contact) {
    unsigned i;
    unsigned u;
    int high_jump;
    int bigbns, genrm;
    if (!e) return 0;
    e->bumper_rebound_vx = 0;
    e->bumper_rebound_x = 0;
    if (skel && (skel->died || skel->paused) && apply_hits) return 0;
    u = e->units_per_pixel ? e->units_per_pixel : 2341u;
    high_jump = (airborne && bones_y + 6u * u <= ground_y) ? 1 : 0;
    bigbns = is_stage_bigbns(e);
    genrm = is_stage_genrm(e);

    tick_bone_shards(u);

    step_player_shots_and_pickups(e, skel, bones_x, bones_y, ground_y, u, haz);

    for (i = 0; i < e->count; i++) {
        stage_enemy *ent = &e->ents[i];
        if (!ent->active) continue;

        switch (ent->kind) {
        case STAGE_ENEMY_GANG: {
            unsigned j;
            if (ent->recorded) {
                /* Only Reach checks for a body hit, once per dive, with one world
                 * unit of horizontal range. Running past Bones does not attack. */
                if (ent->anim >> 5 != 4u) ent->throw_hold = 0;
                else if (apply_hits && skel && !ent->throw_hold && !high_jump &&
                         ent->screen_x >= 0 && ent->screen_x < 320 &&
                         in_range(bones_x, ent->x, 0x10000u) && in_range(bones_y, ent->y, 0xc000u)) {
                    ent->throw_hold = 1;
                    if (hit_bones(e, skel, ent)) return 1;
                }
                break;
            }
            int glass = is_stage_glass(e);
            int valley = is_stage_valley(e);
            int runway_chaser = (is_stage_runway(e) &&
                                 ent->patrol_lo <= 16u * u &&
                                 ent->patrol_hi >= 1500u * u);
            if (valley) {
                ent->anim++;
                if (!in_range(bones_x, ent->x, 14u * u) &&
                    in_range(bones_x, ent->x, 140u * u)) {
                    ent->x = (bones_x > ent->x) ? (ent->x + u)
                                                : (ent->x > u ? ent->x - u : 0u);
                }
            } else if (runway_chaser) {
                uint32_t chase_spd = 2u * u;
                ent->anim++;
                if (bones_x > ent->x + 12u * u) {
                    ent->vx = (int32_t)chase_spd;
                    ent->x += chase_spd;
                } else if (ent->x > bones_x + 16u * u) {
                    ent->vx = -(int32_t)chase_spd;
                    ent->x -= chase_spd;
                } else {
                    patrol(ent);
                }
                if (ent->x < ent->patrol_lo) ent->x = ent->patrol_lo;
                if (ent->x > ent->patrol_hi) ent->x = ent->patrol_hi;
            } else {
                patrol(ent);
            }
            /* Keep gang silhouettes separated (>= 36px) so skeletons never pile into clumps. */
            if (!glass && !bigbns && !valley) {
                uint32_t min_sep = 36u * u;
                for (j = 0; j < i; j++) {
                    const stage_enemy *other = &e->ents[j];
                    if (!other->active || other->kind != STAGE_ENEMY_GANG) continue;
                    if (!in_range(ent->x, other->x, min_sep)) continue;
                    if (ent->x >= other->x && ent->vx < 0)
                        ent->vx = -ent->vx;
                    else if (ent->x < other->x && ent->vx > 0)
                        ent->vx = -ent->vx;
                }
            }
            if (ent->throw_hold > 0u) {
                ent->throw_hold--;
                ent->throw_phase = 5u;
                if (ent->throw_hold == 0u) ent->throw_phase = 0u;
            } else {
                /* VALLEY skeletons watch Bones instead of their patrol heading. */
                if (valley)
                    ent->facing = (bones_x >= ent->x) ? 1 : -1;
                else
                    ent->facing = (ent->vx < 0) ? -1 : 1;
                ent->throw_phase = 0u;
            }
            if (glass) {
                unsigned p = ent->anim & 31u;
                unsigned wave = (p <= 16u) ? p : (32u - p);
                ent->y = (ground_y > (6u + wave * 2u) * u)
                             ? (ground_y - (6u + wave * 2u) * u)
                             : 36u * u;
            }
            if (bigbns) {
                /* 16BIGBNS BUMPER (0x041D0): kinetic bounce away instead of passive overlap. */
                if (!high_jump &&
                    in_range(bones_x, ent->x, STAGE_ENEMY_HIT_RANGE + 4u * u)) {
                    int dir = (bones_x < ent->x) ? -1 : 1;
                    int64_t push_x =
                        (int64_t)ent->x + (int64_t)dir * (int64_t)(16u * u);
                    if (push_x < 4 * (int64_t)u) push_x = 4 * (int64_t)u;
                    if (push_x > (int64_t)(316u * u))
                        push_x = (int64_t)(316u * u);
                    e->bumper_rebound_vx = (int32_t)(dir * 8 * (int)u);
                    e->bumper_rebound_x = (uint32_t)push_x;
                    spawn_pop(e, ent->x, ent->y);
                }
                break;
            }
            if (!glass) {
                if (ent->cooldown) ent->cooldown--;
                if (ent->cooldown > 0u && ent->cooldown <= 4u &&
                    in_range(bones_x, ent->x, 140u * u)) {
                    ent->facing = (bones_x >= ent->x) ? 1 : -1;
                    ent->throw_phase = 4u;
                }
                if (ent->cooldown == 0) {
                    /* Throw__11CGangMember -> CThrown toward Bones when in range. */
                    if (in_range(bones_x, ent->x, 140u * u)) {
                        int32_t tvx = (bones_x >= ent->x) ? (int32_t)(2u * u)
                                                         : -(int32_t)(2u * u);
                        uint32_t tx_spawn;
                        ent->facing = (bones_x >= ent->x) ? 1 : -1;
                        tx_spawn = (ent->facing >= 0)
                                       ? (ent->x + 8u * u)
                                       : (ent->x > 8u * u ? ent->x - 8u * u : 0u);
                        ent->throw_phase = 5u;
                        ent->throw_hold = 4u;
                        if (haz)
                            runway_hazards_spawn_thrown_vel(
                                haz, tx_spawn, ent->y, tvx, ent->damage / 2);
                        else
                            spawn_laff_thrown(e, tx_spawn, ent->y, tvx,
                                              ent->damage / 2);
                        e->throws_spawned++;
                    }
                    ent->cooldown = is_stage_staing(e) ? 26u : (valley ? 64u : 48u);
                }
            }
            if (apply_hits && skel && !high_jump &&
                in_range(bones_x, ent->x, STAGE_ENEMY_HIT_RANGE) &&
                (!is_floater_stage(e) || ent->y == 0u ||
                 in_range(ent->y, bones_y, 40u * u))) {
                int died = hit_bones(e, skel, ent);
                enemy_melee_recoil(e, ent, bones_x, u);
                if (died) return 1;
            }
            break;
        }

        case STAGE_ENEMY_LORD:
            if (is_stage_laff(e)) {
                int enraged = (ent->hp <= STAGE_LAFF_BOSS_HP / 2);
                unsigned spd = enraged ? 4u : 3u;
                unsigned cycle = enraged ? 44u : 56u;
                unsigned t = ent->anim % cycle;
                unsigned hop_px = (t < 24u) ? ((t * (24u - t)) / 4u) : 0u;
                if (bones_x + 220u * u >= ent->patrol_lo && (ent->anim % 44u) < 20u) {
                    ent->vx = (bones_x >= ent->x) ? (int32_t)(spd * u)
                                                  : -(int32_t)(spd * u);
                } else {
                    ent->vx = (ent->vx >= 0) ? (int32_t)(spd * u)
                                             : -(int32_t)(spd * u);
                }
                patrol(ent);
                ent->y = (ground_y > hop_px * u) ? (ground_y - hop_px * u)
                                                 : (16u * u);
                if (ent->cooldown) ent->cooldown--;
                if (ent->cooldown == 0) {
                    if (in_range(bones_x, ent->x, 190u * u)) {
                        int32_t tvx = (bones_x >= ent->x) ? (int32_t)(3u * u)
                                                         : -(int32_t)(3u * u);
                        uint32_t py = (ground_y > 16u * u)
                                          ? (ground_y - 16u * u)
                                          : ground_y;
                        if (haz)
                            runway_hazards_spawn_thrown_vel(
                                haz, ent->x, ent->y, tvx, ent->damage / 2);
                        else
                            spawn_laff_thrown(e, ent->x, py, tvx,
                                              ent->damage / 2);
                        e->throws_spawned++;
                    }
                    ent->cooldown = enraged ? 28u : 42u;
                }
                if (hop_px < 18u && apply_hits && skel && !high_jump &&
                    in_range(bones_x, ent->x, 28u * u)) {
                    int died = hit_bones(e, skel, ent);
                    enemy_melee_recoil(e, ent, bones_x, u);
                    if (died) return 1;
                }
                break;
            }
            if (is_stage_finale(e)) {
                patrol(ent);
                ent->y = (ground_y > 52u * u) ? (ground_y - 52u * u)
                                              : (112u * u);
                break;
            }
            if (is_stage_valley(e)) {
                ent->anim++;
                ent->facing = (bones_x >= ent->x) ? 1 : -1;
                if (!in_range(bones_x, ent->x, 16u * u) &&
                    in_range(bones_x, ent->x, 140u * u)) {
                    ent->x = (bones_x > ent->x) ? (ent->x + u)
                                                : (ent->x > u ? ent->x - u : 0u);
                }
            } else {
                patrol(ent);
            }
            if (is_stage_glass(e)) {
                unsigned p = (ent->anim + 12u) & 31u;
                unsigned wave = (p <= 16u) ? p : (32u - p);
                ent->y = (ground_y > (8u + wave * 2u) * u)
                             ? (ground_y - (8u + wave * 2u) * u)
                             : 32u * u;
            }
            if (apply_hits && skel && !high_jump &&
                in_range(bones_x, ent->x, STAGE_ENEMY_HIT_RANGE + (8u * u)) &&
                (!is_floater_stage(e) || ent->y == 0u ||
                 in_range(ent->y, bones_y, 40u * u))) {
                int died = hit_bones(e, skel, ent);
                enemy_melee_recoil(e, ent, bones_x, u);
                if (died) return 1;
            }
            break;

        case STAGE_ENEMY_POOF_RED:
        case STAGE_ENEMY_POOF_BLUE:
            patrol(ent);
            if (ent->kind == STAGE_ENEMY_POOF_RED && is_stage_vortex(e) &&
                in_range(bones_x, ent->x, 110u * u) &&
                !in_range(bones_x, ent->x, 2u * u)) {
                int32_t step = (int32_t)(1u * u);
                ent->vx = (bones_x > ent->x) ? (int32_t)(2u * u) : -(int32_t)(2u * u);
                ent->x = (bones_x > ent->x) ? (ent->x + (uint32_t)step)
                                            : (ent->x - (uint32_t)step);
            }
            if (apply_hits && skel && !high_jump &&
                in_range(bones_x, ent->x, STAGE_ENEMY_HIT_RANGE) &&
                (!is_floater_stage(e) || ent->y == 0u ||
                 in_range(ent->y, bones_y, 40u * u))) {
                if (hit_bones(e, skel, ent)) return 1;
            }
            break;

        case STAGE_ENEMY_RED:
            /* Approach Bones (CheckForBones / BigCheckForBones). */
            if (bones_x + 4u * u < ent->x)
                ent->x -= 3u * u;
            else if (bones_x > ent->x + 4u * u)
                ent->x += 3u * u;
            ent->anim++;
            ent->facing = (bones_x >= ent->x) ? 1 : -1;
            if (apply_hits && skel && !high_jump &&
                in_range(bones_x, ent->x, STAGE_ENEMY_HIT_RANGE)) {
                int died = hit_bones(e, skel, ent);
                enemy_melee_recoil(e, ent, bones_x, u);
                if (died) return 1;
            }
            break;

        case STAGE_ENEMY_BLUE:
            if (genrm) {
                /* Friendly / converted CBlue polarity orb: orbits gently and recharges Blues/Skel. */
                patrol(ent);
                if (ent->cooldown) ent->cooldown--;
                if (!high_jump && ent->cooldown == 0 &&
                    in_range(bones_x, ent->x, STAGE_ENEMY_HIT_RANGE)) {
                    if (skel)
                        (void)runway_skel_add(skel, RUNWAY_DAMAGE_DEFAULT / 2);
                    e->shot_cooldown = 0;
                    spawn_pop(e, ent->x, ent->y);
                    ent->cooldown = 90u;
                }
            } else {
                if (bones_x + 4u * u < ent->x)
                    ent->x -= 3u * u;
                else if (bones_x > ent->x + 4u * u)
                    ent->x += 3u * u;
                ent->anim++;
                ent->facing = (bones_x >= ent->x) ? 1 : -1;
                if (apply_hits && skel && !high_jump &&
                    in_range(bones_x, ent->x, STAGE_ENEMY_HIT_RANGE)) {
                    if (hit_bones(e, skel, ent)) return 1;
                }
            }
            break;

        case STAGE_ENEMY_ZAP:
            ent->anim++;
            if (ent->cooldown) ent->cooldown--;
            if (ent->cooldown == 0) {
                e->zaps_fired++;
                ent->cooldown = TIMING_ZAP_MAX;
                if (apply_hits && skel && !high_jump &&
                    in_range(bones_x, ent->x, enemy_contact_range(ent, u))) {
                    if (hit_bones(e, skel, ent)) return 1;
                }
            }
            break;

        case STAGE_ENEMY_RING: {
            int32_t dx = ring_offset_signed(ent->anim * 2u);
            (void)ring_offset_signed(ent->anim * 2u + 1u);
            if (ent->cooldown) ent->cooldown--;
            if (ent->cooldown == 0) {
                int32_t ox = dx * (int32_t)u / 4;
                ent->x = (ox < 0 && bones_x < (uint32_t)(-ox))
                             ? 0u
                             : bones_x + (uint32_t)ox;
                e->rings_fired++;
                ent->cooldown = 24u;
                ent->anim++;
                if (apply_hits && skel && !tall &&
                    in_range(bones_x, ent->x, STAGE_ENEMY_HIT_RANGE)) {
                    if (hit_bones(e, skel, ent)) return 1;
                }
            }
            break;
        }

        case STAGE_ENEMY_BOLT:
            ent->anim++;
            if (ent->cooldown) ent->cooldown--;
            /* Lock target column during last 10 frames as a dodgeable warning telegraph. */
            if (ent->cooldown > 10u) {
                if (bones_x + 2u * u < ent->x)
                    ent->x -= 2u * u;
                else if (bones_x > ent->x + 2u * u)
                    ent->x += 2u * u;
            }
            if (ent->cooldown == 0) {
                e->bolts_fired++;
                ent->cooldown = 32u;
                if (apply_hits && skel && !tall &&
                    in_range(bones_x, ent->x, STAGE_ENEMY_HIT_RANGE)) {
                    if (hit_bones(e, skel, ent)) return 1;
                }
            }
            break;

        case STAGE_ENEMY_THROWN: {
            int64_t nx = (int64_t)ent->x + (int64_t)ent->vx;
            if (nx < 0 || ent->anim > 90u || (ent->cooldown && --ent->cooldown == 0)) {
                ent->active = 0;
                break;
            }
            ent->x = (uint32_t)nx;
            ent->anim++;
            if (apply_hits && skel && !high_jump &&
                in_range(bones_x, ent->x, STAGE_ENEMY_HIT_RANGE)) {
                int dead = hit_bones(e, skel, ent);
                ent->active = 0;
                if (dead) return 1;
            }
            break;
        }

        case STAGE_ENEMY_ZOMBIE: {
            const uint32_t close_range = 8u * u;
            if (in_range(bones_x, ent->x, 110u * u) &&
                !in_range(bones_x, ent->x, close_range)) {
                ent->vx = (bones_x > ent->x) ? (int32_t)(1u * u)
                                             : -(int32_t)(1u * u);
                ent->facing = (bones_x >= ent->x) ? 1 : -1;
                ent->x = (bones_x > ent->x) ? (ent->x + u)
                                            : (ent->x > u ? ent->x - u : 0u);
                ent->anim++;
            } else {
                patrol(ent);
            }
            if (apply_hits && skel && !high_jump &&
                (contact ? in_range(bones_x, ent->x, close_range) &&
                               batwood_enemy_overlap(ent, u, contact)
                         : in_range(bones_x, ent->x, STAGE_ENEMY_HIT_RANGE))) {
                int died = hit_bones(e, skel, ent);
                enemy_melee_recoil(e, ent, bones_x, u);
                if (died) return 1;
            }
            break;
        }

        case STAGE_ENEMY_BAT:
        case STAGE_ENEMY_WINGNUT:
            step_swoop_flyer(e, ent, bones_x, ground_y, u);
            if (apply_hits && skel && !tall && !high_jump &&
                in_range(bones_x, ent->x, enemy_contact_range(ent, u)) &&
                (!contact || ent->kind != STAGE_ENEMY_BAT ||
                 batwood_enemy_overlap(ent, u, contact))) {
                if (hit_bones(e, skel, ent)) return 1;
            }
            break;

        case STAGE_ENEMY_HAND:
            ent->anim++;
            ent->facing = (bones_x >= ent->x) ? 1 : -1;
            if (apply_hits && skel && !high_jump &&
                in_range(bones_x, ent->x, STAGE_ENEMY_HIT_RANGE)) {
                if (hit_bones(e, skel, ent)) return 1;
            }
            break;

        case STAGE_ENEMY_LILGUYS:
            patrol(ent);
            if (apply_hits && skel && !high_jump &&
                in_range(bones_x, ent->x, enemy_contact_range(ent, u))) {
                int died = hit_bones(e, skel, ent);
                enemy_melee_recoil(e, ent, bones_x, u);
                if (died) return 1;
            }
            break;

        case STAGE_ENEMY_SHADOW:
            if (ent->cooldown > 0) ent->cooldown--;
            patrol(ent);
            if (ent->cooldown == 0 && in_range(bones_x, ent->x, 84u * u) &&
                !in_range(bones_x, ent->x, 2u * u)) {
                uint32_t step = 2u * u;
                ent->vx = (bones_x > ent->x) ? (int32_t)(3u * u) : -(int32_t)(3u * u);
                ent->x = (bones_x > ent->x) ? (ent->x + step) : (ent->x - step);
            }
            if (apply_hits && skel && !high_jump &&
                in_range(bones_x, ent->x, enemy_contact_range(ent, u))) {
                int died = hit_bones(e, skel, ent);
                enemy_melee_recoil(e, ent, bones_x, u);
                if (died) return 1;
            }
            break;
        }
    }
    if (!is_stage_glass(e) && !bigbns && !is_stage_valley(e)) {
        uint32_t min_sep = 32u * u;
        unsigned a, b;
        for (a = 0; a < e->count; a++) {
            stage_enemy *ea = &e->ents[a];
            if (!ea->active || ea->recorded || ea->kind != STAGE_ENEMY_GANG) continue;
            for (b = a + 1u; b < e->count; b++) {
                stage_enemy *eb = &e->ents[b];
                uint32_t dist;
                if (!eb->active || eb->recorded || eb->kind != STAGE_ENEMY_GANG) continue;
                dist = (ea->x >= eb->x) ? (ea->x - eb->x) : (eb->x - ea->x);
                if (dist < min_sep) {
                    int ea_chase = (is_stage_runway(e) && ea->patrol_lo <= 16u * u && ea->patrol_hi >= 1500u * u);
                    int eb_chase = (is_stage_runway(e) && eb->patrol_lo <= 16u * u && eb->patrol_hi >= 1500u * u);
                    if (ea_chase && eb_chase) {
                        /* Keep the trailing chaser spaced behind without lunging the lead forward. */
                        uint32_t back = (min_sep - dist > 2u * u) ? (2u * u) : (min_sep - dist);
                        if (ea->x <= eb->x)
                            ea->x = (ea->x > ea->patrol_lo + back) ? (ea->x - back) : ea->patrol_lo;
                        else
                            eb->x = (eb->x > eb->patrol_lo + back) ? (eb->x - back) : eb->patrol_lo;
                    } else if (!ea_chase && !eb_chase) {
                        uint32_t push = (min_sep - dist) / 2u + u;
                        if (push > 2u * u) push = 2u * u;
                        if (ea->x <= eb->x) {
                            ea->x = (ea->x > ea->patrol_lo + push) ? (ea->x - push) : ea->patrol_lo;
                            eb->x = (eb->x + push < eb->patrol_hi) ? (eb->x + push) : eb->patrol_hi;
                        } else {
                            eb->x = (eb->x > eb->patrol_lo + push) ? (eb->x - push) : eb->patrol_lo;
                            ea->x = (ea->x + push < ea->patrol_hi) ? (ea->x + push) : ea->patrol_hi;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

int stage_enemies_step_player(stage_enemies *e, runway_skel *skel,
                              uint32_t bones_x, uint32_t bones_y,
                              uint32_t ground_y, int tall, int airborne,
                              runway_hazards *haz, int apply_hits) {
    return stage_enemies_step_player_bounded(e, skel, bones_x, bones_y, ground_y,
                                              tall, airborne, haz, apply_hits, NULL);
}

int stage_enemies_step(stage_enemies *e, runway_skel *skel, uint32_t bones_x,
                       runway_hazards *haz, int apply_hits) {
    unsigned u = (e && e->units_per_pixel) ? e->units_per_pixel : 2341u;
    return stage_enemies_step_player(e, skel, bones_x, 180u * u, 180u * u, 0,
                                     0, haz, apply_hits);
}

unsigned stage_enemies_count_kind(const stage_enemies *e, stage_enemy_kind kind) {
    unsigned i, n = 0;
    if (!e) return 0;
    for (i = 0; i < e->count; i++)
        if (e->ents[i].active && e->ents[i].kind == kind) n++;
    return n;
}

int stage_enemies_laff_boss_alive(const stage_enemies *e) {
    unsigned i;
    if (!e || !is_stage_laff(e)) return 0;
    for (i = 0; i < e->count; i++) {
        if (e->ents[i].active && e->ents[i].kind == STAGE_ENEMY_LORD)
            return 1;
    }
    return 0;
}

int stage_enemies_laff_boss_hp(const stage_enemies *e, int *out_max_hp) {
    unsigned i;
    if (out_max_hp) *out_max_hp = STAGE_LAFF_BOSS_HP;
    if (!e || !is_stage_laff(e)) return 0;
    for (i = 0; i < e->count; i++) {
        if (e->ents[i].active && e->ents[i].kind == STAGE_ENEMY_LORD)
            return e->ents[i].hp > 0 ? e->ents[i].hp : 0;
    }
    return 0;
}

unsigned stage_enemies_targets_remaining(const stage_enemies *e) {
    unsigned i, n = 0;
    if (!e) return 0;
    if (is_stage_laff(e))
        return stage_enemies_laff_boss_alive(e) ? 1u : 0u;
    if (is_stage_genrm(e)) {
        for (i = 0; i < e->count; i++) {
            if (!e->ents[i].active) continue;
            if (e->ents[i].kind == STAGE_ENEMY_RED ||
                e->ents[i].kind == STAGE_ENEMY_POOF_RED)
                n++;
        }
        return n;
    }
    for (i = 0; i < e->count; i++)
        if (e->ents[i].active) n++;
    return n;
}

void stage_enemies_populate_sidescrl_strip(stage_enemies *e, unsigned scroll_w) {
    unsigned u, px, stride = 140u, end_margin = 80u;
    int idx = 0;
    int is_bat = 0, is_village = 0, is_glass = 0, is_vortex = 0, is_shadow = 0, is_laff = 0;
    if (!e || scroll_w <= 360u) return;
    u = e->units_per_pixel ? e->units_per_pixel : 2341u;
    if (e->stage_name) {
        if (!strcmp(e->stage_name, "BAT1") || !strcmp(e->stage_name, "BAT2") ||
            !strcmp(e->stage_name, "BAT3"))
            is_bat = 1;
        else if (!strcmp(e->stage_name, "VILLAGE"))
            is_village = 1;
        else if (!strcmp(e->stage_name, "GLASS"))
            is_glass = 1;
        else if (!strcmp(e->stage_name, "VORTEX"))
            is_vortex = 1;
        else if (!strcmp(e->stage_name, "SHADOW"))
            is_shadow = 1;
        else if (!strcmp(e->stage_name, "LAFF"))
            is_laff = 1;
    }
    if (e->stage_name && !strcmp(e->stage_name, "MAUSO"))
        return;
    if (e->stage_name && (!strcmp(e->stage_name, "BAT2") || !strcmp(e->stage_name, "BAT3")))
        stride = 124u;
    else if (is_village)
        stride = 132u;

    if (is_laff) {
        if (e->count > 0 && e->ents[0].kind == STAGE_ENEMY_LORD) {
            unsigned arena_px = scroll_w - 190u;
            stage_enemy *boss = &e->ents[0];
            boss->x = arena_px * u;
            boss->y = 140u * u;
            boss->hp = STAGE_LAFF_BOSS_HP;
            boss->active = 1;
            boss->patrol_lo = (scroll_w - 320u) * u;
            boss->patrol_hi = (scroll_w - 92u) * u;
            boss->vx = -(int32_t)(3u * u);
            boss->cooldown = 36u;
        }
        stride = 180u;
        end_margin = 420u;
    }

    for (px = 240u; px + end_margin < scroll_w && e->count < STAGE_ENEMY_MAX;
         px += stride, idx++) {
        stage_enemy_kind k;
        stage_enemy *ent;
        unsigned spawn_px = px;
        if (is_bat) {
            k = (idx % 2 == 0) ? STAGE_ENEMY_ZOMBIE : STAGE_ENEMY_BAT;
        } else if (is_village) {
            static const stage_enemy_kind k_vil[4] = {
                STAGE_ENEMY_LILGUYS, STAGE_ENEMY_HAND, STAGE_ENEMY_GANG,
                STAGE_ENEMY_LORD
            };
            k = k_vil[idx & 3];
            /* Keep stationary HAND off the carved river chasms. */
            if (k == STAGE_ENEMY_HAND &&
                ((spawn_px >= 404u && spawn_px <= 468u) ||
                 (spawn_px >= 964u && spawn_px <= 1032u)))
                spawn_px += 52u;
        } else if (is_glass) {
            k = (idx % 2 == 0) ? STAGE_ENEMY_GANG : STAGE_ENEMY_LORD;
        } else if (is_vortex) {
            k = (idx % 2 == 0) ? STAGE_ENEMY_POOF_RED : STAGE_ENEMY_POOF_BLUE;
        } else if (is_shadow) {
            k = (idx % 2 == 0) ? STAGE_ENEMY_SHADOW : STAGE_ENEMY_GANG;
        } else if (is_laff) {
            k = (idx % 2 == 0) ? STAGE_ENEMY_GANG : STAGE_ENEMY_POOF_RED;
        } else {
            k = (idx % 2 == 0) ? STAGE_ENEMY_LORD : STAGE_ENEMY_GANG;
        }
        ent = spawn(e, k, spawn_px,
                    (is_vortex && k == STAGE_ENEMY_POOF_RED)
                        ? RUNWAY_DAMAGE_DEFAULT
                        : RUNWAY_DAMAGE_DEFAULT / 2,
                    u);
        if (ent) {
            ent->anim = (unsigned)(idx * 7);
            if (k == STAGE_ENEMY_GANG && !is_glass)
                ent->cooldown = 50u + (unsigned)(idx * 12);
        }
    }
}

#define STXT_HD_ENEMY_GANG   0
#define STXT_HD_ENEMY_LORD   1
#define STXT_HD_ENEMY_VALLEY 2
#define STXT_HD_ENEMY_ZOMBIE 3
#define STXT_HD_ENEMY_LAFF   4
#define STXT_HD_ENEMY_SHADOW 5

typedef struct {
    uint8_t *rgba;
    unsigned w, h;
} stxt_frame;

static struct {
    int loaded_d1;
    int loaded_d2;
    stxt_frame bones_stand[4];
    unsigned bones_stand_n;
    stxt_frame bones_walk[20];
    unsigned bones_walk_n;
    stxt_frame bones_tall[2];
    unsigned bones_tall_n;
    stxt_frame bones_air[4];
    unsigned bones_air_n;
    stxt_frame bones_stand_hd[4];
    stxt_frame bones_walk_hd[20];
    stxt_frame bones_tall_hd[2];
    stxt_frame bones_air_hd[4];
    int bones_hd_ready;
    int bones_hd_custom_count;
    int bones_suppress_1x;
    stage_bones_pose last_pose;
    int hd_ground_valid;
    int hd_ground_y;
    int hd_night_stage;
    struct {
        int sx, sy, air_px, rx, ry, alpha;
    } hd_shadows[32];
    unsigned hd_shadow_n;
    struct {
        int cx, cy;
        unsigned anim;
        unsigned scale_num, scale_den;
    } hd_thrown[32];
    unsigned hd_thrown_n;
    struct {
        int sx, sy, flip, kind;
        unsigned fidx, scale_num, scale_den;
    } hd_enemies[32];
    unsigned hd_enemy_n;
    stxt_frame gang[8];
    stxt_frame gang_hd[8];
    unsigned gang_hd_factor[8];
    int gang_hd_ready;
    unsigned gang_n;
    stxt_frame thrown[6];
    stxt_frame thrown_hd[6];
    unsigned thrown_hd_factor[6];
    int thrown_hd_ready;
    unsigned thrown_n;
    stxt_frame hud_fill[14];
    unsigned hud_fill_n;
    stxt_frame hud_empty[14];
    unsigned hud_empty_n;
    stxt_frame lord[8];
    stxt_frame lord_hd[8];
    unsigned lord_hd_factor[8];
    int lord_hd_ready;
    unsigned lord_n;
    stxt_frame poof_blue[4];
    unsigned poof_blue_n;
    stxt_frame poof_red[4];
    unsigned poof_red_n;
    stxt_frame bats[12];
    unsigned bats_n;
    stxt_frame zombies[12];
    stxt_frame zombies_hd[12];
    unsigned zombies_hd_factor[12];
    int zombies_hd_ready;
    unsigned zombies_n;
    stxt_frame gen_blue[4];
    unsigned gen_blue_n;
    stxt_frame gen_red[4];
    unsigned gen_red_n;
    stxt_frame gen_zap[4];
    unsigned gen_zap_n;
    stxt_frame bigbns_bumper[16];
    unsigned bigbns_bumper_n;
    stxt_frame bigbns_redpoof[9];
    unsigned bigbns_redpoof_n;
    stxt_frame logs_bluepoof[13];
    unsigned logs_bluepoof_n;
    stxt_frame logs_buttrfly_blue[10];
    unsigned logs_buttrfly_blue_n;
    stxt_frame logs_buttrfly_red[10];
    unsigned logs_buttrfly_red_n;
    stxt_frame vil_mtrspid[13];
    unsigned vil_mtrspid_n;
    stxt_frame vil_spider[16];
    unsigned vil_spider_n;
    stxt_frame moat_wingnut[16];
    unsigned moat_wingnut_n;
    stxt_frame moat_redpoof[9];
    unsigned moat_redpoof_n;
    stxt_frame fin_ring[6];
    unsigned fin_ring_n;
    stxt_frame fin_bolt_v[16];
    unsigned fin_bolt_v_n;
    stxt_frame valley_skel[13];
    stxt_frame valley_skel_hd[13];
    unsigned valley_skel_hd_factor[13];
    int valley_skel_hd_ready;
    unsigned valley_skel_n;
    stxt_frame glass_skull[16];
    unsigned glass_skull_n;
    stxt_frame vortex_blue[8];
    unsigned vortex_blue_n;
    stxt_frame vortex_red[8];
    unsigned vortex_red_n;
    stxt_frame laff_clown[10];
    stxt_frame laff_clown_hd[10];
    unsigned laff_clown_hd_factor[10];
    int laff_clown_hd_ready;
    unsigned laff_clown_n;
    stxt_frame vil_bridge[2];
    unsigned vil_bridge_n;
    stxt_frame vil_hand[4];
    unsigned vil_hand_n;
    stxt_frame vil_house[12];
    unsigned vil_house_n;
    stxt_frame vil_lilguys[12];
    unsigned vil_lilguys_n;
    stxt_frame shadow[12];
    stxt_frame shadow_lit[12];
    stxt_frame shadow_hd[12];
    unsigned shadow_hd_factor[12];
    int shadow_hd_ready;
    unsigned shadow_n;
    unsigned shadow_lit_n;
    stxt_frame glass_plat[6];
    unsigned glass_plat_n;
    stxt_frame glass_portal[2];
    unsigned glass_portal_n;
    stxt_frame guitar_inst[2];
    unsigned guitar_inst_n;
    stxt_frame guitar_crowd_blue[2];
    unsigned guitar_crowd_blue_n;
    stxt_frame guitar_crowd_red[2];
    unsigned guitar_crowd_red_n;
    stxt_frame drum_frond[2];
    unsigned drum_frond_n;
    stxt_frame drum_fg[2];
    unsigned drum_fg_n;
    stxt_frame drum_pylon[2];
    unsigned drum_pylon_n;
    stxt_frame fin_guitar[2];
    unsigned fin_guitar_n;
    stxt_frame ride_bubbles[8];
    unsigned ride_bubbles_n;
    stxt_frame health[28];
    unsigned health_n;
    stxt_frame bones_hurt[2];
    unsigned bones_hurt_n;
    stxt_frame bones_dead;
    int bones_dead_ok;
    stxt_frame runway_ghost;
    int runway_ghost_ok;
    stxt_frame bat_branch[4];
    unsigned bat_branch_n;
    stxt_frame drum_spark;
    unsigned drum_spark_n;
    stxt_frame ice_floe[4];
    unsigned ice_floe_n;
    stxt_frame ice_crack[6];
    unsigned ice_crack_n;
    stxt_frame ice_hole[2];
    unsigned ice_hole_n;
    stxt_frame staing_arms[3];
    unsigned staing_arms_n;
    stxt_frame staing_spire[4];
    unsigned staing_spire_n;
    stxt_frame staing_orb[4];
    unsigned staing_orb_n;
    int bones_hurt_timer;
    int bones_invuln_timer;
    int bones_dead_flag;
    struct {
        int32_t x, y, vx, vy;
        unsigned ttl, spin;
        int active;
    } bone_shards[24];
} g_stxt;

static unsigned stxt_foot_pad(const stxt_frame *f);

static int batwood_enemy_overlap(const stage_enemy *enemy, unsigned units_per_pixel,
                                 const stage_batwood_contact *contact) {
    const unsigned x = enemy->x / units_per_pixel;
    const stxt_frame *sprite;
    int foot, width, height;
    if (enemy->kind == STAGE_ENEMY_ZOMBIE) {
        if (!contact->floor || x >= contact->floor_width || contact->floor[x] < 0 ||
            !g_stxt.zombies_n)
            return 0;
        foot = contact->floor[x];
        sprite = &g_stxt.zombies[(enemy->anim >> 2) % g_stxt.zombies_n];
        width = (int)sprite->w / 2;
        height = (int)(sprite->h - stxt_foot_pad(sprite));
        /* The melee volume is the torso above the legs. */
        int torso = height / 2;
        BoundingBox bounds = {(int)x - width / 2, foot - height, width, torso};
        return aabb_intersect(&contact->player, &bounds);
    }
    if (!g_stxt.bats_n) return 0;
    foot = (int)(enemy->y / units_per_pixel);
    if (foot < 28) foot = 28;
    if (foot > 216) foot = 216;
    foot += contact->camera_y;
    sprite = &g_stxt.bats[(enemy->anim >> 2) % g_stxt.bats_n];
    if (!sprite->h) return 0;
    /* The rendered wings span beyond the bat's body. */
    width = (int)(sprite->w * 156u / 256u) / 2;
    unsigned scaled_height = (sprite->h * 156u + 128u) / 256u;
    height = (int)(scaled_height - stxt_foot_pad(sprite) * scaled_height / sprite->h);
    BoundingBox bounds = {(int)x - width / 2, foot - height, width, height};
    return aabb_intersect(&contact->player, &bounds);
}

static void clear_bone_shards(void) {
    memset(g_stxt.bone_shards, 0, sizeof g_stxt.bone_shards);
}

static void tick_bone_shards(unsigned u) {
    unsigned i;
    for (i = 0; i < 24u; i++) {
        if (!g_stxt.bone_shards[i].active) continue;
        g_stxt.bone_shards[i].x += g_stxt.bone_shards[i].vx;
        g_stxt.bone_shards[i].y += g_stxt.bone_shards[i].vy;
        g_stxt.bone_shards[i].vy += (int32_t)(u / 3u);
        g_stxt.bone_shards[i].spin++;
        if (g_stxt.bone_shards[i].ttl > 0u) g_stxt.bone_shards[i].ttl--;
        if (g_stxt.bone_shards[i].ttl == 0u) g_stxt.bone_shards[i].active = 0;
    }
}

int stage_enemies_health_sprite_count(void) {
    return (int)g_stxt.health_n;
}

static int extract_stage_palette_ex(const uint8_t *code, size_t code_size,
                                    size_t sub_pal_off, uint8_t *palette) {
    uint32_t words[1024], tmp[1024];
    size_t off, consumed, best = 0, i;
    if (!code || code_size < 2048) return -1;
    memset(words, 0, sizeof words);
    for (off = 0; off + 2048 <= code_size && off < 0x80000; off += 4) {
        if (code[off] != 0 || code[off + 1] != 0 || code[off + 2] >= 4 ||
            code[off + 4] != 1)
            continue;
        consumed = 0;
        memset(tmp, 0, sizeof tmp);
        if (pan_palette_apply(code + off, code_size - off, tmp, 1024, &consumed))
            continue;
        if (consumed >= 4096 && consumed <= 4120) {
            best = consumed;
            memcpy(words, tmp, sizeof words);
            break;
        }
        if (consumed >= 2048 && consumed > best) {
            best = consumed;
            memcpy(words, tmp, sizeof words);
        }
    }
    if (best < 512) return -1;
    if (sub_pal_off > 0 && sub_pal_off + 8 <= code_size) {
        consumed = 0;
        (void)pan_palette_apply(code + sub_pal_off, code_size - sub_pal_off,
                                words, 1024, &consumed);
    }
    for (i = 0; i < 1024; i++) {
        palette[4 * i + 0] = 0;
        palette[4 * i + 1] = (uint8_t)words[i];
        palette[4 * i + 2] = (uint8_t)(words[i] >> 8);
        palette[4 * i + 3] = (uint8_t)(words[i] >> 16);
    }
    return 0;
}

static int extract_stage_palette(const uint8_t *code, size_t code_size,
                                 uint8_t *palette) {
    return extract_stage_palette_ex(code, code_size, 0, palette);
}

static int decode_stxt_record(const stxtr_record *rec, const uint8_t *bank_pal,
                              stxt_frame *out) {
    size_t sz;
    uint8_t *px;
    if (!rec || !bank_pal || !out) return -1;
    sz = (size_t)rec->width * rec->height * 4u;
    px = malloc(sz);
    if (!px) return -1;
    if (rec->size == (size_t)rec->width * rec->height) {
        /* Direct uncompressed 8bpp Saturn sprite */
        for (size_t j = 0; j < (size_t)rec->width * rec->height; j++) {
            uint8_t idx = rec->data[j];
            if (idx == 0) {
                px[4 * j + 0] = px[4 * j + 1] = px[4 * j + 2] = px[4 * j + 3] = 0;
            } else {
                px[4 * j + 0] = bank_pal[4 * idx + 1];
                px[4 * j + 1] = bank_pal[4 * idx + 2];
                px[4 * j + 2] = bank_pal[4 * idx + 3];
                px[4 * j + 3] = 255;
            }
        }
    } else if (mb_sprite_decode(rec->data, rec->size, rec->width, rec->height,
                                0x04a8, bank_pal, 1024, px, sz)) {
        free(px);
        return -1;
    }
    out->rgba = px;
    out->w = rec->width;
    out->h = rec->height;
    return 0;
}

static int decode_stxt_at(const void *stxt, size_t stxt_size, size_t offset,
                          const uint8_t *bank_pal, stxt_frame *out) {
    stxtr_record rec;
    size_t off = offset;
    if (!stxt || offset >= stxt_size || !bank_pal || !out) return -1;
    if (stxtr_next(stxt, stxt_size, &off, &rec) != 1) return -1;
    return decode_stxt_record(&rec, bank_pal, out);
}

/* Decode 0x557a0..0x55f20 spinning bone masks into clean shaded 1x ivory.
 * Saturn VDP1 rec 310..315 store 0=transparent and non-zero bone silhouette
 * bytes in [5..255] that do not map linearly into static b3 palette RAM. */
static int decode_thrown_bone_stxt_at(const void *stxt, size_t stxt_size,
                                      size_t offset, stxt_frame *out) {
    stxtr_record rec;
    size_t off = offset, sz;
    uint8_t *px;
    unsigned x, y;
    if (!stxt || offset >= stxt_size || !out) return -1;
    if (stxtr_next(stxt, stxt_size, &off, &rec) != 1) return -1;
    if (rec.width == 0 || rec.height == 0 ||
        rec.size != (size_t)rec.width * rec.height)
        return -1;
    sz = (size_t)rec.width * rec.height * 4u;
    px = (uint8_t *)malloc(sz);
    if (!px) return -1;
    for (y = 0; y < rec.height; y++) {
        for (x = 0; x < rec.width; x++) {
            size_t j = (size_t)y * rec.width + x;
            uint8_t v = rec.data[j];
            if (v == 0) {
                px[4 * j + 0] = px[4 * j + 1] = px[4 * j + 2] = px[4 * j + 3] = 0;
            } else {
                int edge = (x == 0 || y == 0 || x + 1u >= rec.width ||
                            y + 1u >= rec.height ||
                            rec.data[j - 1] == 0 || rec.data[j + 1] == 0 ||
                            rec.data[j - rec.width] == 0 ||
                            rec.data[j + rec.width] == 0);
                int hi = (x + y * 2u < rec.width + rec.height);
                int tex = (int)((v & 31u) * 24u / 31u) - 12;
                int lum = (edge ? 132 : (hi ? 236 : 196)) + tex;
                if (lum < 96) lum = 96;
                if (lum > 248) lum = 248;
                px[4 * j + 0] = (uint8_t)lum;
                px[4 * j + 1] = (uint8_t)((lum * 246) / 255);
                px[4 * j + 2] = (uint8_t)((lum * 224) / 255);
                px[4 * j + 3] = 255;
            }
        }
    }
    out->rgba = px;
    out->w = rec.width;
    out->h = rec.height;
    return 0;
}

/* Build crisp spectral cyan-steel palette from clean b2 monochrome luminance
 * so gang enemies lack b3's brown [160..190] edge artifacts and contrast
 * clearly against warm-ivory Mr. Bones even when overlapping. */
static void build_gang_spectral_pal(const uint8_t *b2, uint8_t *dst_pal) {
    unsigned i;
    memcpy(dst_pal, b2, 1024u);
    for (i = 1; i < 256u; i++) {
        int r = b2[4u * i + 1u];
        int g = b2[4u * i + 2u];
        int b = b2[4u * i + 3u];
        int lum = (r * 30 + g * 59 + b * 11) / 100;
        /* Stretch contrast gently around bone highlights vs joint creases. */
        int adj = (lum - 40) * 255 / 145;
        int cr, cg, cb;
        if (adj < 20) adj = 20;
        if (adj > 226) adj = 226;
        cr = (adj * 178) / 255;
        cg = (adj * 204) / 255;
        cb = (adj * 228) / 255;
        if (cr > 255) cr = 255;
        if (cg > 255) cg = 255;
        if (cb > 255) cb = 255;
        dst_pal[4u * i + 1u] = (uint8_t)cr;
        dst_pal[4u * i + 2u] = (uint8_t)cg;
        dst_pal[4u * i + 3u] = (uint8_t)cb;
    }
}

typedef struct {
    const size_t *offs;
    unsigned count;
    unsigned bank;
    stxt_frame *dst;
    unsigned *dst_n;
    unsigned max_dst;
} stxt_bank_batch;

static int load_stage_stxt_batches(cdimage *cd, const char *code_path,
                                   const char *stxt_path, size_t min_stxt_sz,
                                   size_t sub_pal_off,
                                   const stxt_bank_batch *batches,
                                   unsigned batch_n) {
    void *code = NULL, *stxt = NULL;
    size_t code_sz = 0, stxt_sz = 0;
    uint8_t pal[4096];
    unsigned bi, i;
    int ok = -1;

    if (resource_load(cd, code_path, &code, &code_sz) == 0 &&
        code_sz > 20000 &&
        resource_load(cd, stxt_path, &stxt, &stxt_sz) == 0 &&
        stxt_sz > min_stxt_sz &&
        extract_stage_palette_ex((const uint8_t *)code, code_sz, sub_pal_off,
                                 pal) == 0) {
        for (bi = 0; bi < batch_n; bi++) {
            const stxt_bank_batch *b = &batches[bi];
            const uint8_t *bptr = pal + (b->bank & 3u) * 1024u;
            if (*b->dst_n != 0) continue;
            for (i = 0; i < b->count && *b->dst_n < b->max_dst; i++) {
                if (decode_stxt_at(stxt, stxt_sz, b->offs[i], bptr,
                                   &b->dst[*b->dst_n]) == 0)
                    (*b->dst_n)++;
            }
        }
        ok = 0;
    }
    free(code);
    free(stxt);
    return ok;
}

static int load_stage_stxt_span(cdimage *cd, const char *code_path,
                                const char *stxt_path, size_t sub_pal_off,
                                size_t span_start, size_t span_end,
                                unsigned bank, stxt_frame *dst,
                                unsigned *dst_n, unsigned max_dst) {
    void *code = NULL, *stxt = NULL;
    size_t code_sz = 0, stxt_sz = 0, off;
    uint8_t pal[4096];
    const uint8_t *bptr;
    int ok = -1;
    if (!cd || !dst || !dst_n || *dst_n > 0) return -1;
    if (resource_load(cd, code_path, &code, &code_sz) == 0 &&
        code_sz > 20000 &&
        resource_load(cd, stxt_path, &stxt, &stxt_sz) == 0 &&
        span_start < span_end && span_end <= stxt_sz &&
        extract_stage_palette_ex((const uint8_t *)code, code_sz, sub_pal_off,
                                 pal) == 0) {
        bptr = pal + (bank & 3u) * 1024u;
        off = span_start;
        while (off < span_end && *dst_n < max_dst) {
            stxtr_record rec;
            if (stxtr_next(stxt, span_end, &off, &rec) != 1) break;
            if (decode_stxt_record(&rec, bptr, &dst[*dst_n]) == 0)
                (*dst_n)++;
        }
        if (*dst_n > 0) ok = 0;
    }
    free(code);
    free(stxt);
    return ok;
}

void stage_enemies_load_disc_sprites(cdimage *cd) {
    void *code = NULL, *stxt = NULL;
    size_t code_sz = 0, stxt_sz = 0;
    uint8_t pal[4096];
    unsigned i;
    if (!cd) return;

    if (!g_stxt.loaded_d1) {
        if (resource_load(cd, "/04RUNWAY/03CODE", &code, &code_sz) == 0 &&
            code_sz > 20000 &&
            resource_load(cd, "/04RUNWAY/04STXT", &stxt, &stxt_sz) == 0 &&
            stxt_sz > 0x54000 &&
            extract_stage_palette((const uint8_t *)code, code_sz, pal) == 0) {
            static const size_t k_stand_off[2] = {0x22e20, 0x236d0};
            static const size_t k_tall_off[2] = {0x41180, 0x41690};
            static const size_t k_air_off[4] = {0x38f80, 0x3a170, 0x3b540, 0x3f670};
            static const size_t k_hurt_off[2] = {0x41b40, 0x42230};
            static const size_t k_walk_off[20] = {
                0x1ee40, 0x1f830, 0x202c0, 0x20dc0, 0x218c0,
                0x22390, 0x22e20, 0x236d0, 0x23f50, 0x24900,
                0x252f0, 0x25c70, 0x26620, 0x271f0, 0x27e40,
                0x28a90, 0x29520, 0x29f40, 0x2a7c0, 0x2b250
            };
            static const size_t k_gang_off[8] = {
                0x456d0, 0x45ec0, 0x468e0, 0x473e0,
                0x47f50, 0x48870, 0x4b1c0, 0x4bb90
            };
            static const size_t k_thrown_off[6] = {
                0x557a0, 0x559a0, 0x55a70, 0x55c50, 0x55e50, 0x55f20
            };
            uint8_t gang_pal[1024];
            static const size_t k_hud_fill_off[14] = {
                0x06770, 0x068c0, 0x06990, 0x06a60, 0x06b20, 0x06bd0, 0x06cd0,
                0x06d70, 0x06e30, 0x06e80, 0x06ec0, 0x06f10, 0x06fa0, 0x07010
            };
            static const size_t k_hud_empty_off[14] = {
                0x070a0, 0x070e0, 0x07280, 0x07370, 0x07450, 0x07510, 0x07630,
                0x076e0, 0x077b0, 0x07820, 0x07880, 0x078e0, 0x079b0, 0x07a30
            };
            static const size_t k_poof_off[4] = {0x04eb0, 0x05070, 0x05250, 0x05430};
            const uint8_t *b0 = pal;
            const uint8_t *b2 = pal + 2 * 1024;
            const uint8_t *b3 = pal + 3 * 1024;

            for (i = 0; i < 2; i++)
                if (decode_stxt_at(stxt, stxt_sz, k_stand_off[i], b2,
                                   &g_stxt.bones_stand[g_stxt.bones_stand_n]) == 0)
                    g_stxt.bones_stand_n++;
            for (i = 0; i < 2; i++)
                if (decode_stxt_at(stxt, stxt_sz, k_tall_off[i], b2,
                                   &g_stxt.bones_tall[g_stxt.bones_tall_n]) == 0)
                    g_stxt.bones_tall_n++;
            for (i = 0; i < 4; i++)
                if (decode_stxt_at(stxt, stxt_sz, k_air_off[i], b2,
                                   &g_stxt.bones_air[g_stxt.bones_air_n]) == 0)
                    g_stxt.bones_air_n++;
            for (i = 0; i < 2; i++)
                if (decode_stxt_at(stxt, stxt_sz, k_hurt_off[i], b2,
                                   &g_stxt.bones_hurt[g_stxt.bones_hurt_n]) == 0)
                    g_stxt.bones_hurt_n++;
            if (!g_stxt.bones_dead_ok &&
                decode_stxt_at(stxt, stxt_sz, 0x42bf0u, b2, &g_stxt.bones_dead) == 0)
                g_stxt.bones_dead_ok = 1;
            for (i = 0; i < 20; i++)
                if (decode_stxt_at(stxt, stxt_sz, k_walk_off[i], b2,
                                   &g_stxt.bones_walk[g_stxt.bones_walk_n]) == 0)
                    g_stxt.bones_walk_n++;
            build_gang_spectral_pal(b2, gang_pal);
            if (!g_stxt.runway_ghost_ok &&
                decode_stxt_at(stxt, stxt_sz, 0x05e20u, gang_pal, &g_stxt.runway_ghost) == 0)
                g_stxt.runway_ghost_ok = 1;
            for (i = 0; i < 8; i++)
                if (decode_stxt_at(stxt, stxt_sz, k_gang_off[i], gang_pal,
                                   &g_stxt.gang[g_stxt.gang_n]) == 0)
                    g_stxt.gang_n++;
            for (i = 0; i < 6; i++)
                if (decode_thrown_bone_stxt_at(stxt, stxt_sz, k_thrown_off[i],
                                               &g_stxt.thrown[g_stxt.thrown_n]) == 0)
                    g_stxt.thrown_n++;
            for (i = 0; i < 14; i++) {
                if (decode_stxt_at(stxt, stxt_sz, k_hud_fill_off[i], b0,
                                   &g_stxt.hud_fill[g_stxt.hud_fill_n]) == 0)
                    g_stxt.hud_fill_n++;
                if (decode_stxt_at(stxt, stxt_sz, k_hud_empty_off[i], b0,
                                   &g_stxt.hud_empty[g_stxt.hud_empty_n]) == 0)
                    g_stxt.hud_empty_n++;
            }
            for (i = 0; i < 4; i++) {
                if (decode_stxt_at(stxt, stxt_sz, k_poof_off[i], b3,
                                   &g_stxt.poof_red[g_stxt.poof_red_n]) == 0)
                    g_stxt.poof_red_n++;
                if (decode_stxt_at(stxt, stxt_sz, k_poof_off[i], b2,
                                   &g_stxt.poof_blue[g_stxt.poof_blue_n]) == 0)
                    g_stxt.poof_blue_n++;
            }
            g_stxt.loaded_d1 = 1;
        }
        free(code);
        free(stxt);
        code = stxt = NULL;

        if (g_stxt.loaded_d1) {
            static const size_t k_lord_off[8] = {
                0x2c240, 0x2cbd0, 0x2f1f0, 0x30b00,
                0x317a0, 0x321e0, 0x34a70, 0x355a0
            };
            static const size_t k_bat_off[12] = {
                0x00000, 0x00860, 0x01180, 0x01e30, 0x029c0, 0x03600,
                0x03d90, 0x04920, 0x05630, 0x06050, 0x073e0, 0x080f0
            };
            static const size_t k_zombie_off[12] = {
                0x0ece0, 0x10110, 0x11510, 0x128a0, 0x13fb0, 0x15a30,
                0x17420, 0x193d0, 0x1b0f0, 0x1cbc0, 0x1e600, 0x202d0
            };
            static const size_t k_bigbns_bumper_off[16] = {
                0x041d0, 0x04540, 0x048b0, 0x04c00, 0x04e80, 0x051d0, 0x05520, 0x05890,
                0x05b30, 0x05ea0, 0x06210, 0x06560, 0x067e0, 0x06b30, 0x06ea0, 0x07210
            };
            static const size_t k_bigbns_redpoof_off[9] = {
                0x232b0, 0x23550, 0x23c70, 0x245e0, 0x24f80,
                0x256a0, 0x25d70, 0x26260, 0x26810
            };
            static const size_t k_logs_bluepoof_off[13] = {
                0x040b0, 0x04260, 0x04570, 0x04a30, 0x05180, 0x05940, 0x06190,
                0x06ad0, 0x073e0, 0x07c40, 0x081c0, 0x08680, 0x088b0
            };
            static const size_t k_logs_buttrfly_off[10] = {
                0x08a80, 0x08ca0, 0x08f90, 0x091d0, 0x09410,
                0x09590, 0x097d0, 0x09ac0, 0x09d00, 0x09f20
            };
            static const size_t k_bat_branch_off[4] = {
                0x51d60u, 0x53410u, 0x54ce0u, 0x55d80u
            };
            const stxt_bank_batch mauso_b[1] = {
                {k_lord_off, 8u, 1u, g_stxt.lord, &g_stxt.lord_n, 8u},
            };
            const stxt_bank_batch bat1_b[3] = {
                {k_bat_off, 12u, 1u, g_stxt.bats, &g_stxt.bats_n, 12u},
                {k_zombie_off, 12u, 1u, g_stxt.zombies, &g_stxt.zombies_n, 12u},
                {k_bat_branch_off, 4u, 2u, g_stxt.bat_branch, &g_stxt.bat_branch_n, 4u},
            };
            const stxt_bank_batch bigbns_b[2] = {
                {k_bigbns_bumper_off, 16u, 2u, g_stxt.bigbns_bumper, &g_stxt.bigbns_bumper_n, 16u},
                {k_bigbns_redpoof_off, 9u, 2u, g_stxt.bigbns_redpoof, &g_stxt.bigbns_redpoof_n, 9u},
            };
            const stxt_bank_batch logs_b[3] = {
                {k_logs_bluepoof_off, 13u, 3u, g_stxt.logs_bluepoof, &g_stxt.logs_bluepoof_n, 13u},
                {k_logs_buttrfly_off, 10u, 3u, g_stxt.logs_buttrfly_blue, &g_stxt.logs_buttrfly_blue_n, 10u},
                {k_logs_buttrfly_off, 10u, 1u, g_stxt.logs_buttrfly_red, &g_stxt.logs_buttrfly_red_n, 10u},
            };
            (void)load_stage_stxt_batches(cd, "/05MAUSO/03CODE", "/05MAUSO/04STXT", 0x35600u, 0u, mauso_b, 1u);
            (void)load_stage_stxt_batches(cd, "/08BAT1/03CODE", "/08BAT1/04STXT", 0x56000u, 0u, bat1_b, 3u);
            (void)load_stage_stxt_batches(cd, "/16BIGBNS/03CODE", "/16BIGBNS/04STXT", 0x26800u, 0u, bigbns_b, 2u);
            (void)load_stage_stxt_batches(cd, "/11LOGS/03CODE", "/11LOGS/04STXT", 0x0a000u, 0u, logs_b, 3u);
            if (g_stxt.health_n == 0) {
                (void)load_stage_stxt_span(cd, "/11LOGS/03CODE", "/11LOGS/04STXT",
                                           0u, 0x00000u, 0x01500u, 3u,
                                           g_stxt.health, &g_stxt.health_n, 28u);
            }
            {
                static const size_t valley_offs[13] = {
                    0x3bfa0u, 0x3c790u, 0x3ce30u, 0x3d4d0u, 0x3dc60u,
                    0x3e490u, 0x3ec50u, 0x3f9e0u, 0x40580u, 0x40e90u,
                    0x417a0u, 0x420b0u, 0x42900u
                };
                static const size_t glass_offs[16] = {
                    0x06290u, 0x06a20u, 0x07210u, 0x07a00u, 0x08220u,
                    0x08a10u, 0x09230u, 0x09a50u, 0x0a240u, 0x0aac0u,
                    0x0b340u, 0x0bbc0u, 0x0c410u, 0x0cc30u, 0x0d480u,
                    0x0dcd0u
                };
                static const size_t glass_plat_offs[6] = {
                    0x05200u, 0x059b0u, 0x3cc50u, 0x44c40u, 0x41550u, 0x04c00u
                };
                static const size_t glass_portal_offs[1] = { 0x105a0u };
                static const size_t guitar_inst_offs[2] = { 0x041d0u, 0x04de0u };
                static const size_t guitar_crowd_offs[2] = { 0x1cf10u, 0x24ae0u };
                static const size_t drum_frond_offs[1] = { 0x00000u };
                static const size_t drum_fg_offs[1] = { 0x056d0u };
                static const size_t drum_pylon_offs[2] = { 0x08570u, 0x0ad30u };
                static const size_t drum_spark_offs[1] = { 0x59690u };
                static const size_t vortex_offs[8] = {
                    0x1b740u, 0x1be60u, 0x1c7d0u, 0x1d170u, 0x1d890u,
                    0x1df60u, 0x1e450u, 0x1ea00u
                };
                const stxt_bank_batch valley_b[1] = {
                    { valley_offs, 13u, 1u, g_stxt.valley_skel, &g_stxt.valley_skel_n, 13u }
                };
                const stxt_bank_batch glass_b[3] = {
                    { glass_offs, 16u, 1u, g_stxt.glass_skull, &g_stxt.glass_skull_n, 16u },
                    { glass_plat_offs, 6u, 1u, g_stxt.glass_plat, &g_stxt.glass_plat_n, 6u },
                    { glass_portal_offs, 1u, 1u, g_stxt.glass_portal, &g_stxt.glass_portal_n, 2u }
                };
                const stxt_bank_batch guitar_b[3] = {
                    { guitar_inst_offs, 2u, 0u, g_stxt.guitar_inst, &g_stxt.guitar_inst_n, 2u },
                    { guitar_crowd_offs, 2u, 2u, g_stxt.guitar_crowd_blue, &g_stxt.guitar_crowd_blue_n, 2u },
                    { guitar_crowd_offs, 2u, 3u, g_stxt.guitar_crowd_red, &g_stxt.guitar_crowd_red_n, 2u }
                };
                const stxt_bank_batch drum_b[4] = {
                    { drum_frond_offs, 1u, 1u, g_stxt.drum_frond, &g_stxt.drum_frond_n, 2u },
                    { drum_fg_offs, 1u, 2u, g_stxt.drum_fg, &g_stxt.drum_fg_n, 2u },
                    { drum_pylon_offs, 2u, 3u, g_stxt.drum_pylon, &g_stxt.drum_pylon_n, 2u },
                    { drum_spark_offs, 1u, 3u, &g_stxt.drum_spark, &g_stxt.drum_spark_n, 1u }
                };
                const stxt_bank_batch vortex_b[2] = {
                    { vortex_offs, 8u, 3u, g_stxt.vortex_blue, &g_stxt.vortex_blue_n, 8u },
                    { vortex_offs, 8u, 2u, g_stxt.vortex_red, &g_stxt.vortex_red_n, 8u }
                };
                (void)load_stage_stxt_batches(cd, "/06VALLEY/03CODE", "/06VALLEY/04STXT", 0x42000u, 0u, valley_b, 1u);
                (void)load_stage_stxt_batches(cd, "/15GLASS/03CODE", "/15GLASS/04STXT", 0x0e000u, 0x65f18u, glass_b, 3u);
                (void)load_stage_stxt_batches(cd, "/07GUITAR/03CODE", "/07GUITAR/04STXT", 0x24000u, 0u, guitar_b, 3u);
                (void)load_stage_stxt_batches(cd, "/14DRUM/03CODE", "/14DRUM/04STXT", 0x59800u, 0u, drum_b, 4u);
                (void)load_stage_stxt_batches(cd, "/17VORTEX/03CODE", "/17VORTEX/04STXT", 0x1e000u, 0u, vortex_b, 2u);
            }
        }
    }

    if (!g_stxt.loaded_d2) {
        static const size_t k_gen_off[4] = {0x03310, 0x038a0, 0x03ef0, 0x04770};
        static const size_t k_gen_zap_off[4] = {0x02540, 0x02600, 0x026e0, 0x02860};
        static const size_t k_vil_mtrspid_off[13] = {
            0x31280, 0x31560, 0x31800, 0x31b90, 0x31f60, 0x32200, 0x32550,
            0x32860, 0x32b10, 0x32cf0, 0x33000, 0x33350, 0x33630
        };
        static const size_t k_vil_spider_off[16] = {
            0x392d0, 0x39520, 0x39790, 0x399e0, 0x39c50, 0x39ed0, 0x3a070, 0x3a210,
            0x3a480, 0x3a6f0, 0x3a930, 0x3ab80, 0x3ae00, 0x3afb0, 0x3b160, 0x3b3e0
        };
        static const size_t k_moat_wingnut_off[16] = {
            0x1f000, 0x1f310, 0x1f5c0, 0x1f780, 0x1f930, 0x1fae0, 0x1fd20, 0x20020,
            0x20350, 0x20660, 0x20910, 0x20ad0, 0x20c80, 0x20e30, 0x21070, 0x21370
        };
        static const size_t k_moat_redpoof_off[9] = {
            0x1b4a0, 0x1b740, 0x1be60, 0x1c7d0, 0x1d170,
            0x1d890, 0x1df60, 0x1e450, 0x1ea00
        };
        static const size_t k_fin_ring_off[6] = {
            0x2b6b0, 0x2bba0, 0x2c060, 0x2c500, 0x2ca10, 0x2cf70
        };
        static const size_t k_fin_bolt_v_off[16] = {
            0x2d4d0, 0x2d700, 0x2d8f0, 0x2dc30, 0x2de60, 0x2e2b0, 0x2e5f0, 0x2e820,
            0x2ea50, 0x2ec70, 0x2ee20, 0x2efc0, 0x2f120, 0x2f220, 0x2f300, 0x2f420
        };
        const stxt_bank_batch gen_b[3] = {
            {k_gen_off, 4u, 2u, g_stxt.gen_red, &g_stxt.gen_red_n, 4u},
            {k_gen_off, 4u, 1u, g_stxt.gen_blue, &g_stxt.gen_blue_n, 4u},
            {k_gen_zap_off, 4u, 1u, g_stxt.gen_zap, &g_stxt.gen_zap_n, 4u},
        };
        const stxt_bank_batch vil_b[2] = {
            {k_vil_mtrspid_off, 13u, 1u, g_stxt.vil_mtrspid, &g_stxt.vil_mtrspid_n, 13u},
            {k_vil_spider_off, 16u, 1u, g_stxt.vil_spider, &g_stxt.vil_spider_n, 16u},
        };
        const stxt_bank_batch moat_b[2] = {
            {k_moat_wingnut_off, 16u, 2u, g_stxt.moat_wingnut, &g_stxt.moat_wingnut_n, 16u},
            {k_moat_redpoof_off, 9u, 0u, g_stxt.moat_redpoof, &g_stxt.moat_redpoof_n, 9u},
        };
        const stxt_bank_batch fin_b[2] = {
            {k_fin_ring_off, 6u, 1u, g_stxt.fin_ring, &g_stxt.fin_ring_n, 6u},
            {k_fin_bolt_v_off, 16u, 1u, g_stxt.fin_bolt_v, &g_stxt.fin_bolt_v_n, 16u},
        };
        if (load_stage_stxt_batches(cd, "/25GENRM/03CODE", "/25GENRM/04STXT", 0x05b00u, 0u, gen_b, 3u) == 0) {
            g_stxt.loaded_d2 = 1;
            (void)load_stage_stxt_batches(cd, "/19VILLAGE/03CODE", "/19VILLAGE/04STXT", 0x3b400u, 0u, vil_b, 2u);
            (void)load_stage_stxt_span(cd, "/19VILLAGE/03CODE", "/19VILLAGE/04STXT",
                                       0u, 0x041d0u, 0x05660u, 1u,
                                       g_stxt.vil_bridge, &g_stxt.vil_bridge_n, 2u);
            (void)load_stage_stxt_span(cd, "/19VILLAGE/03CODE", "/19VILLAGE/04STXT",
                                       0u, 0x0f960u, 0x103f0u, 0u,
                                       g_stxt.vil_hand, &g_stxt.vil_hand_n, 4u);
            (void)load_stage_stxt_span(cd, "/19VILLAGE/03CODE", "/19VILLAGE/04STXT",
                                       0u, 0x11780u, 0x17890u, 1u,
                                       g_stxt.vil_house, &g_stxt.vil_house_n, 12u);
            (void)load_stage_stxt_span(cd, "/19VILLAGE/03CODE", "/19VILLAGE/04STXT",
                                       0u, 0x17af0u, 0x186b0u, 1u,
                                       g_stxt.vil_lilguys, &g_stxt.vil_lilguys_n, 12u);
            (void)load_stage_stxt_batches(cd, "/23MOAT/03CODE", "/23MOAT/04STXT", 0x21400u, 0x6f9a4u, moat_b, 2u);
            if (g_stxt.health_n == 0) {
                (void)load_stage_stxt_span(cd, "/23MOAT/03CODE", "/23MOAT/04STXT",
                                           0x6f9a4u, 0x00000u, 0x01500u, 3u,
                                           g_stxt.health, &g_stxt.health_n, 28u);
            }
            {
                static const size_t fin_guitar_offs[2] = { 0x058a0u, 0x064b0u };
                const stxt_bank_batch fin_g[1] = {
                    { fin_guitar_offs, 2u, 0u, g_stxt.fin_guitar, &g_stxt.fin_guitar_n, 2u }
                };
                (void)load_stage_stxt_batches(cd, "/27FINALE/03CODE", "/27FINALE/04STXT", 0x2f500u, 0u, fin_b, 2u);
                (void)load_stage_stxt_batches(cd, "/27FINALE/03CODE", "/27FINALE/04STXT", 0x06800u, 0u, fin_g, 1u);
            }
            if (g_stxt.ride_bubbles_n == 0) {
                (void)load_stage_stxt_span(cd, "/18RIDE/03CODE", "/18RIDE/04STXT",
                                           0u, 0x0b500u, 0x0f800u, 3u,
                                           g_stxt.ride_bubbles, &g_stxt.ride_bubbles_n, 8u);
            }
            {
                static const size_t laff_offs[10] = {
                    0x0e440u, 0x11520u, 0x14130u, 0x16b90u, 0x196a0u,
                    0x1bf00u, 0x1e360u, 0x21230u, 0x242c0u, 0x270e0u
                };
                const stxt_bank_batch laff_b[1] = {
                    { laff_offs, 10u, 3u, g_stxt.laff_clown, &g_stxt.laff_clown_n, 10u }
                };
                (void)load_stage_stxt_batches(cd, "/26LAFF/03CODE", "/26LAFF/04STXT", 0x27000u, 0u, laff_b, 1u);
            }
            /*
             * 22SHADOW authentic giant wall-shadow monster frames [200..223] in
             * /22SHADOW/04STXT (96x105..184x64): bank 2 dark shadow stalker,
             * bank 1 glowing crimson flash when stunned by Blues.
             */
            if (g_stxt.shadow_n == 0) {
                static const size_t shadow_offs[9] = {
                    0x1d960u, 0x200d0u, 0x22990u, 0x257a0u, 0x28f50u,
                    0x2fae0u, 0x32340u, 0x357d0u, 0x436f0u
                };
                const stxt_bank_batch shadow_b[2] = {
                    { shadow_offs, 9u, 2u, g_stxt.shadow, &g_stxt.shadow_n, 12u },
                    { shadow_offs, 9u, 1u, g_stxt.shadow_lit, &g_stxt.shadow_lit_n, 12u }
                };
                (void)load_stage_stxt_batches(cd, "/22SHADOW/03CODE", "/22SHADOW/04STXT",
                                              0x44000u, 0u, shadow_b, 2u);
            }
            {
                static const size_t ice_floe_offs[4] = {
                    0x06130u, 0x081f0u, 0x0a2b0u, 0x0c370u
                };
                static const size_t ice_crack_offs[6] = {
                    0x16ef0u, 0x18710u, 0x19b30u, 0x1af50u, 0x1ceb0u, 0x1ee10u
                };
                static const size_t ice_hole_offs[2] = {
                    0x23c80u, 0x273c0u
                };
                const stxt_bank_batch ice_b[3] = {
                    { ice_floe_offs, 4u, 2u, g_stxt.ice_floe, &g_stxt.ice_floe_n, 4u },
                    { ice_crack_offs, 6u, 2u, g_stxt.ice_crack, &g_stxt.ice_crack_n, 6u },
                    { ice_hole_offs, 2u, 2u, g_stxt.ice_hole, &g_stxt.ice_hole_n, 2u }
                };
                static const size_t staing_orb_offs[4] = {
                    0x040b0u, 0x044c0u, 0x048d0u, 0x04ce0u
                };
                static const size_t staing_spire_offs[4] = {
                    0x20740u, 0x21770u, 0x237b0u, 0x247e0u
                };
                static const size_t staing_arms_offs[3] = {
                    0x2a900u, 0x3bd20u, 0x44730u
                };
                const stxt_bank_batch staing_b[3] = {
                    { staing_orb_offs, 4u, 3u, g_stxt.staing_orb, &g_stxt.staing_orb_n, 4u },
                    { staing_spire_offs, 4u, 2u, g_stxt.staing_spire, &g_stxt.staing_spire_n, 4u },
                    { staing_arms_offs, 3u, 2u, g_stxt.staing_arms, &g_stxt.staing_arms_n, 3u }
                };
                (void)load_stage_stxt_batches(cd, "/20ICE1/03CODE", "/20ICE1/04STXT",
                                              0x28000u, 0u, ice_b, 3u);
                (void)load_stage_stxt_batches(cd, "/24STAING/03CODE", "/24STAING/04STXT",
                                              0x45000u, 0u, staing_b, 3u);
            }
        }
    }
}

static void put_px(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                   int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t *p;
    if (x < 0 || y < 0 || (unsigned)x >= dst_w || (unsigned)y >= dst_h) return;
    p = &dst[((unsigned)y * dst_w + (unsigned)x) * 4u];
    p[0] = r;
    p[1] = g;
    p[2] = b;
    p[3] = 255u;
}

static void blend_px(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                     int x, int y, uint8_t r, uint8_t g, uint8_t b, int a) {
    uint8_t *p;
    if (a <= 0 || x < 0 || y < 0 || (unsigned)x >= dst_w || (unsigned)y >= dst_h)
        return;
    if (a > 255) a = 255;
    p = &dst[((unsigned)y * dst_w + (unsigned)x) * 4u];
    p[0] = (uint8_t)((p[0] * (255 - a) + r * a) / 255);
    p[1] = (uint8_t)((p[1] * (255 - a) + g * a) / 255);
    p[2] = (uint8_t)((p[2] * (255 - a) + b * a) / 255);
    p[3] = 255u;
}

static void draw_electric_seg(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                              int x0, int y0, int x1, int y1, unsigned seed) {
    int dx = x1 - x0, dy = y1 - y0;
    int steps = (dx < 0 ? -dx : dx);
    int ady = (dy < 0 ? -dy : dy);
    int s, prev_x = x0, prev_y = y0;
    if (ady > steps) steps = ady;
    if (steps < 4) steps = 4;
    if (steps > 64) steps = 64;
    for (s = 0; s <= steps; s++) {
        int j = ((s > 1 && s < steps - 1)
                     ? ((int)(((seed + (unsigned)s * 17u) * 2654435761u) >> 29) - 3)
                     : 0);
        int cx = x0 + (dx * s) / steps;
        int cy = y0 + (dy * s) / steps + j;
        int ox, oy;
        for (oy = -2; oy <= 2; oy++) {
            for (ox = -2; ox <= 2; ox++) {
                int d2 = ox * ox + oy * oy;
                if (d2 == 0)
                    blend_px(dst, dst_w, dst_h, cx, cy, 225u, 248u, 255u, 245);
                else if (d2 <= 1)
                    blend_px(dst, dst_w, dst_h, cx + ox, cy + oy, 95u, 225u, 255u, 180);
                else if (d2 <= 4)
                    blend_px(dst, dst_w, dst_h, cx + ox, cy + oy, 45u, 150u, 255u, 80);
            }
        }
        if ((s & 7) == 3 && s < steps - 2) {
            int fx = cx + ((seed & 1u) ? 4 : -4);
            int fy = cy + (((s + (int)seed) & 1) ? 5 : -5);
            blend_px(dst, dst_w, dst_h, (cx + fx) / 2, (cy + fy) / 2, 160u, 238u, 255u, 195);
            blend_px(dst, dst_w, dst_h, fx, fy, 90u, 215u, 255u, 150);
        }
        prev_x = cx;
        prev_y = cy;
    }
    (void)prev_x;
    (void)prev_y;
}

static void draw_rect(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                      int x0, int y0, int w, int h,
                      uint8_t r, uint8_t g, uint8_t b) {
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            put_px(dst, dst_w, dst_h, x0 + x, y0 + y, r, g, b);
}

static unsigned stxt_foot_pad(const stxt_frame *f) {
    unsigned y, x;
    if (!f || !f->rgba || !f->w || !f->h) return 0u;
    for (y = f->h; y > 0u; y--) {
        const uint8_t *row = f->rgba + (size_t)(y - 1u) * f->w * 4u;
        for (x = 0u; x < f->w; x++) {
            if (row[x * 4u + 3u] > 16u)
                return f->h - y;
        }
    }
    return 0u;
}

static void blit_stxt_anchored(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                               const stxt_frame *f, int ax, int ay,
                               int center_y, int flip_x, unsigned scale_num,
                               unsigned scale_den) {
    unsigned pad_b;
    if (!f || !f->rgba || !f->w || !f->h) return;
    pad_b = center_y ? 0u : stxt_foot_pad(f);
    if (scale_den == 0 || scale_num == scale_den) {
        int dy = center_y ? (int)(f->h / 2u) : (int)(f->h - pad_b);
        runway_compose_blit_sprite_ex(dst, dst_w, dst_h, f->rgba, f->w, f->h,
                                      ax - (int)(f->w / 2u), ay - dy, flip_x);
    } else {
        unsigned sw = (f->w * scale_num + scale_den / 2u) / scale_den;
        unsigned sh = (f->h * scale_num + scale_den / 2u) / scale_den;
        int dy;
        if (sw < 2u) sw = 2u;
        if (sh < 2u) sh = 2u;
        dy = center_y ? (int)(sh / 2u)
                      : (int)sh - (int)((pad_b * sh) / f->h);
        runway_compose_blit_sprite_scaled(dst, dst_w, dst_h, f->rgba, f->w, f->h,
                                          ax - (int)(sw / 2u), ay - dy, sw, sh,
                                          flip_x);
    }
}

static void blit_stxt_foot(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                           const stxt_frame *f, int foot_x, int foot_y,
                           int flip_x, unsigned scale_num, unsigned scale_den) {
    blit_stxt_anchored(dst, dst_w, dst_h, f, foot_x, foot_y, 0, flip_x,
                       scale_num, scale_den);
}

static void blit_stxt_anim_foot(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                                const stxt_frame *frames, unsigned count,
                                unsigned frame_idx, int foot_x, int foot_y,
                                int flip_x, unsigned scale_num,
                                unsigned scale_den) {
    if (!frames || count == 0) return;
    blit_stxt_anchored(dst, dst_w, dst_h, &frames[frame_idx % count], foot_x,
                       foot_y, 0, flip_x, scale_num, scale_den);
}

static void blit_stxt_anim_centered(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                                    const stxt_frame *frames, unsigned count,
                                    unsigned frame_idx, int cx, int cy,
                                    int flip_x, unsigned scale_num,
                                    unsigned scale_den) {
    if (!frames || count == 0) return;
    blit_stxt_anchored(dst, dst_w, dst_h, &frames[frame_idx % count], cx, cy, 1,
                       flip_x, scale_num, scale_den);
}

typedef enum {
    STXT_STAGE_OTHER = 0,
    STXT_STAGE_VALLEY,
    STXT_STAGE_BAT,
    STXT_STAGE_GLASS,
    STXT_STAGE_VORTEX,
    STXT_STAGE_BIGBNS,
    STXT_STAGE_LOGS,
    STXT_STAGE_VILLAGE,
    STXT_STAGE_MOAT,
    STXT_STAGE_STAING,
    STXT_STAGE_LAFF,
    STXT_STAGE_FINALE,
    STXT_STAGE_GENRM
} stxt_stage_tag;

static stxt_stage_tag classify_stage_name(const char *s) {
    if (!s) return STXT_STAGE_OTHER;
    if (!strcmp(s, "VALLEY"))
        return STXT_STAGE_VALLEY;
    if (!strcmp(s, "BAT1") || !strcmp(s, "BAT2") || !strcmp(s, "BAT3"))
        return STXT_STAGE_BAT;
    if (!strcmp(s, "GLASS"))
        return STXT_STAGE_GLASS;
    if (!strcmp(s, "VORTEX"))
        return STXT_STAGE_VORTEX;
    if (!strcmp(s, "BIGBNS") || !strcmp(s, "BIGBONES"))
        return STXT_STAGE_BIGBNS;
    if (!strcmp(s, "LOGS"))
        return STXT_STAGE_LOGS;
    if (!strcmp(s, "VILLAGE"))
        return STXT_STAGE_VILLAGE;
    if (!strcmp(s, "MOAT"))
        return STXT_STAGE_MOAT;
    if (!strcmp(s, "STAING"))
        return STXT_STAGE_STAING;
    if (!strcmp(s, "LAFF"))
        return STXT_STAGE_LAFF;
    if (!strcmp(s, "FINALE"))
        return STXT_STAGE_FINALE;
    if (!strcmp(s, "GENRM") || !strcmp(s, "GENROOM"))
        return STXT_STAGE_GENRM;
    return STXT_STAGE_OTHER;
}

static void select_bones_frame(int tall, int airborne, unsigned anim_frame,
                               const stxt_frame **out_orig,
                               const stxt_frame **out_hd) {
    unsigned idx = 0u;
    const stxt_frame *orig = NULL, *hd = NULL;
    if (airborne && g_stxt.bones_air_n > 0) {
        idx = anim_frame % g_stxt.bones_air_n;
        orig = &g_stxt.bones_air[idx];
        hd = &g_stxt.bones_air_hd[idx];
    } else if (tall && g_stxt.bones_tall_n > 0) {
        idx = anim_frame % g_stxt.bones_tall_n;
        orig = &g_stxt.bones_tall[idx];
        hd = &g_stxt.bones_tall_hd[idx];
    } else if (anim_frame > 0 && g_stxt.bones_walk_n > 0) {
        idx = (anim_frame - 1u) % g_stxt.bones_walk_n;
        orig = &g_stxt.bones_walk[idx];
        hd = &g_stxt.bones_walk_hd[idx];
    } else if (g_stxt.bones_stand_n > 0) {
        idx = anim_frame % g_stxt.bones_stand_n;
        orig = &g_stxt.bones_stand[idx];
        hd = &g_stxt.bones_stand_hd[idx];
    } else if (g_stxt.bones_walk_n > 0) {
        orig = &g_stxt.bones_walk[0];
        hd = &g_stxt.bones_walk_hd[0];
    }
    if (out_orig) *out_orig = orig;
    if (out_hd) *out_hd = (hd && hd->rgba) ? hd : orig;
}

void stage_enemies_draw_bones_ex(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                                 int foot_x, int foot_y, int facing, int tall,
                                 int airborne, unsigned anim_frame,
                                 unsigned scale_num, unsigned scale_den) {
    int flip = (facing < 0) ? 1 : 0;
    const stxt_frame *f = NULL;
    if (!dst || !dst_w || !dst_h) return;
    if (foot_y < 20) foot_y = 20;
    if (foot_y > (int)dst_h - 4) foot_y = (int)dst_h - 4;

    g_stxt.last_pose.valid = 1;
    g_stxt.last_pose.foot_x = foot_x;
    g_stxt.last_pose.foot_y = foot_y;
    g_stxt.last_pose.facing = facing;
    g_stxt.last_pose.tall = tall;
    g_stxt.last_pose.airborne = airborne;
    g_stxt.last_pose.anim_frame = anim_frame;
    g_stxt.last_pose.scale_num = scale_num;
    g_stxt.last_pose.scale_den = scale_den;
    if (g_stxt.bones_suppress_1x) return;

    /* 1x ground contact shadow ellipse beneath Bones' feet. */
    {
        int sh_y = g_stxt.hd_ground_valid ? g_stxt.hd_ground_y : foot_y;
        int air_px = (sh_y > foot_y) ? (sh_y - foot_y) : 0;
        int srx = (int)((11u * scale_num) / (scale_den ? scale_den : 256u));
        int sry = (int)((3u * scale_num) / (scale_den ? scale_den : 256u));
        int al = 104 - air_px * 2;
        srx -= air_px / 8;
        if (srx < 5) srx = 5;
        if (sry < 2) sry = 2;
        if (al < 28) al = 28;
        if (sh_y >= 20 && sh_y < (int)dst_h - 1)
            draw_hd_ellipse_shadow(dst, dst_w, dst_h, foot_x, sh_y + 1, srx, sry, al);
    }

    if (g_stxt.bones_invuln_timer > 0 && (g_stxt.bones_invuln_timer & 2) != 0 &&
        !g_stxt.bones_dead_flag)
        return;

    if (g_stxt.bones_dead_flag && g_stxt.bones_dead_ok && g_stxt.bones_dead.rgba) {
        f = &g_stxt.bones_dead;
    } else if (g_stxt.bones_hurt_timer > 0 && g_stxt.bones_hurt_n > 0) {
        f = &g_stxt.bones_hurt[((unsigned)g_stxt.bones_hurt_timer >> 2) % g_stxt.bones_hurt_n];
    } else {
        select_bones_frame(tall, airborne, anim_frame, &f, NULL);
    }
    if (f && f->rgba) {
        blit_stxt_foot(dst, dst_w, dst_h, f, foot_x, foot_y, flip,
                       scale_num, scale_den);
        return;
    }
    /* Minimal fallback only if no disc sprites have been loaded yet (unit tests). */
    draw_rect(dst, dst_w, dst_h, foot_x - 3, foot_y - 28, 7, 28, 225, 220, 200);
}

void stage_enemies_draw_bones(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                              int foot_x, int foot_y, int facing, int tall,
                              int airborne, unsigned anim_frame) {
    stage_enemies_draw_bones_ex(dst, dst_w, dst_h, foot_x, foot_y, facing,
                                tall, airborne, anim_frame, 256u, 256u);
}

void stage_enemies_draw_hud_skelbar(uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                                    int segments, int max_segments,
                                    int dst_x, int dst_y) {
    static const int k_fill_ox[14] = {
        24, 44, 39, 36, 8, 5, -5, -9, -14, -14, -14, -30, -30, -28
    };
    static const int k_fill_oy[14] = {
        12, 12, 12, 0, 11, 10, 11, 12, 10, 3, -5, 10, 3, -3
    };
    static const int k_empty_ox[14] = {
        22, 45, 40, 36, 9, 4, -4, -8, -15, -14, -14, -29, -29, -27
    };
    static const int k_empty_oy[14] = {
        4, 13, 13, 1, 12, 11, 12, 13, 11, 4, -5, 11, 3, -4
    };
    int active, anchor_x, anchor_y, i;
    if (!dst_rgba || !dst_w || !dst_h) return;
    if (g_stxt.hud_fill_n < 14u || g_stxt.hud_empty_n < 14u) {
        runway_compose_hud_skelbar(dst_rgba, dst_w, dst_h, segments, max_segments,
                                   dst_x, dst_y);
        return;
    }
    if (max_segments <= 0) max_segments = 14;
    if (segments < 0) segments = 0;
    if (segments > max_segments) segments = max_segments;
    active = (segments * 14 + max_segments - 1) / max_segments;
    if (segments == 0) active = 0;
    if (active > 14) active = 14;
    anchor_x = dst_x + 48;
    anchor_y = dst_y + 13;
    /* Lost/damaged segments show hollow Blue Skeletism outlines; active
     * intact segments render clean metallic bone over a crisp dark drop-shadow. */
    for (i = active; i < 14; i++) {
        const stxt_frame *e = &g_stxt.hud_empty[i];
        runway_compose_blit_sprite_ex(dst_rgba, dst_w, dst_h, e->rgba, e->w, e->h,
                                      anchor_x - k_empty_ox[i],
                                      anchor_y - k_empty_oy[i], 0);
    }
    for (i = 0; i < active; i++) {
        const stxt_frame *f = &g_stxt.hud_fill[i];
        if (f->rgba && f->w && f->h) {
            unsigned sy, sx;
            int bx = anchor_x - k_fill_ox[i] + 1;
            int by = anchor_y - k_fill_oy[i] + 1;
            for (sy = 0; sy < f->h; sy++) {
                int dy = by + (int)sy;
                if (dy < 0 || (unsigned)dy >= dst_h) continue;
                for (sx = 0; sx < f->w; sx++) {
                    int dx = bx + (int)sx;
                    size_t si = ((size_t)sy * f->w + sx) * 4u;
                    size_t di;
                    uint8_t sa;
                    if (dx < 0 || (unsigned)dx >= dst_w) continue;
                    sa = f->rgba[si + 3u];
                    if (sa < 40u) continue;
                    di = ((size_t)dy * dst_w + (unsigned)dx) * 4u;
                    dst_rgba[di + 0u] = (uint8_t)(dst_rgba[di + 0u] * 3u / 8u);
                    dst_rgba[di + 1u] = (uint8_t)(dst_rgba[di + 1u] * 3u / 8u);
                    dst_rgba[di + 2u] = (uint8_t)(dst_rgba[di + 2u] * 7u / 16u);
                }
            }
        }
        runway_compose_blit_sprite_ex(dst_rgba, dst_w, dst_h, f->rgba, f->w, f->h,
                                      anchor_x - k_fill_ox[i],
                                      anchor_y - k_fill_oy[i], 0);
    }
}

static void draw_poof_orb(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                          int cx, int cy, unsigned anim, int is_blue) {
    const stxt_frame *arr = is_blue ? g_stxt.poof_blue : g_stxt.poof_red;
    unsigned n = is_blue ? g_stxt.poof_blue_n : g_stxt.poof_red_n;
    if (n > 0) {
        blit_stxt_anim_centered(dst, dst_w, dst_h, arr, n, anim >> 2, cx, cy,
                                0, 1u, 1u);
        return;
    }
    draw_rect(dst, dst_w, dst_h, cx - 3, cy - 3, 7, 7,
              is_blue ? 80 : 240, is_blue ? 180 : 80, is_blue ? 255 : 40);
}

static void draw_thrown_bone(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                             int cx, int cy, unsigned anim,
                             unsigned scale_num, unsigned scale_den) {
    if (g_stxt.hd_thrown_n < 32u) {
        unsigned idx = g_stxt.hd_thrown_n++;
        g_stxt.hd_thrown[idx].cx = cx;
        g_stxt.hd_thrown[idx].cy = cy;
        g_stxt.hd_thrown[idx].anim = anim;
        g_stxt.hd_thrown[idx].scale_num = scale_num;
        g_stxt.hd_thrown[idx].scale_den = scale_den;
    }
    if (g_stxt.bones_suppress_1x && g_stxt.thrown_hd_ready) return;
    if (g_stxt.thrown_n > 0) {
        blit_stxt_anim_centered(dst, dst_w, dst_h, g_stxt.thrown,
                                g_stxt.thrown_n, anim, cx, cy, 0, scale_num,
                                scale_den);
        return;
    }
    draw_poof_orb(dst, dst_w, dst_h, cx, cy, anim, 0);
}

static void draw_ring_proj(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                           int cx, int cy, unsigned anim) {
    int rad = 5 + (int)(anim % 4u);
    int dx, dy;
    for (dy = -rad; dy <= rad; dy++) {
        for (dx = -rad; dx <= rad; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 >= (rad - 1) * (rad - 1) && d2 <= rad * rad)
                put_px(dst, dst_w, dst_h, cx + dx, cy + dy, 255, 210, 60);
        }
    }
}

static int is_pre_guitar_stage(const char *s) {
    if (!s || !s[0]) return 1;
    return !strcmp(s, "RUNWAY") || !strcmp(s, "MAUSO") ||
           !strcmp(s, "VALLEY") || !strcmp(s, "BAT1") ||
           !strcmp(s, "BAT2") || !strcmp(s, "BAT3");
}

static int is_night_stage_name(const char *s) {
    if (!s || !s[0]) return 0;
    return !strcmp(s, "RUNWAY") || !strcmp(s, "MAUSO") ||
           !strcmp(s, "VALLEY") || !strcmp(s, "BAT1") ||
           !strcmp(s, "BAT2") || !strcmp(s, "BAT3") ||
           !strcmp(s, "MOAT") || !strcmp(s, "STAING") ||
           !strcmp(s, "VILLAGE") || !strcmp(s, "VORTEX");
}

static void record_hd_shadow(int sx, int sy, int air_px, int rx, int ry, int alpha) {
    if (g_stxt.hd_shadow_n >= 32u) return;
    g_stxt.hd_shadows[g_stxt.hd_shadow_n].sx = sx;
    g_stxt.hd_shadows[g_stxt.hd_shadow_n].sy = sy;
    g_stxt.hd_shadows[g_stxt.hd_shadow_n].air_px = air_px;
    g_stxt.hd_shadows[g_stxt.hd_shadow_n].rx = rx;
    g_stxt.hd_shadows[g_stxt.hd_shadow_n].ry = ry;
    g_stxt.hd_shadows[g_stxt.hd_shadow_n].alpha = alpha;
    g_stxt.hd_shadow_n++;
}

/* Lookup terrain floor Y at screen column sx (z_perspective) or world px.
 * Returns 0 if floor_py is absent or the column has no valid floor. */
static int lookup_floor_py(const uint8_t *floor_py, unsigned floor_w,
                           int z_perspective, int sx, int px) {
    int fc;
    if (!floor_py || floor_w == 0u) return 0;
    fc = z_perspective
             ? (sx < 0 ? 0 : (sx >= (int)floor_w ? (int)floor_w - 1 : sx))
             : px;
    if (fc < 0 || (unsigned)fc >= floor_w) return 0;
    if (floor_py[fc] > 0u && floor_py[fc] < 224u)
        return (int)floor_py[fc];
    return 0;
}

static void record_hd_enemy(int sx, int sy, unsigned fidx, int flip,
                            unsigned scale_num, unsigned scale_den, int kind) {
    unsigned idx;
    if (g_stxt.hd_enemy_n >= 32u) return;
    idx = g_stxt.hd_enemy_n++;
    g_stxt.hd_enemies[idx].sx = sx;
    g_stxt.hd_enemies[idx].sy = sy;
    g_stxt.hd_enemies[idx].fidx = fidx;
    g_stxt.hd_enemies[idx].flip = flip;
    g_stxt.hd_enemies[idx].scale_num = scale_num;
    g_stxt.hd_enemies[idx].scale_den = scale_den;
    g_stxt.hd_enemies[idx].kind = kind;
}

void stage_enemies_draw_houses(const stage_enemies *e, uint8_t *rgba, unsigned width,
                              unsigned height, int camera_x, int camera_y) {
    if (!e || g_stxt.vil_house_n != 12u) return;
    for (unsigned i = 0; i < e->prop_count; i++) {
        const stage_prop *house = &e->props[i];
        const stxt_frame *frame = &g_stxt.vil_house[house->anim];
        runway_compose_blit_sprite_ex(rgba, width, height, frame->rgba, frame->w, frame->h,
                                      house->x - camera_x - house->anchor_x,
                                      house->y - camera_y - house->anchor_y, 0);
    }
}

void stage_enemies_render_ex(const stage_enemies *e, const runway_hazards *haz,
                             uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                             uint32_t camera_x, int screen_anchor_x, int base_y,
                             const uint8_t *floor_py, unsigned floor_w,
                             int z_perspective) {
    unsigned i, u, oi, order_n = 0u;
    unsigned order[STAGE_ENEMY_MAX];
    int order_key[STAGE_ENEMY_MAX];
    stxt_stage_tag stage;
    if (!dst_rgba || !dst_w || !dst_h) return;
    if (base_y < 64 || (unsigned)base_y >= dst_h - 4u) base_y = 148;
    u = (e && e->units_per_pixel) ? e->units_per_pixel : 2341u;
    stage = classify_stage_name(e ? e->stage_name : NULL);
    g_stxt.hd_night_stage = is_night_stage_name(e ? e->stage_name : NULL);

    if (e) {
        /* B2: depth-sort active enemies by grounded screen sy (mirror HD path)
         * so overlapping silhouettes paint back-to-front at 1x. */
        order_n = 0u;
        for (i = 0; i < e->count && i < STAGE_ENEMY_MAX; i++) {
            const stage_enemy *se = &e->ents[i];
            int spx, ssx, ssy;
            int has_col_floor = 0;
            if (!se->active || se->recorded) continue;
            spx = (int)(se->x / u);
            ssx = spx - (int)camera_x + screen_anchor_x;
            ssy = base_y;
            if (floor_py && floor_w > 0u) {
                int fc = z_perspective
                             ? (ssx < 0 ? 0 : (ssx >= (int)floor_w ? (int)floor_w - 1 : ssx))
                             : spx;
                if (fc >= 0 && (unsigned)fc < floor_w &&
                    floor_py[fc] > 0 && floor_py[fc] < 224u) {
                    ssy = (int)floor_py[fc];
                    has_col_floor = 1;
                }
            } else if (z_perspective) {
                int ey = (int)(se->y / u);
                if (ey >= 96 && ey <= 210) ssy = ey;
            }
            if (se->kind == STAGE_ENEMY_ZOMBIE && !has_col_floor && ssy < 124)
                ssy = 124;
            order[order_n] = i;
            order_key[order_n] = ssy;
            order_n++;
        }
        for (oi = 1u; oi < order_n; oi++) {
            unsigned cv = order[oi];
            int ck = order_key[oi];
            unsigned kk = oi;
            while (kk > 0u && order_key[kk - 1u] > ck) {
                order[kk] = order[kk - 1u];
                order_key[kk] = order_key[kk - 1u];
                kk--;
            }
            order[kk] = cv;
            order_key[kk] = ck;
        }

        for (oi = 0u; oi < order_n; oi++) {
            static const unsigned k_gang_walk[6] = {0u, 1u, 2u, 3u, 2u, 1u};
            const stage_enemy *ent;
            int px, sx, sy, flip;
            int has_col_floor = 0;
            unsigned scale_num = 256u, scale_den = 256u;
            unsigned fidx, phase;
            int hover;
            i = order[oi];
            ent = &e->ents[i];
            fidx = ent->anim >> 2;
            phase = (ent->anim >> 1) & 15u;
            hover = (int)(phase <= 8u ? phase : 16u - phase);
            if (!ent->active) continue;
            px = (int)(ent->x / u);
            sx = px - (int)camera_x + screen_anchor_x;
            if (sx < -48 || sx >= (int)dst_w + 48) continue;
            sy = base_y;
            if (floor_py && floor_w > 0u) {
                int fidx_col = z_perspective
                                   ? (sx < 0 ? 0 : (sx >= (int)floor_w ? (int)floor_w - 1 : sx))
                                   : px;
                if (fidx_col >= 0 && (unsigned)fidx_col < floor_w &&
                    floor_py[fidx_col] > 0 && floor_py[fidx_col] < 224u) {
                    sy = (int)floor_py[fidx_col];
                    has_col_floor = 1;
                }
            } else if (z_perspective) {
                int ey = (int)(ent->y / u);
                if (ey >= 96 && ey <= 210) sy = ey;
            }
            if (z_perspective && is_stage_runway(e) &&
                ent->kind == STAGE_ENEMY_GANG &&
                ent->patrol_lo <= 16u * u && ent->patrol_hi >= 1500u * u) {
                unsigned lp = (ent->anim % 44u);
                int leap_px = (lp < 18u) ? (int)((lp * (18u - lp)) / 4u) : 0;
                sy -= leap_px;
                if (sy < 24) sy = 24;
            }
            flip = (ent->facing != 0) ? ((ent->facing < 0) ? 1 : 0)
                                      : ((ent->vx < 0) ? 1 : 0);
            /* Keep grounded zombies on the floor line when no tile floor is present. */
            if (ent->kind == STAGE_ENEMY_ZOMBIE && !has_col_floor && sy < 124)
                sy = 124;
            if (ent->kind == STAGE_ENEMY_LORD || ent->kind == STAGE_ENEMY_ZOMBIE ||
                (ent->kind == STAGE_ENEMY_GANG &&
                 stage != STXT_STAGE_GLASS && stage != STXT_STAGE_VORTEX &&
                 stage != STXT_STAGE_LOGS && stage != STXT_STAGE_MOAT &&
                 stage != STXT_STAGE_STAING && stage != STXT_STAGE_BIGBNS)) {
                int srx = (int)((13u * scale_num) / scale_den);
                int sry = (int)((3u * scale_num) / scale_den);
                if (srx < 7) srx = 7;
                if (sry < 2) sry = 2;
                record_hd_shadow(sx, sy, 0, srx, sry, 112);
                if (!g_stxt.bones_suppress_1x)
                    draw_hd_ellipse_shadow(dst_rgba, dst_w, dst_h, sx, sy + 1,
                                           srx, sry, 92);
            }

            switch (ent->kind) {
            case STAGE_ENEMY_GANG:
                if (stage == STXT_STAGE_VALLEY && g_stxt.valley_skel_n > 0) {
                    static const unsigned k_val_gang[8] = {
                        0u, 1u, 2u, 3u, 4u, 3u, 2u, 1u
                    };
                    /* valley_skel is natively LEFT-facing: mirror only when facing
                     * RIGHT (facing>0 -> flip=0 -> vflip=1). Same flag feeds 1x + HD. */
                    int vflip = flip ? 0 : 1;
                    unsigned vf = k_val_gang[fidx & 7u];
                    record_hd_enemy(sx, sy, vf, vflip, scale_num, scale_den,
                                    STXT_HD_ENEMY_VALLEY);
                    if (!(g_stxt.bones_suppress_1x && g_stxt.valley_skel_hd_ready)) {
                        blit_stxt_anim_foot(dst_rgba, dst_w, dst_h,
                                            g_stxt.valley_skel,
                                            g_stxt.valley_skel_n, vf, sx, sy,
                                            vflip, scale_num, scale_den);
                    }
                } else if (stage == STXT_STAGE_GLASS &&
                           g_stxt.glass_skull_n > 0) {
                    int gsy = (ent->y > 0u && ent->y < 220u * u)
                                  ? (int)(ent->y / u)
                                  : (sy - 8 - hover);
                    blit_stxt_anim_foot(dst_rgba, dst_w, dst_h,
                                        g_stxt.glass_skull,
                                        g_stxt.glass_skull_n, fidx, sx,
                                        gsy, 0, scale_num,
                                        scale_den);
                } else if (stage == STXT_STAGE_VORTEX &&
                           g_stxt.vortex_blue_n > 0) {
                    blit_stxt_anim_foot(dst_rgba, dst_w, dst_h,
                                        g_stxt.vortex_blue,
                                        g_stxt.vortex_blue_n, fidx, sx,
                                        sy - 10 - hover, flip, scale_num,
                                        scale_den);
                } else if (stage == STXT_STAGE_BIGBNS &&
                           g_stxt.bigbns_bumper_n > 0) {
                    blit_stxt_anim_foot(dst_rgba, dst_w, dst_h,
                                        g_stxt.bigbns_bumper,
                                        g_stxt.bigbns_bumper_n, fidx, sx, sy, 0,
                                        scale_num, scale_den);
                } else if (stage == STXT_STAGE_LOGS &&
                           g_stxt.logs_buttrfly_blue_n > 0) {
                    blit_stxt_anim_foot(dst_rgba, dst_w, dst_h,
                                        g_stxt.logs_buttrfly_blue,
                                        g_stxt.logs_buttrfly_blue_n, fidx, sx,
                                        sy - 12 - hover, flip, scale_num,
                                        scale_den);
                } else if (stage == STXT_STAGE_VILLAGE &&
                           g_stxt.vil_spider_n > 0) {
                    blit_stxt_anim_foot(dst_rgba, dst_w, dst_h,
                                        g_stxt.vil_spider, g_stxt.vil_spider_n,
                                        fidx, sx, sy, flip, scale_num,
                                        scale_den);
                } else if (stage == STXT_STAGE_MOAT &&
                           g_stxt.moat_wingnut_n > 0) {
                    blit_stxt_anim_foot(dst_rgba, dst_w, dst_h,
                                        g_stxt.moat_wingnut,
                                        g_stxt.moat_wingnut_n, fidx, sx,
                                        sy - 12 - hover, flip, scale_num,
                                        scale_den);
                } else if (g_stxt.gang_n > 0) {
                    unsigned gang_fidx = (ent->throw_phase >= 4u &&
                                          ent->throw_phase <= 7u)
                                             ? (unsigned)ent->throw_phase
                                             : k_gang_walk[fidx % 6u];
                    record_hd_enemy(sx, sy, gang_fidx, flip, scale_num,
                                    scale_den, STXT_HD_ENEMY_GANG);
                    if (!(g_stxt.bones_suppress_1x && g_stxt.gang_hd_ready)) {
                        blit_stxt_anim_foot(dst_rgba, dst_w, dst_h, g_stxt.gang,
                                            g_stxt.gang_n, gang_fidx, sx, sy,
                                            flip, scale_num, scale_den);
                    }
                } else {
                    draw_rect(dst_rgba, dst_w, dst_h, sx - 4, sy - 24, 8, 24,
                              150, 180, 170);
                }
                break;
            case STAGE_ENEMY_LORD:
                if (stage == STXT_STAGE_VALLEY && g_stxt.valley_skel_n > 0) {
                    static const unsigned k_val_lord[14] = {
                        5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 11u, 10u, 9u, 8u, 7u, 6u
                    };
                    /* valley_skel is natively LEFT-facing: mirror only when facing
                     * RIGHT (facing>0 -> flip=0 -> vflip=1). Same flag feeds 1x + HD. */
                    int vflip = flip ? 0 : 1;
                    unsigned vf = k_val_lord[fidx % 14u];
                    record_hd_enemy(sx, sy, vf, vflip, scale_num,
                                    scale_den, STXT_HD_ENEMY_VALLEY);
                    if (!(g_stxt.bones_suppress_1x && g_stxt.valley_skel_hd_ready)) {
                        blit_stxt_anim_foot(dst_rgba, dst_w, dst_h,
                                            g_stxt.valley_skel,
                                            g_stxt.valley_skel_n, vf,
                                            sx, sy, vflip, scale_num, scale_den);
                    }
                } else if (stage == STXT_STAGE_GLASS &&
                           g_stxt.glass_skull_n > 0) {
                    int gsy = (ent->y > 0u && ent->y < 220u * u)
                                  ? (int)(ent->y / u)
                                  : (sy - 12 - hover);
                    blit_stxt_anim_foot(dst_rgba, dst_w, dst_h,
                                        g_stxt.glass_skull,
                                        g_stxt.glass_skull_n, fidx + 8u, sx,
                                        gsy, 0, scale_num,
                                        scale_den);
                } else if (stage == STXT_STAGE_VORTEX &&
                           g_stxt.vortex_red_n > 0) {
                    blit_stxt_anim_foot(dst_rgba, dst_w, dst_h,
                                        g_stxt.vortex_red,
                                        g_stxt.vortex_red_n, fidx, sx,
                                        sy - 12 - hover, flip, scale_num,
                                        scale_den);
                } else if (stage == STXT_STAGE_LAFF &&
                           g_stxt.laff_clown_n > 0) {
                    int lsy = (ent->y > 0u && ent->y < (unsigned)sy * u)
                                  ? (int)(ent->y / u)
                                  : sy;
                    if (ent->hp <= STAGE_LAFF_BOSS_HP / 2 && (ent->anim & 2u))
                        draw_rect(dst_rgba, dst_w, dst_h, sx - 16, lsy - 1, 32,
                                  3, 255, 75, 110);
                    {
                        /* Shrink the clown so it clears the "DA LAFF 8/8" HUD. */
                        unsigned lnum = (scale_num * 168u) / 256u;
                        record_hd_enemy(sx, lsy, fidx, flip, lnum, scale_den,
                                        STXT_HD_ENEMY_LAFF);
                        if (!(g_stxt.bones_suppress_1x && g_stxt.laff_clown_hd_ready)) {
                            blit_stxt_anim_foot(dst_rgba, dst_w, dst_h,
                                                g_stxt.laff_clown,
                                                g_stxt.laff_clown_n, fidx, sx, lsy,
                                                flip, lnum, scale_den);
                        }
                    }
                } else if (stage == STXT_STAGE_FINALE && g_stxt.lord_n > 0) {
                    int fsy = (ent->y > 0u && ent->y < (unsigned)sy * u)
                                  ? (int)(ent->y / u)
                                  : sy;
                    int lflip = (ent->vx > 0) ? 1 : 0;
                    record_hd_enemy(sx, fsy, fidx, lflip, scale_num, scale_den,
                                    STXT_HD_ENEMY_LORD);
                    if (!(g_stxt.bones_suppress_1x && g_stxt.lord_hd_ready)) {
                        blit_stxt_anim_foot(dst_rgba, dst_w, dst_h, g_stxt.lord,
                                            g_stxt.lord_n, fidx, sx, fsy, lflip,
                                            scale_num, scale_den);
                    }
                } else if (stage == STXT_STAGE_LOGS &&
                    g_stxt.logs_buttrfly_red_n > 0) {
                    blit_stxt_anim_foot(dst_rgba, dst_w, dst_h,
                                        g_stxt.logs_buttrfly_red,
                                        g_stxt.logs_buttrfly_red_n, fidx, sx,
                                        sy - 16 - hover, flip, scale_num,
                                        scale_den);
                } else if (stage == STXT_STAGE_VILLAGE &&
                           g_stxt.vil_mtrspid_n > 0) {
                    blit_stxt_anim_foot(dst_rgba, dst_w, dst_h,
                                        g_stxt.vil_mtrspid,
                                        g_stxt.vil_mtrspid_n, fidx, sx, sy,
                                        flip, scale_num, scale_den);
                } else if (stage == STXT_STAGE_MOAT &&
                           g_stxt.moat_wingnut_n > 0) {
                    blit_stxt_anim_foot(dst_rgba, dst_w, dst_h,
                                        g_stxt.moat_wingnut,
                                        g_stxt.moat_wingnut_n, fidx, sx,
                                        sy - 16 - hover, flip, scale_num,
                                        scale_den);
                } else if (g_stxt.lord_n > 0) {
                    /* 05MAUSO upright skeleton faces left natively. */
                    int lflip = (ent->vx > 0) ? 1 : 0;
                    record_hd_enemy(sx, sy, fidx, lflip, scale_num, scale_den,
                                    STXT_HD_ENEMY_LORD);
                    if (!(g_stxt.bones_suppress_1x && g_stxt.lord_hd_ready)) {
                        blit_stxt_anim_foot(dst_rgba, dst_w, dst_h, g_stxt.lord,
                                            g_stxt.lord_n, fidx, sx, sy, lflip,
                                            scale_num, scale_den);
                    }
                } else if (g_stxt.gang_n > 0) {
                    unsigned gang_fidx = k_gang_walk[fidx % 6u];
                    record_hd_enemy(sx, sy, gang_fidx, flip, scale_num,
                                    scale_den, STXT_HD_ENEMY_GANG);
                    if (!(g_stxt.bones_suppress_1x && g_stxt.gang_hd_ready)) {
                        blit_stxt_anim_foot(dst_rgba, dst_w, dst_h, g_stxt.gang,
                                            g_stxt.gang_n, gang_fidx, sx, sy,
                                            flip, scale_num, scale_den);
                    }
                }
                break;
            case STAGE_ENEMY_ZOMBIE:
                if (g_stxt.zombies_n > 0) {
                    /* sy is pre-clamped when no column floor was found. */
                    record_hd_enemy(sx, sy, fidx, flip, scale_num, scale_den,
                                    STXT_HD_ENEMY_ZOMBIE);
                    if (!(g_stxt.bones_suppress_1x && g_stxt.zombies_hd_ready)) {
                        blit_stxt_anim_foot(dst_rgba, dst_w, dst_h, g_stxt.zombies,
                                            g_stxt.zombies_n, fidx, sx, sy, flip,
                                            scale_num, scale_den);
                    }
                } else if (g_stxt.gang_n > 0) {
                    unsigned gang_fidx = k_gang_walk[fidx % 6u];
                    record_hd_enemy(sx, sy, gang_fidx, flip, scale_num,
                                    scale_den, STXT_HD_ENEMY_GANG);
                    if (!(g_stxt.bones_suppress_1x && g_stxt.gang_hd_ready)) {
                        blit_stxt_anim_foot(dst_rgba, dst_w, dst_h, g_stxt.gang,
                                            g_stxt.gang_n, gang_fidx, sx, sy,
                                            flip, scale_num, scale_den);
                    }
                }
                break;
            case STAGE_ENEMY_BAT:
            case STAGE_ENEMY_WINGNUT: {
                int fly_y = (ent->y > 0u && ent->y < 220u * u)
                                ? (int)(ent->y / u)
                                : (76 + (int)((i % 3u) * 18u) + hover);
                if (fly_y < 28) fly_y = 28;
                if (fly_y > (int)dst_h - 8) fly_y = (int)dst_h - 8;
                if (ent->kind == STAGE_ENEMY_WINGNUT && g_stxt.moat_wingnut_n > 0) {
                    blit_stxt_anim_foot(dst_rgba, dst_w, dst_h, g_stxt.moat_wingnut,
                                        g_stxt.moat_wingnut_n, fidx, sx, fly_y,
                                        flip, scale_num, scale_den);
                } else if (g_stxt.bats_n > 0) {
                    unsigned b_num = (scale_num * 156u) / 256u;
                    blit_stxt_anim_foot(dst_rgba, dst_w, dst_h, g_stxt.bats,
                                        g_stxt.bats_n, fidx, sx, fly_y, flip,
                                        b_num, scale_den);
                } else {
                    draw_poof_orb(dst_rgba, dst_w, dst_h, sx, fly_y - 12,
                                  ent->anim, 1);
                }
                break;
            }
            case STAGE_ENEMY_HAND:
                if (g_stxt.vil_hand_n > 0) {
                    blit_stxt_anim_foot(dst_rgba, dst_w, dst_h, g_stxt.vil_hand,
                                        g_stxt.vil_hand_n, fidx, sx, sy, flip,
                                        scale_num, scale_den);
                } else {
                    draw_rect(dst_rgba, dst_w, dst_h, sx - 12, sy - 14, 24, 14,
                              210, 200, 180);
                }
                break;
            case STAGE_ENEMY_LILGUYS:
                if (g_stxt.vil_lilguys_n > 0) {
                    blit_stxt_anim_foot(dst_rgba, dst_w, dst_h, g_stxt.vil_lilguys,
                                        g_stxt.vil_lilguys_n, fidx, sx, sy, flip,
                                        scale_num, scale_den);
                } else {
                    draw_rect(dst_rgba, dst_w, dst_h, sx - 5, sy - 14, 10, 14,
                              190, 145, 95);
                }
                break;
            case STAGE_ENEMY_SHADOW: {
                unsigned sh_scale = (scale_num * 168u) / 256u;
                if (sh_scale < 96u) sh_scale = 96u;
                if (ent->cooldown > 72u && g_stxt.shadow_lit_n > 0) {
                    blit_stxt_anim_foot(dst_rgba, dst_w, dst_h, g_stxt.shadow_lit,
                                        g_stxt.shadow_lit_n, fidx, sx, sy, flip,
                                        sh_scale, scale_den);
                } else if (g_stxt.shadow_n > 0) {
                    record_hd_enemy(sx, sy, fidx, flip, sh_scale, scale_den,
                                    STXT_HD_ENEMY_SHADOW);
                    if (!(g_stxt.bones_suppress_1x && g_stxt.shadow_hd_ready)) {
                        blit_stxt_anim_foot(dst_rgba, dst_w, dst_h, g_stxt.shadow,
                                            g_stxt.shadow_n, fidx, sx, sy, flip,
                                            sh_scale, scale_den);
                    }
                } else {
                    uint8_t er = ent->cooldown > 0 ? 110u : 20u;
                    uint8_t eg = ent->cooldown > 0 ? 220u : 20u;
                    uint8_t eb = ent->cooldown > 0 ? 255u : 40u;
                    draw_rect(dst_rgba, dst_w, dst_h, sx - 6, sy - 26, 12, 26,
                              18, 14, 28);
                    draw_rect(dst_rgba, dst_w, dst_h, sx - 3, sy - 22, 2, 2, er, eg, eb);
                    draw_rect(dst_rgba, dst_w, dst_h, sx + 1, sy - 22, 2, 2, er, eg, eb);
                }
                break;
            }
            case STAGE_ENEMY_POOF_RED:
            case STAGE_ENEMY_POOF_BLUE:
                if (stage == STXT_STAGE_BAT && g_stxt.bats_n > 0) {
                    blit_stxt_anim_foot(dst_rgba, dst_w, dst_h, g_stxt.bats,
                                        g_stxt.bats_n, fidx, sx,
                                        sy - 28 - (int)((ent->anim >> 1) & 7u),
                                        flip, scale_num, scale_den);
                } else if (stage == STXT_STAGE_VORTEX &&
                           ent->kind == STAGE_ENEMY_POOF_BLUE &&
                           g_stxt.vortex_blue_n > 0) {
                    blit_stxt_anim_centered(
                        dst_rgba, dst_w, dst_h, g_stxt.vortex_blue,
                        g_stxt.vortex_blue_n, fidx, sx, sy - 18, 0, scale_num,
                        scale_den);
                } else if (stage == STXT_STAGE_VORTEX &&
                           ent->kind == STAGE_ENEMY_POOF_RED &&
                           g_stxt.vortex_red_n > 0) {
                    blit_stxt_anim_centered(
                        dst_rgba, dst_w, dst_h, g_stxt.vortex_red,
                        g_stxt.vortex_red_n, fidx, sx, sy - 18, 0, scale_num,
                        scale_den);
                } else if (stage == STXT_STAGE_BIGBNS &&
                           ent->kind == STAGE_ENEMY_POOF_RED &&
                           g_stxt.bigbns_redpoof_n > 0) {
                    blit_stxt_anim_centered(
                        dst_rgba, dst_w, dst_h, g_stxt.bigbns_redpoof,
                        g_stxt.bigbns_redpoof_n, fidx, sx, sy - 18, 0,
                        scale_num, scale_den);
                } else if (stage == STXT_STAGE_LOGS &&
                           ent->kind == STAGE_ENEMY_POOF_BLUE &&
                           g_stxt.logs_bluepoof_n > 0) {
                    blit_stxt_anim_centered(
                        dst_rgba, dst_w, dst_h, g_stxt.logs_bluepoof,
                        g_stxt.logs_bluepoof_n, fidx, sx, sy - 18, 0, scale_num,
                        scale_den);
                } else if (stage == STXT_STAGE_MOAT &&
                           ent->kind == STAGE_ENEMY_POOF_RED &&
                           g_stxt.moat_redpoof_n > 0) {
                    blit_stxt_anim_centered(
                        dst_rgba, dst_w, dst_h, g_stxt.moat_redpoof,
                        g_stxt.moat_redpoof_n, fidx, sx, sy - 18, 0, scale_num,
                        scale_den);
                } else if (stage == STXT_STAGE_STAING &&
                           g_stxt.staing_orb_n > 0) {
                    int oby = (ent->y > 60u * u && ent->y < 200u * u)
                                  ? (int)(ent->y / u)
                                  : (sy - 20);
                    oby -= hover / 2;
                    blit_stxt_anim_centered(
                        dst_rgba, dst_w, dst_h, g_stxt.staing_orb,
                        g_stxt.staing_orb_n, fidx, sx, oby, 0,
                        scale_num, scale_den);
                } else if (stage == STXT_STAGE_GENRM &&
                           g_stxt.poof_red_n == 0 && g_stxt.gen_red_n > 0) {
                    blit_stxt_anim_centered(dst_rgba, dst_w, dst_h,
                                            g_stxt.gen_red, g_stxt.gen_red_n,
                                            fidx, sx, sy - 18, 0, scale_num,
                                            scale_den);
                } else {
                    draw_poof_orb(dst_rgba, dst_w, dst_h, sx,
                                  sy - 18 - (int)((ent->anim >> 1) & 3u),
                                  ent->anim,
                                  ent->kind == STAGE_ENEMY_POOF_BLUE);
                }
                break;
            case STAGE_ENEMY_RED:
                if (g_stxt.gen_red_n > 0) {
                    blit_stxt_anim_foot(dst_rgba, dst_w, dst_h, g_stxt.gen_red,
                                        g_stxt.gen_red_n, fidx, sx, sy, flip,
                                        scale_num, scale_den);
                } else {
                    draw_poof_orb(dst_rgba, dst_w, dst_h, sx, sy - 20,
                                  ent->anim, 0);
                }
                break;
            case STAGE_ENEMY_BLUE:
                if (g_stxt.gen_blue_n > 0) {
                    blit_stxt_anim_foot(dst_rgba, dst_w, dst_h, g_stxt.gen_blue,
                                        g_stxt.gen_blue_n, fidx, sx, sy, flip,
                                        scale_num, scale_den);
                } else {
                    draw_poof_orb(dst_rgba, dst_w, dst_h, sx, sy - 20,
                                  ent->anim, 1);
                }
                break;
            case STAGE_ENEMY_ZAP:
                if (g_stxt.gen_zap_n > 0) {
                    blit_stxt_anim_centered(dst_rgba, dst_w, dst_h,
                                            g_stxt.gen_zap, g_stxt.gen_zap_n,
                                            fidx, sx, sy - 22, 0, scale_num,
                                            scale_den);
                } else {
                    draw_poof_orb(dst_rgba, dst_w, dst_h, sx, sy - 22,
                                  ent->anim, 1);
                }
                break;
            case STAGE_ENEMY_RING:
                if (g_stxt.fin_ring_n > 0) {
                    unsigned rf = (ent->anim * 4u + ent->cooldown) >> 2;
                    blit_stxt_anim_centered(dst_rgba, dst_w, dst_h,
                                            g_stxt.fin_ring, g_stxt.fin_ring_n,
                                            rf, sx, sy - 16, 0, scale_num,
                                            scale_den);
                } else {
                    draw_ring_proj(dst_rgba, dst_w, dst_h, sx, sy - 16,
                                   ent->anim);
                }
                break;
            case STAGE_ENEMY_BOLT:
                if (ent->cooldown > 0u && ent->cooldown <= 10u && sy > 28) {
                    int y, dx;
                    int pulse = (ent->cooldown & 2u) ? 1 : 0;
                    for (y = 28; y < sy; y++) {
                        int fade = 160 + (95 * (y - 28)) / ((sy > 28) ? (sy - 28) : 1);
                        for (dx = -2; dx <= 2; dx++) {
                            int px_x = sx + dx;
                            int a;
                            uint8_t br, bg, bb;
                            size_t di;
                            if (px_x < 0 || (unsigned)px_x >= dst_w) continue;
                            if (dx == 0) {
                                a = (210 * fade) / 255;
                                br = 255u;
                                bg = (uint8_t)(pulse ? 245u : 195u);
                                bb = (uint8_t)(pulse ? 160u : 85u);
                            } else {
                                a = ((abs(dx) == 1 ? 110 : 48) * fade) / 255;
                                br = 255u;
                                bg = (uint8_t)(pulse ? 180u : 105u);
                                bb = 40u;
                            }
                            di = ((size_t)y * dst_w + (unsigned)px_x) * 4u;
                            dst_rgba[di + 0u] = (uint8_t)((dst_rgba[di + 0u] * (255 - a) + br * a) / 255);
                            dst_rgba[di + 1u] = (uint8_t)((dst_rgba[di + 1u] * (255 - a) + bg * a) / 255);
                            dst_rgba[di + 2u] = (uint8_t)((dst_rgba[di + 2u] * (255 - a) + bb * a) / 255);
                        }
                    }
                    for (dx = -7; dx <= 7; dx++) {
                        int px_x = sx + dx;
                        int y0, a;
                        if (px_x < 0 || (unsigned)px_x >= dst_w) continue;
                        a = (190 * (49 - dx * dx)) / 49;
                        if (a <= 0) continue;
                        for (y0 = -1; y0 <= 1; y0++) {
                            int py_y = sy - 1 + y0;
                            int ay = y0 == 0 ? a : (a * 2) / 5;
                            size_t di;
                            if (py_y < 0 || (unsigned)py_y >= dst_h) continue;
                            di = ((size_t)py_y * dst_w + (unsigned)px_x) * 4u;
                            dst_rgba[di + 0u] = (uint8_t)((dst_rgba[di + 0u] * (255 - ay) + 255 * ay) / 255);
                            dst_rgba[di + 1u] = (uint8_t)((dst_rgba[di + 1u] * (255 - ay) + 215 * ay) / 255);
                            dst_rgba[di + 2u] = (uint8_t)((dst_rgba[di + 2u] * (255 - ay) + 85 * ay) / 255);
                        }
                    }
                }
                if (g_stxt.fin_bolt_v_n > 0) {
                    blit_stxt_anim_centered(
                        dst_rgba, dst_w, dst_h, g_stxt.fin_bolt_v,
                        g_stxt.fin_bolt_v_n, fidx, sx, sy - 18, 0, scale_num,
                        scale_den);
                } else {
                    draw_poof_orb(dst_rgba, dst_w, dst_h, sx, sy - 18,
                                  ent->anim, 0);
                }
                break;
            case STAGE_ENEMY_THROWN:
                if (stage == STXT_STAGE_LAFF) {
                    draw_poof_orb(dst_rgba, dst_w, dst_h, sx, sy - 16,
                                  ent->anim, (ent->anim & 2u) ? 1 : 0);
                    draw_rect(dst_rgba, dst_w, dst_h, sx - 2, sy - 18, 4, 4,
                              255, 220, 70);
                } else {
                    draw_thrown_bone(dst_rgba, dst_w, dst_h, sx, sy - 18, fidx,
                                     scale_num, scale_den);
                }
                break;
            }
        }
    }

    if (haz) {
        int is_ride = (e && e->stage_name && !strcmp(e->stage_name, "RIDE"));
        for (i = 0; i < haz->zone_count && i < RUNWAY_HAZARD_ZONES; i++) {
            int cx_px = (int)(((haz->zones[i].x0 + haz->zones[i].x1) / 2u) / u);
            int zsx = cx_px - (int)camera_x + screen_anchor_x;
            int zsy = base_y, fsy;
            unsigned z_anim = (unsigned)(camera_x >> 1) + i * 3u;
            if (zsx < -20 || zsx >= (int)dst_w + 20) continue;
            fsy = lookup_floor_py(floor_py, floor_w, z_perspective, zsx, cx_px);
            if (fsy > 0) zsy = fsy;
            if (is_ride && g_stxt.ride_bubbles_n > 0) {
                blit_stxt_anim_foot(dst_rgba, dst_w, dst_h, g_stxt.ride_bubbles,
                                    g_stxt.ride_bubbles_n, z_anim, zsx, zsy + 8,
                                    0, 224u, 256u);
            } else if (stage == STXT_STAGE_MOAT && g_stxt.moat_redpoof_n > 0) {
                blit_stxt_anim_foot(dst_rgba, dst_w, dst_h, g_stxt.moat_redpoof,
                                    g_stxt.moat_redpoof_n, z_anim, zsx, zsy + 2,
                                    0, 176u, 256u);
            } else if (stage == STXT_STAGE_LOGS && g_stxt.logs_buttrfly_red_n > 0) {
                blit_stxt_anim_foot(dst_rgba, dst_w, dst_h, g_stxt.logs_buttrfly_red,
                                    g_stxt.logs_buttrfly_red_n, z_anim, zsx, zsy,
                                    0, 224u, 256u);
            }
        }
        for (i = 0; i < RUNWAY_THROWN_MAX; i++) {
            int px, sx, haz_sy = base_y;
            unsigned scale_num = 256u;
            if (!haz->thrown[i].active) continue;
            px = (int)(haz->thrown[i].x / u);
            sx = px - (int)camera_x + screen_anchor_x;
            if (sx < -16 || sx >= (int)dst_w + 16) continue;
            if (z_perspective) {
                if (floor_py && floor_w > 0u) {
                    int csx = sx < 0 ? 0 : (sx >= (int)floor_w ? (int)floor_w - 1 : sx);
                    if (floor_py[csx] > 0u && floor_py[csx] < 224u)
                        haz_sy = (int)floor_py[csx];
                } else {
                    int hy = (int)(haz->thrown[i].y / u);
                    if (hy >= 96 && hy <= 210) haz_sy = hy;
                }
            }
            draw_thrown_bone(dst_rgba, dst_w, dst_h, sx, haz_sy - 30,
                             (unsigned)(px >> 2) + i, scale_num, 256u);
        }
    }

    if (e) {
        for (i = 0; i < e->pickup_count && i < STAGE_PICKUP_MAX; i++) {
            const stage_pickup *p = &e->pickups[i];
            int px, sx, sy = base_y, fsy;
            unsigned scale_num = 256u;
            if (!p->active) continue;
            px = (int)(p->x / u);
            sx = px - (int)camera_x + screen_anchor_x;
            if (sx < -24 || sx >= (int)dst_w + 24) continue;
            fsy = lookup_floor_py(floor_py, floor_w, z_perspective, sx, px);
            if (fsy > 0) {
                sy = fsy;
            } else if ((int)(p->y / u) >= 64 && (int)(p->y / u) <= 210) {
                sy = (int)(p->y / u);
            }
            if (g_stxt.health_n > 0) {
                blit_stxt_anim_foot(dst_rgba, dst_w, dst_h, g_stxt.health,
                                    g_stxt.health_n, p->anim >> 2, sx, sy, 0,
                                    scale_num, 256u);
            } else {
                draw_rect(dst_rgba, dst_w, dst_h, sx - 4, sy - 14, 9, 9,
                          245, 210, 60);
                draw_rect(dst_rgba, dst_w, dst_h, sx - 2, sy - 12, 5, 5,
                          255, 250, 210);
            }
        }

        for (i = 0; i < STAGE_SHOT_MAX; i++) {
            const stage_blues_shot *s = &e->shots[i];
            int px, sx, sy, fsy;
            if (!s->active) continue;
            px = (int)(s->x / u);
            sx = px - (int)camera_x + screen_anchor_x;
            sy = (int)(s->y / u);
            if (sy < 24 || sy >= (int)dst_h) sy = base_y;
            fsy = lookup_floor_py(floor_py, floor_w, z_perspective, sx, px);
            if (fsy > 0) {
                int ref_gy = z_perspective ? 140 : base_y;
                int lift = ref_gy - sy;
                if (lift < 0 || lift > 96) lift = 0;
                sy = fsy - lift;
                if (sy < 24) sy = 24;
            }
            if (s->is_radial) {
                int rad = 10 + (int)(12u - s->ttl) * 3;
                int dx, dy;
                int life_a = (int)((s->ttl * 220u) / 12u);
                if (life_a > 220) life_a = 220;
                if (life_a < 70) life_a = 70;
                for (dy = -rad - 1; dy <= rad + 1; dy++) {
                    int py_y = sy - 20 + dy;
                    if (py_y < 0 || (unsigned)py_y >= dst_h) continue;
                    for (dx = -rad - 1; dx <= rad + 1; dx++) {
                        int px_x = sx + dx;
                        int d2 = dx * dx + dy * dy;
                        int r_in = (rad - 2) * (rad - 2);
                        int r_mid = (rad - 1) * (rad - 1);
                        int r_out = (rad + 1) * (rad + 1);
                        int a = 0;
                        uint8_t br = 90u, bg = 225u, bb = 255u;
                        size_t di;
                        if (px_x < 0 || (unsigned)px_x >= dst_w) continue;
                        if (d2 >= r_mid && d2 <= rad * rad) {
                            a = life_a;
                            br = 215u; bg = 248u; bb = 255u;
                        } else if (d2 >= r_in && d2 <= r_out) {
                            a = life_a / 2;
                            br = 60u; bg = 185u; bb = 255u;
                        }
                        if (a <= 0) continue;
                        di = ((size_t)py_y * dst_w + (unsigned)px_x) * 4u;
                        dst_rgba[di + 0u] = (uint8_t)((dst_rgba[di + 0u] * (255 - a) + br * a) / 255);
                        dst_rgba[di + 1u] = (uint8_t)((dst_rgba[di + 1u] * (255 - a) + bg * a) / 255);
                        dst_rgba[di + 2u] = (uint8_t)((dst_rgba[di + 2u] * (255 - a) + bb * a) / 255);
                    }
                }
            } else if (is_pre_guitar_stage(e->stage_name)) {
                int dir = (s->vx < 0) ? -1 : 1;
                int x_tail = sx - dir * 14;
                int y_mid = sy - 20;
                unsigned seed = (32u - s->ttl) * 5u + i * 11u;
                if (sx < -24 || sx >= (int)dst_w + 24) continue;
                draw_electric_seg(dst_rgba, dst_w, dst_h, x_tail, y_mid,
                                  sx, y_mid, seed);
                draw_filled_disc(dst_rgba, dst_w, dst_h, sx, y_mid, 5,
                                 95, 210, 255, 195);
                draw_filled_disc(dst_rgba, dst_w, dst_h, sx, y_mid, 2,
                                 225, 250, 255, 240);
                if (g_stxt.poof_blue_n > 0) {
                    blit_stxt_anim_centered(dst_rgba, dst_w, dst_h,
                                            g_stxt.poof_blue,
                                            g_stxt.poof_blue_n,
                                            (12u - s->ttl) + i, sx, y_mid,
                                            s->vx < 0 ? 1 : 0, 144u, 256u);
                }
            } else {
                int dir = (s->vx < 0) ? -1 : 1;
                int x_tail = sx - dir * 34;
                int y_mid = sy - 20;
                unsigned seed = (32u - s->ttl) * 5u + i * 11u;
                if (sx < -40 || sx >= (int)dst_w + 40) continue;
                draw_electric_seg(dst_rgba, dst_w, dst_h, x_tail, y_mid,
                                  sx, y_mid, seed);
                draw_electric_seg(dst_rgba, dst_w, dst_h, x_tail + dir * 6,
                                  y_mid - 3, sx - dir * 4, y_mid + 2, seed + 7u);
                if (g_stxt.logs_bluepoof_n > 0) {
                    blit_stxt_anim_centered(dst_rgba, dst_w, dst_h,
                                            g_stxt.logs_bluepoof,
                                            g_stxt.logs_bluepoof_n,
                                            (12u - s->ttl) + i, sx, y_mid,
                                            s->vx < 0 ? 1 : 0, 128u, 256u);
                } else if (g_stxt.poof_blue_n > 0) {
                    blit_stxt_anim_centered(dst_rgba, dst_w, dst_h,
                                            g_stxt.poof_blue,
                                            g_stxt.poof_blue_n,
                                            (12u - s->ttl) + i, sx, y_mid,
                                            s->vx < 0 ? 1 : 0, 128u, 256u);
                }
            }
        }

        for (i = 0; i < STAGE_POP_MAX; i++) {
            const stage_pop_fx *pop = &e->pops[i];
            int px, sx, sy, fsy;
            if (!pop->active) continue;
            px = (int)(pop->x / u);
            sx = px - (int)camera_x + screen_anchor_x;
            if (sx < -24 || sx >= (int)dst_w + 24) continue;
            sy = (int)(pop->y / u);
            if (sy < 24 || sy >= (int)dst_h) sy = base_y;
            fsy = lookup_floor_py(floor_py, floor_w, z_perspective, sx, px);
            if (fsy > 0 && (z_perspective || sy + 12 >= fsy))
                sy = fsy;
            draw_poof_orb(dst_rgba, dst_w, dst_h, sx, sy - 20,
                          (14u - pop->ttl) * 2u, 1);
        }

        for (i = 0; i < 24u; i++) {
            int bx, by;
            if (!g_stxt.bone_shards[i].active) continue;
            bx = (int)(g_stxt.bone_shards[i].x / (int32_t)u) - (int)camera_x + screen_anchor_x;
            by = (int)(g_stxt.bone_shards[i].y / (int32_t)u);
            if (bx < -12 || bx >= (int)dst_w + 12 || by < -12 || by >= (int)dst_h + 12)
                continue;
            if (g_stxt.thrown_n > 0) {
                blit_stxt_anim_centered(dst_rgba, dst_w, dst_h, g_stxt.thrown,
                                        g_stxt.thrown_n, g_stxt.bone_shards[i].spin,
                                        bx, by, (int)(i & 1u), 116u, 256u);
            } else {
                draw_rect(dst_rgba, dst_w, dst_h, bx - 1, by - 1, 3, 2, 232, 224, 198);
            }
        }
    }
}

void stage_enemies_render(const stage_enemies *e, const runway_hazards *haz,
                          uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                          uint32_t camera_x, int screen_anchor_x, int base_y) {
    stage_enemies_render_ex(e, haz, dst_rgba, dst_w, dst_h, camera_x,
                            screen_anchor_x, base_y, NULL, 0u, 0);
}

static int try_load_custom_hd_sprite(const char *dir, const char *prefix,
                                     unsigned idx, stxt_frame *out) {
    static const char *const k_exts[3] = {"png", "pam", "tga"};
    char path[512];
    unsigned e;
    if (!dir || !dir[0] || !prefix || !out) return -1;
    for (e = 0; e < 3u; e++) {
        uint8_t *px = NULL;
        unsigned w = 0, h = 0;
        snprintf(path, sizeof path, "%s/%s_%02u.%s", dir, prefix, idx, k_exts[e]);
        if (mb_image_load_rgba(path, &px, &w, &h) == 0 && px && w && h) {
            out->rgba = px;
            out->w = w;
            out->h = h;
            return 0;
        }
    }
    return -1;
}

int stage_enemies_prepare_hd_bones(mb_upscale_mode mode, const char *custom_dir) {
    struct {
        const stxt_frame *orig;
        unsigned count;
        stxt_frame *hd;
        const char *prefix;
        unsigned *hd_factor;
        int *ready;
    } groups[11] = {
        {g_stxt.bones_stand,  g_stxt.bones_stand_n,  g_stxt.bones_stand_hd,  "stand",  NULL,                       NULL},
        {g_stxt.bones_tall,   g_stxt.bones_tall_n,   g_stxt.bones_tall_hd,   "tall",   NULL,                       NULL},
        {g_stxt.bones_air,    g_stxt.bones_air_n,    g_stxt.bones_air_hd,    "air",    NULL,                       NULL},
        {g_stxt.bones_walk,   g_stxt.bones_walk_n,   g_stxt.bones_walk_hd,   "walk",   NULL,                       NULL},
        {g_stxt.thrown,       g_stxt.thrown_n,       g_stxt.thrown_hd,       "thrown", g_stxt.thrown_hd_factor,    &g_stxt.thrown_hd_ready},
        {g_stxt.gang,         g_stxt.gang_n,         g_stxt.gang_hd,         "gang",   g_stxt.gang_hd_factor,      &g_stxt.gang_hd_ready},
        {g_stxt.lord,         g_stxt.lord_n,         g_stxt.lord_hd,         "lord",   g_stxt.lord_hd_factor,      &g_stxt.lord_hd_ready},
        {g_stxt.valley_skel,  g_stxt.valley_skel_n,  g_stxt.valley_skel_hd,  "valley", g_stxt.valley_skel_hd_factor, &g_stxt.valley_skel_hd_ready},
        {g_stxt.zombies,      g_stxt.zombies_n,      g_stxt.zombies_hd,      "zombie", g_stxt.zombies_hd_factor,   &g_stxt.zombies_hd_ready},
        {g_stxt.laff_clown,   g_stxt.laff_clown_n,   g_stxt.laff_clown_hd,   "laff",   g_stxt.laff_clown_hd_factor, &g_stxt.laff_clown_hd_ready},
        {g_stxt.shadow,       g_stxt.shadow_n,       g_stxt.shadow_hd,       "shadow", g_stxt.shadow_hd_factor,    &g_stxt.shadow_hd_ready}
    };
    unsigned g, i;
    unsigned group_count = (unsigned)(sizeof groups / sizeof groups[0]);
    unsigned factor = mb_upscale_mode_factor(mode);
    if (mode == MB_UPSCALE_NEAREST) factor = 3u;
    g_stxt.bones_hd_custom_count = 0;
    g_stxt.thrown_hd_ready = 0;
    g_stxt.gang_hd_ready = 0;
    g_stxt.lord_hd_ready = 0;
    g_stxt.valley_skel_hd_ready = 0;
    g_stxt.zombies_hd_ready = 0;
    g_stxt.laff_clown_hd_ready = 0;
    g_stxt.shadow_hd_ready = 0;
    for (g = 0; g < group_count; g++) {
        for (i = 0; i < groups[g].count; i++) {
            const stxt_frame *src = &groups[g].orig[i];
            stxt_frame *dst = &groups[g].hd[i];
            free(dst->rgba);
            dst->rgba = NULL;
            dst->w = 0;
            dst->h = 0;
            if (try_load_custom_hd_sprite(custom_dir, groups[g].prefix, i, dst) == 0) {
                g_stxt.bones_hd_custom_count++;
                if (groups[g].hd_factor) groups[g].hd_factor[i] = 3u;
                if (groups[g].ready) *groups[g].ready = 1;
            } else if (src->rgba && src->w && src->h) {
                size_t sz = (size_t)src->w * factor * (size_t)src->h * factor * 4u;
                uint8_t *px = (uint8_t *)malloc(sz);
                if (px && mb_upscale_rgba(src->rgba, src->w, src->h, mode,
                                          factor, px, sz) == 0) {
                    dst->rgba = px;
                    dst->w = src->w * factor;
                    dst->h = src->h * factor;
                    if (groups[g].hd_factor) groups[g].hd_factor[i] = factor;
                    if (groups[g].ready) *groups[g].ready = 1;
                } else {
                    free(px);
                }
            }
        }
    }
    g_stxt.bones_hd_ready = 1;
    return 0;
}

void stage_enemies_set_hd_overlay(int enable) {
    g_stxt.bones_suppress_1x = enable ? 1 : 0;
}

void stage_enemies_reset_bones_pose(void) {
    g_stxt.last_pose.valid = 0;
    g_stxt.hd_ground_valid = 0;
    g_stxt.hd_shadow_n = 0;
    g_stxt.hd_thrown_n = 0;
    g_stxt.hd_enemy_n = 0;
    g_stxt.bones_hurt_timer = 0;
    g_stxt.bones_invuln_timer = 0;
    g_stxt.bones_dead_flag = 0;
}

void stage_enemies_set_bones_fx(int hurt_timer, int invuln_timer, int dead) {
    g_stxt.bones_hurt_timer = hurt_timer > 0 ? hurt_timer : 0;
    g_stxt.bones_invuln_timer = invuln_timer > 0 ? invuln_timer : 0;
    g_stxt.bones_dead_flag = dead ? 1 : 0;
}

void stage_enemies_spawn_bone_burst(stage_enemies *e, uint32_t world_x,
                                    uint32_t world_y, int count) {
    unsigned u = (e && e->units_per_pixel) ? e->units_per_pixel : 2341u;
    int k, n = count;
    unsigned seed = (unsigned)(world_x ^ world_y);
    if (n < 1) n = 1;
    if (n > 8) n = 8;
    for (k = 0; k < n; k++) {
        unsigned i, slot = 0u;
        int dir;
        for (i = 0; i < 24u; i++) {
            if (!g_stxt.bone_shards[i].active) {
                slot = i;
                break;
            }
            if (g_stxt.bone_shards[i].ttl < g_stxt.bone_shards[slot].ttl)
                slot = i;
        }
        dir = ((k & 1) ? 1 : -1) * (1 + (k / 2));
        g_stxt.bone_shards[slot].active = 1;
        g_stxt.bone_shards[slot].x = (int32_t)world_x;
        g_stxt.bone_shards[slot].y = (int32_t)(world_y > 16u * u ? world_y - 16u * u : world_y);
        g_stxt.bone_shards[slot].vx = dir * (int32_t)u;
        g_stxt.bone_shards[slot].vy = -(int32_t)((3 + (k % 3)) * (int)u);
        g_stxt.bone_shards[slot].ttl = 22u + (unsigned)(k & 3) * 3u;
        g_stxt.bone_shards[slot].spin = seed + (unsigned)k;
    }
}

void stage_enemies_set_bones_ground_y(int ground_y, const char *stage_name) {
    g_stxt.hd_ground_y = ground_y;
    g_stxt.hd_ground_valid = (ground_y >= 48 && ground_y < 224) ? 1 : 0;
    if (stage_name && stage_name[0])
        g_stxt.hd_night_stage = is_night_stage_name(stage_name);
}

int stage_enemies_last_bones_pose(stage_bones_pose *out) {
    if (!out) return -1;
    *out = g_stxt.last_pose;
    return g_stxt.last_pose.valid ? 0 : -1;
}

int stage_enemies_hd_custom_count(void) {
    return g_stxt.bones_hd_custom_count;
}

void stage_enemies_redraw_suppressed_1x(uint8_t *dst_rgba, unsigned dst_w,
                                        unsigned dst_h) {
    unsigned order[32], n, a, b, s;
    if (!dst_rgba || !dst_w || !dst_h) return;
    n = g_stxt.hd_enemy_n;
    if (n > 32u) n = 32u;
    /* Paint the HD-capable skeletons the overlay hid from the 1x surface,
     * back-to-front, exactly where the render recorded them. */
    for (a = 0; a < n; a++) order[a] = a;
    for (a = 1u; a < n; a++) {
        unsigned cv = order[a];
        int ck = g_stxt.hd_enemies[cv].sy;
        b = a;
        while (b > 0u && g_stxt.hd_enemies[order[b - 1u]].sy > ck) {
            order[b] = order[b - 1u];
            b--;
        }
        order[b] = cv;
    }
    for (a = 0; a < n; a++) {
        unsigned idx = order[a];
        int kind = g_stxt.hd_enemies[idx].kind;
        const stxt_frame *frames = NULL;
        unsigned count = 0u;
        int ready = 0;
        if (kind == STXT_HD_ENEMY_GANG) {
            frames = g_stxt.gang; count = g_stxt.gang_n;
            ready = g_stxt.gang_hd_ready;
        } else if (kind == STXT_HD_ENEMY_LORD) {
            frames = g_stxt.lord; count = g_stxt.lord_n;
            ready = g_stxt.lord_hd_ready;
        } else if (kind == STXT_HD_ENEMY_VALLEY) {
            frames = g_stxt.valley_skel; count = g_stxt.valley_skel_n;
            ready = g_stxt.valley_skel_hd_ready;
        } else if (kind == STXT_HD_ENEMY_ZOMBIE) {
            frames = g_stxt.zombies; count = g_stxt.zombies_n;
            ready = g_stxt.zombies_hd_ready;
        } else if (kind == STXT_HD_ENEMY_LAFF) {
            frames = g_stxt.laff_clown; count = g_stxt.laff_clown_n;
            ready = g_stxt.laff_clown_hd_ready;
        } else if (kind == STXT_HD_ENEMY_SHADOW) {
            frames = g_stxt.shadow; count = g_stxt.shadow_n;
            ready = g_stxt.shadow_hd_ready;
        }
        /* Only re-blit slots the render actually suppressed (hd ready). */
        if (!ready || !frames || count == 0u) continue;
        blit_stxt_anim_foot(dst_rgba, dst_w, dst_h, frames, count,
                            g_stxt.hd_enemies[idx].fidx,
                            g_stxt.hd_enemies[idx].sx,
                            g_stxt.hd_enemies[idx].sy,
                            g_stxt.hd_enemies[idx].flip,
                            g_stxt.hd_enemies[idx].scale_num,
                            g_stxt.hd_enemies[idx].scale_den);
    }
    if (g_stxt.thrown_hd_ready && g_stxt.thrown_n > 0u) {
        for (s = 0; s < g_stxt.hd_thrown_n; s++) {
            blit_stxt_anim_centered(dst_rgba, dst_w, dst_h, g_stxt.thrown,
                                    g_stxt.thrown_n, g_stxt.hd_thrown[s].anim,
                                    g_stxt.hd_thrown[s].cx,
                                    g_stxt.hd_thrown[s].cy, 0,
                                    g_stxt.hd_thrown[s].scale_num,
                                    g_stxt.hd_thrown[s].scale_den);
        }
    }
}

static void draw_hd_ellipse_shadow(uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                                   int cx, int cy, int rx, int ry, int max_alpha) {
    int y, x, x0, x1, y0, y1;
    if (!dst_rgba || rx <= 1 || ry <= 1 || max_alpha <= 0) return;
    x0 = cx - rx; if (x0 < 0) x0 = 0;
    x1 = cx + rx; if (x1 >= (int)dst_w) x1 = (int)dst_w - 1;
    y0 = cy - ry; if (y0 < 0) y0 = 0;
    y1 = cy + ry; if (y1 >= (int)dst_h) y1 = (int)dst_h - 1;
    for (y = y0; y <= y1; y++) {
        float ny = (float)(y - cy) / (float)ry;
        float ny2 = ny * ny;
        if (ny2 >= 1.0f) continue;
        for (x = x0; x <= x1; x++) {
            float nx = (float)(x - cx) / (float)rx;
            float d2 = nx * nx + ny2;
            if (d2 < 1.0f) {
                float w = (1.0f - d2) * (1.0f - d2);
                int a = (int)((float)max_alpha * w + 0.5f);
                if (a > 0) {
                    size_t di = ((size_t)y * dst_w + (unsigned)x) * 4u;
                    int inv = 255 - a;
                    dst_rgba[di + 0u] = (uint8_t)((dst_rgba[di + 0u] * inv + 4 * a) / 255);
                    dst_rgba[di + 1u] = (uint8_t)((dst_rgba[di + 1u] * inv + 8 * a) / 255);
                    dst_rgba[di + 2u] = (uint8_t)((dst_rgba[di + 2u] * inv + 14 * a) / 255);
                }
            }
        }
    }
}

/* Harmonize warm studio-lit HD ivory sprites gently into cool moonlit night
 * stages: keep bone highlights luminous while shifting dark/mid creases blue-slate. */
static const uint8_t *apply_night_grade(const stxt_frame *src, int night,
                                        uint8_t *scratch, size_t scratch_bytes) {
    size_t n = (size_t)src->w * src->h;
    size_t i;
    if (!night || !src || !src->rgba || n * 4u > scratch_bytes)
        return src ? src->rgba : NULL;
    for (i = 0; i < n; i++) {
        size_t p = i * 4u;
        int r = src->rgba[p + 0u];
        int g = src->rgba[p + 1u];
        int b = src->rgba[p + 2u];
        uint8_t a = src->rgba[p + 3u];
        if (a > 0u) {
            int lum = (r * 30 + g * 59 + b * 11) / 100;
            if (lum < 175) {
                int sh = (175 - lum) * 255 / 175;
                r = r - (sh * 14) / 255;
                g = g - (sh * 4) / 255;
                b = b + (sh * 15) / 255;
            } else {
                r = (r * 251) / 255;
                g = (g * 253) / 255;
            }
            if (r < 0) r = 0; if (r > 255) r = 255;
            if (g < 0) g = 0; if (g > 255) g = 255;
            if (b < 0) b = 0; if (b > 255) b = 255;
        }
        scratch[p + 0u] = (uint8_t)r;
        scratch[p + 1u] = (uint8_t)g;
        scratch[p + 2u] = (uint8_t)b;
        scratch[p + 3u] = a;
    }
    return scratch;
}

static void draw_hd_enemy_slot(uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                               unsigned host_scale, unsigned slot_idx,
                               uint8_t *night_scratch, size_t night_bytes) {
    const stxt_frame *orig = NULL, *hd = NULL;
    unsigned n = 0u, fac = 3u, id;
    int kind;
    unsigned num, den, out_w, out_h;
    int dx, dy, fx, fy;
    if (slot_idx >= g_stxt.hd_enemy_n) return;
    kind = g_stxt.hd_enemies[slot_idx].kind;
    if (kind == STXT_HD_ENEMY_GANG && g_stxt.gang_n > 0u) {
        n = g_stxt.gang_n;
        id = g_stxt.hd_enemies[slot_idx].fidx % n;
        orig = &g_stxt.gang[id];
        hd = &g_stxt.gang_hd[id];
        if (g_stxt.gang_hd_factor[id]) fac = g_stxt.gang_hd_factor[id];
    } else if (kind == STXT_HD_ENEMY_LORD && g_stxt.lord_n > 0u) {
        n = g_stxt.lord_n;
        id = g_stxt.hd_enemies[slot_idx].fidx % n;
        orig = &g_stxt.lord[id];
        hd = &g_stxt.lord_hd[id];
        if (g_stxt.lord_hd_factor[id]) fac = g_stxt.lord_hd_factor[id];
    } else if (kind == STXT_HD_ENEMY_VALLEY && g_stxt.valley_skel_n > 0u) {
        n = g_stxt.valley_skel_n;
        id = g_stxt.hd_enemies[slot_idx].fidx % n;
        orig = &g_stxt.valley_skel[id];
        hd = &g_stxt.valley_skel_hd[id];
        if (g_stxt.valley_skel_hd_factor[id]) fac = g_stxt.valley_skel_hd_factor[id];
    } else if (kind == STXT_HD_ENEMY_ZOMBIE && g_stxt.zombies_n > 0u) {
        n = g_stxt.zombies_n;
        id = g_stxt.hd_enemies[slot_idx].fidx % n;
        orig = &g_stxt.zombies[id];
        hd = &g_stxt.zombies_hd[id];
        if (g_stxt.zombies_hd_factor[id]) fac = g_stxt.zombies_hd_factor[id];
    } else if (kind == STXT_HD_ENEMY_LAFF && g_stxt.laff_clown_n > 0u) {
        n = g_stxt.laff_clown_n;
        id = g_stxt.hd_enemies[slot_idx].fidx % n;
        orig = &g_stxt.laff_clown[id];
        hd = &g_stxt.laff_clown_hd[id];
        if (g_stxt.laff_clown_hd_factor[id]) fac = g_stxt.laff_clown_hd_factor[id];
    } else if (kind == STXT_HD_ENEMY_SHADOW && g_stxt.shadow_n > 0u) {
        n = g_stxt.shadow_n;
        id = g_stxt.hd_enemies[slot_idx].fidx % n;
        orig = &g_stxt.shadow[id];
        hd = &g_stxt.shadow_hd[id];
        if (g_stxt.shadow_hd_factor[id]) fac = g_stxt.shadow_hd_factor[id];
    }
    if (!hd || !hd->rgba || !hd->w || !hd->h) return;
    num = g_stxt.hd_enemies[slot_idx].scale_num;
    den = g_stxt.hd_enemies[slot_idx].scale_den;
    if (den == 0u) { num = 256u; den = 256u; }
    if (kind != STXT_HD_ENEMY_GANG && orig && orig->w && orig->h) {
        out_w = (orig->w * num * host_scale + den / 2u) / den;
        out_h = (orig->h * num * host_scale + den / 2u) / den;
    } else {
        out_w = (hd->w * num * host_scale + (fac * den) / 2u) / (fac * den);
        out_h = (hd->h * num * host_scale + (fac * den) / 2u) / (fac * den);
    }
    if (out_w < 2u * host_scale) out_w = 2u * host_scale;
    if (out_h < 2u * host_scale) out_h = 2u * host_scale;
    fx = g_stxt.hd_enemies[slot_idx].sx * (int)host_scale;
    fy = g_stxt.hd_enemies[slot_idx].sy * (int)host_scale;
    dx = fx - (int)(out_w / 2u);
    dy = fy - (int)out_h;
    {
        /* Night-grade HD enemies like Bones/thrown do on moonlit stages. */
        const uint8_t *erg = apply_night_grade(hd, g_stxt.hd_night_stage,
                                               night_scratch, night_bytes);
        (void)mb_upscale_blit_alpha(dst_rgba, dst_w, dst_h, erg, hd->w, hd->h,
                                    dx, dy, out_w, out_h,
                                    g_stxt.hd_enemies[slot_idx].flip);
    }
}

int stage_enemies_draw_bones_hd(uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                                unsigned host_scale) {
    static uint8_t night_scratch[512u * 512u * 4u];
    unsigned enemy_order[32];
    const stxt_frame *orig = NULL, *hd = NULL;
    const uint8_t *bones_rgba = NULL;
    unsigned out_w, out_h, s_idx, e_idx, k;
    int foot_x, foot_y, ground_y, flip, dx, dy;
    if (!dst_rgba || !dst_w || !dst_h || !host_scale || !g_stxt.last_pose.valid)
        return -1;

    /* 1. Grounded contact shadows for enemies and Mr. Bones landing cue. */
    for (s_idx = 0; s_idx < g_stxt.hd_shadow_n; s_idx++) {
        int scx = g_stxt.hd_shadows[s_idx].sx * (int)host_scale;
        int scy = g_stxt.hd_shadows[s_idx].sy * (int)host_scale + (int)host_scale;
        int srx = g_stxt.hd_shadows[s_idx].rx * (int)host_scale;
        int sry = g_stxt.hd_shadows[s_idx].ry * (int)host_scale;
        draw_hd_ellipse_shadow(dst_rgba, dst_w, dst_h, scx, scy, srx, sry,
                               g_stxt.hd_shadows[s_idx].alpha);
    }

    select_bones_frame(g_stxt.last_pose.tall, g_stxt.last_pose.airborne,
                       g_stxt.last_pose.anim_frame, &orig, &hd);
    if (!orig || !hd || !hd->rgba || !hd->w || !hd->h) return -1;
    {
        unsigned bfac = (hd != orig && g_stxt.bones_hd_custom_count > 0)
                            ? 3u
                            : (hd != orig && orig->w > 0 ? (hd->w / orig->w) : 1u);
        unsigned num = g_stxt.last_pose.scale_den ? g_stxt.last_pose.scale_num : 256u;
        unsigned den = g_stxt.last_pose.scale_den ? g_stxt.last_pose.scale_den : 256u;
        if (bfac == 0u) bfac = 1u;
        out_w = (hd->w * num * host_scale + (bfac * den) / 2u) / (bfac * den);
        out_h = (hd->h * num * host_scale + (bfac * den) / 2u) / (bfac * den);
        if (out_w < 2u * host_scale) out_w = 2u * host_scale;
        if (out_h < 2u * host_scale) out_h = 2u * host_scale;
    }
    flip = (g_stxt.last_pose.facing < 0) ? 1 : 0;
    foot_x = g_stxt.last_pose.foot_x * (int)host_scale;
    foot_y = g_stxt.last_pose.foot_y * (int)host_scale;
    ground_y = g_stxt.hd_ground_valid ? g_stxt.hd_ground_y : g_stxt.last_pose.foot_y;
    {
        int air_px = ground_y - g_stxt.last_pose.foot_y;
        int sh_num = (int)(g_stxt.last_pose.scale_den ? g_stxt.last_pose.scale_num : 256u);
        int sh_den = (int)(g_stxt.last_pose.scale_den ? g_stxt.last_pose.scale_den : 256u);
        int sh_x = foot_x - (flip ? -(int)(3u * host_scale) : (int)(3u * host_scale));
        int sh_y = ground_y * (int)host_scale + (int)host_scale;
        int falloff = 256;
        int rx, ry, al;
        if (air_px < 0) {
            sh_y = foot_y + (int)host_scale;
            air_px = 0;
        } else if (air_px > 0) {
            falloff = (256 * 56) / (56 + air_px);
            if (falloff < 96) falloff = 96;
        }
        rx = (int)((14 * (int)host_scale * sh_num * falloff) / (sh_den * 256));
        ry = (int)((4 * (int)host_scale * sh_num * falloff) / (sh_den * 256));
        al = (138 * falloff) / 256;
        if (rx < 4) rx = 4;
        if (ry < 2) ry = 2;
        draw_hd_ellipse_shadow(dst_rgba, dst_w, dst_h, sh_x, sh_y, rx, ry, al);
    }

    /* 2. Depth-sort HD skeleton opponents alongside Mr. Bones by foot_y. */
    for (e_idx = 0; e_idx < g_stxt.hd_enemy_n; e_idx++) enemy_order[e_idx] = e_idx;
    for (e_idx = 1; e_idx < g_stxt.hd_enemy_n; e_idx++) {
        unsigned cur = enemy_order[e_idx];
        int cur_y = g_stxt.hd_enemies[cur].sy;
        k = e_idx;
        while (k > 0u && g_stxt.hd_enemies[enemy_order[k - 1u]].sy > cur_y) {
            enemy_order[k] = enemy_order[k - 1u];
            k--;
        }
        enemy_order[k] = cur;
    }
    for (e_idx = 0; e_idx < g_stxt.hd_enemy_n; e_idx++) {
        unsigned ord = enemy_order[e_idx];
        if (g_stxt.hd_enemies[ord].sy > g_stxt.last_pose.foot_y) break;
        draw_hd_enemy_slot(dst_rgba, dst_w, dst_h, host_scale, ord,
                           night_scratch, sizeof night_scratch);
    }

    dx = foot_x - (int)(out_w / 2u);
    dy = foot_y - (int)out_h;
    bones_rgba = apply_night_grade(hd, g_stxt.hd_night_stage,
                                   night_scratch, sizeof night_scratch);
    if (mb_upscale_blit_alpha(dst_rgba, dst_w, dst_h, bones_rgba, hd->w, hd->h,
                              dx, dy, out_w, out_h, flip) != 0)
        return -1;

    for (; e_idx < g_stxt.hd_enemy_n; e_idx++) {
        draw_hd_enemy_slot(dst_rgba, dst_w, dst_h, host_scale,
                           enemy_order[e_idx], night_scratch,
                           sizeof night_scratch);
    }

    /* 2. Composite crisp 3D HD spinning bones on top (bypassing 1x xBR distortion). */
    if (g_stxt.thrown_hd_ready && g_stxt.thrown_n > 0u) {
        for (s_idx = 0; s_idx < g_stxt.hd_thrown_n; s_idx++) {
            unsigned tid = g_stxt.hd_thrown[s_idx].anim % g_stxt.thrown_n;
            const stxt_frame *th = &g_stxt.thrown_hd[tid];
            unsigned num = g_stxt.hd_thrown[s_idx].scale_num;
            unsigned den = g_stxt.hd_thrown[s_idx].scale_den;
            unsigned tw, thh;
            int tx, ty;
            const uint8_t *trgb;
            unsigned tfac = g_stxt.thrown_hd_factor[tid] ? g_stxt.thrown_hd_factor[tid] : 3u;
            if (!th->rgba || !th->w || !th->h) continue;
            if (den == 0u) { num = 256u; den = 256u; }
            tw = (th->w * num * host_scale + (tfac * den) / 2u) / (tfac * den);
            thh = (th->h * num * host_scale + (tfac * den) / 2u) / (tfac * den);
            if (tw < 4u * host_scale) tw = 4u * host_scale;
            if (thh < 4u * host_scale) thh = 4u * host_scale;
            tx = g_stxt.hd_thrown[s_idx].cx * (int)host_scale - (int)(tw / 2u);
            ty = g_stxt.hd_thrown[s_idx].cy * (int)host_scale - (int)(thh / 2u);
            if (g_stxt.hd_ground_valid && g_stxt.hd_thrown[s_idx].cy < g_stxt.hd_ground_y) {
                int dh = g_stxt.hd_ground_y - g_stxt.hd_thrown[s_idx].cy;
                if (dh < 64) {
                    int af = (54 * (64 - dh)) / 64;
                    draw_hd_ellipse_shadow(dst_rgba, dst_w, dst_h,
                                           g_stxt.hd_thrown[s_idx].cx * (int)host_scale,
                                           g_stxt.hd_ground_y * (int)host_scale + (int)host_scale,
                                           (int)(7u * host_scale), (int)(2u * host_scale), af);
                }
            }
            trgb = apply_night_grade(th, g_stxt.hd_night_stage,
                                     night_scratch, sizeof night_scratch);
            (void)mb_upscale_blit_alpha(dst_rgba, dst_w, dst_h, trgb, th->w, th->h,
                                        tx, ty, tw, thh, 0);
        }
    }
    return 0;
}

void stage_enemies_draw_barrier(uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                                int gx, int y0, int y1, int locked, unsigned frames) {
    int by, bx, pulse;
    if (!dst_rgba || !dst_w || !dst_h || y1 <= y0) return;
    if (y0 < 0) y0 = 0;
    if (y1 > (int)dst_h) y1 = (int)dst_h;
    pulse = (int)((frames >> 3) & 1u);
    /* Slender translucent portal seam with small crystalline top/bottom anchors. */
    for (by = y0; by < y1; by++) {
        int cap = (by < y0 + 4 || by >= y1 - 4);
        int shim = (((by + (int)(frames >> 1)) & 7) == 0);
        for (bx = gx - (cap ? 2 : 1); bx <= gx + (cap ? 2 : 1); bx++) {
            int dist = (bx < gx) ? (gx - bx) : (bx > gx ? (bx - gx) : 0);
            int a;
            uint8_t er, eg, eb;
            uint8_t *px;
            if (bx < 0 || bx >= (int)dst_w) continue;
            px = dst_rgba + ((size_t)by * dst_w + (size_t)bx) * 4u;
            if (locked) {
                a = cap ? (dist == 0 ? 190 : 95) : (dist == 0 ? (shim ? 82 : 52) : 22);
                er = cap ? 240u : 225u;
                eg = (uint8_t)(pulse ? 135u : 105u);
                eb = 65u;
            } else {
                a = cap ? (dist == 0 ? 195 : 100) : (dist == 0 ? (shim ? 88 : 54) : 24);
                er = 110u;
                eg = 240u;
                eb = 255u;
            }
            px[0] = (uint8_t)((px[0] * (255 - a) + er * a) / 255);
            px[1] = (uint8_t)((px[1] * (255 - a) + eg * a) / 255);
            px[2] = (uint8_t)((px[2] * (255 - a) + eb * a) / 255);
            px[3] = 255u;
        }
    }
}

typedef struct {
    unsigned x0, x1;
    uint8_t top_y;
    uint8_t shard_idx;
} glass_shard_seg;

static const glass_shard_seg k_glass_shards[] = {
    {    0u,  148u, 158u, 3u },
    {  172u,  316u, 142u, 3u },
    {  340u,  488u, 128u, 3u },
    {  512u,  660u, 146u, 3u },
    {  684u,  832u, 130u, 3u },
    {  856u, 1004u, 144u, 3u },
    { 1028u, 1280u, 136u, 3u }
};

uint8_t stage_enemies_glass_floor_at(unsigned world_x, unsigned strip_w) {
    unsigned i;
    const unsigned n = (unsigned)(sizeof k_glass_shards / sizeof k_glass_shards[0]);
    if (strip_w > 0u && world_x >= strip_w) world_x = strip_w - 1u;
    for (i = 0; i < n; i++) {
        if (world_x >= k_glass_shards[i].x0 && world_x < k_glass_shards[i].x1)
            return k_glass_shards[i].top_y;
    }
    return 0xFFu;
}

void stage_enemies_draw_glass_platforms(uint8_t *dst_rgba, unsigned dst_w,
                                        unsigned dst_h, unsigned src_x,
                                        unsigned strip_w, unsigned finish_px,
                                        unsigned frames) {
    unsigned i;
    const unsigned n = (unsigned)(sizeof k_glass_shards / sizeof k_glass_shards[0]);
    if (!dst_rgba || !dst_w || !dst_h) return;
    for (i = 0; i < n; i++) {
        int cx = (int)((k_glass_shards[i].x0 + k_glass_shards[i].x1) / 2u) - (int)src_x;
        int w_seg = (int)(k_glass_shards[i].x1 - k_glass_shards[i].x0);
        int top_y = (int)k_glass_shards[i].top_y;
        if (cx + w_seg < -16 || cx - w_seg > (int)dst_w + 16) continue;
        if (g_stxt.glass_plat_n > 0) {
            unsigned si = k_glass_shards[i].shard_idx % g_stxt.glass_plat_n;
            const stxt_frame *f = &g_stxt.glass_plat[si];
            if (f->rgba && f->w && f->h) {
                runway_compose_blit_sprite_scaled(
                    dst_rgba, dst_w, dst_h, f->rgba, f->w, f->h,
                    cx - (w_seg + 2) / 2, top_y - 3,
                    (unsigned)(w_seg + 2), f->h, (i & 1u) ? 1 : 0);
            }
        } else {
            draw_rect(dst_rgba, dst_w, dst_h, cx - w_seg / 2, top_y,
                      w_seg, 6, 36, 150, 118);
        }
    }
    if (finish_px > 0u && finish_px <= strip_w) {
        int px = (int)finish_px - (int)src_x;
        int py = (int)stage_enemies_glass_floor_at(finish_px, strip_w);
        int bob = (int)(((frames >> 3) & 3u) == 2u ? -2 : (((frames >> 3) & 1u) ? -1 : 0));
        if (px >= -40 && px < (int)dst_w + 40) {
            if (g_stxt.glass_portal_n > 0) {
                blit_stxt_foot(dst_rgba, dst_w, dst_h, &g_stxt.glass_portal[0],
                               px + 12, py + 4 + bob, 0, 224u, 256u);
            } else {
                stage_enemies_draw_barrier(dst_rgba, dst_w, dst_h, px, py - 56, py, 0, frames);
            }
        }
    }
}

void stage_enemies_draw_wall_shadow(uint8_t *dst_rgba, unsigned dst_w,
                                    unsigned dst_h, int foot_x, int foot_y,
                                    int facing, int tall, int airborne,
                                    unsigned anim_frame) {
    const stxt_frame *orig = NULL, *hd = NULL;
    int flip, out_w, out_h, dx0, dy0, x, y;
    if (!dst_rgba || !dst_w || !dst_h) return;
    select_bones_frame(tall, airborne, anim_frame, &orig, &hd);
    if (!orig || !orig->rgba || !orig->w || !orig->h) return;
    flip = (facing < 0) ? 1 : 0;
    out_w = (int)((orig->w * 320u) / 256u);
    out_h = (int)((orig->h * 340u) / 256u);
    if (out_w < 4 || out_h < 4) return;
    dx0 = foot_x - 10 - out_w / 2;
    dy0 = foot_y - 4 - out_h;
    for (y = 0; y < out_h; y++) {
        int dy = dy0 + y;
        unsigned sy;
        if (dy < 0 || (unsigned)dy >= dst_h) continue;
        sy = ((unsigned)y * orig->h) / (unsigned)out_h;
        for (x = 0; x < out_w; x++) {
            int dx = dx0 + x;
            unsigned sx, rx;
            const uint8_t *sp;
            uint8_t *dp;
            if (dx < 0 || (unsigned)dx >= dst_w) continue;
            sx = ((unsigned)x * orig->w) / (unsigned)out_w;
            rx = flip ? (orig->w - 1u - sx) : sx;
            sp = orig->rgba + ((size_t)sy * orig->w + rx) * 4u;
            if (sp[3] < 96u) continue;
            dp = dst_rgba + ((size_t)(unsigned)dy * dst_w + (unsigned)dx) * 4u;
            dp[0] = (uint8_t)((dp[0] * 108u) / 255u);
            dp[1] = (uint8_t)((dp[1] * 98u) / 255u);
            dp[2] = (uint8_t)((dp[2] * 86u) / 255u);
        }
    }
}

static void draw_filled_disc(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                             int cx, int cy, int r, uint8_t cr, uint8_t cg,
                             uint8_t cb, int alpha) {
    int dx, dy, r2 = r * r;
    for (dy = -r; dy <= r; dy++) {
        for (dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r2)
                blend_px(dst, dst_w, dst_h, cx + dx, cy + dy, cr, cg, cb, alpha);
        }
    }
}

static void draw_thick_line(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                            int x0, int y0, int x1, int y1,
                            uint8_t r, uint8_t g, uint8_t b, int alpha) {
    int dx = x1 - x0, dy = y1 - y0;
    int steps = (dx < 0 ? -dx : dx);
    int ady = (dy < 0 ? -dy : dy);
    int s;
    if (ady > steps) steps = ady;
    if (steps < 1) steps = 1;
    for (s = 0; s <= steps; s++) {
        int px = x0 + (dx * s) / steps;
        int py = y0 + (dy * s) / steps;
        blend_px(dst, dst_w, dst_h, px, py, r, g, b, alpha);
        blend_px(dst, dst_w, dst_h, px + 1, py, r, g, b, alpha / 2);
        blend_px(dst, dst_w, dst_h, px, py + 1, r, g, b, alpha / 2);
    }
}

void stage_enemies_draw_rhythm_stage_ex(uint8_t *dst_rgba, unsigned dst_w,
                                        unsigned dst_h, const char *stage_name,
                                        unsigned frames, unsigned combo,
                                        int last_grade, unsigned grade_timer,
                                        unsigned expected_kind,
                                        unsigned hits, unsigned hits_need) {
    int is_guitar = (stage_name && !strcmp(stage_name, "GUITAR"));
    int is_drum = (stage_name && !strcmp(stage_name, "DRUM"));
    if (!dst_rgba || !dst_w || !dst_h) return;

    if (is_guitar) {
        const stxt_frame *blue_c = (g_stxt.guitar_crowd_blue_n > 0)
                                       ? &g_stxt.guitar_crowd_blue[(frames >> 4) % g_stxt.guitar_crowd_blue_n]
                                       : NULL;
        const stxt_frame *crowd_ref = (blue_c && blue_c->rgba) ? blue_c
                                                               : ((g_stxt.guitar_crowd_red_n > 0)
                                                                      ? &g_stxt.guitar_crowd_red[(frames >> 3) % g_stxt.guitar_crowd_red_n]
                                                                      : NULL);
        int strum_bob = (grade_timer > 0 && last_grade > 0) ? -2 : 0;
        unsigned g_idx = ((frames >> 2) + combo) & 1u;
        int split_x = (hits_need > 0u) ? (int)((hits * dst_w) / hits_need) : 0;
        if (grade_timer > 0 && last_grade < 0)
            split_x = (split_x > (int)dst_w / 3) ? (split_x - (int)dst_w / 3) : 0;
        if (split_x > (int)dst_w) split_x = (int)dst_w;

        /* Stage speaker towers framing the concert wings behind the stage. */
        draw_rect(dst_rgba, dst_w, dst_h, 6, 86, 22, 68, 28, 30, 38);
        draw_rect(dst_rgba, dst_w, dst_h, (int)dst_w - 28, 86, 22, 68, 28, 30, 38);
        draw_filled_disc(dst_rgba, dst_w, dst_h, 17, 102, 7, 18, 20, 26, 220);
        draw_filled_disc(dst_rgba, dst_w, dst_h, 17, 126, 8, 18, 20, 26, 220);
        draw_filled_disc(dst_rgba, dst_w, dst_h, (int)dst_w - 17, 102, 7, 18, 20, 26, 220);
        draw_filled_disc(dst_rgba, dst_w, dst_h, (int)dst_w - 17, 126, 8, 18, 20, 26, 220);

        /* Draw audience BEHIND Mr. Bones, converting crimson fans to blue left-to-right. */
        if (crowd_ref && crowd_ref->rgba) {
            unsigned y, x;
            for (y = 0; y < crowd_ref->h; y++) {
                int dy = 108 + (int)y;
                if (dy < 0 || (unsigned)dy >= dst_h) continue;
                for (x = 0; x < dst_w; x++) {
                    unsigned sx_f = x % crowd_ref->w;
                    const uint8_t *sp = crowd_ref->rgba + ((size_t)y * crowd_ref->w + sx_f) * 4u;
                    uint8_t cr, cg, cb;
                    if (sp[3] < 64u) continue;
                    if ((int)x < split_x) {
                        cr = (uint8_t)((sp[0] * 3u) / 4u);
                        cg = (uint8_t)(sp[1] > 180u ? 225u : (sp[1] + 24u));
                        cb = (uint8_t)(sp[2] > 200u ? 255u : (sp[2] + 50u));
                    } else {
                        cr = (uint8_t)(sp[0] > 180u ? 245u : (sp[0] + 60u));
                        cg = (uint8_t)(sp[1] / 3u);
                        cb = (uint8_t)(sp[2] / 3u);
                    }
                    blend_px(dst_rgba, dst_w, dst_h, (int)x, dy, cr, cg, cb, sp[3]);
                }
            }
        }
        stage_enemies_set_bones_ground_y(150, stage_name);
        stage_enemies_draw_bones(dst_rgba, dst_w, dst_h, (int)(dst_w / 2u),
                                 150 + strum_bob, 1, 0, 0, 0u);
        if (g_stxt.guitar_inst_n > 0) {
            const stxt_frame *gi = &g_stxt.guitar_inst[g_idx % g_stxt.guitar_inst_n];
            if (gi->rgba && gi->w && gi->h) {
                runway_compose_blit_sprite_scaled(
                    dst_rgba, dst_w, dst_h, gi->rgba, gi->w, gi->h,
                    (int)(dst_w / 2u) - 20, 114 + strum_bob,
                    48u, 18u, 0);
            }
        }
        if (grade_timer > 0 && last_grade > 0) {
            draw_electric_seg(dst_rgba, dst_w, dst_h, (int)(dst_w / 2u) + 16,
                              122 + strum_bob, (int)(dst_w / 2u) + 54,
                              108 + strum_bob, frames);
        }
    } else if (is_drum) {
        static const int k_pad_x[4] = { 122, 152, 166, 198 };
        static const int k_pad_y[4] = { 174, 156, 188, 174 };
        int facing = (expected_kind == 0u || expected_kind == 1u) ? -1 : 1;
        int dx_off = (expected_kind == 0u) ? -8 : (expected_kind == 3u ? 8 : 0);
        int strike_bob = (grade_timer > 0 && last_grade > 0) ? -2 : 0;
        unsigned anim = 0u;
        /* Dual opposing swinging pendulum metronomes driven at 12-frame drum beat cadence. */
        int ph = (int)(frames % 12u);
        int swing = (ph < 6) ? (ph * 4 - 12) : ((12 - ph) * 4 - 12);
        draw_thick_line(dst_rgba, dst_w, dst_h, 52, 34, 52 + swing * 2, 82, 220, 200, 120, 220);
        draw_filled_disc(dst_rgba, dst_w, dst_h, 52 + swing * 2, 84, 5, 255, 220, 95, 240);
        draw_thick_line(dst_rgba, dst_w, dst_h, (int)dst_w - 52, 34,
                        (int)dst_w - 52 - swing * 2, 82, 220, 200, 120, 220);
        draw_filled_disc(dst_rgba, dst_w, dst_h, (int)dst_w - 52 - swing * 2, 84, 5,
                         255, 220, 95, 240);

        if (g_stxt.drum_pylon_n > 0) {
            unsigned pi = (frames >> 2) % g_stxt.drum_pylon_n;
            const stxt_frame *pyl = &g_stxt.drum_pylon[pi];
            if (pyl->rgba) {
                runway_compose_blit_sprite(dst_rgba, dst_w, dst_h, pyl->rgba,
                                           pyl->w, pyl->h, 12, 92);
                runway_compose_blit_sprite_ex(dst_rgba, dst_w, dst_h, pyl->rgba,
                                              pyl->w, pyl->h,
                                              (int)dst_w - 12 - (int)pyl->w, 92, 1);
            }
        }
        stage_enemies_set_bones_ground_y(144, stage_name);
        stage_enemies_draw_bones(dst_rgba, dst_w, dst_h,
                                 (int)(dst_w / 2u) + dx_off, 144 + strike_bob,
                                 facing, 0, 0, anim);
        if (g_stxt.drum_fg_n > 0 && g_stxt.drum_fg[0].rgba) {
            const stxt_frame *fg = &g_stxt.drum_fg[0];
            runway_compose_blit_sprite(dst_rgba, dst_w, dst_h, fg->rgba,
                                       fg->w, fg->h,
                                       ((int)dst_w - (int)fg->w) / 2, 136);
        }
        /* Highlight the active N/E/S/W jungle drum with a glowing cue halo + crimson spark. */
        {
            unsigned pk = expected_kind & 3u;
            int px = k_pad_x[pk], py = k_pad_y[pk];
            int hit_pop = (grade_timer > 0 && last_grade > 0);
            draw_filled_disc(dst_rgba, dst_w, dst_h, px, py, hit_pop ? 13 : 10,
                             hit_pop ? 110 : 255, hit_pop ? 235 : 210,
                             hit_pop ? 255 : 85, 130);
            draw_filled_disc(dst_rgba, dst_w, dst_h, px, py, 4, 255, 248, 200, 210);
            if (hit_pop && g_stxt.drum_spark_n > 0 && g_stxt.drum_spark.rgba) {
                blit_stxt_anchored(dst_rgba, dst_w, dst_h, &g_stxt.drum_spark,
                                   px, py - 8, 1, 0, 200u, 256u);
            }
        }
        if (g_stxt.drum_frond_n > 0 && g_stxt.drum_frond[0].rgba) {
            const stxt_frame *fr = &g_stxt.drum_frond[0];
            runway_compose_blit_sprite(dst_rgba, dst_w, dst_h, fr->rgba,
                                       fr->w, fr->h, 0, 0);
        }
    }
}

void stage_enemies_draw_rhythm_stage(uint8_t *dst_rgba, unsigned dst_w,
                                     unsigned dst_h, const char *stage_name,
                                     unsigned frames, unsigned combo,
                                     int last_grade, unsigned grade_timer,
                                     unsigned expected_kind) {
    stage_enemies_draw_rhythm_stage_ex(dst_rgba, dst_w, dst_h, stage_name,
                                       frames, combo, last_grade, grade_timer,
                                       expected_kind, 0u, 1u);
}

void stage_enemies_draw_village_hud(uint8_t *dst_rgba, unsigned dst_w,
                                    unsigned dst_h, unsigned saved,
                                    unsigned total, unsigned frames) {
    char buf[32];
    int y0, rx, ry;
    unsigned i;
    if (!dst_rgba || !dst_w || dst_h < 40u) return;
    y0 = (int)dst_h - 30;
    for (ry = y0 - 2; ry < y0 + 26; ry++) {
        for (rx = 6; rx < 114; rx++)
            blend_px(dst_rgba, dst_w, dst_h, rx, ry, 10, 14, 22, 145);
    }
    if (g_stxt.vil_hand_n > 0 && g_stxt.vil_hand[0].rgba) {
        const stxt_frame *h = &g_stxt.vil_hand[0];
        runway_compose_blit_sprite_scaled(dst_rgba, dst_w, dst_h, h->rgba, h->w, h->h,
                                          8, y0, 22u, 22u, 0);
    }
    for (i = 0; i < total && i < 6u; i++) {
        int gx = 32 + (int)i * 12;
        if (i < saved && g_stxt.vil_lilguys_n > 0 && g_stxt.vil_lilguys[0].rgba) {
            const stxt_frame *lg = &g_stxt.vil_lilguys[(i + (frames >> 3)) % g_stxt.vil_lilguys_n];
            runway_compose_blit_sprite_scaled(dst_rgba, dst_w, dst_h,
                                              lg->rgba, lg->w, lg->h,
                                              gx, y0, 11u, 14u, 0);
        } else if (i < saved) {
            draw_filled_disc(dst_rgba, dst_w, dst_h, gx + 5, y0 + 8, 4, 80, 235, 195, 230);
        } else {
            draw_filled_disc(dst_rgba, dst_w, dst_h, gx + 5, y0 + 8, 4, 215, 70, 75, 220);
        }
    }
    snprintf(buf, sizeof buf, "%u/%u", saved, total);
    mb_font_draw_text_shadow(dst_rgba, dst_w, dst_h, 34, y0 + 15, buf,
                             175, 240, 220, 20, 8, 8, 1);
}

void stage_enemies_draw_laff_meter(uint8_t *dst_rgba, unsigned dst_w,
                                   unsigned dst_h, int boss_hp,
                                   int boss_max_hp, unsigned frames) {
    int cx = 36;
    int cy = (int)dst_h - 14;
    int a, r;
    int pct, sweep_lim;
    (void)frames;
    if (!dst_rgba || !dst_w || dst_h < 48u) return;
    if (boss_max_hp <= 0) boss_max_hp = 1;
    if (boss_hp < 0) boss_hp = 0;
    if (boss_hp > boss_max_hp) boss_hp = boss_max_hp;
    pct = ((boss_max_hp - boss_hp) * 100) / boss_max_hp;
    sweep_lim = -20 + (pct * 40) / 100;

    /* Sleek dark-obsidian & ice-cyan Saturn Laugh-O-Meter arc gauge. */
    for (r = 12; r <= 22; r++) {
        for (a = -22; a <= 22; a++) {
            int dx = (a * r) / 22;
            int dy2 = r * r - dx * dx;
            int dy = 0;
            uint8_t rr = 20, gg = 28, bb = 42;
            int alpha = 175;
            while ((dy + 1) * (dy + 1) <= dy2) dy++;
            if (r >= 21) {
                rr = 95; gg = 215; bb = 240; alpha = 215;
            } else if (r <= 13) {
                rr = 50; gg = 95; bb = 130; alpha = 180;
            } else if (a <= sweep_lim) {
                rr = 85; gg = 225; bb = 250; alpha = 205;
            }
            blend_px(dst_rgba, dst_w, dst_h, cx + dx, cy - dy, rr, gg, bb, alpha);
        }
    }
    /* Crisp white needle sweeping left (0%) -> right (100%). */
    {
        int na = sweep_lim;
        int nx = (na * 18) / 22;
        int ny2 = 18 * 18 - nx * nx;
        int ny = 0;
        while ((ny + 1) * (ny + 1) <= (ny2 > 0 ? ny2 : 0)) ny++;
        draw_thick_line(dst_rgba, dst_w, dst_h, cx, cy, cx + nx, cy - ny,
                        240, 250, 255, 240);
        draw_filled_disc(dst_rgba, dst_w, dst_h, cx, cy, 3, 185, 235, 255, 255);
    }
    mb_font_draw_text_shadow(dst_rgba, dst_w, dst_h, cx - 14, (int)dst_h - 10,
                             "LAFF", 155, 235, 255, 8, 12, 18, 1);
}

void stage_enemies_draw_mauso_fg(uint8_t *dst_rgba, unsigned dst_w,
                                 unsigned dst_h, uint32_t camera_x,
                                 unsigned frames) {
    int slot;
    unsigned off;
    if (!dst_rgba || !dst_w || dst_h < 40u) return;
    off = ((camera_x * 5u) / 4u) % 48u;
    for (slot = -1; slot < 8; slot++) {
        int bx = slot * 48 + 20 - (int)off;
        int sway = (int)((((frames >> 2) + (unsigned)(slot + 4)) & 7u)) - 4;
        int top_y = (int)dst_h - 22 + (sway > 0 ? sway / 2 : -sway / 2);
        int scx = bx + 16 + sway / 2;
        int scy = top_y + 12;
        int f;
        /* Silhouetted reaching skeletal forearm + splayed fingers */
        draw_thick_line(dst_rgba, dst_w, dst_h, bx - 4, (int)dst_h - 1,
                        bx + sway, top_y - 2, 46, 52, 68, 220);
        draw_thick_line(dst_rgba, dst_w, dst_h, bx - 3, (int)dst_h - 1,
                        bx + sway + 1, top_y - 1, 14, 16, 24, 240);
        for (f = -1; f <= 1; f++) {
            draw_thick_line(dst_rgba, dst_w, dst_h, bx + sway, top_y - 2,
                            bx + sway + f * 4, top_y - 8, 42, 48, 64, 200);
        }
        /* Silhouetted skull cranium with cool backlight rim + jaw */
        draw_filled_disc(dst_rgba, dst_w, dst_h, scx, scy, 9, 54, 64, 84, 215);
        draw_filled_disc(dst_rgba, dst_w, dst_h, scx, scy + 1, 8, 16, 18, 26, 245);
        draw_rect(dst_rgba, dst_w, dst_h, scx - 4, scy + 6, 8, 5, 16, 18, 26);
        draw_filled_disc(dst_rgba, dst_w, dst_h, scx - 3, scy, 2, 6, 8, 12, 240);
        draw_filled_disc(dst_rgba, dst_w, dst_h, scx + 3, scy, 2, 6, 8, 12, 240);
    }
}

void stage_enemies_draw_finale_guitar(uint8_t *dst_rgba, unsigned dst_w,
                                      unsigned dst_h, int foot_x, int foot_y,
                                      int facing, int target_x, int target_y,
                                      int shot_flash) {
    int gx = foot_x + (facing < 0 ? -26 : -14);
    int gy = foot_y - 30;
    if (!dst_rgba || !dst_w || !dst_h) return;
    if (g_stxt.fin_guitar_n > 0) {
        const stxt_frame *fg = &g_stxt.fin_guitar[shot_flash ? (1u % g_stxt.fin_guitar_n) : 0u];
        if (fg->rgba && fg->w && fg->h) {
            runway_compose_blit_sprite_scaled(dst_rgba, dst_w, dst_h,
                                              fg->rgba, fg->w, fg->h,
                                              gx, gy, 42u, 15u,
                                              facing < 0 ? 1 : 0);
        }
    }
    if (shot_flash) {
        int x0 = foot_x + (facing < 0 ? -12 : 12);
        int y0 = foot_y - 24;
        int mx = (x0 + target_x) / 2;
        int my = (y0 + target_y) / 2 + ((shot_flash & 1) ? -8 : 8);
        draw_electric_seg(dst_rgba, dst_w, dst_h, x0, y0, target_x, target_y,
                          (unsigned)shot_flash * 13u);
        draw_electric_seg(dst_rgba, dst_w, dst_h, x0, y0, mx, my,
                          (unsigned)shot_flash * 29u + 3u);
        draw_electric_seg(dst_rgba, dst_w, dst_h, mx, my, target_x, target_y,
                          (unsigned)shot_flash * 41u + 7u);
        draw_filled_disc(dst_rgba, dst_w, dst_h, target_x, target_y, 7,
                         120, 235, 255, 190);
    }
}

void stage_enemies_draw_bat_branch(uint8_t *dst_rgba, unsigned dst_w,
                                   unsigned dst_h, int x0, int surface_y,
                                   int width_px, int variant) {
    int bx;
    if (!dst_rgba || !dst_w || !dst_h || width_px < 12) return;
    if (g_stxt.bat_branch_n > 0) {
        const stxt_frame *br = &g_stxt.bat_branch[(unsigned)variant % g_stxt.bat_branch_n];
        if (br->rgba && br->w && br->h) {
            int step = (int)(br->w > 16u ? br->w - 12u : 48u);
            for (bx = x0; bx < x0 + width_px - 8; bx += step) {
                int rem = (x0 + width_px) - bx;
                unsigned sw = (rem < (int)br->w) ? (unsigned)rem : br->w;
                if (sw < 16u) sw = 16u;
                runway_compose_blit_sprite_scaled(dst_rgba, dst_w, dst_h,
                                                  br->rgba, br->w, br->h,
                                                  bx, surface_y - 6, sw, br->h,
                                                  (variant & 1));
            }
            return;
        }
    }
    draw_rect(dst_rgba, dst_w, dst_h, x0, surface_y, width_px, 6, 48, 72, 34);
    draw_rect(dst_rgba, dst_w, dst_h, x0 + 2, surface_y, width_px - 4, 2, 92, 132, 62);
}

void stage_enemies_draw_ice_floe(uint8_t *dst_rgba, unsigned dst_w,
                                 unsigned dst_h, int center_x, int surface_y,
                                 int variant, unsigned frames) {
    int bob = (int)(((frames >> 4) + (unsigned)variant) & 1u);
    if (!dst_rgba || !dst_w || !dst_h) return;
    if (g_stxt.ice_floe_n > 0) {
        const stxt_frame *f = &g_stxt.ice_floe[(unsigned)variant % g_stxt.ice_floe_n];
        if (f->rgba && f->w && f->h) {
            runway_compose_blit_sprite_ex(dst_rgba, dst_w, dst_h, f->rgba, f->w, f->h,
                                          center_x - (int)(f->w / 2u),
                                          surface_y - 12 + bob, 0);
            return;
        }
    }
    draw_rect(dst_rgba, dst_w, dst_h, center_x - 32, surface_y - 4 + bob, 64, 14,
              115, 195, 240);
}

void stage_enemies_draw_ice_crack(uint8_t *dst_rgba, unsigned dst_w,
                                  unsigned dst_h, int center_x, int ground_y,
                                  int crack_stage, int is_hole) {
    if (!dst_rgba || !dst_w || !dst_h) return;
    if (is_hole && g_stxt.ice_hole_n > 0) {
        const stxt_frame *h = &g_stxt.ice_hole[(unsigned)crack_stage % g_stxt.ice_hole_n];
        if (h->rgba && h->w && h->h) {
            runway_compose_blit_sprite_ex(dst_rgba, dst_w, dst_h, h->rgba, h->w, h->h,
                                          center_x - (int)(h->w / 2u),
                                          ground_y - (int)(h->h / 3u), 0);
            return;
        }
    }
    if (!is_hole && g_stxt.ice_crack_n > 0) {
        unsigned idx = (crack_stage < 0) ? 0u : (unsigned)crack_stage;
        if (idx >= g_stxt.ice_crack_n) idx = g_stxt.ice_crack_n - 1u;
        {
            const stxt_frame *c = &g_stxt.ice_crack[idx];
            if (c->rgba && c->w && c->h) {
                runway_compose_blit_sprite_ex(dst_rgba, dst_w, dst_h, c->rgba, c->w, c->h,
                                              center_x - (int)(c->w / 2u),
                                              ground_y - (int)(c->h / 2u), 0);
                return;
            }
        }
    }
    if (is_hole)
        draw_filled_disc(dst_rgba, dst_w, dst_h, center_x, ground_y + 4, 14, 16, 46, 84, 220);
    else
        draw_thick_line(dst_rgba, dst_w, dst_h, center_x - 10, ground_y - 2,
                        center_x + 10, ground_y + 4, 195, 242, 255, 200);
}

void stage_enemies_draw_staing_arms(uint8_t *dst_rgba, unsigned dst_w,
                                    unsigned dst_h, int center_x, int y,
                                    unsigned frames) {
    if (!dst_rgba || !dst_w || !dst_h) return;
    if (g_stxt.staing_arms_n > 0) {
        const stxt_frame *a = &g_stxt.staing_arms[(frames >> 4) % g_stxt.staing_arms_n];
        if (a->rgba && a->w && a->h) {
            runway_compose_blit_sprite_scaled(dst_rgba, dst_w, dst_h,
                                              a->rgba, a->w, a->h,
                                              center_x - 42, y - 34, 84u, 76u, 0);
            return;
        }
    }
}

void stage_enemies_draw_staing_spire(uint8_t *dst_rgba, unsigned dst_w,
                                     unsigned dst_h, int center_x, int bottom_y,
                                     int variant) {
    if (!dst_rgba || !dst_w || !dst_h) return;
    if (g_stxt.staing_spire_n > 0) {
        const stxt_frame *sp = &g_stxt.staing_spire[(unsigned)variant % g_stxt.staing_spire_n];
        if (sp->rgba && sp->w && sp->h) {
            runway_compose_blit_sprite_ex(dst_rgba, dst_w, dst_h, sp->rgba, sp->w, sp->h,
                                          center_x - (int)(sp->w / 2u),
                                          bottom_y - (int)sp->h, 0);
            return;
        }
    }
}

void stage_enemies_draw_runway_ghost(uint8_t *dst_rgba, unsigned dst_w,
                                     unsigned dst_h, int foot_x, int foot_y,
                                     unsigned frames) {
    int hover = (int)((frames >> 2) & 7u);
    int dy = (hover <= 4) ? -hover : -(8 - hover);
    if (!dst_rgba || !dst_w || !dst_h) return;
    if (g_stxt.runway_ghost_ok && g_stxt.runway_ghost.rgba) {
        blit_stxt_foot(dst_rgba, dst_w, dst_h, &g_stxt.runway_ghost,
                       foot_x, foot_y + dy, 0, 210u, 256u);
    }
}

int stage_enemies_spawn_mauso_assembler(stage_enemies *e, unsigned world_x_px,
                                        int arena_lo_px, int arena_hi_px,
                                        int32_t damage) {
    unsigned u;
    stage_enemy *ent;
    if (!e) return -1;
    u = e->units_per_pixel ? e->units_per_pixel : 2341u;
    ent = spawn(e, STAGE_ENEMY_LORD, world_x_px, damage, u);
    if (!ent) return -1;
    ent->hp = 2;
    if (arena_lo_px < 16) arena_lo_px = 16;
    if (arena_hi_px <= arena_lo_px) arena_hi_px = arena_lo_px + 120;
    ent->patrol_lo = (uint32_t)arena_lo_px * u;
    ent->patrol_hi = (uint32_t)arena_hi_px * u;
    ent->cooldown = 28u;
    return 0;
}

unsigned stage_enemies_mauso_alive(const stage_enemies *e) {
    unsigned i, n = 0;
    if (!e) return 0;
    for (i = 0; i < e->count; i++) {
        if (!e->ents[i].active) continue;
        if (e->ents[i].kind == STAGE_ENEMY_LORD ||
            e->ents[i].kind == STAGE_ENEMY_GANG)
            n++;
    }
    return n;
}
