#ifndef MB_SIDESCRL_PHYSICS_H
#define MB_SIDESCRL_PHYSICS_H

/*
 * Side-scrolling platform physics: vertical/horizontal movement of the
 * protagonist resolved against solid platforms with aabb_hitchk.
 *
 * Clean-room specification (C99; depends only on core/aabb_hitchk.h; no
 * allocation, no globals, no floating point). The step function is
 * deterministic: its result depends only on its arguments.
 *
 * Coordinates: pixels, +x right, +y DOWN. Velocities and the sub-pixel
 * remainders are in fixed-point "units"; one pixel is
 * SIDESCRL_PHYS_UNITS_PER_PIXEL units (same scale as the phase modules).
 *
 * Actor origin: (x, y) is the top-left corner of the STANDING box. Every pose
 * is anchored at the feet, i.e. the bottom edge is always y + 42, so changing
 * pose never moves the feet:
 *
 *   pose   width  height  box top
 *   STAND   20     42     y
 *   DUCK    20     24     y + 18
 *   JUMP    20     36     y + 6
 *
 * The +18 for DUCK is the specified compensation; +6 for JUMP follows from the
 * same feet-anchoring rule.
 *
 * What one step does, in order:
 *   1. vy += GRAVITY_ACCEL, capped at MAX_FALL_SPEED.
 *   2. x += vx, y += vy (fixed point, remainder carried, saturating).
 *   3. Floor contact: hitchk_ground_clamp against the platforms.
 *        lift < 0   -> y += lift, vy = 0, on_ground = true, jumping = false.
 *        lift == 0  -> on_ground = false, unless the actor is resting exactly
 *                      on a surface (see below).
 *
 * Resting contact: when gravity is applied at rest, the actor often moves by
 * less than one whole pixel, so it does not penetrate the floor and lift is 0
 * on most frames. Reading that as "falling" would flip on_ground every few
 * frames. To keep a landing stable the step also probes one pixel below the
 * feet: if that probe would hit a platform and the actor is not rising
 * (vy >= 0), it is resting, so on_ground stays true and vy is zeroed.
 *
 * Limits (deliberate, to keep this module small):
 *   - Floor only. Walls and ceilings are not resolved here; a block whose top
 *     edge is more than SIDESCRL_PHYS_FLOOR_SNAP_PX above the feet is not
 *     climbed. Use a separate horizontal resolver for walls.
 *   - A rising actor (vy < 0) that ends the step within FLOOR_SNAP_PX inside a
 *     platform's top edge is landed like a falling one (step-up).
 *   - Gravity and terminal speed are placeholders, not values recovered from
 *     the original binary. Override them at compile time if needed.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/aabb_hitchk.h"

#define SIDESCRL_PHYS_UNITS_PER_PIXEL 2341

#ifndef SIDESCRL_PHYS_GRAVITY_ACCEL
#define SIDESCRL_PHYS_GRAVITY_ACCEL 450 /* units per frame^2 (~0.19 px) */
#endif
#ifndef SIDESCRL_PHYS_MAX_FALL_SPEED
#define SIDESCRL_PHYS_MAX_FALL_SPEED (8 * SIDESCRL_PHYS_UNITS_PER_PIXEL)
#endif

/* Largest correction treated as a floor contact. Derived from the terminal
 * speed (whole pixels moved per frame, rounded up) plus one pixel of margin,
 * so a fast fall can never skip over a platform. */
#define SIDESCRL_PHYS_FLOOR_SNAP_PX                                          \
    ((SIDESCRL_PHYS_MAX_FALL_SPEED + SIDESCRL_PHYS_UNITS_PER_PIXEL - 1) /    \
         SIDESCRL_PHYS_UNITS_PER_PIXEL + 1)

#define SIDESCRL_ACTOR_WIDTH 20
#define SIDESCRL_STAND_HEIGHT 42
#define SIDESCRL_DUCK_HEIGHT 24
#define SIDESCRL_JUMP_HEIGHT 36

/* Layer bits. The collision gate is (mask_a & mask_b) != 0, so the actor must
 * carry the SOLID bit to collide with platforms built on the SOLID layer. */
#define SIDESCRL_PHYS_LAYER_SOLID 0x0001u
#define SIDESCRL_PHYS_LAYER_ACTOR 0x0100u

typedef enum {
    SIDESCRL_POSE_STAND = 0,
    SIDESCRL_POSE_DUCK = 1,
    SIDESCRL_POSE_JUMP = 2,
    SIDESCRL_POSE_COUNT = 3
} SidescrlPose;

typedef struct {
    int32_t x, y;        /* top-left of the STANDING box, pixels */
    int32_t vx, vy;      /* fixed-point units per frame */
    int32_t rem_x, rem_y; /* sub-pixel remainders, 0..UNITS_PER_PIXEL-1 */
    uint16_t layer_mask; /* collision layers, see SIDESCRL_PHYS_LAYER_* */
    SidescrlPose pose;
    bool on_ground;
    bool facing_left;    /* owned by the caller; the step never touches it */
    bool jumping;        /* cleared on landing; set by the caller */
} SidescrlActor;

/* Zero velocity, STAND pose, layers ACTOR|SOLID, airborne until the first step
 * proves otherwise. NULL is ignored. */
void sidescrl_actor_init(SidescrlActor *actor, int32_t x, int32_t y);

/* Collision box for the actor's current pose. An invalid pose counts as STAND.
 * Coordinates saturate at the int32_t limits. */
BoundingBox sidescrl_actor_box(const SidescrlActor *actor);

/*
 * Switch pose. Shrinking (STAND->DUCK, STAND->JUMP, JUMP->DUCK) always
 * succeeds. Growing upward (DUCK->STAND, DUCK->JUMP, JUMP->STAND) needs
 * headroom: it is refused when the new box would overlap a platform whose
 * layer matches the actor's. The feet never move. Returns true when the actor
 * ends up in the requested pose, false when refused or when `pose` is invalid.
 */
bool sidescrl_actor_set_pose(SidescrlActor *actor, SidescrlPose pose,
                             const Collider *platforms, size_t count);

/* Advance one frame; see the file comment. NULL actor is ignored; NULL
 * platforms with count > 0 behave as "no platforms". */
void sidescrl_physics_step(SidescrlActor *actor, const Collider *platforms,
                           size_t count);

#endif
