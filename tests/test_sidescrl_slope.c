#include "core/sidescrl_slope.h"
#include <stdio.h>
#define CHECK(c) do {if (!(c)) {printf("FAIL %d: %s\n",__LINE__,#c);return 1;}} while (0)

#define U SIDESCRL_PHYS_UNITS_PER_PIXEL
#define GRAV SIDESCRL_PHYS_GRAVITY_ACCEL
#define UP 4
#define DOWN 6
#define N 1500

typedef struct { int32_t x0; int n; const int32_t *y; } Terrain;

static int32_t terrain_y(const void *ctx, int32_t col) {
    const Terrain *t = (const Terrain *)ctx;
    int64_t i = (int64_t)col - t->x0;
    if (i < 0 || i >= t->n) return SIDESCRL_SLOPE_NO_GROUND;
    return t->y[i];
}
static int32_t feet(const SidescrlActor *a) {
    BoundingBox b = sidescrl_actor_box(a);
    return b.y + b.h;
}
static int32_t under(const SidescrlActor *a, const Terrain *t) {
    BoundingBox b = sidescrl_actor_box(a);
    return terrain_y(t, sidescrl_slope_column(&b));
}
static uint32_t rnd(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 16; }
static void set_terrain(Terrain *t, const int32_t *y, int n) { t->x0 = 0; t->n = n; t->y = y; }

/* 45 degrees: one pixel of rise per column. Flat 300 for col < 100, up to 100 at col 300. */
static void fill_ramp_up(int32_t *y) {
    for (int c = 0; c < N; c++) y[c] = c < 100 ? 300 : (c < 300 ? 300 - (c - 100) : 100);
}
/* Mirror image: flat 100 for col < 100, down to 300 at col 300. */
static void fill_ramp_down(int32_t *y) {
    for (int c = 0; c < N; c++) y[c] = c < 100 ? 100 : (c < 300 ? 100 + (c - 100) : 300);
}

/* ---- the pure function --------------------------------------------------- */
static int test_resolve(void) {
    BoundingBox b = {0, 158, 20, 42};             /* feet at 200 */
    BoundingBox lo = {0, INT32_MIN, 20, 42};
    BoundingBox hi = {0, INT32_MAX - 41, 20, 42}; /* feet at INT32_MAX + 1 */

    CHECK(sidescrl_resolve_slope(&b, 200, UP, DOWN) == 0);
    CHECK(sidescrl_slope_in_contact(&b, 200, UP, DOWN));
    CHECK(sidescrl_resolve_slope(&b, 196, UP, DOWN) == -4); /* 4 px step up */
    CHECK(sidescrl_slope_in_contact(&b, 196, UP, DOWN));
    CHECK(sidescrl_resolve_slope(&b, 195, UP, DOWN) == 0);  /* 5 px: too tall */
    CHECK(!sidescrl_slope_in_contact(&b, 195, UP, DOWN));
    CHECK(sidescrl_resolve_slope(&b, 206, UP, DOWN) == 6);  /* 6 px step down */
    CHECK(sidescrl_slope_in_contact(&b, 206, UP, DOWN));
    CHECK(sidescrl_resolve_slope(&b, 207, UP, DOWN) == 0);  /* 7 px: too deep */
    CHECK(!sidescrl_slope_in_contact(&b, 207, UP, DOWN));

    /* zero and negative limits: only a flush surface is a contact */
    CHECK(sidescrl_slope_in_contact(&b, 200, 0, 0));
    CHECK(!sidescrl_slope_in_contact(&b, 199, 0, 0));
    CHECK(!sidescrl_slope_in_contact(&b, 201, 0, 0));
    CHECK(sidescrl_slope_in_contact(&b, 200, -5, -5));
    CHECK(!sidescrl_slope_in_contact(&b, 199, -5, -5));
    CHECK(!sidescrl_slope_in_contact(&b, 201, -5, -5));

    /* asymmetric limits */
    CHECK(sidescrl_resolve_slope(&b, 192, 8, 0) == -8);
    CHECK(!sidescrl_slope_in_contact(&b, 201, 8, 0));
    CHECK(sidescrl_resolve_slope(&b, 203, 0, 3) == 3);
    CHECK(!sidescrl_slope_in_contact(&b, 199, 0, 3));

    /* no ground is never a contact, even with unlimited step down */
    CHECK(!sidescrl_slope_in_contact(&b, SIDESCRL_SLOPE_NO_GROUND, INT32_MAX, INT32_MAX));
    CHECK(sidescrl_resolve_slope(&b, SIDESCRL_SLOPE_NO_GROUND, INT32_MAX, INT32_MAX) == 0);

    /* extremes do not overflow */
    CHECK(!sidescrl_slope_in_contact(&lo, INT32_MAX - 1, INT32_MAX, INT32_MAX));
    CHECK(sidescrl_resolve_slope(&lo, INT32_MAX - 1, INT32_MAX, INT32_MAX) == 0);
    CHECK(sidescrl_resolve_slope(&hi, INT32_MAX - 1, 5, 5) == -2);
    CHECK(sidescrl_resolve_slope(&b, 200 + 1000000, 0, 1000000) == 1000000);

    CHECK(sidescrl_resolve_slope(NULL, 200, UP, DOWN) == 0);
    CHECK(!sidescrl_slope_in_contact(NULL, 200, UP, DOWN));

    /* sampling column: centre of the box */
    CHECK(sidescrl_slope_column(&b) == 10);
    b.x = -30;
    CHECK(sidescrl_slope_column(&b) == -20);
    b.x = INT32_MAX;
    CHECK(sidescrl_slope_column(&b) == INT32_MAX);
    CHECK(sidescrl_slope_column(NULL) == 0);
    return 0;
}

