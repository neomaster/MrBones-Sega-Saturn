#include "core/cdimage.h"
#include "core/identity.h"
#include "core/runway_sfx.h"
#include "core/saturn_pad.h"
#include "core/sidescrl_phase.h"
#include "tile_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

static int open_disc(cdimage *cd, const char *path, const char *man_path, int disc_no) {
    char err[512];
    id_manifest manifest;
    id_result result;
    if (manifest_load(&manifest, man_path, err, sizeof err)) return -1;
    if (cd_open(cd, path, err, sizeof err)) {
        manifest_free(&manifest);
        return -1;
    }
    if (!identity_check(&manifest, cd, disc_no, ID_QUICK, &result)) {
        fprintf(stderr, "identity disc%d: %s\n", disc_no, result.message);
        cd_close(cd);
        manifest_free(&manifest);
        return -1;
    }
    manifest_free(&manifest);
    return 0;
}

static int verify_batwood(sidescrl_phase *p, cdimage *disc, const char *iso,
                          uint32_t initial, uint32_t middle, uint32_t goal, unsigned pieces) {
    saturn_pad pad = {0};
    unsigned transitions = 0, hangs = 0, pulls = 0;
    CHECK(sidescrl_phase_load(p, disc, iso) == 0);
    CHECK(p->body_config == initial && p->exit_config == goal && p->body_piece_count == pieces);
    int sx = p->actor.x, sy = p->actor.y;
    p->actor.x = (int)(p->finish_x / 2341u) - SIDESCRL_ACTOR_WIDTH/2;
    p->actor.y = p->finish_y + p->floor_delta - SIDESCRL_STAND_HEIGHT;
    CHECK(!batwood_exit_contact(p));
    p->actor.x = sx;
    p->actor.y = sy;
    for (unsigned i = 0; i < 5000 && !sidescrl_phase_done(p); i++) {
        uint32_t previous = p->body_config;
        saturn_pad_update_host(&pad, sidescrl_phase_autoplay_buttons(p, i));
        sidescrl_phase_step(p, &pad);
        if (p->body_config != previous) {
            CHECK(p->body_config == (transitions == 0 ? middle : goal));
            transitions++;
        }
        if (p->latch == BAT_HANG && !hangs) {
            saturn_pad pause = pad;
            unsigned clock = p->frames;
            saturn_pad_update_host(&pause, MB_PAD_START | MB_PAD_C);
            sidescrl_phase_step(p, &pause);
            CHECK(p->state == SIDESCRL_ST_PAUSED && p->frames == clock);
            saturn_pad_update_host(&pause, MB_PAD_C);
            sidescrl_phase_step(p, &pause);
            saturn_pad_update_host(&pause, MB_PAD_START | MB_PAD_C);
            sidescrl_phase_step(p, &pause);
            CHECK(p->state != SIDESCRL_ST_PAUSED && p->frames == clock && p->latch == BAT_HANG);
        }
        if (p->latch == BAT_HANG) hangs++;
        if (p->latch == BAT_PULL) pulls++;
    }
    CHECK(transitions == 2 && hangs >= 8 && pulls >= 4 && p->collected);
    CHECK(p->state == SIDESCRL_ST_FINISHED && batwood_exit_contact(p));
    p->actor.y += 200;
    CHECK(!batwood_exit_contact(p));
    printf("%s won at frame %u, hang %u, pull %u, config 0x%x\n",
           iso, p->frames, hangs, pulls, (unsigned)p->body_config);
    sidescrl_phase_free(p);
    return 0;
}

static int play_to_finish(sidescrl_phase *ph) {
    saturn_pad pad = {0};
    int i;
    for (i = 0; i < 5000 && !sidescrl_phase_done(ph); i++) {
        saturn_pad_update_host(&pad,
                               sidescrl_phase_autoplay_buttons(ph, (unsigned)i));
        sidescrl_phase_step(ph, &pad);
    }
    if (ph->state != SIDESCRL_ST_FINISHED) {
        unsigned u = ph->enemies.units_per_pixel ? ph->enemies.units_per_pixel : 2341u;
        printf("play_to_finish incomplete: frames=%d state=%d x=%u/%u y=%u skel=%d status=%s\n",
               i, (int)ph->state, ph->bones_x / u, ph->scroll_w,
               ph->bones_y / u, (int)ph->skel.skeletism, ph->status);
    }
    return ph->state == SIDESCRL_ST_FINISHED;
}

/* ---- disc-less: the phase drives sidescrl_motion over a synthetic floor ------- */

#define RIG_W 640u
#define UPP 2341u
#define PX(units) ((int)((units) / UPP))

static uint8_t g_rig_sprite[16 * 32 * 4];

static int rig_init(sidescrl_phase *ph, const uint8_t floor_py[RIG_W], int spawn_px) {
    memset(ph, 0, sizeof *ph);
    ph->map.strip_w = RIG_W;
    memcpy(ph->map.floor_py, floor_py, RIG_W);
    ph->scroll_w = RIG_W;
    ph->scroll_h = 224;
    ph->scroll_rgba = calloc((size_t)RIG_W * 224u * 4u, 1);
    ph->frame_rgba = calloc(320u * 224u * 4u, 1);
    if (!ph->scroll_rgba || !ph->frame_rgba) return -1;
    ph->sprite_rgba = g_rig_sprite;
    ph->sprite_w = 16;
    ph->sprite_h = 32;
    ph->finish_x = 0xFFFFFFFFu; /* never finishes: these tests are about motion */
    ph->state = SIDESCRL_ST_STAND;
    runway_skel_init(&ph->skel);
    sidescrl_phase_spawn(ph, spawn_px);
    return 0;
}

static void rig_free(sidescrl_phase *ph) {
    free(ph->scroll_rgba);
    free(ph->frame_rgba);
}

static void rig_floor(uint8_t f[RIG_W], unsigned y) {
    unsigned x;
    for (x = 0; x < RIG_W; x++) f[x] = (uint8_t)y;
}

/* Floor `lo` up to column `at`, then rising `rows_per_col` per column until it reaches `top`. */
static void rig_ramp(uint8_t f[RIG_W], unsigned lo, unsigned top, unsigned at, unsigned rows_per_col) {
    unsigned x;
    for (x = 0; x < RIG_W; x++) {
        unsigned rise = x < at ? 0u : (x - at + 1u) * rows_per_col;
        unsigned y = (lo > top && rise < lo - top) ? lo - rise : top;
        f[x] = (uint8_t)(x < at ? lo : y);
    }
}

