#include "stage_enemy.h"
#include "pan_palette.h"
#include "resource.h"
#include "runway_compose.h"
#include "sprite_decode.h"
#include "stxtr.h"
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
    ent->hp = (kind == STAGE_ENEMY_LORD || kind == STAGE_ENEMY_RED) ? 3 : 1;
    ent->patrol_lo = (px > 20u ? px - 20u : 4u) * upp;
    ent->patrol_hi = (px + 20u) * upp;
    ent->vx = (int32_t)(2u * upp);
    if (kind == STAGE_ENEMY_POOF_BLUE || kind == STAGE_ENEMY_BLUE)
        ent->vx = -(int32_t)(2u * upp);
    return ent;
}

void stage_enemies_init_runway(stage_enemies *e, unsigned units_per_pixel) {
    unsigned u = units_per_pixel ? units_per_pixel : 2341u;
    int32_t d = RUNWAY_DAMAGE_DEFAULT;
    int32_t d2 = RUNWAY_DAMAGE_DEFAULT / 2;
    if (!e) return;
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
    memset(e, 0, sizeof *e);
    e->units_per_pixel = u;
    e->stage_name = stage_name ? stage_name : "";
    if (stage_name && (!strcmp(stage_name, "BAT1") || !strcmp(stage_name, "BAT2") ||
                       !strcmp(stage_name, "BAT3")))
        bat = 1;
    /* Poofs on all sidescrl; denser on BAT* (poofs.o / CBlueSparkle). */
    spawn(e, STAGE_ENEMY_POOF_RED, 95u, d, u);
    spawn(e, STAGE_ENEMY_POOF_BLUE, 135u, d2, u);
    if (bat) {
        spawn(e, STAGE_ENEMY_POOF_RED, 70u, d2, u);
        spawn(e, STAGE_ENEMY_POOF_BLUE, 160u, d, u);
    }
}

