#include "core/cdimage.h"
#include "core/identity.h"
#include "core/menu_phase.h"
#include "core/saturn_pad.h"
#include "core/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void) {
    const char *disc = getenv("MB_DISC1");
    const char *man = getenv("MB_MANIFEST");
    const char *save = "test_save_menu";
    char err[512];
    cdimage cd;
    id_manifest manifest;
    id_result result;
    menu_phase menu;
    saturn_pad pad = {0};
    unsigned i;
    int found_runway = 0;

    if (!disc || !man) {
        printf("SKIP menu_phase (set MB_DISC1 and MB_MANIFEST)\n");
        return 0;
    }
    CHECK(manifest_load(&manifest, man, err, sizeof err) == 0);
    CHECK(cd_open(&cd, disc, err, sizeof err) == 0);
    CHECK(identity_check(&manifest, &cd, 1, ID_QUICK, &result));
    manifest_free(&manifest);
    {
        const char *bad_path = getenv("MB_DISC1_CORRUPT_INTRO");
        if (bad_path) {
            cdimage bad;
            CHECK(cd_open(&bad, bad_path, err, sizeof err) == 0);
            CHECK(menu_phase_load(&menu, &bad, save) != 0);
            CHECK(strstr(menu.status, "INTRO/20AVI") != NULL);
            CHECK(strstr(menu.status, "not RIFF/AVI") != NULL);
            menu_phase_free(&menu);
            cd_close(&bad);
        }
    }

    CHECK(menu_phase_load(&menu, &cd, save) == 0);
    /* §16: MENU 04STXT soft-load (gfx_count may be 0 if decode fails). */
    CHECK(menu.stxt_size > 0);
    CHECK(menu.frame_rgba != NULL);
    CHECK(menu.original_menu_rgba != NULL);
    for (i = 0; i < 4; i++) CHECK(menu.original_items[i] != NULL);
    CHECK(menu.frame_rgba);
    CHECK(menu.screen == MENU_SCR_TITLE);
    CHECK(menu.level_count > 0);
    /* The original LevelSelect: 13 rows, IDs 04..10 and 18..23, nothing else. */
    CHECK(menu.level_count == 13u);
    for (i = 0; i < menu.level_count; i++) {
        CHECK(menu_phase_level_selectable(menu.levels[i].dir_id));
        CHECK(strcmp(menu.levels[i].name, "LOGS") != 0 && strcmp(menu.levels[i].name, "FINALE") != 0);
        CHECK(menu.levels[i].unlocked == (menu.levels[i].dir_id == 4));
    }
    CHECK(!menu_phase_level_selectable(11) && !menu_phase_level_selectable(17) &&
          !menu_phase_level_selectable(27) && !menu_phase_level_selectable(3));
    /* The observed boot video is /29INTRO, not the longer /03MENU cinematic. */
    CHECK(menu.titl_size > 0);
    CHECK(menu.avi_size > 0);
#ifdef _WIN32
    CHECK(menu.title_avi_ok);
