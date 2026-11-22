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
    CHECK(drum && guitar);
    CHECK(strstr(drum->source, "26b58c"));
    CHECK(rhythm_kind_button_mask(0) == 0x1000u);
    CHECK(rhythm_kind_button_mask(1) == 0x2000u);
    CHECK(rhythm_kind_button_mask(2) == 0x8000u);
    CHECK(rhythm_kind_button_mask(3) == 0x4000u);
    CHECK(!rhythm_buttons_match(0, 0x8000u) && rhythm_buttons_match(2, 0x8000u));
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

static int test_sidescrl_spawns(void) {
    stage_enemies e;
    runway_skel skel;
    unsigned i;

    runway_skel_init(&skel);
    stage_enemies_init_sidescrl(&e, "BAT1", 2341u);
    CHECK(e.count >= 4u); /* denser on BAT* */
    stage_enemies_init_sidescrl(&e, "MAUSO", 2341u);
    CHECK(e.count == 0u);
    CHECK(stage_enemies_spawn_mauso_assembler(&e, 95u, 80, 120, RUNWAY_DAMAGE_DEFAULT) == 0);
    CHECK(stage_enemies_mauso_alive(&e) == 1u);
    for (i = 0; i < 40u; i++)
        (void)stage_enemies_step(&e, &skel, 95u * 2341u, 0, 1);
    CHECK(e.hits_applied >= 1u || skel.hit_cooldown > 0 || skel.skeletism < RUNWAY_SKEL_MAX);
    printf("sidescrl enemies: MAUSO assembler hits=%u OK\n", e.hits_applied);
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

static int test_blues_combat_and_pickups(void) {
    const unsigned u = 2341u;
    stage_enemies e, bat;
    runway_skel skel;
    runway_hazards haz;
    unsigned i;

    /* C directional bolt kills 1-HP gang member and pops poof_blue. */
    runway_skel_init(&skel);
    runway_hazards_init_runway(&haz, u);
    stage_enemies_init_runway(&e, u);
    CHECK(stage_enemies_fire_blues(&e, 110u * u, 180u * u, 1, 0) == 1);
    CHECK(e.bolts_fired == 1u);
    CHECK(stage_enemies_fire_blues(&e, 110u * u, 180u * u, 1, 0) == 0); /* cd */
    for (i = 0; i < 8u; i++)
        (void)stage_enemies_step_player(&e, &skel, 40u * u, 180u * u, 180u * u,
                                        0, 0, &haz, 0);
    CHECK(e.ents[0].active == 0);
    CHECK(e.defeated_count >= 1u);
    CHECK(e.pops[0].active == 1);

    /* C directional bolt shatters incoming CThrown bone. */
    runway_hazards_init_runway(&haz, u);
    runway_hazards_spawn_thrown_vel(&haz, 140u * u, 150u * u,
                                    -(int32_t)(2u * u), RUNWAY_DAMAGE_DEFAULT);
    CHECK(haz.thrown[0].active == 1);
    for (i = 0; i < 4u; i++)
        (void)stage_enemies_step_player(&e, &skel, 40u * u, 180u * u, 180u * u,
                                        0, 0, &haz, 0);
    CHECK(stage_enemies_fire_blues(&e, 112u * u, 150u * u, 1, 0) == 1);
    for (i = 0; i < 10u; i++)
        (void)stage_enemies_step_player(&e, &skel, 40u * u, 180u * u, 180u * u,
                                        0, 0, &haz, 0);
    CHECK(haz.thrown[0].active == 0);

    /* A radial pulse hits 3-HP RED once (-1 HP) and destroys nearby CThrown. */
    stage_enemies_init_special(&e, "GENRM", u);
    runway_hazards_init_runway(&haz, u);
    runway_hazards_spawn_thrown_vel(&haz, 100u * u, 180u * u, 0,
                                    RUNWAY_DAMAGE_DEFAULT);
    CHECK(e.ents[0].kind == STAGE_ENEMY_RED && e.ents[0].hp == 3);
    CHECK(stage_enemies_fire_blues(&e, 85u * u, 180u * u, 1, 1) == 1);
    for (i = 0; i < 4u; i++)
        (void)stage_enemies_step_player(&e, &skel, 85u * u, 180u * u, 180u * u,
                                        0, 0, &haz, 0);
    CHECK(e.ents[0].hp == 2);
    CHECK(haz.thrown[0].active == 0);

    /* HEALTH pickup heals damaged Bones via runway_skel_add. */
    runway_skel_init(&skel);
    (void)runway_skel_hit(&skel, RUNWAY_DAMAGE_DEFAULT * 2);
    {
        int32_t low = skel.skeletism;
        stage_enemies_add_health_pickup(&e, 45u, 180u, RUNWAY_DAMAGE_DEFAULT);
        (void)stage_enemies_step_player(&e, &skel, 45u * u, 180u * u, 180u * u,
                                        0, 0, 0, 0);
        CHECK(e.pickups_collected == 1u);
        CHECK(skel.skeletism > low);
    }

    /* High jump clears ground enemy & zone hazards; crouch dodges BAT. */
    runway_skel_init(&skel);
    runway_hazards_init_runway(&haz, u);
    stage_enemies_init_runway(&e, u);
    for (i = 0; i < 6u; i++) {
        CHECK(stage_enemies_step_player(&e, &skel, 128u * u, 158u * u,
                                        180u * u, 0, 1, &haz, 1) == 0);
        CHECK(runway_hazards_step_ex(&haz, &skel, 128u * u, 1) == 0);
    }
    CHECK(e.hits_applied == 0u && haz.hits_applied == 0u);

    runway_skel_init(&skel);
    stage_enemies_init_sidescrl(&bat, "BAT1", u);
    CHECK(bat.ents[0].kind == STAGE_ENEMY_BAT);
    (void)stage_enemies_step_player(&bat, &skel, bat.ents[0].x, 180u * u,
                                    180u * u, 1, 0, 0, 1);
    CHECK(bat.hits_applied == 0u);
    (void)stage_enemies_step_player(&bat, &skel, bat.ents[0].x, 180u * u,
                                    180u * u, 0, 0, 0, 1);
    CHECK(bat.hits_applied >= 1u);

    printf("blues combat + pickups + evasion OK\n");
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

static void measure_fb(const uint8_t *fb, size_t nbytes, unsigned *out_nz,
                       unsigned long long *out_r, unsigned long long *out_b,
                       uint32_t *out_hash, int *out_bad_green) {
    unsigned nz = 0;
    unsigned long long sr = 0, sb = 0;
    uint32_t h = 2166136261u;
    int bg = 0;
    for (size_t i = 0; i < nbytes; i += 4u) {
        uint8_t r = fb[i + 0], g = fb[i + 1], b = fb[i + 2], a = fb[i + 3];
        if (a > 0) {
            nz++;
            sr += r;
            sb += b;
            if (r == 0 && g >= 240u && b == 0) bg++;
        }
        h = (h ^ r) * 16777619u;
        h = (h ^ g) * 16777619u;
        h = (h ^ b) * 16777619u;
        h = (h ^ a) * 16777619u;
    }
    if (out_nz) *out_nz = nz;
    if (out_r) *out_r = sr;
    if (out_b) *out_b = sb;
    if (out_hash) *out_hash = h;
    if (out_bad_green) *out_bad_green = bg;
}

static int test_stage_sprite_renders(cdimage *disc1, cdimage *disc2, int has2) {
    static uint8_t fb[320u * 224u * 4u];
    stage_enemies e;
    unsigned nz = 0;
    unsigned long long sr = 0, sb = 0, sr2 = 0, sb2 = 0;
    uint32_t h0 = 0, h1 = 0, h_gang = 0;
    int bad_g = 0;

    stage_enemies_load_disc_sprites(disc1);
    if (has2) stage_enemies_load_disc_sprites(disc2);

    /* RUNWAY poof red vs blue bank polarity fix */
    memset(&e, 0, sizeof e);
    e.units_per_pixel = 2341u;
    e.stage_name = "RUNWAY";
    e.count = 1u;
    e.ents[0].active = 1;
    e.ents[0].kind = STAGE_ENEMY_POOF_RED;
    e.ents[0].x = 160u * 2341u;
    e.ents[0].y = 148u * 2341u;
    memset(fb, 0, sizeof fb);
    stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
    measure_fb(fb, sizeof fb, &nz, &sr, &sb, &h0, NULL);
    CHECK(nz > 60u && sr > sb);

    e.ents[0].kind = STAGE_ENEMY_POOF_BLUE;
    memset(fb, 0, sizeof fb);
    stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
    measure_fb(fb, sizeof fb, &nz, &sr, &sb, &h1, NULL);
    CHECK(nz > 60u && sb > sr);

    /* Generic RUNWAY gang hash for comparison against stage overrides */
    e.ents[0].kind = STAGE_ENEMY_GANG;
    memset(fb, 0, sizeof fb);
    stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
    measure_fb(fb, sizeof fb, &nz, NULL, NULL, &h_gang, NULL);
    CHECK(nz > 60u);

    /* BIGBNS bumper + redpoof */
    e.stage_name = "BIGBNS";
    e.ents[0].kind = STAGE_ENEMY_GANG;
    e.ents[0].anim = 0u;
    memset(fb, 0, sizeof fb);
    stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
    measure_fb(fb, sizeof fb, &nz, NULL, NULL, &h0, &bad_g);
    CHECK(nz > 100u && h0 != h_gang && bad_g == 0);
    e.ents[0].anim = 4u;
    memset(fb, 0, sizeof fb);
    stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
    measure_fb(fb, sizeof fb, NULL, NULL, NULL, &h1, NULL);
    CHECK(h1 != h0);

    e.ents[0].kind = STAGE_ENEMY_POOF_RED;
    e.ents[0].anim = 8u;
    memset(fb, 0, sizeof fb);
    stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
    measure_fb(fb, sizeof fb, &nz, &sr, &sb, NULL, &bad_g);
    CHECK(nz > 100u && sr > sb && bad_g == 0);

    /* LOGS blue/red butterfly + bluepoof */
    e.stage_name = "LOGS";
    e.ents[0].kind = STAGE_ENEMY_GANG;
    e.ents[0].anim = 0u;
    memset(fb, 0, sizeof fb);
    stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
    measure_fb(fb, sizeof fb, &nz, &sr, &sb, &h0, &bad_g);
    CHECK(nz > 60u && sb > sr && h0 != h_gang && bad_g == 0);

    e.ents[0].kind = STAGE_ENEMY_LORD;
    memset(fb, 0, sizeof fb);
    stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
    measure_fb(fb, sizeof fb, &nz, &sr2, &sb2, NULL, &bad_g);
    CHECK(nz > 60u && sr2 > sb2 && bad_g == 0);

    e.ents[0].kind = STAGE_ENEMY_POOF_BLUE;
    e.ents[0].anim = 12u;
    memset(fb, 0, sizeof fb);
    stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
    measure_fb(fb, sizeof fb, &nz, &sr, &sb, NULL, &bad_g);
    CHECK(nz > 100u && sb > sr && bad_g == 0);

    /* VALLEY skeleton zombies (Bank 1) */
    e.stage_name = "VALLEY";
    e.ents[0].kind = STAGE_ENEMY_GANG;
    e.ents[0].anim = 0u;
    memset(fb, 0, sizeof fb);
    stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
    measure_fb(fb, sizeof fb, &nz, NULL, NULL, &h0, &bad_g);
    CHECK(nz > 100u && h0 != h_gang && bad_g == 0);
    e.ents[0].anim = 4u;
    memset(fb, 0, sizeof fb);
    stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
    measure_fb(fb, sizeof fb, NULL, NULL, NULL, &h1, NULL);
    CHECK(h1 != h0);

    /* GLASS floating skull + hands (Bank 1) */
    e.stage_name = "GLASS";
    e.ents[0].kind = STAGE_ENEMY_GANG;
    e.ents[0].anim = 0u;
    memset(fb, 0, sizeof fb);
    stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
    measure_fb(fb, sizeof fb, &nz, NULL, NULL, &h0, &bad_g);
    CHECK(nz > 100u && h0 != h_gang && bad_g == 0);
    e.ents[0].anim = 4u;
    memset(fb, 0, sizeof fb);
    stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
    measure_fb(fb, sizeof fb, NULL, NULL, NULL, &h1, NULL);
    CHECK(h1 != h0);

    /* VORTEX blue (Bank 2) & pink-red (Bank 3) plasma */
    e.stage_name = "VORTEX";
    e.ents[0].kind = STAGE_ENEMY_GANG;
    e.ents[0].anim = 0u;
    memset(fb, 0, sizeof fb);
    stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
    measure_fb(fb, sizeof fb, &nz, &sr, &sb, &h0, &bad_g);
    CHECK(nz > 100u && sb > sr && h0 != h_gang && bad_g == 0);
    e.ents[0].anim = 4u;
    memset(fb, 0, sizeof fb);
    stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
    measure_fb(fb, sizeof fb, NULL, NULL, NULL, &h1, NULL);
    CHECK(h1 != h0);

    e.ents[0].kind = STAGE_ENEMY_LORD;
    e.ents[0].anim = 0u;
    memset(fb, 0, sizeof fb);
    stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
    measure_fb(fb, sizeof fb, &nz, &sr2, &sb2, NULL, &bad_g);
    CHECK(nz > 100u && sr2 > sb2 && bad_g == 0);

    e.ents[0].kind = STAGE_ENEMY_POOF_BLUE;
    memset(fb, 0, sizeof fb);
    stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
    measure_fb(fb, sizeof fb, &nz, &sr, &sb, NULL, &bad_g);
    CHECK(nz > 100u && sb > sr && bad_g == 0);

    e.ents[0].kind = STAGE_ENEMY_POOF_RED;
    memset(fb, 0, sizeof fb);
    stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
    measure_fb(fb, sizeof fb, &nz, &sr, &sb, NULL, &bad_g);
    CHECK(nz > 100u && sr > sb && bad_g == 0);

    if (has2) {
        /* GENRM red/blue bank polarity fix + authentic zap */
        e.stage_name = "GENRM";
        e.ents[0].kind = STAGE_ENEMY_RED;
        e.ents[0].anim = 4u;
        memset(fb, 0, sizeof fb);
        stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
        measure_fb(fb, sizeof fb, &nz, &sr, &sb, NULL, &bad_g);
        CHECK(nz > 100u && sr > sb && bad_g == 0);

        e.ents[0].kind = STAGE_ENEMY_BLUE;
        memset(fb, 0, sizeof fb);
        stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
        measure_fb(fb, sizeof fb, &nz, &sr, &sb, NULL, &bad_g);
        CHECK(nz > 100u && sb > sr && bad_g == 0);

        e.ents[0].kind = STAGE_ENEMY_ZAP;
        e.ents[0].anim = 0u;
        memset(fb, 0, sizeof fb);
        stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
        measure_fb(fb, sizeof fb, &nz, NULL, NULL, &h0, &bad_g);
        CHECK(nz > 50u && bad_g == 0);
        e.ents[0].anim = 4u;
        memset(fb, 0, sizeof fb);
        stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
        measure_fb(fb, sizeof fb, NULL, NULL, NULL, &h1, NULL);
        CHECK(h1 != h0);

        /* VILLAGE spiders */
        e.stage_name = "VILLAGE";
        e.ents[0].kind = STAGE_ENEMY_GANG;
        e.ents[0].anim = 0u;
        memset(fb, 0, sizeof fb);
        stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
        measure_fb(fb, sizeof fb, &nz, NULL, NULL, &h0, &bad_g);
        CHECK(nz > 80u && h0 != h_gang && bad_g == 0);
        e.ents[0].anim = 4u;
        memset(fb, 0, sizeof fb);
        stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
        measure_fb(fb, sizeof fb, NULL, NULL, NULL, &h1, NULL);
        CHECK(h1 != h0);

        /* MOAT wingnut (with sub-PAN bank 2) + redpoof */
        e.stage_name = "MOAT";
        e.ents[0].kind = STAGE_ENEMY_GANG;
        e.ents[0].anim = 0u;
        memset(fb, 0, sizeof fb);
        stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
        measure_fb(fb, sizeof fb, &nz, NULL, NULL, &h0, &bad_g);
        CHECK(nz > 80u && h0 != h_gang && bad_g == 0);
        e.ents[0].kind = STAGE_ENEMY_POOF_RED;
        e.ents[0].anim = 8u;
        memset(fb, 0, sizeof fb);
        stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
        measure_fb(fb, sizeof fb, &nz, &sr, &sb, NULL, &bad_g);
        CHECK(nz > 100u && sr > sb && bad_g == 0);

        /* FINALE plasma ring blast & vertical bolt (bank 1 = authentic Saturn blue-violet plasma) */
        e.stage_name = "FINALE";
        e.ents[0].kind = STAGE_ENEMY_RING;
        e.ents[0].cooldown = 24u;
        e.ents[0].anim = 0u;
        memset(fb, 0, sizeof fb);
        stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
        measure_fb(fb, sizeof fb, &nz, &sr, &sb, &h0, &bad_g);
        CHECK(nz > 80u && sb > sr && bad_g == 0);
        e.ents[0].cooldown = 16u;
        memset(fb, 0, sizeof fb);
        stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
        measure_fb(fb, sizeof fb, NULL, NULL, NULL, &h1, NULL);
        CHECK(h1 != h0);

        e.ents[0].kind = STAGE_ENEMY_BOLT;
        e.ents[0].anim = 0u;
        memset(fb, 0, sizeof fb);
        stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 148);
        measure_fb(fb, sizeof fb, &nz, &sr, &sb, &h0, &bad_g);
        CHECK(nz > 80u && sb > sr && bad_g == 0);

        /* LAFF giant Da Laff jester (Bank 3) */
        e.stage_name = "LAFF";
        e.ents[0].kind = STAGE_ENEMY_LORD;
        e.ents[0].anim = 0u;
        memset(fb, 0, sizeof fb);
        stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 180);
        measure_fb(fb, sizeof fb, &nz, NULL, NULL, &h0, &bad_g);
        CHECK(nz > 200u && h0 != h_gang && bad_g == 0);
        e.ents[0].anim = 4u;
        memset(fb, 0, sizeof fb);
        stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 180);
        measure_fb(fb, sizeof fb, NULL, NULL, NULL, &h1, NULL);
        CHECK(h1 != h0);

        /* Authentic 04STXT HEALTH pickup sprites + Blues shots / poof_blue pop FX */
        CHECK(stage_enemies_health_sprite_count() >= 10);
        memset(&e, 0, sizeof e);
        e.units_per_pixel = 2341u;
        e.stage_name = "RUNWAY";
        stage_enemies_add_health_pickup(&e, 140u, 150u, RUNWAY_DAMAGE_DEFAULT);
        memset(fb, 0, sizeof fb);
        stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 150);
        measure_fb(fb, sizeof fb, &nz, NULL, NULL, &h0, &bad_g);
        CHECK(nz > 30u && bad_g <= 2);
        e.pickups[0].anim = 4u;
        memset(fb, 0, sizeof fb);
        stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 150);
        measure_fb(fb, sizeof fb, &nz, NULL, NULL, &h1, &bad_g);
        CHECK(nz > 30u && h1 != h0 && bad_g == 0);

        e.pickups[0].active = 0;
        CHECK(stage_enemies_fire_blues(&e, 100u * 2341u, 150u * 2341u, 1, 0) == 1);
        CHECK(stage_enemies_fire_blues(&e, 160u * 2341u, 150u * 2341u, 1, 1) == 1);
        e.pops[0].x = 220u * 2341u;
        e.pops[0].y = 150u * 2341u;
        e.pops[0].ttl = 10u;
        e.pops[0].active = 1;
        memset(fb, 0, sizeof fb);
        stage_enemies_render(&e, NULL, fb, 320u, 224u, 0u, 0, 150);
        measure_fb(fb, sizeof fb, &nz, &sr, &sb, NULL, &bad_g);
        CHECK(nz > 60u && sb > sr && bad_g == 0);
    }
    printf("stage 04STXT sprite renders OK\n");
    return 0;
}

