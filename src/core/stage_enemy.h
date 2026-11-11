#ifndef MB_STAGE_ENEMY_H
#define MB_STAGE_ENEMY_H
#include "cdimage.h"
#include "runway_hazard.h"
#include "runway_skel.h"
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
    STAGE_ENEMY_BAT = 11     /* BAT1..3 swooping bat */
} stage_enemy_kind;

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
} stage_enemy;

typedef struct {
    stage_enemy ents[STAGE_ENEMY_MAX];
    unsigned count;
    unsigned hits_applied;
    unsigned throws_spawned;
    unsigned rings_fired;
    unsigned bolts_fired;
    unsigned zaps_fired;
    unsigned units_per_pixel;
    const char *stage_name;
} stage_enemies;

void stage_enemies_init_runway(stage_enemies *e, unsigned units_per_pixel);
/* Authentic /04RUNWAY/03CODE CheckJumpys wave population (Gang waves + bone throws). */
void stage_enemies_init_runway_rom(stage_enemies *e, unsigned units_per_pixel);
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

/* Active count of a kind (for tests / HUD). */
unsigned stage_enemies_count_kind(const stage_enemies *e, stage_enemy_kind kind);

/* Distribute additional patrol enemies across a multi-screen sidescrl strip. */
void stage_enemies_populate_sidescrl_strip(stage_enemies *e, unsigned scroll_w);

/* Decode and cache authentic 04STXT pre-rendered 3D sprites from mounted disc. */
void stage_enemies_load_disc_sprites(cdimage *cd);

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

/*
 * Render the authentic Saturn CSkelBar "B [Skull] N E S" bone-piece HUD
 * from /04RUNWAY/04STXT (with outline slots behind active segments), falling
 * back to runway_compose_hud_skelbar if disc sprites are not loaded.
 */
void stage_enemies_draw_hud_skelbar(uint8_t *dst_rgba, unsigned dst_w, unsigned dst_h,
                                    int segments, int max_segments,
                                    int dst_x, int dst_y);

#endif
