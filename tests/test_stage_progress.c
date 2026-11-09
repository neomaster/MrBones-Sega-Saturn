#include "core/cdimage.h"
#include "core/identity.h"
#include "core/nvm_shadow.h"
#include "core/stage_catalog.h"
#include "core/stage_progress.h"
#include "core/stage_runner.h"
#include "core/special_phase.h"
#include "core/saturn_pad.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

static int open_disc(cdimage *cd, const char *path, const char *man_path, int disc_no) {
    char err[512];
    id_manifest manifest;
    id_result result;
    if (manifest_load(&manifest, man_path, err, sizeof err)) return -1;
    if (cd_open(cd, path, err, sizeof err)) {
        manifest_free(&manifest);
        return -1;
    }
    if (!identity_check(&manifest, cd, disc_no, ID_QUICK, &result)) {
        cd_close(cd);
        manifest_free(&manifest);
        return -1;
    }
    manifest_free(&manifest);
    return 0;
}

static int run_smoke(stage_runner *r, cdimage *cd, int disc, stage_catalog *cat,
                     const char *iso) {
    saturn_pad pad = {0};
    int i, max_frames;
    if (stage_runner_begin(r, cd, disc, cat, iso)) return -1;
    if (r->state == STAGE_RS_NEED_DISC) return 1; /* caller swaps */
    /* Empty/smoke stubs finish fast; special/runway/sidescrl need input. */
    max_frames = (r->special_live || r->runway_live || r->sidescrl_live) ? 5000 : 16;
    if (r->special_live && r->special.mode == SPECIAL_MODE_PRESENT && r->special.movie_ok) {
        r->special.movie.tick_external = 1;
        r->special.movie.tick_us = r->special.movie.us_per_frame + 1u;
        if (r->special.movie.frame_count < 1000000u)
            max_frames = (int)r->special.movie.frame_count + 2;
    }
    for (i = 0; i < max_frames && !stage_runner_done(r); i++) {
        if (r->runway_live || r->sidescrl_live) {
            saturn_pad_update_host(&pad, MB_PAD_RIGHT);
        } else if (r->special_live) {
            unsigned btn = MB_PAD_RIGHT;
            if (r->special.mode == SPECIAL_MODE_RHYTHM &&
                (i % (int)SPECIAL_BEAT_PERIOD) <= 2)
                btn |= MB_PAD_A;
            saturn_pad_update_host(&pad, btn);
        }
        stage_runner_step(r, &pad);
    }
    return stage_runner_done(r) ? 0 : -1;
}

