#include "runway_assets.h"
#include "resource.h"
#include "snd1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void runway_assets_free(runway_assets *assets) {
    if (!assets) return;
    free(assets->title);
    free(assets->textures);
    free(assets->code);
    free(assets->sound);
    memset(assets, 0, sizeof *assets);
}

static int is_runway_dir(const char *iso_dir) {
    return iso_dir && (strcmp(iso_dir, "/04RUNWAY") == 0 ||
                       strstr(iso_dir, "RUNWAY") != NULL);
}

int runway_assets_load_dir(cdimage *cd, const char *iso_dir, runway_assets *assets) {
    static const unsigned char title_start[16] = {
        0x25, 0xc6, 0xa2, 0xa0, 0, 0, 0, 0, 0, 0, 0, 0, 0x25, 0xc6, 0xa3, 0x30
    };
    char path[96];
    snd1_span spans[SND1_SPAN_COUNT];
    int strict;
    if (!assets || !iso_dir) return -1;
    memset(assets, 0, sizeof *assets);
    if (!cd) return -1;
    strict = is_runway_dir(iso_dir);

    snprintf(path, sizeof path, "%s/03CODE", iso_dir);
    if (resource_load(cd, path, &assets->code, &assets->code_size) ||
        assets->code_size == 0)
        goto fail;
    snprintf(path, sizeof path, "%s/04STXT", iso_dir);
    if (resource_load(cd, path, &assets->textures, &assets->textures_size) ||
        assets->textures_size == 0)
        goto fail;
    snprintf(path, sizeof path, "%s/02TITL", iso_dir);
    if (resource_load(cd, path, &assets->title, &assets->title_size)) {
        if (strict) goto fail;
        assets->title = NULL;
        assets->title_size = 0;
    }
    snprintf(path, sizeof path, "%s/05SND1", iso_dir);
    if (resource_load(cd, path, &assets->sound, &assets->sound_size)) {
        if (strict) goto fail;
        assets->sound = NULL;
        assets->sound_size = 0;
    }

    if (strict) {
        if (assets->title_size != 0x13600 || assets->textures_size != 0x56110 ||
            assets->code_size != 690052 || assets->sound_size != 467448 ||
            !assets->title ||
            memcmp(assets->title, title_start, sizeof title_start) ||
            snd1_parse(assets->sound, assets->sound_size, spans))
            goto fail;
    } else if (assets->sound_size > 0) {
        (void)snd1_parse(assets->sound, assets->sound_size, spans);
    }
    return 0;
fail:
    runway_assets_free(assets);
    return -1;
}

int runway_assets_load(cdimage *cd, runway_assets *assets) {
    return runway_assets_load_dir(cd, "/04RUNWAY", assets);
}
