#include "core/cdimage.h"
#include "core/identity.h"
#include "core/stage_catalog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void) {
    const char *d1 = getenv("MB_DISC1");
    const char *d2 = getenv("MB_DISC2");
    const char *man = getenv("MB_MANIFEST");
    char err[512];
    cdimage disc1, disc2;
    id_manifest manifest;
    id_result result;
    stage_catalog cat;
    const stage_info *rw, *ride, *mauso;
    int opened2 = 0;

    if (!d1 || !man) {
        printf("SKIP stage_catalog (set MB_DISC1 and MB_MANIFEST)\n");
        return 0;
    }

    CHECK(manifest_load(&manifest, man, err, sizeof err) == 0);
    CHECK(cd_open(&disc1, d1, err, sizeof err) == 0);
    CHECK(identity_check(&manifest, &disc1, 1, ID_QUICK, &result));
    manifest_free(&manifest);

    if (d2 && cd_open(&disc2, d2, err, sizeof err) == 0)
        opened2 = 1;

    /* §25: literal DirNameTable / DirTable / DirIDTable vs ISO walk. */
    {
        size_t name_bytes = 0, id_count = 0;
        const mb_dir_table *dt;
        const uint8_t *names;
        const uint16_t *ids;
        CHECK(stage_catalog_verify_dir_tables(&disc1, err, sizeof err) == 0);
        CHECK(stage_catalog_dir_count() == 28u);
        CHECK(strcmp(stage_catalog_dir_name(0), "03MENU") == 0);
        CHECK(strcmp(stage_catalog_dir_name(1), "04RUNWAY") == 0);
        CHECK(strcmp(stage_catalog_dir_name(27), "30MAP") == 0);
        CHECK(stage_catalog_dir_id(0) == 1u);
        CHECK(stage_catalog_dir_id(1) == 2u);
        dt = stage_catalog_dir_table();
        CHECK(dt && dt->dir_count == 28u);
        CHECK(dt->name_table_va == 0x0022da00u);
        CHECK(dt->id_table_va == 0x0022f950u);
        names = stage_catalog_dir_name_table_bytes(&name_bytes);
        CHECK(names && name_bytes == MB_DIR_NAME_TABLE_SIZE);
        CHECK(memcmp(names, "03MENU", 6) == 0);
        ids = stage_catalog_dir_id_table(&id_count);
        CHECK(ids && id_count == 28u && ids[0] == 1u);
        printf("DirNameTable: %s\n", err);
    }

    CHECK(stage_catalog_build(&cat, &disc1, opened2 ? &disc2 : NULL) == 0);
    CHECK(cat.count >= 24);
    CHECK(cat.campaign_count == 24);

    rw = stage_catalog_find(&cat, "/04RUNWAY");
    CHECK(rw);
    CHECK(strcmp(rw->name, "RUNWAY") == 0);
    CHECK(rw->playable == 1);
    CHECK(rw->kind == STAGE_KIND_RUNWAY);
    CHECK(rw->disc1_present == 1);
    CHECK(rw->preferred_disc == 1);
    CHECK(rw->code_size_best > STAGE_STUB_CODE_MAX);
    CHECK(rw->dir_id == 2u); /* DirIDTable: 03MENU=1, 04RUNWAY=2 */

    mauso = stage_catalog_find_name(&cat, "MAUSO");
    CHECK(mauso);
    CHECK(mauso->preferred_disc == 1);
    CHECK(mauso->disc1_present == 1);
    CHECK(mauso->playable == 1); /* §17 sidescrl_phase */

    {
        const stage_info *logs = stage_catalog_find_name(&cat, "LOGS");
        CHECK(logs);
        CHECK(logs->pan_size_best > 0);
        CHECK(logs->playable == 1); /* §16 15PAN playable */
    }

    if (opened2) {
        const stage_info *ice1, *moat, *staing;
        ride = stage_catalog_find_name(&cat, "RIDE");
        CHECK(ride);
        CHECK(ride->disc2_present == 1);
        CHECK(ride->preferred_disc == 2);
        CHECK(ride->code_size_best > STAGE_STUB_CODE_MAX);
        CHECK(ride->pan_size_best > 0);
        CHECK(ride->playable == 1);
        /* Disc1 stub for late stages → preferred stays 2. */
        CHECK(stage_catalog_preferred_disc(&cat, ride->iso_dir) == 2);
        ice1 = stage_catalog_find_name(&cat, "ICE1");
        CHECK(ice1 && ice1->playable == 1 && ice1->preferred_disc == 2);
        CHECK(stage_catalog_find_name(&cat, "ICE2")->playable == 1);
        moat = stage_catalog_find_name(&cat, "MOAT");
        CHECK(moat && moat->playable == 1);
        staing = stage_catalog_find_name(&cat, "STAING");
        CHECK(staing && staing->playable == 1);
        printf("catalog: count=%u RUNWAY code=%zu RIDE code=%zu pan_playable=ok\n",
               cat.count, rw->code_size_best, ride->code_size_best);
    } else {
        printf("catalog: count=%u RUNWAY code=%zu (no MB_DISC2)\n",
               cat.count, rw->code_size_best);
    }

    cd_close(&disc1);
    if (opened2) cd_close(&disc2);
    printf("OK stage_catalog\n");
    return 0;
}
