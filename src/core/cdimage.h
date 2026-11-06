/*
 * cdimage: read a Saturn disc image (.cue + per-track .bin, or a lone MODE1/2352 .bin).
 *
 * Three separate views, kept apart on purpose because the game depends on the difference:
 *   A. ISO files   - cd_iso_*  : named files in the ISO 9660 tree of the data track.
 *   B. Tracks      - cd_track_*, cd_read_audio : the .cue TOC; audio tracks are optional and may be absent.
 *   C. Raw sectors - cd_read_raw/cd_read_user : absolute LBA reads, what the game's GFS/CDC layer issues.
 *
 * LBA convention: data-track LBA 0 is the first sector of the data-track file
 * (the same numbering the ISO 9660 directory records use). FAD = LBA + 150.
 */
#ifndef MB_CDIMAGE_H
#define MB_CDIMAGE_H
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

#define CD_RAW_SECTOR 2352
#define CD_USER_SECTOR 2048
#define CD_FAD_OFFSET 150
#define CD_MAX_TRACKS 99
#define CD_PATH_MAX 1024

typedef enum { CD_TRACK_DATA = 0, CD_TRACK_AUDIO = 1 } cd_track_type;

typedef struct {
    int number;
    cd_track_type type;
    char path[CD_PATH_MAX];
    uint32_t index01_frame;   /* frame offset of INDEX 01 inside the file */
    uint64_t file_size;       /* 0 when the file is missing */
    uint32_t sectors;         /* file_size / 2352 */
    int available;            /* file exists and was opened */
    uint32_t start_lba;       /* disc position of INDEX 01 in data-track LBA units */
    int start_lba_valid;      /* 0 once an earlier track file was missing */
    FILE *fp;
} cd_track;

typedef struct {
    cd_track tracks[CD_MAX_TRACKS];
    int ntracks;
    char cue_path[CD_PATH_MAX];
} cdimage;

typedef struct {
    char path[256];
    uint32_t lba;
    uint32_t size;
    int is_dir;
} cd_iso_entry;

typedef int (*cd_iso_cb)(const cd_iso_entry *e, void *user);

/* Opens a .cue (or a bare .bin treated as one MODE1/2352 data track). 0 = ok, else err filled. */
int cd_open(cdimage *cd, const char *path, char *err, size_t errlen);
void cd_close(cdimage *cd);

/* C: raw sectors of the first data track. 0 = ok, -1 = out of range / not available. */
uint32_t cd_data_sectors(const cdimage *cd);
int cd_read_raw(cdimage *cd, uint32_t lba, uint8_t out[CD_RAW_SECTOR]);
int cd_read_user(cdimage *cd, uint32_t lba, uint8_t out[CD_USER_SECTOR]);

/* B: tracks (1-based number). */
int cd_track_count(const cdimage *cd);
const cd_track *cd_track_get(const cdimage *cd, int number);
/* frame counted from INDEX 01 of that audio track. */
int cd_read_audio(cdimage *cd, int number, uint32_t frame, uint8_t out[CD_RAW_SECTOR]);

/* A: ISO 9660 tree of the data track. Paths use '/', match case-insensitively, ignore ";1". */
int cd_iso_lookup(cdimage *cd, const char *path, cd_iso_entry *out);
size_t cd_iso_read(cdimage *cd, const cd_iso_entry *e, uint64_t offset, void *buf, size_t len);
int cd_iso_walk(cdimage *cd, cd_iso_cb cb, void *user);

#endif
