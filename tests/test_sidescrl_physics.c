#include "core/sidescrl_physics.h"
#include <stdio.h>
#define CHECK(c) do {if (!(c)) {printf("FAIL %d: %s\n",__LINE__,#c);return 1;}} while (0)

#define U SIDESCRL_PHYS_UNITS_PER_PIXEL

static Collider solid(int32_t x, int32_t y, int32_t w, int32_t h) {
    Collider c;
    c.box.x = x; c.box.y = y; c.box.w = w; c.box.h = h;
    c.layer_mask = SIDESCRL_PHYS_LAYER_SOLID;
    return c;
}
static int32_t feet(const SidescrlActor *a) {
    BoundingBox b = sidescrl_actor_box(a);
    return b.y + b.h;
}
static uint32_t rnd(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 16; }
static bool same(const SidescrlActor *a, const SidescrlActor *b) {
    return a->x == b->x && a->y == b->y && a->vx == b->vx && a->vy == b->vy &&
           a->rem_x == b->rem_x && a->rem_y == b->rem_y && a->pose == b->pose &&
           a->layer_mask == b->layer_mask && a->on_ground == b->on_ground &&
           a->facing_left == b->facing_left && a->jumping == b->jumping;
}
/* Steps until on_ground; returns the frame count, or -1 if it never lands. */
static int fall_until_landed(SidescrlActor *a, const Collider *p, size_t n, int max_frames) {
    for (int i = 1; i <= max_frames; i++) {
        sidescrl_physics_step(a, p, n);
        if (a->on_ground) return i;
    }
    return -1;
}

/* ---- pose boxes ---------------------------------------------------------- */
static int test_boxes(void) {
    SidescrlActor a;
    BoundingBox b;

    sidescrl_actor_init(&a, 50, 100);
    CHECK(a.x == 50 && a.y == 100 && a.vx == 0 && a.vy == 0 && a.rem_x == 0 && a.rem_y == 0);
    CHECK(a.pose == SIDESCRL_POSE_STAND && !a.on_ground && !a.jumping && !a.facing_left);
    CHECK(a.layer_mask == (SIDESCRL_PHYS_LAYER_ACTOR | SIDESCRL_PHYS_LAYER_SOLID));

    b = sidescrl_actor_box(&a);
    CHECK(b.x == 50 && b.y == 100 && b.w == 20 && b.h == 42);
    a.pose = SIDESCRL_POSE_DUCK;
    b = sidescrl_actor_box(&a);
    CHECK(b.x == 50 && b.y == 118 && b.w == 20 && b.h == 24 && b.y + b.h == 142);
    a.pose = SIDESCRL_POSE_JUMP;
    b = sidescrl_actor_box(&a);
    CHECK(b.x == 50 && b.y == 106 && b.w == 20 && b.h == 36 && b.y + b.h == 142);
    a.pose = (SidescrlPose)99; /* invalid counts as STAND */
    b = sidescrl_actor_box(&a);
    CHECK(b.y == 100 && b.h == 42);

    sidescrl_actor_init(&a, 0, INT32_MAX);
    a.pose = SIDESCRL_POSE_DUCK;
    b = sidescrl_actor_box(&a);
    CHECK(b.y == INT32_MAX); /* saturates, no overflow */

    b = sidescrl_actor_box(NULL);
    CHECK(b.w == 0 && b.h == 0);
    sidescrl_actor_init(NULL, 0, 0);
    return 0;
}

