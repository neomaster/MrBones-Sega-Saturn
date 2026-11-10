#include "platform/host_audio.h"
#include "core/menu_phase.h"
#include "core/special_phase.h"
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_opening_sync(void) {
    const char *path = getenv("MB_DISC1");
    cdimage cd;
    menu_phase menu;
    saturn_pad pad = {0};
    host_audio *audio = NULL;
    char err[512];
    unsigned position = 0, last = 0;
    int state = 0, result = 1;
    ULONGLONG deadline;
    if (!path) { puts("SKIP opening sync: MB_DISC1 unset"); return 0; }
    if (cd_open(&cd, path, err, sizeof err)) { printf("disc: %s\n", err); return 1; }
    if (menu_phase_load(&menu, &cd, "test_audio_reference")) {
        cd_close(&cd);
        return 1;
    }
    if (!menu.title_avi_ok || !menu.title_avi.pcm || !menu.title_avi.pcm_samples) goto done;
    audio = host_audio_start_pcm16(menu.title_avi.pcm, menu.title_avi.pcm_samples,
                                   menu.title_avi.pcm_rate, menu.title_avi.pcm_channels);
    if (!audio) goto done;
    menu.movie_pcm_pending = 0;
    menu.title_avi.tick_external = 1;
    deadline = GetTickCount64() + 65000;
    {
        ULONGLONG previous_tick = GetTickCount64();
        while (GetTickCount64() < deadline && menu.screen == MENU_SCR_TITLE) {
            ULONGLONG now = GetTickCount64();
            unsigned elapsed_us = (unsigned)(now - previous_tick) * 1000u;
            unsigned expected;
            previous_tick = now;
            state = host_audio_movie_clock(audio, &position);
            if (state < 0) { puts("SKIP opening sync: audio device unavailable"); result = 0; goto done; }
            if (state == 0) elapsed_us = 0;
            else if (state == 1 || (state == 2 && position > last)) {
                if (position < last) {
                    printf("opening audio clock moved backwards: %u -> %u\n", last, position);
                    goto done;
                }
                elapsed_us = (position - last) * 1000u;
                last = position;
            }
            menu.title_avi.tick_us = elapsed_us;
            if (!menu_phase_step(&menu, &pad) || menu.title_avi.decode_error) goto done;
            if (state == 1) {
                expected = 1u + (unsigned)((uint64_t)position * 1000u * menu.title_avi.frame_rate / ((uint64_t)menu.title_avi.frame_scale * 1000000u));
                if (expected > menu.title_avi.frame_count) expected = menu.title_avi.frame_count;
                if (menu.title_avi.cur != expected) {
                    printf("opening sync: audio=%u ms frame=%u expected=%u\n",
                           position, menu.title_avi.cur, expected);
                    goto done;
                }
            }
            Sleep(10);
        }
    }
    if (menu.screen != MENU_SCR_START || state != 2 ||
        menu.title_avi.cur != menu.title_avi.frame_count || menu.movie_pcm_pending) {
        printf("opening completion: screen=%d audio=%d position=%u frame=%u/%u\n",
               menu.screen, state, position, menu.title_avi.cur, menu.title_avi.frame_count);
        goto done;
    }
    printf("OK opening audio clock: %u ms, %u frames, menu reached\n", position, menu.title_avi.cur);
    result = 0;
done:
    host_audio_stop(audio);
    menu_phase_free(&menu);
    cd_close(&cd);
    return result;
}

