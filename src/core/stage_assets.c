#include "stage_assets.h"
#include "stage_catalog.h"
#include "resource.h"
#include "snd1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void stage_assets_free(stage_assets *a) {
    if (!a) return;
    free(a->title);
    free(a->code);
    free(a->textures);
    free(a->sound);
    free(a->pan);
    free(a->duck);
    free(a->avi);
    memset(a, 0, sizeof *a);
}

static int load_opt(cdimage *cd, const char *dir, const char *file,
                    void **data, size_t *size) {
    char path[96];
    snprintf(path, sizeof path, "%s/%s", dir, file);
    if (resource_load(cd, path, data, size) == 0)
        return 0;
    *data = NULL;
    *size = 0;
    return -1;
}

int stage_assets_load_ex(cdimage *cd, const char *iso_dir, stage_assets *out,
                         int allow_avi_only) {
    snd1_span spans[SND1_SPAN_COUNT];
    if (!cd || !iso_dir || !out) return -1;
    memset(out, 0, sizeof *out);
    snprintf(out->iso_dir, sizeof out->iso_dir, "%s", iso_dir);

    (void)load_opt(cd, iso_dir, "03CODE", &out->code, &out->code_size);
    (void)load_opt(cd, iso_dir, "02TITL", &out->title, &out->title_size);
    (void)load_opt(cd, iso_dir, "04STXT", &out->textures, &out->textures_size);
    (void)load_opt(cd, iso_dir, "05SND1", &out->sound, &out->sound_size);
    (void)load_opt(cd, iso_dir, "15PAN", &out->pan, &out->pan_size);
    (void)load_opt(cd, iso_dir, "19DUCK", &out->duck, &out->duck_size);
    (void)load_opt(cd, iso_dir, "20AVI", &out->avi, &out->avi_size);

    if (out->code_size == 0) {
        if (allow_avi_only && out->avi_size > 0) {
            out->is_stub = 0;
            return 0;
        }
        stage_assets_free(out);
        return -1;
    }
    out->is_stub = out->code_size <= STAGE_STUB_CODE_MAX;
    if (out->sound_size > 0 && snd1_parse(out->sound, out->sound_size, spans)) {
        /* Non-fatal. */
    }
    return 0;
}

int stage_assets_load(cdimage *cd, const char *iso_dir, stage_assets *out) {
    return stage_assets_load_ex(cd, iso_dir, out, 0);
}

int stage_assets_probe(cdimage *cd, const char *iso_dir,
                       size_t *code_size, size_t *pan_size, int *is_stub) {
    stage_assets a;
    if (stage_assets_load_ex(cd, iso_dir, &a, 1)) return -1;
    if (code_size) *code_size = a.code_size;
    if (pan_size) *pan_size = a.pan_size;
    if (is_stub) *is_stub = a.is_stub;
    stage_assets_free(&a);
    return 0;
}
