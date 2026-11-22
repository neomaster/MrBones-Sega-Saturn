#ifndef MB_RUNWAY_PHASE_H
#define MB_RUNWAY_PHASE_H
#include "core/bones_bbs.h"
#include "core/cdimage.h"
#include "core/mplay_cue.h"
#include "core/pan_collision_map.h"
#include "core/pan_scroll.h"
#include "core/runway_hazard.h"
#include "core/runway_skel.h"
#include "core/saturn_pad.h"
#include "core/stage_enemy.h"
#include <stddef.h>
#include <stdint.h>

/*
 * Native RUNWAY phase loop — progressive reconstruction, not a full SH-2 port.
 * Stand/Walk adapted from _Stand_Brain / InputBrain / mover / collision.
 * BBS post/peek/consume adapts loader 0x0020b264 / 0x0020b2e8 / 0x0020b308.
 * Known differences in docs/coverage.md and docs/rea-runway-session.md.
 */

typedef enum {
    RUNWAY_ST_STAND = 0,
    RUNWAY_ST_WALK = 1,
    RUNWAY_ST_AIR = 2,       /* jump / fall — Stand peeks 0x20800 */
    RUNWAY_ST_FINISHED = 3,  /* EvalSuccess → EndLevel(0) win */
    RUNWAY_ST_ABORTED = 4,
    RUNWAY_ST_PAUSED = 5,    /* CSkelBar PauseBrain / bPaused */
    RUNWAY_ST_LOST = 6       /* BonesLoose → EndLevel(1) death */
} runway_phase_state;

/* Bones STXTR descriptor banks in 03CODE (stride 0x38).
 * Bank 0 morph @0x23204C ×13; bank 1 full @0x253858 ×20. */
#define RUNWAY_ANIM_BANK_MORPH 0u
#define RUNWAY_ANIM_BANK_FULL 1u
#define RUNWAY_ANIM_BANK_COUNT 2u
#define RUNWAY_ANIM_MAX_FRAMES 20u

typedef struct {
    unsigned aim, aim_ticks;
    int claw_x, claw_y, claw_vx, claw_vy, claw_dip,claw_ax,claw_roll;
} runway_claws;

typedef struct {
    uint8_t *entries; /* BE16 screen X + PAN boundary, signed Y, animation and frame. */
    unsigned count, next, last_pan;
} runway_gang_track;

typedef struct {
    uint8_t *pixels;
    unsigned width, height;
    int anchor_x, anchor_y;
} runway_gang_shape;