/* Original head-control1 replay, from CHead::RunWaveBrain to EndLevel(0).
 * Capitals select physical pad masks. Lowercase repeats 1..26 inputs from
 * four retraces earlier; no song, video or game-state payload is embedded. */
static int test_bumper_replay(special_phase *phase) {
    static const uint16_t masks[]={0x0, 0x1, 0x2, 0x40, 0x41, 0x42, 0x840, 0x841, 0x842, 0x1040, 0x1041, 0x1042};
    static const char tape[]=
        "CCCCeAcCaGHBBHkEEzDDbEaAAaEBaDFACIIClAHHBBdEErKKnHHzzzzzzzzzzzzzzzzzzzIICCrABKKBaHHbKKbHHb"
        "KKbHHbKKbHHbKKbIHbKKbHHbEEuCIICzGBBHHfEEzpHHfKKbHHbKKbHHbKKbHHbKKaAGGaBKKAAGHBBKKaAGGbIICC"
        "GGBBEEbDFAaEEBaDFAaEEBbDAADEBBEbADDaBEaACDEBBEbADDACGGBBHHaADFCaEEBBKKjEDACLJaAIIbKKbDDBBK"
        "KzzzzzzzzzzzzzzzzzzzgALLCChFFzkBcCdLLbKKBBhJJCCLLbFFfDDbFaAAaFCaDDaCFaAAaFCaDDaCFaAAaFCaDD"
        "ACFLaBJJCCLLaADEBaFFCCDEAaFFCbDABDFCCFbADDACFFCbDAAIICCzzzzzzzzzzzzzzzzzzzoLBBKKrDFCCFzeJK"
        "BBKzeJLCCLoFDABDFaCLLCaJJbFDABEaCCFFABDaCCFaABDaCCFbADEACFFCbDAADFCCFbADDBBJJCCLLaAEEBaFFC"
        "CDEAAFFCCDEAaFFCaDEAaFFCbIbIqAAcBGaCCIaABDFCCFbADDACFFCbDAAaFCaDDaCFaBBKKaAJLCCFFeAKKBBzxA"
        "CLLCeFFzsAJKACLLCaFDAAaFCaDDACFFCbDAAaFCaDDaCFaAAaFCBKKBzzbJCCLLfFFzyLbLgIIbLLbIIAALLCCIGA"
        "AJLCCIIaAJJaCIGBBJLCCIIaAKKBBJLCCFFbDEAaFFCbDAADFCCFbADDACFFCbDAADFCCIKBBKzzfALLCCdFFziKBB"
        "KrCLLCeFFzhIKBBKeCCIIfKKBBqHaCIICzzsABHHBaEEcHbHeACGaBBKKfEEAADaBBKJCCFFlABDDaApKKBBzzzzzz"
        "zzzzzzzzzzzzyLcKeCLLCeFFzhLLaAKKBBoALLCChFFzlLLbFFzcADDaCFaAADFCCFbADKBBKJCCLLaAEEBaFFCaDE"
        "BaFFCCDEAaFFCaDDAaFFCbDAADFCCFbADDaCFKBBKcJLCCFKBBKnCLLCeFFzKKBBvCCLLfFFsKBBKztAJLCCLpJAAF"
        "FCaDDACFFCbDAAaFCaDDaCFaAAaFCaDDaCKKBBrCCLLbFFbKKBBtJJbLLCCdFFrJKBBKuCCLLbFFvKKBBvAAcCJaBB"
        "KJCCLLpABJaCCIIABJLCCIIaAJKaCIGBBJJCCIIABJLCCIJBBKKdACLLCeFFeADDBaJLCCLcIGAAGICCFFBBKKaAJL"
        "CCFFeADDaCFaABDFCCFbADDACFFCbDAADFCCFbADDACFLaAJJCCLLbDEAAFFCCDEAaFFCbDAADFCCFbADDaCFaABDF"
        "CCFbADDACFLaBJJCCLKBBKzlCLLCaFFnLLbFFzdIIzzzzzzzzzzzzzzzzzzzqGBBHHfEEzoCcBdHHfKKbHHbKKaAGG"
        "aBKKAAGHBBKKaAGGaBKJACGHBBKKaAIIaBbCCaGBBEEaADFCaEEBBDFAAEEBBDFAaEEBaDFAaEEBbDCCDEBBEbADFA"
        "BEEBbDAADEBBEbADDABEEBbDAAaEBaFFABEEBbDACDEBBEbADDABEEBaKGACIaBBEEACDa";
    static const unsigned hits[]={74, 726, 1543, 1663, 1671, 2235, 2400, 2483, 2638, 2863, 2864, 2972, 3037, 3040, 3041, 3048, 3049, 3059, 3079, 3080, 3110, 3168, 3791, 3992, 4047, 4104, 4215, 4247, 4301, 4359, 4380, 4471, 4494, 4632, 4678, 5411};
    static const unsigned connects[]={131, 970, 1676, 3236, 4738, 5495};
    saturn_pad pad={0};
    unsigned frames=0, bounced=0, parts=0;
    uint16_t last[4]={0};
    for (const char *token=tape; *token; ++token) {
        unsigned count=*token>='a' ? (unsigned)(*token-'a'+1) : 1u;
        while (count--) {
            unsigned slot=frames&3u;
            if (*token<'a') last[slot]=masks[*token-'A'];
            saturn_pad_update_host(&pad,last[slot]);
            special_phase_step(phase,&pad);
            ++frames;
            while (bounced<sizeof hits/sizeof hits[0] && hits[bounced]==frames) ++bounced;
            if (parts<sizeof connects/sizeof connects[0] && connects[parts]==frames) ++parts;
            CHECK(phase->bumper_bounces==bounced && phase->parts_collected==parts);
            CHECK(frames==5497 ? phase->state==SPECIAL_ST_FINISHED : phase->state==SPECIAL_ST_PLAY);
        }
    }
    CHECK(frames==5497 && bounced==36 && parts==6);
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
    CHECK(phase.score.notes[0].time == 0 && phase.score.notes[0].kind == 0);
    CHECK(phase.score.notes[1].time == 22 && phase.score.notes[1].flags == 2);
    CHECK(phase.score.notes[phase.score.count-1].time == 8878u);
    while (phase.music_tick < 206) {
        saturn_pad_update_host(&pad, MB_PAD_RIGHT);
        special_phase_step(&phase, &pad);
    }
    CHECK(!phase.music_latch && phase.misses == 1); /* First group takes UP. */
    for (i = 0; i < 9200 && !special_phase_done(&phase); i++) {
        saturn_pad_update_host(&pad, special_phase_autoplay_buttons(&phase, phase.frames));
        special_phase_step(&phase, &pad);
    }
    CHECK(phase.state == SPECIAL_ST_FINISHED && phase.misses == 1);
    printf("DRUM wired: hits=%u notes=%u %s\n", phase.hits,
           phase.rhythm->count, phase.status);
    special_phase_free(&phase);

    CHECK(special_phase_load(&phase, &disc1, "/07GUITAR", STAGE_KIND_RHYTHM) ==
          0);
    CHECK(phase.rhythm != 0);
    CHECK(phase.music_masks[0] == 0x400 && phase.music_masks[5] == 0x10);
    /* Exactly the two-sector cadence and four-retrace fret holds of the
     * recorded score2 input. Ymir wrote the reference energy after each
     * retrace from the first CGBones 2613aa through EndLevel(0). */
    {
        const uint32_t fret[] = {MB_PAD_A,MB_PAD_B,MB_PAD_C,MB_PAD_X,MB_PAD_Y,MB_PAD_Z};
        saturn_pad replay = {0};
        unsigned beat = 0, press = 0;
        uint32_t energy_hash = UINT32_C(2166136261);
        for (unsigned tick=1; tick<=1715; ++tick) {
            unsigned sector = (tick*7500u+2988u)/5976u;
            unsigned next_beat = (unsigned)(((uint64_t)sector*0x85cd8u)>>24);
            if (next_beat != beat) { beat=next_beat; press=tick+20; }
            saturn_pad_update_host(&replay,press && tick>=press && tick<press+4u ? fret[beat%6] : 0);
            CHECK(!special_phase_done(&phase));
            special_phase_step(&phase,&replay);
            energy_hash=(energy_hash^(uint32_t)phase.skel.skeletism)*UINT32_C(16777619);
        }
        CHECK(energy_hash==UINT32_C(0xd8a92610));
        CHECK(phase.state==SPECIAL_ST_FINISHED && phase.hits==62 && !phase.misses);
    }
    special_phase_free(&phase);

    CHECK(special_phase_load(&phase, &disc1, "/16BIGBNS",
                             STAGE_KIND_BIGBONES) == 0);
    CHECK(phase.big_count == 16u);
    /* Samples polled at even retraces of wave2, origin CHead::RunWaveBrain.
     * Hash of signed 16.16 x/y for 100 original retraces including fall,
     * -0x9999, paddle reset and the next ball's launch position. */
    {
        const uint16_t even_samples[] = {
            0x840, 0x840, 0x841, 0x841, 0x841, 0x41, 0x41, 0x41, 0x41, 0x41,
            0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
            0x40, 0x40, 0x41, 0x40, 0x41, 0x1040, 0x40, 0x840, 0x842, 0x40,
            0x842, 0x842, 0x842, 0x842, 0x841, 0x841, 0x841, 0x841, 0x840, 0x841,
            0x841, 0x841, 0x842, 0x842, 0x842, 0x842, 0x842, 0x842, 0x840, 0x841
        };
        saturn_pad replay={0};
        uint32_t hash=UINT32_C(2166136261);
        for(unsigned tick=1;tick<=100;++tick) {
            saturn_pad_update_host(&replay,(tick&1u)?0:even_samples[tick/2u-1]);
            special_phase_step(&phase,&replay);
            hash=(hash^(uint32_t)phase.big_x)*UINT32_C(16777619);
            hash=(hash^(uint32_t)phase.big_y)*UINT32_C(16777619);
        }
        CHECK(hash==UINT32_C(0x0204fbdf));
        CHECK(phase.skel.skeletism==0x20000-0x9999);
    }
    {
        unsigned non_flat = 0, bright = 0;
        for (size_t p = 0; p < 320u * 224u * 4u; p += 4u) {
            uint8_t r = phase.bg_rgba[p], g = phase.bg_rgba[p + 1], b = phase.bg_rgba[p + 2];
            if (r != 24 || g != 40 || b != 24) non_flat++;
            if (r > 180 && g > 180 && b > 180) bright++;
        }
        CHECK(non_flat > 50000u && bright > 4000u);
    }
    special_phase_free(&phase);
    CHECK(!special_phase_load(&phase,&disc1,"/16BIGBNS",STAGE_KIND_BIGBONES));
    CHECK(!test_bumper_replay(&phase));
    special_phase_free(&phase);

    if (opened2) {
        CHECK(special_phase_load(&phase, &disc2, "/25GENRM",
                                 STAGE_KIND_GENROOM) == 0);
        CHECK(!phase.enemies.count && phase.skel.skeletism == 0x8000);
        /* Gating one shot on B held and the first 313 fixed-point positions
         * are independent regressions from the original 25GENRM captures. */
        {
            const uint8_t moves[][2]={ {0,30},{9,1},{5,18},{4,1},{5,1},{4,2},{6,1},
              {0,6},{10,1},{6,3},{70,1},{6,1},{70,1},{6,1},{70,1},{2,1},{70,1},
              {10,1},{74,1},{10,1},{66,1},{10,1},{74,1},{10,1},{0,12},{10,20},
              {8,1},{9,2},{8,3},{10,1},{8,3},{9,2},{8,2},{5,20},{4,5},{5,1},{68,1},{6,6} };
            saturn_pad pos_tape={0};
            unsigned row=0,remaining=moves[0][1];
            uint32_t hash=UINT32_C(2166136261);
            for(unsigned tick=1;tick<=313;++tick){
                unsigned mask=moves[row][0];
                saturn_pad_update_host(&pos_tape,mask);
                special_phase_step(&phase,&pos_tape);
                hash=(hash^(uint32_t)phase.gen_x)*UINT32_C(16777619);
                hash=(hash^(uint32_t)phase.gen_y)*UINT32_C(16777619);
                if (!(tick&1u) && !--remaining) remaining=moves[++row][1];
            }
            CHECK(hash==UINT32_C(0xf5be069f) && !phase.enemies.zaps_fired);
            for(unsigned i=0;i<4;++i){saturn_pad_update_host(&pos_tape,MB_PAD_B);special_phase_step(&phase,&pos_tape);}
            CHECK(phase.enemies.zaps_fired==1);
        }
        {
            unsigned non_flat = 0;
            for (size_t p = 0; p < 320u * 224u * 4u; p += 4u) {
                if (phase.bg_rgba[p] != 24 || phase.bg_rgba[p + 1] != 40 ||
                    phase.bg_rgba[p + 2] != 24)
                    non_flat++;
            }
            CHECK(non_flat > 50000u);
        }
        special_phase_free(&phase);

        CHECK(special_phase_load(&phase, &disc2, "/27FINALE",
                                 STAGE_KIND_FINALE) == 0);
        CHECK(special_phase_finale_defer_ending(&phase) == 0);
        CHECK(phase.ring_active_count == 16 && !phase.finale_charged);
        for (i = 0; i < 150; i++) {
            saturn_pad_update_host(&pad, 0);
            CHECK(special_phase_step(&phase, &pad) == 0);
        }
        CHECK(phase.enemies.bolts_fired == 1 && phase.hazard_hits == 1);
        CHECK(phase.skel.skeletism == RUNWAY_SKEL_MAX - 0x9999 && !phase.ring_shots);
        special_phase_free(&phase);
    }

    if (test_stage_sprite_renders(&disc1, &disc2, opened2)) return 1;
    if (opened2) cd_close(&disc2);

    cd_close(&disc1);
    printf("special_phase wire OK\n");
    return 0;
}

