#include "core/aabb_hitchk.h"

static bool box_is_empty(const BoundingBox *b) {
    return b->w <= 0 || b->h <= 0;
}

bool aabb_intersect(const BoundingBox *a, const BoundingBox *b) {
    int64_t a_right, a_bottom, b_right, b_bottom;

    if (a == NULL || b == NULL)
        return false;
    if (box_is_empty(a) || box_is_empty(b))
        return false;

    a_right = (int64_t)a->x + a->w;
    a_bottom = (int64_t)a->y + a->h;
    b_right = (int64_t)b->x + b->w;
    b_bottom = (int64_t)b->y + b->h;

    return (int64_t)a->x < b_right && (int64_t)b->x < a_right &&
           (int64_t)a->y < b_bottom && (int64_t)b->y < a_bottom;
}

bool hitchk_layers_match(uint16_t mask_a, uint16_t mask_b) {
    return (mask_a & mask_b) != 0;
}

bool hitchk_overlap(const Collider *a, const Collider *b) {
    if (a == NULL || b == NULL)
        return false;
    if (!hitchk_layers_match(a->layer_mask, b->layer_mask))
        return false;
    return aabb_intersect(&a->box, &b->box);
}

int32_t ground_clamp(const BoundingBox *actor, const BoundingBox *ground,
                     int32_t max_snap) {
    int64_t depth;

    if (!aabb_intersect(actor, ground))
        return 0;
    /* Overlap implies actor bottom > ground top, so depth >= 1. */
    depth = (int64_t)actor->y + actor->h - ground->y;
    if (depth <= 0 || depth > max_snap)
        return 0;
    return (int32_t)-depth;
}

int32_t hitchk_ground_clamp(const Collider *actor, const Collider *terrain,
                            size_t count, int32_t max_snap) {
    int32_t best = 0;
    size_t i;

    if (actor == NULL || terrain == NULL)
        return 0;
    for (i = 0; i < count; i++) {
        int32_t lift;
        if (!hitchk_layers_match(actor->layer_mask, terrain[i].layer_mask))
            continue;
        lift = ground_clamp(&actor->box, &terrain[i].box, max_snap);
        if (lift < best)
            best = lift;
    }
    return best;
}