/* ---- fixed-point motion, gravity cap, saturation (no platforms) ---------- */
static int test_motion(void) {
    SidescrlActor a;
    int32_t prev_y;

    /* quarter-pixel steps carry the remainder: 4 steps stay put, the 5th moves */
    sidescrl_actor_init(&a, 0, 0);
    a.vx = U / 4;
    for (int i = 0; i < 4; i++) sidescrl_physics_step(&a, NULL, 0);
    CHECK(a.x == 0 && a.rem_x == 4 * (U / 4));
    sidescrl_physics_step(&a, NULL, 0);
    CHECK(a.x == 1 && a.rem_x == 5 * (U / 4) - U);

    /* negative velocity: floor division, remainder stays in 0..U-1 */
    sidescrl_actor_init(&a, 0, 0);
    a.vx = -(U / 4);
    sidescrl_physics_step(&a, NULL, 0);
    CHECK(a.x == -1 && a.rem_x == U - U / 4);
    for (int i = 0; i < 3; i++) sidescrl_physics_step(&a, NULL, 0);
    CHECK(a.x == -1 && a.rem_x == U - 4 * (U / 4)); /* total -4*(U/4) units */
    sidescrl_physics_step(&a, NULL, 0);
    CHECK(a.x == -2 && a.rem_x >= 0 && a.rem_x < U);

    /* U steps at vx = -100 units is exactly -100 pixels; same for +1 unit */
    sidescrl_actor_init(&a, 0, 0);
    a.vx = -100;
    for (int i = 0; i < U; i++) sidescrl_physics_step(&a, NULL, 0);
    CHECK(a.x == -100 && a.rem_x == 0);
    sidescrl_actor_init(&a, 0, 0);
    a.vx = 1;
    for (int i = 0; i < U; i++) sidescrl_physics_step(&a, NULL, 0);
    CHECK(a.x == 1 && a.rem_x == 0);

    /* saturation instead of signed overflow */
    sidescrl_actor_init(&a, INT32_MAX - 1, 0);
    a.vx = INT32_MAX;
    sidescrl_physics_step(&a, NULL, 0);
    CHECK(a.x == INT32_MAX);
    sidescrl_actor_init(&a, INT32_MIN + 1, 0);
    a.vx = INT32_MIN;
    sidescrl_physics_step(&a, NULL, 0);
    CHECK(a.x == INT32_MIN);

    /* saturation: INT32_MAX remains representable, very negative vy only gains gravity */
    sidescrl_actor_init(&a, 0, 0);
    a.vy = INT32_MAX;
    sidescrl_physics_step(&a, NULL, 0);
    CHECK(a.vy == INT32_MAX);
    sidescrl_actor_init(&a, 0, 0);
    a.vy = INT32_MIN;
    sidescrl_physics_step(&a, NULL, 0);
    CHECK(a.vy == INT32_MIN + SIDESCRL_PHYS_GRAVITY_ACCEL);

    /* free fall: speed grows by the authored impulse each tick */
    sidescrl_actor_init(&a, 0, 0);
    a.facing_left = true;
    a.jumping = true;
    prev_y = a.y;
    for (int i = 0; i < 200; i++) {
        int32_t prev_vy = a.vy;
        sidescrl_physics_step(&a, NULL, 0);
        CHECK(!a.on_ground);
        CHECK(a.vy == prev_vy + SIDESCRL_PHYS_GRAVITY_ACCEL);
        CHECK(a.y >= prev_y);
        prev_y = a.y;
    }
    CHECK(a.vy == 200 * SIDESCRL_PHYS_GRAVITY_ACCEL);
    CHECK(a.facing_left && a.jumping); /* the step does not own these flags */
    CHECK(a.y > 1000);
    return 0;
}

