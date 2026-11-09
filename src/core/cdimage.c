#include "cdimage.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define seek64(f, o) _fseeki64((f), (__int64)(o), SEEK_SET)
#define tell64(f) ((uint64_t)_ftelli64(f))
#else
#define seek64(f, o) fseeko((f), (off_t)(o), SEEK_SET)
#define tell64(f) ((uint64_t)ftello(f))
#endif

static void seterr(char *err, size_t n, const char *fmt, const char *a) {
    if (err && n) snprintf(err, n, fmt, a ? a : "");
}

static int is_abs_path(const char *p) {
    return p[0] == '/' || p[0] == '\\' || (isalpha((unsigned char)p[0]) && p[1] == ':');
}

static void dir_of(const char *path, char *out, size_t n) {
    size_t len = strlen(path), i = len;
    while (i > 0 && path[i - 1] != '/' && path[i - 1] != '\\') i--;
    if (i >= n) i = n - 1;
    memcpy(out, path, i);
    out[i] = 0;
}

static int parse_msf(const char *s, uint32_t *frames) {
    unsigned m, sec, f;
    if (sscanf(s, "%u:%u:%u", &m, &sec, &f) != 3) return -1;
    *frames = (m * 60 + sec) * 75 + f;
    return 0;
}

static int open_track_file(cd_track *t) {
    t->available = 0;
    t->file_size = 0;
    t->sectors = 0;
    t->fp = fopen(t->path, "rb");
    if (!t->fp) return -1;
    if (seek64(t->fp, 0) != 0) { fclose(t->fp); t->fp = NULL; return -1; }
    fseek(t->fp, 0, SEEK_END);
    t->file_size = tell64(t->fp);
    t->sectors = (uint32_t)(t->file_size / CD_RAW_SECTOR);
    t->available = 1;
    return 0;
}

/* Returns the next whitespace-delimited or quoted token; advances *p. */
static int next_token(const char **p, char *out, size_t n) {
    const char *s = *p;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s || *s == '\r' || *s == '\n') return 0;
    size_t k = 0;
    if (*s == '"') {
        s++;
        while (*s && *s != '"' && *s != '\r' && *s != '\n') { if (k + 1 < n) out[k++] = *s; s++; }
        if (*s == '"') s++;
    } else {
        while (*s && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n') { if (k + 1 < n) out[k++] = *s; s++; }
    }
    out[k] = 0;
    *p = s;
    return 1;
}

static int ieq_prefix(const char *a, const char *kw) {
    size_t n = strlen(kw);
    for (size_t i = 0; i < n; i++)
        if (toupper((unsigned char)a[i]) != kw[i]) return 0;
    return 1;
}

