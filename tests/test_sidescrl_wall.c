#include "core/sidescrl_wall.h"
#include <stdio.h>
#define CHECK(c) do {if (!(c)) {printf("FAIL %d: %s\n",__LINE__,#c);return 1;}} while (0)

#define MAXSCAN SIDESCRL_WALL_MAX_SCAN

typedef struct { int32_t x, y, w, h; } Block;
typedef struct {
    const Block *blocks;
    int n;
    long calls;
    int32_t min_col, max_col, min_row, max_row;
} World;

static void world_init(World *w, const Block *b, int n) {
    w->blocks = b;
    w->n = n;
    w->calls = 0;
    w->min_col = INT32_MAX;
    w->max_col = INT32_MIN;
    w->min_row = INT32_MAX;
    w->max_row = INT32_MIN;
}
static bool world_solid(void *ctx, int32_t col, int32_t row) {
    World *w = (World *)ctx;
    w->calls++;
    if (col < w->min_col) w->min_col = col;
    if (col > w->max_col) w->max_col = col;
    if (row < w->min_row) w->min_row = row;
    if (row > w->max_row) w->max_row = row;
    for (int i = 0; i < w->n; i++) {
        const Block *b = &w->blocks[i];
        if (col >= b->x && col < b->x + b->w && row >= b->y && row < b->y + b->h) return true;
    }
    return false;
}
static int32_t clamp(const BoundingBox *a, int32_t dx, int32_t step, const Block *b, int n) {
    World w;
    world_init(&w, b, n);
    return sidescrl_wall_clamp_dx(a, dx, step, world_solid, &w);
}

/* A floor everywhere at row >= floor_y, and a 45 degree ramp (one row per column)
 * starting at ramp_col: its surface is floor_y - (col - ramp_col + 1). */
typedef struct { int32_t floor_y, ramp_col; } Ramp;
static bool ramp_solid(void *ctx, int32_t col, int32_t row) {
    const Ramp *r = (const Ramp *)ctx;
    if (row >= r->floor_y) return true;
    return col >= r->ramp_col && row >= r->floor_y - (col - r->ramp_col + 1);
}

static uint32_t rnd(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 16; }

/* The actor: columns [100,120), rows [158,200), feet at 200. */
static const BoundingBox ACTOR = {100, 158, 20, 42};

/* ---- free space: nothing locks up --------------------------------------- */
static int test_free_space(void) {
    World w;
    Block floor_only = {0, 200, 1000, 40};
    Block above_head = {130, 100, 10, 58};  /* rows 100..157: just above the head */
    Block touches_head = {130, 100, 10, 59}; /* rows 100..158: touches the top row */
    BoundingBox none = {100, 158, 0, 42}, flat = {100, 158, 20, 0}, tall = {100, 0, 20, 2000};

    CHECK(clamp(&ACTOR, 5, 4, NULL, 0) == 5);
    CHECK(clamp(&ACTOR, -7, 4, NULL, 0) == -7);
    CHECK(clamp(&ACTOR, 1, 4, NULL, 0) == 1);
    CHECK(clamp(&ACTOR, -1, 4, NULL, 0) == -1);
    CHECK(clamp(&ACTOR, 0, 4, NULL, 0) == 0);

    CHECK(clamp(&ACTOR, 30, 4, &floor_only, 1) == 30); /* the floor is below the feet */
    CHECK(clamp(&ACTOR, -30, 4, &floor_only, 1) == -30);
    CHECK(clamp(&ACTOR, -30, 0, &floor_only, 1) == -30); /* even with no step allowance */

    CHECK(clamp(&ACTOR, 15, 4, &above_head, 1) == 15);
    CHECK(clamp(&ACTOR, 15, 4, &touches_head, 1) == 10);

    /* the callback is asked only about the columns entered and the blocking rows */
    world_init(&w, NULL, 0);
    CHECK(sidescrl_wall_clamp_dx(&ACTOR, 5, 4, world_solid, &w) == 5);
    CHECK(w.calls == 5 * 38);
    CHECK(w.min_col == 120 && w.max_col == 124 && w.min_row == 158 && w.max_row == 195);
    world_init(&w, NULL, 0);
    CHECK(sidescrl_wall_clamp_dx(&ACTOR, -5, 4, world_solid, &w) == -5);
    CHECK(w.calls == 5 * 38);
    CHECK(w.min_col == 95 && w.max_col == 99 && w.min_row == 158 && w.max_row == 195);

    /* bounded work for huge requests */
    CHECK(clamp(&ACTOR, INT32_MAX, 4, NULL, 0) == MAXSCAN);
    CHECK(clamp(&ACTOR, INT32_MIN, 4, NULL, 0) == -MAXSCAN);
    CHECK(sidescrl_wall_clamp_dx(&ACTOR, INT32_MAX, 4, NULL, NULL) == MAXSCAN);
    CHECK(sidescrl_wall_clamp_dx(&ACTOR, -9, 4, NULL, NULL) == -9);

    /* degenerate input */
    CHECK(sidescrl_wall_clamp_dx(NULL, 5, 4, world_solid, &w) == 0);
    CHECK(clamp(&none, 6, 4, &floor_only, 1) == 6);   /* nothing to collide with */
    CHECK(clamp(&flat, -6, 4, &floor_only, 1) == -6);
    world_init(&w, NULL, 0);
    CHECK(sidescrl_wall_clamp_dx(&tall, 5, 4, world_solid, &w) == 0 && w.calls == 0);
    return 0;
}