static void rig_run(sidescrl_phase *ph, uint32_t buttons, int frames) {
    saturn_pad pad;
    int i;
    memset(&pad, 0, sizeof pad);
    for (i = 0; i < frames; i++) {
        saturn_pad_update_host(&pad, buttons);
        sidescrl_phase_step(ph, &pad);
    }
}

/* Isolate the Disc 1 gait from slopes, knockback and camera movement. */
static int test_valley_stride(const sidescrl_phase *valley) {
    sidescrl_phase rig;
    uint8_t floor[RIG_W];
    rig_floor(floor, 180);
    CHECK(valley->walk_root != NULL && rig_init(&rig, floor, 320) == 0);
    rig.walk_root = valley->walk_root;
    for (int dir = -1; dir <= 1; dir += 2) {
        saturn_pad pad = {0};
        sidescrl_phase_spawn(&rig, 320);
        rig.actor.projection_px = valley->projection_px;
        rig.actor.vx = dir * 8064;
        int start = (int)rig.bones_x;
        for (unsigned i = 0; i < 48; i++) {
            saturn_pad_update_host(&pad, dir > 0 ? MB_PAD_RIGHT : MB_PAD_LEFT);
            sidescrl_phase_step(&rig, &pad);
            CHECK(rig.actor.on_ground && rig.actor.vx == dir * (i < 4 ? 8064 : 9024));
        }
        CHECK(rig.walk_tick == 0 && (int)rig.bones_x - start == dir * (4 * 8064 + 44 * 9024));
        rig_run(&rig, dir > 0 ? MB_PAD_RIGHT : MB_PAD_LEFT, 8);
        CHECK(rig.walk_tick == 8);
        rig_run(&rig, 0, 1);
        CHECK(rig.walk_tick == 0);
    }
    rig_free(&rig);
    return 0;
}

static int test_spawn_and_walk(void) {
    uint8_t f[RIG_W];
    sidescrl_phase ph;
    int i, last;
    saturn_pad pad;
    rig_floor(f, 180);
    CHECK(rig_init(&ph, f, 40) == 0);
    CHECK(PX(ph.bones_x) == 40 && PX(ph.bones_y) == 180);
    CHECK(ph.actor.on_ground && !ph.airborne && ph.facing == 1);
    memset(&pad, 0, sizeof pad);
    last = PX(ph.bones_x);
    for (i = 0; i < 60; i++) {
        saturn_pad_update_host(&pad, MB_PAD_RIGHT);
        sidescrl_phase_step(&ph, &pad);
        CHECK(PX(ph.bones_x) >= last); /* never backwards */
        CHECK(ph.bones_y == 180u * UPP && !ph.airborne); /* stays flush on a flat floor */
        last = PX(ph.bones_x);
    }
    CHECK(last > 150); /* ~3 px/frame once accelerated, not the legacy 6 */
    CHECK(last < 40 + 60 * 4);
    CHECK(ph.state == SIDESCRL_ST_WALK && ph.facing == 1);
    rig_run(&ph, 0, 10); /* friction: comes to rest */
    CHECK(ph.state == SIDESCRL_ST_STAND && ph.vel_x == 0);
    {
        int rest = PX(ph.bones_x);
        rig_run(&ph, 0, 5);
        CHECK(PX(ph.bones_x) == rest);
    }
    rig_run(&ph, MB_PAD_LEFT, 5);
    CHECK(ph.facing == -1);
    rig_free(&ph);
    return 0;
}

static int test_slopes(void) {
    static const unsigned steepness[] = {1, 2, 3};
    uint8_t f[RIG_W];
    sidescrl_phase ph;
    unsigned s;
    for (s = 0; s < sizeof steepness / sizeof steepness[0]; s++) {
        rig_ramp(f, 180, 120, 100, steepness[s]); /* 60 rows up from column 100 */
        CHECK(rig_init(&ph, f, 40) == 0);
        rig_run(&ph, MB_PAD_RIGHT, 250);
        CHECK(PX(ph.bones_x) > 100 + (int)(60u / steepness[s]) + 10); /* over the crest */
        CHECK(ph.bones_y == 120u * UPP && !ph.airborne); /* standing on the top */
        rig_free(&ph);
    }
    /* 5 rows per column is a wall: the actor stops at the foot and never gets over it. */
    rig_ramp(f, 180, 120, 100, 5);
    CHECK(rig_init(&ph, f, 40) == 0);
    rig_run(&ph, MB_PAD_RIGHT, 250);
    CHECK(PX(ph.bones_x) < 100 + 12);
    CHECK(ph.bones_y > 150u * UPP && !ph.airborne);
    rig_free(&ph);
    return 0;
}

static int test_wall(void) {
    uint8_t f[RIG_W];
    sidescrl_phase ph;
    unsigned x;
    rig_floor(f, 180);
    for (x = 200; x < RIG_W; x++) f[x] = 100; /* an 80-row face at column 200 */
    CHECK(rig_init(&ph, f, 40) == 0);
    rig_run(&ph, MB_PAD_RIGHT, 150);
    CHECK(PX(ph.bones_x) == 190); /* box [180, 200): flush against the face */
    CHECK(ph.state == SIDESCRL_ST_STAND && ph.vel_x == 0);
    CHECK(ph.bones_y == 180u * UPP);
    rig_free(&ph);
    return 0;
}

static int test_side_sound_cadence(void) {
    uint8_t floor[RIG_W];
    sidescrl_phase phase;
    saturn_pad pad = {0};
    runway_sfx_frame sounds;
    unsigned footfalls = 0;
    rig_floor(floor, 180);
    for (unsigned x = 200; x < RIG_W; ++x) floor[x] = 100;
    CHECK(rig_init(&phase, floor, 40) == 0);
    memcpy(phase.stage_name, "MAUSO", sizeof "MAUSO");
    phase.anim_count = 1;
    phase.anim_rgba[0] = g_rig_sprite;
    phase.anim_w[0] = 16;
    phase.anim_h[0] = 32;

    saturn_pad_update_host(&pad, MB_PAD_DOWN);
    runway_sfx_begin_frame(&sounds);
    sidescrl_phase_step(&phase, &pad);
    runway_sfx_end_frame();
    CHECK(sounds.count == 1u && sounds.commands[0].bank == 0x0cu);

    saturn_pad_update_host(&pad, MB_PAD_DOWN);
    runway_sfx_begin_frame(&sounds);
    sidescrl_phase_step(&phase, &pad);
    runway_sfx_end_frame();
    CHECK(sounds.count == 0u);

    for (unsigned tick = 0; tick < 150u; ++tick) {
        int32_t before = phase.actor.x;
        saturn_pad_update_host(&pad, MB_PAD_RIGHT);
        runway_sfx_begin_frame(&sounds);
        sidescrl_phase_step(&phase, &pad);
        runway_sfx_end_frame();
        CHECK(sounds.count == 0u); /* The Down cue is not a walk sound. */
        if (phase.sfx_flags & RUNWAY_SFX_FLAG_WALK) {
            CHECK((phase.frames & 3u) == 0 && phase.actor.x != before);
            ++footfalls;
        }
    }
    CHECK(footfalls > 0 && PX(phase.bones_x) == 190);
    rig_free(&phase);
    return 0;
}

