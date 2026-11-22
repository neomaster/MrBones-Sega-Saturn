#include "core/sidescrl_motion.h"

static int32_t clamp_i32(int64_t v) {
    if (v > INT32_MAX)
        return INT32_MAX;
    if (v < INT32_MIN)
        return INT32_MIN;
    return (int32_t)v;
}

/* Move v toward target by at most delta, never past it. */
static int32_t approach(int32_t v, int32_t target, int32_t delta) {
    int64_t diff = (int64_t)target - v;
    int64_t limit = delta > 0 ? delta : 0;

    if (diff > limit)
        diff = limit;
    if (diff < -limit)
        diff = -limit;
    return clamp_i32((int64_t)v + diff);
}

/*
 * How far the ground under the leading column of the box (the last column on the
 * side of the move) is above the feet; 0 if it is level with or below them. The
 * slope resolver puts the feet at the ground under the CENTRE, so on a rising
 * ramp the leading edge is already above the feet. Obstacle heights are measured
 * from that ground, otherwise a 45 degree ramp would look like a wall.
 */
static int64_t lead_rise(const BoundingBox *box, int32_t desired, SidescrlGroundFn get_ground,
                         void *map_ctx) {
    int64_t col, ground, rise;

    if (get_ground == NULL || desired == 0 || box->w <= 0)
        return 0;
    col = desired > 0 ? (int64_t)box->x + box->w - 1 : (int64_t)box->x;
    if (col < INT32_MIN || col > INT32_MAX)
        return 0;
    ground = get_ground(map_ctx, (int32_t)col);
    if (ground == SIDESCRL_SLOPE_NO_GROUND)
        return 0;
    rise = (int64_t)box->y + box->h - ground;
    return rise > 0 ? rise : 0;
}

/*
 * Shorten a ground move so the slope resolver can still follow it. After moving by k
 * the terrain under the CENTRE column must not be more than `up` pixels above the
 * feet; a taller rise is out of the contact window of sidescrl_slope_step, and the
 * actor would lose contact and sink into the solid. Returns the largest k in
 * [0, dx] (toward 0) that is acceptable. A column with no ground is acceptable: the
 * actor then simply walks off an edge.
 */
static int32_t follow_dx(const BoundingBox *box, int32_t dx, int32_t up,
                         SidescrlGroundFn get_ground, void *map_ctx) {
    const int64_t feet = (int64_t)box->y + box->h;
    const int32_t toward_zero = dx > 0 ? -1 : 1;
    const int64_t reach = up > 0 ? up : 0;
    int32_t k;

    if (get_ground == NULL)
        return dx;
    for (k = dx; k != 0; k += toward_zero) {
        BoundingBox moved = *box;
        int32_t ground;

        moved.x = clamp_i32((int64_t)box->x + k);
        ground = get_ground(map_ctx, sidescrl_slope_column(&moved));
        if (ground == SIDESCRL_SLOPE_NO_GROUND || (int64_t)ground >= feet - reach)
            return k;
    }
    return 0;
}

void sidescrl_motion_step(SidescrlActor *actor, int32_t walk_dir, bool jump_edge,
                          int32_t max_climb_step, SidescrlWallFn is_wall,
                          SidescrlGroundFn get_ground, void *map_ctx) {
    const int64_t unit = SIDESCRL_PHYS_UNITS_PER_PIXEL;
    int dir;
    int64_t total, whole, frac;
    int32_t desired, allowed, saved_vx, climb = 0;
    BoundingBox box;

    if (actor == NULL)
        return;
    dir = walk_dir > 0 ? 1 : (walk_dir < 0 ? -1 : 0);

    /* a) walk */
    if (dir != 0)
        actor->facing_left = dir < 0;
    if (actor->on_ground) {
        actor->vx = approach(actor->vx, dir * sidescrl_project_impulse(actor, actor->walk_speed),
                             sidescrl_project_impulse(actor, dir != 0 ? SIDESCRL_MOTION_WALK_ACCEL
                                                                      : SIDESCRL_MOTION_FRICTION));
    }

    /* b) horizontal move, stopped by walls */
    total = (int64_t)actor->rem_x + (int64_t)actor->vx;
    whole = total / unit;
    frac = total % unit;
    if (frac < 0) {
        frac += unit;
        whole -= 1;
    }
    desired = clamp_i32(whole);
    box = sidescrl_actor_box(actor);
    if (actor->on_ground) { /* in the air there is no allowance at all */
        const int64_t base = max_climb_step > 0 ? max_climb_step : 0;
        const int64_t room = (int64_t)box.h - 1 - base; /* the top row must keep blocking */
        int64_t rise = lead_rise(&box, desired, get_ground, map_ctx);

        if (rise > room)
            rise = room > 0 ? room : 0;
        climb = clamp_i32(base + rise);
    }
    allowed = sidescrl_wall_clamp_dx(&box, desired, climb, is_wall, map_ctx);
    if (actor->on_ground && is_wall != NULL) /* NULL is_wall: the caller wants no walls */
        allowed = follow_dx(&box, allowed, max_climb_step, get_ground, map_ctx);
    if (allowed != desired) {
        actor->vx = 0;
        actor->rem_x = 0;
    } else {
        actor->rem_x = (int32_t)frac;
    }
    actor->x = clamp_i32((int64_t)actor->x + allowed);

    /* c) jump */
    if (jump_edge && actor->on_ground) {
        actor->vy = sidescrl_project_impulse(actor, actor->jump_impulse);
        actor->on_ground = false;
        actor->jumping = true;
        actor->spring_ticks = actor->boost_duration;
    }

    /* d) vertical move; x was already integrated, so hold vx at 0 for this call */
    saved_vx = actor->vx;
    actor->vx = 0; /* x was already integrated above */
    sidescrl_slope_step(actor, get_ground, map_ctx, max_climb_step, max_climb_step);
    actor->vx = saved_vx;
}
