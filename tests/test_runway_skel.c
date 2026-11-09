#include "core/runway_hazard.h"
#include "core/runway_skel.h"
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void) {
    runway_skel s;
    runway_hazards h;

    runway_skel_init(&s);
    CHECK(s.skeletism == RUNWAY_SKEL_MAX);
    CHECK(s.segments == RUNWAY_SKEL_SEGMENTS);
    CHECK(s.paused == 0 && s.died == 0);

    runway_skel_add(&s, 100);
    CHECK(s.skeletism == RUNWAY_SKEL_MAX); /* clamp */

    CHECK(runway_skel_loose(&s, 0x4000) == 0);
    CHECK(s.skeletism == RUNWAY_SKEL_MAX - 0x4000);
    CHECK(s.pump == 0);

    /* Death → EndLevel(1) stand-in. */
    CHECK(runway_skel_loose(&s, RUNWAY_SKEL_MAX) == 1);
    CHECK(s.died == 1 && s.skeletism == 0);

    runway_skel_init(&s);
    CHECK(runway_skel_hit(&s, RUNWAY_DAMAGE_DEFAULT) == 0);
    CHECK(s.hit_cooldown == RUNWAY_HIT_TIMEOUT);
    CHECK(runway_skel_hit(&s, RUNWAY_DAMAGE_DEFAULT) == 0); /* cooldown blocks */
    CHECK(s.skeletism == RUNWAY_SKEL_MAX - RUNWAY_DAMAGE_DEFAULT);
    for (int i = 0; i < RUNWAY_HIT_TIMEOUT; i++)
        runway_skel_tick_cooldown(&s);
    CHECK(s.hit_cooldown == 0);

    runway_skel_set_paused(&s, 1);
    CHECK(s.paused == 1);
    CHECK(runway_skel_hit(&s, RUNWAY_DAMAGE_DEFAULT) == 0); /* paused ignores */
    runway_skel_set_paused(&s, 0);

    /* Hazards: zone at 120–135 px with u=2341. */
    runway_hazards_init_runway(&h, 2341u);
    CHECK(h.zone_count == 3u);
    runway_skel_init(&s);
    CHECK(runway_hazards_step(&h, &s, 100u * 2341u) == 0); /* outside */
    CHECK(h.hits_applied == 0u);
    CHECK(runway_hazards_step(&h, &s, 125u * 2341u) == 0);
    CHECK(h.hits_applied == 1u);
    CHECK(s.skeletism == RUNWAY_SKEL_MAX - RUNWAY_DAMAGE_DEFAULT);

    runway_hazards_spawn_thrown(&h, 50u * 2341u, 0, RUNWAY_DAMAGE_DEFAULT);
    for (int i = 0; i < RUNWAY_HIT_TIMEOUT; i++)
        runway_skel_tick_cooldown(&s);
    CHECK(runway_hazards_step(&h, &s, 50u * 2341u) == 0);
    CHECK(h.hits_applied == 2u);

    /* Drain to death via repeated hits. */
    runway_skel_init(&s);
    runway_hazards_init_runway(&h, 2341u);
    {
        int dead = 0;
        for (int n = 0; n < 32 && !dead; n++) {
            for (int i = 0; i < RUNWAY_HIT_TIMEOUT; i++)
                runway_skel_tick_cooldown(&s);
            dead = runway_hazards_step(&h, &s, 125u * 2341u);
        }
        CHECK(dead == 1);
        CHECK(s.died == 1);
    }

    /* §22: per-stage hazard layouts for all 7 pan stages. */
    {
        const char *stages[] = {"RUNWAY", "LOGS", "ICE1", "ICE2", "MOAT", "RIDE",
                                "STAING"};
        unsigned i;
        for (i = 0; i < sizeof stages / sizeof stages[0]; i++) {
            runway_hazards_init_pan_stage(&h, stages[i], 2341u);
            CHECK(h.zone_count == 3u);
            CHECK(h.zones[0].x1 > h.zones[0].x0);
        }
        /* LOGS zones 70–85 / 110–125 / 145–155; 50 is outside, 75 hits. */
        runway_hazards_init_pan_stage(&h, "LOGS", 2341u);
        runway_skel_init(&s);
        CHECK(runway_hazards_step(&h, &s, 50u * 2341u) == 0);
        CHECK(h.hits_applied == 0u);
        CHECK(runway_hazards_step(&h, &s, 75u * 2341u) == 0);
        CHECK(h.hits_applied == 1u);
    }

    printf("OK runway_skel/hazard\n");
    return 0;
}
