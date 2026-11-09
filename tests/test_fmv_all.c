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
            while (!duck_avi_done(&movie)) {
                if (!duck_avi_step(&movie)) {
                    fprintf(stderr, "disc %u %s: %s\n", d + 1, list.paths[i], movie.status);
                    duck_avi_free(&movie); free(blob); cd_close(&cd); return 1;
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
