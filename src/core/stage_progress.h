#ifndef MB_STAGE_PROGRESS_H
#define MB_STAGE_PROGRESS_H
#include "nvm_shadow.h"
#include "stage_catalog.h"

/*
 * §15 — campaign progression + automatic _NvmShadow save.
 * Win → unlock next campaign index, persist nvmshadow.bin.
 * Lose → route to /28LOSE then return to MENU (or retry).
 * FINALE win → campaign complete.
 */

typedef enum {
    STAGE_OUTCOME_NONE = 0,
    STAGE_OUTCOME_WIN = 1,
    STAGE_OUTCOME_LOSE = 2,
    STAGE_OUTCOME_ABORT = 3
} stage_outcome;

typedef enum {
    STAGE_ROUTE_MENU = 0,
    STAGE_ROUTE_STAGE = 1,
    STAGE_ROUTE_LOSE = 2,
    STAGE_ROUTE_COMPLETE = 3 /* FINALE cleared */
} stage_route;

typedef struct {
    const stage_catalog *cat;
    mb_nvm_shadow *nvm;
    char save_dir[260];
    int current_campaign; /* index into campaign[]; -1 if none */
    stage_route route;
    char next_iso[32];
    char status[160];
} stage_progress;

void stage_progress_init(stage_progress *p, const stage_catalog *cat,
                         mb_nvm_shadow *nvm, const char *save_dir);

/* Sync unlock bits from catalog presence: unlock all cleared + current. */
void stage_progress_apply_nvm(stage_progress *p);

/* Start campaign at first unlocked, or force iso_dir. */
int stage_progress_begin(stage_progress *p, const char *iso_dir_or_null);

/* After a stage finishes: update NvmShadow, choose next route. */
int stage_progress_on_outcome(stage_progress *p, const char *finished_iso,
                              stage_outcome outcome);

/* Skip EMPTY campaign entries when advancing. */
const stage_info *stage_progress_skip_empty(const stage_catalog *cat,
                                            const stage_info *from_next);

#endif
