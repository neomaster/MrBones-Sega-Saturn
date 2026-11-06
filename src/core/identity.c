#include "identity.h"
#include "sha256.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static void seterr(char *err, size_t n, const char *fmt, const char *a) {
    if (err && n) snprintf(err, n, fmt, a ? a : "");
}

int manifest_load(id_manifest *m, const char *path, char *err, size_t errlen) {
    memset(m, 0, sizeof *m);
    FILE *f = fopen(path, "r");
    if (!f) { seterr(err, errlen, "cannot open manifest: %s", path); return -1; }
    char line[1024];
    int cap = 0, have_product = 0;
    while (fgets(line, sizeof line, f)) {
        char *s = line;
        while (isspace((unsigned char)*s)) s++;
        if (!*s || *s == '#') continue;
        size_t L = strlen(s);
        while (L && isspace((unsigned char)s[L - 1])) s[--L] = 0;
        if (!strncmp(s, "product ", 8)) {
            if (sscanf(s + 8, "%15s %15s %15s %7s", m->product, m->version, m->date, m->area) != 4) goto bad;
            have_product = 1;
        } else if (!strncmp(s, "disc ", 5)) {
            int n; unsigned long long sz; char sha[65];
            if (sscanf(s + 5, "%d track01 %llu %64s", &n, &sz, sha) != 3 || n < 1 || n > ID_MAX_DISCS || strlen(sha) != 64) goto bad;
            m->track01_size[n] = sz;
            snprintf(m->track01_sha[n], 65, "%s", sha);
        } else if (!strncmp(s, "file ", 5)) {
            int n, off = 0; char kind[8], sha[65]; unsigned lba, size;
            if (sscanf(s + 5, "%d %7s %u %u %64s %n", &n, kind, &lba, &size, sha, &off) < 5 || off <= 0 || strlen(sha) != 64) goto bad;
            if (m->nfiles == cap) {
                cap = cap ? cap * 2 : 256;
                id_file *nf = (id_file *)realloc(m->files, (size_t)cap * sizeof *nf);
                if (!nf) { fclose(f); manifest_free(m); return -1; }
                m->files = nf;
            }
            id_file *e = &m->files[m->nfiles++];
            e->disc = n; e->core = !strcmp(kind, "core"); e->lba = lba; e->size = size;
            snprintf(e->sha256, 65, "%s", sha);
            snprintf(e->path, sizeof e->path, "%s", s + 5 + off);
        } else goto bad;
    }
    fclose(f);
    if (!have_product || m->nfiles == 0) { seterr(err, errlen, "manifest incomplete: %s", path); manifest_free(m); return -1; }
    return 0;
bad:
    fclose(f);
    seterr(err, errlen, "manifest has a malformed line: %s", line);
    manifest_free(m);
    return -1;
}

void manifest_free(id_manifest *m) { free(m->files); m->files = NULL; m->nfiles = 0; }

static void fail(id_result *r, const char *fmt, const char *a, const char *b, const char *c) {
    r->ok = 0;
    r->files_failed++;
    if (!r->message[0]) snprintf(r->message, sizeof r->message, fmt, a ? a : "", b ? b : "", c ? c : "");
}

static void trim_field(const uint8_t *src, size_t n, char *out) {
    size_t k = 0;
    for (size_t i = 0; i < n; i++) out[k++] = (char)src[i];
    while (k && (out[k - 1] == ' ' || out[k - 1] == 0)) k--;
    out[k] = 0;
}

static int hash_track01(cdimage *cd, char out[65]) {
    const cd_track *t = cd_track_get(cd, 1);
    FILE *f = t ? fopen(t->path, "rb") : NULL;
    if (!f) return -1;
    sha256_ctx c; uint8_t d[32]; static uint8_t buf[1 << 20];
    sha256_init(&c);
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) sha256_update(&c, buf, n);
    fclose(f);
    sha256_final(&c, d);
    sha256_hex(d, out);
    return 0;
}

