#include "core/cdimage.h"
#include "core/identity.h"
#include "core/saturn_pad.h"
#include "core/runway_sfx.h"
#include "core/sha256.h"
#include "core/special_phase.h"
#include "core/stage_catalog.h"
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

static int play_rhythm(special_phase *ph) {
    saturn_pad pad = {0};
    int i;
    int period = ph->beat_period ? (int)ph->beat_period : (int)SPECIAL_BEAT_PERIOD;
    for (i = 0; i < 8000 && !special_phase_done(ph); i++) {
        /* RIGHT strum + A on beat edges. */
        unsigned btn = MB_PAD_RIGHT;
        if ((i % period) <= 2)
            btn |= MB_PAD_A;
        saturn_pad_update_host(&pad, btn);
        special_phase_step(ph, &pad);
    }
    return ph->state == SPECIAL_ST_FINISHED;
}

static int play_action(special_phase *ph) {
    saturn_pad pad = {0};
    int i;
    for (i = 0; i < 5000 && !special_phase_done(ph); i++) {
        saturn_pad_update_host(&pad, MB_PAD_RIGHT);
        special_phase_step(ph, &pad);
    }
    return ph->state == SPECIAL_ST_FINISHED;
}

static int play_present(special_phase *ph) {
    saturn_pad pad = {0};
    unsigned i, max_frames = ph->movie_ok && ph->kind != STAGE_KIND_LOSE ? ph->movie.frame_count + 2 : 5000;
    if (ph->movie_ok && ph->kind != STAGE_KIND_LOSE) {
        ph->movie.tick_external = 1;
        ph->movie.tick_us = ph->movie.us_per_frame + 1u;
    }
    for (i = 0; i < max_frames && !special_phase_done(ph); i++) {
        saturn_pad_update_host(&pad, MB_PAD_RIGHT);
        special_phase_step(ph, &pad);
    }
    return special_phase_done(ph);
}