/* ---- a wall that cannot be passed: stop flush against its face ----------- */
static int test_frontal_wall(void) {
    Block right = {130, 100, 10, 140};  /* columns 130..139 */
    Block left = {60, 100, 10, 140};    /* columns 60..69 */
    Block thin = {130, 150, 1, 60};     /* one pixel wide */
    Block thick = {130, 100, 100, 140};
    Block top_row = {130, 158, 10, 1};  /* only touches the top row of the body */
    BoundingBox flush_right = {110, 158, 20, 42}; /* right edge already at 130 */
    BoundingBox flush_left = {70, 158, 20, 42};   /* left edge already at 70 */

    CHECK(clamp(&ACTOR, 15, 4, &right, 1) == 10);
    CHECK(clamp(&ACTOR, 11, 4, &right, 1) == 10);
    CHECK(clamp(&ACTOR, 10, 4, &right, 1) == 10);
    CHECK(clamp(&ACTOR, 9, 4, &right, 1) == 9);
    CHECK(clamp(&ACTOR, 5, 4, &right, 1) == 5);
    CHECK(clamp(&flush_right, 1, 4, &right, 1) == 0);
    CHECK(clamp(&flush_right, 50, 4, &right, 1) == 0);

    CHECK(clamp(&ACTOR, -50, 4, &left, 1) == -30);
    CHECK(clamp(&ACTOR, -31, 4, &left, 1) == -30);
    CHECK(clamp(&ACTOR, -30, 4, &left, 1) == -30);
    CHECK(clamp(&ACTOR, -29, 4, &left, 1) == -29);
    CHECK(clamp(&flush_left, -1, 4, &left, 1) == 0);
    CHECK(clamp(&flush_left, -50, 4, &left, 1) == 0);

    CHECK(clamp(&ACTOR, 20, 4, &thin, 1) == 10);
    CHECK(clamp(&ACTOR, 20, 4, &thick, 1) == 10);   /* stops at the face, not inside */
    CHECK(clamp(&ACTOR, 20, 4, &top_row, 1) == 10);

    CHECK(clamp(&ACTOR, -5, 4, &right, 1) == -5);   /* moving away from a wall */
    CHECK(clamp(&ACTOR, 5, 4, &left, 1) == 5);

    /* two walls, one each side: each direction stops at its own */
    {
        Block both[2];
        both[0] = right;
        both[1] = left;
        CHECK(clamp(&ACTOR, 40, 4, both, 2) == 10);
        CHECK(clamp(&ACTOR, -40, 4, both, 2) == -30);
    }

    /* after the clamp the box touches the face, and one pixel more would enter it */
    {
        int32_t r = clamp(&ACTOR, 15, 4, &right, 1);
        BoundingBox wall = {130, 100, 10, 140};
        BoundingBox at = {ACTOR.x + r, ACTOR.y, ACTOR.w, ACTOR.h};
        BoundingBox past = {ACTOR.x + r + 1, ACTOR.y, ACTOR.w, ACTOR.h};
        CHECK(at.x + at.w == wall.x);
        CHECK(!aabb_intersect(&at, &wall) && aabb_intersect(&past, &wall));
    }
    {
        int32_t r = clamp(&ACTOR, -50, 4, &left, 1);
        BoundingBox wall = {60, 100, 10, 140};
        BoundingBox at = {ACTOR.x + r, ACTOR.y, ACTOR.w, ACTOR.h};
        BoundingBox past = {ACTOR.x + r - 1, ACTOR.y, ACTOR.w, ACTOR.h};
        CHECK(at.x == wall.x + wall.w);
        CHECK(!aabb_intersect(&at, &wall) && aabb_intersect(&past, &wall));
    }
    return 0;
}

