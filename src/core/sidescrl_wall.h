#ifndef MB_SIDESCRL_WALL_H
#define MB_SIDESCRL_WALL_H

/*
 * Horizontal wall collision for side-scrolling platform phases: the piece that
 * sidescrl_slope.h leaves out. It limits a desired horizontal displacement so
 * the actor stops flush against a wall, and lets it walk into low obstacles
 * that the slope resolver will then step up.
 *
 * Clean-room specification (C99, integer only, no allocation, no globals,
 * deterministic). The terrain is queried pixel by pixel through a callback.
 *
 * Coordinates: pixels, +x right, +y DOWN. The box covers columns
 * [x, x + w) and rows [y, y + h). With feet = y + h:
 *
 *   climbable rows   [feet - max_climb_step, feet)    the bottom max_climb_step
 *                                                     rows: a solid pixel here
 *                                                     is a step, not a wall
 *   blocking rows    [y, feet - max_climb_step)       a solid pixel here in a
 *                                                     column the actor would
 *                                                     enter stops it
 *
 * Moving right the columns entered are x + w, x + w + 1, ... ; moving left they
 * are x - 1, x - 2, ... . The result is the number of whole columns, up to
 * |desired_dx|, that are free of solid pixels in the blocking rows, taken in
 * order and stopping at the first blocked column, signed like desired_dx. The
 * actor therefore ends flush against the wall face.
 *
 * Obstacle height is measured from the CURRENT feet. Pair this with
 * sidescrl_slope: pass the same value as max_step_up, so a face the wall
 * resolver lets through is exactly one the slope resolver can climb. A
 * ramp that rises one pixel per column is therefore passable for at most
 * max_climb_step columns in one call.
 *
 * Pixels below the feet (the floor) are never queried. A step as tall as the
 * actor (max_climb_step >= h) can never block. A negative max_climb_step counts
 * as 0. The exemption applies whether or not the actor is on the ground: pass 0
 * while airborne if low ledges should stop a jumping actor too.
 *
 * Limits, so the cost of a call is bounded:
 *   - |desired_dx| is first limited to SIDESCRL_WALL_MAX_SCAN pixels.
 *   - A box taller than SIDESCRL_WALL_MAX_SCAN is not supported: result 0.
 *   - Cost is at most |dx| * (h - max_climb_step) callback calls, and the scan
 *     stops at the first blocked column.
 * Pixels outside the int32_t range do not exist: a column there blocks the
 * movement (world edge), a row there is ignored.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/aabb_hitchk.h"

#define SIDESCRL_WALL_MAX_SCAN 1024

/* True when the pixel at (col_x, row_y) is solid wall. `ctx` is passed through. */
typedef bool (*SidescrlWallFn)(void *ctx, int32_t col_x, int32_t row_y);

/*
 * Largest horizontal displacement, no larger in magnitude than desired_dx and
 * with the same sign, that does not push the actor into a wall. Returns 0 for
 * desired_dx == 0, a NULL actor, a blocked first column or an unsupported box.
 * A NULL is_solid_wall means no walls; an empty box (w <= 0 or h <= 0) has
 * nothing to collide with. In both cases desired_dx is returned as is, after
 * the SIDESCRL_WALL_MAX_SCAN limit.
 */
int32_t sidescrl_wall_clamp_dx(const BoundingBox *actor, int32_t desired_dx,
                               int32_t max_climb_step,
                               SidescrlWallFn is_solid_wall, void *ctx);

#endif
