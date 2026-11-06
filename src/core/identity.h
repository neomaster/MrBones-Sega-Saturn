/*
 * identity: decide whether a disc image is a supported version, from a text manifest.
 *
 * Manifest lines (tools/make_manifest.py writes them; '#' comments ignored):
 *   product <product> <version> <release-date> <area>
 *   disc <n> track01 <bytes> <sha256>
 *   file <n> <core|extra> <lba> <size> <sha256> <iso path>
 *
 * quick: IP.BIN header + every "core" file (loader, per-level code/title).
 * full : quick + every "extra" file + SHA-256 of the whole Track 01 image.
 * Nothing is executed or loaded when a check fails.
 */
#ifndef MB_IDENTITY_H
#define MB_IDENTITY_H
#include "cdimage.h"

#define ID_MAX_DISCS 2

typedef struct {
    int disc;
    int core;
    uint32_t lba;
    uint32_t size;
    char sha256[65];
    char path[256];
} id_file;

typedef struct {
    char product[16];
    char version[16];
    char date[16];
    char area[8];
    uint64_t track01_size[ID_MAX_DISCS + 1];
    char track01_sha[ID_MAX_DISCS + 1][65];
    id_file *files;
    int nfiles;
} id_manifest;

typedef enum { ID_QUICK = 0, ID_FULL = 1 } id_mode;

typedef struct {
    int ok;
    int disc_number;          /* from IP.BIN "CD-n/2" */
    int files_checked;
    int files_failed;
    char message[1024];       /* first failure, human readable */
} id_result;

int manifest_load(id_manifest *m, const char *path, char *err, size_t errlen);
void manifest_free(id_manifest *m);

/* expected_disc 0 = accept whichever disc the header says. Returns result->ok. */
int identity_check(const id_manifest *m, cdimage *cd, int expected_disc, id_mode mode, id_result *r);

#endif