int cd_open(cdimage *cd, const char *path, char *err, size_t errlen) {
    memset(cd, 0, sizeof *cd);
    snprintf(cd->cue_path, sizeof cd->cue_path, "%s", path);
    size_t pl = strlen(path);
    int is_cue = pl >= 4 && (strcmp(path + pl - 4, ".cue") == 0 || strcmp(path + pl - 4, ".CUE") == 0);

    if (!is_cue) {
        cd_track *t = &cd->tracks[0];
        t->number = 1; t->type = CD_TRACK_DATA; t->start_lba_valid = 1;
        snprintf(t->path, sizeof t->path, "%s", path);
        if (open_track_file(t) != 0) { seterr(err, errlen, "cannot open image: %s", path); return -1; }
        cd->ntracks = 1;
    } else {
        FILE *f = fopen(path, "rb");
        if (!f) { seterr(err, errlen, "cannot open cue: %s", path); return -1; }
        char base[CD_PATH_MAX], line[2048];
        dir_of(path, base, sizeof base);
        cd_track *cur = NULL;
        int file_pending = 0;
        char pending_file[CD_PATH_MAX] = "";
        while (fgets(line, sizeof line, f)) {
            const char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            char tok[CD_PATH_MAX];
            if (ieq_prefix(p, "FILE")) {
                p += 4;
                if (!next_token(&p, tok, sizeof tok)) continue;
                if (file_pending) { fclose(f); seterr(err, errlen, "cue: FILE with more than one TRACK is unsupported (%s)", path); return -1; }
                snprintf(pending_file, sizeof pending_file, "%s", tok);
                file_pending = 1;
            } else if (ieq_prefix(p, "TRACK")) {
                p += 5;
                char num[16], mode[32];
                if (!next_token(&p, num, sizeof num) || !next_token(&p, mode, sizeof mode)) continue;
                if (!file_pending || cd->ntracks >= CD_MAX_TRACKS) { fclose(f); seterr(err, errlen, "cue: TRACK without FILE or too many tracks (%s)", path); return -1; }
                cur = &cd->tracks[cd->ntracks++];
                memset(cur, 0, sizeof *cur);
                cur->number = atoi(num);
                cur->type = ieq_prefix(mode, "AUDIO") ? CD_TRACK_AUDIO : CD_TRACK_DATA;
                if (cur->type == CD_TRACK_DATA && !ieq_prefix(mode, "MODE1/2352")) {
                    fclose(f); seterr(err, errlen, "cue: only MODE1/2352 data tracks are supported (%s)", mode); return -1;
                }
                if (is_abs_path(pending_file)) snprintf(cur->path, sizeof cur->path, "%s", pending_file);
                else snprintf(cur->path, sizeof cur->path, "%s%s", base, pending_file);
                file_pending = 0;
            } else if (ieq_prefix(p, "INDEX")) {
                p += 5;
                char idx[16], msf[32];
                uint32_t fr;
                if (!cur || !next_token(&p, idx, sizeof idx) || !next_token(&p, msf, sizeof msf) || parse_msf(msf, &fr) != 0) continue;
                if (atoi(idx) == 1) cur->index01_frame = fr;
            }
        }
        fclose(f);
        if (cd->ntracks == 0) { seterr(err, errlen, "cue has no tracks: %s", path); return -1; }
        if (cd->tracks[0].type != CD_TRACK_DATA) { seterr(err, errlen, "first track is not data: %s", path); return -1; }
        uint32_t cum = 0; int valid = 1;
        for (int i = 0; i < cd->ntracks; i++) {
            cd_track *t = &cd->tracks[i];
            open_track_file(t);
            if (i == 0 && !t->available) { seterr(err, errlen, "data track file missing: %s", t->path); return -1; }
            t->start_lba_valid = valid;
            t->start_lba = cum + t->index01_frame;
            if (!t->available) valid = 0;
            cum += t->sectors;
        }
    }
    /* sanity: sector 0 of the data track must carry the MODE1 sync pattern. */
    uint8_t s0[CD_RAW_SECTOR];
    static const uint8_t sync[12] = {0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0};
    if (cd_read_raw(cd, 0, s0) != 0 || memcmp(s0, sync, 12) != 0 || s0[15] != 1) {
        cd_close(cd);
        seterr(err, errlen, "not a MODE1/2352 raw image: %s", path);
        return -1;
    }
    return 0;
}

void cd_close(cdimage *cd) {
    if (!cd) return;
    for (int i = 0; i < cd->ntracks; i++) {
        cd->tracks[i].available = 0;
        if (cd->tracks[i].fp) {
            fclose(cd->tracks[i].fp);
            cd->tracks[i].fp = NULL;
        }
    }
}

uint32_t cd_data_sectors(const cdimage *cd) { return (cd && cd->ntracks) ? cd->tracks[0].sectors : 0; }

int cd_read_raw(cdimage *cd, uint32_t lba, uint8_t out[CD_RAW_SECTOR]) {
    if (!cd || cd->ntracks <= 0) return -1;
    cd_track *t = &cd->tracks[0];
    if (!t->available || !t->fp || lba >= t->sectors) return -1;
    if (seek64(t->fp, (uint64_t)lba * CD_RAW_SECTOR) != 0) return -1;
    return fread(out, 1, CD_RAW_SECTOR, t->fp) == CD_RAW_SECTOR ? 0 : -1;
}

