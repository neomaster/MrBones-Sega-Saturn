#include "core/cdimage.h"
#include "core/identity.h"
#include "core/rhythm_table.h"
#include "core/runway_hazard.h"
#include "core/runway_skel.h"
#include "core/saturn_pad.h"
#include "core/special_phase.h"
#include "core/stage_enemy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c)                                                               \
    do {                                                                       \
        if (!(c)) {                                                            \
            printf("FAIL %d: %s\n", __LINE__, #c);                             \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int test_rhythm_tables(void) {
    const rhythm_table *drum = rhythm_table_for_stage("DRUM");
    const rhythm_table *guitar = rhythm_table_for_stage("GUITAR");
    unsigned i;

    CHECK(drum != 0);
    CHECK(guitar != 0);
    CHECK(drum->period == 12u);
    CHECK(guitar->period == 16u);
    CHECK(drum->count >= 32u);
    CHECK(guitar->count >= 16u);
    CHECK(drum->notes[0].time == 0u);
    CHECK(drum->notes[0].kind == 0u);
    CHECK(drum->notes[1].kind == 3u);
    CHECK(drum->notes[2].time == 22u);
    CHECK(drum->notes[2].kind == 2u);
    CHECK(strstr(drum->source, "26b58c") != 0);
    CHECK(rhythm_kind_button_mask(0) == 0x0400u);
    CHECK(rhythm_kind_button_mask(1) == 0x0100u);
    CHECK(rhythm_kind_button_mask(2) == 0x0200u);
    CHECK(rhythm_kind_button_mask(3) == 0x8000u);
    CHECK(rhythm_buttons_match(0, 0x0400u) == 1);
    CHECK(rhythm_buttons_match(1, 0x8000u) == 1); /* RIGHT universal */
    for (i = 0; i < drum->count; i++)
        CHECK(drum->notes[i].kind <= 3u);
    for (i = 0; i < guitar->count; i++)
        CHECK(guitar->notes[i].kind <= 3u);
    printf("rhythm tables: drum=%u guitar=%u period %u/%u OK\n", drum->count,
           guitar->count, drum->period, guitar->period);
    return 0;
}

static int test_runway_enemies(void) {
    stage_enemies e;
    runway_skel skel;
    runway_hazards haz;
    unsigned i;

    runway_skel_init(&skel);
    runway_hazards_init_runway(&haz, 2341u);
    stage_enemies_init_runway(&e, 2341u);
    CHECK(e.count >= 4u);
    CHECK(stage_enemies_count_kind(&e, STAGE_ENEMY_GANG) >= 2u);
    CHECK(stage_enemies_count_kind(&e, STAGE_ENEMY_LORD) >= 1u);
    CHECK(stage_enemies_count_kind(&e, STAGE_ENEMY_POOF_RED) >= 1u);
    CHECK(stage_enemies_count_kind(&e, STAGE_ENEMY_POOF_BLUE) >= 1u);

    for (i = 0; i < 120u; i++)
        (void)stage_enemies_step(&e, &skel, 128u * 2341u, &haz, 1);
    CHECK(e.throws_spawned >= 1u);
    printf("runway enemies: count=%u throws=%u hits=%u OK\n", e.count,
           e.throws_spawned, e.hits_applied);
    return 0;
}

static int test_sidescrl_poofs(void) {
    stage_enemies e;
    runway_skel skel;
    unsigned i;

    runway_skel_init(&skel);
    stage_enemies_init_sidescrl(&e, "BAT1", 2341u);
    CHECK(e.count >= 4u); /* denser on BAT* */
    stage_enemies_init_sidescrl(&e, "MAUSO", 2341u);
    CHECK(e.count == 2u);
    for (i = 0; i < 40u; i++)
        (void)stage_enemies_step(&e, &skel, 95u * 2341u, 0, 1);
    CHECK(e.hits_applied >= 1u || skel.hit_cooldown > 0 || skel.skeletism < RUNWAY_SKEL_MAX);
    printf("sidescrl poofs: MAUSO=%u hits=%u OK\n", e.count, e.hits_applied);
    return 0;
}

static int test_special_enemies(void) {
    stage_enemies gen, fin, big;
    runway_skel skel;
    unsigned i;

    runway_skel_init(&skel);
    stage_enemies_init_special(&gen, "GENRM", 2341u);
    CHECK(stage_enemies_count_kind(&gen, STAGE_ENEMY_RED) >= 1u);
    CHECK(stage_enemies_count_kind(&gen, STAGE_ENEMY_BLUE) >= 1u);
    CHECK(stage_enemies_count_kind(&gen, STAGE_ENEMY_ZAP) >= 1u);
    for (i = 0; i < 40u; i++)
        (void)stage_enemies_step(&gen, &skel, 110u * 2341u, 0, 1);
    CHECK(gen.zaps_fired >= 1u);

    stage_enemies_init_special(&fin, "FINALE", 2341u);
    CHECK(stage_enemies_count_kind(&fin, STAGE_ENEMY_RING) >= 1u);
    CHECK(stage_enemies_count_kind(&fin, STAGE_ENEMY_BOLT) >= 1u);
    for (i = 0; i < 80u; i++)
        (void)stage_enemies_step(&fin, &skel, 200u * 2341u, 0, 0); /* fx only */
    CHECK(fin.rings_fired >= 1u);
    CHECK(fin.bolts_fired >= 1u);
    CHECK(skel.died == 0); /* PRESENT path must not kill */

    stage_enemies_init_special(&big, "BIGBNS", 2341u);
    CHECK(stage_enemies_count_kind(&big, STAGE_ENEMY_LORD) >= 1u);
    CHECK(stage_enemies_count_kind(&big, STAGE_ENEMY_GANG) >= 1u);

    printf("special enemies: gen zaps=%u fin rings=%u bolts=%u big=%u OK\n",
           gen.zaps_fired, fin.rings_fired, fin.bolts_fired, big.count);
    return 0;
}

static int open_disc(cdimage *cd, const char *path, const char *man_path,
                     int disc_no) {
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

static int test_special_phase_wired(void) {
    const char *d1 = getenv("MB_DISC1");
    const char *d2 = getenv("MB_DISC2");
    const char *man = getenv("MB_MANIFEST");
    cdimage disc1, disc2;
    special_phase phase;
    saturn_pad pad = {0};
    int i, opened2 = 0;

    if (!d1 || !man) {
        printf("SKIP special_phase wire (set MB_DISC1/MB_MANIFEST)\n");
        return 0;
    }
    CHECK(open_disc(&disc1, d1, man, 1) == 0);
    /* Each walk frame must load from the supplied disc, not cycle a partial cache. */
    stage_enemies_load_disc_sprites(&disc1);
    {
        static uint8_t pixels[320u * 224u * 4u];
        uint32_t hashes[20];
        for (unsigned frame = 0; frame < 20; frame++) {
            uint32_t hash = 2166136261u;
            memset(pixels, 0, sizeof pixels);
            stage_enemies_draw_bones(pixels, 320, 224, 160, 150, 1, 0, 0, frame + 1);
            for (size_t k = 0; k < sizeof pixels; k++) hash = (hash ^ pixels[k]) * 16777619u;
            for (unsigned prior = 0; prior < frame; prior++) CHECK(hash != hashes[prior]);
            hashes[frame] = hash;
        }
    }
    if (d2 && open_disc(&disc2, d2, man, 2) == 0) opened2 = 1;

    CHECK(special_phase_load(&phase, &disc1, "/14DRUM", STAGE_KIND_RHYTHM) == 0);
    CHECK(phase.rhythm != 0);
    CHECK(phase.rhythm->count >= 32u);
    CHECK(phase.beat_period == 12u);
    for (i = 0; i < 400 && !special_phase_done(&phase); i++) {
        unsigned btn = MB_PAD_RIGHT;
        if ((i % (int)phase.beat_period) <= 2) btn |= MB_PAD_A;
        saturn_pad_update_host(&pad, btn);
        special_phase_step(&phase, &pad);
    }
    CHECK(phase.state == SPECIAL_ST_FINISHED);
    printf("DRUM wired: hits=%u notes=%u %s\n", phase.hits,
           phase.rhythm->count, phase.status);
    special_phase_free(&phase);

    CHECK(special_phase_load(&phase, &disc1, "/07GUITAR", STAGE_KIND_RHYTHM) ==
          0);
    CHECK(phase.rhythm != 0);
    CHECK(phase.beat_period == 16u);
    special_phase_free(&phase);

    CHECK(special_phase_load(&phase, &disc1, "/16BIGBNS",
                             STAGE_KIND_BIGBONES) == 0);
    CHECK(phase.enemies.count >= 2u);
    special_phase_free(&phase);

    if (opened2) {
        CHECK(special_phase_load(&phase, &disc2, "/25GENRM",
                                 STAGE_KIND_GENROOM) == 0);
        CHECK(stage_enemies_count_kind(&phase.enemies, STAGE_ENEMY_RED) >= 1u);
        CHECK(stage_enemies_count_kind(&phase.enemies, STAGE_ENEMY_ZAP) >= 1u);
        special_phase_free(&phase);

        CHECK(special_phase_load(&phase, &disc2, "/27FINALE",
                                 STAGE_KIND_FINALE) == 0);
        CHECK(stage_enemies_count_kind(&phase.enemies, STAGE_ENEMY_RING) >= 1u);
        for (i = 0; i < 60 && !special_phase_done(&phase); i++) {
            saturn_pad_update_host(&pad, MB_PAD_RIGHT);
            special_phase_step(&phase, &pad);
        }
        CHECK(phase.enemies.rings_fired >= 1u || phase.enemies.bolts_fired >= 1u);
        special_phase_free(&phase);
        cd_close(&disc2);
    }

    cd_close(&disc1);
    printf("special_phase wire OK\n");
    return 0;
}

int main(void) {
    if (test_rhythm_tables()) return 1;
    if (test_runway_enemies()) return 1;
    if (test_sidescrl_poofs()) return 1;
    if (test_special_enemies()) return 1;
    if (test_special_phase_wired()) return 1;
    printf("OK rhythm_enemies\n");
    return 0;
}
