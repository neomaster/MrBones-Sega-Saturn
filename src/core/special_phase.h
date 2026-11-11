#ifndef MB_SPECIAL_PHASE_H
#define MB_SPECIAL_PHASE_H
#include "core/cdimage.h"
#include "core/duck_avi.h"
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
#define SPECIAL_HITS_TO_WIN 8u
#define SPECIAL_PRESENT_PAGES 6u
/* CRing: 16 entries of 0x6c bytes; docs/finale-ring-analysis.md */
#define FINALE_RING_SLOTS 16u
#define FINALE_RING_PHASE_WRAP 0x01000000

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
    unsigned note_cursor;       /* index into rhythm->notes */

    /* §20 DUCK AVI present */
    duck_avi movie;
    int movie_ok;
    int movie_pcm_pending; /* host should play movie.pcm once */

    /* Action / boss enemies (§24) */
    uint32_t bones_x, bones_y;
    uint32_t finish_x;
    int facing;
    int32_t vel_x, vel_y;
    int airborne;
    runway_skel skel;
    stage_enemies enemies;
    unsigned hazard_hits;

    /* FINALE CRing arena (ShootRing / Brain_CRing) */
    int ring_active[FINALE_RING_SLOTS]; /* entry +0x68 */
    int ring_phase;                     /* object+0x704 (ShootRing select) */
    int ring_phase_acc;                 /* Brain accumulator before >>16 */
    int ring_phase_mode;                /* index into 0x002b2158 incr table */
    int ring_active_count;              /* object+0x708 */
    int ring_cleared;                   /* EndLevel(0) / clear flag fired */
    int finale_movie_armed;             /* 20AVI opened; PlayDuckMovie ready */
    unsigned ring_shots;
    int ring_last_index;                /* last ShootRing slot, -1 none */
    int ring_last_activated;            /* 1=activated, 0=deactivated */

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
/* PlayDuckMovie(3) stand-in: rewind/open /27FINALE/20AVI and set movie_ok. */
int special_phase_finale_play_ending(special_phase *phase);

int special_phase_load(special_phase *phase, cdimage *cd, const char *iso_dir,
                       stage_kind kind);
void special_phase_free(special_phase *phase);
int special_phase_step(special_phase *phase, const saturn_pad *pad);
int special_phase_render(special_phase *phase);
int special_phase_done(const special_phase *phase);

#endif
