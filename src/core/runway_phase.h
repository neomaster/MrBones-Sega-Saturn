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
    uint32_t finish_x;
    char iso_dir[32];
    char stage_name[16];
    uint8_t *hud_health_rgba;
    unsigned hud_health_w, hud_health_h;
    uint8_t *hud_pause_rgba;
    unsigned hud_pause_w, hud_pause_h;
    char status[128];
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

#endif