static int hash_iso_file(cdimage *cd, const cd_iso_entry *e, char out[65]) {
    sha256_ctx c; uint8_t d[32]; static uint8_t buf[1 << 16];
    sha256_init(&c);
    uint64_t off = 0;
    while (off < e->size) {
        size_t n = cd_iso_read(cd, e, off, buf, sizeof buf);
        if (n == 0) return -1;
        sha256_update(&c, buf, n);
        off += n;
    }
    sha256_final(&c, d);
    sha256_hex(d, out);
    return 0;
}

int identity_check(const id_manifest *m, cdimage *cd, int expected_disc, id_mode mode, id_result *r) {
    memset(r, 0, sizeof *r);
    r->ok = 1;
    uint8_t ip[CD_USER_SECTOR];
    if (cd_read_user(cd, 0, ip) != 0) { fail(r, "cannot read sector 0 of the data track", 0, 0, 0); return 0; }
    if (memcmp(ip, "SEGA SEGASATURN ", 16) != 0) { fail(r, "not a Sega Saturn disc (no SEGA SEGASATURN header)", 0, 0, 0); return 0; }
    char product[16], version[16], date[16], dev[16], area[8];
    trim_field(ip + 0x20, 10, product);
    trim_field(ip + 0x2A, 6, version);
    trim_field(ip + 0x30, 8, date);
    trim_field(ip + 0x38, 8, dev);
    trim_field(ip + 0x40, 1, area);
    if (strcmp(product, m->product) || strcmp(version, m->version) || strcmp(date, m->date) || strcmp(area, m->area)) {
        char got[96];
        snprintf(got, sizeof got, "%s %s %s %s", product, version, date, area);
        char want[96];
        snprintf(want, sizeof want, "%s %s %s %s", m->product, m->version, m->date, m->area);
        fail(r, "unsupported version: disc is '%s', supported is '%s'", got, want, 0);
        return 0;
    }
    int n = 0;
    if (sscanf(dev, "CD-%d/2", &n) != 1 || n < 1 || n > ID_MAX_DISCS) { fail(r, "cannot read the disc number from '%s'", dev, 0, 0); return 0; }
    r->disc_number = n;
    if (expected_disc && expected_disc != n) {
        char a[8], b[8];
        snprintf(a, sizeof a, "%d", expected_disc); snprintf(b, sizeof b, "%d", n);
        fail(r, "expected disc %s but this is disc %s", a, b, 0);
        return 0;
    }
    for (int i = 0; i < m->nfiles; i++) {
        const id_file *e = &m->files[i];
        if (e->disc != n) continue;
        if (!e->core && mode != ID_FULL) continue;
        r->files_checked++;
        cd_iso_entry ent;
        if (cd_iso_lookup(cd, e->path, &ent) != 0) { fail(r, "file missing from disc: %s", e->path, 0, 0); continue; }
        if (ent.lba != e->lba || ent.size != e->size) {
            char a[64];
            snprintf(a, sizeof a, "lba %u size %u", ent.lba, ent.size);
            fail(r, "file layout differs: %s (%s)", e->path, a, 0);
            continue;
        }
        char hex[65];
        if (ent.size == 0) { sha256_ctx c; uint8_t d[32]; sha256_init(&c); sha256_final(&c, d); sha256_hex(d, hex); }
        else if (hash_iso_file(cd, &ent, hex) != 0) { fail(r, "cannot read file: %s", e->path, 0, 0); continue; }
        if (strcmp(hex, e->sha256)) fail(r, "hash mismatch: %s", e->path, 0, 0);
    }
    if (mode == ID_FULL) {
        r->files_checked++;
        char hex[65];
        if (hash_track01(cd, hex) != 0) fail(r, "cannot read Track 01", 0, 0, 0);
        else if (strcmp(hex, m->track01_sha[n]) != 0) fail(r, "Track 01 image hash differs from the supported dump", 0, 0, 0);
    }
    return r->ok;
}