static int test_world_edges(void) {
    uint8_t f[RIG_W];
    sidescrl_phase ph;
    rig_floor(f, 180);
    CHECK(rig_init(&ph, f, 40) == 0);
    rig_run(&ph, MB_PAD_LEFT, 100);
    CHECK(PX(ph.bones_x) == 4); /* four columns of margin, as before */
    rig_free(&ph);
    CHECK(rig_init(&ph, f, 600) == 0);
    rig_run(&ph, MB_PAD_RIGHT, 100);
    CHECK(PX(ph.bones_x) == (int)RIG_W - 10); /* the box never leaves the strip */
    rig_free(&ph);
    return 0;
}

static int jump_arc(int press_again_in_air, int *min_y, int *air_frames, unsigned *jumps,
                    unsigned *lands) {
    uint8_t f[RIG_W];
    sidescrl_phase ph;
    saturn_pad pad;
    int i;
    rig_floor(f, 180);
    if (rig_init(&ph, f, 300)) return 1;
    memset(&pad, 0, sizeof pad);
    *min_y = PX(ph.bones_y);
    *air_frames = 0;
    *jumps = *lands = 0;
    for (i = 0; i < 200; i++) {
        uint32_t buttons = (i == 0 || (press_again_in_air && i == 5)) ? MB_PAD_B : 0;
        saturn_pad_update_host(&pad, buttons);
        sidescrl_phase_step(&ph, &pad);
        if (ph.sfx_flags & RUNWAY_SFX_FLAG_JUMP) (*jumps)++;
        if (ph.sfx_flags & RUNWAY_SFX_FLAG_LAND) (*lands)++;
        if (PX(ph.bones_y) < *min_y) *min_y = PX(ph.bones_y);
        if (ph.airborne) {
            (*air_frames)++;
            if (ph.state != SIDESCRL_ST_AIR) return 2;
        }
    }
    if (ph.bones_y != 180u * UPP || ph.airborne) return 3;
    rig_free(&ph);
    return 0;
}

static int test_jump(void) {
    int min_a, min_b, air_a, air_b;
    unsigned jumps_a, jumps_b, lands_a, lands_b;
    CHECK(jump_arc(0, &min_a, &air_a, &jumps_a, &lands_a) == 0);
    CHECK(180 - min_a >= 40 && 180 - min_a <= 130); /* a real jump, not the legacy 400 px */
    CHECK(air_a >= 20 && air_a <= 120);
    CHECK(jumps_a == 1 && lands_a == 1); /* one JUMP and one LAND sound */
    CHECK(jump_arc(1, &min_b, &air_b, &jumps_b, &lands_b) == 0);
    CHECK(min_b == min_a && air_b == air_a && jumps_b == 1); /* no double jump */
    return 0;
}

static int test_drop(void) {
    uint8_t f[RIG_W];
    sidescrl_phase ph;
    saturn_pad pad;
    int i, was_air = 0;
    unsigned lands = 0, x;
    rig_floor(f, 150);
    for (x = 150; x < RIG_W; x++) f[x] = 200; /* a 50-row drop */
    CHECK(rig_init(&ph, f, 40) == 0);
    memset(&pad, 0, sizeof pad);
    for (i = 0; i < 200; i++) {
        saturn_pad_update_host(&pad, MB_PAD_RIGHT);
        sidescrl_phase_step(&ph, &pad);
        if (ph.airborne) was_air = 1;
        if (ph.sfx_flags & RUNWAY_SFX_FLAG_LAND) lands++;
    }
    CHECK(was_air && lands == 1); /* too deep to follow: a fall, then a landing */
    CHECK(ph.bones_y == 200u * UPP && !ph.airborne);
    CHECK(PX(ph.bones_x) > 150);
    rig_free(&ph);
    return 0;
}

/*
 * The view is applied once. A synthetic plane A of twenty 32-pixel blocks, each tagged so the
 * R of its first dot names it: screen column x must show plane column src_x + x, where src_x is
 * the camera pulled back a third of the screen and clamped to the strip. Drawing the strip at the
 * camera and cropping it from src_x as well put plane column 2*camera - 107 + x on screen.
 */
static int test_view_applied_once(void) {
    uint8_t cdb[SCRL_CDB_HEADER_BYTES + 64], til[20u * 400u], f[RIG_W];
    size_t cdb_size, til_size = 0;
    uint32_t d[16];
    unsigned b, i;
    sidescrl_phase ph;
    static const struct { unsigned cam_px, x; } k_view[] = {
        {400, 2}, {400, 26}, {150, 0}, {2000, 0}, {2000, 319}, {10, 6}
    };

    rig_floor(f, 180);
    CHECK(rig_init(&ph, f, 40) == 0);
    cdb_size = make_cdb_sized(cdb, 1, 640, 64);
    for (b = 0; b < 20u; b++) {
        const unsigned tags[1] = {b};
        all_cells(d, OWN(0));
        til_size = add_record(til, til_size, b, 1, tags, d);
    }
    memset(ph.palette, 0, sizeof ph.palette);
    for (i = 0; i < 1024; i++) {
        ph.palette[4 * i + 1] = (uint8_t)(i & 0xffu); /* R names the dot */
        ph.palette[4 * i + 2] = (uint8_t)(255u - (i & 0xffu));
        ph.palette[4 * i + 3] = (uint8_t)((i & 0xffu) / 2u);
    }
    CHECK(scrl_map_init(&ph.map, cdb, cdb_size, til, til_size, NULL, 0, NULL, 0, NULL, 0, NULL, 0,
                        NULL, 0, NULL, 0, NULL, 0) == 0);
    CHECK(scrl_map_cached_planes(&ph.map) == 1u);
    ph.map.strip_w = RIG_W;
    memcpy(ph.map.floor_py, f, RIG_W);
    for (i = 0; i < sizeof k_view / sizeof k_view[0]; i++) {
        int target = (int)k_view[i].cam_px - 106;
        unsigned src_x, col;
        uint8_t want_r;
        if (target < 0) target = 0;
        if (target > (int)(RIG_W - 320u)) target = (int)(RIG_W - 320u);
        src_x = (unsigned)target;
        ph.camera_x = k_view[i].cam_px * UPP;
        CHECK(sidescrl_phase_render(&ph) == 0);
        /* Row 0 of a block: dot (x mod 8, 0) of cell 0 is dot(tag, x mod 8). */
        col = src_x + k_view[i].x;
        want_r = dot(col / 32u, col % 8u);
        CHECK(ph.frame_rgba[((size_t)0 * 320u + k_view[i].x) * 4u + 0u] == want_r);
    }
    scrl_map_free(&ph.map);
    rig_free(&ph);
    return 0;
}

