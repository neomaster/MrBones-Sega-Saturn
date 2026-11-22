#ifndef MB_SIDESCRL_PHASE_H
#define MB_SIDESCRL_PHASE_H
#include "core/cdimage.h"
#include "core/runway_projection.h"
#include "core/runway_skel.h"
#include "core/saturn_pad.h"
#include "core/scrl_map.h"
#include "core/sidescrl_motion.h"
#include "core/stage_enemy.h"
#include <stddef.h>
#include <stdint.h>

/*
 * §17/§23 native side-scroll host (MAUSO, VILLAGE, BAT1-3, GLASS, VALLEY,
 * SHADOW, VORTEX, LAFF). MAP: scroller.o + scrlcoll.o + sidescrl.o + bkgdata.o.
 * §23: LoadCDB/LoadMap/DrawMap multi-layer ACDB/ATIL…DTIL + AMAP floor;
 * SCRLCOLLGetHixel3D / pFloorHixels via scrl_map. Palette from 02TITL/04STXT/CODE.
 * §24: poofs.o / CBlueSparkle enemies (denser on BAT1–3).
 *
 * Motion: one sidescrl_motion_step per frame on `actor` (walk, wall clamp, jump,
 * slope, gravity). The terrain it sees is the scrl_map floor profile: the ground
 * under a column is floor_py, every pixel at or below it is solid, and the world
 * edges are walls. bones_x/bones_y/vel_x/airborne/ground_y are derived from the
 * actor after each step (bones_x is the centre column and bones_y the feet, both
 * in units) so enemies, rendering and the stage runner read what they always did.
 * See docs/sidescrl-motion-architecture.md.
 */

typedef enum {
    SIDESCRL_ST_STAND = 0,
    SIDESCRL_ST_WALK = 1,
    SIDESCRL_ST_AIR = 2,
    SIDESCRL_ST_FINISHED = 3,
    SIDESCRL_ST_ABORTED = 4,
    SIDESCRL_ST_PAUSED = 5,
    SIDESCRL_ST_LOST = 6
} sidescrl_phase_state;

#define SIDESCRL_ANIM_MAX 16u
#define SIDESCRL_LAYER_MAX 10u

typedef struct {
    char name[16];
    void *data;
    size_t size;
} sidescrl_layer;

typedef struct {
    uint16_t x0, x1, y, anchor_x;
    int32_t charge;
    uint8_t variant;
    uint8_t kind;  /* 0=MAUSO slab, 1=BAT pine branch, 2=GLASS crystal, 3=VALLEY ledge */
    uint8_t state; /* 0=intact, 1=cracking, 2=shattered */
    uint8_t timer;
} sidescrl_plat;