/* ---- a low obstacle is passable: the slope resolver will step up -------- */
static int test_climbable_step(void) {
    Block step4 = {130, 196, 50, 44};   /* top at 196: 4 above the feet */
    Block step5 = {130, 195, 50, 45};   /* 5 above the feet */
    Block lstep = {70, 196, 10, 44};    /* left side, columns 70..79 */
    Block lnear = {80, 196, 20, 44};    /* left side, columns 80..99: first column entered */
    Block ledge1 = {130, 199, 10, 41};  /* a 1 pixel ledge */
    Block wall = {130, 100, 10, 140};
    Ramp ramp = {200, 120};
    int32_t r;

    CHECK(clamp(&ACTOR, 15, 4, &step4, 1) == 15);
    CHECK(clamp(&ACTOR, 15, 8, &step4, 1) == 15);
    CHECK(clamp(&ACTOR, 15, 3, &step4, 1) == 10);
    CHECK(clamp(&ACTOR, 15, 0, &step4, 1) == 10);
    CHECK(clamp(&ACTOR, 15, -3, &step4, 1) == 10);  /* negative counts as 0 */

    CHECK(clamp(&ACTOR, 15, 4, &step5, 1) == 10);
    CHECK(clamp(&ACTOR, 15, 5, &step5, 1) == 15);

    CHECK(clamp(&ACTOR, -30, 4, &lstep, 1) == -30);   /* climbable on the left too */
    CHECK(clamp(&ACTOR, -30, 3, &lstep, 1) == -20);
    CHECK(clamp(&ACTOR, -30, 3, &lnear, 1) == 0);     /* blocked at the first column */
    CHECK(clamp(&ACTOR, -30, 4, &lnear, 1) == -30);

    CHECK(clamp(&ACTOR, 15, 0, &ledge1, 1) == 10);    /* airborne: no allowance */
    CHECK(clamp(&ACTOR, 15, 1, &ledge1, 1) == 15);    /* grounded: a 1 px step */

    CHECK(clamp(&ACTOR, 15, 42, &wall, 1) == 15);     /* a step as tall as the actor */
    CHECK(clamp(&ACTOR, 15, 100, &wall, 1) == 15);
    CHECK(clamp(&ACTOR, 15, 41, &wall, 1) == 10);     /* one row short: top row blocks */

    /* 45 degree ramp: heights seen from the feet grow 1, 2, 3 ... per column */
    r = sidescrl_wall_clamp_dx(&ACTOR, 3, 4, ramp_solid, &ramp);
    CHECK(r == 3);
    r = sidescrl_wall_clamp_dx(&ACTOR, 4, 4, ramp_solid, &ramp);
    CHECK(r == 4);
    r = sidescrl_wall_clamp_dx(&ACTOR, 5, 4, ramp_solid, &ramp);
    CHECK(r == 4);                                    /* the 5th column is 5 high */
    r = sidescrl_wall_clamp_dx(&ACTOR, 9, 8, ramp_solid, &ramp);
    CHECK(r == 8);
    r = sidescrl_wall_clamp_dx(&ACTOR, 5, 0, ramp_solid, &ramp);
    CHECK(r == 0);                                    /* no allowance: first column blocks */
    r = sidescrl_wall_clamp_dx(&ACTOR, -5, 4, ramp_solid, &ramp);
    CHECK(r == -5);                                   /* walking away from the ramp */
    return 0;
}