static int test_etapa2_special_rules(void) {
    unsigned u = 2341u;
    stage_enemies gen, big;
    runway_skel skel;
    int i;
#define RCHK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

    /* GENRM: RED->BLUE conversion, CBlue heal, ZAP pylon indestructibility. */
    stage_enemies_init_special(&gen, "GENRM", u);
    runway_skel_init(&skel);
    RCHK(stage_enemies_targets_remaining(&gen) == 2u);
    RCHK(stage_enemies_count_kind(&gen, STAGE_ENEMY_ZAP) == 1u);
    /* Fire Blues bolt directly at ZAP (x=110): ZAP survives & fires pulse. */
    gen.ents[0].active = 0; /* isolate ZAP + BLUE */
    gen.ents[3].active = 0;
    stage_enemies_fire_blues(&gen, 96u * u, 180u * u, 1, 0);
    for (i = 0; i < 18; i++)
        stage_enemies_step_player(&gen, &skel, 40u * u, 180u * u, 180u * u, 0,
                                  0, NULL, 1);
    RCHK(stage_enemies_count_kind(&gen, STAGE_ENEMY_ZAP) == 1u);

    /* Re-init full GENRM and shoot CRed until converted -> CBlue. */
    stage_enemies_init_special(&gen, "GENRM", u);
    runway_skel_init(&skel);
    for (i = 0; i < 45 && stage_enemies_count_kind(&gen, STAGE_ENEMY_RED) > 0u;
         i++) {
        stage_enemies_fire_blues(&gen, gen.ents[0].x - 10u * u, 180u * u, 1, 0);
        stage_enemies_step_player(&gen, &skel, 20u * u, 150u * u, 180u * u, 0,
                                  1, NULL, 1);
    }
    RCHK(stage_enemies_count_kind(&gen, STAGE_ENEMY_RED) == 0u);
    RCHK(stage_enemies_count_kind(&gen, STAGE_ENEMY_BLUE) == 2u);
    RCHK(stage_enemies_targets_remaining(&gen) == 1u); /* POOF_RED only */

    /* CBlue in GENRM is non-lethal and heals Skeletism on contact (isolate BLUE). */
    gen.ents[2].active = 0;
    gen.ents[3].active = 0;
    (void)runway_skel_hit(&skel, RUNWAY_DAMAGE_DEFAULT);
    {
        int32_t before = skel.skeletism;
        gen.ents[1].cooldown = 0;
        stage_enemies_step_player(&gen, &skel, gen.ents[1].x, 180u * u,
                                  180u * u, 0, 0, NULL, 1);
        RCHK(skel.skeletism > before && gen.hits_applied == 0u);
    }

    /* BIGBNS: BUMPER bounce + 2-hit disarm. */
    stage_enemies_init_special(&big, "BIGBNS", u);
    runway_skel_init(&skel);
    RCHK(stage_enemies_targets_remaining(&big) == 3u);
    stage_enemies_step_player(&big, &skel, big.ents[1].x - 6u * u, 180u * u,
                              180u * u, 0, 0, NULL, 1);
    RCHK(big.bumper_rebound_vx < 0 && big.bumper_rebound_x < big.ents[1].x);
    /* 1 hit leaves bumper active; 2nd hit disarms it. */
    stage_enemies_fire_blues(&big, big.ents[1].x - 6u * u, 180u * u, 1, 0);
    for (i = 0; i < 12; i++)
        stage_enemies_step_player(&big, &skel, 20u * u, 150u * u, 180u * u, 0,
                                  1, NULL, 1);
    RCHK(big.ents[1].active && stage_enemies_targets_remaining(&big) == 3u);
    stage_enemies_fire_blues(&big, big.ents[1].x - 6u * u, 180u * u, 1, 0);
    for (i = 0; i < 12; i++)
        stage_enemies_step_player(&big, &skel, 20u * u, 150u * u, 180u * u, 0,
                                  1, NULL, 1);
    RCHK(!big.ents[1].active && stage_enemies_targets_remaining(&big) == 2u);
#undef RCHK
    printf("etapa2 special rules: GENRM convert/zap/heal + BIGBNS bumper OK\n");
    return 0;
}

