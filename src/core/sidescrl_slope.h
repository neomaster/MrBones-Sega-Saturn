#ifndef MB_SIDESCRL_SLOPE_H
#define MB_SIDESCRL_SLOPE_H

/*
 * Slopes and steps: terrain given as a continuous elevation profile instead of
 * isolated boxes.
 *
 * Clean-room specification (C99, integer only, no allocation, no globals).
 * Depends on core/aabb_hitchk.h (BoundingBox) and core/sidescrl_physics.h
 * (SidescrlActor, gravity and fixed-point integration).
 *
 * Coordinates: pixels, +x right, +y DOWN. "Ground y" is the y of the terrain
 * surface; everything below it is solid. Let
 *
 *     feet = actor_box->y + actor_box->h
 *     d    = target_ground_y - feet          (> 0: ground is below the feet)
 *
 * The actor is in contact with the terrain when  -max_step_up <= d <= max_step_down:
 *
 *   d <  0, within max_step_up    step up: feet sink into the surface by -d
 *                                 pixels and are raised to the crest.
 *   d == 0                        flush.
 *   d >  0, within max_step_down  step down: the terrain fell away by d
 *                                 pixels and the feet follow it instead of the
 *                                 actor entering free fall.
 *   anything else                 no contact: a wall taller than max_step_up,
 *                                 or a drop deeper than max_step_down.
 *
 * Negative limits count as 0. SIDESCRL_SLOPE_NO_GROUND (no terrain at all,
 * e.g. an abyss) is never a contact, whatever the limits.
 *
 * Limit: this module resolves the vertical axis only. It does not stop
 * horizontal motion, so an actor that walks into a face taller than
 * max_step_up is simply no longer supported (and keeps falling through the
 * solid). Block horizontal motion with a separate wall resolver first.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/aabb_hitchk.h"
#include "core/sidescrl_physics.h"

#define SIDESCRL_SLOPE_NO_GROUND INT32_MAX

/* Terrain query: surface y at pixel column `column`, or SIDESCRL_SLOPE_NO_GROUND
 * when there is no ground there. `ctx` is passed through untouched. */
typedef int32_t (*SidescrlGroundFn)(const void *ctx, int32_t column);

/* True when the box is in contact with a surface at target_ground_y. NULL box
 * -> false. 64-bit arithmetic internally: no overflow for any int32_t input. */
bool sidescrl_slope_in_contact(const BoundingBox *actor_box,
                               int32_t target_ground_y, int32_t max_step_up,
                               int32_t max_step_down);

/*
 * Vertical displacement to ADD to the actor's y so its feet sit on
 * target_ground_y: negative = up (step up), positive = down (step down),
 * 0 = already flush OR no contact. Use sidescrl_slope_in_contact to tell those
 * two zeros apart. Pure and deterministic.
 */
int32_t sidescrl_resolve_slope(const BoundingBox *actor_box,
                               int32_t target_ground_y, int32_t max_step_up,
                               int32_t max_step_down);

/* Column the terrain is sampled at: the centre of the box, x + w / 2. */
int32_t sidescrl_slope_column(const BoundingBox *actor_box);

/*
 * One frame of an actor on sloped terrain. Runs sidescrl_physics_step with no
 * platforms (gravity with terminal speed, x += vx, y += vy), samples the
 * terrain under the new position and then:
 *
 *   rising (vy < 0)       airborne; the terrain is ignored.
 *   was on_ground         contact window [-max_step_up, +max_step_down]:
 *                         inside it the actor is moved onto the surface
 *                         (stick to slopes, climb steps); outside it the
 *                         actor loses contact and from then on only gravity
 *                         acts on it.
 *   was airborne, falling lands only by reaching the surface: the window is
 *                         [-max(max_step_up, SIDESCRL_PHYS_FLOOR_SNAP_PX), 0],
 *                         wide enough that a fast fall cannot skip the
 *                         surface, and never adheres to ground below.
 *
 * On contact: on_ground = true, vy = 0, jumping = false. Otherwise:
 * on_ground = false. `ground == NULL` means no terrain. As a rule of thumb
 * max_step_up must be at least the rise per frame of the steepest slope at the
 * fastest walking speed (45 degrees at 3 px/frame rises 3 px/frame).
 */
void sidescrl_slope_step(SidescrlActor *actor, SidescrlGroundFn ground,
                         const void *ctx, int32_t max_step_up,
                         int32_t max_step_down);

#endif