static int test_pit_fall(void) {
    uint8_t f[RIG_W];
    sidescrl_phase ph;
    saturn_pad pad = {0};
    unsigned x;
    int i;
    rig_floor(f, 180);
    for (x = 120; x < 152; x++) f[x] = SCRL_FLOOR_PIT;
    CHECK(rig_init(&ph, f, 40) == 0);
    ph.finish_x = (RIG_W - 60u) * UPP;
    saturn_pad_update_host(&pad, MB_PAD_RIGHT);
    for (i = 0; i < 200 && !sidescrl_phase_done(&ph); i++) {
        sidescrl_phase_step(&ph, &pad);
    }
    CHECK(ph.state == SIDESCRL_ST_LOST && ph.skel.died == 1);
    rig_free(&ph);
    return 0;
}

static int test_branch_launch(void) {
    sidescrl_phase ph;
    uint8_t floor[RIG_W];
    saturn_pad pad = {0};
    rig_floor(floor, 180);
    CHECK(rig_init(&ph, floor, 95) == 0);
    ph.plat_count = 1;
    ph.on_plat_idx = 0;
    ph.plats[0] = (sidescrl_plat){.x0 = 80, .x1 = 105, .y = 180, .anchor_x = 80,
                                  .kind = 1, .charge = 0x170a, .timer = 4};
    saturn_pad_update_host(&pad, MB_PAD_C);
    for (int i = 0; i < 3; i++) {
        sidescrl_phase_step(&ph, &pad);
        CHECK(ph.actor.on_ground && ph.on_plat_idx == 0);
    }
    sidescrl_phase_step(&ph, &pad);
    CHECK(!ph.actor.on_ground && ph.on_plat_idx == -1 && ph.actor.vy < 0);
    CHECK(ph.actor.spring_ticks == 19);
    rig_free(&ph);
    return 0;
}

static int test_integration(void) {
    CHECK(test_branch_launch() == 0);
    CHECK(test_spawn_and_walk() == 0);
    CHECK(test_slopes() == 0);
    CHECK(test_wall() == 0);
    CHECK(test_side_sound_cadence() == 0);
    CHECK(test_world_edges() == 0);
    CHECK(test_jump() == 0);
    CHECK(test_drop() == 0);
    CHECK(test_pit_fall() == 0);
    CHECK(test_view_applied_once() == 0);
    printf("OK sidescrl_phase motion (synthetic terrain)\n");
    return 0;
}

static int test_valley_mechanics(cdimage *disc1) {
    sidescrl_phase phase;
    saturn_pad tp = {0};
    unsigned i;
    int32_t low;
    CHECK(sidescrl_phase_load(&phase, disc1, "/06VALLEY") == 0);
    CHECK(phase.fence_count == 3u && phase.fence_key_count == 3u && phase.blue_tomb_count == 2u);
    for (i = 0; i < phase.enemies.count; i++)
        phase.enemies.ents[i].active = 0;
    for (i = 0; i < 3u; i++) {
        int fen_floor_y = phase.fences[i].y;
        for (int y = phase.fences[i].y - 32; y <= phase.fences[i].y + 120; y++) {
            if (scrl_tilecache_hixel(&phase.map.planes[0].tiles,
                                     (unsigned)(phase.fences[i].x - 16), (unsigned)y) > 0 &&
                scrl_tilecache_hixel(&phase.map.planes[0].tiles,
                                     (unsigned)(phase.fences[i].x - 16), (unsigned)(y - 1)) <= 0) {
                fen_floor_y = y;
                break;
            }
        }
        /* Blocked while paired fence is closed. */
        CHECK(phase.fences[i].open == 0 && phase.fence_keys[i].triggered == 0);
        phase.actor.x = phase.fences[i].x - 20 - SIDESCRL_ACTOR_WIDTH / 2;
        phase.actor.y = fen_floor_y - SIDESCRL_STAND_HEIGHT;
        phase.actor.vx = phase.actor.rem_x = phase.actor.vy = phase.actor.rem_y = 0;
        phase.actor.on_ground = true;
        for (int s = 0; s < 16; s++) {
            saturn_pad_update_host(&tp, MB_PAD_RIGHT);
            sidescrl_phase_step(&phase, &tp);
        }
        CHECK(phase.actor.x + SIDESCRL_ACTOR_WIDTH / 2 < phase.fences[i].x - 4);
        /* Negative: C button / standing on ground at key must NOT trigger CFenceKey. */
        {
            int ground_y = phase.fence_keys[i].y;
            while (ground_y < phase.fence_keys[i].y + 32 &&
                   scrl_tilecache_hixel(&phase.map.planes[0].tiles,
                                        (unsigned)phase.fence_keys[i].x,
                                        (unsigned)ground_y) <= 0)
                ground_y++;
            phase.actor.x = phase.fence_keys[i].x - SIDESCRL_ACTOR_WIDTH / 2;
            phase.actor.y = ground_y - SIDESCRL_STAND_HEIGHT;
            phase.actor.vy = phase.actor.rem_y = 0;
            phase.actor.on_ground = true;
            saturn_pad_update_host(&tp, MB_PAD_C);
            runway_sfx_frame sounds;
            runway_sfx_begin_frame(&sounds);
            sidescrl_phase_step(&phase, &tp);
            runway_sfx_end_frame();
            CHECK(phase.fence_keys[i].triggered == 0 && phase.fences[i].open == 0);
            CHECK(sounds.count == 0u);
        }
        /* Positive: downward drop onto key triggers it and opens ONLY the paired fence. */
        phase.actor.x = phase.fence_keys[i].x - SIDESCRL_ACTOR_WIDTH / 2;
        phase.actor.y = phase.fence_keys[i].y - SIDESCRL_STAND_HEIGHT - 4;
        phase.actor.vy = 2000;
        phase.actor.on_ground = false;
        saturn_pad_update_host(&tp, 0u);
        runway_sfx_frame sounds;
        runway_sfx_begin_frame(&sounds);
        sidescrl_phase_step(&phase, &tp);
        runway_sfx_end_frame();
        CHECK(phase.fence_keys[i].triggered == 1 && phase.fences[i].open == 1);
        CHECK(sounds.count == 1u && sounds.commands[0].bank == 0x38u);
        for (unsigned j = i + 1u; j < 3u; j++)
            CHECK(phase.fences[j].open == 0);
        /* Once open, walking past fence x succeeds on the real cave floor. */
        int start_x = phase.fences[i].x - 16;
        int start_y = phase.fences[i].y;
        start_y += scrl_tilecache_floor_offset(&phase.map.planes[0].tiles, start_x, start_y);
        phase.actor.x = start_x - SIDESCRL_ACTOR_WIDTH / 2;
        phase.actor.y = start_y - SIDESCRL_STAND_HEIGHT;
        phase.actor.vx = phase.actor.rem_x = phase.actor.vy = phase.actor.rem_y = 0;
        phase.actor.on_ground = true;
        for (int s = 0; s < 16; s++) {
            saturn_pad_update_host(&tp, MB_PAD_RIGHT);
            sidescrl_phase_step(&phase, &tp);
        }
        CHECK(phase.actor.x + SIDESCRL_ACTOR_WIDTH / 2 > phase.fences[i].x + 6);
    }
    /* CBlueTomb heals +0x12c00 once when standing still on ground at tomb. */
    runway_skel_init(&phase.skel);
    CHECK(runway_skel_loose(&phase.skel, RUNWAY_SKEL_MAX - 0x1000) == 0);
    low = phase.skel.skeletism;
    {
        int tomb_x = phase.blue_tombs[0].x;
        int tomb_y = phase.blue_tombs[0].y;
        tomb_y += scrl_tilecache_floor_offset(&phase.map.planes[0].tiles, tomb_x, tomb_y);
        phase.actor.x = tomb_x - SIDESCRL_ACTOR_WIDTH / 2;
        phase.actor.y = tomb_y - SIDESCRL_STAND_HEIGHT;
        phase.actor.vx = phase.actor.rem_x = phase.actor.vy = phase.actor.rem_y = 0;
        phase.actor.on_ground = true;
    }
    saturn_pad_update_host(&tp, 0u);
    sidescrl_phase_step(&phase, &tp);
    CHECK(phase.blue_tombs[0].used == 1 && phase.skel.skeletism == low + 0x12c00);
    runway_skel_loose(&phase.skel, 0x1000);
    sidescrl_phase_step(&phase, &tp);
    CHECK(phase.skel.skeletism == low + 0x12c00 - 0x1000);
    sidescrl_phase_free(&phase);
    return 0;
}

