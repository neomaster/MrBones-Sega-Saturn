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
    c->music_on = 1;
    c->stereo_on = 0;
    c->volume = MB_CONFIG_VOLUME_MAX;
}

static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

/* on/off, true/false, yes/no, 1/0. Anything else leaves *out as it was. */
static void parse_switch(const char *v, int *out) {
    if (ieq(v, "on") || ieq(v, "true") || ieq(v, "yes") || !strcmp(v, "1")) *out = 1;
    else if (ieq(v, "off") || ieq(v, "false") || ieq(v, "no") || !strcmp(v, "0")) *out = 0;
}

/* Decimal digits only; above the maximum clamps, anything else is ignored. */
static void parse_volume(const char *v, unsigned *out) {
    unsigned n = 0;
    const char *p = v;
    if (!*p) return;
    for (; *p; p++) {
        if (*p < '0' || *p > '9') return;
        if (n <= MB_CONFIG_VOLUME_MAX) n = n * 10u + (unsigned)(*p - '0');
    }
    *out = n > MB_CONFIG_VOLUME_MAX ? MB_CONFIG_VOLUME_MAX : n;
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
        else if (!strcmp(k, "music")) parse_switch(v, &c->music_on);
        else if (!strcmp(k, "stereo")) parse_switch(v, &c->stereo_on);
        else if (!strcmp(k, "volume")) parse_volume(v, &c->volume);
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
    fprintf(f, "music = %s\nstereo = %s\nvolume = %u\n", c->music_on ? "on" : "off",
            c->stereo_on ? "on" : "off",
            c->volume > MB_CONFIG_VOLUME_MAX ? MB_CONFIG_VOLUME_MAX : c->volume);
    fclose(f);
    return 0;
}

static const char *const k_option_keys[3] = {"music", "stereo", "volume"};

/* Index into k_option_keys if `line` is "key = ...", else -1. */
static int option_key(const char *line) {
    int i;
    while (*line == ' ' || *line == '\t') line++;
    for (i = 0; i < 3; i++) {
        size_t n = strlen(k_option_keys[i]);
        const char *p = line + n;
        if (strncmp(line, k_option_keys[i], n)) continue;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '=') return i;
    }
    return -1;
}

int config_save_options(const char *path, int music_on, int stereo_on, unsigned volume) {
    char *old = NULL, *out, *w;
    size_t old_len = 0;
    int seen[3] = {0, 0, 0}, i;
    char val[3][16];
    FILE *f;
    if (!path || !*path) return -1;
    if (volume > MB_CONFIG_VOLUME_MAX) volume = MB_CONFIG_VOLUME_MAX;
    snprintf(val[0], sizeof val[0], "%s", music_on ? "on" : "off");
    snprintf(val[1], sizeof val[1], "%s", stereo_on ? "on" : "off");
    snprintf(val[2], sizeof val[2], "%u", volume);

    f = fopen(path, "rb");
    if (f) {
        long n;
        if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || n > (1L << 20) ||
            fseek(f, 0, SEEK_SET)) { fclose(f); return -1; }
        old = malloc((size_t)n + 1);
        if (!old) { fclose(f); return -1; }
        old_len = fread(old, 1, (size_t)n, f);
        old[old_len] = 0;
        fclose(f);
    }
    /* Copied lines never grow; each of the three written lines fits in 16 bytes
     * ("volume = 15\r\n" is the longest), plus one newline for an unterminated tail. */
    out = w = malloc(old_len + 3 * 16 + 2);
    if (!out) { free(old); return -1; }
    if (old) {
        const char *p = old;
        while (*p) {
            const char *nl = strchr(p, '\n');
            const char *end = nl ? nl + 1 : p + strlen(p);
            int k = option_key(p);
            if (k < 0) {
                memcpy(w, p, (size_t)(end - p));
                w += end - p;
                if (!nl) *w++ = '\n'; /* last line had no newline */
            } else if (!seen[k]) {
                int crlf = nl && nl > p && nl[-1] == '\r';
                w += sprintf(w, "%s = %s%s", k_option_keys[k], val[k], crlf ? "\r\n" : "\n");
                seen[k] = 1;
            } /* a repeated option key is dropped */
            p = end;
        }
    }
    for (i = 0; i < 3; i++)
        if (!seen[i]) w += sprintf(w, "%s = %s\n", k_option_keys[i], val[i]);
    free(old);
    f = fopen(path, "wb");
    if (!f) { free(out); return -1; }
    i = fwrite(out, 1, (size_t)(w - out), f) == (size_t)(w - out) ? 0 : -1;
    if (fclose(f)) i = -1;
    free(out);
    return i;
}
