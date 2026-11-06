#include "config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_user_dir(char *out, size_t n) {
    const char *base = getenv("LOCALAPPDATA");
    if (base && *base) snprintf(out, n, "%s\\MrBonesPort", base);
    else snprintf(out, n, ".");
}

static void set(char *dst, const char *v) { snprintf(dst, MB_PATH_MAX, "%s", v ? v : ""); }

void config_defaults(mb_config *c) {
    char u[MB_PATH_MAX];
    memset(c, 0, sizeof *c);
    config_user_dir(u, sizeof u);
    snprintf(c->data_dir, MB_PATH_MAX, "%s\\data", u);
    snprintf(c->save_dir, MB_PATH_MAX, "%s\\saves", u);
    snprintf(c->manifest, MB_PATH_MAX, "manifest\\versions.txt");
    snprintf(c->config_path, MB_PATH_MAX, "%s\\mrbones.ini", u);
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

int config_load_ini(mb_config *c, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        char *s = trim(line);
        if (!*s || *s == '#' || *s == ';') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(s), *v = trim(eq + 1);
        size_t vl = strlen(v);
        if (vl >= 2 && v[0] == '"' && v[vl - 1] == '"') { v[vl - 1] = 0; v++; }
        if (!strcmp(k, "disc1")) set(c->disc[0], v);
        else if (!strcmp(k, "disc2")) set(c->disc[1], v);
        else if (!strcmp(k, "data_dir")) set(c->data_dir, v);
        else if (!strcmp(k, "save_dir")) set(c->save_dir, v);
        else if (!strcmp(k, "manifest")) set(c->manifest, v);
    }
    fclose(f);
    return 0;
}

void config_apply_env(mb_config *c) {
    const char *v;
    if ((v = getenv("MB_DISC1")) && *v) set(c->disc[0], v);
    if ((v = getenv("MB_DISC2")) && *v) set(c->disc[1], v);
    if ((v = getenv("MB_DATA_DIR")) && *v) set(c->data_dir, v);
    if ((v = getenv("MB_SAVE_DIR")) && *v) set(c->save_dir, v);
    if ((v = getenv("MB_MANIFEST")) && *v) set(c->manifest, v);
}

int config_load(mb_config *c, const char *explicit_path) {
    config_defaults(c);
    const char *env_cfg = getenv("MB_CONFIG");
    if (explicit_path && *explicit_path) set(c->config_path, explicit_path);
    else if (env_cfg && *env_cfg) set(c->config_path, env_cfg);
    int rc = config_load_ini(c, c->config_path);
    config_apply_env(c);
    return rc;
}

int config_save_ini(const mb_config *c, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "# Mr. Bones port configuration. Paths may be absolute or relative to the working directory.\n");
    fprintf(f, "disc1 = %s\ndisc2 = %s\ndata_dir = %s\nsave_dir = %s\nmanifest = %s\n",
            c->disc[0], c->disc[1], c->data_dir, c->save_dir, c->manifest);
    fclose(f);
    return 0;
}