/* ---- 1: climbing a 45 degree ramp step by step --------------------------- */
static int test_ramp_up(void) {
    int32_t y[N];
    Terrain t;
    SidescrlActor a;

    fill_ramp_up(y);
    set_terrain(&t, y, N);
    for (int speed = 1; speed <= 4; speed++) {
        int32_t prev_y;
        int climbed = 0;

        sidescrl_actor_init(&a, 40, 258); /* feet 300, centre column 50 */
        sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN);
        CHECK(a.on_ground && a.y == 258);
        a.vx = speed * U;
        prev_y = a.y;
        for (int k = 1; k <= 300 / speed; k++) {
            int col;
            sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN);
            col = a.x + SIDESCRL_ACTOR_WIDTH / 2;
            CHECK(a.x == 40 + k * speed);
            CHECK(a.on_ground && a.vy == 0 && !a.jumping);
            CHECK(feet(&a) == under(&a, &t)); /* glued to the surface */
            CHECK(prev_y - a.y >= 0 && prev_y - a.y <= speed);
            if (speed == 1 && col >= 101 && col <= 300)
                CHECK(prev_y - a.y == 1); /* 45 degrees: one pixel per step */
            if (a.y < prev_y) climbed++;
            prev_y = a.y;
        }
        CHECK(climbed > 0);
        CHECK(a.y == 100 - SIDESCRL_STAND_HEIGHT); /* on the plateau */
    }
    return 0;
}

/* A face taller than max_step_up is not a step. This module does not block
 * horizontal motion, so the actor loses its support there. */
static int test_tall_face(void) {
    int32_t y[N];
    Terrain t;
    SidescrlActor a;

    for (int c = 0; c < N; c++) y[c] = c < 100 ? 300 : 290; /* 10 px face */
    set_terrain(&t, y, N);
    sidescrl_actor_init(&a, 40, 258);
    sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN);
    CHECK(a.on_ground);
    a.vx = 3 * U;
    for (int k = 1; k <= 16; k++) { /* centre column 50 + 3k <= 98 */
        sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN);
        CHECK(a.on_ground && a.y == 258);
    }
    sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN); /* centre column 101 */
    CHECK(!a.on_ground);
    CHECK(a.y == 258); /* not teleported up the face */
    return 0;
}

