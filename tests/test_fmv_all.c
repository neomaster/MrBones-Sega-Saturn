#include "core/cdimage.h"
#include "core/duck_avi.h"
#include "core/resource.h"
#include "core/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MOVIES 64
struct movies { char paths[MAX_MOVIES][256]; unsigned count; };

static int collect(const cd_iso_entry *e, void *context) {
    struct movies *list = context;
    size_t len = strlen(e->path);
    if (!e->is_dir && e->size && len >= 6 && !strcmp(e->path + len - 6, "/20AVI")) {
        if (list->count >= MAX_MOVIES) return -1;
        snprintf(list->paths[list->count++], sizeof list->paths[0], "%s", e->path);
    }
    return 0;
}

int main(void) {
    const char *discs[] = {getenv("MB_DISC1"), getenv("MB_DISC2")};
    unsigned total = 0;
    for (unsigned d = 0; d < 2; d++) {
        cdimage cd;
        char error[512];
        struct movies list = {0};
        if (!discs[d]) { fprintf(stderr, "MB_DISC%u missing\n", d + 1); return 1; }
        if (cd_open(&cd, discs[d], error, sizeof error)) { fprintf(stderr, "%s\n", error); return 1; }
        if (cd_iso_walk(&cd, collect, &list)) { fprintf(stderr, "ISO walk failed\n"); cd_close(&cd); return 1; }
        for (unsigned i = 0; i < list.count; i++) {
            void *blob = NULL;
            size_t size = 0;
            duck_avi movie;
            if (resource_load(&cd, list.paths[i], &blob, &size)) {
                fprintf(stderr, "disc %u %s: read failed\n", d + 1, list.paths[i]); cd_close(&cd); return 1;
            }
            if (duck_avi_open(&movie, blob, size)) {
                fprintf(stderr, "disc %u %s: %s\n", d + 1, list.paths[i], movie.status);
                free(blob); cd_close(&cd); return 1;
            }
            if (!movie.frame_count || !movie.pcm_samples || movie.pcm_channels != 2) {
                fprintf(stderr, "disc %u %s: missing video or stereo audio\n", d + 1, list.paths[i]);
                duck_avi_free(&movie); free(blob); cd_close(&cd); return 1;
            }
            {
                uint64_t video_us = (uint64_t)movie.frame_count * movie.us_per_frame;
                uint64_t audio_us = (uint64_t)movie.pcm_samples * 1000000u /
                                    movie.pcm_channels / movie.pcm_rate;
                uint64_t diff = video_us > audio_us ? video_us - audio_us : audio_us - video_us;
                if (diff > (uint64_t)movie.us_per_frame * 2u) {
                    fprintf(stderr, "disc %u %s: durations differ by %llu us\n", d + 1,
                            list.paths[i], (unsigned long long)diff);
                    duck_avi_free(&movie); free(blob); cd_close(&cd); return 1;
                }
            }
            if (d == 1 && !strcmp(list.paths[i], "/27FINALE/20AVI")) {
                sha256_ctx hash;
                uint8_t before[32], after[32];
                if (movie.frame_count < 2 || movie.frames[1].size != 15 ||
                    (movie.frames[1].data[1] ^ movie.frames[1].data[2]) != 0 ||
                    !duck_avi_step(&movie)) {
                    fprintf(stderr, "FINALE: missing initial repeat interval\n");
                    return 1;
                }
                sha256_init(&hash); sha256_update(&hash, movie.rgba, 320u * 224u * 4u);
                sha256_final(&hash, before);
                {
                    uint8_t invalid[15];
                    memcpy(invalid, movie.frames[1].data, sizeof invalid);
                    invalid[6] ^= 1; /* A repeat cannot change its dimensions. */
                    movie.frames[1].data = invalid;
                    if (duck_avi_step(&movie) || !movie.decode_error) {
                        fprintf(stderr, "FINALE: invalid repeat dimensions accepted\n");
                        return 1;
                    }
                    duck_avi_free(&movie);
                    if (duck_avi_open(&movie, blob, size) || !duck_avi_step(&movie)) return 1;
                }
                if (!duck_avi_step(&movie)) return 1;
                sha256_init(&hash); sha256_update(&hash, movie.rgba, 320u * 224u * 4u);
                sha256_final(&hash, after);
                if (memcmp(before, after, sizeof before)) {
                    fprintf(stderr, "FINALE: repeat interval erased the previous picture\n");
                    return 1;
                }
            }
            while (!duck_avi_done(&movie)) {
                unsigned interval = movie.cur;
                if (!duck_avi_step(&movie)) {
                    fprintf(stderr, "disc %u %s: %s\n", d + 1, list.paths[i], movie.status);
                    duck_avi_free(&movie); free(blob); cd_close(&cd); return 1;
                }
                {
                    const char *expected = NULL;
                    if (d == 0 && !strcmp(list.paths[i], "/06VALLEY/20AVI"))
                        expected = interval == 228 ? "653977c30018f5657e209f483436f56fce4a4985db8076c0352733af11ef0c37" :
                                   interval == 401 ? "c923491e4e5603fbdac3c7fb2c1885564d7e2f167096256bd67bfed64493a69d" :
                                   interval == 837 ? "1e578e67e575ed5f4a1fff7abfc0016cfe562d7590307a7a866169c22d371b42" : NULL;
                    else if (d == 1 && !strcmp(list.paths[i], "/27FINALE/20AVI"))
                        expected = interval == 2653 ? "205f46bcc8e94c4d4a399b7e3bde621bfd49f1b55cbda9533b59fdd6dbe872d3" :
                                   interval == 3954 ? "f5e3b5d5e996d6dadf51fcd23c3feb7adf03775afd7968c8b4699588f48d977f" :
                                   interval == 5038 ? "e9ab38a026ea3969b377c3022a03bc79888857a33ee295f3b5f461943a0ffcb0" : NULL;
                    if (expected) {
                        sha256_ctx hash;
                        uint8_t digest[32];
                        char hex[65];
                        sha256_init(&hash); sha256_update(&hash, movie.rgba, 320u * 224u * 4u);
                        sha256_final(&hash, digest); sha256_hex(digest, hex);
                        if (strcmp(hex, expected)) {
                            fprintf(stderr, "%s: interval %u differs from Ymir\n", list.paths[i], interval);
                            return 1;
                        }
                    }
                }
            }
            {
                sha256_ctx hash;
                uint8_t digest[32];
                char hex[65];
                sha256_init(&hash);
                sha256_update(&hash, movie.pcm, movie.pcm_samples * sizeof *movie.pcm);
                sha256_final(&hash, digest);
                sha256_hex(digest, hex);
                printf("disc %u %s: %u intervals, %zu audio samples, %u Hz, pcm_sha256=%s\n", d + 1,
                       list.paths[i], movie.frame_count, movie.pcm_samples, movie.pcm_rate, hex);
            }
            fflush(stdout);
            total++;
            duck_avi_free(&movie);
            free(blob);
        }
        cd_close(&cd);
        if (!list.count) { fprintf(stderr, "disc %u contains no movies\n", d + 1); return 1; }
    }
    printf("OK %u FMVs decoded\n", total);
    return 0;
}
