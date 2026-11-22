#ifndef MB_STAGE_ENEMY_H
#define MB_STAGE_ENEMY_H
#include "aabb_hitchk.h"
#include "cdimage.h"
#include "runway_hazard.h"
#include "runway_skel.h"
#include "upscale.h"
#include <stdint.h>

/*
 * §24 host stand-ins for MAP enemy/boss objs across stages.
 *
 * RUNWAY:  gang.o (CGangMember CheckObstacle@26bbac Throw@26c32c
 *          CheckForBonesHit@26c510 Brain@26c934), lord.o (Brain@26b410),
 *          poofs.o (CRedPoof/CBluePoof), tbones.o (CThrown CheckForHit@26da50
 *          range DAT 0xB6DB).
 * SIDESCRL BAT*: poofs.o / CBlueSparkle.
 * GENROOM: reds.o/blues.o/zap.o (CheckForZap, TimingZapMax=0x10 @2673d8).
 * FINALE:  finale.o/fbones.o/ring.o/bolt.o — RingDefinitionTable@2b0158,
 *          ShootRing, BoltHit.
 * BIGBONES: heavy skel zones (pan_coll + SkelBar).
 */

#define STAGE_ENEMY_MAX 24u
#define STAGE_ENEMY_HIT_RANGE 0xB6DBu /* CheckForHit_CThrown DAT_0026daa4 */
#define STAGE_SHOT_MAX 4u
#define STAGE_PICKUP_MAX 4u
#define STAGE_POP_MAX 4u

typedef enum {
    STAGE_ENEMY_GANG = 0,
    STAGE_ENEMY_LORD = 1,
    STAGE_ENEMY_POOF_RED = 2,
    STAGE_ENEMY_POOF_BLUE = 3,
    STAGE_ENEMY_RED = 4,   /* GENROOM CRed */
    STAGE_ENEMY_BLUE = 5,  /* GENROOM CBlue */
    STAGE_ENEMY_ZAP = 6,
    STAGE_ENEMY_RING = 7,  /* FINALE CRing */
    STAGE_ENEMY_BOLT = 8,  /* FINALE CBolt */
    STAGE_ENEMY_THROWN = 9,
    STAGE_ENEMY_ZOMBIE = 10, /* BAT1..3 graveyard zombie */
    STAGE_ENEMY_BAT = 11,    /* BAT1..3 swooping bat */
    STAGE_ENEMY_HAND = 12,   /* VILLAGE road/bridge hand hazard */
    STAGE_ENEMY_LILGUYS = 13,/* VILLAGE small fast swarmer */
    STAGE_ENEMY_WINGNUT = 14,/* MOAT swooping winged hazard */
    STAGE_ENEMY_SHADOW = 15  /* SHADOW ambush stalker */
} stage_enemy_kind;

#define STAGE_PROP_MAX 12u
typedef struct {
    int x, y, anchor_x, anchor_y; /* authored map coordinates and texture origin in pixels */
    unsigned anim;
} stage_prop;

typedef struct {
    uint32_t x, y;
    int32_t vx;
    unsigned ttl;
    unsigned anim;
    int power;
    int is_radial; /* 1 = A radial Blues pulse, 0 = C directional bolt */
    int active;
} stage_blues_shot;

typedef struct {
    uint32_t x, y;
    int32_t heal;
    unsigned anim;
    int active;
} stage_pickup;

typedef struct {
    uint32_t x, y;
    unsigned ttl;
    int active;
} stage_pop_fx;

typedef struct {
    stage_enemy_kind kind;
    uint32_t x, y;     /* world units */
    int32_t vx;        /* patrol / projectile velocity */
    int32_t damage;
    int active;
    int hp;
    unsigned cooldown; /* frames until next throw / zap / ring */
    unsigned anim;
    uint32_t patrol_lo, patrol_hi;
    int8_t facing;       /* -1 left, 1 right, 0 derive from vx */
    uint8_t throw_phase; /* 0 = patrol run, 4..5 = windup, 6..7 = release */
    uint8_t throw_hold;
    uint8_t recorded; /* CGangMember pose and movement come from TableRead. */
    int screen_x, screen_y;
} stage_enemy;