int cd_read_user(cdimage *cd, uint32_t lba, uint8_t out[CD_USER_SECTOR]) {
    uint8_t raw[CD_RAW_SECTOR];
    if (cd_read_raw(cd, lba, raw) != 0) return -1;
    memcpy(out, raw + 16, CD_USER_SECTOR);
    return 0;
}

int cd_track_count(const cdimage *cd) { return cd ? cd->ntracks : 0; }

const cd_track *cd_track_get(const cdimage *cd, int number) {
    if (!cd) return NULL;
    for (int i = 0; i < cd->ntracks; i++)
        if (cd->tracks[i].number == number) return &cd->tracks[i];
    return NULL;
}

int cd_read_audio(cdimage *cd, int number, uint32_t frame, uint8_t out[CD_RAW_SECTOR]) {
    if (!cd) return -1;
    cd_track *t = NULL;
    for (int i = 0; i < cd->ntracks; i++) if (cd->tracks[i].number == number) t = &cd->tracks[i];
    if (!t || t->type != CD_TRACK_AUDIO || !t->available || !t->fp) return -1;
    uint64_t sec = (uint64_t)t->index01_frame + frame;
    if (sec >= t->sectors) return -1;
    if (seek64(t->fp, sec * CD_RAW_SECTOR) != 0) return -1;
    return fread(out, 1, CD_RAW_SECTOR, t->fp) == CD_RAW_SECTOR ? 0 : -1;
}

/* ---- ISO 9660 ---- */

static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static uint8_t *load_extent(cdimage *cd, uint32_t lba, uint32_t size) {
    uint32_t padded = (size + 2047u) / 2048u * 2048u;
    if (padded == 0) padded = 2048;
    uint8_t *b = (uint8_t *)malloc(padded);
    if (!b) return NULL;
    for (uint32_t o = 0; o < padded; o += 2048)
        if (cd_read_user(cd, lba + o / 2048, b + o) != 0) { free(b); return NULL; }
    return b;
}

static int root_extent(cdimage *cd, uint32_t *lba, uint32_t *size) {
    uint8_t pvd[2048];
    if (cd_read_user(cd, 16, pvd) != 0 || memcmp(pvd + 1, "CD001", 5) != 0 || pvd[0] != 1) return -1;
    *lba = le32(pvd + 158);
    *size = le32(pvd + 166);
    return 0;
}

static void clean_name(const uint8_t *id, int len, char *out, size_t n) {
    int k = 0;
    for (int i = 0; i < len && k + 1 < (int)n; i++) {
        if (id[i] == ';') break;
        out[k++] = (char)id[i];
    }
    out[k] = 0;
}

typedef struct { cd_iso_entry e; } dirrec;

/* Iterate records of one directory extent. cb returns nonzero to stop. */
static int iter_dir(cdimage *cd, uint32_t lba, uint32_t size, int (*cb)(const char *name, uint32_t l, uint32_t s, int dir, void *u), void *u) {
    uint8_t *d = load_extent(cd, lba, size);
    if (!d) return -1;
    int rc = 0;
    for (uint32_t p = 0; p < size;) {
        uint8_t n = d[p];
        if (!n) { p = (p / 2048 + 1) * 2048; continue; }
        if (n < 34 || p + n > size) { rc = -1; break; }
        uint8_t idlen = d[p + 32];
        if (!(idlen == 1 && (d[p + 33] == 0 || d[p + 33] == 1))) {
            char name[256];
            clean_name(d + p + 33, idlen, name, sizeof name);
            if (cb(name, le32(d + p + 2), le32(d + p + 10), (d[p + 25] & 2) != 0, u)) { rc = 1; break; }
        }
        p += n;
    }
    free(d);
    return rc;
}

