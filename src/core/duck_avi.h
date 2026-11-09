#ifndef MB_DUCK_AVI_H
#define MB_DUCK_AVI_H
#include "duck_tm1.h"
#include <stddef.h>
#include <stdint.h>

/*
 * §20/§22 Saturn TrueMotion (DUCK) AVI host player.
 * Parses RIFF/AVI with movi LIST/"rec " wrappers used by Mr. Bones mfp_avi.
 * §22: decodes 00dc with duck_tm1 using real 19DUCK ydt/cdt/vector tables when
 * supplied (MENU/GUITAR/DRUM/BIGBNS/FINALE/28LOSE); soft block blit fallback.
 * Plays interleaved 01wb PCM via caller-supplied sink.
 */

typedef struct {
    const uint8_t *data; /* frame payload (after 00dc header) */
    unsigned size;
    unsigned pcm_off; /* optional 01wb in same rec; 0 = none */
    unsigned pcm_size;
} duck_avi_frame;

typedef struct {
    const uint8_t *blob;
    size_t blob_size;
    unsigned width, height;   /* stream dims (avih/strf) */
    unsigned us_per_frame;
    unsigned frame_scale, frame_rate, frame_fraction;
    uint64_t elapsed_us;
    unsigned tick_us;
    int tick_external;
    int playback_done;
    int decode_error;
    unsigned frame_count;
    duck_avi_frame *frames;
    size_t frame_capacity;
    /* Soft framebuffer 320×224 RGBA */
    uint8_t *rgba;
    unsigned cur;
    unsigned frames_drawn;
    unsigned tm1_frames; /* frames decoded by TrueMotion1 */
    unsigned soft_frames; /* retained diagnostic; synthetic frames are never produced */
    /* Concatenated PCM (s16le mono or stereo as stored) */
    int16_t *pcm;
    size_t pcm_samples; /* individual interleaved samples */
    size_t pcm_capacity;
    unsigned pcm_rate;
    unsigned pcm_channels;
    int audio_dk3_unsupported;
    void *video_decoder;
    void *audio_decoder;
    unsigned audio_block_align;
    duck_tm1 tm1;
    int tm1_ready;
    char status[128];
} duck_avi;

/* Parse buffer in-place (blob must remain valid). Returns 0 on success.
 * duck19 may be NULL → built-in TM1 tables / soft fallback. */
int duck_avi_open(duck_avi *avi, const void *blob, size_t size);
int duck_avi_open_with_duck(duck_avi *avi, const void *blob, size_t size,
                            const void *duck19, size_t duck_size);
void duck_avi_free(duck_avi *avi);

/* Advance one movie frame into avi->rgba (scaled into 320×224). */
int duck_avi_step(duck_avi *avi);
int duck_avi_tick(duck_avi *avi, unsigned elapsed_us);
int duck_avi_done(const duck_avi *avi);
int duck_avi_playback_done(const duck_avi *avi);
const uint8_t *duck_avi_frame_rgba(const duck_avi *avi);

#endif