static int test_lose_sync(void) {
    const char *path = getenv("MB_DISC1");
    cdimage cd;
    special_phase phase;
    saturn_pad pad = {0};
    host_audio *audio = NULL;
    char err[512];
    unsigned position = 0, last = 0;
    int state, result = 1;
    ULONGLONG deadline;
    if (!path) { puts("SKIP LOSE sync: MB_DISC1 unset"); return 0; }
    if (cd_open(&cd, path, err, sizeof err)) { printf("disc: %s\n", err); return 1; }
    if (special_phase_load(&phase, &cd, "/28LOSE", STAGE_KIND_LOSE)) {
        cd_close(&cd);
        return 1;
    }
    if (!phase.movie_ok || !phase.movie.pcm || !phase.movie.pcm_samples) goto done;
    audio = host_audio_start_pcm16(phase.movie.pcm, phase.movie.pcm_samples,
                                   phase.movie.pcm_rate, phase.movie.pcm_channels);
    if (!audio) goto done;
    phase.movie_pcm_pending = 0;
    phase.movie.tick_external = 1;
    deadline = GetTickCount64() + 20000;
    {
        ULONGLONG previous_tick = GetTickCount64();
        while (GetTickCount64() < deadline && phase.state == SPECIAL_ST_PLAY) {
            ULONGLONG now = GetTickCount64();
            unsigned elapsed_us = (unsigned)(now - previous_tick) * 1000u;
            unsigned expected;
            previous_tick = now;
            state = host_audio_movie_clock(audio, &position);
            if (state < 0) { puts("SKIP LOSE sync: audio device unavailable"); result = 0; goto done; }
            if (state == 0) elapsed_us = 0;
            else if (state == 1 || (state == 2 && position > last)) {
                if (position < last) {
                    printf("LOSE audio clock moved backwards: %u -> %u state=%d\n", last, position, state);
                    goto done;
                }
                elapsed_us = (position - last) * 1000u;
                last = position;
            }
            phase.movie.tick_us = elapsed_us;
            if (special_phase_step(&phase, &pad) != 0 || phase.movie.decode_error) {
                printf("LOSE step failed: state=%d audio=%u error=%s\n", phase.state, position, phase.movie.status);
                goto done;
            }
            if (state == 1) {
                expected = 1u + (unsigned)((uint64_t)position * 1000u * phase.movie.frame_rate / ((uint64_t)phase.movie.frame_scale * 1000000u));
                if (expected > phase.movie.frame_count) expected = phase.movie.frame_count;
                if (phase.movie.cur != expected) {
                    printf("LOSE sync: audio=%u ms frame=%u expected=%u\n",
                           position, phase.movie.cur, expected);
                    goto done;
                }
            }
            Sleep(10);
        }
    }
    if (phase.state != SPECIAL_ST_LOST || state != 2 || phase.movie.cur != phase.movie.frame_count) {
        printf("LOSE completion: state=%d audio=%d position=%u frame=%u/%u\n",
               phase.state, state, position, phase.movie.cur, phase.movie.frame_count);
        goto done;
    }
    printf("OK LOSE audio clock: %u ms, %u frames, scene complete\n", position, phase.movie.cur);
    result = 0;
done:
    host_audio_stop(audio);
    special_phase_free(&phase);
    cd_close(&cd);
    return result;
}

static int test_story_clock(void) {
    const char *path = getenv("MB_DISC1");
    cdimage cd;
    menu_phase menu;
    saturn_pad pad = {0};
    host_audio *audio = NULL;
    char err[512];
    unsigned position = 0, last = 0;
    int full = getenv("MB_FULL_STORY") != NULL;
    int state = 0, result = 1;
    ULONGLONG deadline, previous_tick;
    if (!path) { puts("SKIP story sync: MB_DISC1 unset"); return 0; }
    if (cd_open(&cd, path, err, sizeof err)) return 1;
    if (menu_phase_load(&menu, &cd, "test_audio_reference")) { cd_close(&cd); return 1; }
    menu_phase_show_start(&menu);
    saturn_pad_update_host(&pad, MB_PAD_START);
    if (!menu_phase_step(&menu, &pad) || menu.screen != MENU_SCR_STORY ||
        !menu.title_avi.pcm || menu.title_avi.frame_count <= 2048) goto done;
    saturn_pad_update_host(&pad, 0);
    audio = host_audio_start_pcm16(menu.title_avi.pcm, menu.title_avi.pcm_samples,
                                   menu.title_avi.pcm_rate, menu.title_avi.pcm_channels);
    if (!audio) goto done;
    menu.movie_pcm_pending = 0;
    menu.title_avi.tick_external = 1;
    deadline = GetTickCount64() + (full ? 240000u : 5000u);
    previous_tick = GetTickCount64();
    while (GetTickCount64() < deadline &&
           (full ? menu.state == MENU_ST_ACTIVE : position < 1500)) {
        ULONGLONG now = GetTickCount64();
        unsigned elapsed_us = (unsigned)(now - previous_tick) * 1000u;
        unsigned expected;
        previous_tick = now;
        state = host_audio_movie_clock(audio, &position);
        if (state < 0) { puts("SKIP story sync: audio device unavailable"); result = 0; goto done; }
        if (state == 0) elapsed_us = 0;
        else if (state == 1 || (state == 2 && position > last)) {
            if (position < last) goto done;
            elapsed_us = (position - last) * 1000u;
            last = position;
        }
        menu.title_avi.tick_us = elapsed_us;
        if (!menu_phase_step(&menu, &pad) || menu.title_avi.decode_error) goto done;
        expected = 1u + (unsigned)((uint64_t)position * menu.title_avi.frame_rate /
                                   ((uint64_t)menu.title_avi.frame_scale * 1000u));
        if (expected > menu.title_avi.frame_count) expected = menu.title_avi.frame_count;
        if (state == 1 && menu.title_avi.cur != expected) {
            printf("story sync: audio=%u ms frame=%u expected=%u\n", position,
                   menu.title_avi.cur, expected);
            goto done;
        }
        Sleep(10);
    }
    if (position < 1500 || (full && (state != 2 || menu.state != MENU_ST_START_LEVEL ||
        menu.title_avi.cur != menu.title_avi.frame_count || !menu_phase_selected_iso(&menu) ||
        strcmp(menu_phase_selected_iso(&menu), "/04RUNWAY") != 0))) {
        printf("story incomplete: audio=%d position=%u menu=%d frame=%u/%u\n",
               state, position, menu.state, menu.title_avi.cur, menu.title_avi.frame_count);
        goto done;
    }
    printf("OK %sstory audio clock: %u ms, %u/%u frames\n", full ? "full " : "",
           position, menu.title_avi.cur, menu.title_avi.frame_count);
    result = 0;
done:
    host_audio_stop(audio);
    menu_phase_free(&menu);
    cd_close(&cd);
    return result;
}

