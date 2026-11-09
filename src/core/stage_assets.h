#ifndef MB_STAGE_ASSETS_H
#define MB_STAGE_ASSETS_H
#include "cdimage.h"
#include <stddef.h>

/*
 * Generalized runway_assets (§14/§15): load 02TITL/03CODE/04STXT/05SND1/15PAN
 * plus optional 19DUCK/20AVI (DUKMOVIE / LOSE).
 */

typedef struct {
    char iso_dir[32];
    void *title;
    size_t title_size;
    void *code;
    size_t code_size;
    void *textures;
    size_t textures_size;
    void *sound;
    size_t sound_size;
    void *pan;
    size_t pan_size;
    void *duck;
    size_t duck_size;
    void *avi;
    size_t avi_size;
    int is_stub; /* 03CODE ≤ STAGE_STUB_CODE_MAX (0 CODE → stub unless AVI) */
} stage_assets;

/* Load from iso_dir. allow_avi_only=1 → OK with 20AVI and no 03CODE (LOSE). */
int stage_assets_load(cdimage *cd, const char *iso_dir, stage_assets *out);
int stage_assets_load_ex(cdimage *cd, const char *iso_dir, stage_assets *out,
                         int allow_avi_only);
void stage_assets_free(stage_assets *assets);

int stage_assets_probe(cdimage *cd, const char *iso_dir,
                       size_t *code_size, size_t *pan_size, int *is_stub);

#endif