typedef struct {
    sidescrl_phase_state state;
    sidescrl_phase_state pre_pause_state;
    char iso_dir[32];
    char stage_name[16];

    uint8_t *frame_rgba;   /* 320×224 composed */
    uint8_t *scroll_rgba;  /* wide strip from DrawMap */
    unsigned scroll_w, scroll_h;
    uint8_t *sprite_rgba;
    unsigned sprite_w, sprite_h;
    uint8_t *anim_rgba[SIDESCRL_ANIM_MAX];
    unsigned anim_w[SIDESCRL_ANIM_MAX], anim_h[SIDESCRL_ANIM_MAX];
    unsigned anim_count, anim_index;

    sidescrl_layer layers[SIDESCRL_LAYER_MAX];
    unsigned layer_count;
    void *title;
    size_t title_size;
    void *textures;
    size_t textures_size;
    void *code;
    size_t code_size;
    void *amap;
    size_t amap_size;

    scrl_map map; /* §23 LoadCDB/DrawMap/SCRLCOLL */
    uint8_t palette[4096];

    unsigned projection_px;
    bones_stage_init initial;
    unsigned body_piece_count;
    uint32_t config_table, piece_table, exit_config;
    SidescrlActor actor; /* the authoritative pose; the fields below mirror it */
    uint32_t bones_x, bones_y; /* centre column / feet, in units */
    uint32_t camera_x;
    int finish_left;
    uint32_t finish_x;
    int finish_y, finish_width, finish_height; /* CWinBox parameters in projected pixels. */
    uint32_t ground_y;
    int facing;
    int32_t vel_x, vel_y;
    int airborne;
    int tall;
    uint8_t coyote_timer;
    uint8_t jump_buf_timer;
    uint32_t walk_dist;
    const uint8_t *walk_root; /* VALLEY's full-body root motion in the loaded overlay */
    unsigned walk_tick;
    int32_t fly_y_px;
    unsigned shadow_eye_phase;
    sidescrl_plat plats[48];
    unsigned plat_count;
    struct batwood_pickup {
        int x, y; /* projected map position in 24.8 pixels */
        int rise, grace, falling;
        unsigned mask;
        uint32_t attracted;
    } body_pieces[24];
    uint32_t body_config, collected;
    int floor_delta, hand_delta, latch_y, latch_wait, latch_type;
    unsigned latch_frame;
    int32_t exit_world_x, exit_world_y;
    enum { BAT_FREE, BAT_HANG, BAT_PULL } latch;
    int branch_launch;
    int on_plat_idx;
    struct {
        int x, y;
        unsigned id;
        int open, dissolve_timer;
    } fences[4];
    unsigned fence_count;
    struct {
        int x, y;
        unsigned id;
        int triggered, anim_timer;
    } fence_keys[4];
    unsigned fence_key_count;
    struct {
        int x, y;
        int used, drain_timer;
    } blue_tombs[4];
    unsigned blue_tomb_count;
    struct {
        int enabled;
        int phase; /* 0=idle, 1=partition, 2=assembler, 3=butfly_exit */
        int arena_x, arena_w, arena_y;
        int left_edge, right_edge, lock_edges;
        unsigned wave_stage, wave_timer;
        int wave_idx[4], wave_dir[4], wave_acc[4];
        unsigned wave_msg[4];
        uint8_t key_timer[16], key_msg[16];
        int skels_spawned, skels_total;
        unsigned ambient_bf_timer;
        int bh_x, bh_y, bh_active, bh_buzz;
        int bf_x, bf_y, bf_taken, bf_wait;
    } mauso;
    struct {
        int enabled;
        int spider_count;
        struct {
            int x, top_y, cur_y, drop_dist;
            int state; /* 0=descending, 1=grabbed_rising, 2=cut_reset, 3=eat_cooldown */
            int timer, has_guy;
        } spiders[12];
        int mushroom_count;
        struct {
            int x, y, taken, kind;
        } mushrooms[8];
        int bh_x, bh_y, bh_active;
        unsigned little_guys_saved;
        unsigned little_guys_eaten;
        int spider_meter;
    } village;
    struct {
        int enabled;
        int junior_x, junior_y;
        unsigned skel_count;
        int16_t skel_x[80], skel_y[80];
        int laff_meter;
        int last_joke_kind; /* 0=none, 1=good(CringeBrain), 2=bad(LaffBrain) */
        unsigned good_jokes, bad_jokes;
        unsigned joke_timer;
        int last_boss_hp;
    } laff;
    struct {
        int enabled;
        unsigned total_records;
        unsigned safe_count, turn_count, trap_count, shoot_count, blue_count, fog_count;
        int keyhole_x, keyhole_y, keyhole_unlocked;
        unsigned subpalette_lit;
        struct {
            uint16_t x0, x1;
            uint8_t top_y, kind;
            int broken;
        } shards[180];
        unsigned shard_count;
    } glass;
    struct {
        int enabled;
        int rot_x, rot_y;
        unsigned ring_count;
        struct {
            int x, y;
            unsigned kind, spawn_frame;
            int cleared;
        } rings[20];
        unsigned rings_done;
        int exit_unlocked;
        int breaker_cooldown;
    } vortex;
    struct {
        int enabled;
        int bh_x, bh_y, bh_active;
        int config_ok;
        int trials;
        uint8_t trial_mask;
        unsigned gaze_penalties, crouch_dodges;
    } shadow;
    uint8_t hurt_timer;
    int32_t knockback_vx;
    runway_skel skel;
    stage_enemies enemies;
    unsigned frames;
    unsigned audio_track;
    unsigned sfx_flags;
    unsigned hazard_hits;
    char status[160];
} sidescrl_phase;

/*
 * Put the actor standing on the floor under world column `world_x_px` (the centre
 * column of its box), at rest, and refresh the mirrored fields. Needs phase->map
 * with its collision table built. Used by load; also by tests.
 */
void sidescrl_phase_spawn(sidescrl_phase *phase, int world_x_px);

int sidescrl_phase_load(sidescrl_phase *phase, cdimage *cd, const char *iso_dir);
void sidescrl_phase_free(sidescrl_phase *phase);
int sidescrl_phase_step(sidescrl_phase *phase, const saturn_pad *pad);
int sidescrl_phase_render(sidescrl_phase *phase);
int sidescrl_phase_done(const sidescrl_phase *phase);
unsigned sidescrl_phase_boss_remaining(const sidescrl_phase *phase);

uint32_t batwood_piece(const sidescrl_phase *phase, unsigned mask);
int batwood_exit_contact(const sidescrl_phase *phase);
void batwood_draw(const sidescrl_phase *phase, int screen_x, int feet_y, int camera_x);

/* Autoplay bot buttons: walks right, jumps upcoming pits/hazards, fires C/A. */
unsigned sidescrl_phase_autoplay_buttons(const sidescrl_phase *phase, unsigned frame);

/* 19VILLAGE spider silk cut (`CSpider::CheckThreadHit`) and 26LAFF joke machine helpers. */
int sidescrl_village_cut_thread(sidescrl_phase *phase, int hit_x, int hit_y);
int sidescrl_laff_tell_joke(sidescrl_phase *phase, int is_bad_joke);

/* Segment C helpers: 15GLASS CKeyHole lock-in & 22SHADOW CSpirit trial progression. */
int sidescrl_glass_unlock_keyhole(sidescrl_phase *phase);
int sidescrl_shadow_complete_trial(sidescrl_phase *phase, int trial_idx);

#endif
