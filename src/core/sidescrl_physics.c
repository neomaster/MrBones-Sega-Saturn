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

int32_t sidescrl_project_impulse(const SidescrlActor *actor, int32_t world_impulse) {
    return clamp_i32((int64_t)world_impulse * actor->projection_px / 28);
}

void sidescrl_actor_init(SidescrlActor *actor, int32_t x, int32_t y) {
    if (actor == NULL)
        return;
    actor->spring_held = false;
    actor->spring_ticks = 0;
    actor->jump_impulse = BONES_SPRING_IMPULSE;
    actor->walk_speed = SIDESCRL_MOTION_WALK_MAX;
    actor->boost_duration = BONES_SPRING_TICKS;
    actor->projection_px = 28;
    actor->hit_width = actor->hit_height = 0;
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
    height = actor->hit_height ? actor->hit_height : pose_height(actor->pose);
    b.w = actor->hit_width ? actor->hit_width : SIDESCRL_ACTOR_WIDTH;
    b.x = clamp_i32((int64_t)actor->x + SIDESCRL_ACTOR_WIDTH / 2 - b.w / 2);
    b.y = clamp_i32((int64_t)actor->y + (SIDESCRL_STAND_HEIGHT - height));
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
    int32_t old_y;
    int32_t lift;

    if (actor == NULL)
        return;

    old_y = actor->y;
    int32_t gravity = SIDESCRL_PHYS_GRAVITY_ACCEL;
    if (actor->jumping && actor->spring_ticks) {
        if (actor->spring_held) {
            gravity -= BONES_SPRING_BOOST;
            actor->spring_ticks--;
        } else actor->spring_ticks = 0;
    }
    actor->vy = clamp_i32((int64_t)actor->vy + sidescrl_project_impulse(actor, gravity));

    /* 2. integrate */
    move_axis(&actor->x, &actor->rem_x, actor->vx);
    move_axis(&actor->y, &actor->rem_y, actor->vy);

    /* 3. floor contact */
    self = actor_collider(actor);
    lift = hitchk_ground_clamp(&self, platforms, count,
                               SIDESCRL_PHYS_FLOOR_SNAP_PX);
    if (platforms && actor->y > old_y) {
        /* Sweep the feet so a thin platform survives a high-speed fall. */
        int64_t bottom = (int64_t)actor->y + SIDESCRL_STAND_HEIGHT;
        for (size_t i = 0; i < count; i++) {
            const BoundingBox *p = &platforms[i].box;
            if (p->h > 0 && p->w > 0 && hitchk_layers_match(self.layer_mask, platforms[i].layer_mask) &&
                (int64_t)self.box.x + self.box.w > p->x && (int64_t)p->x + p->w > self.box.x &&
                (int64_t)old_y + SIDESCRL_STAND_HEIGHT <= p->y && p->y < bottom + lift)
                lift = clamp_i32(p->y - bottom);
        }
    }
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
