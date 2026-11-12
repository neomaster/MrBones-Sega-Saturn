#ifndef MR_BONES_NATIVE_STATE_H
#define MR_BONES_NATIVE_STATE_H
#include <stdint.h>
typedef struct bones_state bones_state;
typedef struct bones_state_context bones_state_context;
struct bones_state {
    void (*enter)(bones_state_context *);
    void (*exit)(bones_state_context *);
};
struct bones_state_context {
    const bones_state *current, *last, *blocked;
    uint32_t hit_timeout;
    uint32_t previous_clock, current_clock, repeat_count;
    uint32_t position_x, position_y;
    void (*yield)(bones_state_context *);
    uint32_t latch_target, latch_position[3], latched;
    uint32_t velocity[3], acceleration[3];
};
/* Derived from RUNWAY _BonesChangeState (0x0025d484). */
int bones_change_state(bones_state_context *c, const bones_state *next);
void bones_first_state(bones_state_context *c, const bones_state *next);
void bones_set_latch(bones_state_context *c, uint32_t target, const uint32_t position[3]);
void bones_clear_latch(bones_state_context *c);
void bones_kill_velocity_acceleration(bones_state_context *c);

/*
 * NearVel_25B1C8 core (03CODE base 0x00232000):
 *   vel @0x2858FC += accel @0x285908
 *   then world @0x2858BC[+0/+4/+8] += vel
 * Order differs from CMover_Move_259D28 (pos then vel). Signed int32 on Saturn words.
 * wx/wy/wz may be NULL to skip an axis (host often gates X/Y with collision first).
 */
void bones_near_vel_integrate(int32_t vel[3], const int32_t accel[3]);
void bones_near_vel_apply(const int32_t vel[3], uint32_t *wx, uint32_t *wy, uint32_t *wz);
/* Integrate then apply — full core without projection/collision. */
void bones_near_vel(int32_t vel[3], const int32_t accel[3],
                    uint32_t *wx, uint32_t *wy, uint32_t *wz);
/* KillVelAccel_25E088 on freestanding vec3s (phase vel/accel cells). */
void bones_kill_vel_accel_vec(int32_t vel[3], int32_t accel[3]);
#endif
