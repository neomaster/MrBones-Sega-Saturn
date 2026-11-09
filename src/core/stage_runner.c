#include "stage_runner.h"
#include "runway_sfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int try_load_runway(stage_runner *r, cdimage *cd) {
    if (runway_phase_load_dir(&r->runway, cd, r->iso_dir))
        return -1;
    r->runway_live = 1;
    r->state = STAGE_RS_PLAYING;
    snprintf(r->status, sizeof r->status, "STAGE %s playing (runway_phase)", r->name);
    return 0;
}

static int try_load_sidescrl(stage_runner *r, cdimage *cd) {
    if (sidescrl_phase_load(&r->sidescrl, cd, r->iso_dir))
        return -1;
    r->sidescrl_live = 1;
    r->state = STAGE_RS_PLAYING;
    snprintf(r->status, sizeof r->status, "STAGE %s playing (sidescrl_phase)", r->name);
    return 0;
}

static int try_load_special(stage_runner *r, cdimage *cd) {
    if (special_phase_load(&r->special, cd, r->iso_dir, r->kind))
        return -1;
    r->special_live = 1;
    r->state = STAGE_RS_PLAYING;
    snprintf(r->status, sizeof r->status, "STAGE %s playing (special_phase mode=%d)",
             r->name, (int)r->special.mode);
    return 0;
}

static int try_load_smoke(stage_runner *r, cdimage *cd, int avi_only) {
    if (stage_assets_load_ex(cd, r->iso_dir, &r->assets, avi_only))
        return -1;
    if (!avi_only && r->assets.is_stub)
        return -1;
    r->state = STAGE_RS_PLAYING;
    snprintf(r->status, sizeof r->status,
             "STAGE %s [%s] code=%zu stxt=%zu snd=%zu pan=%zu avi=%zu (smoke)",
             r->name, stage_kind_name(r->kind), r->assets.code_size,
             r->assets.textures_size, r->assets.sound_size, r->assets.pan_size,
             r->assets.avi_size);
    return 0;
}

static int try_load_empty(stage_runner *r) {
    r->state = STAGE_RS_PLAYING;
    r->outcome = STAGE_OUTCOME_WIN;
    snprintf(r->status, sizeof r->status,
             "STAGE %s [empty] skip stub (instant)", r->name);
    return 0;
}

static int name_looks_pan_playable(const char *name) {
    return name &&
           (strcmp(name, "RUNWAY") == 0 || strcmp(name, "LOGS") == 0 ||
            strcmp(name, "ICE1") == 0 || strcmp(name, "ICE2") == 0 ||
            strcmp(name, "MOAT") == 0 || strcmp(name, "RIDE") == 0 ||
            strcmp(name, "STAING") == 0);
}

static int name_looks_side_playable(const char *name) {
    return name &&
           (strcmp(name, "MAUSO") == 0 || strcmp(name, "VALLEY") == 0 ||
            strcmp(name, "BAT1") == 0 || strcmp(name, "BAT2") == 0 ||
            strcmp(name, "BAT3") == 0 || strcmp(name, "GLASS") == 0 ||
            strcmp(name, "VILLAGE") == 0 || strcmp(name, "SHADOW") == 0 ||
            strcmp(name, "VORTEX") == 0 || strcmp(name, "LAFF") == 0);
}

static int name_looks_special_playable(const char *name) {
    return name &&
           (strcmp(name, "GUITAR") == 0 || strcmp(name, "DRUM") == 0 ||
            strcmp(name, "BIGBNS") == 0 || strcmp(name, "GENRM") == 0 ||
            strcmp(name, "FINALE") == 0 || strcmp(name, "LOSE") == 0);
}

static int kind_is_special(stage_kind k) {
    return k == STAGE_KIND_RHYTHM || k == STAGE_KIND_BIGBONES ||
           k == STAGE_KIND_GENROOM || k == STAGE_KIND_FINALE ||
           k == STAGE_KIND_LOSE || k == STAGE_KIND_MOVIE;
}

static int try_load_playable(stage_runner *r, cdimage *cd) {
    size_t pan = 0;
    int stub = 1;
    (void)stage_assets_probe(cd, r->iso_dir, NULL, &pan, &stub);
    /* §18 specials first (even when 15PAN present — BIGBNS/GENRM). */
    if (kind_is_special(r->kind) || name_looks_special_playable(r->name)) {
        if (try_load_special(r, cd) == 0) return 0;
    }
    if (pan > 100) {
        if (try_load_runway(r, cd) == 0) return 0;
    }
    if (name_looks_side_playable(r->name) || r->kind == STAGE_KIND_SIDE) {
        if (try_load_sidescrl(r, cd) == 0) return 0;
    }
    if (try_load_runway(r, cd) == 0) return 0;
    if (try_load_sidescrl(r, cd) == 0) return 0;
    if (try_load_special(r, cd) == 0) return 0;
    return -1;
}

