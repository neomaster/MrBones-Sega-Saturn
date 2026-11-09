#include "runway_skel.h"

void runway_skel_init(runway_skel *s) {
    if (!s) return;
    s->skeletism = RUNWAY_SKEL_MAX;
    s->pump = 0;
    s->paused = 0;
    s->hit_cooldown = 0;
    s->died = 0;
    runway_skel_update_bar(s);
}

void runway_skel_update_bar(runway_skel *s) {
    int32_t v;
    int seg;
    if (!s) return;
    /* RealSkeletism stand-in = base + pump, clamped >= 0. */
    if (s->skeletism + s->pump < 0)
        s->skeletism = 0;
    v = s->skeletism;
    seg = 1;
    while (v > 0 && seg < RUNWAY_SKEL_SEGMENTS) {
        v += RUNWAY_SKEL_SEG_STEP;
        seg++;
    }
    s->segments = seg;
}

void runway_skel_add(runway_skel *s, int32_t amount) {
    if (!s || amount <= 0) return;
    s->skeletism += amount;
    if (s->skeletism > RUNWAY_SKEL_MAX)
        s->skeletism = RUNWAY_SKEL_MAX;
    runway_skel_update_bar(s);
}

int runway_skel_loose(runway_skel *s, int32_t amount) {
    if (!s || amount <= 0) return 0;
    s->skeletism -= amount;
    if (s->skeletism < 0) {
        s->skeletism = 0;
        s->pump = 0;
        s->died = 1;
        runway_skel_update_bar(s);
        return 1;
    }
    s->pump = 0;
    runway_skel_update_bar(s);
    return 0;
}

int runway_skel_hit(runway_skel *s, int32_t amount) {
    if (!s || s->died || s->paused) return 0;
    if (s->hit_cooldown > 0) return 0;
    s->hit_cooldown = RUNWAY_HIT_TIMEOUT;
    return runway_skel_loose(s, amount);
}

void runway_skel_tick_cooldown(runway_skel *s) {
    if (!s) return;
    if (s->hit_cooldown > 0)
        s->hit_cooldown--;
}

void runway_skel_set_paused(runway_skel *s, int paused) {
    if (!s) return;
    s->paused = paused ? 1 : 0;
}
