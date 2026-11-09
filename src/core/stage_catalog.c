#include "stage_catalog.h"
#include "resource.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Literal DirNameTable leaves — ISO top-level order from disc1 walk
 * (GFDR_SetupDirTbl runtime fill of BSS @0x0022da00). Each slot is
 * MB_DIR_NAME_ENTRY (16) bytes, NUL-padded. Campaign stages = 04..27.
 */
static const char k_dir_names[MB_DIR_NAME_SLOTS][MB_DIR_NAME_ENTRY] = {
    "03MENU",   "04RUNWAY", "05MAUSO",  "06VALLEY", "07GUITAR", "08BAT1",
    "09BAT2",   "10BAT3",   "11LOGS",   "12TROLL1", "13TROLL2", "14DRUM",
    "15GLASS",  "16BIGBNS", "17VORTEX", "18RIDE",   "19VILLAGE","20ICE1",
    "21ICE2",   "22SHADOW", "23MOAT",   "24STAING", "25GENRM",  "26LAFF",
    "27FINALE", "28LOSE",   "29INTRO",  "30MAP",
};

static const unsigned k_dir_name_count = 28u;

static const mb_dir_table k_dir_table = {
    (uint16_t)28u,
    0u,
    0x0022da00u,
    0x0022f950u,
};

/* DirIDTable@22f950 — sequential host IDs matching GFDR NameToId order. */
static uint16_t k_dir_ids[MB_DIR_ID_SLOTS];
static uint8_t k_dir_name_bytes[MB_DIR_NAME_TABLE_SIZE];
static int k_dir_inited;

static void ensure_dir_tables(void) {
    unsigned i, o;
    if (k_dir_inited) return;
    memset(k_dir_name_bytes, 0, sizeof k_dir_name_bytes);
    memset(k_dir_ids, 0, sizeof k_dir_ids);
    for (i = 0; i < k_dir_name_count && i < MB_DIR_NAME_SLOTS; i++) {
        o = i * MB_DIR_NAME_ENTRY;
        snprintf((char *)(k_dir_name_bytes + o), MB_DIR_NAME_ENTRY, "%s",
                 k_dir_names[i]);
        k_dir_ids[i] = (uint16_t)(i + 1u); /* 1-based like GFS dir ids */
    }
    k_dir_inited = 1;
}

typedef struct {
    const char *leaf; /* DirNameTable entry without slash */
    stage_kind kind;
    int in_campaign;
} stage_def;

static stage_kind kind_for_leaf(const char *leaf) {
    if (!leaf) return STAGE_KIND_SIDE;
    if (!strcmp(leaf, "03MENU") || !strcmp(leaf, "30MAP")) return STAGE_KIND_META;
    if (!strcmp(leaf, "04RUNWAY")) return STAGE_KIND_RUNWAY;
    if (!strcmp(leaf, "07GUITAR") || !strcmp(leaf, "14DRUM"))
        return STAGE_KIND_RHYTHM;
    if (!strcmp(leaf, "16BIGBNS")) return STAGE_KIND_BIGBONES;
    if (!strcmp(leaf, "25GENRM")) return STAGE_KIND_GENROOM;
    if (!strcmp(leaf, "27FINALE")) return STAGE_KIND_FINALE;
    if (!strcmp(leaf, "28LOSE")) return STAGE_KIND_LOSE;
    if (!strcmp(leaf, "29INTRO")) return STAGE_KIND_INTRO;
    if (!strcmp(leaf, "12TROLL1") || !strcmp(leaf, "13TROLL2"))
        return STAGE_KIND_EMPTY;
    return STAGE_KIND_SIDE;
}

static int campaign_for_leaf(const char *leaf) {
    unsigned n;
    if (!leaf || leaf[0] < '0' || leaf[1] < '0') return 0;
    /* Campaign = 04RUNWAY .. 27FINALE (skip MENU/LOSE/INTRO/MAP). */
    n = (unsigned)(leaf[0] - '0') * 10u + (unsigned)(leaf[1] - '0');
    return n >= 4u && n <= 27u;
}