/* ---- the edges of the int32_t range ------------------------------------- */
static int test_edges(void) {
    BoundingBox at_max = {INT32_MAX - 20, 158, 20, 42}; /* right edge at INT32_MAX */
    BoundingBox at_min = {INT32_MIN + 1, 158, 20, 42};
    BoundingBox wide = {0, 158, INT32_MAX, 42};
    BoundingBox low = {100, INT32_MAX - 10, 20, 42};    /* rows run past INT32_MAX */
    Block deep = {130, INT32_MAX - 5, 10, 5};

    CHECK(sidescrl_wall_clamp_dx(&at_max, 10, 0, NULL, NULL) == 10); /* no callback: no edge */
    {
        World w;
        world_init(&w, NULL, 0);
        CHECK(sidescrl_wall_clamp_dx(&at_max, 10, 0, world_solid, &w) == 1);
        world_init(&w, NULL, 0);
        CHECK(sidescrl_wall_clamp_dx(&at_min, -10, 0, world_solid, &w) == -1);
        world_init(&w, NULL, 0);
        CHECK(sidescrl_wall_clamp_dx(&wide, 5, 0, world_solid, &w) == 1);
    }
    CHECK(clamp(&at_max, -10, 0, NULL, 0) == -10);
    CHECK(clamp(&low, 15, 0, &deep, 1) == 10);          /* rows still seen below INT32_MAX */
    CHECK(clamp(&low, 15, 0, NULL, 0) == 15);
    return 0;
}

/* ---- random walls against a geometric oracle ---------------------------- */
static bool oracle_blocked(const Block *b, int n, int32_t col, int32_t row_lo, int32_t row_hi) {
    for (int i = 0; i < n; i++)
        if (col >= b[i].x && col < b[i].x + b[i].w && b[i].y < row_hi && b[i].y + b[i].h > row_lo)
            return true;
    return false;
}

static int test_fuzz(void) {
    uint32_t seed = 2024;

    for (int it = 0; it < 4000; it++) {
        Block blocks[5];
        int n = 1 + (int)(rnd(&seed) % 5u);
        BoundingBox a;
        int32_t dx, step, r, row_lo, row_hi, want, expect = 0;
        int blocked = 0;

        for (int i = 0; i < n; i++) {
            blocks[i].x = 60 + (int32_t)(rnd(&seed) % 160u);
            blocks[i].y = 100 + (int32_t)(rnd(&seed) % 160u);
            blocks[i].w = 1 + (int32_t)(rnd(&seed) % 15u);
            blocks[i].h = 1 + (int32_t)(rnd(&seed) % 80u);
        }
        a.x = 80 + (int32_t)(rnd(&seed) % 60u);
        a.y = 120 + (int32_t)(rnd(&seed) % 80u);
        a.w = 12 + (int32_t)(rnd(&seed) % 13u);
        a.h = 20 + (int32_t)(rnd(&seed) % 25u);
        dx = (int32_t)(rnd(&seed) % 51u) - 25;
        step = (int32_t)(rnd(&seed) % 13u) - 2;

        r = clamp(&a, dx, step, blocks, n);
        CHECK(r == clamp(&a, dx, step, blocks, n)); /* deterministic */

        row_lo = a.y;
        row_hi = a.y + a.h - (step > 0 ? step : 0);
        want = dx < 0 ? -dx : dx;
        for (int k = 0; k < want; k++) {
            int32_t col = dx > 0 ? a.x + a.w + k : a.x - 1 - k;
            if (row_hi > row_lo && oracle_blocked(blocks, n, col, row_lo, row_hi)) {
                blocked = 1;
                break;
            }
            expect++;
        }
        CHECK(r == (dx > 0 ? expect : -expect));
        CHECK(r == 0 || (r > 0) == (dx > 0));
        CHECK((r < 0 ? -r : r) <= want);

        if (row_hi > row_lo) { /* the strip entered is clear, the next column is not */
            int32_t len = r < 0 ? -r : r;
            int32_t sx = dx > 0 ? a.x + a.w : a.x - len;
            int32_t nx = dx > 0 ? a.x + a.w + len : a.x - len - 1;
            BoundingBox strip = {sx, row_lo, len, row_hi - row_lo};
            BoundingBox next = {nx, row_lo, 1, row_hi - row_lo};
            int hit_strip = 0, hit_next = 0;
            for (int i = 0; i < n; i++) {
                BoundingBox bb = {blocks[i].x, blocks[i].y, blocks[i].w, blocks[i].h};
                if (aabb_intersect(&strip, &bb)) hit_strip = 1;
                if (aabb_intersect(&next, &bb)) hit_next = 1;
            }
            CHECK(!hit_strip);
            if (blocked) CHECK(hit_next); /* when stopped, the next column is solid */
        }
    }
    return 0;
}

int main(void) {
    if (test_free_space()) return 1;
    if (test_frontal_wall()) return 1;
    if (test_climbable_step()) return 1;
    if (test_edges()) return 1;
    if (test_fuzz()) return 1;
    return 0;
}
