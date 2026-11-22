#include "runway_hazard.h"
#include <string.h>

void runway_hazards_init_runway(runway_hazards *h, unsigned units_per_pixel) {
    runway_hazards_init_pan_stage(h, "RUNWAY", units_per_pixel);
}

void runway_hazards_init_pan_stage(runway_hazards *h, const char *stage_name,
                                   unsigned units_per_pixel) {
    /* Retain the public initialization signature. Each stage now supplies its own
     * terrain or moving actor contacts from the loaded disc. */
    (void)stage_name;
    (void)units_per_pixel;
    if (h) memset(h,0,sizeof *h);
}

void runway_hazards_spawn_thrown_vel(runway_hazards *h, uint32_t x, uint32_t y,
                                     int32_t vx, int32_t damage) {
    unsigned i;
    if (!h) return;
    for (i = 0; i < RUNWAY_THROWN_MAX; i++) {
        if (h->thrown[i].active) continue;
        h->thrown[i].x = x;
        h->thrown[i].y = y;
        h->thrown[i].vx = vx;
        h->thrown[i].damage = damage > 0 ? damage : RUNWAY_DAMAGE_DEFAULT;
        h->thrown[i].ttl = vx != 0 ? 110u : 0u;
        h->thrown[i].active = 1;
        if (i + 1u > h->thrown_count)
            h->thrown_count = i + 1u;
        return;
    }
}

void runway_hazards_spawn_thrown(runway_hazards *h, uint32_t x, uint32_t y,
                                 int32_t damage) {
    runway_hazards_spawn_thrown_vel(h, x, y, 0, damage);
}

static int in_range(uint32_t a, uint32_t b, uint32_t range) {
    uint32_t d = (a > b) ? (a - b) : (b - a);
    return d <= range;
}

int runway_hazards_step_ex(runway_hazards *h, runway_skel *skel, uint32_t bones_x,
                           int airborne_high) {
    unsigned i;
    if (!h || !skel || skel->died || skel->paused) return 0;

    if (!airborne_high) {
        for (i = 0; i < h->zone_count; i++) {
            if (bones_x >= h->zones[i].x0 && bones_x <= h->zones[i].x1) {
                int was_cd = skel->hit_cooldown;
                if (runway_skel_hit(skel, h->zones[i].damage)) {
                    h->hits_applied++;
                    return 1;
                }
                if (was_cd == 0 && skel->hit_cooldown == RUNWAY_HIT_TIMEOUT)
                    h->hits_applied++;
                break; /* one zone hit attempt per frame */
            }
        }
    }

    for (i = 0; i < RUNWAY_THROWN_MAX; i++) {
        if (!h->thrown[i].active) continue;
        if (h->thrown[i].vx != 0) {
            int64_t nx = (int64_t)h->thrown[i].x + (int64_t)h->thrown[i].vx;
            if (nx < 0 || nx > UINT32_MAX) {
                h->thrown[i].active = 0;
                continue;
            }
            h->thrown[i].x = (uint32_t)nx;
        }
        if (h->thrown[i].ttl > 0u) {
            h->thrown[i].ttl--;
            if (h->thrown[i].ttl == 0u) {
                h->thrown[i].active = 0;
                continue;
            }
        }
        if (airborne_high) continue;
        if (!in_range(bones_x, h->thrown[i].x, RUNWAY_THROWN_RANGE)) continue;
        /* CheckForHit → BonesObjHitChk stand-in: proximity enough on host. */
        {
            int was_cd = skel->hit_cooldown;
            if (runway_skel_hit(skel, h->thrown[i].damage)) {
                h->hits_applied++;
                h->thrown[i].active = 0;
                return 1;
            }
            if (was_cd == 0 && skel->hit_cooldown == RUNWAY_HIT_TIMEOUT) {
                h->hits_applied++;
                h->thrown[i].active = 0;
            }
        }
    }
    return 0;
}

int runway_hazards_step(runway_hazards *h, runway_skel *skel, uint32_t bones_x) {
    return runway_hazards_step_ex(h, skel, bones_x, 0);
}

static int32_t ride_word(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
                     (uint32_t)p[2] << 8 | p[3]);
}

int ride_current_init(ride_current *c, const uint8_t *code, size_t size) {
    /* Constructor at 0x23ec74, in the actual GetCurrentX/Y operand order. */
    static const struct { unsigned va, end; } table[] = {
        {0x268ca4,0x268f94}, {0x26899c,0x268ca4}, {0x2685dc,0x2688cc},
        {0x2688cc,0x26899c}, {0x26835c,0x26836c}, {0x26836c,0x2685dc},
        {0x26833c,0x26834c}, {0x26834c,0x26835c}
    };
    if (!c || !code || size < 0x268f94u - 0x232000u) return -1;
    memset(c, 0, sizeof *c);
    for (unsigned i = 0; i < 8; i++) {
        ride_channel *ch = &c->channels[i];
        ch->keys = code + (table[i].va - 0x232000);
        ch->count = (table[i].end - table[i].va) / 8;
        int previous = -1;
        for (unsigned j = 0; j < ch->count; j++) {
            int at = ride_word(ch->keys + 8*j);
            int strength = ride_word(ch->keys + 8*j+4);
            if (at <= previous || strength < 0 || strength > 256) return -1;
            previous = at;
        }
        if (previous != 2500 || ride_word(ch->keys) != 1) return -1;
        ch->next = 1;
    }
    for (unsigned i = 0; i < 16; i++) {
        const uint8_t *p = code + (0x26831c - 0x232000) + i*2;
        c->wall_angle[i] = (uint16_t)((unsigned)p[0]*256u + p[1]);
    }
    return 0;
}

void ride_current_step(ride_current *c, unsigned record, int *dx, int *dy) {
    /* CCurrent reads last tick's strengths, then Yeild calls UpdateValue. */
    *dx = c->channels[1].value * 1966 / 256 - c->channels[0].value * 1966 / 256;
    *dy = c->channels[2].value * 1966 / 256 - c->channels[3].value * 1966 / 256;
    for (unsigned i = 0; i < 8; i++) {
        ride_channel *ch = &c->channels[i];
        while (ch->previous_record < record) {
            unsigned tick = ++ch->previous_record;
            ch->value += ch->delta;
            if (ch->next < ch->count &&
                tick == (unsigned)ride_word(ch->keys + (ch->next-1)*8)) {
                const uint8_t *key = ch->keys + ch->next++*8;
                ch->delta = (ride_word(key + 4) - ch->value) / (ride_word(key) - (int)tick);
            } else if (ch->next == ch->count && tick >= (unsigned)ride_word(ch->keys + (ch->count-1)*8)) {
                ch->delta = 0;
            }
        }
    }
}

unsigned ride_wall_coverage(const pan_collision_map *m, int x, int y) {
    /* GetHixelCoverage probes sin/cos in 0x100-turn increments, radius 256 >> 3. */
    static const int offsets[4][2] = {{0,32},{32,0},{0,-32},{-32,0}};
    unsigned mask = 0;
    for (unsigned i = 0; i < 4; i++)
        if (pan_collision_hixel_pixel(m, x + offsets[i][0], y + offsets[i][1]) != 0)
            mask |= 1u << i;
    return mask;
}
