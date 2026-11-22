#include "core/sidescrl_slope.h"

static int32_t clamp_i32(int64_t v) {
    if (v > INT32_MAX)
        return INT32_MAX;
    if (v < INT32_MIN)
        return INT32_MIN;
    return (int32_t)v;
}

static int64_t limit(int32_t v) {
    return v > 0 ? (int64_t)v : 0;
}

/* Distance from the feet to the surface, if the surface is in contact range. */
static bool contact_delta(const BoundingBox *b, int32_t target, int32_t up,
                          int32_t down, int32_t *dy) {
    int64_t d;

    if (b == NULL || target == SIDESCRL_SLOPE_NO_GROUND)
        return false;
    d = (int64_t)target - ((int64_t)b->y + (int64_t)b->h);
    if (d < -limit(up) || d > limit(down))
        return false;
    if (dy != NULL)
        *dy = (int32_t)d; /* |d| <= INT32_MAX by the range check above */
    return true;
}

bool sidescrl_slope_in_contact(const BoundingBox *actor_box,
                               int32_t target_ground_y, int32_t max_step_up,
                               int32_t max_step_down) {
    return contact_delta(actor_box, target_ground_y, max_step_up,
                         max_step_down, NULL);
}

int32_t sidescrl_resolve_slope(const BoundingBox *actor_box,
                               int32_t target_ground_y, int32_t max_step_up,
                               int32_t max_step_down) {
    int32_t dy = 0;

    if (!contact_delta(actor_box, target_ground_y, max_step_up, max_step_down,
                       &dy))
        return 0;
    return dy;
}

int32_t sidescrl_slope_column(const BoundingBox *actor_box) {
    if (actor_box == NULL)
        return 0;
    return clamp_i32((int64_t)actor_box->x + actor_box->w / 2);
}

void sidescrl_slope_step(SidescrlActor *actor, SidescrlGroundFn ground,
                         const void *ctx, int32_t max_step_up,
                         int32_t max_step_down) {
    bool was_grounded;
    int32_t old_y;
    BoundingBox box;
    int32_t target, up, down, dy = 0;

    if (actor == NULL)
        return;
    old_y = actor->y;
    was_grounded = actor->on_ground;

    /* Query from the old feet before integration can cross a thin floor. */
    box = sidescrl_actor_box(actor);
    target = ground ? ground(ctx, sidescrl_slope_column(&box)) : SIDESCRL_SLOPE_NO_GROUND;
    /* Gravity and x/y integration; leaves on_ground false. */
    sidescrl_physics_step(actor, NULL, 0);
    if (ground == NULL || actor->vy < 0) {
        actor->on_ground = false;
        return;
    }

    BoundingBox moved = sidescrl_actor_box(actor);
    if (moved.x != box.x) target = ground(ctx, sidescrl_slope_column(&moved));
    box = moved;

    if (was_grounded) {
        up = max_step_up;
        down = max_step_down;
    } else {
        up = max_step_up > SIDESCRL_PHYS_FLOOR_SNAP_PX
                 ? max_step_up
                 : (int32_t)SIDESCRL_PHYS_FLOOR_SNAP_PX;
        if ((int64_t)actor->y - old_y > up) up = clamp_i32((int64_t)actor->y - old_y);
        down = 0;
    }

    if (contact_delta(&box, target, up, down, &dy)) {
        actor->y = clamp_i32((int64_t)actor->y + dy);
        actor->vy = 0;
        actor->rem_y = 0;
        actor->on_ground = true;
        actor->jumping = false;
    } else {
        actor->on_ground = false;
    }
}