int main(void) {
    {
        special_phase arena = {0};
        unsigned si;
        arena.kind = STAGE_KIND_FINALE;
        for (si = 0; si < FINALE_RING_SLOTS; si++) arena.ring_active[si] = 1;
        arena.ring_active_count = (int)FINALE_RING_SLOTS;
        /* HIT = JUMP|C + SoundEvent 0x2A; ACTIVATE = C + SoundEvent 0x32. */
        CHECK(special_phase_finale_shoot(&arena, 0) == 0);
        CHECK(arena.sfx_flags == (RUNWAY_SFX_FLAG_JUMP | RUNWAY_SFX_FLAG_C));
        CHECK(arena.ring_sfx_event == 0x2Au);
        arena.sfx_flags = 0;
        arena.ring_sfx_event = 0;
        CHECK(special_phase_finale_shoot(&arena, 0) == 0);
        CHECK(arena.sfx_flags == RUNWAY_SFX_FLAG_C);
        CHECK(arena.ring_sfx_event == 0x32u);
        arena.sfx_flags = 0;
        arena.ring_sfx_event = 0;
        /* dir LEFT selects slot 12 (still lit) → hit SoundEvent 0x2B. */
        CHECK(special_phase_finale_shoot(&arena, 2) == 0);
        CHECK(arena.ring_sfx_event == 0x2Bu);
    }
    const char *d1 = getenv("MB_DISC1");
    const char *d2 = getenv("MB_DISC2");
    const char *man = getenv("MB_MANIFEST");
    cdimage disc1, disc2;
    special_phase phase;
    int opened2 = 0;

    if (!d1 || !man) {
        printf("SKIP special_phase (set MB_DISC1 and MB_MANIFEST)\n");
        return 0;
    }
    CHECK(open_disc(&disc1, d1, man, 1) == 0);
    if (d2 && open_disc(&disc2, d2, man, 2) == 0)
        opened2 = 1;

    CHECK(special_phase_load(&phase, &disc1, "/07GUITAR", STAGE_KIND_RHYTHM) == 0);
    CHECK(phase.mode == SPECIAL_MODE_RHYTHM);
    CHECK(phase.textures_size > 0);
    CHECK(!phase.movie_ok && !phase.movie_pcm_pending && phase.avi_size == 0);
    {
        saturn_pad pad = {0};
        saturn_pad_update_host(&pad, MB_PAD_START);
        special_phase_step(&phase, &pad);
        CHECK(phase.state == SPECIAL_ST_PAUSED);
        saturn_pad_update_host(&pad, MB_PAD_START);
        special_phase_step(&phase, &pad);
        CHECK(phase.state == SPECIAL_ST_PAUSED);
        saturn_pad_update_host(&pad, 0);
        special_phase_step(&phase, &pad);
        saturn_pad_update_host(&pad, MB_PAD_START);
        special_phase_step(&phase, &pad);
        CHECK(phase.state == SPECIAL_ST_PLAY);
    }
    CHECK(play_rhythm(&phase));
    printf("GUITAR: frames=%u hits=%u %s\n", phase.frames, phase.hits, phase.status);
    special_phase_free(&phase);

    CHECK(special_phase_load(&phase, &disc1, "/14DRUM", STAGE_KIND_RHYTHM) == 0);
    CHECK(!phase.movie_ok && !phase.movie_pcm_pending && phase.avi_size == 0);
    CHECK(play_rhythm(&phase));
    printf("DRUM: frames=%u hits=%u %s\n", phase.frames, phase.hits, phase.status);
    special_phase_free(&phase);

    CHECK(special_phase_load(&phase, &disc1, "/16BIGBNS", STAGE_KIND_BIGBONES) == 0);
    CHECK(phase.mode == SPECIAL_MODE_ACTION);
    CHECK(!phase.movie_ok && !phase.movie_pcm_pending && phase.avi_size == 0);
    CHECK(phase.pan_size > 0);
    CHECK(play_action(&phase));
    printf("BIGBNS: frames=%u pan=%zu %s\n", phase.frames, phase.pan_size, phase.status);
    special_phase_free(&phase);

    if (opened2) {
        CHECK(special_phase_load(&phase, &disc2, "/25GENRM", STAGE_KIND_GENROOM) == 0);
        CHECK(phase.mode == SPECIAL_MODE_ACTION);
        CHECK(!phase.movie_ok && !phase.movie_pcm_pending && phase.avi_size == 0);
        CHECK(play_action(&phase));
        printf("GENRM: frames=%u %s\n", phase.frames, phase.status);
        special_phase_free(&phase);

        CHECK(special_phase_load(&phase, &disc2, "/27FINALE", STAGE_KIND_FINALE) == 0);
        CHECK(phase.mode == SPECIAL_MODE_PRESENT);
        CHECK(phase.movie_ok && phase.movie.frame_count > 240);
        CHECK(phase.enemies.count > 0);
        CHECK(memcmp(phase.frame_rgba, duck_avi_frame_rgba(&phase.movie),
                     320u * 224u * 4u) == 0);
        {
            saturn_pad pad = {0};
            phase.movie.tick_external = 1;
            phase.movie.tick_us = 0;
            saturn_pad_update_host(&pad, MB_PAD_RIGHT);
            for (int i = 0; i < 16; i++) special_phase_step(&phase, &pad);
            CHECK(phase.movie.frames_drawn == 1 && phase.state == SPECIAL_ST_PLAY);
            CHECK(memcmp(phase.frame_rgba, duck_avi_frame_rgba(&phase.movie),
                         320u * 224u * 4u) == 0);
            saturn_pad_update_host(&pad, 0);
            phase.movie.tick_us = phase.movie.us_per_frame + 1u;
            for (int i = 0; i < 117; i++) special_phase_step(&phase, &pad);
            {
                sha256_ctx hash;
                uint8_t digest[32];
                char hex[65];
                CHECK(phase.movie.frames_drawn == 118);
                sha256_init(&hash);
                sha256_update(&hash, phase.frame_rgba, 320u * 224u * 4u);
                sha256_final(&hash, digest);
                sha256_hex(digest, hex);
                /* Ymir F12, FINALE selected through the loader debugger. */
                CHECK(strcmp(hex, "48152dd9d19d9944a53f223ecfb6ccb9dce27e8385b096e3f2312df733bf9b27") == 0);
            }
            for (int i = 0; i < 171; i++) special_phase_step(&phase, &pad);
            {
                sha256_ctx hash;
                uint8_t digest[32];
                char hex[65];
                CHECK(phase.movie.frames_drawn == 289);
                sha256_init(&hash);
                sha256_update(&hash, phase.frame_rgba, 320u * 224u * 4u);
                sha256_final(&hash, digest);
                sha256_hex(digest, hex);
                /* Ymir F12 after the FINALE result was forced to victory. */
                CHECK(strcmp(hex, "4a00986965f279613fabd9567a0e462176ba08bfc128fe903377df1cd1b9e446") == 0);
            }
            phase.movie.tick_external = 0;
        }
        CHECK(play_present(&phase));
        CHECK(phase.state == SPECIAL_ST_FINISHED);
        CHECK(phase.movie.frames_drawn == phase.movie.frame_count);
        CHECK(duck_avi_playback_done(&phase.movie));
        printf("FINALE: frames=%u avi=%zu %s\n", phase.frames, phase.avi_size,
               phase.status);
        special_phase_free(&phase);

        CHECK(special_phase_load(&phase, &disc2, "/27FINALE", STAGE_KIND_FINALE) == 0);
        {
            saturn_pad held = {0};
            saturn_pad_update_host(&held, MB_PAD_START);
            saturn_pad_update_host(&held, MB_PAD_START);
            CHECK(!held.pressed && (held.held & 0x0800u));
            special_phase_step(&phase, &held);
            CHECK(phase.state == SPECIAL_ST_FINISHED && phase.movie.playback_done);
        }
        special_phase_free(&phase);

        /* Arena: ShootRing clears 16 lit slots → EndLevel(0). */
        CHECK(special_phase_load(&phase, &disc2, "/27FINALE", STAGE_KIND_FINALE) == 0);
        CHECK(phase.ring_active_count == (int)FINALE_RING_SLOTS);
        CHECK(phase.ring_cleared == 0);
        /* STXTR ring strip: 5×112×30 from CG 0x25C29AA0..0x25C2CF60. */
        CHECK(phase.ring_sprite_count == FINALE_RING_SPRITE_FRAMES);
        CHECK(phase.ring_sprite_w[0] == 112u && phase.ring_sprite_h[0] == 30u);
        CHECK(phase.sound_size > 0);
        {
            runway_sfx sfx;
            const int8_t *pcm = NULL;
            size_t len = 0;
            CHECK(runway_sfx_load_dir(&sfx, &disc2, "/27FINALE") == 0);
            CHECK(runway_sfx_event_pcm(&sfx, 0x2Au, &pcm, &len) == 0 && len == 68u);
            CHECK(runway_sfx_event_pcm(&sfx, 0x29u, &pcm, &len) == 0 && len == 68u);
            CHECK(runway_sfx_event_pcm(&sfx, 0x2Bu, &pcm, &len) == 0 && len == 68u);
            /* Activate IDs cite span4[19..21]; host falls back to sample 15. */
            CHECK(runway_sfx_event_pcm(&sfx, 0x32u, &pcm, &len) == 0 && len == 516u);
            runway_sfx_free(&sfx);
        }
        {
            unsigned k;
            for (k = 0; k < FINALE_RING_SLOTS; k++) {
                /* dir 0: idx = ((0+0x100-phase)+8)>>4 → phase = 255-16*k selects k. */
                phase.ring_phase = 255 - (int)(16u * k);
                CHECK(special_phase_finale_shoot(&phase, 0) ==
                      (k + 1u == FINALE_RING_SLOTS ? 1 : 0));
                CHECK(phase.ring_last_index == (int)k);
                CHECK(phase.ring_last_activated == 0);
            }
            CHECK(phase.ring_active_count == 0);
            CHECK(phase.ring_cleared == 1);
            CHECK(phase.ring_shots == FINALE_RING_SLOTS);
            /* Second clear attempt is a no-op. */
            CHECK(special_phase_finale_shoot(&phase, 0) == 0);
        }
        printf("FINALE ring arena: shots=%u cleared=%d OK\n", phase.ring_shots,
               phase.ring_cleared);
        special_phase_free(&phase);

        /* Clearing the ring during an active movie must not end the movie. */
        CHECK(special_phase_load(&phase, &disc2, "/27FINALE", STAGE_KIND_FINALE) == 0);
        {
            saturn_pad input = {0};
            input.pressed = 0x0100u;
            phase.movie.tick_external = 1;
            phase.movie.tick_us = 0;
            /* mode 1 incr @0x2b2158 is 32768: one Brain tick keeps high-16 phase. */
            phase.ring_phase_mode = 1;
            for (unsigned k = 0; k < FINALE_RING_SLOTS; k++) {
                phase.ring_phase_acc = (255 - (int)(16u * k)) * 65536;
                CHECK(special_phase_step(&phase, &input) == 0);
                CHECK(phase.sfx_flags == 0);
            }
            CHECK(phase.ring_cleared && phase.ring_shots == FINALE_RING_SLOTS);
            CHECK(phase.state == SPECIAL_ST_PLAY && !phase.movie.playback_done);
            CHECK(phase.movie.cur == 1);
            input.pressed = 0;
            phase.movie.tick_us = 100000000u;
            for (unsigned k = 0; k < 5 && phase.state == SPECIAL_ST_PLAY; k++)
                CHECK(special_phase_step(&phase, &input) == 0);
            CHECK(phase.state == SPECIAL_ST_FINISHED && phase.movie.playback_done);
            CHECK(phase.movie.cur == phase.movie.frame_count);
        }
        special_phase_free(&phase);

        /* Arena-first: defer ending → clear rings → PlayDuckMovie(3) → FINISHED. */
        CHECK(special_phase_load(&phase, &disc2, "/27FINALE", STAGE_KIND_FINALE) == 0);
        CHECK(phase.movie_ok == 1 && phase.finale_movie_armed == 1);
        CHECK(special_phase_finale_defer_ending(&phase) == 0);
        CHECK(phase.movie_ok == 0 && phase.finale_movie_armed == 1);
        {
            saturn_pad input = {0};
            input.pressed = 0x0100u;
            phase.ring_phase_mode = 1;
            for (unsigned k = 0; k < FINALE_RING_SLOTS; k++) {
                phase.ring_phase_acc = (255 - (int)(16u * k)) * 65536;
                CHECK(special_phase_step(&phase, &input) == 0);
            }
            CHECK(phase.ring_cleared && phase.movie_ok);
            CHECK(phase.state == SPECIAL_ST_PLAY && !phase.movie.playback_done);
            CHECK(phase.movie.cur >= 1u);
            input.pressed = 0;
            phase.movie.tick_external = 1;
            phase.movie.tick_us = 100000000u;
            for (unsigned k = 0; k < 5 && phase.state == SPECIAL_ST_PLAY; k++)
                CHECK(special_phase_step(&phase, &input) == 0);
            CHECK(phase.state == SPECIAL_ST_FINISHED && phase.movie.playback_done);
            CHECK(phase.movie.cur == phase.movie.frame_count);
        }
        printf("FINALE arena→ending movie: shots=%u frames=%u OK\n",
               phase.ring_shots, phase.movie.frames_drawn);
        special_phase_free(&phase);
    }

    CHECK(special_phase_load(&phase, &disc1, "/28LOSE", STAGE_KIND_LOSE) == 0);
    CHECK(phase.mode == SPECIAL_MODE_PRESENT);
    CHECK(phase.avi_size > 0);
    CHECK(phase.movie_ok == 1);
    CHECK(phase.movie.frame_count > 0);
    {
        saturn_pad pad = {0};
        for (int i = 0; i < 4; i++) {
            special_phase_step(&phase, &pad);
            CHECK(phase.state == SPECIAL_ST_PLAY && !phase.movie.playback_done);
            CHECK(phase.movie.frames_drawn == 1);
            saturn_pad_update_host(&pad, 0);
        }
        special_phase_step(&phase, &pad);
        CHECK(phase.movie.frames_drawn == 2);
        CHECK(memcmp(phase.frame_rgba, duck_avi_frame_rgba(&phase.movie),
                     320u * 224u * 4u) == 0);
    }
    CHECK(play_present(&phase));
    CHECK(phase.state == SPECIAL_ST_LOST); /* DUKMOVIE/LOSE ends as lose */
    CHECK(phase.movie.frames_drawn == 203);
    {
        uint64_t video_us = (uint64_t)phase.frames * 16000u;
        uint64_t audio_us = (uint64_t)phase.movie.pcm_samples * 1000000u /
                            phase.movie.pcm_channels / phase.movie.pcm_rate;
        CHECK(video_us >= audio_us && video_us - audio_us < 100000u);
    }
    printf("LOSE: frames=%u avi=%zu duck_frames=%u drawn=%u %s\n", phase.frames,
           phase.avi_size, phase.movie.frame_count, phase.movie.frames_drawn,
           phase.status);
    special_phase_free(&phase);

    CHECK(special_phase_load(&phase, &disc1, "/28LOSE", STAGE_KIND_LOSE) == 0);
    {
        saturn_pad held = {0};
        saturn_pad_update_host(&held, MB_PAD_START);
        saturn_pad_update_host(&held, MB_PAD_START);
        CHECK(!held.pressed && (held.held & 0x0800u));
        special_phase_step(&phase, &held);
        CHECK(phase.state == SPECIAL_ST_LOST && phase.movie.playback_done);
    }
    special_phase_free(&phase);

    cd_close(&disc1);
    if (opened2) cd_close(&disc2);
    printf("OK special_phase\n");
    return 0;
}