static void leaf_to_iso(char *out, size_t n, const char *leaf) {
    snprintf(out, n, "/%s", leaf);
}

static void leaf_to_name(char *out, size_t n, const char *leaf) {
    /* Strip NN prefix → RUNWAY, MAUSO, … */
    if (leaf && leaf[0] >= '0' && leaf[0] <= '9' && leaf[1] >= '0' &&
        leaf[1] <= '9')
        snprintf(out, n, "%s", leaf + 2);
    else
        snprintf(out, n, "%s", leaf ? leaf : "");
}

const char *stage_kind_name(stage_kind k) {
    switch (k) {
    case STAGE_KIND_SIDE: return "side";
    case STAGE_KIND_RUNWAY: return "runway";
    case STAGE_KIND_RHYTHM: return "rhythm";
    case STAGE_KIND_GENROOM: return "genroom";
    case STAGE_KIND_BIGBONES: return "bigbones";
    case STAGE_KIND_FINALE: return "finale";
    case STAGE_KIND_LOSE: return "lose";
    case STAGE_KIND_INTRO: return "intro";
    case STAGE_KIND_MOVIE: return "movie";
    case STAGE_KIND_EMPTY: return "empty";
    case STAGE_KIND_META: return "meta";
    }
    return "?";
}

static stage_info *find_or_add_leaf(stage_catalog *cat, const char *leaf,
                                    uint16_t dir_id) {
    char iso[STAGE_ISO_MAX], name[STAGE_NAME_MAX];
    stage_kind kind;
    int in_camp;
    unsigned i;
    leaf_to_iso(iso, sizeof iso, leaf);
    leaf_to_name(name, sizeof name, leaf);
    kind = kind_for_leaf(leaf);
    in_camp = campaign_for_leaf(leaf);
    if (kind == STAGE_KIND_META) return NULL; /* MENU/MAP not in stage list */
    for (i = 0; i < cat->count; i++) {
        if (strcmp(cat->stages[i].iso_dir, iso) == 0) {
            cat->stages[i].dir_id = dir_id;
            return &cat->stages[i];
        }
    }
    if (cat->count >= STAGE_CATALOG_MAX) return NULL;
    i = cat->count++;
    memset(&cat->stages[i], 0, sizeof cat->stages[i]);
    snprintf(cat->stages[i].iso_dir, sizeof cat->stages[i].iso_dir, "%s", iso);
    snprintf(cat->stages[i].name, sizeof cat->stages[i].name, "%s", name);
    cat->stages[i].kind = kind;
    cat->stages[i].in_campaign = in_camp;
    cat->stages[i].playable = (kind == STAGE_KIND_RUNWAY) ? 1 : 0;
    cat->stages[i].campaign_index = -1;
    cat->stages[i].dir_id = dir_id;
    return &cat->stages[i];
}

static size_t file_size(cdimage *cd, const char *dir, const char *file) {
    char path[96];
    cd_iso_entry entry;
    snprintf(path, sizeof path, "%s/%s", dir, file);
    if (!cd || cd_iso_lookup(cd, path, &entry) != 0 || entry.is_dir) return 0;
    return entry.size;
}

static void mark_disc(stage_info *s, int disc, size_t code, size_t pan, size_t avi) {
    int real = code > STAGE_STUB_CODE_MAX;
    if (disc == 1)
        s->disc1_present = real;
    else if (disc == 2)
        s->disc2_present = real;
    if (real && code >= s->code_size_best) {
        s->code_size_best = code;
        s->preferred_disc = disc;
    }
    if (pan > s->pan_size_best)
        s->pan_size_best = pan;
    if (avi > s->avi_size_best)
        s->avi_size_best = avi;
    if (s->kind == STAGE_KIND_LOSE || s->kind == STAGE_KIND_INTRO) {
        if (avi > 0 && (s->preferred_disc == 0 || avi >= s->avi_size_best))
            s->preferred_disc = disc;
        if (disc == 1 && avi > 0) s->disc1_present = 1;
        if (disc == 2 && avi > 0) s->disc2_present = 1;
    }
    if (s->disc1_present && !s->disc2_present)
        s->preferred_disc = 1;
    else if (s->disc2_present && !s->disc1_present)
        s->preferred_disc = 2;
}

