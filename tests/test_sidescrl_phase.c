#include "core/cdimage.h"
#include "core/identity.h"
#include "core/runway_sfx.h"
#include "core/saturn_pad.h"
#include "core/sidescrl_phase.h"
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

static int play_to_finish(sidescrl_phase *ph) {
    saturn_pad pad = {0};
    int i;
    for (i = 0; i < 5000 && !sidescrl_phase_done(ph); i++) {
        saturn_pad_update_host(&pad, MB_PAD_RIGHT);
        sidescrl_phase_step(ph, &pad);
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

static int test_integration(void) {
    CHECK(test_spawn_and_walk() == 0);
    CHECK(test_slopes() == 0);
    CHECK(test_wall() == 0);
    CHECK(test_world_edges() == 0);
    CHECK(test_jump() == 0);
    CHECK(test_drop() == 0);
    printf("OK sidescrl_phase motion (synthetic terrain)\n");
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
    CHECK(phase.map.plane_count >= 1); /* §23 LoadCDB planes */
    CHECK(phase.map.strip_w >= 320);
    CHECK(scrl_map_find_floor(&phase.map, 40) > 0);
    CHECK(play_to_finish(&phase));
    printf("MAUSO: frames=%u layers=%u planes=%u %s\n", phase.frames,
           phase.layer_count, phase.map.plane_count, phase.status);
    sidescrl_phase_free(&phase);

    CHECK(sidescrl_phase_load(&phase, &disc1, "/06VALLEY") == 0);
    CHECK(phase.map.has_amap == 1 || phase.map.plane_count >= 1);
    CHECK(play_to_finish(&phase));
    printf("VALLEY: frames=%u amap=%u planes=%u %s\n", phase.frames,
           phase.map.amap_n, phase.map.plane_count, phase.status);
    sidescrl_phase_free(&phase);

    if (opened2) {
        CHECK(sidescrl_phase_load(&phase, &disc2, "/19VILLAGE") == 0);
        CHECK(play_to_finish(&phase));
        printf("VILLAGE: frames=%u layers=%u %s\n", phase.frames, phase.layer_count,
               phase.status);
        sidescrl_phase_free(&phase);

        CHECK(sidescrl_phase_load(&phase, &disc2, "/26LAFF") == 0);
        CHECK(play_to_finish(&phase));
        printf("LAFF: frames=%u %s\n", phase.frames, phase.status);
        sidescrl_phase_free(&phase);
    }

    cd_close(&disc1);
    if (opened2) cd_close(&disc2);
    printf("OK sidescrl_phase\n");
    return 0;
}
