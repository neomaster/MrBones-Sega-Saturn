#include "core/sidescrl_motion.h"
#include <stdio.h>
#define CHECK(c) do {if (!(c)) {printf("FAIL %d: %s\n",__LINE__,#c);return 1;}} while (0)

#define U SIDESCRL_PHYS_UNITS_PER_PIXEL
#define MAXV SIDESCRL_PHYS_MAX_FALL_SPEED
#define GRAV SIDESCRL_PHYS_GRAVITY_ACCEL
#define WALK SIDESCRL_MOTION_WALK_MAX
#define ACCEL SIDESCRL_MOTION_WALK_ACCEL
#define FRIC SIDESCRL_MOTION_FRICTION
#define CLIMB 6
#define N 700

/* One height map is the single truth for both callbacks: g[col] is the y of the highest
 * solid pixel in that column, and a pixel is solid from there down. */
typedef struct { int32_t g[N]; } World;

static int32_t ground_at(const void *ctx, int32_t col) {
    const World *w = (const World *)ctx;
    if (col < 0 || col >= N) return SIDESCRL_SLOPE_NO_GROUND;
    return w->g[col];
}
static bool wall_at(void *ctx, int32_t col, int32_t row) {
    const World *w = (const World *)ctx;
    if (col < 0 || col >= N) return false;
    return row >= w->g[col];
}
static void step(SidescrlActor *a, World *w, int dir, bool jump) {
    sidescrl_motion_step(a, dir, jump, CLIMB, wall_at, ground_at, w);
}
static int32_t feet(const SidescrlActor *a) {
    BoundingBox b = sidescrl_actor_box(a);
    return b.y + b.h;
}
static int32_t under(const SidescrlActor *a, const World *w) {
    BoundingBox b = sidescrl_actor_box(a);
    return ground_at(w, sidescrl_slope_column(&b));
}
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static uint32_t rnd(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 16; }
static bool same(const SidescrlActor *a, const SidescrlActor *b) {
    return a->x == b->x && a->y == b->y && a->vx == b->vx && a->vy == b->vy &&
           a->rem_x == b->rem_x && a->rem_y == b->rem_y && a->pose == b->pose &&
           a->on_ground == b->on_ground && a->facing_left == b->facing_left &&
           a->jumping == b->jumping;
}

static void flat(World *w, int32_t y) {
    for (int c = 0; c < N; c++) w->g[c] = y;
}
/* Flat 200, with columns >= col raised to `top` (a step or a cliff face on the right). */
static void fill_right(World *w, int32_t col, int32_t top) {
    flat(w, 200);
    for (int c = col; c < N; c++) w->g[c] = top;
}
/* Flat 200, with columns <= col raised to `top` (on the left). */
static void fill_left(World *w, int32_t col, int32_t top) {
    flat(w, 200);
    for (int c = 0; c <= col; c++) w->g[c] = top;
}
static void fill_block(World *w, int32_t c0, int32_t c1, int32_t top) {
    flat(w, 200);
    for (int c = c0; c < c1; c++) w->g[c] = top;
}
/* 45 degrees: flat 300, then up one row per column to a plateau at 100. */
static void fill_ramp_up(World *w) {
    for (int c = 0; c < N; c++) w->g[c] = c < 100 ? 300 : (c < 300 ? 300 - (c - 100) : 100);
}
/* Flat 100, then down one row per column to a plateau at 300. */
static void fill_ramp_down(World *w) {
    for (int c = 0; c < N; c++) w->g[c] = c < 100 ? 100 : (c < 300 ? 100 + (c - 100) : 300);
}
static void mirror(const World *src, World *dst) {
    for (int c = 0; c < N; c++) dst->g[c] = src->g[N - 1 - c];
}