static int test_finale_clock(void) {
    const char *path = getenv("MB_DISC2");
    cdimage cd;
    special_phase phase;
    saturn_pad pad = {0};
    host_audio *audio = NULL;
    char err[512];
    unsigned position = 0, last = 0;
    int result = 1;
    int full = getenv("MB_FULL_FINALE") != NULL;
    int state = 0, start_sent = 0;
    ULONGLONG deadline;
    if (!path) { puts("SKIP finale sync: MB_DISC2 unset"); return 0; }
    if (cd_open(&cd, path, err, sizeof err)) { printf("disc2: %s\n", err); return 1; }
    if (special_phase_load(&phase, &cd, "/27FINALE", STAGE_KIND_FINALE)) {
        cd_close(&cd);
        return 1;
    }
    if (!phase.movie_ok || !phase.movie.pcm || !phase.movie.pcm_samples) goto done;
    audio = host_audio_start_pcm16(phase.movie.pcm, phase.movie.pcm_samples,
                                   phase.movie.pcm_rate, phase.movie.pcm_channels);
    if (!audio) goto done;
    phase.movie_pcm_pending = 0;
    phase.movie.tick_external = 1;
    {
        saturn_pad shots = {0};
        shots.pressed = 0x0100u;
        phase.movie.tick_us = 0;
        /* mode 1 incr @0x2b2158 is 32768: one Brain tick keeps high-16 phase. */
        phase.ring_phase_mode = 1;
        for (unsigned k = 0; k < FINALE_RING_SLOTS; k++) {
            phase.ring_phase_acc = (255 - (int)(16u * k)) * 65536;
            if (special_phase_step(&phase, &shots) || phase.state != SPECIAL_ST_PLAY ||
                phase.movie.playback_done) {
                puts("FINALE audio interrupted by ring clear");
                goto done;
            }
        }
        if (!phase.ring_cleared || phase.movie.cur != 1) goto done;
    }
    saturn_pad_update_host(&pad, MB_PAD_RIGHT);
    deadline = GetTickCount64() + (full ?
        ((uint64_t)phase.movie.frame_count * phase.movie.us_per_frame / 1000u) + 30000u : 5000u);
    ULONGLONG previous_tick = GetTickCount64();
    while (GetTickCount64() < deadline && (full ? phase.state == SPECIAL_ST_PLAY : position < 1500)) {
        unsigned expected;
        ULONGLONG now = GetTickCount64();
        unsigned elapsed_us = (unsigned)(now - previous_tick) * 1000u;
        previous_tick = now;
        state = host_audio_movie_clock(audio, &position);
        if (state < 0) { puts("SKIP finale sync: audio device unavailable"); result = 0; goto done; }
        if (state == 0) phase.movie.tick_us = 0;
        else if ((state == 1 || (state == 2 && position > last)) && position >= last) {
            phase.movie.tick_us = (position - last) * 1000u;
            last = position;
        } else if (state == 2) phase.movie.tick_us = elapsed_us;
        else goto done;
        if (!start_sent && state == 1 && position >= 500) {
            saturn_pad_update_host(&pad, MB_PAD_START);
            start_sent = 1;
        } else saturn_pad_update_host(&pad, MB_PAD_RIGHT);
        if (special_phase_step(&phase, &pad) != 0 || phase.movie.decode_error ||
            (phase.state != SPECIAL_ST_PLAY && !(full && phase.state == SPECIAL_ST_FINISHED))) goto done;
        expected = 1u + (unsigned)((uint64_t)position * 1000u * phase.movie.frame_rate / ((uint64_t)phase.movie.frame_scale * 1000000u));
        if (expected > phase.movie.frame_count) expected = phase.movie.frame_count;
        if (state == 1 && phase.movie.cur != expected) {
            printf("finale sync: audio=%u ms frame=%u expected=%u\n", position, phase.movie.cur, expected);
            goto done;
        }
        Sleep(10);
    }
    if (!start_sent || position < 1500 || (full && (state != 2 || phase.state != SPECIAL_ST_FINISHED ||
        phase.movie.cur != phase.movie.frame_count))) {
        printf("finale incomplete: audio=%d position=%u state=%d frame=%u/%u\n",
               state, position, phase.state, phase.movie.cur, phase.movie.frame_count);
        goto done;
    }
    printf("OK %sfinale audio clock after START: %u ms, %u/%u frames\n",
           full ? "full " : "", position, phase.movie.cur, phase.movie.frame_count);
    result = 0;
done:
    host_audio_stop(audio);
    special_phase_free(&phase);
    cd_close(&cd);
    return result;
}

