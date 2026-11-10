#ifndef MB_MENU_PHASE_H
#define MB_MENU_PHASE_H
#include "core/cdimage.h"
#include "core/duck_avi.h"
#include "core/nvm_shadow.h"
#include "core/saturn_pad.h"
#include <stddef.h>
#include <stdint.h>

/*
 * Native 03MENU reconstruction (§13/§25) — CMenuSelector stand-in.
 * Screens: Title (INTRO/20AVI opening) → Start → Options / Level Select.
 * SelectMenuEntry cases: 0/1/6 start level, 2 Options, 3 LevelSelect, 4/5 toggles.
 * Intro AVI and audio come from /29INTRO, with menu resources from /03MENU.
 */

typedef enum {
    MENU_SCR_TITLE = 0,
    MENU_SCR_START = 1,
    MENU_SCR_OPTIONS = 2,
    MENU_SCR_LEVELS = 3,
    MENU_SCR_STORY = 4
} menu_screen;

typedef enum {
    MENU_ST_ACTIVE = 0,
    MENU_ST_START_LEVEL = 1, /* leave menu → load selected level */
    MENU_ST_QUIT = 2
} menu_phase_state;

#define MENU_LEVEL_MAX 28
#define MENU_LEVEL_NAME 16

/*
 * One row of the original LevelSelect screen. UpdateLevels_CMenuSelector reads
 * _NvmShadow[dir_id] for each entry; the entries are the 13 stages with IDs
 * 04..10 and 18..23 (docs/ghidra-menu-level-table.txt). Other campaign dirs
 * (LOGS, DRUM, GLASS, BIGBNS, VORTEX, STAING, GENRM, LAFF, FINALE) are reached
 * only by playing.
 */
typedef struct {
    char name[MENU_LEVEL_NAME]; /* e.g. "RUNWAY" */
    char iso_dir[32];           /* e.g. "/04RUNWAY" */
    int dir_id;                 /* DirNameTable prefix, 4 = 04RUNWAY */
    int unlocked;
} menu_level;

/* 1 if a directory ID is one of the 13 rows of the original LevelSelect. */
int menu_phase_level_selectable(int dir_id);

typedef struct {
    menu_phase_state state;
    cdimage *cd;
    menu_screen screen;
    int cursor;          /* row index within screen */
    int music_on;        /* case 4 toggle */
    int sfx_on;          /* case 5 toggle */
    unsigned frames;
    mb_nvm_shadow nvm;
    char save_dir[1024];
    menu_level levels[MENU_LEVEL_MAX];
    unsigned level_count;
    int selected_level;  /* index into levels[] when START_LEVEL */
    char status[160];
    uint8_t *frame_rgba; /* 320×224 diagnostic title/menu frame */
    uint8_t *original_menu_rgba;
    uint8_t *original_items[4];
    /* §16: /03MENU/04STXT decoded plates + optional CODE palette. */
    uint8_t *stxt_blob;
    size_t stxt_size;
    uint8_t *code_blob;
    size_t code_size;
    uint8_t *gfx_rgba[8];
    unsigned gfx_w[8], gfx_h[8];
    unsigned gfx_count;
    uint8_t *copyright_rgba;
    unsigned copyright_w, copyright_h;
    /* §25 title opening */
    void *titl_blob;
    size_t titl_size;
    void *avi_blob;
    size_t avi_size;
    void *duck_blob;
    size_t duck_size;
    duck_avi title_avi;
    int title_avi_ok;
    unsigned title_avi_frames;
    int movie_pcm_pending;
} menu_phase;

int menu_phase_load(menu_phase *menu, cdimage *cd, const char *save_dir);
void menu_phase_free(menu_phase *menu);
int menu_phase_step(menu_phase *menu, const saturn_pad *pad);
void menu_phase_show_start(menu_phase *menu);
int menu_phase_done(const menu_phase *menu);
const char *menu_phase_selected_iso(const menu_phase *menu);

#endif