/* ---- walking: acceleration, friction, facing, momentum in the air -------- */
static int test_walk(void) {
    World w;
    SidescrlActor a, b;
    int64_t sum = 0;

    flat(&w, 200);
    sidescrl_actor_init(&a, 40, 158);
    step(&a, &w, 0, false);
    CHECK(a.on_ground && a.vx == 0 && a.x == 40 && a.y == 158);

    for (int k = 1; k <= 8; k++) { /* speeding up, then holding the top speed */
        int32_t ev = k * ACCEL > WALK ? WALK : k * ACCEL;
        step(&a, &w, 1, false);
        sum += ev;
        CHECK(a.vx == ev);
        CHECK(a.x == 40 + (int32_t)(sum / U) && a.rem_x == (int32_t)(sum % U));
        CHECK(a.on_ground && a.y == 158 && !a.facing_left);
    }
    CHECK(a.vx == WALK);
    for (int k = 1; k <= 8; k++) { /* releasing: friction down to a stop */
        int32_t ev = WALK - k * FRIC > 0 ? WALK - k * FRIC : 0;
        step(&a, &w, 0, false);
        sum += ev;
        CHECK(a.vx == ev);
        CHECK(a.x == 40 + (int32_t)(sum / U) && a.rem_x == (int32_t)(sum % U));
        CHECK(a.on_ground && a.y == 158);
    }
    CHECK(a.vx == 0);

    step(&a, &w, -1, false);
    CHECK(a.facing_left);
    step(&a, &w, 0, false);
    CHECK(a.facing_left); /* standing still keeps the facing */
    step(&a, &w, 1, false);
    CHECK(!a.facing_left);

    /* reversing: the speed goes through zero toward the other top speed */
    sidescrl_actor_init(&a, 300, 158);
    step(&a, &w, 0, false);
    for (int k = 0; k < 6; k++) step(&a, &w, 1, false);
    CHECK(a.vx == WALK);
    for (int k = 1; k <= 8; k++) {
        int32_t ev = WALK - k * ACCEL < -WALK ? -WALK : WALK - k * ACCEL;
        step(&a, &w, -1, false);
        CHECK(a.vx == ev && a.on_ground);
    }
    CHECK(a.vx == -WALK);

    /* any non-zero walk_dir counts by its sign */
    for (int s = -1; s <= 1; s += 2) {
        sidescrl_actor_init(&a, 300, 158);
        sidescrl_actor_init(&b, 300, 158);
        step(&a, &w, 0, false);
        step(&b, &w, 0, false);
        for (int k = 0; k < 6; k++) {
            step(&a, &w, 7 * s, false);
            step(&b, &w, s, false);
            CHECK(same(&a, &b));
        }
    }

    /* in the air the speed is kept: no air control, no air friction */
    sidescrl_actor_init(&a, 40, 158);
    step(&a, &w, 0, false);
    for (int k = 0; k < 6; k++) step(&a, &w, 1, false);
    step(&a, &w, 1, true);
    CHECK(!a.on_ground && a.jumping && a.vx == WALK);
    for (int k = 0; k < 10; k++) {
        int32_t x0 = a.x;
        step(&a, &w, (k & 1) ? -1 : 0, false);
        CHECK(!a.on_ground && a.vx == WALK && a.x == x0 + WALK / U);
    }
    return 0;
}