static int test_movie_interrupt(void) {
    const char *path = getenv("MB_DISC1");
    const char *movies[] = {"/07GUITAR", "/28LOSE"};
    cdimage cd;
    special_phase phase = {0};
    host_audio *audio = NULL;
    char err[512];
    int result = 1;
    if (!path) { puts("SKIP interrupt: MB_DISC1 unset"); return 0; }
    if (cd_open(&cd, path, err, sizeof err)) return 1;
    for (int i = 0; i < 2; i++) {
        unsigned position = 0;
        int state = 0;
        ULONGLONG deadline, stopped_at;
        if (special_phase_load(&phase, &cd, movies[i], STAGE_KIND_MOVIE)) goto done;
        audio = host_audio_start_pcm16(phase.movie.pcm, phase.movie.pcm_samples,
                                       phase.movie.pcm_rate, phase.movie.pcm_channels);
        if (!audio) goto done;
        deadline = GetTickCount64() + 3000;
        while (GetTickCount64() < deadline && position < 100) {
            state = host_audio_movie_clock(audio, &position);
            if (state < 0) { puts("SKIP interrupt: audio device unavailable"); result = 0; goto done; }
            if (state == 2) goto done;
            Sleep(10);
        }
        if (state != 1 || position < 100) goto done;
        {
            saturn_pad pad = {0};
            saturn_pad_update_host(&pad, MB_PAD_START);
            special_phase_step(&phase, &pad);
            if (phase.state != SPECIAL_ST_FINISHED) goto done;
        }
        stopped_at = GetTickCount64();
        host_audio_stop(audio);
        audio = NULL;
        if (GetTickCount64() - stopped_at > 1000) {
            puts("movie audio stop took more than one second");
            goto done;
        }
        special_phase_free(&phase);
    }
    puts("OK movie interruption and replacement audio");
    result = 0;
done:
    host_audio_stop(audio);
    special_phase_free(&phase);
    cd_close(&cd);
    return result;
}

int main(void) {
    if (getenv("MB_STORY_ONLY")) return test_story_clock();
    if (getenv("MB_FINALE_ONLY")) return test_finale_clock();
    int16_t samples[44100] = {0}; /* 0.5 s of stereo silence */
    host_audio *audio = host_audio_start_pcm16(samples, 44100, 44100, 2);
    unsigned before = 0, paused = 0, after = 0;
    int state = 0;
    if (!audio) return 1;
    for (int i = 0; i < 100 && state == 0; i++) {
        Sleep(10);
        state = host_audio_movie_clock(audio, &before);
    }
    if (state < 0) {
        puts("SKIP: audio device unavailable or does not report milliseconds");
        host_audio_stop(audio);
        return 0;
    }
    if (state != 1) { host_audio_stop(audio); return 1; }
    Sleep(60);
    host_audio_movie_pause(audio, 1);
    Sleep(40);
    state = host_audio_movie_clock(audio, &paused);
    Sleep(70);
    state = host_audio_movie_clock(audio, &after);
    if (state < 0) {
        puts("SKIP: audio device cannot report playback position");
        host_audio_stop(audio);
        return 0;
    }
    if (state != 1 || after < before || after > paused + 30) {
        printf("pause clock invalid: %u %u %u state=%d\n", before, paused, after, state);
        host_audio_stop(audio);
        return 1;
    }
    host_audio_movie_pause(audio, 0);
    for (int i = 0; i < 100 && state == 1; i++) {
        Sleep(10);
        state = host_audio_movie_clock(audio, &after);
    }
    host_audio_stop(audio);
    if (state != 2 || after != 500) {
        printf("completion clock invalid: %u state=%d\n", after, state);
        return 1;
    }
    puts("OK movie audio clock");
    if (test_opening_sync()) return 1;
    if (test_lose_sync()) return 1;
    if (test_story_clock()) return 1;
    if (test_finale_clock()) return 1;
    return test_movie_interrupt();
}


