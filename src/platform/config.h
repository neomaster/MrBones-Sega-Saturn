/*
 * Configurable paths. Precedence (low to high): defaults < mrbones.ini < environment < command line.
 * Environment: MB_DISC1, MB_DISC2, MB_DATA_DIR, MB_SAVE_DIR, MB_MANIFEST, MB_CONFIG.
 * ini keys:    disc1, disc2, data_dir, save_dir, manifest   (key = value, '#' or ';' comments)
 *              music, sfx (on/off), volume (0..15): the host menu options.
 * The options live here and not in nvmshadow.bin: the original keeps its two
 * option toggles in loader RAM and never saves them, so the 0x28-byte shadow
 * has no byte for them (docs/stage-progress-nvm.md section 8).
 */
#ifndef MB_CONFIG_H
#define MB_CONFIG_H
#include <stddef.h>

#define MB_PATH_MAX 1024
#define MB_CONFIG_VOLUME_MAX 15u

typedef struct {
    char disc[2][MB_PATH_MAX];  /* .cue or .bin of disc 1 / disc 2; empty = not set */
    char data_dir[MB_PATH_MAX];
    char save_dir[MB_PATH_MAX];
    char manifest[MB_PATH_MAX];
    char config_path[MB_PATH_MAX]; /* file the values were read from / will be written to */
    int music_on;    /* default 1 */
    int sfx_on;      /* default 1 */
    unsigned volume; /* 0..MB_CONFIG_VOLUME_MAX, default the maximum */
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
/*
 * Rewrite only the music, sfx and volume keys of the ini at `path`, keeping
 * every other line as it is (paths, comments, unknown keys); missing keys are
 * appended and a missing file is created with just these three. Unlike
 * config_save_ini this never writes paths, so values that came from the
 * environment are not frozen into the file. Volume is clamped.
 * 0 = written, -1 = the file could not be read or written.
 */
int config_save_options(const char *path, int music_on, int sfx_on, unsigned volume);

#endif