void stage_enemies_init_special(stage_enemies *e, const char *stage_name,
                                unsigned units_per_pixel) {
    unsigned u = units_per_pixel ? units_per_pixel : 2341u;
    int32_t d = RUNWAY_DAMAGE_DEFAULT;
    int32_t d2 = RUNWAY_DAMAGE_DEFAULT / 2;
    if (!e) return;
    memset(e, 0, sizeof *e);
    e->units_per_pixel = u;
    e->stage_name = stage_name ? stage_name : "";
    if (!stage_name) return;

    if (!strcmp(stage_name, "GENRM") || !strcmp(stage_name, "GENROOM")) {
        spawn(e, STAGE_ENEMY_RED, 90u, d, u);
        spawn(e, STAGE_ENEMY_BLUE, 130u, d, u);
        spawn(e, STAGE_ENEMY_ZAP, 110u, d2, u);
        spawn(e, STAGE_ENEMY_POOF_RED, 150u, d2, u);
        e->ents[2].cooldown = TIMING_ZAP_MAX;
    } else if (!strcmp(stage_name, "BIGBNS") || !strcmp(stage_name, "BIGBONES")) {
        spawn(e, STAGE_ENEMY_LORD, 100u, d, u);
        spawn(e, STAGE_ENEMY_GANG, 140u, d, u);
        spawn(e, STAGE_ENEMY_POOF_RED, 80u, d2, u);
        e->ents[1].cooldown = 45u;
    } else if (!strcmp(stage_name, "FINALE")) {
        /* Boss: CFBones + ring/bolt projectiles from RingDefinitionTable. */
        spawn(e, STAGE_ENEMY_LORD, 200u, d, u); /* fbones stand-in */
        spawn(e, STAGE_ENEMY_RING, 180u, d2, u);
        spawn(e, STAGE_ENEMY_BOLT, 220u, d, u);
        e->ents[1].cooldown = 20u;
        e->ents[2].cooldown = 28u;
        e->ents[0].hp = 5;
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

int stage_enemies_step(stage_enemies *e, runway_skel *skel, uint32_t bones_x,
                       runway_hazards *haz, int apply_hits) {
    unsigned i;
    unsigned u;
    if (!e) return 0;
    if (skel && (skel->died || skel->paused) && apply_hits) return 0;
    u = e->units_per_pixel ? e->units_per_pixel : 2341u;

    for (i = 0; i < e->count; i++) {
        stage_enemy *ent = &e->ents[i];
        if (!ent->active) continue;

        switch (ent->kind) {
        case STAGE_ENEMY_GANG:
            patrol(ent);
            if (ent->cooldown) ent->cooldown--;
            if (ent->cooldown == 0) {
                /* Throw__11CGangMember → CThrown near bones. */
                uint32_t tx =
                    bones_x + (ent->vx > 0 ? 10u * u
                                           : (uint32_t)(-(int32_t)(10u * u)));
                if (haz)
                    runway_hazards_spawn_thrown(haz, tx, ent->y, ent->damage / 2);
                e->throws_spawned++;
                ent->cooldown = 48u;
            }
            if (apply_hits && skel &&
                in_range(bones_x, ent->x, STAGE_ENEMY_HIT_RANGE)) {
                if (hit_bones(e, skel, ent)) return 1;
            }
            break;

        case STAGE_ENEMY_LORD:
            patrol(ent);
            if (apply_hits && skel &&
                in_range(bones_x, ent->x, STAGE_ENEMY_HIT_RANGE + (8u * u))) {
                if (hit_bones(e, skel, ent)) return 1;
            }
            break;

        case STAGE_ENEMY_POOF_RED:
        case STAGE_ENEMY_POOF_BLUE:
            patrol(ent);
            if (apply_hits && skel &&
                in_range(bones_x, ent->x, STAGE_ENEMY_HIT_RANGE)) {
                if (hit_bones(e, skel, ent)) return 1;
            }
            break;

        case STAGE_ENEMY_RED:
        case STAGE_ENEMY_BLUE:
            /* Approach Bones (CheckForBones / BigCheckForBones). */
            if (bones_x + 4u * u < ent->x)
                ent->x -= 3u * u;
            else if (bones_x > ent->x + 4u * u)
                ent->x += 3u * u;
            ent->anim++;
            if (apply_hits && skel &&
                in_range(bones_x, ent->x, STAGE_ENEMY_HIT_RANGE)) {
                if (hit_bones(e, skel, ent)) return 1;
            }
            break;

        case STAGE_ENEMY_ZAP:
            if (ent->cooldown) ent->cooldown--;
            if (ent->cooldown == 0) {
                e->zaps_fired++;
                ent->cooldown = TIMING_ZAP_MAX;
                if (apply_hits && skel &&
                    in_range(bones_x, ent->x,
                             STAGE_ENEMY_HIT_RANGE + 4u * u)) {
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
                if (apply_hits && skel &&
                    in_range(bones_x, ent->x, STAGE_ENEMY_HIT_RANGE)) {
                    if (hit_bones(e, skel, ent)) return 1;
                }
            }
            break;
        }

        case STAGE_ENEMY_BOLT:
            if (ent->cooldown) ent->cooldown--;
            if (bones_x + 2u * u < ent->x)
                ent->x -= 4u * u;
            else if (bones_x > ent->x + 2u * u)
                ent->x += 4u * u;
            if (ent->cooldown == 0) {
                e->bolts_fired++;
                ent->cooldown = 32u;
            }
            if (apply_hits && skel &&
                in_range(bones_x, ent->x, STAGE_ENEMY_HIT_RANGE)) {
                if (hit_bones(e, skel, ent)) return 1;
            }
            break;

        case STAGE_ENEMY_THROWN:
            ent->x += (uint32_t)ent->vx;
            if (apply_hits && skel &&
                in_range(bones_x, ent->x, STAGE_ENEMY_HIT_RANGE)) {
                int dead = hit_bones(e, skel, ent);
                ent->active = 0;
                if (dead) return 1;
            }
            break;
        }
    }
    return 0;
}

unsigned stage_enemies_count_kind(const stage_enemies *e, stage_enemy_kind kind) {
    unsigned i, n = 0;
    if (!e) return 0;
    for (i = 0; i < e->count; i++)
        if (e->ents[i].active && e->ents[i].kind == kind) n++;
    return n;
}

void stage_enemies_populate_sidescrl_strip(stage_enemies *e, unsigned scroll_w) {
    unsigned u, px;
    int idx = 0;
    if (!e || scroll_w <= 360u) return;
    u = e->units_per_pixel ? e->units_per_pixel : 2341u;
    for (px = 260u; px + 80u < scroll_w && e->count < STAGE_ENEMY_MAX; px += 150u, idx++) {
        stage_enemy_kind k = (idx % 3 == 0) ? STAGE_ENEMY_GANG
                           : ((idx % 3 == 1) ? STAGE_ENEMY_POOF_RED
                                             : STAGE_ENEMY_POOF_BLUE);
        stage_enemy *ent = spawn(e, k, px, RUNWAY_DAMAGE_DEFAULT / 2, u);
        if (ent && k == STAGE_ENEMY_GANG)
            ent->cooldown = 50u + (unsigned)(idx * 12);
    }
}

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
    stxt_frame gang[8];
    unsigned gang_n;
    stxt_frame lord[8];
    unsigned lord_n;
    stxt_frame poof_blue[4];
    unsigned poof_blue_n;
    stxt_frame poof_red[4];
    unsigned poof_red_n;
    stxt_frame bats[6];
    unsigned bats_n;
    stxt_frame gen_blue[4];
    unsigned gen_blue_n;
    stxt_frame gen_red[4];
    unsigned gen_red_n;
} g_stxt;



static int extract_stage_palette(const uint8_t *code, size_t code_size,
                                 uint8_t *palette) {
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
    for (i = 0; i < 1024; i++) {
        palette[4 * i + 0] = 0;
        palette[4 * i + 1] = (uint8_t)(words[i] >> 16);
        palette[4 * i + 2] = (uint8_t)(words[i] >> 8);
        palette[4 * i + 3] = (uint8_t)words[i];
    }
    return 0;
}

static int decode_stxt_at(const void *stxt, size_t stxt_size, size_t offset,
                          const uint8_t *bank_pal, stxt_frame *out) {
    stxtr_record rec;
    size_t off = offset, sz;
    uint8_t *px;
    if (!stxt || offset >= stxt_size || !bank_pal || !out) return -1;
    if (stxtr_next(stxt, stxt_size, &off, &rec) != 1) return -1;
    sz = (size_t)rec.width * rec.height * 4u;
    px = malloc(sz);
    if (!px) return -1;
    if (rec.size == (size_t)rec.width * rec.height) {
        /* Direct uncompressed 8bpp Saturn sprite */
        for (size_t j = 0; j < (size_t)rec.width * rec.height; j++) {
            uint8_t idx = rec.data[j];
            if (idx == 0) {
                px[4 * j + 0] = px[4 * j + 1] = px[4 * j + 2] = px[4 * j + 3] = 0;
            } else {
                px[4 * j + 0] = bank_pal[4 * idx + 1];
                px[4 * j + 1] = bank_pal[4 * idx + 2];
                px[4 * j + 2] = bank_pal[4 * idx + 3];
                px[4 * j + 3] = 255;
            }
        }
    } else if (mb_sprite_decode(rec.data, rec.size, rec.width, rec.height, 0x04a8,
                         bank_pal, 1024, px, sz)) {
        free(px);
        return -1;
    }
    out->rgba = px;
    out->w = rec.width;
    out->h = rec.height;
    return 0;
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
            static const size_t k_stand_off[4] = {0x43490, 0x43e00, 0x447c0, 0x44f50};
            static const size_t k_tall_off[2] = {0x41b40, 0x42230};
            static const size_t k_air_off[4] = {0x3f000, 0x3fc50, 0x40720, 0x41180};
            static const size_t k_walk_off[20] = {
                0x1ee40, 0x1f830, 0x202c0, 0x20dc0, 0x218c0,
                0x22390, 0x22e20, 0x236d0, 0x23f50, 0x24900,
                0x252f0, 0x25c70, 0x26620, 0x271f0, 0x27e40,
                0x28a90, 0x29520, 0x29f40, 0x2a7c0, 0x2b250
            };
            static const size_t k_gang_off[8] = {
                0x456d0, 0x45ec0, 0x468e0, 0x473e0,
                0x47f50, 0x48870, 0x49170, 0x49840
            };
            static const size_t k_poof_off[4] = {0x04eb0, 0x05070, 0x05250, 0x05430};
            const uint8_t *b2 = pal + 2 * 1024;
            const uint8_t *b3 = pal + 3 * 1024;

            for (i = 0; i < 4; i++)
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
            for (i = 0; i < 20; i++)
                if (decode_stxt_at(stxt, stxt_sz, k_walk_off[i], b2,
                                   &g_stxt.bones_walk[g_stxt.bones_walk_n]) == 0)
                    g_stxt.bones_walk_n++;
            for (i = 0; i < 8; i++)
                if (decode_stxt_at(stxt, stxt_sz, k_gang_off[i], b3,
                                   &g_stxt.gang[g_stxt.gang_n]) == 0)
                    g_stxt.gang_n++;
            for (i = 0; i < 4; i++) {
                if (decode_stxt_at(stxt, stxt_sz, k_poof_off[i], b2,
                                   &g_stxt.poof_blue[g_stxt.poof_blue_n]) == 0)
                    g_stxt.poof_blue_n++;
                if (decode_stxt_at(stxt, stxt_sz, k_poof_off[i], b3,
                                   &g_stxt.poof_red[g_stxt.poof_red_n]) == 0)
                    g_stxt.poof_red_n++;
            }
            g_stxt.loaded_d1 = 1;
        }
        free(code);
        free(stxt);
        code = stxt = NULL;

        if (g_stxt.loaded_d1 &&
            resource_load(cd, "/05MAUSO/03CODE", &code, &code_sz) == 0 &&
            code_sz > 20000 &&
            resource_load(cd, "/05MAUSO/04STXT", &stxt, &stxt_sz) == 0 &&
            stxt_sz > 0x35600 &&
            extract_stage_palette((const uint8_t *)code, code_sz, pal) == 0) {
            static const size_t k_lord_off[8] = {
                0x2c240, 0x2cbd0, 0x2f1f0, 0x30b00,
                0x317a0, 0x321e0, 0x34a70, 0x355a0
            };
            const uint8_t *b1 = pal + 1 * 1024;
            for (i = 0; i < 8; i++)
                if (decode_stxt_at(stxt, stxt_sz, k_lord_off[i], b1,
                                   &g_stxt.lord[g_stxt.lord_n]) == 0)
                    g_stxt.lord_n++;
        }
        free(code);
        free(stxt);
        code = stxt = NULL;

        if (g_stxt.loaded_d1 &&
            resource_load(cd, "/08BAT1/03CODE", &code, &code_sz) == 0 &&
            code_sz > 20000 &&
            resource_load(cd, "/08BAT1/04STXT", &stxt, &stxt_sz) == 0 &&
            stxt_sz > 0x06100 &&
            extract_stage_palette((const uint8_t *)code, code_sz, pal) == 0) {
            static const size_t k_bat_off[6] = {
                0x00000, 0x00860, 0x01180, 0x01e30, 0x03d90, 0x06050
            };
            const uint8_t *b1 = pal + 1 * 1024;
            for (i = 0; i < 6; i++)
                if (decode_stxt_at(stxt, stxt_sz, k_bat_off[i], b1,
                                   &g_stxt.bats[g_stxt.bats_n]) == 0)
                    g_stxt.bats_n++;
        }
        free(code);
        free(stxt);
        code = stxt = NULL;
    }

    if (!g_stxt.loaded_d2) {
        if (resource_load(cd, "/25GENRM/03CODE", &code, &code_sz) == 0 &&
            code_sz > 20000 &&
            resource_load(cd, "/25GENRM/04STXT", &stxt, &stxt_sz) == 0 &&
            stxt_sz > 0x05b00 &&
            extract_stage_palette((const uint8_t *)code, code_sz, pal) == 0) {
            static const size_t k_gen_off[4] = {0x03310, 0x038a0, 0x03ef0, 0x04770};
            const uint8_t *b1 = pal + 1 * 1024;
            const uint8_t *b2 = pal + 2 * 1024;
            for (i = 0; i < 4; i++) {
                if (decode_stxt_at(stxt, stxt_sz, k_gen_off[i], b1,
                                   &g_stxt.gen_blue[g_stxt.gen_blue_n]) == 0)
                    g_stxt.gen_blue_n++;
                if (decode_stxt_at(stxt, stxt_sz, k_gen_off[i], b2,
                                   &g_stxt.gen_red[g_stxt.gen_red_n]) == 0)
                    g_stxt.gen_red_n++;
            }
            g_stxt.loaded_d2 = 1;
        }
        free(code);
        free(stxt);
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

static void draw_rect(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                      int x0, int y0, int w, int h,
                      uint8_t r, uint8_t g, uint8_t b) {
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            put_px(dst, dst_w, dst_h, x0 + x, y0 + y, r, g, b);
}

static void blit_stxt_foot(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                           const stxt_frame *f, int foot_x, int foot_y,
                           int flip_x) {
    if (!f || !f->rgba || !f->w || !f->h) return;
    runway_compose_blit_sprite_ex(dst, dst_w, dst_h, f->rgba, f->w, f->h,
                                  foot_x - (int)(f->w / 2u),
                                  foot_y - (int)f->h, flip_x);
}

void stage_enemies_draw_bones(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                              int foot_x, int foot_y, int facing, int tall,
                              int airborne, unsigned anim_frame) {
    int flip = (facing < 0) ? 1 : 0;
    const stxt_frame *f = NULL;
    if (!dst || !dst_w || !dst_h) return;
    if (foot_y < 36) foot_y = 135;
    if (foot_y > (int)dst_h - 4) foot_y = (int)dst_h - 4;

    if (airborne && g_stxt.bones_air_n > 0) {
        f = &g_stxt.bones_air[anim_frame % g_stxt.bones_air_n];
    } else if (tall && g_stxt.bones_tall_n > 0) {
        f = &g_stxt.bones_tall[anim_frame % g_stxt.bones_tall_n];
    } else if (anim_frame > 0 && g_stxt.bones_walk_n > 0) {
        f = &g_stxt.bones_walk[anim_frame % g_stxt.bones_walk_n];
    } else if (g_stxt.bones_stand_n > 0) {
        f = &g_stxt.bones_stand[anim_frame % g_stxt.bones_stand_n];
    } else if (g_stxt.bones_walk_n > 0) {
        f = &g_stxt.bones_walk[0];
    }
    if (f && f->rgba) {
        blit_stxt_foot(dst, dst_w, dst_h, f, foot_x, foot_y, flip);
        return;
    }
    /* Minimal fallback only if no disc sprites have been loaded yet (unit tests). */
    draw_rect(dst, dst_w, dst_h, foot_x - 3, foot_y - 28, 7, 28, 225, 220, 200);
}

static void draw_poof_orb(uint8_t *dst, unsigned dst_w, unsigned dst_h,
                          int cx, int cy, unsigned anim, int is_blue) {
    const stxt_frame *arr = is_blue ? g_stxt.poof_blue : g_stxt.poof_red;
    unsigned n = is_blue ? g_stxt.poof_blue_n : g_stxt.poof_red_n;
    if (n > 0) {
        const stxt_frame *f = &arr[(anim >> 2) % n];
        runway_compose_blit_sprite_ex(dst, dst_w, dst_h, f->rgba, f->w, f->h,
                                      cx - (int)(f->w / 2u),
                                      cy - (int)(f->h / 2u), 0);
        return;
    }
    draw_rect(dst, dst_w, dst_h, cx - 3, cy - 3, 7, 7,
              is_blue ? 80 : 240, is_blue ? 180 : 80, is_blue ? 255 : 40);
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

void stage_enemies_render(const stage_enemies *e, const runway_hazards *haz,
                          uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                          uint32_t camera_x, int screen_anchor_x, int base_y) {
    unsigned i, u;
    int is_bat_stage = 0;
    if (!dst_rgba || !dst_w || !dst_h) return;
    if (base_y < 64 || (unsigned)base_y >= dst_h - 4u) base_y = 148;
    u = (e && e->units_per_pixel) ? e->units_per_pixel : 2341u;
    if (e && e->stage_name &&
        (!strcmp(e->stage_name, "BAT1") || !strcmp(e->stage_name, "BAT2") ||
         !strcmp(e->stage_name, "BAT3")))
        is_bat_stage = 1;

    if (e) {
        for (i = 0; i < e->count; i++) {
            const stage_enemy *ent = &e->ents[i];
            int px, sx, sy, flip;
            if (!ent->active) continue;
            px = (int)(ent->x / u);
            sx = px - (int)camera_x + screen_anchor_x;
            if (sx < -48 || sx >= (int)dst_w + 48) continue;
            sy = base_y;
            flip = (ent->vx < 0) ? 1 : 0;

            switch (ent->kind) {
            case STAGE_ENEMY_GANG:
                if (g_stxt.gang_n > 0) {
                    const stxt_frame *f =
                        &g_stxt.gang[(ent->anim >> 2) % g_stxt.gang_n];
                    blit_stxt_foot(dst_rgba, dst_w, dst_h, f, sx, sy, flip);
                } else {
                    draw_rect(dst_rgba, dst_w, dst_h, sx - 4, sy - 24, 8, 24,
                              150, 180, 170);
                }
                break;
            case STAGE_ENEMY_LORD:
                if (g_stxt.lord_n > 0) {
                    /* 05MAUSO upright skeleton faces left natively. */
                    const stxt_frame *f =
                        &g_stxt.lord[(ent->anim >> 2) % g_stxt.lord_n];
                    blit_stxt_foot(dst_rgba, dst_w, dst_h, f, sx, sy,
                                   ent->vx > 0 ? 1 : 0);
                } else if (g_stxt.gang_n > 0) {
                    const stxt_frame *f =
                        &g_stxt.gang[(ent->anim >> 2) % g_stxt.gang_n];
                    blit_stxt_foot(dst_rgba, dst_w, dst_h, f, sx, sy, flip);
                }
                break;
            case STAGE_ENEMY_POOF_RED:
            case STAGE_ENEMY_POOF_BLUE:
                if (is_bat_stage && g_stxt.bats_n > 0) {
                    const stxt_frame *f =
                        &g_stxt.bats[(ent->anim >> 2) % g_stxt.bats_n];
                    blit_stxt_foot(dst_rgba, dst_w, dst_h, f, sx,
                                   sy - 24 - (int)((ent->anim >> 1) & 7u),
                                   flip);
                } else {
                    draw_poof_orb(dst_rgba, dst_w, dst_h, sx,
                                  sy - 18 - (int)((ent->anim >> 1) & 3u),
                                  ent->anim,
                                  ent->kind == STAGE_ENEMY_POOF_BLUE);
                }
                break;
            case STAGE_ENEMY_RED:
                if (g_stxt.gen_red_n > 0) {
                    const stxt_frame *f =
                        &g_stxt.gen_red[(ent->anim >> 2) % g_stxt.gen_red_n];
                    blit_stxt_foot(dst_rgba, dst_w, dst_h, f, sx, sy, 0);
                } else {
                    draw_poof_orb(dst_rgba, dst_w, dst_h, sx, sy - 20,
                                  ent->anim, 0);
                }
                break;
            case STAGE_ENEMY_BLUE:
                if (g_stxt.gen_blue_n > 0) {
                    const stxt_frame *f =
                        &g_stxt.gen_blue[(ent->anim >> 2) % g_stxt.gen_blue_n];
                    blit_stxt_foot(dst_rgba, dst_w, dst_h, f, sx, sy, 0);
                } else {
                    draw_poof_orb(dst_rgba, dst_w, dst_h, sx, sy - 20,
                                  ent->anim, 1);
                }
                break;
            case STAGE_ENEMY_ZAP:
                draw_poof_orb(dst_rgba, dst_w, dst_h, sx, sy - 22,
                              ent->anim, 1);
                break;
            case STAGE_ENEMY_RING:
                draw_ring_proj(dst_rgba, dst_w, dst_h, sx, sy - 16, ent->anim);
                break;
            case STAGE_ENEMY_BOLT:
                draw_poof_orb(dst_rgba, dst_w, dst_h, sx, sy - 18,
                              ent->anim, 0);
                break;
            case STAGE_ENEMY_THROWN:
                draw_poof_orb(dst_rgba, dst_w, dst_h, sx, sy - 16,
                              ent->anim, 0);
                break;
            }
        }
    }

    if (haz) {
        for (i = 0; i < RUNWAY_THROWN_MAX; i++) {
            int px, sx;
            if (!haz->thrown[i].active) continue;
            px = (int)(haz->thrown[i].x / u);
            sx = px - (int)camera_x + screen_anchor_x;
            if (sx < -16 || sx >= (int)dst_w + 16) continue;
            draw_poof_orb(dst_rgba, dst_w, dst_h, sx, base_y - 18, i * 4u, 0);
        }
    }
}
