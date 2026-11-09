/* Synthetic Saturn-like disc: exercises the three cdimage views and the identity check
 * without needing any real disc. */
#include "core/cdimage.h"
#include "core/identity.h"
#include "core/resource.h"
#include "core/runway_assets.h"
#include "core/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
#define NSEC 25

static uint8_t user[NSEC][2048];
static uint8_t code[3000], small[100];

static void le(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void be(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void rec(uint8_t *d, int *pos, const char *name, int nl, uint32_t lba, uint32_t size, int isdir) {
    uint8_t *r = d + *pos;
    int len = 33 + nl;
    if (len & 1) len++;
    r[0] = (uint8_t)len;
    le(r + 2, lba); be(r + 6, lba);
    le(r + 10, size); be(r + 14, size);
    r[25] = isdir ? 2 : 0;
    r[32] = (uint8_t)nl;
    memcpy(r + 33, name, nl);
    *pos += len;
}

static void raw_sector(FILE *f, const uint8_t *u) {
    static const uint8_t sync[12] = {0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0};
    uint8_t s[2352] = {0};
    memcpy(s, sync, 12);
    s[15] = 1;
    memcpy(s + 16, u, 2048);
    fwrite(s, 1, sizeof s, f);
}

static void build(void) {
    memset(user, 0, sizeof user);
    memcpy(user[0], "SEGA SEGASATURN SEGA ENTERPRISES", 32);
    memset(user[0] + 0x20, ' ', 0x40);
    memcpy(user[0] + 0x20, "TEST-001", 8);
    memcpy(user[0] + 0x2A, "V1.000", 6);
    memcpy(user[0] + 0x30, "19960926", 8);
    memcpy(user[0] + 0x38, "CD-1/2  ", 8);
    user[0][0x40] = 'U';
    user[16][0] = 1; memcpy(user[16] + 1, "CD001", 5);
    int p = 0; (void)p;
    uint8_t *root = user[16] + 156;
    root[0] = 34; le(root + 2, 20); le(root + 10, 2048); root[25] = 2; root[32] = 1;
    int pos = 0;
    rec(user[20], &pos, "\0", 1, 20, 2048, 1);
    rec(user[20], &pos, "\1", 1, 20, 2048, 1);
    rec(user[20], &pos, "04RUNWAY", 8, 21, 2048, 1);
    rec(user[20], &pos, "02;1", 4, 24, 100, 0);
    pos = 0;
    rec(user[21], &pos, "\0", 1, 21, 2048, 1);
    rec(user[21], &pos, "\1", 1, 20, 2048, 1);
    rec(user[21], &pos, "03CODE;1", 8, 22, 3000, 0);
    rec(user[21], &pos, "20AVI;1", 7, 23, 0, 0);
    for (int i = 0; i < 3000; i++) code[i] = (uint8_t)(i * 7 + 3);
    for (int i = 0; i < 100; i++) small[i] = (uint8_t)(i * 3);
    memcpy(user[22], code, 2048);
    memcpy(user[23], code + 2048, 952);
    memcpy(user[24], small, 100);
}

static void write_disc(void) {
    FILE *f = fopen("t_disc.bin", "wb");
    for (int i = 0; i < NSEC; i++) raw_sector(f, user[i]);
    fclose(f);
    f = fopen("t_a2.bin", "wb");
    for (int fr = 0; fr < 160; fr++) { uint8_t s[2352]; memset(s, fr & 255, sizeof s); fwrite(s, 1, sizeof s, f); }
    fclose(f);
    f = fopen("t.cue", "w");
    fputs("FILE \"t_disc.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
          "FILE \"t_a2.bin\" BINARY\n  TRACK 02 AUDIO\n    INDEX 00 00:00:00\n    INDEX 01 00:02:00\n"
          "FILE \"t_a3_missing.bin\" BINARY\n  TRACK 03 AUDIO\n    INDEX 00 00:00:00\n    INDEX 01 00:02:00\n", f);
    fclose(f);
}

typedef struct { int n; char first[256]; char order[512]; } walkres;
static int wcb(const cd_iso_entry *e, void *u) {
    walkres *w = (walkres *)u;
    if (!w->n) snprintf(w->first, sizeof w->first, "%s", e->path);
    strncat(w->order, e->path, sizeof w->order - strlen(w->order) - 1);
    strncat(w->order, ",", sizeof w->order - strlen(w->order) - 1);
    w->n++;
    return 0;
}

static void hexof(const void *d, size_t n, char out[65]) {
    sha256_ctx c; uint8_t h[32];
    sha256_init(&c); sha256_update(&c, d, n); sha256_final(&c, h); sha256_hex(h, out);
}

static void write_manifest(const char *product, uint64_t t1size, const char *t1sha) {
    char hc[65], hs[65], he[65];
    hexof(code, 3000, hc); hexof(small, 100, hs); hexof("", 0, he);
    FILE *f = fopen("t_manifest.txt", "w");
    fprintf(f, "# test\nproduct %s V1.000 19960926 U\n", product);
    fprintf(f, "disc 1 track01 %llu %s\n", (unsigned long long)t1size, t1sha);
    fprintf(f, "file 1 core 24 100 %s /02\n", hs);
    fprintf(f, "file 1 core 22 3000 %s /04RUNWAY/03CODE\n", hc);
    fprintf(f, "file 1 extra 23 0 %s /04RUNWAY/20AVI\n", he);
    fclose(f);
}

int main(void) {
    build();
    write_disc();
    char err[256];
    cdimage cd;
    CHECK(cd_open(&cd, "t.cue", err, sizeof err) == 0);

    /* B: tracks */
    CHECK(cd_track_count(&cd) == 3);
    const cd_track *t2 = cd_track_get(&cd, 2), *t3 = cd_track_get(&cd, 3);
    CHECK(t2->type == CD_TRACK_AUDIO && t2->available && t2->sectors == 160);
    CHECK(t2->start_lba == NSEC + 150 && t2->start_lba_valid);
    CHECK(!t3->available && t3->start_lba == NSEC + 160 + 150);
    uint8_t a[2352];
    CHECK(cd_read_audio(&cd, 2, 0, a) == 0 && a[0] == (150 & 255) && a[2351] == (150 & 255));
    CHECK(cd_read_audio(&cd, 2, 9, a) == 0 && a[100] == ((150 + 9) & 255));
    CHECK(cd_read_audio(&cd, 2, 10, a) != 0);   /* past the end */
    CHECK(cd_read_audio(&cd, 3, 0, a) != 0);    /* missing track: clean failure */

    /* C: raw sectors */
    CHECK(cd_data_sectors(&cd) == NSEC);
    uint8_t raw[2352], u[2048];
    CHECK(cd_read_raw(&cd, 0, raw) == 0 && raw[15] == 1 && !memcmp(raw + 16, "SEGA SEGASATURN ", 16));
    CHECK(cd_read_user(&cd, 22, u) == 0 && !memcmp(u, code, 2048));
    CHECK(cd_read_user(&cd, NSEC, u) != 0);

    /* A: ISO files */
    cd_iso_entry e;
    CHECK(cd_iso_lookup(&cd, "/04RUNWAY/03CODE", &e) == 0 && e.lba == 22 && e.size == 3000 && !e.is_dir);
    CHECK(cd_iso_lookup(&cd, "/04runway/03code", &e) == 0 && e.size == 3000);   /* case-insensitive */
    CHECK(cd_iso_lookup(&cd, "04RUNWAY", &e) == 0 && e.is_dir);
    CHECK(cd_iso_lookup(&cd, "/04RUNWAY/NOPE", &e) != 0);
    CHECK(cd_iso_lookup(&cd, "/02/child", &e) != 0);                              /* file is not a dir */
    uint8_t buf[3000];
    CHECK(cd_iso_lookup(&cd, "/04RUNWAY/03CODE", &e) == 0);
    CHECK(cd_iso_read(&cd, &e, 0, buf, sizeof buf) == 3000 && !memcmp(buf, code, 3000));
    CHECK(cd_iso_read(&cd, &e, 2040, buf, 20) == 20 && !memcmp(buf, code + 2040, 20));   /* crosses a sector */
    CHECK(cd_iso_read(&cd, &e, 2990, buf, 100) == 10);                                   /* clamped to size */
    CHECK(cd_iso_read(&cd, &e, 3000, buf, 10) == 0);
    void *loaded = NULL;
    size_t loaded_size = 0;
    CHECK(resource_load(&cd, "/04RUNWAY/03CODE", &loaded, &loaded_size) == 0);
    CHECK(loaded && loaded_size == sizeof code && !memcmp(loaded, code, sizeof code));
    free(loaded);
    CHECK(resource_load(&cd, "/04RUNWAY/20AVI", &loaded, &loaded_size) == 0);
    CHECK(loaded && loaded_size == 0);
    free(loaded);
    CHECK(resource_load(&cd, "/missing", &loaded, &loaded_size) != 0 && !loaded && !loaded_size);
    CHECK(resource_load(&cd, "/04RUNWAY", &loaded, &loaded_size) != 0 && !loaded && !loaded_size);
    runway_assets assets;
    CHECK(runway_assets_load(&cd, &assets) != 0);
    CHECK(!assets.title && !assets.textures && !assets.title_size && !assets.textures_size);
    CHECK(!assets.code && !assets.sound && !assets.code_size && !assets.sound_size);
    runway_assets_free(&assets);
    walkres w = {0, "", ""};
    CHECK(cd_iso_walk(&cd, wcb, &w) == 0 && w.n == 4);
    CHECK(!strcmp(w.order, "/04RUNWAY,/04RUNWAY/03CODE,/04RUNWAY/20AVI,/02,"));
    cd_close(&cd);

    /* identity */
    FILE *f = fopen("t_disc.bin", "rb");
    static uint8_t img[NSEC * 2352];
    CHECK(fread(img, 1, sizeof img, f) == sizeof img);
    fclose(f);
    char t1[65];
    hexof(img, sizeof img, t1);
    write_manifest("TEST-001", sizeof img, t1);
    id_manifest m;
    CHECK(manifest_load(&m, "t_manifest.txt", err, sizeof err) == 0 && m.nfiles == 3);
    id_result r;
    CHECK(cd_open(&cd, "t.cue", err, sizeof err) == 0);
    CHECK(identity_check(&m, &cd, 1, ID_QUICK, &r) == 1 && r.disc_number == 1 && r.files_checked == 2);
    CHECK(identity_check(&m, &cd, 1, ID_FULL, &r) == 1 && r.files_checked == 4);   /* + extra + track01 */
    CHECK(identity_check(&m, &cd, 2, ID_QUICK, &r) == 0 && strstr(r.message, "expected disc 2"));
    cd_close(&cd);
    manifest_free(&m);

    write_manifest("OTHER-99", sizeof img, t1);
    CHECK(manifest_load(&m, "t_manifest.txt", err, sizeof err) == 0);
    CHECK(cd_open(&cd, "t.cue", err, sizeof err) == 0);
    CHECK(identity_check(&m, &cd, 0, ID_QUICK, &r) == 0 && strstr(r.message, "unsupported version"));
    cd_close(&cd);
    manifest_free(&m);

    /* corrupt one byte of 03CODE on disk: hash mismatch must be reported, by path */
    f = fopen("t_disc.bin", "r+b");
    fseek(f, 22 * 2352 + 16 + 5, SEEK_SET);
    fputc(code[5] ^ 0xFF, f);
    fclose(f);
    write_manifest("TEST-001", sizeof img, t1);
    CHECK(manifest_load(&m, "t_manifest.txt", err, sizeof err) == 0);
    CHECK(cd_open(&cd, "t.cue", err, sizeof err) == 0);
    CHECK(identity_check(&m, &cd, 1, ID_QUICK, &r) == 0 && strstr(r.message, "hash mismatch: /04RUNWAY/03CODE"));
    CHECK(identity_check(&m, &cd, 1, ID_FULL, &r) == 0 && r.files_failed >= 2);    /* file + whole image */
    cd_close(&cd);
    manifest_free(&m);

    /* bad inputs */
    CHECK(cd_open(&cd, "no_such.cue", err, sizeof err) != 0);
    f = fopen("t_bad.bin", "wb"); fputs("not a disc image at all", f); fclose(f);
    CHECK(cd_open(&cd, "t_bad.bin", err, sizeof err) != 0);

    remove("t_disc.bin"); remove("t_a2.bin"); remove("t.cue"); remove("t_manifest.txt"); remove("t_bad.bin");
    printf("cdimage + identity: all checks pass\n");
    return 0;
}
