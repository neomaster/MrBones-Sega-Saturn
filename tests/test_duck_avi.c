#include "core/cdimage.h"
#include "core/duck_avi.h"
#include "core/identity.h"
#include "core/resource.h"
#include "core/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

static unsigned read_le32(const uint8_t *p) {
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

static void write_le32(uint8_t *p, unsigned n) {
    p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8);
    p[2] = (uint8_t)(n >> 16); p[3] = (uint8_t)(n >> 24);
}

static int dump_frame_hashes(duck_avi *movie, const char *path) {
    FILE *out = fopen(path, "w");
    if (!out) return -1;
    while (!duck_avi_done(movie)) {
        unsigned size = movie->frames[movie->cur].size;
        sha256_ctx ctx;
        uint8_t digest[32];
        char hex[65];
        if (!duck_avi_step(movie)) { fclose(out); return -1; }
        if (!size) continue;
        sha256_init(&ctx);
        sha256_update(&ctx, movie->rgba, 320u * 224u * 4u);
        sha256_final(&ctx, digest);
        sha256_hex(digest, hex);
        if (fprintf(out, "%s\n", hex) < 0) { fclose(out); return -1; }
    }
    return fclose(out) == 0 ? 0 : -1;
}

int main(void) {
    {
        duck_avi invalid;
        const uint8_t header[] = {'R', 'I', 'F', 'F'};
        CHECK(duck_avi_open(&invalid, header, sizeof header) != 0);
        CHECK(strstr(invalid.status, "truncated header") != NULL);
    }
    {
        duck_avi timed = {0};
        duck_avi_frame frames[3] = {{0}};
        uint8_t pixel[4] = {0};
        timed.frames = frames;
        timed.frame_count = 3;
        timed.rgba = pixel;
        timed.us_per_frame = 100000;
        CHECK(duck_avi_tick(&timed, 250000) == 2);
        CHECK(timed.cur == 2 && timed.elapsed_us == 50000);
        CHECK(duck_avi_tick(&timed, 49000) == 0);
        CHECK(duck_avi_tick(&timed, 1000) == 1);
        CHECK(duck_avi_done(&timed));
        CHECK(!duck_avi_playback_done(&timed));
        CHECK(duck_avi_tick(&timed, 99999) == 0);
        CHECK(!duck_avi_playback_done(&timed));
        CHECK(duck_avi_tick(&timed, 1) == 0);
        CHECK(duck_avi_playback_done(&timed));
    }
    {
        duck_avi timed = {0};
        duck_avi_frame frames[3] = {{0}};
        uint8_t pixel[4] = {0};
        timed.frames = frames;
        timed.frame_count = 3;
        timed.rgba = pixel;
        timed.us_per_frame = 100000;
        timed.pcm_samples = 450;
        timed.pcm_rate = 1000;
        timed.pcm_channels = 1;
        CHECK(duck_avi_step(&timed) == 1);
        CHECK(duck_avi_tick(&timed, 200000) == 2 && duck_avi_done(&timed));
        CHECK(duck_avi_tick(&timed, 249999) == 0 && !duck_avi_playback_done(&timed));
        CHECK(duck_avi_tick(&timed, 1) == 0 && duck_avi_playback_done(&timed));
    }
    {
        duck_avi timed = {0};
        static duck_avi_frame frames[5658] = {{0}};
        uint8_t pixel[4] = {0};
        timed.frames = frames;
        timed.frame_count = 5658;
        timed.rgba = pixel;
        timed.us_per_frame = 66666;
        timed.frame_scale = 1;
        timed.frame_rate = 15;
        CHECK(duck_avi_tick(&timed, 377199999) == 5657);
        CHECK(duck_avi_tick(&timed, 1) == 1);
    }
    /* A matching delta signature near the end must not read past the blob. */
    {
        uint8_t truncated[224] = {0};
        const uint8_t signature[16] = {0,0, 255,254, 0,2, 255,250,
                                       0,6, 255,244, 0,12, 255,244};
        duck_tm1 tm;
        memcpy(truncated + 160, signature, sizeof signature);
        CHECK(duck_tm1_init(&tm, truncated, sizeof truncated) == 0);
        CHECK(tm.tables_from_duck == 0);
        duck_tm1_free(&tm);
    }
    const char *d1 = getenv("MB_DISC1");
    const char *man = getenv("MB_MANIFEST");
    char err[512];
    cdimage cd;
    id_manifest manifest;
    id_result result;
    void *avi = NULL, *duck = NULL;
    size_t avi_size = 0, duck_size = 0;
    duck_avi movie;
    unsigned stepped = 0;

    if (!d1 || !man) {
        printf("SKIP duck_avi\n");
        return 0;
    }
    CHECK(manifest_load(&manifest, man, err, sizeof err) == 0);
    CHECK(cd_open(&cd, d1, err, sizeof err) == 0);
    CHECK(identity_check(&manifest, &cd, 1, ID_QUICK, &result));
    manifest_free(&manifest);

    CHECK(resource_load(&cd, "/29INTRO/20AVI", &avi, &avi_size) == 0);
#ifdef _WIN32
    {
        uint8_t *bad = malloc(avi_size);
        size_t off;
        CHECK(bad != NULL);
        memcpy(bad, avi, avi_size);
        for (off = 12; off + 24 <= avi_size; off++)
            if (!memcmp(bad + off, "strf", 4) && bad[off + 8] == 0x62 &&
                bad[off + 9] == 0 && bad[off + 10] == 2 && bad[off + 11] == 0) break;
        CHECK(off + 24 <= avi_size);
        /* Interpret the copied payload as PCM to verify stream metadata, not samples. */
        bad[off + 8] = 1; bad[off + 9] = 0;
        bad[off + 12] = 0x00; bad[off + 13] = 0x7d; bad[off + 14] = 0; bad[off + 15] = 0;
        bad[off + 20] = 4; bad[off + 21] = 0;
        bad[off + 22] = 16; bad[off + 23] = 0;
        CHECK(duck_avi_open(&movie, bad, avi_size) == 0);
        CHECK(movie.pcm_channels == 2 && movie.pcm_rate == 32000 && movie.audio_block_align == 4);
        duck_avi_free(&movie);
        memcpy(bad, avi, avi_size);
        bad[off + 20] = 1; bad[off + 21] = 8; /* 2049: original chunks end mid-block */
        CHECK(duck_avi_open(&movie, bad, avi_size) != 0);
        memcpy(bad, avi, avi_size);
        {
            unsigned removed = 0;
            for (size_t j = 12; j + 4 <= avi_size; j++) {
                if (!memcmp(bad + j, "01wb", 4)) {
                    memcpy(bad + j, "JUNK", 4);
                    removed++;
                    j += 3;
                }
            }
            CHECK(removed > 0);
            CHECK(duck_avi_open(&movie, bad, avi_size) != 0);
            CHECK(strstr(movie.status, "audio stream has no samples") != NULL);
            for (off = 20; off + 4 <= avi_size; off++)
                if (!memcmp(bad + off - 20, "LIST", 4) &&
                    !memcmp(bad + off - 12, "strlstrh", 8) &&
                    !memcmp(bad + off, "auds", 4)) break;
            CHECK(off + 4 <= avi_size);
            memcpy(bad + off - 20, "JUNK", 4);
            CHECK(duck_avi_open(&movie, bad, avi_size) == 0);
            CHECK(movie.frame_count > 0 && movie.pcm_channels == 0 && movie.pcm_samples == 0);
            duck_avi_free(&movie);
        }
        free(bad);
    }
#endif
    CHECK(duck_avi_open(&movie, avi, avi_size) == 0);
    CHECK(movie.frame_count == 764u);
    CHECK(movie.frame_scale == 1 && movie.frame_rate == 15);
    /* The fifteenth interval ends at one second, not 999990 microseconds. */
    {
        duck_avi timed = {0};
        duck_avi_frame frames[15] = {{0}};
        uint8_t pixel[4] = {0};
        timed.frames = frames;
        timed.frame_count = 15;
        timed.rgba = pixel;
        timed.us_per_frame = 66666;
        timed.frame_scale = 1;
        timed.frame_rate = 15;
        CHECK(duck_avi_tick(&timed, 999999) == 14);
        CHECK(duck_avi_tick(&timed, 1) == 1);
    }
    {
        duck_avi timed = {0};
        duck_avi_frame frames[3] = {{0}};
        uint8_t pixel[4] = {0};
        timed.frames = frames;
        timed.frame_count = 3;
        timed.rgba = pixel;
        timed.us_per_frame = 66666;
        timed.frame_scale = 1;
        timed.frame_rate = 15;
        timed.pcm_samples = 250;
        timed.pcm_rate = 1000;
        timed.pcm_channels = 1;
        CHECK(duck_avi_step(&timed) == 1);
        CHECK(duck_avi_tick(&timed, 249999) == 2);
        CHECK(!duck_avi_playback_done(&timed));
        CHECK(duck_avi_tick(&timed, 1) == 0 && duck_avi_playback_done(&timed));
    }
    if (getenv("MB_DUCK_INTRO_HASH_OUT")) {
        CHECK(dump_frame_hashes(&movie, getenv("MB_DUCK_INTRO_HASH_OUT")) == 0);
        duck_avi_free(&movie);
        CHECK(duck_avi_open(&movie, avi, avi_size) == 0);
    }
    for (unsigned k = 0; k <= 90; k++) {
        CHECK(duck_avi_step(&movie) == 1);
#ifdef _WIN32
        if (k == 45) {
            sha256_ctx ctx;
            uint8_t digest[32];
            char hex[65];
            sha256_init(&ctx);
            sha256_update(&ctx, movie.rgba, 320u * 224u * 4u);
            sha256_final(&ctx, digest);
            sha256_hex(digest, hex);
            CHECK(strcmp(hex, "2e102dc68b5e1e950bdfdaf9e3c0268441ce5c1e8ee7d76bc6365560a6372603") == 0);
        }
#endif
    }
    if (getenv("MB_DUCK_INTRO_FRAME_OUT")) {
        FILE *out = fopen(getenv("MB_DUCK_INTRO_FRAME_OUT"), "wb");
        CHECK(out != NULL);
        CHECK(fwrite(movie.rgba, 1, 320u * 224u * 4u, out) == 320u * 224u * 4u);
        CHECK(fclose(out) == 0);
    }
#ifdef _WIN32
    {
        sha256_ctx ctx;
        uint8_t digest[32];
        char hex[65];
        static const unsigned frames[] = {120, 175, 240, 250, 365};
        static const char *const reference[] = {
            "c41dff39751a52646b147bfb7c96236cfc3a2ed85e8993b6f386ea8d7e12965c",
            "6d240cd339dd5943b79c3d3296f549884c03ecaed07d51a2e9fda43d19512791",
            "5116575aeb33d0c9bff802dc4333320b7cb2bf8e8a358a1c9835a60c4b4648b0",
            "63be8ff59b6e5d5cdc08b402ddb84079e8b41df3044c2925cfc53ac82737f9d3",
            "7869f83e753698a66b6bfd3167c16dd5c392fc0618328ae470a9dab334030765"
        };
        unsigned target = 0;
        for (unsigned k = 91; k <= 365; k++) {
            CHECK(duck_avi_step(&movie) == 1);
            if (k != frames[target]) continue;
            sha256_init(&ctx);
            sha256_update(&ctx, movie.rgba, 320u * 224u * 4u);
            sha256_final(&ctx, digest);
            sha256_hex(digest, hex);
            CHECK(strcmp(hex, reference[target]) == 0);
            target++;
        }
        CHECK(target == 5);
    }
#endif
    duck_avi_free(&movie);
    free(avi);
    avi = NULL;

    CHECK(resource_load(&cd, "/03MENU/20AVI", &avi, &avi_size) == 0);
    CHECK(duck_avi_open(&movie, avi, avi_size) == 0);
    CHECK(movie.frame_count > 2048);
    CHECK(movie.pcm_rate == 44100 && movie.pcm_channels == 2);
    CHECK((movie.audio_decoder && movie.pcm_samples > 0) ||
          (movie.audio_dk3_unsupported && movie.pcm_samples == 0));
#ifdef _WIN32
    {
        unsigned decoded = 0;
        while (!duck_avi_done(&movie)) {
            unsigned size = movie.frames[movie.cur].size;
            CHECK(duck_avi_step(&movie) == 1);
            if (!size) continue;
            decoded++;
            if (decoded == 2049 || decoded == 3085) {
                sha256_ctx hash;
                uint8_t digest[32];
                char hex[65];
                sha256_init(&hash);
                sha256_update(&hash, movie.rgba, 320u * 224u * 4u);
                sha256_final(&hash, digest);
                sha256_hex(digest, hex);
                CHECK(strcmp(hex, decoded == 2049
                    ? "d12def9f9cf37656d4afd3b59f7db6f3713a8b106a0ea868d0747a76242556dd"
                    : "9c1045b4d71cefabf84abc6414c9f8376ded15eff01646c0e49bb5f6910f721d") == 0);
            }
        }
        CHECK(decoded == 3085);
        duck_avi_free(&movie);
        CHECK(duck_avi_open(&movie, avi, avi_size) == 0);
    }
#endif
    if (getenv("MB_DUCK_MENU_HASH_OUT")) {
        CHECK(dump_frame_hashes(&movie, getenv("MB_DUCK_MENU_HASH_OUT")) == 0);
        {
            const uint8_t *border = movie.rgba + (50u * 320u + 10u) * 4u;
            CHECK(border[0] == 0 && border[1] == 0 && border[2] == 0 && border[3] == 255);
        }
    }
    if (getenv("MB_DUCK_MENU_LAST_OUT")) {
        FILE *out = fopen(getenv("MB_DUCK_MENU_LAST_OUT"), "wb");
        CHECK(out != NULL);
        CHECK(fwrite(movie.rgba, 1, 320u * 224u * 4u, out) == 320u * 224u * 4u);
        CHECK(fclose(out) == 0);
    }
    if (getenv("MB_DUCK_AUDIO_OUT") && movie.pcm) {
        FILE *out = fopen(getenv("MB_DUCK_AUDIO_OUT"), "wb");
        CHECK(out != NULL);
        CHECK(fwrite(movie.pcm, sizeof *movie.pcm, movie.pcm_samples, out) == movie.pcm_samples);
        CHECK(fclose(out) == 0);
    }
    if (getenv("MB_DUCK_FRAME_OUT")) {
        FILE *out;
        for (unsigned k = 0; k <= 30; k++) CHECK(duck_avi_step(&movie) == 1);
        out = fopen(getenv("MB_DUCK_FRAME_OUT"), "wb");
        CHECK(out != NULL);
        CHECK(fwrite(movie.rgba, 1, 320u * 224u * 4u, out) == 320u * 224u * 4u);
        CHECK(fclose(out) == 0);
    }
    duck_avi_free(&movie);
#ifdef _WIN32
    {
        static const uint8_t invalid[] = {0, 0, 0};
        CHECK(duck_avi_open(&movie, avi, avi_size) == 0);
        movie.frames[0].data = invalid;
        movie.frames[0].size = sizeof invalid;
        CHECK(duck_avi_tick(&movie, movie.us_per_frame + 1u) == -1);
        CHECK(movie.decode_error && duck_avi_playback_done(&movie));
        CHECK(strstr(movie.status, "invalid video frame") != NULL);
        duck_avi_free(&movie);
    }
#endif
    free(avi);
    avi = NULL;

    /* §22: LOSE 20AVI + 19DUCK real tables. */
    CHECK(resource_load(&cd, "/28LOSE/20AVI", &avi, &avi_size) == 0);
    CHECK(avi_size > 1000);
    {
        uint8_t *joined = malloc(avi_size);
        size_t movi, first, second;
        unsigned n1, n2;
        duck_avi original, grouped;
        CHECK(joined != NULL);
        memcpy(joined, avi, avi_size);
        for (movi = 12; movi + 12 < avi_size; movi++)
            if (!memcmp(joined + movi, "LIST", 4) && !memcmp(joined + movi + 8, "movi", 4)) break;
        CHECK(movi + 12 < avi_size);
        first = movi + 12;
        while (first + 20 < avi_size && !memcmp(joined + first, "LIST", 4) &&
               memcmp(joined + first + 12, "00dc", 4))
            first += 8u + read_le32(joined + first + 4);
        CHECK(first + 20 < avi_size && !memcmp(joined + first + 12, "00dc", 4));
        n1 = read_le32(joined + first + 4);
        second = first + 8u + n1;
        CHECK(!(n1 & 1u) && second + 20 < avi_size && !memcmp(joined + second, "LIST", 4) &&
              !memcmp(joined + second + 8, "rec ", 4) && !memcmp(joined + second + 12, "00dc", 4));
        n2 = read_le32(joined + second + 4);
        CHECK(!(n2 & 1u) && second + 8u + n2 <= avi_size);
        write_le32(joined + first + 4, n1 + n2 - 4u);
        write_le32(joined + movi + 4, read_le32(joined + movi + 4) - 12u);
        write_le32(joined + 4, read_le32(joined + 4) - 12u);
        memmove(joined + second, joined + second + 12, avi_size - second - 12);
        CHECK(duck_avi_open(&original, avi, avi_size) == 0);
        CHECK(duck_avi_open(&grouped, joined, avi_size - 12) == 0);
        CHECK(grouped.frame_count == original.frame_count && grouped.pcm_samples == original.pcm_samples);
        CHECK(memcmp(grouped.pcm, original.pcm, original.pcm_samples * sizeof *original.pcm) == 0);
        for (int k = 0; k < 5; k++) {
            CHECK(duck_avi_step(&original) == 1 && duck_avi_step(&grouped) == 1);
            CHECK(memcmp(original.rgba, grouped.rgba, 320u * 224u * 4u) == 0);
        }
        duck_avi_free(&original);
        duck_avi_free(&grouped);
        free(joined);
    }
    {
        uint8_t *bad = malloc(avi_size);
        size_t pos;
        CHECK(bad != NULL);
        memcpy(bad, avi, avi_size);
        CHECK(duck_avi_open(&movie, bad, avi_size - 1) != 0);
        CHECK(strstr(movie.status, "truncated") != NULL);
        for (pos = 12; pos + 20 < avi_size; pos++)
            if (!memcmp(bad + pos, "strh", 4) && !memcmp(bad + pos + 8, "vidsDUCK", 8)) break;
        CHECK(pos + 20 < avi_size);
        memcpy(bad + pos + 12, "BAD!", 4);
        CHECK(duck_avi_open(&movie, bad, avi_size) != 0);
        CHECK(strstr(movie.status, "unsupported or invalid stream") != NULL);
        memcpy(bad, avi, avi_size);
        for (pos = 12; pos + 24 < avi_size; pos++)
            if (!memcmp(bad + pos, "strf", 4) && bad[pos + 8] == 0x62 && bad[pos + 9] == 0) break;
        CHECK(pos + 24 < avi_size);
        bad[pos + 8] = 0x63;
        CHECK(duck_avi_open(&movie, bad, avi_size) != 0);
        CHECK(strstr(movie.status, "unsupported or invalid stream") != NULL);
        memcpy(bad, avi, avi_size);
        for (pos = 12; pos + 20 < avi_size; pos++)
            if (!memcmp(bad + pos, "LIST", 4) && !memcmp(bad + pos + 8, "movi", 4)) break;
        CHECK(pos + 20 < avi_size);
        pos += 12;
        if (!memcmp(bad + pos, "LIST", 4)) pos += 12;
        CHECK(pos + 8 < avi_size);
        memset(bad + pos + 4, 0xff, 4);
        CHECK(duck_avi_open(&movie, bad, avi_size) != 0);
        CHECK(strstr(movie.status, "invalid or truncated") != NULL);
        memcpy(bad, avi, avi_size);
        for (pos = 12; pos + 20 < avi_size; pos++)
            if (!memcmp(bad + pos, "LIST", 4) && !memcmp(bad + pos + 8, "movi", 4)) break;
        CHECK(pos + 40 < avi_size);
        pos += 12;
        CHECK(!memcmp(bad + pos, "LIST", 4) && !memcmp(bad + pos + 8, "rec ", 4));
        pos += 12;
        CHECK(!memcmp(bad + pos, "01wb", 4));
        {
            unsigned size = (unsigned)bad[pos + 4] | ((unsigned)bad[pos + 5] << 8) |
                            ((unsigned)bad[pos + 6] << 16) | ((unsigned)bad[pos + 7] << 24);
            unsigned junk = size - 10;
            CHECK(size >= 10 && !(size & 1) && pos + 8u + size <= avi_size);
            bad[pos + 4] = 1; bad[pos + 5] = bad[pos + 6] = bad[pos + 7] = 0;
            memcpy(bad + pos + 10, "JUNK", 4);
            bad[pos + 14] = (uint8_t)junk; bad[pos + 15] = (uint8_t)(junk >> 8);
            bad[pos + 16] = (uint8_t)(junk >> 16); bad[pos + 17] = (uint8_t)(junk >> 24);
            CHECK(duck_avi_open(&movie, bad, avi_size) != 0);
            CHECK(strstr(movie.status, "invalid or truncated") != NULL);
        }
#ifdef _WIN32
        memcpy(bad, avi, avi_size);
        bad[pos + 8 + 14] = 0xff; /* Invalid DK3 predictor step index. */
        CHECK(duck_avi_open(&movie, bad, avi_size) != 0);
        CHECK(strstr(movie.status, "invalid or truncated") != NULL);
#endif
        free(bad);
    }
    CHECK(resource_load(&cd, "/28LOSE/19DUCK", &duck, &duck_size) == 0);
    CHECK(duck_size == 134744u);
    CHECK(duck_avi_open_with_duck(&movie, avi, avi_size, duck, duck_size) == 0);
    if (getenv("MB_DUCK_LOSE_HASH_OUT")) {
        CHECK(dump_frame_hashes(&movie, getenv("MB_DUCK_LOSE_HASH_OUT")) == 0);
        duck_avi_free(&movie);
        CHECK(duck_avi_open_with_duck(&movie, avi, avi_size, duck, duck_size) == 0);
    }
    CHECK(movie.frame_count == 203);
    CHECK(movie.width >= 160 && movie.height >= 100);
#ifdef _WIN32
    CHECK(movie.video_decoder != NULL);
    CHECK(movie.audio_decoder != NULL);
#else
    CHECK(movie.video_decoder || (movie.tm1_ready && movie.tm1.tables_from_duck));
#endif
    if (getenv("MB_DUCK_LOSE_AUDIO_OUT") && movie.pcm) {
        FILE *out = fopen(getenv("MB_DUCK_LOSE_AUDIO_OUT"), "wb");
        CHECK(out != NULL);
        CHECK(fwrite(movie.pcm, sizeof *movie.pcm, movie.pcm_samples, out) == movie.pcm_samples);
        CHECK(fclose(out) == 0);
    }
    printf("LOSE avi: %ux%u frames=%u pcm=%zu @%u duck=%d %s\n", movie.width,
           movie.height, movie.frame_count, movie.pcm_samples, movie.pcm_rate,
           movie.tm1.tables_from_duck, movie.status);

    while (!duck_avi_done(&movie) && stepped < 64) {
        CHECK(duck_avi_step(&movie) == 1);
        CHECK(duck_avi_frame_rgba(&movie) != NULL);
        if (movie.cur == 49 && getenv("MB_DUCK_LOSE_48_OUT")) {
            FILE *out = fopen(getenv("MB_DUCK_LOSE_48_OUT"), "wb");
            CHECK(out != NULL);
            CHECK(fwrite(movie.rgba, 1, 320u * 224u * 4u, out) == 320u * 224u * 4u);
            CHECK(fclose(out) == 0);
        }
#ifdef _WIN32
        if (movie.cur == 32 || movie.cur == 40) {
            sha256_ctx ctx;
            uint8_t digest[32];
            char hex[65];
            sha256_init(&ctx);
            sha256_update(&ctx, movie.rgba, 320u * 224u * 4u);
            sha256_final(&ctx, digest);
            sha256_hex(digest, hex);
            /* Ymir F12, LOSE selected through the loader debugger. */
            CHECK(strcmp(hex, movie.cur == 32
                ? "aabea1344b7df0e75dc76d0de2bc562cfb60d5bcbdfd8ce90cc69ae13991e3d1"
                : "8ca7079c8f4176deb7d4c55916dccdc93af3c8947e3be7b3c9fad05f3f580db1") == 0);
        }
#endif
        stepped++;
    }
    CHECK(stepped >= 10);
    CHECK(movie.frames_drawn == stepped);
    CHECK(movie.tm1_frames + movie.soft_frames == stepped);
    CHECK(movie.tm1_frames > 0); /* §22 real TM1 path exercised */
    if (getenv("MB_DUCK_LOSE_FRAME_OUT")) {
        FILE *out = fopen(getenv("MB_DUCK_LOSE_FRAME_OUT"), "wb");
        CHECK(out != NULL);
        CHECK(fwrite(movie.rgba, 1, 320u * 224u * 4u, out) == 320u * 224u * 4u);
        CHECK(fclose(out) == 0);
    }
#ifdef _WIN32
    while (movie.cur < 94) CHECK(duck_avi_step(&movie) == 1);
    {
        sha256_ctx ctx;
        uint8_t digest[32];
        char hex[65];
        sha256_init(&ctx);
        sha256_update(&ctx, movie.rgba, 320u * 224u * 4u);
        sha256_final(&ctx, digest);
        sha256_hex(digest, hex);
        CHECK(strcmp(hex, "1005d8174a6f2a9edad1376c828d341ab473bb56074ae5cf3e00ecd42e532d97") == 0);
    }
#endif
    if (getenv("MB_DUCK_LOSE_LATE_OUT")) {
        FILE *out;
        while (movie.cur < 100) CHECK(duck_avi_step(&movie) == 1);
        out = fopen(getenv("MB_DUCK_LOSE_LATE_OUT"), "wb");
        CHECK(out != NULL);
        CHECK(fwrite(movie.rgba, 1, 320u * 224u * 4u, out) == 320u * 224u * 4u);
        CHECK(fclose(out) == 0);
    }
    while (movie.cur < 101) CHECK(duck_avi_step(&movie) == 1);
#ifdef _WIN32
    {
        sha256_ctx ctx;
        uint8_t digest[32];
        char hex[65];
        sha256_init(&ctx);
        sha256_update(&ctx, movie.rgba, 320u * 224u * 4u);
        sha256_final(&ctx, digest);
        sha256_hex(digest, hex);
        CHECK(strcmp(hex, "811a21a28045551ab6fdb3a94f27d5aee5043f0ba626f166295fc2e5813444b0") == 0);
    }
#endif
    while (movie.cur < 106) CHECK(duck_avi_step(&movie) == 1);
    {
        const uint8_t *border = movie.rgba + (12u * 320u + 319u) * 4u;
        CHECK(border[0] == 0 && border[1] == 0 && border[2] == 0 && border[3] == 255);
    }
    {
        uint8_t saved[320u * 224u * 4u];
        memcpy(saved, movie.rgba, sizeof saved);
        while (!duck_avi_done(&movie)) CHECK(duck_avi_step(&movie) == 1);
        CHECK(memcmp(saved, movie.rgba, sizeof saved) == 0);
    }
    duck_avi_free(&movie);
    free(avi);
    free(duck);
    avi = duck = NULL;

    /* GUITAR 20AVI + 19DUCK (same 134744 blob family). */
    CHECK(resource_load(&cd, "/07GUITAR/20AVI", &avi, &avi_size) == 0);
    CHECK(resource_load(&cd, "/07GUITAR/19DUCK", &duck, &duck_size) == 0);
    CHECK(duck_size == 134744u);
    CHECK(duck_avi_open_with_duck(&movie, avi, avi_size, duck, duck_size) == 0);
    CHECK(movie.frame_count > 0);
#ifdef _WIN32
    CHECK(movie.video_decoder != NULL);
#else
    CHECK(movie.video_decoder || movie.tm1.tables_from_duck == 1);
#endif
    CHECK(duck_avi_step(&movie) == 1);
    printf("GUITAR avi: frames=%u tm1=%u soft=%u %s\n", movie.frame_count,
           movie.tm1_frames, movie.soft_frames, movie.status);
    duck_avi_free(&movie);
    free(avi);
    free(duck);

    /* Soft open without 19DUCK still works (built-in tables). */
    CHECK(resource_load(&cd, "/28LOSE/20AVI", &avi, &avi_size) == 0);
    CHECK(duck_avi_open(&movie, avi, avi_size) == 0);
    CHECK(duck_avi_step(&movie) == 1);
    duck_avi_free(&movie);
    free(avi);

    cd_close(&cd);
    printf("OK duck_avi\n");
    return 0;
}