static void scan_disc(stage_catalog *cat, cdimage *cd, int disc) {
    unsigned i;
    if (!cat || !cd) return;
    ensure_dir_tables();
    for (i = 0; i < k_dir_name_count; i++) {
        stage_info *s;
        size_t code, pan, avi;
        char iso[STAGE_ISO_MAX];
        s = find_or_add_leaf(cat, k_dir_names[i], k_dir_ids[i]);
        if (!s) continue;
        leaf_to_iso(iso, sizeof iso, k_dir_names[i]);
        code = file_size(cd, iso, "03CODE");
        pan = file_size(cd, iso, "15PAN");
        avi = file_size(cd, iso, "20AVI");
        mark_disc(s, disc, code, pan, avi);
    }
}

static int name_is_pan_playable(const char *name) {
    return name &&
           (strcmp(name, "RUNWAY") == 0 || strcmp(name, "LOGS") == 0 ||
            strcmp(name, "ICE1") == 0 || strcmp(name, "ICE2") == 0 ||
            strcmp(name, "MOAT") == 0 || strcmp(name, "RIDE") == 0 ||
            strcmp(name, "STAING") == 0);
}

static int name_is_side_playable(const char *name) {
    return name &&
           (strcmp(name, "MAUSO") == 0 || strcmp(name, "VALLEY") == 0 ||
            strcmp(name, "BAT1") == 0 || strcmp(name, "BAT2") == 0 ||
            strcmp(name, "BAT3") == 0 || strcmp(name, "GLASS") == 0 ||
            strcmp(name, "VILLAGE") == 0 || strcmp(name, "SHADOW") == 0 ||
            strcmp(name, "VORTEX") == 0 || strcmp(name, "LAFF") == 0);
}

static void build_campaign(stage_catalog *cat) {
    unsigned i, n = 0;
    cat->campaign_count = 0;
    for (i = 0; i < cat->count && n < STAGE_CAMPAIGN_MAX; i++) {
        if (!cat->stages[i].in_campaign) continue;
        cat->stages[i].campaign_index = (int)n;
        if (name_is_pan_playable(cat->stages[i].name) &&
            cat->stages[i].pan_size_best > 0 &&
            cat->stages[i].code_size_best > STAGE_STUB_CODE_MAX)
            cat->stages[i].playable = 1;
        if (name_is_side_playable(cat->stages[i].name) &&
            cat->stages[i].code_size_best > STAGE_STUB_CODE_MAX &&
            cat->stages[i].pan_size_best < 100)
            cat->stages[i].playable = 1;
        if ((cat->stages[i].kind == STAGE_KIND_RHYTHM ||
             cat->stages[i].kind == STAGE_KIND_BIGBONES ||
             cat->stages[i].kind == STAGE_KIND_GENROOM ||
             cat->stages[i].kind == STAGE_KIND_FINALE) &&
            cat->stages[i].code_size_best > STAGE_STUB_CODE_MAX)
            cat->stages[i].playable = 1;
        cat->campaign[n++] = &cat->stages[i];
    }
    cat->campaign_count = n;
}

int stage_catalog_build(stage_catalog *cat, cdimage *disc1, cdimage *disc2) {
    if (!cat) return -1;
    ensure_dir_tables();
    memset(cat, 0, sizeof *cat);
    if (disc1) scan_disc(cat, disc1, 1);
    if (disc2) scan_disc(cat, disc2, 2);
    build_campaign(cat);
    return cat->count > 0 ? 0 : -1;
}

const stage_info *stage_catalog_find(const stage_catalog *cat, const char *iso_dir) {
    unsigned i;
    if (!cat || !iso_dir) return NULL;
    for (i = 0; i < cat->count; i++)
        if (strcmp(cat->stages[i].iso_dir, iso_dir) == 0)
            return &cat->stages[i];
    return NULL;
}

