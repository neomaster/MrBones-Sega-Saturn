#ifndef MB_CDDA_H
#define MB_CDDA_H
#include "core/cdimage.h"

/* 588 stereo PCM frames per CD sector at 44,100 Hz. */
#define CDDA_SAMPLES_PER_SECTOR (CD_RAW_SECTOR / 2)
int cdda_read_sector(cdimage *cd, int track, uint32_t frame,
                     int16_t samples[CDDA_SAMPLES_PER_SECTOR]);
void cdda_mix_menu(int16_t *samples, size_t stereo_frames);
#endif