/* ---- walls and steps ----------------------------------------------------- */
static int test_walls(void) {
    World w;
    SidescrlActor a, b;
    int saw_rem = 0;

    /* 1: walking into a cliff face on the right stops flush, with vx and rem_x at zero */
    fill_right(&w, 130, 100);
    sidescrl_actor_init(&a, 40, 158);
    step(&a, &w, 0, false);
    a.vx = 2500; /* a non-round speed and a remainder, so rem_x is really exercised */
    a.rem_x = 1234;
    for (int f = 0; f < 80; f++) {
        step(&a, &w, 1, false);
        CHECK(a.x + SIDESCRL_ACTOR_WIDTH <= 130 && a.on_ground && a.y == 158);
        if (a.rem_x != 0 && a.vx != 0) saw_rem = 1;
    }
    CHECK(saw_rem);
    CHECK(a.x == 110 && a.vx == 0 && a.rem_x == 0);
    for (int f = 0; f < 50; f++) { /* keeps pushing, or lets go: nothing moves */
        step(&a, &w, f < 40 ? 1 : 0, false);
        CHECK(a.x == 110 && a.vx == 0 && a.rem_x == 0 && a.on_ground && a.y == 158);
    }

    /* the same on the left */
    fill_left(&w, 69, 100);
    sidescrl_actor_init(&a, 200, 158);
    step(&a, &w, 0, false);
    a.vx = -2500;
    a.rem_x = 1234;
    for (int f = 0; f < 80; f++) {
        step(&a, &w, -1, false);
        CHECK(a.x >= 70 && a.on_ground && a.y == 158);
    }
    CHECK(a.x == 70 && a.vx == 0 && a.rem_x == 0);

    /* 2: a step up to CLIMB pixels is walked over, a taller one is a wall */
    for (int h = 1; h <= 8; h++) {
        fill_right(&w, 130, 200 - h);
        sidescrl_actor_init(&a, 40, 158);
        step(&a, &w, 0, false);
        for (int f = 0; f < 80; f++) step(&a, &w, 1, false);
        if (h <= CLIMB) {
            CHECK(a.x > 130 && a.y == 158 - h && a.on_ground && a.vx == WALK);
        } else {
            CHECK(a.x == 110 && a.y == 158 && a.on_ground && a.vx == 0 && a.rem_x == 0);
        }

        fill_left(&w, 69, 200 - h);
        sidescrl_actor_init(&a, 200, 158);
        step(&a, &w, 0, false);
        for (int f = 0; f < 50; f++) step(&a, &w, -1, false); /* stay inside the map: col >= 0 */
        if (h <= CLIMB) {
            CHECK(a.x < 70 && a.y == 158 - h && a.on_ground && a.vx == -WALK);
        } else {
            CHECK(a.x == 70 && a.y == 158 && a.on_ground && a.vx == 0 && a.rem_x == 0);
        }
    }

    /* 3: in the air there is no climb allowance. A 5 px ledge that a walker steps onto
     * stops an actor that is still low in the air (its feet are below the ledge top) */
    fill_right(&w, 130, 195);
    sidescrl_actor_init(&a, 110, 155); /* feet at 197, flush against the ledge face */
    a.vx = WALK;
    step(&a, &w, 1, false);
    CHECK(!a.on_ground && a.x == 110 && a.vx == 0 && a.rem_x == 0);

    sidescrl_actor_init(&b, 110, 158); /* the same spot, on the ground */
    step(&b, &w, 0, false);
    CHECK(b.on_ground);
    for (int f = 0; f < 10; f++) step(&b, &w, 1, false);
    CHECK(b.on_ground && b.x > 110 && b.y == 153 && b.vx == WALK);
    return 0;
}

/* ---- ramps: up and down, both directions, without false stops ------------ */
static int run_ramp(World *w, int32_t x0, int32_t y0, int dir, int frames, int32_t y_end) {
    SidescrlActor a;
    int reached = 0;

    sidescrl_actor_init(&a, x0, y0);
    step(&a, w, 0, false);
    CHECK(a.on_ground && a.y == y0);
    for (int f = 0; f < frames; f++) {
        step(&a, w, dir, false);
        CHECK(a.on_ground);
        CHECK(feet(&a) == under(&a, w)); /* stuck to the surface, up or down */
        if (a.vx == dir * WALK) reached = 1;
        if (reached) CHECK(a.vx == dir * WALK); /* never stopped by the ramp itself */
    }
    CHECK(reached);
    CHECK(a.y == y_end);
    return 0;
}

/* 45 degree ramp from 300 up to 200 over columns 100..199, then a cliff face up to 100. */
static void fill_ramp_wall(World *w) {
    for (int c = 0; c < N; c++)
        w->g[c] = c < 100 ? 300 : (c < 200 ? 300 - (c - 100) : 100);
}