const stage_info *stage_catalog_find_name(const stage_catalog *cat, const char *name) {
    unsigned i;
    if (!cat || !name) return NULL;
    for (i = 0; i < cat->count; i++)
        if (strcmp(cat->stages[i].name, name) == 0)
            return &cat->stages[i];
    return NULL;
}

int stage_catalog_preferred_disc(const stage_catalog *cat, const char *iso_dir) {
    const stage_info *s = stage_catalog_find(cat, iso_dir);
    return s ? s->preferred_disc : 0;
}

const stage_info *stage_catalog_campaign(const stage_catalog *cat, unsigned index) {
    if (!cat || index >= cat->campaign_count) return NULL;
    return cat->campaign[index];
}

const stage_info *stage_catalog_next(const stage_catalog *cat, const char *iso_dir) {
    const stage_info *s = stage_catalog_find(cat, iso_dir);
    if (!s || !s->in_campaign || s->campaign_index < 0) return NULL;
    return stage_catalog_campaign(cat, (unsigned)s->campaign_index + 1u);
}

unsigned stage_catalog_dir_count(void) {
    ensure_dir_tables();
    return k_dir_name_count;
}

const char *stage_catalog_dir_name(unsigned index) {
    ensure_dir_tables();
    if (index >= k_dir_name_count) return NULL;
    return k_dir_names[index];
}

uint16_t stage_catalog_dir_id(unsigned index) {
    ensure_dir_tables();
    if (index >= k_dir_name_count) return 0;
    return k_dir_ids[index];
}

const mb_dir_table *stage_catalog_dir_table(void) {
    ensure_dir_tables();
    return &k_dir_table;
}

const uint8_t *stage_catalog_dir_name_table_bytes(size_t *out_size) {
    ensure_dir_tables();
    if (out_size) *out_size = MB_DIR_NAME_TABLE_SIZE;
    return k_dir_name_bytes;
}

const uint16_t *stage_catalog_dir_id_table(size_t *out_count) {
    ensure_dir_tables();
    if (out_count) *out_count = k_dir_name_count;
    return k_dir_ids;
}

typedef struct {
    char paths[64][STAGE_ISO_MAX];
    unsigned count;
} dir_walk_ctx;

static int collect_top_dir(const cd_iso_entry *e, void *user) {
    dir_walk_ctx *ctx = user;
    const char *p;
    if (!e || !e->is_dir || !ctx) return 0;
    p = e->path;
    if (p[0] != '/' || strchr(p + 1, '/')) return 0;
    if (ctx->count >= 64) return 0;
    snprintf(ctx->paths[ctx->count], sizeof ctx->paths[0], "%s", p + 1); /* leaf */
    ctx->count++;
    return 0;
}

int stage_catalog_verify_dir_tables(cdimage *cd, char *err, size_t err_len) {
    dir_walk_ctx walk;
    unsigned i, j, matched = 0;
    ensure_dir_tables();
    if (!cd) {
        if (err && err_len) snprintf(err, err_len, "no cdimage");
        return -1;
    }
    memset(&walk, 0, sizeof walk);
    if (cd_iso_walk(cd, collect_top_dir, &walk) != 0 && walk.count == 0) {
        if (err && err_len) snprintf(err, err_len, "iso walk failed");
        return -1;
    }
    for (i = 0; i < k_dir_name_count; i++) {
        int found = 0;
        for (j = 0; j < walk.count; j++) {
            if (strcmp(walk.paths[j], k_dir_names[i]) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            if (err && err_len)
                snprintf(err, err_len, "DirNameTable[%u]=%s missing on ISO", i,
                         k_dir_names[i]);
            return -1;
        }
        matched++;
    }
    if (k_dir_table.dir_count != (uint16_t)k_dir_name_count) {
        if (err && err_len) snprintf(err, err_len, "DirTable.count mismatch");
        return -1;
    }
    if (k_dir_table.name_table_va != 0x0022da00u ||
        k_dir_table.id_table_va != 0x0022f950u) {
        if (err && err_len) snprintf(err, err_len, "DirTable VA mismatch");
        return -1;
    }
    if (err && err_len)
        snprintf(err, err_len, "DirNameTable %u/%u matched ISO top-level",
                 matched, walk.count);
    return 0;
}
