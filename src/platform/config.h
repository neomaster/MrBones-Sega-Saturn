/*
 * Configurable paths. Precedence (low to high): defaults < mrbones.ini < environment < command line.
 * Environment: MB_DISC1, MB_DISC2, MB_DATA_DIR, MB_SAVE_DIR, MB_MANIFEST, MB_CONFIG.
 * ini keys:    disc1, disc2, data_dir, save_dir, manifest   (key = value, '#' or ';' comments)
 */
#ifndef MB_CONFIG_H
#define MB_CONFIG_H
#include <stddef.h>

#define MB_PATH_MAX 1024

typedef struct {
    char disc[2][MB_PATH_MAX];  /* .cue or .bin of disc 1 / disc 2; empty = not set */
    char data_dir[MB_PATH_MAX];
    char save_dir[MB_PATH_MAX];
    char manifest[MB_PATH_MAX];
    char config_path[MB_PATH_MAX]; /* file the values were read from / will be written to */
} mb_config;

/* %LOCALAPPDATA%\MrBonesPort (falls back to "." when the variable is absent). */
void config_user_dir(char *out, size_t n);
void config_defaults(mb_config *c);
/* 0 = loaded, -1 = file missing (defaults kept), -2 = unreadable. */
int config_load_ini(mb_config *c, const char *path);
void config_apply_env(mb_config *c);
/* defaults -> ini (path, or MB_CONFIG, or the user-dir default) -> env. */
int config_load(mb_config *c, const char *explicit_path);
int config_save_ini(const mb_config *c, const char *path);

#endif