static int begin_on_cd(stage_runner *r, cdimage *cd, int mounted_disc,
                       const stage_catalog *cat) {
    const stage_info *info = cat ? stage_catalog_find(cat, r->iso_dir) : NULL;
    int preferred = info ? info->preferred_disc : 0;
    int playable;

    if (info) {
        r->kind = info->kind;
        r->playable = info->playable;
    } else if (!r->playable) {
        r->playable = name_looks_pan_playable(r->name) ||
                      name_looks_side_playable(r->name) ||
                      name_looks_special_playable(r->name) ||
                      kind_is_special(r->kind);
    }
    playable = r->playable;
    r->mounted_disc = mounted_disc;
    r->outcome = STAGE_OUTCOME_NONE;

    if (r->kind == STAGE_KIND_EMPTY)
        return try_load_empty(r);

    if (preferred != 0 && preferred != mounted_disc) {
        size_t code = 0;
        int stub = 1;
        if (r->kind == STAGE_KIND_LOSE || r->kind == STAGE_KIND_INTRO) {
            stage_assets a;
            if (stage_assets_load_ex(cd, r->iso_dir, &a, 1) == 0 && a.avi_size > 0) {
                stage_assets_free(&a);
            } else {
                stage_assets_free(&a);
                r->state = STAGE_RS_NEED_DISC;
                r->need_disc = preferred;
                snprintf(r->status, sizeof r->status,
                         "STAGE %s NEED_DISC %d (INSERTCD) mounted=%d",
                         r->name, preferred, mounted_disc);
                return 0;
            }
        } else if (stage_assets_probe(cd, r->iso_dir, &code, NULL, &stub) == 0 &&
                   !stub) {
            /* Data actually here. */
        } else {
            r->state = STAGE_RS_NEED_DISC;
            r->need_disc = preferred;
            snprintf(r->status, sizeof r->status,
                     "STAGE %s NEED_DISC %d (INSERTCD) mounted=%d",
                     r->name, preferred, mounted_disc);
            return 0;
        }
    }

    if (r->kind == STAGE_KIND_LOSE || r->kind == STAGE_KIND_MOVIE ||
        r->kind == STAGE_KIND_INTRO || r->kind == STAGE_KIND_FINALE) {
        if (try_load_special(r, cd) == 0) return 0;
    } else {
        if (playable && try_load_playable(r, cd) == 0) return 0;
        if (try_load_smoke(r, cd, 0) == 0) return 0;
    }

    r->state = STAGE_RS_FAILED;
    r->outcome = STAGE_OUTCOME_LOSE;
    snprintf(r->status, sizeof r->status, "STAGE %s load failed on disc %d: %.120s",
             r->name, mounted_disc, r->special.status);
    return -1;
}

static void set_stage_name(stage_runner *r, const stage_catalog *cat,
                           const char *iso_dir) {
    const char *p = strrchr(iso_dir, '/');
    const stage_info *info = cat ? stage_catalog_find(cat, iso_dir) : NULL;
    p = p ? p + 1 : iso_dir;
    if (info) {
        snprintf(r->name, sizeof r->name, "%s", info->name);
        r->kind = info->kind;
        return;
    }
    if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9')
        p += 2;
    snprintf(r->name, sizeof r->name, "%s", p);
}

int stage_runner_begin(stage_runner *r, cdimage *cd, int mounted_disc,
                       const stage_catalog *cat, const char *iso_dir) {
    if (!r || !cd || !iso_dir) return -1;
    memset(r, 0, sizeof *r);
    snprintf(r->iso_dir, sizeof r->iso_dir, "%s", iso_dir);
    set_stage_name(r, cat, iso_dir);
    return begin_on_cd(r, cd, mounted_disc, cat);
}

int stage_runner_begin_movie(stage_runner *r, cdimage *cd, int mounted_disc,
                             const char *iso_dir) {
    if (!r || !cd || !iso_dir) return -1;
    memset(r, 0, sizeof *r);
    snprintf(r->iso_dir, sizeof r->iso_dir, "%s", iso_dir);
    set_stage_name(r, NULL, iso_dir);
    r->kind = STAGE_KIND_MOVIE;
    r->mounted_disc = mounted_disc;
    r->playable = 1;
    if (try_load_special(r, cd) == 0) return 0;
    r->state = STAGE_RS_FAILED;
    snprintf(r->status, sizeof r->status, "STAGE %s movie load failed: %.120s",
             r->name, r->special.status);
    return -1;
}