static int test_ramps(void) {
    World up, down, mup, mdown;

    fill_ramp_up(&up);
    fill_ramp_down(&down);
    mirror(&up, &mup);
    mirror(&down, &mdown);
    if (run_ramp(&up, 40, 258, 1, 130, 100 - SIDESCRL_STAND_HEIGHT)) return 1;
    if (run_ramp(&down, 40, 58, 1, 130, 300 - SIDESCRL_STAND_HEIGHT)) return 1;
    if (run_ramp(&mup, 639, 258, -1, 130, 100 - SIDESCRL_STAND_HEIGHT)) return 1;
    if (run_ramp(&mdown, 639, 58, -1, 130, 300 - SIDESCRL_STAND_HEIGHT)) return 1;

    /* a cliff at the top of the ramp is still a wall: the lead-column allowance is
     * measured from the ground the leading edge stands on, not from the cliff top */
    {
        World rw;
        SidescrlActor a;

        fill_ramp_wall(&rw);
        sidescrl_actor_init(&a, 40, 258);
        step(&a, &rw, 0, false);
        for (int f = 0; f < 150; f++) {
            step(&a, &rw, 1, false);
            CHECK(a.x + SIDESCRL_ACTOR_WIDTH <= 200 && a.on_ground);
            CHECK(feet(&a) == under(&a, &rw));
        }
        CHECK(a.x == 180 && a.vx == 0 && a.rem_x == 0);
        CHECK(a.y == 210 - SIDESCRL_STAND_HEIGHT); /* ground under the centre, column 190 */
    }
    return 0;
}

/* ---- jumping over a solid obstacle and landing steadily ------------------ */
static int test_jump(void) {
    World w;
    SidescrlActor a, b;
    int32_t max_alt = 0;
    int landed_at = -1, jumped = 0;

    fill_block(&w, 170, 190, 170); /* a 30 px block, 20 px wide */

    /* jumped in time: clears it and lands on the far side */
    sidescrl_actor_init(&a, 100, 158);
    step(&a, &w, 0, false);
    CHECK(a.on_ground);
    for (int f = 1; f <= 140; f++) {
        step(&a, &w, 1, f == 5);
        if (f == 5)
            CHECK(a.vy == SIDESCRL_MOTION_JUMP_IMPULSE + GRAV && a.jumping && !a.on_ground);
        if (f >= 5 && landed_at < 0) {
            if (f > 5 && a.on_ground) {
                landed_at = f;
            } else {
                int32_t alt = 158 - a.y;
                if (alt > max_alt) max_alt = alt;
                CHECK(!a.on_ground && a.vx == WALK); /* never stopped in flight */
                if (a.x < 190 && a.x + SIDESCRL_ACTOR_WIDTH > 170) CHECK(alt >= 30);
            }
        }
        if (landed_at > 0) { /* landing is steady: no bounce, no flicker, speed kept */
            CHECK(a.on_ground && a.y == 158 && a.vy == 0 && !a.jumping);
            CHECK(a.x >= 190 && a.vx == WALK);
        }
    }
    CHECK(landed_at > 5 && landed_at < 140);
    CHECK(max_alt > 60);

    /* never jumped: the block is a wall */
    sidescrl_actor_init(&a, 100, 158);
    step(&a, &w, 0, false);
    for (int f = 0; f < 120; f++) step(&a, &w, 1, false);
    CHECK(a.x == 150 && a.vx == 0 && a.rem_x == 0 && a.on_ground && a.y == 158);

    /* jumped too late: still low when it reaches the face, so the air does not let it through */
    sidescrl_actor_init(&a, 100, 158);
    step(&a, &w, 0, false);
    for (int f = 0; f < 200; f++) {
        bool j = !jumped && a.on_ground && a.x + SIDESCRL_ACTOR_WIDTH >= 160;
        if (j) jumped = 1;
        step(&a, &w, 1, j);
        CHECK(a.x + SIDESCRL_ACTOR_WIDTH <= 170);
    }
    CHECK(jumped && a.x == 150 && a.vx == 0 && a.on_ground && a.y == 158);

    /* a jump pressed in the air changes nothing */
    flat(&w, 200);
    sidescrl_actor_init(&a, 40, 158);
    sidescrl_actor_init(&b, 40, 158);
    step(&a, &w, 0, false);
    step(&b, &w, 0, false);
    for (int f = 1; f <= 5; f++) {
        step(&a, &w, 1, f == 5);
        step(&b, &w, 1, f == 5);
    }
    for (int f = 0; f < 120; f++) {
        step(&a, &w, 1, !a.on_ground);
        step(&b, &w, 1, false);
        CHECK(same(&a, &b));
    }
    return 0;
}

