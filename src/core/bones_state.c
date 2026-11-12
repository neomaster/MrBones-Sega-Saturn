#include "bones_state.h"
#include <stddef.h>

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

void bones_kill_vel_accel_vec(int32_t vel[3], int32_t accel[3]) {
    unsigned i;
    if (!vel || !accel) return;
    for (i = 0; i < 3; ++i) {
        accel[i] = 0;
        vel[i] = 0;
    }
}

/* NearVel_25B1C8: vel += accel (cells 0x2858FC / 0x285908). */
void bones_near_vel_integrate(int32_t vel[3], const int32_t accel[3]) {
    unsigned i;
    if (!vel || !accel) return;
    for (i = 0; i < 3; ++i)
        vel[i] += accel[i];
}

/* NearVel world write: *0x2858BC[+0/+4/+8] += vel. */
void bones_near_vel_apply(const int32_t vel[3], uint32_t *wx, uint32_t *wy, uint32_t *wz) {
    if (!vel) return;
    if (wx) *wx = (uint32_t)((int32_t)*wx + vel[0]);
    if (wy) *wy = (uint32_t)((int32_t)*wy + vel[1]);
    if (wz) *wz = (uint32_t)((int32_t)*wz + vel[2]);
}

void bones_near_vel(int32_t vel[3], const int32_t accel[3],
                    uint32_t *wx, uint32_t *wy, uint32_t *wz) {
    bones_near_vel_integrate(vel, accel);
    bones_near_vel_apply(vel, wx, wy, wz);
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
