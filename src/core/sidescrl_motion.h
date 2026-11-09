#ifndef MB_SIDESCRL_MOTION_H
#define MB_SIDESCRL_MOTION_H

/*
 * Per-frame motion of the protagonist on a side-scrolling map: walking with
 * sub-pixel velocity, wall stops, jumping, slopes, steps and gravity composed
 * into one deterministic function.
 *
 * Clean-room specification (C99, integer only, no allocation, no globals). It
 * composes sidescrl_physics (fixed point, gravity), sidescrl_wall (horizontal
 * wall clamp) and sidescrl_slope (vertical terrain resolution).
 *
 * One frame, in this order:
 *
 *   a) Walk. walk_dir is reduced to -1 / 0 / +1. A non-zero direction sets
 *      facing_left. Only while on_ground, vx moves toward walk_dir *
 *      SIDESCRL_MOTION_WALK_MAX by SIDESCRL_MOTION_WALK_ACCEL per frame, or toward
 *      0 by SIDESCRL_MOTION_FRICTION per frame when walk_dir is 0. In the air vx
 *      is kept: there is no air control and no air friction.
 *   b) Horizontal move. The whole pixels of (rem_x + vx) are the desired dx; the
 *      fractional part stays in rem_x. The dx goes through sidescrl_wall_clamp_dx
 *      with a climb allowance of max_climb_step + R while on_ground and 0 in the
 *      air, where R is how far the ground under the leading column of the box is
 *      above the feet (0 when it is level or lower), capped so that the top row of
 *      the box always keeps blocking: R <= height - 1 - max_climb_step.
 *      While on_ground (and is_wall is not NULL) the move is then shortened again
 *      until the ground under the CENTRE column after the move is at most
 *      max_climb_step above the feet: the slope resolver can follow exactly that
 *      much, a taller rise would make the actor lose contact and sink into the
 *      solid. If either limit cut the move, vx and rem_x are zeroed. x += the
 *      allowed dx.
 *   c) Jump. If jump_edge and the actor is on_ground: vy = JUMP_IMPULSE,
 *      on_ground = false, jumping = true. A jump_edge in the air does nothing.
 *   d) Vertical move. sidescrl_slope_step with max_step_up = max_step_down =
 *      max_climb_step: gravity, y integration, landing, stepping up and down.
 *
 * Both callbacks must describe the same terrain: is_wall(col, row) is true for
 * a solid pixel, get_ground(col) is the y of the highest solid pixel in that
 * column (SIDESCRL_SLOPE_NO_GROUND when the column is empty). The terrain is
 * sampled at the centre column of the actor for the ground, per pixel for walls.
 *
 * Notes on how this differs from a naive composition:
 *   - The slope resolver puts the feet at the ground under the CENTRE column, so on
 *     a rising ramp the box's leading edge is already above the feet. Passing
 *     max_climb_step alone to the wall clamp turns a 45 degree ramp into a wall that
 *     stops the actor at its foot. R above removes that: obstacle heights count from
 *     the ground the leading edge stands on. A cliff face still blocks, because it
 *     is tall relative to that ground too.
 *   - sidescrl_slope_step also integrates x with vx (through sidescrl_physics_
 *     step). Step b) already moved x, so vx is held at 0 during step d) and then
 *     restored; otherwise the actor would move twice and slip through walls.
 *   - max_step_down is not a parameter here; it equals max_climb_step.
 *   - The box is derived from the pose (sidescrl_actor_box); there is no stored
 *     box to update.
 *
 * Limits:
 *   - Steepness, in rows per column, with the default max_climb_step of 6 and the
 *     default walk speed (3 px/frame). Up to 2 keeps full speed (the limit for full
 *     speed is max_climb_step divided by the pixels moved per frame). 3 is climbed
 *     at no more than 2 px/frame and 4 at no more than 1, because every cut zeroes
 *     vx and the actor accelerates again. 5 or more is a wall. 4 depends on the side:
 *     the centre column is x + w / 2, which sits 9 columns behind the right edge
 *     but 10 behind the left one, so 4 is climbed to the right and is a wall to
 *     the left. Only up to 3 is guaranteed in both directions.
 *   - Going down, the contact window is max_climb_step per frame. A downslope that
 *     drops more per frame than that (steeper than 2 rows per column at full
 *     speed) is taken as a fall: the actor goes airborne, keeps vx and lands.
 *   - The air uses a climb allowance of 0, so a jump that lands sideways into
 *     a slope loses its horizontal speed at the first rising column.
 *   - With is_wall == NULL there is no wall clamp and no follow limit: an actor
 *     can walk into a face and sink through it. Pass a real is_wall.
 *   - A speed above SIDESCRL_WALL_MAX_SCAN pixels per frame counts as hitting a
 *     wall (the clamp is bounded); it cannot occur with the walk constants.
 *   - The constants below are placeholders, not recovered from the original
 *     binary. Override them at compile time if needed.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/sidescrl_physics.h"
#include "core/sidescrl_slope.h"
#include "core/sidescrl_wall.h"

#ifndef SIDESCRL_MOTION_WALK_MAX
#define SIDESCRL_MOTION_WALK_MAX (3 * SIDESCRL_PHYS_UNITS_PER_PIXEL)
#endif
#ifndef SIDESCRL_MOTION_WALK_ACCEL
#define SIDESCRL_MOTION_WALK_ACCEL SIDESCRL_PHYS_UNITS_PER_PIXEL
#endif
#ifndef SIDESCRL_MOTION_FRICTION
#define SIDESCRL_MOTION_FRICTION SIDESCRL_PHYS_UNITS_PER_PIXEL
#endif
/* Negative is up. About 6 px/frame: a jump of roughly 90 px with the default gravity. */
#ifndef SIDESCRL_MOTION_JUMP_IMPULSE
#define SIDESCRL_MOTION_JUMP_IMPULSE (-6 * SIDESCRL_PHYS_UNITS_PER_PIXEL)
#endif

/*
 * Advance the actor by one frame. walk_dir: -1 left, 0 still, +1 right (any
 * other value counts by its sign). jump_edge: true only on the frame the jump
 * was pressed. A NULL actor is ignored. A NULL is_wall means no walls; a NULL
 * get_ground means no terrain (the actor falls).
 */
void sidescrl_motion_step(SidescrlActor *actor, int32_t walk_dir, bool jump_edge,
                          int32_t max_climb_step, SidescrlWallFn is_wall,
                          SidescrlGroundFn get_ground, void *map_ctx);

#endif
