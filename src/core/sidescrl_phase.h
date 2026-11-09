#ifndef MB_SIDESCRL_PHASE_H
#define MB_SIDESCRL_PHASE_H
#include "core/cdimage.h"
#include "core/runway_skel.h"
#include "core/saturn_pad.h"
#include "core/scrl_map.h"
#include "core/stage_enemy.h"
#include <stddef.h>
#include <stdint.h>

/*
 * §17/§23 native side-scroll host (MAUSO, VILLAGE, BAT1-3, GLASS, VALLEY,
 * SHADOW, VORTEX, LAFF). MAP: scroller.o + scrlcoll.o + sidescrl.o + bkgdata.o.
 * §23: LoadCDB/LoadMap/DrawMap multi-layer ACDB/ATIL…DTIL + AMAP floor;
 * SCRLCOLLGetHixel3D / pFloorHixels via scrl_map. Palette from 02TITL/04STXT/CODE.
 * §24: poofs.o / CBlueSparkle enemies (denser on BAT1–3).
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

    uint32_t bones_x, bones_y;
    uint32_t camera_x;
    uint32_t finish_x;
    uint32_t ground_y;
    int facing;
    int32_t vel_x, vel_y;
    int airborne;
    runway_skel skel;
    stage_enemies enemies;
    unsigned frames;
    unsigned audio_track;
    unsigned sfx_flags;
    unsigned hazard_hits;
    char status[160];
} sidescrl_phase;

int sidescrl_phase_load(sidescrl_phase *phase, cdimage *cd, const char *iso_dir);
void sidescrl_phase_free(sidescrl_phase *phase);
int sidescrl_phase_step(sidescrl_phase *phase, const saturn_pad *pad);
int sidescrl_phase_render(sidescrl_phase *phase);
int sidescrl_phase_done(const sidescrl_phase *phase);

#endif
