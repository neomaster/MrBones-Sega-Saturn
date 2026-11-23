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
    unsigned win_events = 0;
    int i;
    for (i = 0; i < 12000 && !special_phase_done(ph); i++) {
        saturn_pad_update_host(&pad, special_phase_autoplay_buttons(ph, (unsigned)i));
        runway_sfx_frame effects;
        runway_sfx_begin_frame(&effects);
        special_phase_step(ph, &pad);
        runway_sfx_end_frame();
        for (unsigned j = 0; j < effects.count; ++j) {
            if (effects.commands[j].kind == 0u && effects.commands[j].bank == 0x18u) {
                if (ph->state != SPECIAL_ST_FINISHED) return 0;
                ++win_events;
            }
        }
    }
    return ph->state == SPECIAL_ST_FINISHED && win_events == (ph->drum_notes ? 0u : 1u);
}

static int play_action(special_phase *ph) {
    saturn_pad pad = {0};
    unsigned i;
    for (i = 0; i < special_phase_autoplay_frame_limit(ph) && !special_phase_done(ph); i++) {
        saturn_pad_update_host(
            &pad, special_phase_autoplay_buttons(ph, (unsigned)i));
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
    CHECK(phase.max_combo >= 4u && phase.rhythm_heals >= 1u);
    printf("GUITAR: frames=%u hits=%u max_combo=%u heals=%u %s\n",
           phase.frames, phase.hits, phase.max_combo, phase.rhythm_heals,
           phase.status);
    special_phase_free(&phase);

    CHECK(special_phase_load(&phase, &disc1, "/14DRUM", STAGE_KIND_RHYTHM) == 0);
    CHECK(!phase.movie_ok && !phase.movie_pcm_pending && phase.avi_size == 0);
    CHECK(play_rhythm(&phase));
    CHECK(phase.max_combo >= 4u && phase.rhythm_heals >= 1u);
    printf("DRUM: frames=%u hits=%u max_combo=%u heals=%u %s\n",
           phase.frames, phase.hits, phase.max_combo, phase.rhythm_heals,
           phase.status);
    special_phase_free(&phase);

    const char *music_stages[] = {"/07GUITAR", "/14DRUM"};
    for (unsigned stage = 0; stage < 2; ++stage) {
        for (unsigned control = 0; control < 2; ++control) {
            saturn_pad probe = {0};
            CHECK(!special_phase_load(&phase, &disc1, music_stages[stage], STAGE_KIND_RHYTHM));
            for (unsigned t = 0; t < 12000 && !special_phase_done(&phase); ++t) {
                saturn_pad_update_host(&probe, control ? MB_PAD_RIGHT : 0);
                special_phase_step(&phase, &probe);
            }
            CHECK(phase.state == SPECIAL_ST_LOST);
            special_phase_free(&phase);
        }
    }

    CHECK(special_phase_load(&phase, &disc1, "/16BIGBNS", STAGE_KIND_BIGBONES) == 0);
    CHECK(phase.mode == SPECIAL_MODE_ACTION);
    CHECK(!phase.movie_ok && !phase.movie_pcm_pending && phase.avi_size == 0);
    CHECK(phase.pan_size > 0);
    CHECK(phase.big_count == 16 && phase.parts_collected == 0);
    CHECK(phase.big_bumpers[0].x == -2479445 && phase.part_type == 0xa48);
    CHECK(play_action(&phase));
    CHECK(phase.parts_collected == 6 && phase.big_wave == 5 && phase.bumper_bounces > 0);
    CHECK(special_phase_targets_remaining(&phase) == 0u);
    printf("BIGBNS: frames=%u pan=%zu %s\n", phase.frames, phase.pan_size, phase.status);
    special_phase_free(&phase);

    if (opened2) {
        CHECK(special_phase_load(&phase, &disc2, "/25GENRM", STAGE_KIND_GENROOM) == 0);
        CHECK(phase.mode == SPECIAL_MODE_ACTION);
        CHECK(!phase.movie_ok && !phase.movie_pcm_pending && phase.avi_size == 0);
        CHECK(play_action(&phase));
        CHECK(special_phase_targets_remaining(&phase) == 0u);
        CHECK(phase.plasma_collected >= 16 && phase.skel.skeletism > 0x16666);
        CHECK(phase.enemies.zaps_fired > 0);
        printf("GENRM: frames=%u %s\n", phase.frames, phase.status);
        special_phase_free(&phase);

        CHECK(special_phase_load(&phase, &disc2, "/27FINALE", STAGE_KIND_FINALE) == 0);
        CHECK(phase.mode == SPECIAL_MODE_PRESENT);
        CHECK(phase.movie_ok && phase.movie.frame_count > 240);
        CHECK(phase.red_bolt.age == 0 && phase.finale_red[0].count == 28);
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

        /*
         * Distorted-quad paint: RingDefinitionTable record 0 verts map a
         * 112×30 sprite onto a thin trapezoid (not a centred AABB). Uses a
         * synthetic opaque sprite so HUD/STXT content cannot mask geometry.
         * Alpha-0 texels in the sprite must leave the backdrop untouched.
         */
        CHECK(special_phase_load(&phase, &disc2, "/27FINALE", STAGE_KIND_FINALE) == 0);
        CHECK(special_phase_finale_defer_ending(&phase) == 0);
        CHECK(phase.movie_ok == 0);
        /* Ymir US disc 2, victory replay tick 70: the displayed Bones and
         * guitar occupy [142,69 .. 193,147) and 1088 opaque VDP1 texels. */
        {
            uint8_t *neutral = calloc(320u * 224u, 4), *hit = calloc(320u * 224u, 4);
            finale_body *rig = finale_body_load(phase.code, phase.code_size,
                                                phase.textures, phase.textures_size, phase.finale_random_table);
            int left = 320, top = 224, right = 0, bottom = 0;
            unsigned covered = 0, flashed = 0, unchanged_blue = 0;
            CHECK(rig && neutral && hit);
            for (unsigned tick = 1; tick <= 70; ++tick) {
                if (tick == 67) finale_body_command(rig, 0); /* first sampled north */
                finale_body_tick(rig, 0, NULL);
            }
            finale_body_draw(rig, neutral, 0, 0);
            finale_body_draw(rig, hit, 1, 0);
            for (int y = 0; y < 224; ++y) {
                for (int x = 0; x < 320; ++x) {
                    size_t offset = 4u * (size_t)(320*y+x);
                    const uint8_t *n = neutral + offset, *h = hit + offset;
                    CHECK(n[3] == h[3]);
                    if (!n[3]) continue;
                    if (x < left) left = x;
                    if (y < top) top = y;
                    if (x+1 > right) right = x+1;
                    if (y+1 > bottom) bottom = y+1;
                    ++covered;
                    if (h[0] > n[0]+60) ++flashed;
                    if (n[2] > n[0]+70 && !memcmp(n,h,3)) ++unchanged_blue;
                }
            }
            CHECK(abs(left-142) <= 2 && abs(top-69) <= 2);
            CHECK(abs(right-193) <= 2 && abs(bottom-147) <= 2);
            CHECK(covered > 1000 && covered < 1180 && flashed > 250 && unchanged_blue > 50);
            /* Rendeng draws the wrist in front of the guitar at (167,90)
             * on this frame (VDP1 0x880e). Only the wrist turns red. */
            size_t wrist = 4u * (320u * 90u + 167u);
            CHECK(hit[wrist] > neutral[wrist] + 60);
            /* N/S/E in the same tape. At tick 85 the guitar crosses over
             * the head; original guitar bounds are [150,70 .. 197,95).
             * Painting the south pose alone used to put it below the waist. */
            for (unsigned tick = 71; tick <= 85; ++tick) {
                if (tick == 76) finale_body_command(rig, 1);
                if (tick == 85) finale_body_command(rig, 2);
                finale_body_tick(rig, 0, NULL);
            }
            memset(neutral, 0, 320u * 224u * 4u);
            finale_body_draw(rig, neutral, 0, 0);
            unsigned above = 0, below = 0;
            for (unsigned y = 0; y < 224; ++y) {
                for (unsigned x = 0; x < 320; ++x) {
                    const uint8_t *px = neutral + 4u*(320u*y+x);
                    if (px[2] <= px[0]+40 || px[2] <= 50) continue;
                    if (x >= 148 && x <= 198 && y >= 68 && y <= 96) ++above;
                    if (y >= 100) ++below;
                }
            }
            CHECK(above > 150 && below == 0);
            /* Displayed VDP1 frame 85. The diagonal edge passes between
             * these neighbours; its covered texel is 0x880b. */
            CHECK(neutral[4u*(320u*100u+152u)+3u] == 255);
            CHECK(neutral[4u*(320u*99u+153u)+3u] == 0);
            /* Direction changes from east to west. The full 320x224 VDP1
             * body/guitar silhouette on frame 101 contains 1104 pixels. */
            for (unsigned tick = 86; tick <= 101; ++tick) {
                if (tick == 94) finale_body_command(rig, 3);
                finale_body_tick(rig, 0, NULL);
            }
            memset(neutral, 0, 320u * 224u * 4u);
            finale_body_draw(rig, neutral, 0, 0);
            uint32_t silhouette = 2166136261u;
            for (size_t i = 0; i < 320u * 224u; ++i)
                silhouette = (silhouette ^ (uint32_t)(neutral[4u*i+3u] != 0)) * 16777619u;
            CHECK(silhouette == 0xf4d8ddcbu);
            /* VDP1 scaled sprites repeat the lower middle texel on even
             * extents; the damage sprite's center row and column exercise both. */
            const uint8_t effect_pixels[8] = {1, 1, 0, 0, 0, 0, 0, 0};
            finale_sprite effects[FINALE_SPRITES_MAX] = {0};
            effects[3] = (finale_sprite){effect_pixels, 4, 2, 8, 8, 0};
            finale_body_tick(rig, 0, effects);
            memset(neutral, 0, 320u * 224u * 4u);
            finale_body_draw(rig, neutral, 0, 0);
            CHECK(neutral[4u*(320u*9u+10u)+3u] == 255);
            CHECK(neutral[4u*(320u*9u+11u)+3u] == 0);
            CHECK(neutral[4u*(320u*10u+10u)+3u] == 0);
            free(rig);
            free(hit);
            free(neutral);
        }
        {
            saturn_pad neutral_pad = {0};
            for (unsigned tick = 1; tick <= 85; ++tick)
                CHECK(special_phase_step(&phase, &neutral_pad) == 0);
            CHECK(special_phase_render(&phase) == 0);
            /* RGB samples from the displayed Ymir ring, including an overlapping edge. */
            const uint8_t red[3] = {68,4,8}, white[3] = {185,185,187};
            CHECK(!memcmp(phase.frame_rgba + 4u*(19u*320u + 128u), red, 3));
            CHECK(!memcmp(phase.frame_rgba + 4u*(20u*320u + 165u), white, 3));
            /* CShotManager's warning is painted by the bank-3 background palette. */
            const uint8_t warning[3] = {87,0,0};
            CHECK(!memcmp(phase.frame_rgba + 4u*(39u*320u + 226u), warning, 3));
            CHECK(!special_phase_finale_shoot(&phase, 0));
            int slot = phase.ring_last_index;
            CHECK(!phase.ring_active[slot]);
            for (unsigned age = 1; age <= 17; ++age) {
                CHECK(special_phase_step(&phase, &neutral_pad) == 0);
                if (age == 3) CHECK(phase.ring_list[0].slots[slot] == &phase.ring_anim[1].frames[0]);
                if (age == 16) CHECK(phase.ring_list[0].slots[slot] == &phase.ring_anim[1].frames[4]);
            }
            CHECK(phase.ring_list[0].slots[slot] == NULL);
        }
        /* CRing selects four idle leaves and reverses the five exit leaves on activation. */
        CHECK(phase.ring_anim[0].count == 4 && phase.ring_anim[0].ticks == 3);
        CHECK(phase.ring_anim[1].count == 5 && phase.ring_anim[1].ticks == 3);
        CHECK(phase.ring_anim[2].count == 5 && phase.ring_anim[2].ticks == 5);
        for (unsigned i = 0; i < 5; ++i) {
            CHECK(phase.ring_anim[1].frames[i].data == phase.ring_anim[2].frames[4-i].data);
            CHECK(phase.ring_anim[1].frames[i].data != phase.ring_anim[0].frames[0].data);
        }
        special_phase_free(&phase);

        /* Arena: ShootRing clears 16 lit slots → EndLevel(0). */
        CHECK(special_phase_load(&phase, &disc2, "/27FINALE", STAGE_KIND_FINALE) == 0);
        CHECK(phase.ring_active_count == (int)FINALE_RING_SLOTS);
        CHECK(phase.ring_cleared == 0);
        CHECK(phase.ring_age[0] == -12 && phase.ring_age[15] == 2);
        CHECK(phase.sound_size > 0);
        {
            runway_sfx sfx;
            const int8_t *pcm = NULL;
            size_t len = 0;
            CHECK(runway_sfx_load_dir(&sfx, &disc2, "/27FINALE") == 0);
            static const unsigned ids[] = {0x18, 0x29, 0x2a, 0x2b, 0x2c, 0x2d,
                                           0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33};
            uint32_t hashes[sizeof ids / sizeof *ids];
            for (size_t i = 0; i < sizeof ids / sizeof *ids; ++i) {
                CHECK(runway_sfx_event_pcm(&sfx, ids[i], &pcm, &len) == 0 && len > 16u);
                uint32_t hash = 2166136261u;
                for (size_t j = 0; j < len; ++j) hash = (hash ^ (uint8_t)pcm[j]) * 16777619u;
                hashes[i] = hash;
            }
            /* Three pitches share a source waveform but must not sound identical. */
            CHECK(hashes[1] != hashes[2] && hashes[2] != hashes[3] && hashes[1] != hashes[3]);
            CHECK(runway_sfx_event_pcm(&sfx, 0x15u, &pcm, &len) != 0);
            printf("FINALE bank events and note pitch OK\n");
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

        /* Arena inputs during the ending cannot mutate the ring or its audio clock. */
        CHECK(special_phase_load(&phase, &disc2, "/27FINALE", STAGE_KIND_FINALE) == 0);
        {
            saturn_pad input = {0};
            input.pressed = 0x0100u;
            phase.movie.tick_external = 1;
            phase.movie.tick_us = 0;
            for (unsigned k = 0; k < FINALE_RING_SLOTS; k++) {
                CHECK(special_phase_step(&phase, &input) == 0);
                CHECK(phase.sfx_flags == 0);
            }
            CHECK(!phase.ring_cleared && phase.ring_shots == 0 && phase.ring_active_count == 16);
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
        {
            saturn_pad idle = {0};
            for (unsigned i = 0; i < 4; ++i) CHECK(special_phase_step(&phase, &idle) == 0);
        }
        CHECK(special_phase_finale_defer_ending(&phase) == 0 && phase.frames == 0);
        CHECK(phase.movie_ok == 0 && phase.finale_movie_armed == 1);
        {
            saturn_pad input = {0};
            static const uint32_t dirs[] = {MB_PAD_UP, MB_PAD_DOWN, MB_PAD_RIGHT, MB_PAD_LEFT};
            int32_t energy = phase.skel.skeletism;
            uint32_t x = phase.bones_x, y = phase.bones_y;
            CHECK(phase.finale_origin_x == 159 && phase.finale_origin_y == 96);
            for (unsigned d = 0; d < 4; ++d) {
                CHECK(phase.finale_rows[d] == (d == 0 ? 29u : 28u));
                CHECK(phase.finale_red[d].impact == (d == 1 ? 21u : 25u));
                CHECK(phase.finale_blue[d].impact == 19u);
            }
            {
                /* East, record 9, zero/literal packets at SH-2 0x271578. */
                const finale_bolt_frame *f = &phase.finale_red[2].frames[9];
                sha256_ctx ctx;
                uint8_t digest[32];
                char hex[65];
                CHECK(f->width == 72 && f->height == 42 && f->unpacked != NULL);
                sha256_init(&ctx);
                sha256_update(&ctx, f->pixels, (size_t)f->width * f->height);
                sha256_final(&ctx, digest);
                sha256_hex(digest, hex);
                CHECK(!strcmp(hex, "cd2d31643795a1b4db6ba2325d966a110a0d652c906d262c90b47dd7c684cd78"));
            }
            {
                const finale_bolt_frame *burst = &phase.finale_hit[0].frames[6];
                sha256_ctx ctx;
                uint8_t digest[32];
                char hex[65];
                CHECK(phase.finale_hit[0].count == 15 && phase.finale_hit[4].count == 15);
                CHECK(phase.finale_hit[5].count == 8);
                CHECK(phase.finale_ring_hit_y[0] == phase.finale_origin_y - 84);
                CHECK(phase.finale_ring_hit_x[3] == phase.finale_origin_x - 168);
                CHECK(burst->width == 104 && burst->height == 134);
                sha256_init(&ctx);
                sha256_update(&ctx, burst->pixels, (size_t)burst->width * burst->height);
                sha256_final(&ctx, digest);
                sha256_hex(digest, hex);
                CHECK(!strcmp(hex, "b1a0a46b9b284735fe24de66105db2aef3b584f6d3df15304074dc74718e2c10"));
                CHECK(phase.finale_hit_x[6] == phase.finale_origin_x + 21);
                CHECK(phase.finale_hit_y[4] == phase.finale_origin_y - 21);
            }
            while (!phase.red_bolt.age && phase.frames < 120u) {
                saturn_pad_update_host(&input, phase.frames % 2 ? MB_PAD_B : 0);
                CHECK(special_phase_step(&phase, &input) == 0);
            }
            CHECK(phase.red_bolt.age && !phase.finale_charged && !phase.ring_shots);
            unsigned d = phase.red_bolt.dir;
            while (!phase.finale_charged && phase.red_bolt.age) {
                saturn_pad_update_host(&input, dirs[d] | MB_PAD_B);
                CHECK(special_phase_step(&phase, &input) == 0);
            }
            CHECK(phase.finale_charged && phase.ring_sfx_event == 0x2f);
            CHECK(phase.finale_hit_tick == phase.frames && phase.finale_hit_type == d + 4);
            CHECK(phase.skel.skeletism == energy && phase.hazard_hits == 0);
            saturn_pad_update_host(&input, dirs[d] | MB_PAD_B);
            CHECK(special_phase_step(&phase, &input) == 0 && phase.finale_charged);
            saturn_pad_update_host(&input, MB_PAD_A | MB_PAD_C);
            CHECK(special_phase_step(&phase, &input) == 0 && phase.finale_charged);
            CHECK(phase.finale_facing == d);
            for (unsigned pending = 0; pending < 2; ++pending) {
                saturn_pad_update_host(&input, MB_PAD_A | MB_PAD_C);
                CHECK(special_phase_step(&phase, &input) == 0 && phase.finale_charged);
            }
            CHECK(phase.finale_facing == 4 && phase.bones_x == x && phase.bones_y == y);
            /* A one-VBlank B pulse between reports never reaches CheckJoy. */
            saturn_pad_update_host(&input, 0);
            if (!(phase.frames & 1u)) CHECK(special_phase_step(&phase, &input) == 0);
            saturn_pad_update_host(&input, MB_PAD_B);
            CHECK(special_phase_step(&phase, &input) == 0);
            for (unsigned t = 0; t < 3; ++t) {
                saturn_pad_update_host(&input, 0);
                CHECK(special_phase_step(&phase, &input) == 0 && phase.finale_charged);
                CHECK(!phase.blue_bolts[0].age && !phase.blue_bolts[1].age);
            }
            /* Even the correct direction costs energy when the guitar is full. */
            do {
                uint32_t direction = phase.red_bolt.age ? dirs[phase.red_bolt.dir] : 0;
                saturn_pad_update_host(&input, direction);
                CHECK(special_phase_step(&phase, &input) == 0);
            } while (!phase.hazard_hits && phase.frames < 300);
            CHECK(!phase.finale_charged && phase.ring_sfx_event == 0x30);
            CHECK(phase.finale_hit_tick == phase.frames && phase.finale_hit_type == phase.red_bolt.dir);
            CHECK(phase.skel.skeletism == energy - 0x9999);
            /* Ymir damage 137: BONES changes color at 139, loses its S at 140. */
            for (unsigned age = 0; age < 5; ++age) {
                static const int skull_red[] = {150, 150, 247, 247, 245};
                const uint8_t *skull = phase.frame_rgba + 4*(320*14 + 252);
                const uint8_t *letter = phase.frame_rgba + 4*(320*13 + 312);
                CHECK(abs((int)skull[0] - skull_red[age]) <= 2);
                CHECK(age < 3 ? letter[0] > 140 : letter[0] < 32);
                saturn_pad_update_host(&input, 0);
                CHECK(special_phase_step(&phase, &input) == 0);
            }
            while (!phase.finale_charged && phase.frames < 450) {
                saturn_pad_update_host(&input, special_phase_autoplay_buttons(&phase, phase.frames));
                CHECK(special_phase_step(&phase, &input) == 0);
            }
            CHECK(phase.finale_charged);
            /* The same pulse in the acquired report fires, including when
             * the physical pad has already been released on publication. */
            saturn_pad_update_host(&input, 0);
            if (phase.frames & 1u) CHECK(special_phase_step(&phase, &input) == 0);
            saturn_pad_update_host(&input, MB_PAD_B);
            CHECK(special_phase_step(&phase, &input) == 0 && phase.finale_charged);
            saturn_pad_update_host(&input, 0);
            CHECK(special_phase_step(&phase, &input) == 0);
            CHECK(!phase.finale_charged && phase.blue_bolts[0].dir == 1 && phase.blue_bolts[0].age == 1);
            for (unsigned t = 1; t <= 20; ++t) {
                saturn_pad_update_host(&input, 0);
                CHECK(special_phase_step(&phase, &input) == 0);
                CHECK(phase.ring_shots == (t == 20));
            }
            CHECK(phase.ring_active_count == 15);
            while (!phase.movie_ok && phase.frames < 5000 && !special_phase_done(&phase)) {
                saturn_pad_update_host(&input, special_phase_autoplay_buttons(&phase, phase.frames));
                CHECK(special_phase_step(&phase, &input) == 0);
            }
            CHECK(phase.ring_cleared && phase.movie_ok);
            CHECK(special_phase_finale_defer_ending(&phase) != 0 && phase.movie_ok);
            CHECK(phase.state == SPECIAL_ST_PLAY && !phase.movie.playback_done);
            CHECK(phase.movie.cur == 1u && phase.finale_movie_preroll == 159);
            phase.movie.tick_external = 1;
            phase.movie.tick_us = 16683;
            /* A held Start is first polled at request+158 in DUKMOVIE. */
            saturn_pad_update_host(&input, MB_PAD_START);
            for (unsigned k = 1; k <= 157; ++k) {
                CHECK(special_phase_step(&phase, &input) == 0);
                CHECK(phase.state == SPECIAL_ST_PLAY && phase.movie.cur == 1u);
                CHECK(phase.movie_pcm_pending == (k == 157));
            }
            CHECK(special_phase_step(&phase, &input) == 0);
            CHECK(phase.state == SPECIAL_ST_FINISHED && phase.movie.playback_done);
            CHECK(phase.movie.cur == 1u && !phase.movie_pcm_pending);
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

    for (unsigned control=0; control<2; ++control) {
        saturn_pad tp={0};
        CHECK(!special_phase_load(&phase,&disc1,"/16BIGBNS",STAGE_KIND_BIGBONES));
        for (unsigned i=0;i<5000 && !special_phase_done(&phase);++i) {
            saturn_pad_update_host(&tp,control ? MB_PAD_RIGHT : 0);
            special_phase_step(&phase,&tp);
        }
        CHECK(phase.state == SPECIAL_ST_LOST && phase.parts_collected == 0);
        special_phase_free(&phase);
    }

    /* Etapa 2: Rhythm miss resets combo, 4-streak heals Skeletism. */
    CHECK(special_phase_load(&phase, &disc1, "/07GUITAR", STAGE_KIND_RHYTHM) == 0);
    {
        saturn_pad tp = {0};
        int32_t initial = phase.skel.skeletism;
        for (unsigned i=0; i<3; ++i) {
            saturn_pad_update_host(&tp, MB_PAD_A);
            special_phase_step(&phase, &tp);
        }
        CHECK(phase.misses == 1 && phase.combo == 0 && phase.skel.skeletism < initial);
        for (unsigned i = 0; i < 250 && phase.hits < 4; ++i) {
            saturn_pad_update_host(&tp, special_phase_autoplay_buttons(&phase, phase.frames));
            special_phase_step(&phase, &tp);
        }
        CHECK(phase.combo >= 4 && phase.rhythm_heals >= 4 && phase.skel.skeletism > initial);
    }
    special_phase_free(&phase);

    if (opened2) {
        CHECK(!special_phase_load(&phase,&disc2,"/25GENRM",STAGE_KIND_GENROOM));
        CHECK(phase.skel.skeletism == 0x8000);
        saturn_pad collision_pad={0};
        while (phase.hurt_timer) special_phase_step(&phase,&collision_pad);
        phase.enemies.count=1;
        stage_enemy *orb=&phase.enemies.ents[0];
        orb->active=1; orb->kind=STAGE_ENEMY_RED;
        orb->x=phase.bones_x; orb->y=phase.bones_y;
        special_phase_step(&phase,&collision_pad);
        CHECK(phase.skel.skeletism==0x8000-0xa00 && phase.plasma_collected==0);
        orb->active=1; orb->kind=STAGE_ENEMY_BLUE;
        special_phase_step(&phase,&collision_pad);
        CHECK(phase.skel.skeletism==0x8000-0xa00+0xf00 && phase.plasma_collected==1 && !orb->active);
        phase.enemies.defeated_count=2; phase.bones_x=phase.finish_x;
        special_phase_step(&phase,&collision_pad);
        CHECK(phase.state==SPECIAL_ST_PLAY);
        special_phase_free(&phase);
        for (unsigned control=0; control<2; ++control) {
            saturn_pad tp={0};
            CHECK(!special_phase_load(&phase,&disc2,"/25GENRM",STAGE_KIND_GENROOM));
            for (unsigned i=0;i<5000 && !special_phase_done(&phase);++i) {
                saturn_pad_update_host(&tp,control ? MB_PAD_RIGHT : 0);
                special_phase_step(&phase,&tp);
            }
            CHECK((phase.state==SPECIAL_ST_PLAY || phase.state==SPECIAL_ST_LOST) && !phase.plasma_collected);
            special_phase_free(&phase);
        }

        /* Etapa 4: Interactive FINALE arena (apply_hits=1, no free page skip,
         * autoplay dodge + directional 16-slot ring clear -> PlayDuckMovie(3)). */
        CHECK(special_phase_load(&phase, &disc2, "/27FINALE", STAGE_KIND_FINALE) == 0);
        CHECK(special_phase_finale_defer_ending(&phase) == 0);
        {
            saturn_pad tp = {0};
            unsigned f;
            for (f = 0; f < 600u && !special_phase_done(&phase); f++) {
                saturn_pad_update_host(&tp, 0u);
                special_phase_step(&phase, &tp);
                if (phase.frames == 37u) {
                    /* At x=57 the fully lit blue is 41 in both rows.
                     * Only the final scanline has received the -40 update. */
                    CHECK(phase.frame_rgba[4u*(222u*320u+57u)+2u] == 0u);
                    CHECK(phase.frame_rgba[4u*(223u*320u+57u)+2u] == 1u);
                }
                if (phase.frames == 135u) {
                    sha256_ctx ctx;
                    uint8_t digest[32];
                    char hex[65];
                    sha256_init(&ctx);
                    sha256_update(&ctx, phase.frame_rgba + 223u*320u*4u, 320u*4u);
                    sha256_final(&ctx, digest);
                    sha256_hex(digest, hex);
                    CHECK(!strcmp(hex, "546bd2d9ddd64cf76e98915cdb9e0f5cf5acc2587ed1090d3c285d63f58c6372"));
                }
            }
            CHECK(phase.state == SPECIAL_ST_LOST && phase.skel.died == 1);
            CHECK(phase.hazard_hits == 4u && phase.ring_shots == 0 && !phase.ring_cleared);
            for (size_t pixel = 0; pixel < 320u * 224u * 4u; pixel += 4)
                CHECK((phase.frame_rgba[pixel] | phase.frame_rgba[pixel+1] |
                       phase.frame_rgba[pixel+2]) == 0 && phase.frame_rgba[pixel+3] == 255);
            CHECK(phase.finale_end_tick && phase.frames - phase.finale_end_tick == 63u);
        }
        special_phase_free(&phase);

        CHECK(special_phase_load(&phase, &disc2, "/27FINALE", STAGE_KIND_FINALE) == 0);
        CHECK(special_phase_finale_defer_ending(&phase) == 0);
        {
            saturn_pad tp = {0};
            unsigned f;
            for (f = 0; f < 5000u && !phase.ring_cleared && !special_phase_done(&phase); f++) {
                uint32_t btn = special_phase_autoplay_buttons(&phase, f);
                saturn_pad_update_host(&tp, btn);
                special_phase_step(&phase, &tp);
            }
            CHECK(phase.ring_cleared && !phase.movie_ok && phase.finale_end_tick == f);
            CHECK(phase.hazard_hits == 0 && phase.ring_shots == 16);
            for (unsigned delay = 1; delay <= 63; ++delay) {
                saturn_pad_update_host(&tp, 0);
                CHECK(special_phase_step(&phase, &tp) == 0);
                CHECK(phase.movie_ok == (delay == 63));
            }
            CHECK(phase.ring_shots == 16 && phase.hazard_hits <= 1); /* airborne red during fade */
            printf("FINALE B/shield: clear at tick %u, film at %u, bolts=%u, hits=%u, shots=%u\n", f, phase.frames, phase.enemies.bolts_fired, phase.hazard_hits, phase.ring_shots);
            CHECK(phase.state == SPECIAL_ST_PLAY && !phase.skel.died);
            phase.movie.tick_external = 1;
            phase.movie.tick_us = 16683;
            for (unsigned wait = 1; wait <= 159; ++wait) {
                /* A pulse during the seeks has no effect on the later movie. */
                saturn_pad_update_host(&tp, wait == 100 ? MB_PAD_START : 0);
                CHECK(special_phase_step(&phase, &tp) == 0);
                CHECK(phase.finale_movie_preroll == 159 - wait && phase.movie.cur == 1);
                CHECK(phase.movie.elapsed_us == (wait <= 157 ? 0u : (wait - 157) * 16683u));
                CHECK(phase.movie_pcm_pending == (wait >= 157));
                if (wait == 158) {
                    for (size_t pixel = 0; pixel < 320u * 224u * 4u; pixel += 4)
                        CHECK(!(phase.frame_rgba[pixel] | phase.frame_rgba[pixel+1] |
                                phase.frame_rgba[pixel+2]) && phase.frame_rgba[pixel+3] == 255);
                }
            }
            CHECK(!memcmp(phase.frame_rgba, duck_avi_frame_rgba(&phase.movie), 320u * 224u * 4u));
            phase.movie.tick_us = 100000000u;
            saturn_pad_update_host(&tp, 0u);
            for (f = 0; f < 5u && phase.state == SPECIAL_ST_PLAY; f++)
                special_phase_step(&phase, &tp);
            CHECK(phase.state == SPECIAL_ST_FINISHED && phase.movie.playback_done);
            printf("FINALE Etapa4 interactive fight: shots=%u hits=%u skel=%d OK\n",
                   phase.ring_shots, phase.hazard_hits, phase.skel.skeletism);
        }
        special_phase_free(&phase);
    }

    cd_close(&disc1);
    if (opened2) cd_close(&disc2);
    printf("OK special_phase\n");
    return 0;
}