typedef struct {
    runway_phase_state state;
    pan_collision_map collision;
    bones_bbs bbs;
    uint8_t *bg_rgba;       /* aliases pan.bg_rgba after load */
    uint8_t *frame_rgba;    /* composed */
    /* Current frame view (aliases active bank anim_rgba[anim_index]). */
    uint8_t *sprite_rgba;
    unsigned sprite_w, sprite_h;
    /* §12l: two descriptor banks; anim_count = frames in active bank. */
    uint8_t *anim_rgba[RUNWAY_ANIM_BANK_COUNT][RUNWAY_ANIM_MAX_FRAMES];
    unsigned anim_w[RUNWAY_ANIM_BANK_COUNT][RUNWAY_ANIM_MAX_FRAMES];
    unsigned anim_h[RUNWAY_ANIM_BANK_COUNT][RUNWAY_ANIM_MAX_FRAMES];
    unsigned anim_bank_count[RUNWAY_ANIM_BANK_COUNT];
    unsigned anim_bank;   /* active bank */
    unsigned anim_count;  /* = anim_bank_count[anim_bank] */
    unsigned anim_index;
    int32_t jump_impulse;
    unsigned boost_ticks;
    unsigned projection_px;
    pan_hit_offsets hit;  /* HitChkScrn* from BonesSetConfig template */
    /* Fixed-point world units matching projection helpers (≈2340 units/pixel).
     * bones_x/y ≡ world at 0x2858BC/+4 (RWorldSprite). input_cell aliases bones_x. */
    uint32_t bones_x, bones_y;
    uint32_t camera_x, camera_y;
    int facing; /* +1 right, -1 left */
    int tall;   /* *0x28DB8C via TallSet_26D784 */
    uint32_t brain_slot; /* *0x28589C config/brain; crouch ∈ bnscfg set */
    unsigned crouch_index; /* TallBrain stand-in index 0..7 */
    int sched_enqueued_0c; /* loader 0x204128(0xc) fired this Down edge */
    uint32_t input_cell; /* same cell as bones_x (0x2858BC); kept for InputBrain call site */
    /* Velocity chain (§12h): vel @0x2858FC, accel @0x285908; velA=vel[2]@0x285904; velB@0x285928. */
    int32_t vel[3];
    int32_t accel[3];
    int32_t vel_a; /* 0x285904 — BonesWalk speed component */
    int32_t vel_b; /* 0x285928 — BonesWalk speed component */
    int airborne; /* 1 while jump/fall until ground contact */
    uint32_t obj_flags; /* Stand_Brain object+0x1c bitfield (partial) */
    uint32_t last_message;
    unsigned frames;
    unsigned audio_track; /* CDDA track number for phase BGM; 0 = none */
    int audio_started;
    unsigned sfx_flags; /* RUNWAY_SFX_FLAG_* : walk/tall/C/jump/land */
    unsigned sound_event_id; /* last _SoundEvent index (e.g. 0xc on Down) */
    /* §12m: 15PAN stream + Mplaynext cue sequencer. */
    uint8_t *pan_blob;
    size_t pan_blob_size;
    pan_scroll pan;
    mplay_seq mplay;
    /* §12o/§24: CSkelBar / hazards / gang·lord·poofs / EndLevel. */
    runway_skel skel;
    runway_hazards hazards;
    stage_enemies enemies;
    runway_phase_state pre_pause_state; /* restore after PauseBrain */
    /* §16: generic 15PAN stages + HUD sprites (HEALTH.O / PAUSE.O). */
    int generic_pan; /* 1 = non-RUNWAY pan stage (soft assets / shorter goal) */
    uint8_t *scrl_bg_rgba; /* optional C/D SCRL backdrop behind transparent 15PAN */
    uint32_t finish_x;
    char iso_dir[32];
    char stage_name[16];
    uint8_t *hud_health_rgba;
    unsigned hud_health_w, hud_health_h;
    uint8_t *hud_pause_rgba;
    unsigned hud_pause_w, hud_pause_h;
    char status[128];
    unsigned runway_win_record, runway_clock, gang_start_tick;
    struct { int32_t x, y; } fall_profile[27];
    unsigned fall_index, fall_wait;
    int32_t fall_height;
    struct { unsigned frame, mask; } pan_events[152];
    unsigned pan_event_count, pan_event_index;
    /* Retail generic-PAN coordinates are signed 16.16 with positive X to the left. */
    enum { COURSE_NONE, COURSE_LOGS, COURSE_RIDE, COURSE_STAING, COURSE_MOAT } course;
    int32_t course_x, course_y;
    uint8_t *course_code, *swim_sprite;
    ride_current current;
    int swim_pitch, swim_yaw, swim_roll;
    unsigned wall_tick, stage_exit_wait, section;
    uint16_t swim_wall_mask;
    int swim_dx, swim_dy;
    struct { int32_t x, y; unsigned side, turns; } nuts[2];
    struct { int32_t x, y; unsigned id; } sparky_targets[9];
    unsigned sparky_target, nuts_released, nut_contact;
    uint32_t moat_sectors[15];
    unsigned moat_clip, moat_time, moat_huffs, moat_shake, moat_growls, moat_escape;
    int moat_vy, moat_vx;
    int32_t moat_floor_screen, moat_floor_world;
    unsigned nut_busy[2];
    struct { int32_t x, y; } claw_aim[11];
    struct { int32_t x, y, z; unsigned birth; int red; } flight_objects[128];
    unsigned flight_count, rewards, red_hits, glass_hits;
    struct {int32_t x,y; unsigned ticks,action,target;} log_tracks[3][29];
    struct {int32_t ox,oy,x,y,from_x,from_y; unsigned type,age,knot,elapsed;} log_actors[12];
    unsigned log_object_count, log_spawned[4];
    runway_gang_shape log_head[9];
    int swim_pump;
    unsigned swim_input, swim_config, swim_keyhole, swim_detached, swim_motion_tick;
    uint32_t swim_tracks, swim_burst_frame[5], swim_order[16][10];
    unsigned swim_burst, swim_bubble_count, swim_bubbles_left, swim_bubble_wait, swim_spawned;
    struct { int x,y,z,vx,vy,vz; unsigned age; } swim_bubbles[6];
    struct { unsigned joint,age; int x,y,z,vx,vy,vz,attract; } swim_bones[8];
    unsigned swim_recovered,swim_pickups;
    struct finale_body *swim_body;
    runway_gang_shape swim_bubble_pose[16];
    runway_claws claws;
    struct finale_body *claw_body;
    uint32_t claw_tracks[3],claw_motion;
    unsigned claw_tick;
    int flight_joints[3][2]; /* projected hips, left foot, right foot (VDP1 center). */
    runway_gang_shape flight_shapes[3][4];
    runway_gang_shape nut_pose[2][16];
    unsigned butterfly_start[16], butterfly_count, butterfly_collected;
    struct { runway_gang_shape shape; unsigned end_tick; } butterfly_anim[26];
    runway_gang_track gang_tracks[4];
    runway_gang_shape gang_shapes[256];
    /* Width in columns of the pre-rendered scrl_bg_rgba strip (rows = PAN_HEIGHT).
     * 0 = frozen backdrop: scrl_bg_rgba is the old fixed 320x224 render at camera 0
     * and render samples it at the same pixel every frame. */
    unsigned scrl_bg_w;
    int scrl_origin_x;
    uint8_t coyote_timer;
    uint8_t spring_ticks;
    uint8_t jump_buf_timer;
    uint32_t walk_dist;
    int32_t fly_y_px;
    uint8_t hurt_timer;
    int32_t knockback_vx;
    struct runway_ice *ice;
    unsigned ice_block_count, ice_path_count, ice_node_count;
    int ice_support; /* CIceBlock index in the overlay list, or -1 on the ice floor. */
    int32_t ice_x, ice_y;
    unsigned ice_carried, ice_falls;
} runway_phase;

