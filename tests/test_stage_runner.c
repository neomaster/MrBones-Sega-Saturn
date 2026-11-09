#include "core/cdimage.h"
#include "core/identity.h"
#include "core/saturn_pad.h"
#include "core/sha256.h"
#include "core/stage_catalog.h"
#include "core/stage_runner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        fprintf(stderr, "identity disc%d: %s\n", disc_no, result.message);
        cd_close(cd);
        manifest_free(&manifest);
        return -1;
    }
    manifest_free(&manifest);
    return 0;
}

int main(void) {
    const char *d1 = getenv("MB_DISC1");
    const char *d2 = getenv("MB_DISC2");
    const char *man = getenv("MB_MANIFEST");
    cdimage disc1, disc2;
    stage_catalog cat;
    stage_runner runner;
    saturn_pad pad = {0};
    const stage_info *ride;
    int i, opened2 = 0;

    if (!d1 || !man) {
        printf("SKIP stage_runner (set MB_DISC1 and MB_MANIFEST)\n");
        return 0;
    }
    CHECK(open_disc(&disc1, d1, man, 1) == 0);
    if (d2 && open_disc(&disc2, d2, man, 2) == 0)
        opened2 = 1;

    CHECK(stage_catalog_build(&cat, &disc1, opened2 ? &disc2 : NULL) == 0);
    {
        const char *bad_path = getenv("MB_DISC2_CORRUPT_FINALE");
        if (bad_path) {
            cdimage bad;
            char err[512];
            CHECK(cd_open(&bad, bad_path, err, sizeof err) == 0);
            CHECK(stage_runner_begin(&runner, &bad, 2, &cat, "/27FINALE") != 0);
            CHECK(runner.state == STAGE_RS_FAILED && !runner.special_live);
            CHECK(strstr(runner.status, "not RIFF/AVI") != NULL);
            stage_runner_free(&runner);
            cd_close(&bad);
        }
    }
    {
        const char *bad_path = getenv("MB_DISC1_CORRUPT_LOSE");
        if (bad_path) {
            cdimage bad;
            char err[512];
            CHECK(cd_open(&bad, bad_path, err, sizeof err) == 0);
            CHECK(stage_runner_begin(&runner, &bad, 1, &cat, "/28LOSE") != 0);
            CHECK(runner.state == STAGE_RS_FAILED && !runner.special_live);
            CHECK(strstr(runner.status, "load failed") != NULL);
            stage_runner_free(&runner);
            cd_close(&bad);
        }
    }

    CHECK(stage_runner_begin_movie(&runner, &disc1, 1, "/99MISSING") != 0);
    CHECK(runner.state == STAGE_RS_FAILED && strstr(runner.status, "missing /99MISSING/20AVI"));
    stage_runner_free(&runner);

    /* Disc1 RUNWAY → full runway_phase path. */
    /* Start skips a transition FMV and completes the runner. */
    CHECK(stage_runner_begin_movie(&runner, &disc1, 1, "/07GUITAR") == 0);
    CHECK(runner.special_live && runner.special.movie_ok && runner.special.movie.pcm_samples > 0);
    runner.special.movie.tick_external = 1;
    runner.special.movie.tick_us = runner.special.movie.us_per_frame + 1u;
    for (i = 0; i < 99; i++) CHECK(stage_runner_step(&runner, &pad) == 1);
    {
        sha256_ctx hash;
        uint8_t digest[32];
        char hex[65];
        CHECK(runner.special.movie.frames_drawn == 100);
        sha256_init(&hash);
        sha256_update(&hash, stage_runner_frame(&runner), 320u * 224u * 4u);
        sha256_final(&hash, digest);
        sha256_hex(digest, hex);
        /* Ymir F12, GUITAR selected through the loader debugger. */
        CHECK(strcmp(hex, "c8d30f83e58d728e403dce6718cfb28ad710a88d9ff6870af1ca42acdb8d0ec8") == 0);
    }
    saturn_pad_update_host(&pad, MB_PAD_START);
    CHECK(stage_runner_step(&runner, &pad) == 1);
    CHECK(stage_runner_done(&runner) && runner.outcome == STAGE_OUTCOME_WIN);
    stage_runner_free(&runner);
    memset(&pad, 0, sizeof pad);

    CHECK(stage_runner_begin(&runner, &disc1, 1, &cat, "/04RUNWAY") == 0);
    CHECK(runner.state == STAGE_RS_PLAYING);
    CHECK(runner.runway_live == 1);
    for (i = 0; i < 5000 && !stage_runner_done(&runner); i++) {
        saturn_pad_update_host(&pad, MB_PAD_RIGHT);
        stage_runner_step(&runner, &pad);
    }
    CHECK(runner.state == STAGE_RS_FINISHED);
    printf("runner RUNWAY: frames=%u status=%s\n", runner.frames, runner.status);
    stage_runner_free(&runner);

    /* Disc1 side-scroll → §17 sidescrl_phase. */
    CHECK(stage_runner_begin(&runner, &disc1, 1, &cat, "/05MAUSO") == 0);
    CHECK(runner.state == STAGE_RS_PLAYING);
    CHECK(runner.runway_live == 0);
    CHECK(runner.sidescrl_live == 1);
    CHECK(runner.playable == 1);
    for (i = 0; i < 5000 && !stage_runner_done(&runner); i++) {
        saturn_pad_update_host(&pad, MB_PAD_RIGHT);
        stage_runner_step(&runner, &pad);
    }
    CHECK(runner.state == STAGE_RS_FINISHED);
    printf("runner MAUSO: frames=%u %s\n", runner.frames, runner.status);
    stage_runner_free(&runner);

    /* Disc1 LOGS → §16 runway_phase playable (15PAN). */
    CHECK(stage_runner_begin(&runner, &disc1, 1, &cat, "/11LOGS") == 0);
    CHECK(runner.state == STAGE_RS_PLAYING);
    CHECK(runner.runway_live == 1);
    CHECK(runner.playable == 1);
    for (i = 0; i < 5000 && !stage_runner_done(&runner); i++) {
        saturn_pad_update_host(&pad, MB_PAD_RIGHT);
        stage_runner_step(&runner, &pad);
    }
    CHECK(runner.state == STAGE_RS_FINISHED);
    printf("runner LOGS: frames=%u status=%s\n", runner.frames, runner.status);
    stage_runner_free(&runner);

    if (opened2) {
        ride = stage_catalog_find_name(&cat, "RIDE");
        CHECK(ride);
        /* Mounted disc1 but RIDE prefers disc2 → NEED_DISC (INSERTCD stand-in). */
        CHECK(stage_runner_begin(&runner, &disc1, 1, &cat, ride->iso_dir) == 0);
        CHECK(runner.state == STAGE_RS_NEED_DISC);
        CHECK(runner.need_disc == 2);
        CHECK(runner.playable == 1);
        printf("runner RIDE NEED_DISC: %s\n", runner.status);
        CHECK(stage_runner_resume(&runner, &disc2, 2) == 0);
        CHECK(runner.state == STAGE_RS_PLAYING);
        CHECK(runner.runway_live == 1); /* §16: pan playable after swap */
        for (i = 0; i < 5000 && !stage_runner_done(&runner); i++) {
            saturn_pad_update_host(&pad, MB_PAD_RIGHT);
            stage_runner_step(&runner, &pad);
        }
        CHECK(runner.state == STAGE_RS_FINISHED);
        printf("runner RIDE after swap: frames=%u %s\n", runner.frames, runner.status);
        stage_runner_free(&runner);

        /* Direct disc2 LAFF → §17 sidescrl_phase. */
        CHECK(stage_runner_begin(&runner, &disc2, 2, &cat, "/26LAFF") == 0);
        CHECK(runner.state == STAGE_RS_PLAYING);
        CHECK(runner.sidescrl_live == 1);
        for (i = 0; i < 5000 && !stage_runner_done(&runner); i++) {
            saturn_pad_update_host(&pad, MB_PAD_RIGHT);
            stage_runner_step(&runner, &pad);
        }
        CHECK(runner.state == STAGE_RS_FINISHED);
        printf("runner LAFF disc2: frames=%u %s\n", runner.frames, runner.status);
        stage_runner_free(&runner);
    }

    cd_close(&disc1);
    if (opened2) cd_close(&disc2);
    printf("OK stage_runner multi-phase\n");
    return 0;
}