static int test_bat1_mechanics(cdimage *disc1) {
    sidescrl_phase phase;
    saturn_pad tp = {0};
    unsigned i;
    int saved_y;
    CHECK(sidescrl_phase_load(&phase, disc1, "/08BAT1") == 0);
    CHECK(phase.body_piece_count == 11u && phase.exit_config == 0x2d7074u);
    /* Negative: reaching exit (53, 80) without exit_config cannot clear stage. */
    phase.actor.x = 53 - SIDESCRL_ACTOR_WIDTH / 2;
    phase.actor.y = 80 - SIDESCRL_STAND_HEIGHT + phase.floor_delta;
    CHECK(batwood_exit_contact(&phase) == 0);
    /* Hit while wearing 0x2d66c4 detaches to 0x2d69ac and respawns a CAnyBone piece. */
    phase.collected = (1u << phase.body_piece_count) - 1u;
    phase.body_config = 0x2d66c4u;
    phase.skel.hit_cooldown = 0;
    phase.hurt_timer = 0;
    for (i = 0; i < phase.enemies.count; i++) phase.enemies.ents[i].active = 0;
    CHECK(phase.enemies.count > 0u);
    phase.enemies.ents[0].active = 1;
    phase.enemies.ents[0].kind = STAGE_ENEMY_BAT;
    phase.enemies.ents[0].hp = 4;
    sidescrl_phase_spawn(&phase, 53);
    phase.enemies.ents[0].x = phase.bones_x;
    phase.enemies.ents[0].y = phase.bones_y - 12u * UPP;
    phase.enemies.ents[0].vx = 0;
    saturn_pad_update_host(&tp, 0u);
    sidescrl_phase_step(&phase, &tp);
    CHECK(phase.body_config == 0x2d69acu);
    CHECK((phase.collected & 1u) == 0u && phase.body_pieces[0].falling == 1);
    sidescrl_phase_free(&phase);

    /* Positive full finish gives exit_config 0x2d7074; wrong Y rejects exit contact. */
    CHECK(sidescrl_phase_load(&phase, disc1, "/08BAT1") == 0);
    CHECK(play_to_finish(&phase));
    CHECK(phase.body_config == phase.exit_config && batwood_exit_contact(&phase) != 0);
    saved_y = phase.actor.y;
    phase.actor.y = saved_y + 150;
    CHECK(batwood_exit_contact(&phase) == 0);
    phase.actor.y = saved_y;
    sidescrl_phase_free(&phase);
    return 0;
}

static int test_mauso_mechanics(cdimage *disc1) {
    sidescrl_phase phase;
    saturn_pad tp = {0};
    CHECK(sidescrl_phase_load(&phase, disc1, "/05MAUSO") == 0);
    CHECK(phase.mauso.enabled == 1 && phase.mauso.phase == 1 && phase.mauso.bh_active == 1);
    /* Step inside arena so lock_edges activates, then verify right barrier clamps past 652. */
    sidescrl_phase_spawn(&phase, phase.mauso.left_edge + 30);
    saturn_pad_update_host(&tp, 0u);
    sidescrl_phase_step(&phase, &tp);
    CHECK(phase.mauso.lock_edges == 1);
    phase.actor.x = 652 - SIDESCRL_ACTOR_WIDTH / 2;
    saturn_pad_update_host(&tp, MB_PAD_RIGHT);
    sidescrl_phase_step(&phase, &tp);
    CHECK(phase.actor.x + SIDESCRL_ACTOR_WIDTH / 2 <= 644);
    CHECK(sidescrl_phase_boss_remaining(&phase) == 1u && phase.state != SIDESCRL_ST_FINISHED);
    /* CBlueHead heals up to full 0x20000 once. */
    runway_skel_loose(&phase.skel, 0x10000);
    phase.actor.x = phase.mauso.bh_x - SIDESCRL_ACTOR_WIDTH / 2;
    phase.actor.y = phase.mauso.bh_y - SIDESCRL_STAND_HEIGHT;
    saturn_pad_update_host(&tp, 0u);
    sidescrl_phase_step(&phase, &tp);
    CHECK(phase.mauso.bh_active == 0 && phase.skel.skeletism == 0x20000);
    sidescrl_phase_free(&phase);
    return 0;
}