/* ---- 2: walking down a ramp keeps the actor on the ground ---------------- */
static int test_ramp_down(void) {
    int32_t y[N];
    Terrain t;
    SidescrlActor a;
    int lost, landed;

    fill_ramp_down(y);
    set_terrain(&t, y, N);
    for (int speed = 1; speed <= 6; speed++) { /* 6 px/frame == max_step_down */
        sidescrl_actor_init(&a, 40, 58); /* feet 100, centre column 50 */
        sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN);
        CHECK(a.on_ground && a.y == 58);
        a.vx = speed * U;
        for (int k = 1; k <= 300 / speed; k++) {
            sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN);
            CHECK(a.x == 40 + k * speed);
            CHECK(a.on_ground && a.vy == 0); /* adheres: never enters free fall */
            CHECK(feet(&a) == under(&a, &t));
        }
        CHECK(a.y == 300 - SIDESCRL_STAND_HEIGHT);
    }

    /* faster than max_step_down: the ramp falls away, contact is lost, the
     * actor falls under gravity alone and lands on the lower plateau */
    sidescrl_actor_init(&a, 40, 58);
    sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN);
    a.vx = 7 * U;
    lost = 0;
    landed = 0;
    for (int k = 0; k < 600 && !landed; k++) {
        sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN);
        if (!a.on_ground) lost = 1;
        else if (lost) landed = 1;
    }
    CHECK(lost && landed);
    CHECK(a.y == 300 - SIDESCRL_STAND_HEIGHT && a.vy == 0);
    return 0;
}

/* ---- 3: ground ends abruptly -> free fall under gravity only ------------- */
static int test_abyss(void) {
    int32_t y[100];
    Terrain t;
    SidescrlActor a;
    int32_t prev_y, expect_vy;

    for (int c = 0; c < 100; c++) y[c] = 200;
    set_terrain(&t, y, 100); /* columns >= 100 have no ground */
    sidescrl_actor_init(&a, 30, 158); /* feet 200, centre column 40 */
    sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN);
    CHECK(a.on_ground && a.y == 158);
    a.vx = 3 * U;
    for (int k = 1; k <= 19; k++) { /* centre column 40 + 3k <= 97 */
        sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN);
        CHECK(a.on_ground && a.y == 158 && a.vy == 0);
    }
    sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN); /* centre column 100: the edge */
    CHECK(a.x == 90);
    CHECK(!a.on_ground);
    CHECK(a.vy == GRAV);

    /* from here on only gravity acts: vy grows by GRAV per frame up to the cap */
    prev_y = a.y;
    expect_vy = a.vy;
    for (int k = 0; k < 120; k++) {
        sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN);
        expect_vy += GRAV;
        CHECK(!a.on_ground);
        CHECK(a.vy == expect_vy);
        CHECK(a.y >= prev_y);
        CHECK(a.x == 90 + 3 * (k + 1)); /* horizontal motion is untouched */
        prev_y = a.y;
    }
    CHECK(a.vy == expect_vy);
    CHECK(a.y > 158 + 500);

    /* missing terrain function: always airborne */
    sidescrl_actor_init(&a, 0, 0);
    sidescrl_slope_step(&a, NULL, NULL, UP, DOWN);
    CHECK(!a.on_ground && a.vy == GRAV);
    sidescrl_slope_step(NULL, terrain_y, &t, UP, DOWN);
    return 0;
}

/* ---- boundary: how deep a drop is still followed ------------------------ */
static int test_drop_limits(void) {
    int32_t y[N];
    Terrain t;
    SidescrlActor a;

    for (int drop = 5; drop <= 8; drop++) {
        int landed = 0;

        for (int c = 0; c < N; c++) y[c] = c < 100 ? 200 : 200 + drop;
        set_terrain(&t, y, N);
        sidescrl_actor_init(&a, 30, 158);
        sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN);
        a.vx = 3 * U;
        for (int k = 1; k <= 19; k++) {
            sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN);
            CHECK(a.on_ground && a.y == 158);
        }
        sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN); /* centre column 100 */
        if (drop <= DOWN) { /* followed down at once */
            CHECK(a.on_ground && a.y == 158 + drop && a.vy == 0);
            continue;
        }
        CHECK(!a.on_ground && a.y == 158); /* too deep: contact lost */
        for (int k = 0; k < 300 && !landed; k++) {
            sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN);
            landed = a.on_ground;
        }
        CHECK(landed);
        CHECK(a.y == 200 + drop - SIDESCRL_STAND_HEIGHT && a.vy == 0);
    }
    return 0;
}

