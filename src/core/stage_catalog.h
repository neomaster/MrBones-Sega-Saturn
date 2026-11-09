#ifndef MB_STAGE_CATALOG_H
#define MB_STAGE_CATALOG_H
#include "cdimage.h"
#include <stddef.h>
#include <stdint.h>

/*
 * §14/§15/§25 — DirNameTable / DirTable / DirIDTable from loader `/02`.
 * LOADER.MAP: COMMON _DirNameTable@0022da00 size 0x3c0, _DirTable@0022ddc0
 * size 0xc, _DirIDTable@0022f950 size 0x1e0 (BSS filled by GFDR_SetupDirTbl
 * @00210218 / gfdr_setupDirNameTbl@00210418 at runtime from ISO dirs).
 * Host embeds the literal disc-1 top-level dir set verified against ISO walk.
 *
 * Special MAP kinds: GUITAR/DRUM (rhythm+scrlcoll), GENROOM/BIGBONES (pan_coll),
 * FINALE (scrlcoll boss), DUKMOVIE (AVI), LOSE (AVI).
 */

#define STAGE_CATALOG_MAX 32
#define STAGE_NAME_MAX 16
#define STAGE_ISO_MAX 32
#define STAGE_CAMPAIGN_MAX 24

#define STAGE_STUB_CODE_MAX 20000u /* disc stub 03CODE ≈19800 */

/* LOADER COMMON sizes (bytes). */
#define MB_DIR_NAME_TABLE_SIZE 0x3c0u
#define MB_DIR_TABLE_SIZE 0xcu
#define MB_DIR_ID_TABLE_SIZE 0x1e0u
#define MB_DIR_NAME_ENTRY 16u /* padded ISO dir leaf (e.g. "04RUNWAY") */
#define MB_DIR_NAME_SLOTS (MB_DIR_NAME_TABLE_SIZE / MB_DIR_NAME_ENTRY) /* 60 */
#define MB_DIR_ID_SLOTS (MB_DIR_ID_TABLE_SIZE / 2u)                    /* 240 */

typedef enum {
    STAGE_KIND_SIDE = 0,     /* scroller/scrlcoll or pan side-scroll */
    STAGE_KIND_RUNWAY = 1,   /* native playable */
    STAGE_KIND_RHYTHM = 2,   /* GUITAR / DRUM */
    STAGE_KIND_GENROOM = 3,  /* GENRM */
    STAGE_KIND_BIGBONES = 4, /* BIGBNS */
    STAGE_KIND_FINALE = 5,
    STAGE_KIND_LOSE = 6,     /* AVI-only transition */
    STAGE_KIND_INTRO = 7,
    STAGE_KIND_MOVIE = 8,    /* DUKMOVIE stand-in via 19DUCK/20AVI */
    STAGE_KIND_EMPTY = 9,    /* TROLL stubs / no real CODE */
    STAGE_KIND_META = 10     /* MENU / MAP / non-campaign ISO dirs */
} stage_kind;

typedef struct {
    char name[STAGE_NAME_MAX];
    char iso_dir[STAGE_ISO_MAX];
    stage_kind kind;
    int disc1_present; /* 1 if 03CODE above stub on disc1 */
    int disc2_present;
    int preferred_disc; /* 1 or 2; 0 unknown */
    size_t code_size_best;
    size_t pan_size_best;
    size_t avi_size_best;
    int playable;       /* 1 = native phase */
    int in_campaign;    /* 1 = one of the 24 story stages */
    int campaign_index; /* 0..23 when in_campaign */
    uint16_t dir_id;    /* DirIDTable entry */
} stage_info;

typedef struct {
    stage_info stages[STAGE_CATALOG_MAX];
    unsigned count;
    const stage_info *campaign[STAGE_CAMPAIGN_MAX];
    unsigned campaign_count;
} stage_catalog;

/* Soft DirTable@22ddc0 (12 bytes): count + VA stand-ins. */
typedef struct {
    uint16_t dir_count;
    uint16_t reserved;
    uint32_t name_table_va; /* 0x0022da00 */
    uint32_t id_table_va;   /* 0x0022f950 */
} mb_dir_table;

int stage_catalog_build(stage_catalog *cat, cdimage *disc1, cdimage *disc2);
const stage_info *stage_catalog_find(const stage_catalog *cat, const char *iso_dir);
const stage_info *stage_catalog_find_name(const stage_catalog *cat, const char *name);
int stage_catalog_preferred_disc(const stage_catalog *cat, const char *iso_dir);
const stage_info *stage_catalog_next(const stage_catalog *cat, const char *iso_dir);
const stage_info *stage_catalog_campaign(const stage_catalog *cat, unsigned index);
const char *stage_kind_name(stage_kind k);

/* §25 literal Dir* tables (LOADER BSS stand-ins filled from ISO evidence). */
unsigned stage_catalog_dir_count(void);
const char *stage_catalog_dir_name(unsigned index); /* "04RUNWAY" leaf */
uint16_t stage_catalog_dir_id(unsigned index);
const mb_dir_table *stage_catalog_dir_table(void);
const uint8_t *stage_catalog_dir_name_table_bytes(size_t *out_size); /* 0x3c0 */
const uint16_t *stage_catalog_dir_id_table(size_t *out_count);

/*
 * Verify DirNameTable leaves against ISO top-level dirs on `cd`.
 * Returns 0 if every table entry (except optional holes) matches a disc dir.
 */
int stage_catalog_verify_dir_tables(cdimage *cd, char *err, size_t err_len);

#endif
