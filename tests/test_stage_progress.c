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

/*
 * Disc-less part: a hand-built catalog with the 24 campaign dirs in
 * DirNameTable order (TROLL1/TROLL2 EMPTY, 18RIDE the first disc-2 dir) plus
 * 28LOSE. Checks stage_progress_skip_empty, the unlock bytes and level word
 * after every win, and that the bytes stage_progress does not model survive
 * every win, the disc-1 → disc-2 boundary (VORTEX → RIDE) and save/load.
 */
static const char *const k_leaves[24] = {
    "04RUNWAY", "05MAUSO",  "06VALLEY", "07GUITAR", "08BAT1",   "09BAT2",
    "10BAT3",   "11LOGS",   "12TROLL1", "13TROLL2", "14DRUM",   "15GLASS",
    "16BIGBNS", "17VORTEX", "18RIDE",   "19VILLAGE", "20ICE1",  "21ICE2",
    "22SHADOW", "23MOAT",   "24STAING", "25GENRM",  "26LAFF",   "27FINALE"};

static void synth_catalog(stage_catalog *cat) {
    unsigned i;
    memset(cat, 0, sizeof *cat);
    for (i = 0; i < 24u; i++) {
        stage_info *s = &cat->stages[cat->count++];
        snprintf(s->iso_dir, sizeof s->iso_dir, "/%s", k_leaves[i]);
        snprintf(s->name, sizeof s->name, "%s", k_leaves[i] + 2);
        s->kind = (i == 8u || i == 9u) ? STAGE_KIND_EMPTY
                : (i == 23u)           ? STAGE_KIND_FINALE
                                       : STAGE_KIND_SIDE;
        s->in_campaign = 1;
        s->campaign_index = (int)i;
        s->preferred_disc = (i < 14u) ? 1 : 2;
        s->playable = 1;
        cat->campaign[cat->campaign_count++] = s;
    }
    {
        stage_info *s = &cat->stages[cat->count++];
        snprintf(s->iso_dir, sizeof s->iso_dir, "%s", "/28LOSE");
        snprintf(s->name, sizeof s->name, "%s", "LOSE");
        s->kind = STAGE_KIND_LOSE;
        s->campaign_index = -1;
        s->preferred_disc = 1;
    }
}

static int markers_intact(const mb_nvm_shadow *nvm) {
    return nvm->bytes[0] == 0x11 && nvm->bytes[2] == 0x33 && nvm->bytes[3] == 0x44 &&
           mb_nvm_get_u32(nvm, MB_NVM_DEFAULT_EXTRA_OFF) == 0xAABBCCDDu &&
           mb_nvm_get_u32(nvm, MB_NVM_TAG_OFF) == MB_NVM_TAG_VALUE;
}