static int test_etapa3_sidescrl_ai(void) {
    uint32_t u = 2341u;
    uint32_t ground_y = 180u * u;
    stage_enemies e;
    runway_skel skel;
    unsigned i;
    int found;

    /* 1. BAT1 swoop vertical Y dive + MOAT WINGNUT swoop Y dive. */
    stage_enemies_init_sidescrl(&e, "BAT1", u);
    stage_enemies_populate_sidescrl_strip(&e, 1024u);
    runway_skel_init(&skel);
    found = 0;
    for (i = 0; i < e.count; i++) {
        if (e.ents[i].active && e.ents[i].kind == STAGE_ENEMY_BAT) {
            uint32_t initial_y = e.ents[i].y;
            uint32_t bx = e.ents[i].x - 40u * u;
            int step;
            CHECK(initial_y < ground_y - 30u * u);
            for (step = 0; step < 10; step++) {
                stage_enemies_step_player(&e, &skel, bx, ground_y, ground_y,
                                          0, 0, NULL, 0);
            }
            CHECK(e.ents[i].y > initial_y);
            found = 1;
            break;
        }
    }
    CHECK(found);

    stage_enemies_init_pan_stage(&e, "MOAT", u);
    runway_skel_init(&skel);
    found = 0;
    for (i = 0; i < e.count; i++) {
        if (e.ents[i].active && e.ents[i].kind == STAGE_ENEMY_WINGNUT) {
            uint32_t initial_y = e.ents[i].y;
            uint32_t bx = e.ents[i].x - 36u * u;
            int step;
            CHECK(initial_y < ground_y - 30u * u);
            for (step = 0; step < 10; step++) {
                stage_enemies_step_player(&e, &skel, bx, ground_y, ground_y,
                                          0, 0, NULL, 0);
            }
            CHECK(e.ents[i].y > initial_y);
            found = 1;
            break;
        }
    }
    CHECK(found);

    /* 2. GLASS floating sine-wave Y oscillation. */
    stage_enemies_init_sidescrl(&e, "GLASS", u);
    stage_enemies_populate_sidescrl_strip(&e, 1280u);
    runway_skel_init(&skel);
    CHECK(e.count >= 1u);
    {
        uint32_t y0 = e.ents[0].y;
        int moved = 0, step;
        for (step = 0; step < 12; step++) {
            stage_enemies_step_player(&e, &skel, 20u * u, ground_y, ground_y,
                                      0, 0, NULL, 0);
            if (e.ents[0].y != y0) moved = 1;
        }
        CHECK(moved);
    }

    /* 3. VORTEX POOF_BLUE catalytic Blues heal on pop. */
    stage_enemies_init_sidescrl(&e, "VORTEX", u);
    stage_enemies_populate_sidescrl_strip(&e, 512u);
    runway_skel_init(&skel);
    (void)runway_skel_hit(&skel, RUNWAY_DAMAGE_DEFAULT * 2);
    {
        int32_t skel_before = skel.skeletism;
        found = 0;
        for (i = 0; i < e.count; i++) {
            if (e.ents[i].active && e.ents[i].kind == STAGE_ENEMY_POOF_BLUE) {
                int step;
                CHECK(stage_enemies_fire_blues(&e, e.ents[i].x - 6u * u,
                                               e.ents[i].y, 1, 0) == 1);
                for (step = 0; step < 16 && e.ents[i].active; step++) {
                    stage_enemies_step_player(&e, &skel, 20u * u, ground_y,
                                              ground_y, 0, 0, NULL, 0);
                }
                CHECK(!e.ents[i].active);
                CHECK(skel.skeletism > skel_before);
                found = 1;
                break;
            }
        }
        CHECK(found);
    }

    /* 4. SHADOW stealth reveal + 2 dmg on radial A pulse. */
    stage_enemies_init_sidescrl(&e, "SHADOW", u);
    stage_enemies_populate_sidescrl_strip(&e, 400u);
    runway_skel_init(&skel);
    found = 0;
    for (i = 0; i < e.count; i++) {
        if (e.ents[i].active && e.ents[i].kind == STAGE_ENEMY_SHADOW) {
            int hp0 = e.ents[i].hp;
            uint32_t bx = e.ents[i].x - 10u * u;
            int step;
            CHECK(e.ents[i].cooldown == 0u);
            CHECK(stage_enemies_fire_blues(&e, bx, ground_y, 1, 1) == 1);
            for (step = 0; step < 6; step++) {
                stage_enemies_step_player(&e, &skel, bx, ground_y, ground_y, 0,
                                          0, NULL, 0);
            }
            CHECK(e.ents[i].cooldown > 0u);
            CHECK(e.ents[i].hp <= hp0 - 2);
            found = 1;
            break;
        }
    }
    CHECK(found);

    printf("etapa3 sidescrl/pan AI: BAT/WINGNUT swoop + GLASS + VORTEX + SHADOW OK\n");
    return 0;
}

