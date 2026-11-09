#include "runway_hazard.h"
#include <string.h>

void runway_hazards_init_runway(runway_hazards *h, unsigned units_per_pixel) {
    runway_hazards_init_pan_stage(h, "RUNWAY", units_per_pixel);
}

static void set_zone(runway_hazards *h, unsigned i, unsigned u, unsigned px0,
                     unsigned px1, int32_t dmg) {
    if (i >= RUNWAY_HAZARD_ZONES) return;
    h->zones[i].x0 = px0 * u;
    h->zones[i].x1 = px1 * u;
    h->zones[i].damage = dmg;
}

void runway_hazards_init_pan_stage(runway_hazards *h, const char *stage_name,
                                   unsigned units_per_pixel) {
    unsigned u = units_per_pixel ? units_per_pixel : 2341u;
    int32_t d = RUNWAY_DAMAGE_DEFAULT;
    int32_t d2 = RUNWAY_DAMAGE_DEFAULT / 2;
    if (!h) return;
    memset(h, 0, sizeof *h);
    if (!stage_name) stage_name = "RUNWAY";

    /* Zones sit inside short generic_pan finish (~40→160px) and full RUNWAY (~40→280). */
    if (!strcmp(stage_name, "LOGS")) {
        set_zone(h, 0, u, 70, 85, d);
        set_zone(h, 1, u, 110, 125, d);
        set_zone(h, 2, u, 145, 155, d2);
    } else if (!strcmp(stage_name, "ICE1") || !strcmp(stage_name, "ICE2")) {
        set_zone(h, 0, u, 60, 75, d2);
        set_zone(h, 1, u, 100, 115, d2);
        set_zone(h, 2, u, 140, 155, d2);
    } else if (!strcmp(stage_name, "MOAT")) {
        set_zone(h, 0, u, 80, 100, d);
        set_zone(h, 1, u, 125, 145, d);
        set_zone(h, 2, u, 150, 158, d2);
    } else if (!strcmp(stage_name, "RIDE")) {
        set_zone(h, 0, u, 55, 70, d);
        set_zone(h, 1, u, 95, 110, d2);
        set_zone(h, 2, u, 140, 155, d);
    } else if (!strcmp(stage_name, "STAING")) {
        set_zone(h, 0, u, 65, 80, d);
        set_zone(h, 1, u, 105, 120, d);
        set_zone(h, 2, u, 145, 158, d2);
    } else {
        /* RUNWAY default (and unknown pan). */
        set_zone(h, 0, u, 120, 135, d);
        set_zone(h, 1, u, 175, 190, d);
        set_zone(h, 2, u, 230, 245, d2);
    }
    h->zone_count = RUNWAY_HAZARD_ZONES;
}

void runway_hazards_spawn_thrown(runway_hazards *h, uint32_t x, uint32_t y,
                                 int32_t damage) {
    unsigned i;
    if (!h) return;
    for (i = 0; i < RUNWAY_THROWN_MAX; i++) {
        if (h->thrown[i].active) continue;
        h->thrown[i].x = x;
        h->thrown[i].y = y;
        h->thrown[i].damage = damage > 0 ? damage : RUNWAY_DAMAGE_DEFAULT;
        h->thrown[i].active = 1;
        if (i + 1u > h->thrown_count)
            h->thrown_count = i + 1u;
        return;
    }
}

static int in_range(uint32_t a, uint32_t b, uint32_t range) {
    uint32_t d = (a > b) ? (a - b) : (b - a);
    return d <= range;
}

int runway_hazards_step(runway_hazards *h, runway_skel *skel, uint32_t bones_x) {
    unsigned i;
    if (!h || !skel || skel->died || skel->paused) return 0;

    for (i = 0; i < h->zone_count; i++) {
        if (bones_x >= h->zones[i].x0 && bones_x <= h->zones[i].x1) {
            if (runway_skel_hit(skel, h->zones[i].damage)) {
                h->hits_applied++;
                return 1;
            }
            if (skel->hit_cooldown == RUNWAY_HIT_TIMEOUT)
                h->hits_applied++;
            break; /* one zone hit attempt per frame */
        }
    }

    for (i = 0; i < RUNWAY_THROWN_MAX; i++) {
        if (!h->thrown[i].active) continue;
        if (!in_range(bones_x, h->thrown[i].x, RUNWAY_THROWN_RANGE)) continue;
        /* CheckForHit → BonesObjHitChk stand-in: proximity enough on host. */
        if (runway_skel_hit(skel, h->thrown[i].damage)) {
            h->hits_applied++;
            h->thrown[i].active = 0;
            return 1;
        }
        if (skel->hit_cooldown == RUNWAY_HIT_TIMEOUT) {
            h->hits_applied++;
            h->thrown[i].active = 0;
        }
    }
    return 0;
}
