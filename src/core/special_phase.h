#ifndef MB_SPECIAL_PHASE_H
#define MB_SPECIAL_PHASE_H
#include "core/cdimage.h"
#include "core/duck_avi.h"
#include "core/finale_body.h"
#include "core/pan_scroll.h"
#include "core/rhythm_table.h"
#include "core/runway_skel.h"
#include "core/saturn_pad.h"
#include "core/stage_catalog.h"
#include "core/stage_enemy.h"
#include <stddef.h>
#include <stdint.h>

/*
 * §18/§20/§24 native special-stage host phase.
 * MAP: guitar.o/drum.o (rhythm+scrlcoll), GENROOM/BIGBONES (pan_coll+skel),
 * finale.o (scrlcoll boss), DUKMOVIE (19DUCK/20AVI via LOSE), LOSE AVI-only.
 * §20: duck_avi soft TrueMotion present + PCM; per-stage rhythm period.
 * §22: 19DUCK ydt/cdt/vector tables via duck_avi_open_with_duck.
 * §24: DrumSequence@26b58c + SequenceTable guitar notes; GENRM reds/blues/zap;
 *      FINALE RingDefinitionTable rings/bolts (fx during PRESENT).
 */

typedef struct {
    int x, y;
    unsigned power, type;
    int touch;
} big_bumper;

typedef enum {
    SPECIAL_MODE_RHYTHM = 0,  /* GUITAR / DRUM */
    SPECIAL_MODE_ACTION = 1,  /* BIGBNS / GENRM */
    SPECIAL_MODE_PRESENT = 2  /* FINALE / LOSE (DUKMOVIE stand-in) */
} special_mode;

typedef enum {
    SPECIAL_ST_PLAY = 0,
    SPECIAL_ST_PAUSED = 1,
    SPECIAL_ST_FINISHED = 2, /* EndLevel(0) win */
    SPECIAL_ST_LOST = 3,     /* EndLevel(1) / LOSE presentation done */
    SPECIAL_ST_ABORTED = 4
} special_phase_state;

#define SPECIAL_GFX_MAX 24u
#define SPECIAL_BEAT_PERIOD 16u
#define SPECIAL_BEAT_WINDOW 6u
#define SPECIAL_HITS_TO_WIN 16u
#define SPECIAL_PRESENT_PAGES 6u
/* CRing: 16 entries of 0x6c bytes; docs/finale-ring-analysis.md */
#define FINALE_RING_SLOTS 16u
#define FINALE_RING_PHASE_WRAP 0x01000000
typedef struct {
    stxtr_record frames[5];
    unsigned count, ticks;
} finale_ring_anim;
typedef struct {
    const stxtr_record *slots[FINALE_RING_SLOTS];
    uint8_t order[FINALE_RING_SLOTS];
    int phase;
} finale_ring_list;

/* Disc-backed leaf images remain valid until special_phase_free. */
typedef struct {
    const uint8_t *pixels;
    uint8_t *unpacked;
    int x_origin, y_origin;
    unsigned width, height, ticks;
} finale_bolt_frame;

typedef struct {
    finale_bolt_frame frames[28];
    unsigned count, impact;
    int32_t z_bias;
} finale_bolt_anim;

typedef struct {
    unsigned dir, age; /* age 1 displays animation record 0 */
} finale_bolt;

typedef struct { uint8_t dir, slot; } finale_ring_contact;
typedef struct { unsigned dir, tick; } finale_ring_impact;