int runway_phase_load(runway_phase *phase, cdimage *cd);
/* Load playable phase assets from `iso_dir` (RUNWAY path; §14 stage_runner). */
int runway_phase_load_dir(runway_phase *phase, cdimage *cd, const char *iso_dir);
void runway_phase_free(runway_phase *phase);
/* One simulation step from a Saturn pad snapshot. */
int runway_phase_step(runway_phase *phase, const saturn_pad *pad);
/* Rebuild frame_rgba from bg + projected sprite. */
int runway_phase_render(runway_phase *phase);
int runway_phase_done(const runway_phase *phase);

/*
 * Distant parallax backdrop (pan stages LOGS/RIDE/ICE1/ICE2/MOAT), measured on the
 * discs. The backdrop is pre-rendered once at camera 0 and scrolled as ONE image, so
 * it has a single parallax rate; the per-layer rates of scrl_map_draw are not
 * reproduced. The strip is NOT assumed to wrap: the scrolled planes are wide world
 * planes (ICE1 plane A 1864 columns, ICE2 plane A 640) whose art does not tile, so the
 * strip is as wide as the widest plane (<= PAN_SCRL_BG_MAX_W) and the offset is
 * clamped to strip_w - PAN_WIDTH instead of wrapping (no seam by construction).
 * A stage with a one-screen picture plane (width <= PAN_WIDTH: LOGS plane D, RIDE
 * plane C, MOAT plane D, all 320x224) cannot scroll without exposing that picture's
 * own edge, so it keeps the frozen 320x224 backdrop (scrl_bg_w == 0).
 */
#define PAN_SCRL_BG_MAX_W 2048u
/* Minimum scroll room (strip_w - PAN_WIDTH) worth a moving backdrop. */
#define PAN_SCRL_BG_MIN_ROOM 128u
/* Offset = camera_px / CAM_DIV + pan_index / IDX_DIV, in columns, clamped to the
 * strip. pan_index counts 15PAN records (thousands on LOGS/RIDE, 118 on ICE1/ICE2),
 * so it is scaled down to stay slower than the foreground. */
#define PAN_SCRL_BG_CAM_DIV 2u
#define PAN_SCRL_BG_IDX_DIV 4u

/* Strip column sampled for screen column x (0..PAN_WIDTH-1): x plus the offset
 * above, clamped so the view never leaves [0, strip_w). Never wraps. */
unsigned runway_phase_bg_column(unsigned x, unsigned camera_px,
                                unsigned pan_index, unsigned strip_w);

/* Autoplay bot buttons: runs right, jumps hazards/throws, shoots C at threats. */
unsigned runway_phase_autoplay_buttons(const runway_phase *phase, unsigned frame);

#endif