typedef struct { const char *want; cd_iso_entry found; int hit; } find_ctx;

static int find_cb(const char *name, uint32_t l, uint32_t s, int dir, void *u) {
    find_ctx *c = (find_ctx *)u;
    if (_stricmp(name, c->want) != 0) return 0;
    snprintf(c->found.path, sizeof c->found.path, "%s", name);
    c->found.lba = l; c->found.size = s; c->found.is_dir = dir; c->hit = 1;
    return 1;
}

int cd_iso_lookup(cdimage *cd, const char *path, cd_iso_entry *out) {
    uint32_t lba, size;
    if (root_extent(cd, &lba, &size) != 0) return -1;
    cd_iso_entry cur = {"/", lba, size, 1};
    char full[256] = "";
    const char *p = path;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char comp[128]; size_t k = 0;
        while (*p && *p != '/' && k + 1 < sizeof comp) comp[k++] = *p++;
        comp[k] = 0;
        if (!cur.is_dir) return -1;
        find_ctx c = {comp, {{0}, 0, 0, 0}, 0};
        if (iter_dir(cd, cur.lba, cur.size, find_cb, &c) < 0 || !c.hit) return -1;
        cur = c.found;
        strncat(full, "/", sizeof full - strlen(full) - 1);
        strncat(full, comp, sizeof full - strlen(full) - 1);
    }
    snprintf(cur.path, sizeof cur.path, "%s", full[0] ? full : "/");
    *out = cur;
    return 0;
}

size_t cd_iso_read(cdimage *cd, const cd_iso_entry *e, uint64_t offset, void *buf, size_t len) {
    if (e->is_dir || offset >= e->size) return 0;
    if (len > e->size - offset) len = (size_t)(e->size - offset);
    uint8_t sec[CD_USER_SECTOR];
    size_t done = 0;
    while (done < len) {
        uint64_t pos = offset + done;
        uint32_t lba = e->lba + (uint32_t)(pos / 2048);
        size_t in = (size_t)(pos % 2048), n = 2048 - in;
        if (n > len - done) n = len - done;
        if (cd_read_user(cd, lba, sec) != 0) break;
        memcpy((uint8_t *)buf + done, sec + in, n);
        done += n;
    }
    return done;
}

typedef struct { cd_iso_cb cb; void *user; char prefix[256]; int depth; cdimage *cd; int stop; } walk_ctx;

static int walk_dir(walk_ctx *w, uint32_t lba, uint32_t size);

typedef struct { walk_ctx *w; } walk_cb_ctx;

static int walk_entry(const char *name, uint32_t l, uint32_t s, int dir, void *u) {
    walk_ctx *w = (walk_ctx *)u;
    cd_iso_entry e;
    snprintf(e.path, sizeof e.path, "%s/%s", w->prefix, name);
    e.lba = l; e.size = s; e.is_dir = dir;
    if (w->cb(&e, w->user)) { w->stop = 1; return 1; }
    if (dir && w->depth < 8) {
        char saved[256];
        snprintf(saved, sizeof saved, "%s", w->prefix);
        snprintf(w->prefix, sizeof w->prefix, "%s", e.path);
        w->depth++;
        walk_dir(w, l, s);
        w->depth--;
        snprintf(w->prefix, sizeof w->prefix, "%s", saved);
        if (w->stop) return 1;
    }
    return 0;
}

static int walk_dir(walk_ctx *w, uint32_t lba, uint32_t size) {
    return iter_dir(w->cd, lba, size, walk_entry, w);
}

int cd_iso_walk(cdimage *cd, cd_iso_cb cb, void *user) {
    uint32_t lba, size;
    if (root_extent(cd, &lba, &size) != 0) return -1;
    walk_ctx w = {cb, user, "", 0, cd, 0};
    return walk_dir(&w, lba, size) < 0 ? -1 : 0;
}