typedef struct {
    BoundingBox player;
    const int16_t *floor;
    unsigned floor_width;
    int camera_y;
} stage_batwood_contact;

typedef struct {
    stage_enemy ents[STAGE_ENEMY_MAX];
    unsigned count;
    unsigned hits_applied;
    unsigned throws_spawned;
    unsigned rings_fired;
    unsigned bolts_fired;
    unsigned zaps_fired;
    unsigned defeated_count;
    unsigned shot_cooldown;
    unsigned pulse_cooldown;
    stage_blues_shot shots[STAGE_SHOT_MAX];
    stage_pickup pickups[STAGE_PICKUP_MAX];
    unsigned pickup_count;
    unsigned pickups_collected;
    stage_pop_fx pops[STAGE_POP_MAX];
    stage_prop props[STAGE_PROP_MAX];
    unsigned prop_count;
    unsigned units_per_pixel;
    const char *stage_name;
    int32_t bumper_rebound_vx; /* BIGBNS bumper push velocity this frame (0 if none) */
    uint32_t bumper_rebound_x; /* safe pushed-out X when bounced off a bumper */
} stage_enemies;

void stage_enemies_init_runway(stage_enemies *e, unsigned units_per_pixel);
void stage_enemies_init_pan_stage(stage_enemies *e, const char *stage_name,
                                  unsigned units_per_pixel);
void stage_enemies_init_sidescrl(stage_enemies *e, const char *stage_name,
                                 unsigned units_per_pixel);
void stage_enemies_init_special(stage_enemies *e, const char *stage_name,
                                unsigned units_per_pixel);

/*
 * Step AI one frame. Optional `haz` receives Gang throws (CThrown).
 * `apply_hits` 0 = animate/count only (FINALE PRESENT / --check safe).
 * Returns 1 if BonesLoose death this frame.
 */
int stage_enemies_step(stage_enemies *e, runway_skel *skel, uint32_t bones_x,
                       runway_hazards *haz, int apply_hits);

/*
 * Height- and player-action-aware step: tracks Blues shots/pulses, HEALTH
 * pickups (via runway_skel_add), and high jumps over ground enemies.
 */
int stage_enemies_step_player(stage_enemies *e, runway_skel *skel,
                              uint32_t bones_x, uint32_t bones_y,
                              uint32_t ground_y, int tall, int airborne,
                              runway_hazards *haz, int apply_hits);
int stage_enemies_step_player_bounded(stage_enemies *e, runway_skel *skel,
                                     uint32_t bones_x, uint32_t bones_y,
                                     uint32_t ground_y, int tall, int airborne,
                                     runway_hazards *haz, int apply_hits,
                                     const stage_batwood_contact *contact);

/* Emit player Blues attack (is_radial=0: C directional bolt, 1: A pulse).
 * Returns 1 if fired, 0 if on cooldown or invalid. */
int stage_enemies_fire_blues(stage_enemies *e, uint32_t bones_x, uint32_t bones_y,
                             int facing, int is_radial);

/* Add a collectible HEALTH pickup at world pixel coordinates (px, py_px). */
void stage_enemies_add_health_pickup(stage_enemies *e, unsigned px,
                                     unsigned py_px, int32_t heal);

/* Render test-accessible HEALTH helper for sprite inspection / HUD. */
int stage_enemies_health_sprite_count(void);

/* Active count of a kind (for tests / HUD). */
unsigned stage_enemies_count_kind(const stage_enemies *e, stage_enemy_kind kind);

#define STAGE_LAFF_BOSS_HP 8
/* True if 26LAFF boss Da Laff (STAGE_ENEMY_LORD) is still active. */
int stage_enemies_laff_boss_alive(const stage_enemies *e);
/* Return current HP of active 26LAFF boss (0 if defeated/absent) and optional max HP. */
int stage_enemies_laff_boss_hp(const stage_enemies *e, int *out_max_hp);

/* Remaining objective targets in arena stages (BIGBNS active ents, GENRM RED/POOF_RED, LAFF boss). */
unsigned stage_enemies_targets_remaining(const stage_enemies *e);

