#include "stage_progress.h"
#include <stdio.h>
#include <string.h>

void stage_progress_init(stage_progress *p, const stage_catalog *cat,
                         mb_nvm_shadow *nvm, const char *save_dir) {
    if (!p) return;
    memset(p, 0, sizeof *p);
    p->cat = cat;
    p->nvm = nvm;
    p->current_campaign = -1;
    p->route = STAGE_ROUTE_MENU;
    if (save_dir)
        snprintf(p->save_dir, sizeof p->save_dir, "%s", save_dir);
}

void stage_progress_apply_nvm(stage_progress *p) {
    unsigned i;
    if (!p || !p->cat || !p->nvm) return;
    for (i = 0; i < p->cat->campaign_count; i++) {
        if (mb_nvm_level_unlocked(p->nvm, i))
            continue;
        /* Keep host defaults: only index 0 unlocked until wins. */
    }
    (void)i;
}

const stage_info *stage_progress_skip_empty(const stage_catalog *cat,
                                            const stage_info *from_next) {
    const stage_info *s = from_next;
    while (s && s->kind == STAGE_KIND_EMPTY)
        s = stage_catalog_next(cat, s->iso_dir);
    return s;
}

int stage_progress_begin(stage_progress *p, const char *iso_dir_or_null) {
    const stage_info *s;
    if (!p || !p->cat) return -1;
    if (iso_dir_or_null && iso_dir_or_null[0]) {
        s = stage_catalog_find(p->cat, iso_dir_or_null);
        if (!s) s = stage_catalog_find_name(p->cat, iso_dir_or_null);
    } else {
        unsigned i;
        s = NULL;
        for (i = 0; i < p->cat->campaign_count; i++) {
            if (p->nvm && mb_nvm_level_unlocked(p->nvm, i)) {
                s = p->cat->campaign[i];
                break;
            }
        }
        if (!s && p->cat->campaign_count > 0)
            s = p->cat->campaign[0];
    }
    if (!s) return -1;
    if (s->kind == STAGE_KIND_EMPTY)
        s = stage_progress_skip_empty(p->cat, stage_catalog_next(p->cat, s->iso_dir));
    if (!s) return -1;
    p->current_campaign = s->campaign_index;
    p->route = STAGE_ROUTE_STAGE;
    snprintf(p->next_iso, sizeof p->next_iso, "%s", s->iso_dir);
    snprintf(p->status, sizeof p->status, "PROGRESS begin %s (camp=%d)",
             s->name, s->campaign_index);
    return 0;
}

static int persist(stage_progress *p) {
    if (!p || !p->nvm || !p->save_dir[0]) return -1;
    return mb_nvm_save(p->nvm, p->save_dir);
}

int stage_progress_on_outcome(stage_progress *p, const char *finished_iso,
                              stage_outcome outcome) {
    const stage_info *cur;
    const stage_info *next;
    if (!p || !p->cat || !finished_iso) return -1;
    cur = stage_catalog_find(p->cat, finished_iso);
    if (!cur) {
        p->route = STAGE_ROUTE_MENU;
        snprintf(p->status, sizeof p->status, "PROGRESS unknown stage -> MENU");
        return -1;
    }

    if (cur->kind == STAGE_KIND_LOSE) {
        p->route = STAGE_ROUTE_MENU;
        p->next_iso[0] = 0;
        snprintf(p->status, sizeof p->status, "PROGRESS LOSE -> MENU");
        return 0;
    }

    if (outcome == STAGE_OUTCOME_LOSE || outcome == STAGE_OUTCOME_ABORT) {
        p->route = STAGE_ROUTE_LOSE;
        snprintf(p->next_iso, sizeof p->next_iso, "%s", "/28LOSE");
        snprintf(p->status, sizeof p->status,
                 "PROGRESS %s -> LOSE (outcome=%d)", cur->name, (int)outcome);
        return 0;
    }

    if (outcome != STAGE_OUTCOME_WIN) {
        p->route = STAGE_ROUTE_MENU;
        return -1;
    }

    /* Win: unlock current + next; advance current level word. */
    if (p->nvm && cur->in_campaign && cur->campaign_index >= 0) {
        mb_nvm_unlock_level(p->nvm, (unsigned)cur->campaign_index);
        next = stage_progress_skip_empty(p->cat,
                                         stage_catalog_next(p->cat, cur->iso_dir));
        if (next && next->campaign_index >= 0)
            mb_nvm_unlock_level(p->nvm, (unsigned)next->campaign_index);
        mb_nvm_set_u32(p->nvm, MB_NVM_DEFAULT_LEVEL_OFF,
                       (uint32_t)(cur->campaign_index + 1));
        p->nvm->dirty = 1;
        if (persist(p) == 0)
            snprintf(p->status, sizeof p->status, "PROGRESS saved nvm after %s",
                     cur->name);
        else
            snprintf(p->status, sizeof p->status, "PROGRESS nvm save failed after %s",
                     cur->name);
    }

    if (cur->kind == STAGE_KIND_FINALE || cur->kind == STAGE_KIND_LOSE) {
        p->route = STAGE_ROUTE_COMPLETE;
        p->next_iso[0] = 0;
        snprintf(p->status, sizeof p->status, "PROGRESS campaign complete (%s)",
                 cur->name);
        return 0;
    }

    next = stage_progress_skip_empty(p->cat, stage_catalog_next(p->cat, cur->iso_dir));
    if (!next) {
        p->route = STAGE_ROUTE_COMPLETE;
        p->next_iso[0] = 0;
        snprintf(p->status, sizeof p->status, "PROGRESS no next after %s", cur->name);
        return 0;
    }
    p->current_campaign = next->campaign_index;
    p->route = STAGE_ROUTE_STAGE;
    snprintf(p->next_iso, sizeof p->next_iso, "%s", next->iso_dir);
    snprintf(p->status, sizeof p->status, "PROGRESS %s win -> %s (disc%d)",
             cur->name, next->name, next->preferred_disc);
    return 0;
}