typedef struct {
    special_phase_state state;
    special_phase_state pre_pause_state;
    special_mode mode;
    stage_kind kind;
    char iso_dir[32];
    char stage_name[16];

    uint8_t *frame_rgba;
    uint8_t *bg_rgba; /* 320×224 backdrop from STXT/TITL heuristic */
    uint8_t *gfx_rgba[SPECIAL_GFX_MAX];
    unsigned gfx_w[SPECIAL_GFX_MAX], gfx_h[SPECIAL_GFX_MAX];
    unsigned gfx_count;
    unsigned gfx_index;

    void *title;
    size_t title_size;
    void *textures;
    size_t textures_size;
    void *code;
    size_t code_size;
    void *sound;
    size_t sound_size;
    void *duck;
    size_t duck_size;
    void *avi;
    size_t avi_size;
    void *pan;
    size_t pan_size;

    /* Rhythm (§20/§24) */
    unsigned beat_period; /* GUITAR 16 / DRUM 12 */
    unsigned beat_index;
    unsigned hits;
    unsigned misses;
    int in_window;
    int hit_this_beat;
    const rhythm_table *rhythm; /* DrumSequence / guitar table */
    unsigned expected_kind;     /* current note kind 0..3 */
    rhythm_table score;
    rhythm_note *drum_notes;
    uint16_t music_masks[6], drum_preview_mask, drum_judged_mask;
    unsigned lick_durations[6], lick_left, arm_tick, music_wait;
    int active_lick, lick_valid;
    unsigned music_tick, drum_prompt, music_latch, music_ending;
    uint16_t guitar_sample;
    unsigned note_cursor;       /* index into rhythm->notes */
    unsigned combo;
    unsigned max_combo;
    unsigned rhythm_heals;
    int last_hit_grade;         /* 1 = clean beat hit, -1 = miss, 0 = none */
    unsigned grade_timer;       /* visual flash timer */

    /* §20 DUCK AVI present */
    duck_avi movie;
    int movie_ok;
    int movie_pcm_pending; /* host should play movie.pcm once */
    unsigned finale_movie_preroll; /* remaining arena→ending retraces before scanout */

    /* Action / boss enemies (§24) */
    uint32_t bones_x, bones_y;
    uint32_t finish_x;
    int facing;
    int32_t vel_x, vel_y;
    int airborne;
    int tall;
    runway_skel skel;
    stage_enemies enemies;
    unsigned hazard_hits;
    unsigned bumper_bounces;
    unsigned big_wave, big_count, wave_bounces, parts_collected, boost_left, part_type;
    int big_x, big_y, big_vx, big_vy, paddle_x, paddle_vx, part_x, part_y;
    unsigned big_preroll, big_drop, big_transition, big_resume_tick, big_end_rest;
    uint32_t big_config, big_next_config;
    unsigned part_joint;
    unsigned bumper_impulse;
    void *angle_loader;
    finale_body *action_rig;
    int32_t collision_joints[FINALE_BODY_JOINTS][3];
    unsigned pose_ticks;
    uint32_t pose_tracks;
    uint16_t big_pad, big_latched;
    big_bumper big_bumpers[32];
    unsigned plasma_collected, plasma_emitted, blue_head, zap_cursor;
    struct { int x,y,vx,vy; unsigned shrink, change, age, heading, birth; } plasma[20];
    int gen_x,gen_y,gen_vx,gen_vy,gen_rot,gen_rot_vel,gen_rot_acc,gen_rot_limit;
    uint16_t gen_sample;
    unsigned zap_clock, change_frames, gen_motion_timer;
    int zap_pending, zap_origin_x, zap_origin_y, zap_angle;
    int zap_x[8], zap_y[8], zap_vx[8], zap_vy[8];
    unsigned zap_ttl[8], zap_birth[8];

    /* FINALE CRing arena (ShootRing / Brain_CRing) */
    int ring_active[FINALE_RING_SLOTS]; /* entry +0x68 */
    int ring_phase;                     /* object+0x704 (ShootRing select) */
    int ring_phase_acc;                 /* Brain accumulator before >>16 */
    int ring_active_count;              /* object+0x708 */
    int ring_cleared;                   /* EndLevel(0) / clear flag fired */
    int finale_movie_armed;             /* 20AVI opened; PlayDuckMovie ready */
    unsigned ring_shots;
    unsigned finale_clear_tick; /* publication of the final sector */
    finale_ring_contact finale_pending_shots[2];
    unsigned finale_pending_count;
    int ring_last_index;                /* last ShootRing slot, -1 none */
    int ring_last_activated;            /* 1=activated, 0=deactivated */
    unsigned ring_sfx_event;            /* Saturn SoundEvent id from last shot */
    finale_ring_anim ring_anim[3];
    unsigned ring_motion[FINALE_RING_SLOTS];
    int ring_age[FINALE_RING_SLOTS];
    uint8_t ring_order[FINALE_RING_SLOTS];
    finale_ring_list ring_list[3];

    /* Animated live 15PAN backdrop (BIGBNS lunar dome / GENRM spinning spokes). */
    pan_scroll live_pan;
    int live_pan_ok;
    int finale_origin_x, finale_origin_y;
    finale_body *finale_body_mesh;
    unsigned finale_body_hit_tick, finale_health_before_hit;
    unsigned finale_facing, finale_recenter, finale_charged;
    unsigned finale_cycle_tick, finale_spawn_tick, finale_next_dir;
    saturn_pad finale_pad; /* most recent report delivered to CheckJoy */
    uint16_t finale_pad_pending;
    unsigned finale_end_tick; /* CEndLevel created by the last sector or lethal hit */
    uint32_t finale_rng;
    uint8_t finale_random_table[256];
    struct finale_lights *finale_lights;
    unsigned finale_rows[4];
    finale_bolt_anim finale_red[4], finale_blue[4], finale_hit[6]; /* 0..3 damage, 4 guard, 5 ring */
    finale_bolt_anim finale_face;
    unsigned finale_face_tick;
    void *finale_dtxt;
    size_t finale_dtxt_size;
    unsigned finale_hit_tick, finale_hit_type;
    int finale_hit_x[8], finale_hit_y[8];
    int finale_ring_hit_x[4], finale_ring_hit_y[4];
    finale_ring_impact finale_ring_hits[2]; /* one CRender per blue shot */
    int32_t finale_hit_z[8];
    finale_bolt red_bolt, blue_bolts[2];
    finale_bolt finale_display_bolts[2][3];
    int32_t arena_y_px;
    uint32_t walk_dist;
    uint8_t hurt_timer;
    int32_t knockback_vx;

    /* Present */
    unsigned page;
    unsigned pages_needed;

    unsigned frames;
    unsigned audio_track;
    unsigned sfx_flags;
    char status[176];
} special_phase;

/* ShootRing_CRing stand-in: dir 0..3 (table 0,128,192,64). Returns 1 if
 * deactivating the last active ring triggered EndLevel(0). */
int special_phase_finale_shoot(special_phase *phase, unsigned dir);

/* Interactive arena-first: hide ending AVI until rings clear (PlayDuckMovie).
 * Headless/tests leave movie_ok set so the film starts immediately. */
int special_phase_finale_defer_ending(special_phase *phase);
/* Dispatch PlayDuckMovie(3) after the arena. PCM and scanout start on
 * separate retraces of the observed US disc 2 transition. */
int special_phase_finale_play_ending(special_phase *phase);

int special_phase_load(special_phase *phase, cdimage *cd, const char *iso_dir,
                       stage_kind kind);
void special_phase_free(special_phase *phase);
int special_phase_step(special_phase *phase, const saturn_pad *pad);
int special_phase_render(special_phase *phase);
int special_phase_done(const special_phase *phase);
unsigned special_phase_targets_remaining(const special_phase *phase);
int special_phase_read_music(special_phase *phase);
unsigned special_phase_autoplay_frame_limit(const special_phase *phase);
uint32_t special_phase_autoplay_buttons(const special_phase *phase,
                                          unsigned frame);

#endif