static int synthetic_chain(void) {
    const char *save = "test_save_prog_synth";
    stage_catalog cat;
    stage_progress prog;
    mb_nvm_shadow nvm, disk;
    unsigned i, played = 0, crossed = 0;
#ifdef _WIN32
    _mkdir(save);
#else
    mkdir(save, 0755);
#endif
    synth_catalog(&cat);
    CHECK(cat.campaign_count == 24u);

    /* skip_empty: LOGS → DRUM over both TROLLs; NULL and end stay NULL. */
    {
        const stage_info *logs = stage_catalog_find(&cat, "/11LOGS");
        const stage_info *drum = stage_catalog_find(&cat, "/14DRUM");
        const stage_info *s;
        unsigned n = 0;
        CHECK(logs && drum);
        CHECK(stage_progress_skip_empty(&cat, stage_catalog_next(&cat, logs->iso_dir)) == drum);
        CHECK(stage_progress_skip_empty(&cat, stage_catalog_find(&cat, "/12TROLL1")) == drum);
        CHECK(stage_progress_skip_empty(&cat, stage_catalog_find(&cat, "/13TROLL2")) == drum);
        CHECK(stage_progress_skip_empty(&cat, drum) == drum);
        CHECK(stage_progress_skip_empty(&cat, NULL) == NULL);
        CHECK(stage_progress_skip_empty(&cat, stage_catalog_next(&cat, "/27FINALE")) == NULL);
        for (s = cat.campaign[0]; s; s = stage_progress_skip_empty(&cat, stage_catalog_next(&cat, s->iso_dir)))
            n++;
        CHECK(n == 22u);
    }

    mb_nvm_init_default(&nvm);
    nvm.bytes[0] = 0x11; nvm.bytes[2] = 0x33; nvm.bytes[3] = 0x44;
    mb_nvm_set_u32(&nvm, MB_NVM_DEFAULT_EXTRA_OFF, 0xAABBCCDDu);
    stage_progress_init(&prog, &cat, &nvm, save);

    /* begin on an EMPTY dir lands on the next playable; begin(NULL) on RUNWAY. */
    CHECK(stage_progress_begin(&prog, "/12TROLL1") == 0);
    CHECK(strcmp(prog.next_iso, "/14DRUM") == 0 && prog.current_campaign == 10);
    CHECK(stage_progress_begin(&prog, NULL) == 0);
    CHECK(strcmp(prog.next_iso, "/04RUNWAY") == 0 && prog.route == STAGE_ROUTE_STAGE);

    /* begin(NULL) follows the level word like LoadFirstLevel: a directory ID
     * 04..27 starts there (an EMPTY one moves on); 3 (MENU), 0, 28LOSE and
     * garbage fall back to the first unlocked stage. */
    {
        static const struct { uint32_t id; const char *iso; } cases[] = {
            {14u, "/14DRUM"}, {12u, "/14DRUM"}, {0x1bu, "/27FINALE"}, {4u, "/04RUNWAY"},
            {3u, "/04RUNWAY"}, {0u, "/04RUNWAY"}, {0x1cu, "/04RUNWAY"}, {0xffffffffu, "/04RUNWAY"}};
        for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            mb_nvm_set_u32(&nvm, MB_NVM_DEFAULT_LEVEL_OFF, cases[i].id);
            CHECK(stage_progress_begin(&prog, NULL) == 0);
            CHECK(strcmp(prog.next_iso, cases[i].iso) == 0);
        }
        mb_nvm_set_u32(&nvm, MB_NVM_DEFAULT_LEVEL_OFF, MB_NVM_MENU_DIR_ID);
        CHECK(stage_progress_begin(&prog, NULL) == 0);
        CHECK(strcmp(prog.next_iso, "/04RUNWAY") == 0);
    }
    CHECK(mb_nvm_get_u8(&nvm, MB_NVM_COMPLETED_OFF) == 0);

    while (prog.route == STAGE_ROUTE_STAGE) {
        const stage_info *cur = stage_catalog_find(&cat, prog.next_iso);
        CHECK(cur && cur->kind != STAGE_KIND_EMPTY);
        played++;
        CHECK(stage_progress_on_outcome(&prog, cur->iso_dir, STAGE_OUTCOME_WIN) == 0);
        CHECK(mb_nvm_level_unlocked(&nvm, (unsigned)cur->campaign_index) == 1);
        CHECK(markers_intact(&nvm));
        CHECK(mb_nvm_get_u8(&nvm, MB_NVM_COMPLETED_OFF) ==
              (cur->kind == STAGE_KIND_FINALE ? 1 : 0));
        if (prog.route == STAGE_ROUTE_STAGE) {
            const stage_info *nx = stage_catalog_find(&cat, prog.next_iso);
            CHECK(nx && nx->kind != STAGE_KIND_EMPTY);
            CHECK(mb_nvm_level_unlocked(&nvm, (unsigned)nx->campaign_index) == 1);
            CHECK(mb_nvm_get_u32(&nvm, MB_NVM_DEFAULT_LEVEL_OFF) == MB_NVM_LEVEL_ID(nx->campaign_index));
            if (strcmp(cur->iso_dir, "/17VORTEX") == 0) {
                /* Disc 1 → disc 2: the file written on the last disc-1 win is
                 * byte for byte what RIDE starts from. */
                CHECK(strcmp(nx->iso_dir, "/18RIDE") == 0);
                CHECK(cur->preferred_disc == 1 && nx->preferred_disc == 2);
                CHECK(mb_nvm_load(&disk, save) == 0 && disk.loaded == 1);
                CHECK(memcmp(disk.bytes, nvm.bytes, MB_NVM_SHADOW_SIZE) == 0);
                CHECK(markers_intact(&disk));
                crossed = 1;
            }
        }
    }
    CHECK(crossed == 1);
    CHECK(played == 22u);
    CHECK(prog.route == STAGE_ROUTE_COMPLETE && prog.next_iso[0] == 0);
    for (i = 0; i < 24u; i++)
        CHECK(mb_nvm_level_unlocked(&nvm, i) == ((i != 8u && i != 9u) ? 1 : 0));
    CHECK(mb_nvm_get_u32(&nvm, MB_NVM_DEFAULT_LEVEL_OFF) == MB_NVM_MENU_DIR_ID);
    CHECK(mb_nvm_load(&disk, save) == 0);
    CHECK(memcmp(disk.bytes, nvm.bytes, MB_NVM_SHADOW_SIZE) == 0);
    CHECK(mb_nvm_get_u8(&disk, MB_NVM_COMPLETED_OFF) == 1); /* persisted with FINALE */

    /* Lose and abort route to LOSE, LOSE routes to MENU; none of them writes nvm. */
    {
        mb_nvm_shadow before = nvm;
        CHECK(stage_progress_on_outcome(&prog, "/05MAUSO", STAGE_OUTCOME_LOSE) == 0);
        CHECK(prog.route == STAGE_ROUTE_LOSE && strcmp(prog.next_iso, "/28LOSE") == 0);
        CHECK(stage_progress_on_outcome(&prog, "/05MAUSO", STAGE_OUTCOME_ABORT) == 0);
        CHECK(prog.route == STAGE_ROUTE_LOSE && strcmp(prog.next_iso, "/28LOSE") == 0);
        CHECK(stage_progress_on_outcome(&prog, "/28LOSE", STAGE_OUTCOME_LOSE) == 0);
        CHECK(prog.route == STAGE_ROUTE_MENU && prog.next_iso[0] == 0);
        CHECK(stage_progress_on_outcome(&prog, "/05MAUSO", STAGE_OUTCOME_NONE) == -1);
        CHECK(prog.route == STAGE_ROUTE_MENU);
        CHECK(stage_progress_on_outcome(&prog, "/99NOPE", STAGE_OUTCOME_WIN) == -1);
        CHECK(memcmp(before.bytes, nvm.bytes, MB_NVM_SHADOW_SIZE) == 0);
    }
    printf("synthetic chain: 22 wins, TROLL skipped, nvm markers intact across VORTEX->RIDE\n");
    return 0;
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

    if (synthetic_chain()) return 1;

    if (!d1 || !man) {
        printf("SKIP stage_progress disc part\n");
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

    /* /28LOSE finishing its own playback (a WIN outcome for that movie-only
     * stage) is not a campaign win: it must route straight to MENU, like the
     * abort case above, never STAGE_ROUTE_COMPLETE (that would let the host
     * show its credits roll after a loss, which the original never does:
     * docs/fmv-routes.md section 3, result 1 = LOSE movie then 03MENU). */
    {
        mb_nvm_shadow before = nvm;
        CHECK(stage_progress_on_outcome(&prog, "/28LOSE", STAGE_OUTCOME_WIN) == 0);
        CHECK(prog.route == STAGE_ROUTE_MENU);
        CHECK(prog.next_iso[0] == 0);
        CHECK(memcmp(before.bytes, nvm.bytes, MB_NVM_SHADOW_SIZE) == 0); /* not in_campaign: untouched */
    }

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
        /* Disc switch on the real catalog: RIDE is the first dir that needs
         * disc 2, and the nvm image is not touched by NEED_DISC → resume. */
        mb_nvm_shadow before = nvm;
        const stage_info *ride = stage_catalog_find(&cat, "/18RIDE");
        CHECK(ride && ride->preferred_disc == 2);
        CHECK(stage_catalog_find(&cat, "/17VORTEX")->preferred_disc == 1);
        CHECK(stage_progress_skip_empty(&cat, stage_catalog_next(&cat, "/17VORTEX")) == ride);
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
        CHECK(memcmp(before.bytes, nvm.bytes, MB_NVM_SHADOW_SIZE) == 0);
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