/* Distribute additional patrol enemies across a multi-screen sidescrl strip. */
void stage_enemies_populate_sidescrl_strip(stage_enemies *e, unsigned scroll_w);

/* Decode and cache authentic 04STXT pre-rendered 3D sprites from mounted disc. */
void stage_enemies_load_disc_sprites(cdimage *cd);

void stage_enemies_draw_houses(const stage_enemies *e, uint8_t *rgba, unsigned width,
                              unsigned height, int camera_x, int camera_y);

/* Render all active stage_enemies + runway_hazards (CThrown/zones) into dst_rgba. */
void stage_enemies_render(const stage_enemies *e, const runway_hazards *haz,
                          uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                          uint32_t camera_x, int screen_anchor_x, int base_y);

/*
 * Extended enemy renderer supporting per-X terrain floor lookup (floor_py[0..floor_w-1])
 * and RUNWAY RWorldSprite perspective scaling (z_perspective != 0).
 */
void stage_enemies_render_ex(const stage_enemies *e, const runway_hazards *haz,
                             uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                             uint32_t camera_x, int screen_anchor_x, int base_y,
                             const uint8_t *floor_py, unsigned floor_w,
                             int z_perspective);

/* Render Mr. Bones' authentic 04STXT 3D sprite at (foot_x, foot_y). */
void stage_enemies_draw_bones(uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                              int foot_x, int foot_y, int facing, int tall,
                              int airborne, unsigned anim_frame);

/* Scaled variant using RWorldSprite 256-fixed-point numerator/denominator. */
void stage_enemies_draw_bones_ex(uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                                 int foot_x, int foot_y, int facing, int tall,
                                 int airborne, unsigned anim_frame,
                                 unsigned scale_num, unsigned scale_den);

typedef struct {
    int valid;
    int foot_x, foot_y;
    int facing;
    int tall;
    int airborne;
    unsigned anim_frame;
    unsigned scale_num;
    unsigned scale_den;
} stage_bones_pose;

/* Prepare in-memory HD Mr. Bones sprites (load custom PNG/PAM/TGA overrides from
 * `custom_dir` when present, upscaling remaining frames with `mode`). */
int stage_enemies_prepare_hd_bones(mb_upscale_mode mode, const char *custom_dir);
void stage_enemies_set_hd_overlay(int enable);
void stage_enemies_reset_bones_pose(void);
void stage_enemies_set_bones_ground_y(int ground_y, const char *stage_name);
void stage_enemies_set_bones_fx(int hurt_timer, int invuln_timer, int dead);
void stage_enemies_spawn_bone_burst(stage_enemies *e, uint32_t world_x,
                                    uint32_t world_y, int count);
int stage_enemies_last_bones_pose(stage_bones_pose *out);
int stage_enemies_hd_custom_count(void);
/* Composite HD Mr. Bones sprite using last_pose onto an upscaled RGBA surface. */
int stage_enemies_draw_bones_hd(uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                                unsigned host_scale);

/*
 * Re-blit the 1x enemy/thrown sprites that the HD overlay suppresses from the
 * base surface (gang/lord/valley skeletons + spinning bones), using the slots
 * recorded during the last render. For 1x PPM capture while HD overlay is on.
 */
void stage_enemies_redraw_suppressed_1x(uint8_t *dst_rgba, unsigned dst_w,
                                        unsigned dst_h);

/*
 * Render the authentic Saturn CSkelBar "B [Skull] N E S" bone-piece HUD
 * from /04RUNWAY/04STXT (with outline slots behind active segments), falling
 * back to runway_compose_hud_skelbar if disc sprites are not loaded.
 */
void stage_enemies_draw_hud_skelbar(uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                                    int segments, int max_segments,
                                    int dst_x, int dst_y);

/* Glowing vertical energy portal barrier (amber-red when locked, emerald when open). */
void stage_enemies_draw_barrier(uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                                int gx, int y0, int y1, int locked, unsigned frames);

