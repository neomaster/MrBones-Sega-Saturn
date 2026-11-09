#ifndef MB_STAGE_RUNNER_H
#define MB_STAGE_RUNNER_H
#include "cdimage.h"
#include "runway_phase.h"
#include "saturn_pad.h"
#include "sidescrl_phase.h"
#include "special_phase.h"
#include "stage_assets.h"
#include "stage_catalog.h"
#include "stage_progress.h"
#include <stddef.h>

/*
 * §14–§18 stage_runner — any DirNameTable entry.
 * RUNWAY/pan → runway_phase. Side-scroll → sidescrl_phase.
 * Specials → special_phase. NEED_DISC → INSERTCD.
 */

typedef enum {
    STAGE_RS_IDLE = 0,
    STAGE_RS_NEED_DISC = 1,
    STAGE_RS_PLAYING = 2,
    STAGE_RS_FINISHED = 3,
    STAGE_RS_FAILED = 4
} stage_runner_state;

typedef struct {
    stage_runner_state state;
    stage_assets assets;
    runway_phase runway;
    sidescrl_phase sidescrl;
    special_phase special;
    int runway_live;
    int sidescrl_live;
    int special_live;
    int playable; /* native phase (kept across NEED_DISC resume) */
    int need_disc;
    int mounted_disc;
    stage_kind kind;
    stage_outcome outcome; /* set when done */
    char iso_dir[32];
    char name[16];
    unsigned frames;
    char status[192];
} stage_runner;

int stage_runner_begin(stage_runner *r, cdimage *cd, int mounted_disc,
                       const stage_catalog *cat, const char *iso_dir);
int stage_runner_begin_movie(stage_runner *r, cdimage *cd, int mounted_disc,
                             const char *iso_dir);
void stage_runner_free(stage_runner *r);
int stage_runner_resume(stage_runner *r, cdimage *cd, int mounted_disc);

int stage_runner_step(stage_runner *r, const saturn_pad *pad);
int stage_runner_done(const stage_runner *r);
const uint8_t *stage_runner_frame(const stage_runner *r);
const char *stage_runner_status(const stage_runner *r);
stage_outcome stage_runner_outcome(const stage_runner *r);

#endif