/* ---- requirement 1: free fall and a stable landing ---------------------- */
static int test_landing(void) {
    SidescrlActor a;
    Collider floor = solid(0, 600, 400, 40);
    Collider thin = solid(0, 600, 400, 2);
    uint32_t seed = 99;

    sidescrl_actor_init(&a, 100, 0);
    {
        int32_t vmax = 0;
        int landed = 0;
        for (int i = 0; i < 2000 && !landed; i++) {
            sidescrl_physics_step(&a, &floor, 1);
            CHECK(a.vy == 0 || a.vy == (i + 1) * SIDESCRL_PHYS_GRAVITY_ACCEL);
            if (a.vy > vmax) vmax = a.vy;
            landed = a.on_ground;
            if (!landed) CHECK(feet(&a) <= 600); /* never sinks into the floor */
        }
        CHECK(landed);
        CHECK(vmax > 8 * U); /* exceeds the former cap */
    }
    CHECK(a.y == 600 - SIDESCRL_STAND_HEIGHT && feet(&a) == 600);
    CHECK(a.vy == 0 && a.rem_y == 0 && a.on_ground && !a.jumping);
    for (int i = 0; i < 300; i++) { /* stable: no flicker, no drift */
        sidescrl_physics_step(&a, &floor, 1);
        CHECK(a.on_ground && a.y == 558 && a.vy == 0 && a.rem_y == 0);
    }

    /* a 2-pixel platform is not tunnelled even at terminal speed */
    sidescrl_actor_init(&a, 100, 0);
    CHECK(fall_until_landed(&a, &thin, 1, 2000) > 0);
    CHECK(a.y == 558 && a.vy == 0);
    for (int i = 0; i < 60; i++) {
        sidescrl_physics_step(&a, &thin, 1);
        CHECK(a.on_ground && a.y == 558);
    }

    /* pose does not change where the feet land (bottom-anchored boxes) */
    for (int p = 0; p < SIDESCRL_POSE_COUNT; p++) {
        sidescrl_actor_init(&a, 100, 0);
        a.pose = (SidescrlPose)p;
        CHECK(fall_until_landed(&a, &floor, 1, 2000) > 0);
        CHECK(a.y == 558 && feet(&a) == 600);
    }

    /* random starts: always lands flush, then stays put */
    for (int n = 0; n < 300; n++) {
        sidescrl_actor_init(&a, (int32_t)(rnd(&seed) % 381u), (int32_t)(rnd(&seed) % 501u));
        a.vy = (int32_t)(rnd(&seed) % (uint32_t)(50 * U));
        a.rem_y = (int32_t)(rnd(&seed) % (uint32_t)U);
        a.pose = (SidescrlPose)(rnd(&seed) % 3u);
        CHECK(fall_until_landed(&a, &floor, 1, 600) > 0);
        CHECK(a.y == 558 && a.vy == 0 && feet(&a) == 600);
        for (int i = 0; i < 5; i++) {
            sidescrl_physics_step(&a, &floor, 1);
            CHECK(a.on_ground && a.y == 558);
        }
    }

    /* a jump leaves the floor (the resting probe must not cancel a rise), then
     * comes back down to the same spot */
    sidescrl_actor_init(&a, 100, 558);
    sidescrl_physics_step(&a, &floor, 1);
    CHECK(a.on_ground);
    a.vy = -6 * U;
    a.jumping = true;
    a.on_ground = false;
    sidescrl_physics_step(&a, &floor, 1);
    CHECK(!a.on_ground && a.jumping && a.y < 558 && a.vy < 0);
    {
        int32_t top = a.y;
        int landed = 0;
        for (int i = 0; i < 500 && !landed; i++) {
            sidescrl_physics_step(&a, &floor, 1);
            if (a.y < top) top = a.y;
            landed = a.on_ground;
        }
        CHECK(landed && top < 558 - 20);
        CHECK(a.y == 558 && a.vy == 0 && !a.jumping);
    }
    return 0;
}