static int test_etapa4_boss_ai(void) {
    const unsigned u = 2341u;
    const uint32_t ground_y = 140u * u;
    stage_enemies laff, fin;
    runway_skel skel;
    uint32_t min_y;
    unsigned i;

    /* 1. 26LAFF Boss Da Laff: high HP, vertical hop, jester projectiles, Blues defeat. */
    stage_enemies_init_sidescrl(&laff, "LAFF", u);
    stage_enemies_populate_sidescrl_strip(&laff, 2048u);
    CHECK(stage_enemies_laff_boss_alive(&laff) == 1);
    {
        int bmax = 0;
        CHECK(stage_enemies_laff_boss_hp(&laff, &bmax) == STAGE_LAFF_BOSS_HP);
        CHECK(bmax == STAGE_LAFF_BOSS_HP);
    }
    CHECK(stage_enemies_targets_remaining(&laff) == 1u);
    runway_skel_init(&skel);
    min_y = ground_y;
    for (i = 0; i < 60u; i++) {
        uint32_t bx = laff.ents[0].x - 70u * u;
        stage_enemies_step_player(&laff, &skel, bx, ground_y, ground_y, 0, 0,
                                  NULL, 0);
        if (laff.ents[0].y < min_y) min_y = laff.ents[0].y;
    }
    CHECK(min_y + 12u * u < ground_y);
    CHECK(laff.throws_spawned >= 1u);

    /* Fire Blues C bolts repeatedly until Da Laff HP drops through phase 2 to 0. */
    for (i = 0; i < 240u && stage_enemies_laff_boss_alive(&laff); i++) {
        uint32_t bx = laff.ents[0].x > 52u * u ? laff.ents[0].x - 52u * u : 0u;
        stage_enemies_fire_blues(&laff, bx, ground_y, 1, 0);
        stage_enemies_step_player(&laff, &skel, bx, ground_y, ground_y, 0, 0,
                                  NULL, 0);
    }
    CHECK(stage_enemies_laff_boss_alive(&laff) == 0);
    CHECK(stage_enemies_targets_remaining(&laff) == 0u);

    /* 2. 27FINALE interactive hazard physics: idle hit vs crouch/dodge survival. */
    stage_enemies_init_special(&fin, "FINALE", u);
    runway_skel_init(&skel);
    for (i = 0; i < 80u; i++) {
        stage_enemies_step_player(&fin, &skel, 100u * u, 180u * u, 180u * u, 0,
                                  0, NULL, 1);
    }
    CHECK(fin.hits_applied >= 1u && skel.skeletism < RUNWAY_SKEL_MAX);

    /* Dodging away from locked BOLT + crouching during RING avoids BOLT/RING damage. */
    stage_enemies_init_special(&fin, "FINALE", u);
    runway_skel_init(&skel);
    for (i = 0; i < 90u; i++) {
        uint32_t bx = 100u * u;
        const stage_enemy *bolt = &fin.ents[2];
        if (bolt->active && bolt->cooldown <= 10u) {
            uint32_t d = (bolt->x > bx) ? (bolt->x - bx) : (bx - bolt->x);
            if (d < 24u * u) bx = (bolt->x > 160u * u) ? 80u * u : 240u * u;
        }
        stage_enemies_step_player(&fin, &skel, bx, 180u * u, 180u * u, 1, 0,
                                  NULL, 1);
    }
    CHECK(fin.bolts_fired >= 2u && fin.rings_fired >= 3u);
    CHECK(fin.hits_applied == 0u && skel.skeletism == RUNWAY_SKEL_MAX);

    printf("etapa4 boss AI: LAFF Da Laff + FINALE CBolt/CRing OK\n");
    return 0;
}