int main(void) {
    const char *d1 = getenv("MB_DISC1");
    const char *d2 = getenv("MB_DISC2");
    const char *man = getenv("MB_MANIFEST");
    cdimage disc1, disc2;
    sidescrl_phase phase;
    int opened2 = 0;

    if (test_integration()) return 1;
    if (!d1 || !man) {
        printf("SKIP sidescrl_phase (set MB_DISC1 and MB_MANIFEST)\n");
        return 0;
    }
    CHECK(open_disc(&disc1, d1, man, 1) == 0);
    if (d2 && open_disc(&disc2, d2, man, 2) == 0)
        opened2 = 1;

    CHECK(sidescrl_phase_load(&phase, &disc1, "/05MAUSO") == 0);
    CHECK(phase.layer_count > 0);
    CHECK(phase.textures_size > 0);
    CHECK(phase.frame_rgba != NULL);
    CHECK(phase.map.plane_count >= 4); /* §23 LoadCDB planes */
    CHECK(phase.map.planes[0].parallax_coeff == 0x10000);
    CHECK(phase.map.planes[1].parallax_coeff == 0x06000);
    CHECK(phase.map.planes[2].parallax_coeff == 0x00000);
    CHECK(phase.map.planes[3].parallax_coeff == 0x10000);
    CHECK(phase.map.strip_w >= 320);
    CHECK(scrl_map_find_floor(&phase.map, 40) > 0);
    /* MAUSO parallax wiring: B at 3/8 raised 90 rows, C static, D one to one raised 100. */
    {
        unsigned pi;
        for (pi = 0; pi < phase.map.plane_count; pi++) {
            const scrl_plane *pl = &phase.map.planes[pi];
            if (strcmp(pl->cdb_name, "09BCDB") == 0)
                CHECK(pl->parallax_coeff == 0x6000 && pl->base_y == -90);
            if (strcmp(pl->cdb_name, "11CCDB") == 0)
                CHECK(pl->parallax_coeff == 0 && pl->base_y == 0);
            if (strcmp(pl->cdb_name, "13DCDB") == 0)
                CHECK(pl->parallax_coeff == 0x10000 && pl->base_y == -100);
        }
    }
    CHECK(phase.enemies.pickup_count >= 1u);
    {
        saturn_pad tp = {0};
        /* Down is a ground action. The retail initial position can be in the air. */
        for (int f = 0; f < 180 && !phase.actor.on_ground; f++)
            sidescrl_phase_step(&phase, &tp);
        CHECK(phase.actor.on_ground);
        saturn_pad_update_host(&tp, MB_PAD_DOWN);
        sidescrl_phase_step(&phase, &tp);
        CHECK(phase.tall == 1 && (phase.sfx_flags & RUNWAY_SFX_FLAG_TALL));
        saturn_pad_update_host(&tp, MB_PAD_C);
        sidescrl_phase_step(&phase, &tp);
        CHECK(phase.tall == 0);
        CHECK(phase.enemies.bolts_fired >= 1u &&
              (phase.sfx_flags & RUNWAY_SFX_FLAG_C));
        saturn_pad_update_host(&tp, MB_PAD_A);
        sidescrl_phase_step(&phase, &tp);
        CHECK(phase.enemies.bolts_fired >= 2u);
    }
    CHECK(phase.scroll_w == 1176u);
    {
        unsigned u = phase.enemies.units_per_pixel ? phase.enemies.units_per_pixel : UPP;
        CHECK(phase.finish_x > 1000u * u);
    }
    CHECK(play_to_finish(&phase));
    CHECK(sidescrl_phase_boss_remaining(&phase) == 0u);
    printf("MAUSO: frames=%u w=%u layers=%u planes=%u %s\n", phase.frames,
           phase.scroll_w, phase.layer_count, phase.map.plane_count, phase.status);
    sidescrl_phase_free(&phase);
    CHECK(test_mauso_mechanics(&disc1) == 0);

    CHECK(sidescrl_phase_load(&phase, &disc1, "/06VALLEY") == 0);
    CHECK(phase.map.has_amap == 1 || phase.map.plane_count >= 1);
    CHECK(phase.map.plane_count >= 4);
    CHECK(phase.map.planes[1].parallax_coeff == 0x06000);
    CHECK(phase.map.planes[2].parallax_coeff == 0x03000);
    CHECK(phase.scroll_w == 15808u && phase.plat_count == 3);
    CHECK(phase.projection_px == 42u && phase.initial.hit.top_left_x == -7);
    CHECK(phase.map.vfloor[80] == 734 && phase.map.vfloor[380] == 1122);
    CHECK(scrl_tilecache_floor_offset(&phase.map.planes[0].tiles, 80, 733) == 0);
    CHECK(scrl_tilecache_floor_offset(&phase.map.planes[0].tiles, 80, 734) == -2);
    CHECK(scrl_tilecache_floor_offset(&phase.map.planes[0].tiles, 80, 730) == 2);
    CHECK(phase.map.floor_py[470] != SCRL_FLOOR_PIT);
    CHECK(phase.actor.x + SIDESCRL_ACTOR_WIDTH / 2 == 600);
    CHECK(phase.actor.y + SIDESCRL_STAND_HEIGHT == 818);
    CHECK(phase.finish_x / UPP == 15704u);
    CHECK(test_valley_stride(&phase) == 0);
    CHECK(phase.finish_y == 68 && phase.finish_width == 10000 && phase.finish_height == 10000);
    CHECK(play_to_finish(&phase));
    printf("VALLEY: frames=%u w=%u amap=%u planes=%u %s\n", phase.frames,
           phase.scroll_w, phase.map.amap_n, phase.map.plane_count, phase.status);
    sidescrl_phase_free(&phase);
    CHECK(test_valley_mechanics(&disc1) == 0);

    CHECK(sidescrl_phase_load(&phase, &disc1, "/08BAT1") == 0);
    CHECK(phase.scroll_w == 3160u && phase.plat_count == 18u);
    CHECK(stage_enemies_count_kind(&phase.enemies, STAGE_ENEMY_BAT) >= 1u);
    CHECK(play_to_finish(&phase));
    printf("BAT1: frames=%u w=%u %s\n", phase.frames, phase.scroll_w, phase.status);
    sidescrl_phase_free(&phase);
    CHECK(test_bat1_mechanics(&disc1) == 0);

    CHECK(verify_batwood(&phase, &disc1, "/09BAT2", 0x2d89b0, 0x2d84d8, 0x2d83e0, 12) == 0);
    CHECK(verify_batwood(&phase, &disc1, "/10BAT3", 0x2da550, 0x2da458, 0x2da170, 19) == 0);

    CHECK(sidescrl_phase_load(&phase, &disc1, "/15GLASS") == 0);
    CHECK(phase.scroll_w == 1280u);
    CHECK(phase.glass.enabled == 1 && phase.glass.total_records == 176u &&
          phase.glass.shard_count >= 160u && phase.glass.subpalette_lit > 0u);
    {
        unsigned u = phase.enemies.units_per_pixel ? phase.enemies.units_per_pixel : UPP;
        saturn_pad tp = {0};
        unsigned si, trap_idx = phase.glass.shard_count;
        for (si = 0; si < phase.glass.shard_count; si++) {
            if ((phase.glass.shards[si].kind == 2u || phase.glass.shards[si].kind == 3u) &&
                phase.glass.shards[si].x0 + 4u < phase.map.strip_w &&
                phase.map.floor_py[phase.glass.shards[si].x0 + 4u] < 224u) {
                trap_idx = si;
                break;
            }
        }
        CHECK(trap_idx < phase.glass.shard_count && phase.glass.shards[trap_idx].broken == 0);
        sidescrl_phase_spawn(&phase, (int)phase.glass.shards[trap_idx].x0 + 4);
        saturn_pad_update_host(&tp, MB_PAD_C);
        for (si = 0; si < 6u && !phase.glass.shards[trap_idx].broken; si++) {
            sidescrl_phase_step(&phase, &tp);
            saturn_pad_update_host(&tp, 0u);
        }
        CHECK(phase.glass.shards[trap_idx].broken == 1);
        /* Negative: exit blocked at finish_x while CKeyHole remains locked. */
        sidescrl_phase_spawn(&phase, (int)(phase.finish_x / u) - 2);
        CHECK(sidescrl_phase_boss_remaining(&phase) == 1u);
        saturn_pad_update_host(&tp, MB_PAD_RIGHT);
        sidescrl_phase_step(&phase, &tp);
        CHECK(phase.state != SIDESCRL_ST_FINISHED && phase.bones_x < phase.finish_x);
        /* Positive: CKeyHole lock-in unlocks exit. */
        CHECK(sidescrl_glass_unlock_keyhole(&phase) == 0 && phase.glass.keyhole_unlocked == 1);
        CHECK(sidescrl_phase_boss_remaining(&phase) == 0u);
    }
    sidescrl_phase_free(&phase);
    CHECK(sidescrl_phase_load(&phase, &disc1, "/15GLASS") == 0);
    CHECK(play_to_finish(&phase));
    CHECK(phase.glass.keyhole_unlocked == 1 && sidescrl_phase_boss_remaining(&phase) == 0u);
    printf("GLASS: frames=%u w=%u shards=%u lit=%u %s\n", phase.frames,
           phase.scroll_w, phase.glass.shard_count, phase.glass.subpalette_lit,
           phase.status);
    sidescrl_phase_free(&phase);

    CHECK(sidescrl_phase_load(&phase, &disc1, "/17VORTEX") == 0);
    CHECK(phase.scroll_w == 640u);
    CHECK(stage_enemies_count_kind(&phase.enemies, STAGE_ENEMY_POOF_BLUE) >= 1u);
    CHECK(phase.vortex.enabled == 1 && phase.vortex.ring_count == 17u);
    /* D2: VORTEX activates the feathered horizontal blend on plane A. */
    if (phase.map.plane_count > 0)
        CHECK(phase.map.planes[0].blend_mode == 1u);
    {
        unsigned u = phase.enemies.units_per_pixel ? phase.enemies.units_per_pixel : UPP;
        saturn_pad tp = {0};
        /* Negative: exit blocked before all 17 CGlass wave rings complete. */
        sidescrl_phase_spawn(&phase, (int)(phase.finish_x / u) - 2);
        CHECK(sidescrl_phase_boss_remaining(&phase) == 1u);
        saturn_pad_update_host(&tp, MB_PAD_RIGHT);
        sidescrl_phase_step(&phase, &tp);
        CHECK(phase.state != SIDESCRL_ST_FINISHED && phase.bones_x < phase.finish_x);
    }
    sidescrl_phase_free(&phase);
    CHECK(sidescrl_phase_load(&phase, &disc1, "/17VORTEX") == 0);
    CHECK(play_to_finish(&phase));
    CHECK(phase.vortex.rings_done == 17u && phase.vortex.exit_unlocked == 1 &&
          sidescrl_phase_boss_remaining(&phase) == 0u);
    printf("VORTEX: frames=%u w=%u rings=%u %s\n", phase.frames, phase.scroll_w,
           phase.vortex.rings_done, phase.status);
    sidescrl_phase_free(&phase);

    if (opened2) {
        CHECK(sidescrl_phase_load(&phase, &disc2, "/19VILLAGE") == 0);
        CHECK(phase.map.plane_count >= 4);
        CHECK(phase.map.planes[1].parallax_coeff == 0x0C000);
        CHECK(phase.map.planes[2].parallax_coeff == 0x08000);
        CHECK(phase.map.planes[3].parallax_coeff == 0x14000);
        CHECK(phase.scroll_w == 1600u);
        {
            unsigned u = phase.enemies.units_per_pixel ? phase.enemies.units_per_pixel : UPP;
            CHECK(phase.finish_x > 1400u * u);
        }
        CHECK(phase.enemies.prop_count == 8u);
        CHECK(phase.enemies.props[0].x == 347 && phase.enemies.props[0].y == 289);
        CHECK(phase.enemies.props[0].anchor_x == 26 && phase.enemies.props[0].anchor_y == 48);
        CHECK(stage_enemies_count_kind(&phase.enemies, STAGE_ENEMY_HAND) >= 1u);
        CHECK(stage_enemies_count_kind(&phase.enemies, STAGE_ENEMY_LILGUYS) >= 1u);
        CHECK(phase.village.enabled == 1 && phase.village.spider_count == 8 &&
              phase.village.mushroom_count == 5 && phase.village.bh_active == 1);
        /* Negative: uncut spider retracting with grabbed Little Guy executes EatLittleGuy. */
        {
            saturn_pad tp = {0};
            phase.village.spiders[0].state = 1;
            phase.village.spiders[0].has_guy = 1;
            phase.village.spiders[0].cur_y = phase.village.spiders[0].top_y + 2;
            saturn_pad_update_host(&tp, 0u);
            sidescrl_phase_step(&phase, &tp);
            CHECK(phase.village.little_guys_eaten >= 1u && phase.village.little_guys_saved == 0u);
            /* Positive: Blues cut on active silk thread drops Little Guy safely (+meter, +saved). */
            phase.village.spiders[0].state = 1;
            phase.village.spiders[0].has_guy = 1;
            phase.village.spiders[0].cur_y = phase.village.spiders[0].top_y + 60;
            CHECK(sidescrl_village_cut_thread(&phase, phase.village.spiders[0].x,
                                              phase.village.spiders[0].top_y + 30) == 1);
            CHECK(phase.village.little_guys_saved == 1u && phase.village.spider_meter == 10);
        }
        sidescrl_phase_free(&phase);
        CHECK(sidescrl_phase_load(&phase, &disc2, "/19VILLAGE") == 0);
        CHECK(play_to_finish(&phase));
        printf("VILLAGE: frames=%u w=%u props=%u layers=%u %s\n", phase.frames,
               phase.scroll_w, phase.enemies.prop_count, phase.layer_count,
               phase.status);
        sidescrl_phase_free(&phase);

        CHECK(sidescrl_phase_load(&phase, &disc2, "/22SHADOW") == 0);
        CHECK(phase.scroll_w == 640u);
        CHECK(stage_enemies_count_kind(&phase.enemies, STAGE_ENEMY_SHADOW) >= 1u);
        CHECK(phase.shadow.enabled == 1 && phase.shadow.config_ok == 1 &&
              phase.shadow.bh_active == 1);
        {
            unsigned u = phase.enemies.units_per_pixel ? phase.enemies.units_per_pixel : UPP;
            saturn_pad tp = {0};
            int prev_skel = phase.skel.skeletism;
            unsigned prev_pen;
            /* Negative 1: moving during active gaze (>= 140) without crouch triggers penalty. */
            phase.frames = 150u;
            saturn_pad_update_host(&tp, MB_PAD_RIGHT);
            for (int k = 0; k < 4; k++) sidescrl_phase_step(&phase, &tp);
            CHECK(phase.shadow.gaze_penalties > 0u && phase.skel.skeletism < prev_skel);
            prev_pen = phase.shadow.gaze_penalties;
            prev_skel = phase.skel.skeletism;
            saturn_pad_update_host(&tp, MB_PAD_DOWN);
            sidescrl_phase_step(&phase, &tp);
            CHECK(phase.shadow.crouch_dodges > 0u &&
                  phase.shadow.gaze_penalties == prev_pen &&
                  phase.skel.skeletism == prev_skel);
            /* Negative 2: _IsEndOfLevel__7CSpirit blocks exit until config_ok && trials > 6. */
            sidescrl_phase_spawn(&phase, (int)(phase.finish_x / u) - 2);
            saturn_pad_update_host(&tp, MB_PAD_RIGHT);
            sidescrl_phase_step(&phase, &tp);
            CHECK(sidescrl_phase_boss_remaining(&phase) == 1u &&
                  phase.state != SIDESCRL_ST_FINISHED &&
                  phase.bones_x < phase.finish_x);
            phase.shadow.trials = 7;
            phase.shadow.config_ok = 0;
            sidescrl_phase_step(&phase, &tp);
            CHECK(sidescrl_phase_boss_remaining(&phase) == 1u &&
                  phase.state != SIDESCRL_ST_FINISHED);
        }
        sidescrl_phase_free(&phase);
        CHECK(sidescrl_phase_load(&phase, &disc2, "/22SHADOW") == 0);
        CHECK(play_to_finish(&phase));
        CHECK(phase.shadow.trials > 6 && phase.shadow.config_ok == 1 &&
              phase.shadow.gaze_penalties == 0u && phase.shadow.crouch_dodges > 0u &&
              sidescrl_phase_boss_remaining(&phase) == 0u);
        printf("SHADOW: frames=%u w=%u trials=%d dodges=%u %s\n", phase.frames,
               phase.scroll_w, phase.shadow.trials, phase.shadow.crouch_dodges,
               phase.status);
        sidescrl_phase_free(&phase);

        CHECK(sidescrl_phase_load(&phase, &disc2, "/26LAFF") == 0);
        CHECK(phase.map.plane_count >= 3);
        CHECK(phase.map.planes[1].parallax_coeff == 0x10000);
        CHECK(phase.scroll_w == 2536u);
        {
            unsigned u = phase.enemies.units_per_pixel ? phase.enemies.units_per_pixel : UPP;
            saturn_pad tp = {0};
            int bmax = 0;
            CHECK(sidescrl_phase_boss_remaining(&phase) == 1u);
            CHECK(stage_enemies_laff_boss_hp(&phase.enemies, &bmax) == STAGE_LAFF_BOSS_HP);
            CHECK(phase.laff.enabled == 1 && phase.laff.skel_count == 80u && phase.laff.laff_meter == 0);
            /* Negative: GoodJoke enters CringeBrain with zero CLaffMeter gain. */
            runway_sfx_frame sounds;
            runway_sfx_begin_frame(&sounds);
            int good_joke = sidescrl_laff_tell_joke(&phase, 0);
            runway_sfx_end_frame();
            CHECK(good_joke == 0);
            CHECK(sounds.count == 1u && sounds.commands[0].bank == 0x29u);
            CHECK(phase.laff.last_joke_kind == 1 && phase.laff.good_jokes == 1u && phase.laff.laff_meter == 0);
            /* Positive: BadJoke enters LaffBrain and advances CLaffMeter. */
            CHECK(sidescrl_laff_tell_joke(&phase, 1) == 1);
            CHECK(phase.laff.last_joke_kind == 2 && phase.laff.bad_jokes == 1u && phase.laff.laff_meter == 16);
            /* Exit lock: trying to walk past finish_x while Da Laff is active stays blocked. */
            sidescrl_phase_spawn(&phase, (int)(phase.finish_x / u) - 2);
            saturn_pad_update_host(&tp, MB_PAD_RIGHT);
            sidescrl_phase_step(&phase, &tp);
            CHECK(phase.state != SIDESCRL_ST_FINISHED && phase.bones_x < phase.finish_x);
            CHECK(sidescrl_phase_boss_remaining(&phase) == 1u);
        }
        sidescrl_phase_free(&phase);

        CHECK(sidescrl_phase_load(&phase, &disc2, "/26LAFF") == 0);
        CHECK(play_to_finish(&phase));
        CHECK(sidescrl_phase_boss_remaining(&phase) == 0u);
        CHECK(phase.laff.laff_meter == 100 && phase.laff.bad_jokes >= 8u);
        printf("LAFF: frames=%u w=%u boss_rem=%u %s\n", phase.frames,
               phase.scroll_w, sidescrl_phase_boss_remaining(&phase),
               phase.status);
        sidescrl_phase_free(&phase);
    }

    cd_close(&disc1);
    if (opened2) cd_close(&disc2);
    printf("OK sidescrl_phase\n");
    return 0;
}