/* ---- requirement 2: stand <-> duck without crossing the floor ----------- */
static int test_poses(void) {
    SidescrlActor a;
    BoundingBox b;
    Collider plats[2];

    plats[0] = solid(0, 200, 400, 40);   /* floor, top 200 */
    plats[1] = solid(90, 140, 60, 20);   /* ceiling, bottom 160 */

    sidescrl_actor_init(&a, 100, 158);   /* stand box [158,200): flush with the floor */
    sidescrl_physics_step(&a, plats, 1);
    CHECK(a.on_ground && a.y == 158);

    /* shrinking always works and keeps the feet on the floor */
    CHECK(sidescrl_actor_set_pose(&a, SIDESCRL_POSE_DUCK, plats, 2));
    b = sidescrl_actor_box(&a);
    CHECK(b.y == 176 && b.h == 24 && b.y + b.h == 200);
    for (int i = 0; i < 40; i++) {
        sidescrl_physics_step(&a, plats, 2);
        CHECK(a.on_ground && a.y == 158 && feet(&a) == 200);
    }

    /* standing up under the ceiling is refused; the box does not change */
    CHECK(!sidescrl_actor_set_pose(&a, SIDESCRL_POSE_STAND, plats, 2));
    CHECK(a.pose == SIDESCRL_POSE_DUCK);
    b = sidescrl_actor_box(&a);
    CHECK(b.y == 176 && b.h == 24);

    /* JUMP's box (top 164) fits under the ceiling (bottom 160); STAND's does not */
    CHECK(sidescrl_actor_set_pose(&a, SIDESCRL_POSE_JUMP, plats, 2));
    b = sidescrl_actor_box(&a);
    CHECK(b.y == 164 && b.h == 36 && b.y + b.h == 200);
    for (int i = 0; i < 40; i++) {
        sidescrl_physics_step(&a, plats, 2);
        CHECK(a.on_ground && a.y == 158 && feet(&a) == 200);
    }
    CHECK(!sidescrl_actor_set_pose(&a, SIDESCRL_POSE_STAND, plats, 2));
    CHECK(a.pose == SIDESCRL_POSE_JUMP);
    CHECK(sidescrl_actor_set_pose(&a, SIDESCRL_POSE_DUCK, plats, 2));

    /* without the ceiling the actor stands up */
    CHECK(sidescrl_actor_set_pose(&a, SIDESCRL_POSE_STAND, plats, 1));
    b = sidescrl_actor_box(&a);
    CHECK(b.y == 158 && b.h == 42 && b.y + b.h == 200);
    for (int i = 0; i < 40; i++) {
        sidescrl_physics_step(&a, plats, 1);
        CHECK(a.on_ground && a.y == 158);
    }

    /* same pose is a no-op success; bad input is refused */
    CHECK(sidescrl_actor_set_pose(&a, SIDESCRL_POSE_STAND, plats, 2));
    CHECK(!sidescrl_actor_set_pose(&a, (SidescrlPose)7, plats, 2));
    CHECK(!sidescrl_actor_set_pose(&a, SIDESCRL_POSE_COUNT, plats, 2));
    CHECK(!sidescrl_actor_set_pose(NULL, SIDESCRL_POSE_DUCK, plats, 2));
    CHECK(sidescrl_actor_set_pose(&a, SIDESCRL_POSE_DUCK, NULL, 5)); /* NULL = no platforms */
    CHECK(sidescrl_actor_set_pose(&a, SIDESCRL_POSE_STAND, NULL, 5));

    /* toggling every frame never drops the actor through or off the floor */
    for (int i = 0; i < 200; i++) {
        CHECK(sidescrl_actor_set_pose(&a, (i & 1) ? SIDESCRL_POSE_STAND : SIDESCRL_POSE_DUCK, plats, 1));
        sidescrl_physics_step(&a, plats, 1);
        CHECK(a.on_ground && a.y == 158 && feet(&a) == 200 && a.vy == 0);
    }
    return 0;
}

