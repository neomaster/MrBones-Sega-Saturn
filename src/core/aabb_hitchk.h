#ifndef MB_AABB_HITCHK_H
#define MB_AABB_HITCHK_H

/*
 * Subsystem B -- AABB overlap test, layer filtering and ground contact.
 *
 * Clean-room interoperability specification (C99, no third-party code, no
 * system calls). Every function is pure and deterministic: no globals, no
 * allocation, no floating point. This module is a specification of the
 * algorithm, not a port of the original HitChk code, and it is not wired into
 * the existing pan_collision_map path.
 *
 * Coordinate system: integer pixels, +x right, +y DOWN (screen space).
 * A box covers the half-open ranges [x, x + w) and [y, y + h), so two boxes
 * that merely share an edge do NOT intersect. Boxes with w <= 0 or h <= 0 are
 * empty and never intersect anything. Edge sums are computed in 64 bits, so
 * any int32_t box is safe.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int32_t x; /* left edge (inclusive)  */
    int32_t y; /* top edge (inclusive)   */
    int32_t w; /* width in pixels        */
    int32_t h; /* height in pixels       */
} BoundingBox;

/* A box plus the layer bits it belongs to. */
typedef struct {
    BoundingBox box;
    uint16_t layer_mask;
} Collider;

/* True when the two boxes share at least one pixel. NULL -> false. */
bool aabb_intersect(const BoundingBox *a, const BoundingBox *b);

/* Collision matrix gate: the geometric test runs only when the masks share at
 * least one bit, i.e. (mask_a & mask_b) != 0. */
bool hitchk_layers_match(uint16_t mask_a, uint16_t mask_b);

/* Layer gate first, geometry second. NULL -> false. */
bool hitchk_overlap(const Collider *a, const Collider *b);

/*
 * Floor contact against one solid block.
 *
 * Returns the smallest vertical displacement to ADD to actor->y so the actor's
 * bottom edge sits on the block's top edge. The result is <= 0 (upwards in
 * +y-down space); 0 means no correction. The actor must overlap the block and
 * the required lift must not exceed `max_snap`; a deeper overlap is treated as
 * a wall or ceiling hit, not a floor contact, and left to other resolvers.
 * `max_snap <= 0` disables the clamp; pass INT32_MAX for "no limit".
 * Only upward resolution is performed, so an actor is never pushed down
 * through a platform.
 */
int32_t ground_clamp(const BoundingBox *actor, const BoundingBox *ground,
                     int32_t max_snap);

/*
 * Layer-filtered floor contact against `count` terrain colliders.
 * Terrain whose layer does not match the actor is skipped. Returns the largest
 * single lift (most negative value) that any matching block requires, which
 * clears every block the actor overlapped at the start of the call. One pass
 * only: after applying the result the caller should re-test, since a block
 * that did not overlap before may overlap after the move.
 */
int32_t hitchk_ground_clamp(const Collider *actor, const Collider *terrain,
                            size_t count, int32_t max_snap);

#endif