int stage_runner_resume(stage_runner *r, cdimage *cd, int mounted_disc) {
    stage_kind keep_kind;
    char keep_name[16];
    char keep_iso[32];
    int keep_playable;
    if (!r || !cd) return -1;
    if (r->state != STAGE_RS_NEED_DISC) return -1;
    keep_kind = r->kind;
    keep_playable = r->playable;
    snprintf(keep_name, sizeof keep_name, "%s", r->name);
    snprintf(keep_iso, sizeof keep_iso, "%s", r->iso_dir);
    stage_assets_free(&r->assets);
    if (r->runway_live) {
        runway_phase_free(&r->runway);
        r->runway_live = 0;
    }
    if (r->sidescrl_live) {
        sidescrl_phase_free(&r->sidescrl);
        r->sidescrl_live = 0;
    }
    if (r->special_live) {
        special_phase_free(&r->special);
        r->special_live = 0;
    }
    r->kind = keep_kind;
    r->playable = keep_playable;
    snprintf(r->name, sizeof r->name, "%s", keep_name);
    snprintf(r->iso_dir, sizeof r->iso_dir, "%s", keep_iso);
    return begin_on_cd(r, cd, mounted_disc, NULL);
}

void stage_runner_free(stage_runner *r) {
    if (!r) return;
    if (r->runway_live)
        runway_phase_free(&r->runway);
    if (r->sidescrl_live)
        sidescrl_phase_free(&r->sidescrl);
    if (r->special_live)
        special_phase_free(&r->special);
    stage_assets_free(&r->assets);
    memset(r, 0, sizeof *r);
}

int stage_runner_step(stage_runner *r, const saturn_pad *pad) {
    if (!r || stage_runner_done(r)) return 0;
    r->frames++;
    if (r->state == STAGE_RS_NEED_DISC || r->state == STAGE_RS_FAILED)
        return 0;
    if (r->runway_live) {
        runway_phase_step(&r->runway, pad);
        snprintf(r->status, sizeof r->status, "%s", r->runway.status);
        if (runway_phase_done(&r->runway)) {
            if (r->runway.state == RUNWAY_ST_FINISHED) {
                r->state = STAGE_RS_FINISHED;
                r->outcome = STAGE_OUTCOME_WIN;
            } else {
                r->state = STAGE_RS_FAILED;
                r->outcome = STAGE_OUTCOME_LOSE;
            }
        }
        return 1;
    }
    if (r->sidescrl_live) {
        sidescrl_phase_step(&r->sidescrl, pad);
        snprintf(r->status, sizeof r->status, "%s", r->sidescrl.status);
        if (sidescrl_phase_done(&r->sidescrl)) {
            if (r->sidescrl.state == SIDESCRL_ST_FINISHED) {
                r->state = STAGE_RS_FINISHED;
                r->outcome = STAGE_OUTCOME_WIN;
            } else {
                r->state = STAGE_RS_FAILED;
                r->outcome = STAGE_OUTCOME_LOSE;
            }
        }
        return 1;
    }
    if (r->special_live) {
        special_phase_step(&r->special, pad);
        snprintf(r->status, sizeof r->status, "%s", r->special.status);
        if (special_phase_done(&r->special)) {
            if (r->special.state == SPECIAL_ST_FINISHED) {
                r->state = STAGE_RS_FINISHED;
                r->outcome = STAGE_OUTCOME_WIN;
            } else {
                r->state = STAGE_RS_FAILED;
                r->outcome = STAGE_OUTCOME_LOSE;
            }
        }
        return 1;
    }
    if (r->frames >= 3) {
        r->state = STAGE_RS_FINISHED;
        if (r->outcome == STAGE_OUTCOME_NONE)
            r->outcome = STAGE_OUTCOME_WIN;
        snprintf(r->status, sizeof r->status,
                 "STAGE %s [%s] finished smoke code=%zu avi=%zu",
                 r->name, stage_kind_name(r->kind), r->assets.code_size,
                 r->assets.avi_size);
    }
    return 1;
}

int stage_runner_done(const stage_runner *r) {
    return r && (r->state == STAGE_RS_FINISHED || r->state == STAGE_RS_FAILED);
}

const uint8_t *stage_runner_frame(const stage_runner *r) {
    if (!r) return NULL;
    if (r->runway_live) return r->runway.frame_rgba;
    if (r->sidescrl_live) return r->sidescrl.frame_rgba;
    if (r->special_live) return r->special.frame_rgba;
    return NULL;
}

const char *stage_runner_status(const stage_runner *r) {
    return r ? r->status : "";
}

stage_outcome stage_runner_outcome(const stage_runner *r) {
    return r ? r->outcome : STAGE_OUTCOME_NONE;
}