#endif
    if (menu.title_avi_ok) {
        sha256_ctx hash;
        uint8_t digest[32];
        char hex[65];
        CHECK(menu.avi_size == 7718942u);
        CHECK(menu.title_avi.frame_count == 764u);
        CHECK(menu.title_avi.pcm_rate == 44100 && menu.title_avi.pcm_channels == 2);
        CHECK(menu.title_avi.pcm_samples > 0);
        sha256_init(&hash);
        sha256_update(&hash, menu.title_avi.pcm, menu.title_avi.pcm_samples * sizeof(int16_t));
        sha256_final(&hash, digest);
        sha256_hex(digest, hex);
        CHECK(strcmp(hex, "c2b10269f23db928a9f60ed9d5387408aec013455380c9292e3ac6edc3e66461") == 0);
        CHECK(duck_avi_frame_rgba(&menu.title_avi) != NULL);
        printf("title AVI: frames=%u size=%zu titl=%zu duck=%zu\n",
               menu.title_avi.frame_count, menu.avi_size, menu.titl_size,
               menu.duck_size);
    } else {
        printf("title AVI soft-fail (size=%zu) — STXT/TITL still OK\n",
               menu.avi_size);
    }
    for (i = 0; i < menu.level_count; i++) {
        if (strcmp(menu.levels[i].name, "RUNWAY") == 0) {
            found_runway = 1;
            CHECK(menu.levels[i].unlocked == 1);
            CHECK(i == 0); /* RUNWAY sorted first */
        }
    }
    CHECK(found_runway);

    /* Title → Start via Start button. */
    saturn_pad_update_host(&pad, MB_PAD_START);
    menu_phase_step(&menu, &pad);
    CHECK(menu.screen == MENU_SCR_START);
    CHECK(menu.movie_pcm_pending == 0);
    if (getenv("MB_MENU_FRAME_OUT")) {
        FILE *out = fopen(getenv("MB_MENU_FRAME_OUT"), "wb");
        CHECK(out != NULL);
        CHECK(fwrite(menu.frame_rgba, 1, 320u * 224u * 4u, out) == 320u * 224u * 4u);
        CHECK(fclose(out) == 0);
    }
    {
        uint8_t first[320u * 224u * 4u];
        memcpy(first, menu.frame_rgba, sizeof first);
        saturn_pad_update_host(&pad, 0);
        menu_phase_step(&menu, &pad);
        saturn_pad_update_host(&pad, MB_PAD_DOWN);
        menu_phase_step(&menu, &pad);
        CHECK(menu.cursor == 1);
        CHECK(memcmp(first, menu.frame_rgba, sizeof first) != 0);
        saturn_pad_update_host(&pad, 0);
        menu_phase_step(&menu, &pad);
        saturn_pad_update_host(&pad, MB_PAD_UP);
        menu_phase_step(&menu, &pad);
        CHECK(menu.cursor == 0);
        CHECK(memcmp(first, menu.frame_rgba, sizeof first) == 0);
    }
    saturn_pad_update_host(&pad, 0);
    menu_phase_step(&menu, &pad);

    /* Cursor down to Options, activate. */
    saturn_pad_update_host(&pad, MB_PAD_DOWN);
    menu_phase_step(&menu, &pad);
    CHECK(menu.cursor == 1);
    saturn_pad_update_host(&pad, 0);
    menu_phase_step(&menu, &pad);
    saturn_pad_update_host(&pad, MB_PAD_A);
    menu_phase_step(&menu, &pad);
    CHECK(menu.screen == MENU_SCR_OPTIONS);
    saturn_pad_update_host(&pad, 0);
    menu_phase_step(&menu, &pad);

    /* Toggle music, back. */
    saturn_pad_update_host(&pad, MB_PAD_A);
    menu_phase_step(&menu, &pad);
    CHECK(menu.music_on == 0);
    saturn_pad_update_host(&pad, 0);
    menu_phase_step(&menu, &pad);
    saturn_pad_update_host(&pad, MB_PAD_DOWN);
    menu_phase_step(&menu, &pad);
    saturn_pad_update_host(&pad, 0);
    menu_phase_step(&menu, &pad);
    /* Case 5 is STEREO, OFF at boot as in the original. */
    CHECK(menu.cursor == 1 && menu.stereo_on == 0);
    saturn_pad_update_host(&pad, MB_PAD_A);
    menu_phase_step(&menu, &pad);
    CHECK(menu.stereo_on == 1 && strstr(menu.status, "stereo=1"));
    saturn_pad_update_host(&pad, 0);
    menu_phase_step(&menu, &pad);
    saturn_pad_update_host(&pad, MB_PAD_DOWN);
    menu_phase_step(&menu, &pad);
    saturn_pad_update_host(&pad, 0);
    menu_phase_step(&menu, &pad);
    CHECK(menu.cursor == 2 && menu.volume == 15);
    saturn_pad_update_host(&pad, MB_PAD_LEFT);
    menu_phase_step(&menu, &pad);
    CHECK(menu.cursor == 2 && menu.volume == 14);
    saturn_pad_update_host(&pad, 0);
    menu_phase_step(&menu, &pad);
    saturn_pad_update_host(&pad, MB_PAD_DOWN);
    menu_phase_step(&menu, &pad);
    saturn_pad_update_host(&pad, 0);
    menu_phase_step(&menu, &pad);
    saturn_pad_update_host(&pad, MB_PAD_A); /* Back */
    menu_phase_step(&menu, &pad);
    CHECK(menu.screen == MENU_SCR_START);

    /* Start Game → RUNWAY. */
    menu.cursor = 0;
    saturn_pad_update_host(&pad, 0);
    menu_phase_step(&menu, &pad);
    saturn_pad_update_host(&pad, MB_PAD_START);
    menu_phase_step(&menu, &pad);
    CHECK(menu.screen == MENU_SCR_STORY && menu.state == MENU_ST_ACTIVE);
    CHECK(menu.title_avi_ok && menu.title_avi.frame_count > 2048);
    CHECK(menu.movie_pcm_pending && menu.title_avi.pcm_samples > 0);
    saturn_pad_update_host(&pad, 0);
    menu_phase_step(&menu, &pad);
    saturn_pad_update_host(&pad, MB_PAD_START);
    menu_phase_step(&menu, &pad);
    CHECK(menu.state == MENU_ST_START_LEVEL && !menu.movie_pcm_pending);
    CHECK(menu_phase_selected_iso(&menu));
    CHECK(strstr(menu_phase_selected_iso(&menu), "RUNWAY") != NULL);

    menu_phase_free(&menu);
    CHECK(menu_phase_load(&menu, &cd, save) == 0);
    menu_phase_show_start(&menu);
    memset(&pad, 0, sizeof pad);
    saturn_pad_update_host(&pad, MB_PAD_START);
    CHECK(menu_phase_step(&menu, &pad));
    CHECK(menu.screen == MENU_SCR_STORY);
    saturn_pad_update_host(&pad, 0);
    menu.title_avi.tick_external = 1;
    menu.title_avi.tick_us = 1000000;
    for (i = 0; i < 205; i++) {
        CHECK(menu_phase_step(&menu, &pad));
        CHECK(menu.state == MENU_ST_ACTIVE);
    }
    CHECK(menu_phase_step(&menu, &pad));
    CHECK(menu.state == MENU_ST_START_LEVEL && !menu.movie_pcm_pending);
    CHECK(menu_phase_selected_iso(&menu) && strstr(menu_phase_selected_iso(&menu), "RUNWAY"));
    menu_phase_free(&menu);
    CHECK(menu_phase_load(&menu, &cd, save) == 0);
    CHECK(menu.title_avi_ok && menu.title_avi.us_per_frame > 0);
    memset(&pad, 0, sizeof pad);
    menu.title_avi.tick_external = 1;
    menu.title_avi.tick_us = 1000000;
    for (i = 0; i < 50; i++) CHECK(menu_phase_step(&menu, &pad));
    CHECK(menu.screen == MENU_SCR_TITLE && !duck_avi_playback_done(&menu.title_avi));
    CHECK(menu_phase_step(&menu, &pad));
    CHECK(menu.screen == MENU_SCR_START && duck_avi_playback_done(&menu.title_avi));
    CHECK(menu.movie_pcm_pending == 0 && menu.original_menu_rgba != NULL);
    menu_phase_free(&menu);
#ifdef _WIN32
    {
        static const uint8_t invalid_frame[] = {0, 0, 0};
        CHECK(menu_phase_load(&menu, &cd, save) == 0);
        memset(&pad, 0, sizeof pad);
        CHECK(menu.title_avi.cur < menu.title_avi.frame_count);
        menu.title_avi.frames[menu.title_avi.cur].data = invalid_frame;
        menu.title_avi.frames[menu.title_avi.cur].size = sizeof invalid_frame;
        menu.title_avi.tick_external = 1;
        menu.title_avi.tick_us = menu.title_avi.us_per_frame + 1u;
        CHECK(menu_phase_step(&menu, &pad));
        CHECK(menu.state == MENU_ST_QUIT && menu.title_avi.decode_error);
        CHECK(strstr(menu.status, "invalid video frame") != NULL);
        CHECK(menu.frame_rgba[0] == 0 && menu.frame_rgba[1] == 0 && menu.frame_rgba[2] == 0);
        menu_phase_free(&menu);
    }
#endif
    cd_close(&cd);
    printf("OK menu_phase\n");
    return 0;
}