/* Structural emerald glass shard floor profile and 04STXT renderer for 15GLASS. */
uint8_t stage_enemies_glass_floor_at(unsigned world_x, unsigned strip_w);
void stage_enemies_draw_glass_platforms(uint8_t *dst_rgba, unsigned dst_w,
                                        unsigned dst_h, unsigned src_x,
                                        unsigned strip_w, unsigned finish_px,
                                        unsigned frames);

/* Projected dark wall silhouette behind Mr. Bones on 22SHADOW. */
void stage_enemies_draw_wall_shadow(uint8_t *dst_rgba, unsigned dst_w,
                                    unsigned dst_h, int foot_x, int foot_y,
                                    int facing, int tall, int airborne,
                                    unsigned anim_frame);

/* Authentic 07GUITAR / 14DRUM 04STXT stagecraft (crowd, electric guitar, drums, pylons). */
void stage_enemies_draw_rhythm_stage(uint8_t *dst_rgba, unsigned dst_w,
                                     unsigned dst_h, const char *stage_name,
                                     unsigned frames, unsigned combo,
                                     int last_grade, unsigned grade_timer,
                                     unsigned expected_kind);

void stage_enemies_draw_rhythm_stage_ex(uint8_t *dst_rgba, unsigned dst_w,
                                        unsigned dst_h, const char *stage_name,
                                        unsigned frames, unsigned combo,
                                        int last_grade, unsigned grade_timer,
                                        unsigned expected_kind,
                                        unsigned hits, unsigned hits_need);

/* Bottom-left hand + escaping little-guys rescue HUD for 19VILLAGE. */
void stage_enemies_draw_village_hud(uint8_t *dst_rgba, unsigned dst_w,
                                    unsigned dst_h, unsigned saved,
                                    unsigned total, unsigned frames);

/* Bottom-left semicircular Laugh-O-Meter arc gauge for 26LAFF. */
void stage_enemies_draw_laff_meter(uint8_t *dst_rgba, unsigned dst_w,
                                   unsigned dst_h, int boss_hp,
                                   int boss_max_hp, unsigned frames);

/* Swaying foreground skeletal skulls and arms layer for 05MAUSO. */
void stage_enemies_draw_mauso_fg(uint8_t *dst_rgba, unsigned dst_w,
                                 unsigned dst_h, uint32_t camera_x,
                                 unsigned frames);
int stage_enemies_spawn_mauso_assembler(stage_enemies *e, unsigned world_x_px,
                                        int arena_lo_px, int arena_hi_px,
                                        int32_t damage);
unsigned stage_enemies_mauso_alive(const stage_enemies *e);

/* Authentic 27FINALE blue electric guitar + directional Blues chord beam. */
void stage_enemies_draw_finale_guitar(uint8_t *dst_rgba, unsigned dst_w,
                                      unsigned dst_h, int foot_x, int foot_y,
                                      int facing, int target_x, int target_y,
                                      int shot_flash);

/* Stage-specific 04STXT environment props, obstacles, and atmospheric actors. */
void stage_enemies_draw_bat_branch(uint8_t *dst_rgba, unsigned dst_w,
                                   unsigned dst_h, int x0, int surface_y,
                                   int width_px, int variant);
void stage_enemies_draw_ice_floe(uint8_t *dst_rgba, unsigned dst_w,
                                 unsigned dst_h, int center_x, int surface_y,
                                 int variant, unsigned frames);
void stage_enemies_draw_ice_crack(uint8_t *dst_rgba, unsigned dst_w,
                                  unsigned dst_h, int center_x, int ground_y,
                                  int crack_stage, int is_hole);
void stage_enemies_draw_staing_arms(uint8_t *dst_rgba, unsigned dst_w,
                                    unsigned dst_h, int center_x, int y,
                                    unsigned frames);
void stage_enemies_draw_staing_spire(uint8_t *dst_rgba, unsigned dst_w,
                                     unsigned dst_h, int center_x, int bottom_y,
                                     int variant);
void stage_enemies_draw_runway_ghost(uint8_t *dst_rgba, unsigned dst_w,
                                     unsigned dst_h, int foot_x, int foot_y,
                                     unsigned frames);

#endif
