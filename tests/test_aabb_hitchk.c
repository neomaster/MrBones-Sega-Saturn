#include "core/aabb_hitchk.h"
#include <stdio.h>
#define CHECK(c) do {if (!(c)) {printf("FAIL %d: %s\n",__LINE__,#c);return 1;}} while (0)

#define L_PLAYER 0x0001u
#define L_TERRAIN 0x0002u
#define L_ENEMY 0x0004u

static BoundingBox bb(int32_t x, int32_t y, int32_t w, int32_t h) {
    BoundingBox b; b.x = x; b.y = y; b.w = w; b.h = h; return b;
}
static Collider col(BoundingBox b, uint16_t m) { Collider c; c.box = b; c.layer_mask = m; return c; }

int main(void) {
    /* aabb_intersect */
    BoundingBox a = bb(0, 0, 10, 10);
    BoundingBox b = bb(5, 5, 10, 10);
    CHECK(aabb_intersect(&a, &b) && aabb_intersect(&b, &a));
    b = bb(10, 0, 10, 10);  CHECK(!aabb_intersect(&a, &b)); /* touching right edge */
    b = bb(0, 10, 10, 10);  CHECK(!aabb_intersect(&a, &b)); /* touching bottom edge */
    b = bb(9, 9, 5, 5);     CHECK(aabb_intersect(&a, &b));  /* one pixel of overlap */
    b = bb(-10, 0, 10, 10); CHECK(!aabb_intersect(&a, &b)); /* touching left edge */
    b = bb(0, -10, 10, 10); CHECK(!aabb_intersect(&a, &b)); /* touching top edge */
    b = bb(2, 2, 3, 3);     CHECK(aabb_intersect(&a, &b) && aabb_intersect(&b, &a)); /* contained */
    b = bb(100, 100, 5, 5); CHECK(!aabb_intersect(&a, &b));
    b = bb(-5, -5, 10, 10); CHECK(aabb_intersect(&a, &b));  /* negative origin */
    CHECK(aabb_intersect(&a, &a));
    b = bb(2, 2, 0, 5);     CHECK(!aabb_intersect(&a, &b)); /* empty */
    b = bb(2, 2, 5, -3);    CHECK(!aabb_intersect(&a, &b));
    CHECK(!aabb_intersect(&b, &b));
    CHECK(!aabb_intersect(NULL, &a) && !aabb_intersect(&a, NULL) && !aabb_intersect(NULL, NULL));
    /* extremes: no signed overflow */
    a = bb(INT32_MAX - 4, INT32_MAX - 4, INT32_MAX, INT32_MAX);
    b = bb(INT32_MAX - 1, INT32_MAX - 1, 10, 10);
    CHECK(aabb_intersect(&a, &b));
    b = bb(INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX);
    CHECK(!aabb_intersect(&a, &b));
    a = bb(INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX); /* covers [INT32_MIN, -1) */
    b = bb(-2, -2, 2, 2);
    CHECK(aabb_intersect(&a, &b));
    b = bb(-1, -1, 2, 2);                                /* touches the edge only */
    CHECK(!aabb_intersect(&a, &b));

    /* layer matrix */
    CHECK(hitchk_layers_match(L_PLAYER, L_PLAYER));
    CHECK(!hitchk_layers_match(L_PLAYER, L_TERRAIN));
    CHECK(hitchk_layers_match(L_PLAYER | L_ENEMY, L_ENEMY | L_TERRAIN));
    CHECK(!hitchk_layers_match(0, 0xFFFF) && !hitchk_layers_match(0xFFFF, 0));
    {
        Collider p = col(bb(0, 0, 10, 10), L_PLAYER);
        Collider t = col(bb(5, 5, 10, 10), L_TERRAIN);
        Collider e = col(bb(5, 5, 10, 10), L_PLAYER | L_ENEMY);
        Collider far = col(bb(50, 50, 10, 10), L_PLAYER);
        CHECK(!hitchk_overlap(&p, &t));  /* geometry overlaps, layers do not */
        CHECK(aabb_intersect(&p.box, &t.box));
        CHECK(hitchk_overlap(&p, &e) && hitchk_overlap(&e, &p));
        CHECK(!hitchk_overlap(&p, &far)); /* layers match, geometry does not */
        CHECK(!hitchk_overlap(NULL, &p) && !hitchk_overlap(&p, NULL));
    }

    /* ground_clamp */
    {
        BoundingBox ground = bb(0, 100, 200, 50);
        BoundingBox actor = bb(10, 90, 20, 20);          /* bottom 110, 10 deep */
        CHECK(ground_clamp(&actor, &ground, INT32_MAX) == -10);
        actor = bb(10, 80, 20, 20);                      /* resting: bottom 100 */
        CHECK(ground_clamp(&actor, &ground, INT32_MAX) == 0);
        actor = bb(10, 70, 20, 20);                      /* in the air */
        CHECK(ground_clamp(&actor, &ground, INT32_MAX) == 0);
        actor = bb(300, 90, 20, 20);                     /* beside the block */
        CHECK(ground_clamp(&actor, &ground, INT32_MAX) == 0);
        actor = bb(200, 90, 20, 20);                     /* touching its right edge */
        CHECK(ground_clamp(&actor, &ground, INT32_MAX) == 0);
        actor = bb(10, 105, 20, 20);                     /* 25 deep */
        CHECK(ground_clamp(&actor, &ground, 8) == 0);    /* too deep for a floor snap */
        CHECK(ground_clamp(&actor, &ground, 24) == 0);
        CHECK(ground_clamp(&actor, &ground, 25) == -25);
        CHECK(ground_clamp(&actor, &ground, INT32_MAX) == -25);
        CHECK(ground_clamp(&actor, &ground, 0) == 0);    /* disabled */
        CHECK(ground_clamp(&actor, &ground, -5) == 0);
        CHECK(ground_clamp(NULL, &ground, INT32_MAX) == 0);
        CHECK(ground_clamp(&actor, NULL, INT32_MAX) == 0);
        actor = bb(10, 99, 20, 0);                       /* empty actor */
        CHECK(ground_clamp(&actor, &ground, INT32_MAX) == 0);
        /* actor already below the block's top edge but within max_snap: lifts */
        actor = bb(10, 120, 20, 20);                     /* 40 deep */
        CHECK(ground_clamp(&actor, &ground, 40) == -40);
        /* extremes stay in range */
        actor = bb(10, INT32_MAX - 10, 20, 100);
        ground = bb(0, 0, 100, INT32_MAX);
        CHECK(ground_clamp(&actor, &ground, INT32_MAX) == 0); /* depth exceeds int32 */
    }

    /* post-condition: after the clamp the actor no longer overlaps the block */
    {
        uint32_t seed = 7;
        for (unsigned i = 0; i < 5000; i++) {
            BoundingBox g, p;
            int32_t dy;
            seed = seed * 1664525u + 1013904223u; g.x = (int32_t)((seed >> 16) % 200) - 100;
            seed = seed * 1664525u + 1013904223u; g.y = (int32_t)((seed >> 16) % 200) - 100;
            seed = seed * 1664525u + 1013904223u; g.w = (int32_t)((seed >> 16) % 80) + 1;
            seed = seed * 1664525u + 1013904223u; g.h = (int32_t)((seed >> 16) % 80) + 1;
            seed = seed * 1664525u + 1013904223u; p.x = (int32_t)((seed >> 16) % 200) - 100;
            seed = seed * 1664525u + 1013904223u; p.y = (int32_t)((seed >> 16) % 200) - 100;
            seed = seed * 1664525u + 1013904223u; p.w = (int32_t)((seed >> 16) % 40) + 1;
            seed = seed * 1664525u + 1013904223u; p.h = (int32_t)((seed >> 16) % 40) + 1;
            dy = ground_clamp(&p, &g, INT32_MAX);
            CHECK(dy <= 0);
            if (aabb_intersect(&p, &g)) {
                CHECK(dy < 0);
                p.y += dy;
                CHECK(!aabb_intersect(&p, &g));
                CHECK(p.y + p.h == g.y);          /* minimal: bottom lands on the top edge */
                CHECK(ground_clamp(&p, &g, INT32_MAX) == 0); /* idempotent */
            } else {
                CHECK(dy == 0);
            }
        }
    }

    /* hitchk_ground_clamp */
    {
        /* (mask_a & mask_b) != 0: the actor must carry the terrain bit to collide. */
        Collider actor = col(bb(10, 95, 30, 20), L_PLAYER | L_TERRAIN); /* bottom 115 */
        Collider terrain[4];
        terrain[0] = col(bb(0, 100, 20, 50), L_TERRAIN);    /* lift 15 */
        terrain[1] = col(bb(20, 96, 20, 50), L_TERRAIN);    /* lift 19 */
        terrain[2] = col(bb(0, 90, 100, 50), L_ENEMY);      /* wrong layer: skipped */
        terrain[3] = col(bb(200, 100, 20, 50), L_TERRAIN);  /* not overlapping */
        CHECK(hitchk_ground_clamp(&actor, terrain, 4, INT32_MAX) == -19);
        CHECK(hitchk_ground_clamp(&actor, terrain, 1, INT32_MAX) == -15);
        CHECK(hitchk_ground_clamp(&actor, terrain, 4, 16) == -15);  /* 19-deep block ignored */
        CHECK(hitchk_ground_clamp(&actor, terrain, 4, 10) == 0);
        CHECK(hitchk_ground_clamp(&actor, terrain + 2, 1, INT32_MAX) == 0);
        CHECK(hitchk_ground_clamp(&actor, terrain, 0, INT32_MAX) == 0);
        CHECK(hitchk_ground_clamp(&actor, NULL, 4, INT32_MAX) == 0);
        CHECK(hitchk_ground_clamp(NULL, terrain, 4, INT32_MAX) == 0);
        actor.layer_mask = L_PLAYER;                        /* no terrain bit: nothing matches */
        CHECK(hitchk_ground_clamp(&actor, terrain, 4, INT32_MAX) == 0);
        actor.layer_mask = L_PLAYER | L_ENEMY;              /* matches terrain[2] only */
        CHECK(hitchk_ground_clamp(&actor, terrain + 2, 1, INT32_MAX) == -25);
        CHECK(hitchk_ground_clamp(&actor, terrain, 4, INT32_MAX) == -25);
        actor.layer_mask = 0;
        CHECK(hitchk_ground_clamp(&actor, terrain, 4, INT32_MAX) == 0);
    }
    return 0;
}