int main(void) {
    const char *d1 = getenv("MB_DISC1");
    const char *d2 = getenv("MB_DISC2");
    const char *man = getenv("MB_MANIFEST");
    const char *save = "test_save_prog";
    cdimage disc1, disc2;
    stage_catalog cat;
    stage_progress prog;
    mb_nvm_shadow nvm;
    stage_runner runner;
    const stage_info *guitar, *genrm, *finale, *lose, *troll;
    int opened2 = 0;
    unsigned i;

    if (!d1 || !man) {
        printf("SKIP stage_progress\n");
        return 0;
    }
#ifdef _WIN32
    _mkdir(save);
#else
    mkdir(save, 0755);
#endif
    CHECK(open_disc(&disc1, d1, man, 1) == 0);
    if (d2 && open_disc(&disc2, d2, man, 2) == 0)
        opened2 = 1;

    CHECK(stage_catalog_build(&cat, &disc1, opened2 ? &disc2 : NULL) == 0);
    CHECK(cat.campaign_count == 24);

    guitar = stage_catalog_find_name(&cat, "GUITAR");
    CHECK(guitar && guitar->kind == STAGE_KIND_RHYTHM);
    CHECK(guitar->preferred_disc == 1);
    CHECK(guitar->code_size_best > STAGE_STUB_CODE_MAX);

    troll = stage_catalog_find_name(&cat, "TROLL1");
    CHECK(troll && troll->kind == STAGE_KIND_EMPTY);

    if (opened2) {
        genrm = stage_catalog_find_name(&cat, "GENRM");
        finale = stage_catalog_find_name(&cat, "FINALE");
        CHECK(genrm && genrm->kind == STAGE_KIND_GENROOM);
        CHECK(genrm->preferred_disc == 2);
        CHECK(finale && finale->kind == STAGE_KIND_FINALE);
        CHECK(finale->preferred_disc == 2);
    }

    lose = stage_catalog_find_name(&cat, "LOSE");
    CHECK(lose && lose->kind == STAGE_KIND_LOSE);
    CHECK(lose->avi_size_best > 0);

    mb_nvm_init_default(&nvm);
    stage_progress_init(&prog, &cat, &nvm, save);
    CHECK(stage_progress_begin(&prog, "/04RUNWAY") == 0);
    CHECK(prog.route == STAGE_ROUTE_STAGE);

    /* Win RUNWAY → unlock MAUSO, save. */
    CHECK(stage_progress_on_outcome(&prog, "/04RUNWAY", STAGE_OUTCOME_WIN) == 0);
    CHECK(mb_nvm_level_unlocked(&nvm, 0) == 1);
    CHECK(mb_nvm_level_unlocked(&nvm, 1) == 1);
    CHECK(prog.route == STAGE_ROUTE_STAGE);
    CHECK(strstr(prog.next_iso, "MAUSO"));

    /* Lose → LOSE. */
    CHECK(stage_progress_on_outcome(&prog, "/05MAUSO", STAGE_OUTCOME_LOSE) == 0);
    CHECK(prog.route == STAGE_ROUTE_LOSE);
    CHECK(strcmp(prog.next_iso, "/28LOSE") == 0);
    CHECK(stage_progress_on_outcome(&prog, "/28LOSE", STAGE_OUTCOME_LOSE) == 0);
    CHECK(prog.route == STAGE_ROUTE_MENU);
    CHECK(prog.next_iso[0] == 0);

    /* Specials smoke. */
    memset(&runner, 0, sizeof runner);
    CHECK(run_smoke(&runner, &disc1, 1, &cat, "/07GUITAR") == 0);
    stage_runner_free(&runner);
    CHECK(run_smoke(&runner, &disc1, 1, &cat, "/14DRUM") == 0);
    stage_runner_free(&runner);
    CHECK(run_smoke(&runner, &disc1, 1, &cat, "/16BIGBNS") == 0);
    stage_runner_free(&runner);
    CHECK(run_smoke(&runner, &disc1, 1, &cat, "/12TROLL1") == 0); /* empty skip */
    CHECK(runner.outcome == STAGE_OUTCOME_WIN);
    stage_runner_free(&runner);
    CHECK(run_smoke(&runner, &disc1, 1, &cat, "/28LOSE") == 0);
    {
        size_t avi = runner.special_live ? runner.special.avi_size
                                         : runner.assets.avi_size;
        CHECK(avi > 0);
    }
    stage_runner_free(&runner);

    if (opened2) {
        CHECK(stage_runner_begin(&runner, &disc1, 1, &cat, "/25GENRM") == 0);
        CHECK(runner.state == STAGE_RS_NEED_DISC);
        CHECK(stage_runner_resume(&runner, &disc2, 2) == 0);
        {
            saturn_pad pad = {0};
            int f;
            for (f = 0; f < 5000 && !stage_runner_done(&runner); f++) {
                saturn_pad_update_host(&pad, MB_PAD_RIGHT);
                stage_runner_step(&runner, &pad);
            }
        }
        CHECK(runner.state == STAGE_RS_FINISHED);
        stage_runner_free(&runner);

        CHECK(run_smoke(&runner, &disc2, 2, &cat, "/27FINALE") == 0);
        stage_runner_free(&runner);
    }

    /* Walk campaign next chain. */
    {
        const stage_info *s = cat.campaign[0];
        unsigned n = 0;
        while (s) {
            n++;
            s = stage_progress_skip_empty(&cat, stage_catalog_next(&cat, s->iso_dir));
        }
        CHECK(n >= 20); /* 24 minus empties still advances */
        printf("campaign chain length from RUNWAY (skip empty)=%u total=%u\n",
               n, cat.campaign_count);
    }

    /* Count kinds present. */
    {
        int rhythm = 0, special = 0;
        for (i = 0; i < cat.count; i++) {
            if (cat.stages[i].kind == STAGE_KIND_RHYTHM) rhythm++;
            if (cat.stages[i].kind == STAGE_KIND_GENROOM ||
                cat.stages[i].kind == STAGE_KIND_BIGBONES ||
                cat.stages[i].kind == STAGE_KIND_FINALE)
                special++;
        }
        CHECK(rhythm == 2);
        CHECK(special >= 2);
        printf("kinds: rhythm=%d special=%d\n", rhythm, special);
    }

    cd_close(&disc1);
    if (opened2) cd_close(&disc2);
    printf("OK stage_progress\n");
    return 0;
}