/* ---- steep ramps: climbed without losing the terrain, or a wall ---------- */
/* Flat 300, then up `per_col` rows per column from column 100 to a plateau at 100. */
static void fill_steep(World *w, int32_t per_col) {
    for (int c = 0; c < N; c++) {
        int32_t top = 300 - per_col * (c - 100);
        w->g[c] = c < 100 ? 300 : (top < 100 ? 100 : top);
    }
}

/* Up to 3 px per column is climbed in both directions; 5 or more is a wall. 4 depends on
 * the side (the centre column x + w / 2 is not centred in the 20 px box), so only the
 * invariants are checked for it. Always the actor stays on the surface: never sinks
 * into it, never falls through it. */
static int run_steep(int32_t per_col, bool left) {
    World w, m;
    SidescrlActor a;
    bool climbs = per_col <= 3;

    fill_steep(&w, per_col);
    if (left) {
        mirror(&w, &m);
        w = m;
    }
    sidescrl_actor_init(&a, left ? 640 : 40, 258);
    step(&a, &w, 0, false);
    for (int f = 0; f < 200; f++) { /* 200 frames at 3 px stay inside the 700 column map */
        step(&a, &w, left ? -1 : 1, false);
        CHECK(a.on_ground && a.y <= 258);
        CHECK(feet(&a) == under(&a, &w));
    }
    if (climbs) {
        CHECK(a.y == 100 - SIDESCRL_STAND_HEIGHT && (left ? a.x < 540 : a.x > 140));
    } else if (per_col >= 5) {
        CHECK(a.y == 258 && a.vx == 0 && a.rem_x == 0 && (left ? a.x > 580 : a.x < 100));
    }
    return 0;
}

/* A 3 px per column ramp ending in a cliff face at column 125: the cliff is a wall even
 * though the leading edge already stands high on the ramp (R is large there). */
static int test_steep(void) {
    World w, m;
    SidescrlActor a;

    for (int s = 3; s <= 8; s++)
        if (run_steep(s, false) || run_steep(s, true)) return 1;

    for (int left = 0; left <= 1; left++) {
        for (int c = 0; c < N; c++)
            w.g[c] = c < 100 ? 300 : (c < 125 ? 300 - 3 * (c - 100) : 100);
        if (left) {
            mirror(&w, &m);
            w = m;
        }
        sidescrl_actor_init(&a, left ? 640 : 40, 258);
        step(&a, &w, 0, false);
        for (int f = 0; f < 300; f++) {
            step(&a, &w, left ? -1 : 1, false);
            CHECK(a.on_ground && feet(&a) == under(&a, &w));
            CHECK(left ? a.x >= N - 125 : a.x + SIDESCRL_ACTOR_WIDTH <= 125);
        }
        CHECK(a.x == (left ? N - 125 : 105) && a.vx == 0 && a.rem_x == 0);
    }
    return 0;
}

/* ---- an abyss: the follow limit must not turn a missing floor into a wall --- */
static int test_abyss(void) {
    World w;
    SidescrlActor a;

    flat(&w, 200);
    for (int c = 200; c < 260; c++) w.g[c] = SIDESCRL_SLOPE_NO_GROUND;
    sidescrl_actor_init(&a, 40, 158);
    step(&a, &w, 0, false);
    for (int f = 0; f < 60; f++) step(&a, &w, 1, false);
    CHECK(!a.on_ground && a.x > 190 && a.y > 158 && a.vy > 0);
    return 0;
}

