#include "bones_state.h"
/* RUNWAY _BonesFirstState (0x0025d460). */
void bones_first_state(bones_state_context *c, const bones_state *next) {
    c->last = 0;
    c->current = next;
    next->enter(c);
}

/* RUNWAY _BonesSetLatch (0x0025c0c4); fields are 32-bit quantities. */
void bones_set_latch(bones_state_context *c, uint32_t target, const uint32_t position[3]) {
    c->latch_target = target;
    c->latch_position[0] = position[0];
    c->latch_position[1] = position[1];
    c->latch_position[2] = position[2];
    c->latched = 1;
}

/* RUNWAY _BonesClearLatch (0x0025c0f4) retains the stored target and vector. */
void bones_clear_latch(bones_state_context *c) {
    c->latched = 0;
}

/* RUNWAY _BonesKillVelAccel (0x0025e088). */
void bones_kill_velocity_acceleration(bones_state_context *c) {
    for (unsigned i = 0; i < 3; ++i) {
        c->acceleration[i] = 0;
        c->velocity[i] = 0;
    }
}

int bones_change_state(bones_state_context *c, const bones_state *next) {
    if (next == c->blocked && c->hit_timeout != 0) return 0;
    c->current->exit(c);
    if (c->previous_clock == c->current_clock) {
        uint32_t old_count = c->repeat_count++;
        if (old_count > 16 && old_count <= INT32_MAX) {
            c->position_x += 655;
            c->position_y -= 655;
            c->repeat_count = 0;
            c->yield(c);
        }
    } else {
        c->previous_clock = c->current_clock;
        c->repeat_count = 0;
    }
    c->last = c->current;
    c->current = next;
    next->enter(c);
    return 1;
}