/* 06VALLEY: GANG and generic LORD face Bones (not their patrol heading). */
static int test_valley_facing(void) {
    const unsigned u = 2341u;
    stage_enemies v, m;
    runway_skel skel;
    int32_t mauso_facing;

    runway_skel_init(&skel);
    stage_enemies_init_sidescrl(&v, "VALLEY", u);
    CHECK(v.count == 2u);
    CHECK(v.ents[0].kind == STAGE_ENEMY_LORD);
    CHECK(v.ents[1].kind == STAGE_ENEMY_GANG);

    /* Bones left of both -> both face left. */
    (void)stage_enemies_step_player(&v, &skel, 40u * u, 180u * u, 180u * u, 0,
                                    0, NULL, 0);
    CHECK(v.ents[0].x > 40u * u && v.ents[1].x > 40u * u);
    CHECK(v.ents[0].facing < 0);
    CHECK(v.ents[1].facing < 0);

    /* Bones right of both -> both face right. */
    (void)stage_enemies_step_player(&v, &skel, 300u * u, 180u * u, 180u * u, 0,
                                    0, NULL, 0);
    CHECK(v.ents[0].x < 300u * u && v.ents[1].x < 300u * u);
    CHECK(v.ents[0].facing > 0);
    CHECK(v.ents[1].facing > 0);

    /* Non-VALLEY (MAUSO) LORD facing is not driven by Bones (left untouched). */
    stage_enemies_init_sidescrl(&m, "MAUSO", u);
    CHECK(stage_enemies_spawn_mauso_assembler(&m, 95u, 80, 120, RUNWAY_DAMAGE_DEFAULT) == 0);
    CHECK(m.ents[0].kind == STAGE_ENEMY_LORD);
    mauso_facing = m.ents[0].facing;
    (void)stage_enemies_step_player(&m, &skel, 40u * u, 180u * u, 180u * u, 0,
                                    0, NULL, 0);
    CHECK(m.ents[0].facing == mauso_facing);

    printf("valley facing: GANG/LORD track Bones OK\n");
    return 0;
}

int main(void) {
    if (test_rhythm_tables()) return 1;
    if (test_runway_enemies()) return 1;
    if (test_sidescrl_spawns()) return 1;
    if (test_special_enemies()) return 1;
    if (test_blues_combat_and_pickups()) return 1;
    if (test_etapa2_special_rules()) return 1;
    if (test_etapa3_sidescrl_ai()) return 1;
    if (test_etapa4_boss_ai()) return 1;
    if (test_valley_facing()) return 1;
    if (test_special_phase_wired()) return 1;
    printf("OK rhythm_enemies\n");
    return 0;
}
