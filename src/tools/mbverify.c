/*
 * mbverify: choose the local discs and check that they are a supported version.
 *   mbverify [--config FILE] [--disc1 PATH] [--disc2 PATH] [--manifest FILE]
 *            [--quick | --full] [--save-config]
 * Exit: 0 all given discs verified, 2 identity mismatch, 3 config/usage/IO problem.
 */
#include "../core/cdimage.h"
#include "../core/identity.h"
#include "../platform/config.h"
#include <stdio.h>
#include <string.h>

static int usage(void) {
    fprintf(stderr, "usage: mbverify [--config FILE] [--disc1 PATH] [--disc2 PATH] [--manifest FILE] [--quick|--full] [--save-config]\n");
    return 3;
}

int main(int argc, char **argv) {
    const char *cfg_path = NULL, *cli_disc[2] = {NULL, NULL}, *cli_manifest = NULL;
    id_mode mode = ID_QUICK;
    int save = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--config") && i + 1 < argc) cfg_path = argv[++i];
        else if (!strcmp(argv[i], "--disc1") && i + 1 < argc) cli_disc[0] = argv[++i];
        else if (!strcmp(argv[i], "--disc2") && i + 1 < argc) cli_disc[1] = argv[++i];
        else if (!strcmp(argv[i], "--manifest") && i + 1 < argc) cli_manifest = argv[++i];
        else if (!strcmp(argv[i], "--quick")) mode = ID_QUICK;
        else if (!strcmp(argv[i], "--full")) mode = ID_FULL;
        else if (!strcmp(argv[i], "--save-config")) save = 1;
        else return usage();
    }
    mb_config cfg;
    config_load(&cfg, cfg_path);
    for (int d = 0; d < 2; d++)
        if (cli_disc[d]) snprintf(cfg.disc[d], MB_PATH_MAX, "%s", cli_disc[d]);
    if (cli_manifest) snprintf(cfg.manifest, MB_PATH_MAX, "%s", cli_manifest);

    if (!cfg.disc[0][0] && !cfg.disc[1][0]) {
        fprintf(stderr, "no disc configured. Use --disc1/--disc2, MB_DISC1/MB_DISC2, or %s\n", cfg.config_path);
        return 3;
    }
    char err[512];
    id_manifest man;
    if (manifest_load(&man, cfg.manifest, err, sizeof err) != 0) { fprintf(stderr, "%s\n", err); return 3; }

    int worst = 0;
    for (int d = 0; d < 2; d++) {
        if (!cfg.disc[d][0]) { printf("disc %d: not configured\n", d + 1); continue; }
        cdimage cd;
        if (cd_open(&cd, cfg.disc[d], err, sizeof err) != 0) {
            printf("disc %d: FAIL cannot open (%s)\n", d + 1, err);
            worst = worst < 3 ? 3 : worst;
            continue;
        }
        id_result r;
        identity_check(&man, &cd, d + 1, mode, &r);
        int audio = 0;
        for (int t = 2; t <= cd_track_count(&cd); t++) { const cd_track *k = cd_track_get(&cd, t); if (k && k->available) audio++; }
        if (r.ok)
            printf("disc %d: OK (%s, %d files checked, %d/%d audio tracks present)\n", d + 1,
                   mode == ID_FULL ? "full" : "quick", r.files_checked, audio, cd_track_count(&cd) - 1);
        else {
            printf("disc %d: FAIL %s\n", d + 1, r.message);
            worst = 2;
        }
        cd_close(&cd);
    }
    manifest_free(&man);
    if (save && worst == 0) {
        if (config_save_ini(&cfg, cfg.config_path) == 0) printf("saved %s\n", cfg.config_path);
        else { fprintf(stderr, "cannot write %s\n", cfg.config_path); return 3; }
    }
    return worst;
}