/* ---- requirement 3: walking past the edge makes the actor fall ---------- */
static int test_edges(void) {
    SidescrlActor a;
    Collider plats[2];
    int32_t prev_y;
    int landed;

    /* right edge: platform A covers x [0,100), B is lower and starts at 100 */
    plats[0] = solid(0, 200, 100, 40);
    plats[1] = solid(100, 300, 300, 40);
    sidescrl_actor_init(&a, 70, 158);
    sidescrl_physics_step(&a, plats, 2);
    CHECK(a.on_ground && a.y == 158);
    a.vx = 3 * U;
    for (int k = 1; k <= 9; k++) { /* box [x,x+20) still overlaps A while x < 100 */
        sidescrl_physics_step(&a, plats, 2);
        CHECK(a.x == 70 + 3 * k);
        CHECK(a.on_ground && a.y == 158);
    }
    sidescrl_physics_step(&a, plats, 2); /* x == 100: no longer over A */
    CHECK(a.x == 100);
    CHECK(!a.on_ground);
    prev_y = a.y;
    landed = 0;
    for (int i = 0; i < 400 && !landed; i++) { /* falls, then lands on B */
        sidescrl_physics_step(&a, plats, 2);
        landed = a.on_ground;
        if (!landed) CHECK(a.y >= prev_y); /* sub-pixel gravity: may not move for a frame */
        prev_y = a.y;
    }
    CHECK(landed && a.y == 300 - SIDESCRL_STAND_HEIGHT && a.vy == 0);

    /* left edge: platform A covers x [200,300), B is lower and ends at 200 */
    plats[0] = solid(200, 200, 100, 40);
    plats[1] = solid(0, 300, 200, 40);
    sidescrl_actor_init(&a, 210, 158);
    sidescrl_physics_step(&a, plats, 2);
    CHECK(a.on_ground && a.y == 158);
    a.vx = -3 * U;
    for (int k = 1; k <= 9; k++) { /* overlaps A while x + 20 > 200, i.e. x > 180 */
        sidescrl_physics_step(&a, plats, 2);
        CHECK(a.x == 210 - 3 * k);
        CHECK(a.on_ground && a.y == 158);
    }
    sidescrl_physics_step(&a, plats, 2); /* x == 180: no longer over A */
    CHECK(a.x == 180);
    CHECK(!a.on_ground);
    landed = 0;
    for (int i = 0; i < 400 && !landed; i++) {
        sidescrl_physics_step(&a, plats, 2);
        landed = a.on_ground;
    }
    CHECK(landed && a.y == 258 && a.vy == 0);

    /* a lone platform: after the edge it just keeps falling */
    plats[0] = solid(0, 200, 100, 40);
    sidescrl_actor_init(&a, 70, 158);
    sidescrl_physics_step(&a, plats, 1);
    CHECK(a.on_ground);
    a.vx = 3 * U;
    for (int i = 0; i < 10; i++) sidescrl_physics_step(&a, plats, 1);
    CHECK(!a.on_ground);
    for (int i = 0; i < 100; i++) {
        sidescrl_physics_step(&a, plats, 1);
        CHECK(!a.on_ground);
    }
    CHECK(a.y > 158 + 100);

    /* a low step (4 px) is stepped up; a tall block is not mistaken for floor */
    plats[0] = solid(0, 200, 400, 40);
    plats[1] = solid(150, 196, 100, 44);
    sidescrl_actor_init(&a, 100, 158);
    sidescrl_physics_step(&a, plats, 2);
    a.vx = 3 * U;
    for (int i = 0; i < 40; i++) {
        sidescrl_physics_step(&a, plats, 2);
        CHECK(a.on_ground);
        CHECK(a.y == (a.x + SIDESCRL_ACTOR_WIDTH > 150 ? 154 : 158));
    }
    plats[1] = solid(150, 160, 50, 80); /* top is 40 px above the feet */
    sidescrl_actor_init(&a, 100, 158);
    sidescrl_physics_step(&a, plats, 2);
    a.vx = 3 * U;
    for (int i = 0; i < 40; i++) {
        sidescrl_physics_step(&a, plats, 2);
        CHECK(a.on_ground && a.y == 158); /* not teleported on top of the block */
    }
    return 0;
}

/* ---- NULL handling and determinism -------------------------------------- */
static int test_misc(void) {
    SidescrlActor a, b;
    Collider plats[3];
    uint32_t seed = 4242;

    sidescrl_physics_step(NULL, NULL, 0);
    sidescrl_actor_init(&a, 0, 0);
    sidescrl_physics_step(&a, NULL, 5); /* NULL platforms: nothing to land on */
    CHECK(!a.on_ground && a.vy > 0);
    sidescrl_physics_step(&a, plats, 0);
    CHECK(!a.on_ground);

    plats[0] = solid(0, 200, 150, 40);
    plats[1] = solid(150, 170, 150, 70);
    plats[2] = solid(300, 230, 200, 40);
    sidescrl_actor_init(&a, 10, 0);
    b = a;
    for (int i = 0; i < 4000; i++) {
        uint32_t r = rnd(&seed);
        a.vx = (int32_t)(r % 7u) * U - 3 * U;
        if ((r & 0x70u) == 0x70u && a.on_ground) { /* jump */
            a.vy = -6 * U;
            a.jumping = true;
            a.on_ground = false;
        }
        if ((r & 0x300u) == 0x300u)
            sidescrl_actor_set_pose(&a, (SidescrlPose)((r >> 12) % 3u), plats, 3);
        if (a.x < -50) a.x = 500;
        if (a.x > 600) a.x = 0;
        if (a.y > 3000) { a.y = 0; a.rem_y = 0; a.vy = 0; }
        b = a;
        sidescrl_physics_step(&a, plats, 3);
        sidescrl_physics_step(&b, plats, 3);
        CHECK(same(&a, &b));
        CHECK(a.rem_x >= 0 && a.rem_x < U && a.rem_y >= 0 && a.rem_y < U);
        CHECK(!a.on_ground || (a.vy == 0 && !a.jumping));
    }
    return 0;
}

int main(void) {
    if (test_boxes()) return 1;
    if (test_motion()) return 1;
    if (test_landing()) return 1;
    if (test_poses()) return 1;
    if (test_edges()) return 1;
    if (test_misc()) return 1;
    return 0;
}
