#include "core/sidescrl_physics.h"

static int32_t clamp_i32(int64_t v) {
    if (v > INT32_MAX)
        return INT32_MAX;
    if (v < INT32_MIN)
        return INT32_MIN;
    return (int32_t)v;
}

static int32_t pose_height(SidescrlPose pose) {
    switch (pose) {
    case SIDESCRL_POSE_DUCK:
        return SIDESCRL_DUCK_HEIGHT;
    case SIDESCRL_POSE_JUMP:
        return SIDESCRL_JUMP_HEIGHT;
    case SIDESCRL_POSE_STAND:
    default:
        return SIDESCRL_STAND_HEIGHT;
    }
}

static Collider actor_collider(const SidescrlActor *a) {
    Collider c;
    c.box = sidescrl_actor_box(a);
    c.layer_mask = a->layer_mask;
    return c;
}

/* pos += vel / UNITS_PER_PIXEL, carrying the fractional part in *rem. The
 * remainder is always in 0..UNITS_PER_PIXEL-1 afterwards (floor division, so
 * negative velocities behave symmetrically). */
static void move_axis(int32_t *pos, int32_t *rem, int32_t vel) {
    const int64_t unit = SIDESCRL_PHYS_UNITS_PER_PIXEL;
    int64_t total = (int64_t)*rem + (int64_t)vel;
    int64_t whole = total / unit;
    int64_t frac = total % unit;

    if (frac < 0) {
        frac += unit;
        whole -= 1;
    }
    *rem = (int32_t)frac;
    *pos = clamp_i32((int64_t)*pos + whole);
}

/* True when the actor stands exactly on a platform: one pixel lower it would
 * overlap by one pixel. Only meaningful when the actor is not penetrating. */
static bool has_support(const SidescrlActor *a, const Collider *platforms,
                        size_t count) {
    Collider probe = actor_collider(a);

    probe.box.y = clamp_i32((int64_t)probe.box.y + 1);
    return hitchk_ground_clamp(&probe, platforms, count,
                               SIDESCRL_PHYS_FLOOR_SNAP_PX) < 0;
}

void sidescrl_actor_init(SidescrlActor *actor, int32_t x, int32_t y) {
    if (actor == NULL)
        return;
    actor->x = x;
    actor->y = y;
    actor->vx = 0;
    actor->vy = 0;
    actor->rem_x = 0;
    actor->rem_y = 0;
    actor->layer_mask = (uint16_t)(SIDESCRL_PHYS_LAYER_ACTOR |
                                   SIDESCRL_PHYS_LAYER_SOLID);
    actor->pose = SIDESCRL_POSE_STAND;
    actor->on_ground = false;
    actor->facing_left = false;
    actor->jumping = false;
}

BoundingBox sidescrl_actor_box(const SidescrlActor *actor) {
    BoundingBox b;
    int32_t height;

    if (actor == NULL) {
        b.x = 0;
        b.y = 0;
        b.w = 0;
        b.h = 0;
        return b;
    }
    height = pose_height(actor->pose);
    b.x = actor->x;
    b.y = clamp_i32((int64_t)actor->y + (SIDESCRL_STAND_HEIGHT - height));
    b.w = SIDESCRL_ACTOR_WIDTH;
    b.h = height;
    return b;
}

bool sidescrl_actor_set_pose(SidescrlActor *actor, SidescrlPose pose,
                             const Collider *platforms, size_t count) {
    SidescrlActor next;
    BoundingBox old_box, new_box;
    size_t i;

    if (actor == NULL || (unsigned)pose >= (unsigned)SIDESCRL_POSE_COUNT)
        return false;
    if (actor->pose == pose)
        return true;

    next = *actor;
    next.pose = pose;
    old_box = sidescrl_actor_box(actor);
    new_box = sidescrl_actor_box(&next);

    /* Same feet, so only a box that grows upward can run into something. */
    if (new_box.y < old_box.y && platforms != NULL) {
        for (i = 0; i < count; i++) {
            if (hitchk_layers_match(actor->layer_mask, platforms[i].layer_mask) &&
                aabb_intersect(&new_box, &platforms[i].box))
                return false;
        }
    }
    actor->pose = pose;
    return true;
}

void sidescrl_physics_step(SidescrlActor *actor, const Collider *platforms,
                           size_t count) {
    Collider self;
    int64_t vy;
    int32_t lift;

    if (actor == NULL)
        return;

    /* 1. gravity with terminal speed */
    vy = (int64_t)actor->vy + SIDESCRL_PHYS_GRAVITY_ACCEL;
    if (vy > SIDESCRL_PHYS_MAX_FALL_SPEED)
        vy = SIDESCRL_PHYS_MAX_FALL_SPEED;
    actor->vy = (int32_t)vy;

    /* 2. integrate */
    move_axis(&actor->x, &actor->rem_x, actor->vx);
    move_axis(&actor->y, &actor->rem_y, actor->vy);

    /* 3. floor contact */
    self = actor_collider(actor);
    lift = hitchk_ground_clamp(&self, platforms, count,
                               SIDESCRL_PHYS_FLOOR_SNAP_PX);
    if (lift < 0) {
        actor->y = clamp_i32((int64_t)actor->y + lift);
        actor->vy = 0;
        actor->rem_y = 0;
        actor->on_ground = true;
        actor->jumping = false;
    } else if (actor->vy >= 0 && has_support(actor, platforms, count)) {
        actor->vy = 0;
        actor->rem_y = 0;
        actor->on_ground = true;
        actor->jumping = false;
    } else {
        actor->on_ground = false;
    }
}
