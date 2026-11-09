#include "core/cdimage.h"
#include "core/identity.h"
#include "core/saturn_pad.h"
#include "core/sidescrl_phase.h"
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

static int play_to_finish(sidescrl_phase *ph) {
    saturn_pad pad = {0};
    int i;
    for (i = 0; i < 5000 && !sidescrl_phase_done(ph); i++) {
        saturn_pad_update_host(&pad, MB_PAD_RIGHT);
        sidescrl_phase_step(ph, &pad);
    }
    return ph->state == SIDESCRL_ST_FINISHED;
}

int main(void) {
    const char *d1 = getenv("MB_DISC1");
    const char *d2 = getenv("MB_DISC2");
    const char *man = getenv("MB_MANIFEST");
    cdimage disc1, disc2;
    sidescrl_phase phase;
    int opened2 = 0;

    if (!d1 || !man) {
        printf("SKIP sidescrl_phase (set MB_DISC1 and MB_MANIFEST)\n");
        return 0;
    }
    CHECK(open_disc(&disc1, d1, man, 1) == 0);
    if (d2 && open_disc(&disc2, d2, man, 2) == 0)
        opened2 = 1;

    CHECK(sidescrl_phase_load(&phase, &disc1, "/05MAUSO") == 0);
    CHECK(phase.layer_count > 0);
    CHECK(phase.textures_size > 0);
    CHECK(phase.frame_rgba != NULL);
    CHECK(phase.map.plane_count >= 1); /* §23 LoadCDB planes */
    CHECK(phase.map.strip_w >= 320);
    CHECK(scrl_map_find_floor(&phase.map, 40) > 0);
    CHECK(play_to_finish(&phase));
    printf("MAUSO: frames=%u layers=%u planes=%u %s\n", phase.frames,
           phase.layer_count, phase.map.plane_count, phase.status);
    sidescrl_phase_free(&phase);

    CHECK(sidescrl_phase_load(&phase, &disc1, "/06VALLEY") == 0);
    CHECK(phase.map.has_amap == 1 || phase.map.plane_count >= 1);
    CHECK(play_to_finish(&phase));
    printf("VALLEY: frames=%u amap=%u planes=%u %s\n", phase.frames,
           phase.map.amap_n, phase.map.plane_count, phase.status);
    sidescrl_phase_free(&phase);

    if (opened2) {
        CHECK(sidescrl_phase_load(&phase, &disc2, "/19VILLAGE") == 0);
        CHECK(play_to_finish(&phase));
        printf("VILLAGE: frames=%u layers=%u %s\n", phase.frames, phase.layer_count,
               phase.status);
        sidescrl_phase_free(&phase);

        CHECK(sidescrl_phase_load(&phase, &disc2, "/26LAFF") == 0);
        CHECK(play_to_finish(&phase));
        printf("LAFF: frames=%u %s\n", phase.frames, phase.status);
        sidescrl_phase_free(&phase);
    }

    cd_close(&disc1);
    if (opened2) cd_close(&disc2);
    printf("OK sidescrl_phase\n");
    return 0;
}