/* ---- degenerate input ---------------------------------------------------- */
static int test_degenerate(void) {
    World w;
    SidescrlActor a;

    flat(&w, 200);
    sidescrl_motion_step(NULL, 1, true, CLIMB, wall_at, ground_at, &w);

    fill_right(&w, 130, 100); /* no wall callback: the face does not stop the actor */
    sidescrl_actor_init(&a, 40, 158);
    sidescrl_motion_step(&a, 0, false, CLIMB, NULL, ground_at, &w);
    for (int f = 0; f < 60; f++) sidescrl_motion_step(&a, 1, false, CLIMB, NULL, ground_at, &w);
    CHECK(a.x > 130);

    flat(&w, 200); /* no terrain callback: always falling, a jump cannot start */
    sidescrl_actor_init(&a, 40, 158);
    for (int f = 0; f < 10; f++) {
        sidescrl_motion_step(&a, 0, f == 3, CLIMB, wall_at, NULL, &w);
        CHECK(!a.on_ground && !a.jumping && a.vy > 0);
    }
    CHECK(a.y > 158);

    sidescrl_actor_init(&a, 0, 0); /* no callbacks at all */
    sidescrl_motion_step(&a, 1, false, CLIMB, NULL, NULL, NULL);
    CHECK(!a.on_ground && a.vy == GRAV);

    sidescrl_actor_init(&a, 0, 0); /* absurd speeds are bounded and cannot overflow */
    a.vx = INT32_MAX;
    sidescrl_motion_step(&a, 0, false, CLIMB, NULL, NULL, NULL);
    CHECK(a.x == SIDESCRL_WALL_MAX_SCAN && a.vx == 0 && a.rem_x == 0);
    sidescrl_actor_init(&a, 0, 0);
    a.vx = INT32_MIN;
    sidescrl_motion_step(&a, 0, false, CLIMB, NULL, NULL, NULL);
    CHECK(a.x == -SIDESCRL_WALL_MAX_SCAN && a.vx == 0 && a.rem_x == 0);
    sidescrl_actor_init(&a, INT32_MAX - 1, 0);
    a.vx = WALK;
    sidescrl_motion_step(&a, 0, false, CLIMB, NULL, NULL, NULL);
    CHECK(a.x == INT32_MAX);
    return 0;
}

/* ---- random terrain and inputs: invariants and determinism --------------- */
static int test_fuzz(void) {
    World w;
    SidescrlActor a, b;
    uint32_t seed = 777;
    int32_t cur = 200;
    int dir = 1;

    for (int c = 0; c < N; c++) {
        uint32_t r = rnd(&seed);
        cur += (int32_t)(r % 5u) - 2;
        if (r % 40u == 0) cur += (r & 0x100u) ? 10 : -10;
        if (r % 97u == 0) cur += (r & 0x100u) ? 60 : -60;
        if (cur < 100) cur = 100;
        if (cur > 300) cur = 300;
        w.g[c] = cur;
    }
    sidescrl_actor_init(&a, 100, w.g[110] - SIDESCRL_STAND_HEIGHT);

    for (int f = 0; f < 6000; f++) {
        uint32_t r = rnd(&seed);
        int32_t prev_x;
        bool jump = ((r >> 8) & 31u) == 0;

        if ((r & 15u) == 0) dir = (int)((r >> 4) % 3u) - 1;
        if (a.x < 40 || a.x > N - 80 || a.y > 1000) {
            int32_t x = 100 + (int32_t)(r % 400u);
            sidescrl_actor_init(&a, x, w.g[x + 10] - SIDESCRL_STAND_HEIGHT);
        }
        b = a;
        prev_x = a.x;
        step(&a, &w, dir, jump);
        step(&b, &w, dir, jump);
        CHECK(same(&a, &b));
        CHECK(a.rem_x >= 0 && a.rem_x < U && a.rem_y >= 0 && a.rem_y < U);
        CHECK(a.vy <= MAXV);
        CHECK(iabs(a.x - prev_x) <= WALK / U + 1);
        CHECK(iabs(a.vx) <= WALK);
        if (a.on_ground) {
            CHECK(a.vy == 0 && !a.jumping);
            CHECK(feet(&a) == under(&a, &w));
        }
        if (dir != 0) CHECK(a.facing_left == (dir < 0));
    }
    return 0;
}

int main(void) {
    if (test_walk()) return 1;
    if (test_walls()) return 1;
    if (test_ramps()) return 1;
    if (test_steep()) return 1;
    if (test_abyss()) return 1;
    if (test_jump()) return 1;
    if (test_degenerate()) return 1;
    if (test_fuzz()) return 1;
    return 0;
}
