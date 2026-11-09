#ifndef MB_RUNWAY_ASSETS_H
#define MB_RUNWAY_ASSETS_H
#include "cdimage.h"

typedef struct {
    void *title;
    size_t title_size;
    void *textures;
    size_t textures_size;
    void *code;
    size_t code_size;
    void *sound;
    size_t sound_size;
} runway_assets;

/* Valid for USA V1.000 /04RUNWAY sizes; free before reloading. */
int runway_assets_load(cdimage *cd, runway_assets *assets);
/* Same as load, paths under `iso_dir` (e.g. "/04RUNWAY"). Strict size check only for RUNWAY. */
int runway_assets_load_dir(cdimage *cd, const char *iso_dir, runway_assets *assets);
void runway_assets_free(runway_assets *assets);
#endif