/* ---- landing from a fall and from a jump -------------------------------- */
static int test_fall_and_jump(void) {
    int32_t y[N];
    Terrain t;
    SidescrlActor a;
    int32_t vmax = 0, top;
    int landed = 0;

    for (int c = 0; c < N; c++) y[c] = 600;
    set_terrain(&t, y, N);
    sidescrl_actor_init(&a, 100, 0);
    for (int i = 0; i < 2000 && !landed; i++) {
        sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN);
        if (a.vy > vmax) vmax = a.vy;
        landed = a.on_ground;
        if (!landed) CHECK(feet(&a) <= 600); /* never sinks in */
    }
    CHECK(landed);
    CHECK(vmax > 8 * U); /* landed from a high-speed fall without tunnelling */
    CHECK(a.y == 600 - SIDESCRL_STAND_HEIGHT && a.vy == 0);
    for (int i = 0; i < 100; i++) {
        sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN);
        CHECK(a.on_ground && a.y == 558); /* stable, no flicker */
    }

    /* a jump leaves the ground and comes back to the same spot */
    a.vy = -6 * U;
    a.jumping = true;
    a.on_ground = false;
    sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN);
    CHECK(!a.on_ground && a.jumping && a.y < 558);
    top = a.y;
    landed = 0;
    for (int i = 0; i < 500 && !landed; i++) {
        sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN);
        if (a.y < top) top = a.y;
        landed = a.on_ground;
    }
    CHECK(landed && top < 538);
    CHECK(a.y == 558 && a.vy == 0 && !a.jumping);

    /* forgetting to clear on_ground when jumping must not glue the actor down */
    a.vy = -6 * U;
    a.jumping = true;
    sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN);
    CHECK(!a.on_ground && a.y < 558);
    return 0;
}

/* ---- random terrain: invariants and determinism ------------------------- */
static int test_fuzz(void) {
    int32_t y[N];
    Terrain t;
    SidescrlActor a, b;
    uint32_t seed = 31337;
    int32_t cur = 300;

    for (int i = 0; i < N; i++) {
        uint32_t r = rnd(&seed);
        int32_t step = (int32_t)(r % 7u) - 3;
        if ((r & 0x70u) == 0x70u) step = ((r >> 8) & 1u) ? 12 : -12;
        cur += step;
        if (cur < 120) cur = 120;
        if (cur > 500) cur = 500;
        y[i] = ((r & 0x3F00u) == 0x3F00u) ? SIDESCRL_SLOPE_NO_GROUND : cur;
    }
    set_terrain(&t, y, N);
    sidescrl_actor_init(&a, 50, 0);
    for (int f = 0; f < 6000; f++) {
        uint32_t r = rnd(&seed);
        bool was = a.on_ground;
        int32_t prev_y = a.y;

        a.vx = ((int32_t)(r % 9u) - 4) * U;
        if ((r & 0x3F0u) == 0x3F0u && a.on_ground) {
            a.vy = -6 * U;
            a.jumping = true;
            a.on_ground = false;
        }
        if (a.x < 0 || a.x > 1400 || a.y > 2000) {
            sidescrl_actor_init(&a, 50 + (int32_t)(r % 300u), 0);
            was = false;
            prev_y = a.y;
        }
        b = a;
        sidescrl_slope_step(&a, terrain_y, &t, UP, DOWN);
        sidescrl_slope_step(&b, terrain_y, &t, UP, DOWN);
        CHECK(a.x == b.x && a.y == b.y && a.vy == b.vy && a.rem_y == b.rem_y);
        CHECK(a.on_ground == b.on_ground && a.jumping == b.jumping);
        CHECK(a.rem_x >= 0 && a.rem_x < U && a.rem_y >= 0 && a.rem_y < U);
        if (a.on_ground) {
            CHECK(a.vy == 0 && !a.jumping);
            CHECK(under(&a, &t) != SIDESCRL_SLOPE_NO_GROUND);
            CHECK(feet(&a) == under(&a, &t));
        }
        if (was && a.on_ground) {
            int32_t d = a.y - prev_y;
            CHECK(d >= -UP && d <= DOWN); /* stayed grounded: moved within the window */
        }
    }
    return 0;
}

int main(void) {
    if (test_resolve()) return 1;
    if (test_ramp_up()) return 1;
    if (test_tall_face()) return 1;
    if (test_ramp_down()) return 1;
    if (test_abyss()) return 1;
    if (test_drop_limits()) return 1;
    if (test_fall_and_jump()) return 1;
    if (test_fuzz()) return 1;
    return 0;
}
