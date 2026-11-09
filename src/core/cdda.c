#include "cdda.h"

int cdda_read_sector(cdimage *cd, int track, uint32_t frame,
                     int16_t samples[CDDA_SAMPLES_PER_SECTOR]) {
    uint8_t raw[CD_RAW_SECTOR];
    if (!cd || !samples || cd_read_audio(cd, track, frame, raw) != 0) return -1;
    for (size_t i = 0; i < CDDA_SAMPLES_PER_SECTOR; i++) {
        unsigned v = raw[i * 2] | ((unsigned)raw[i * 2 + 1] << 8);
        samples[i] = (int16_t)(v >= 0x8000 ? (int32_t)v - 65536 : (int32_t)v);
    }
    return 0;
}

/* Menu capture matches a 3/8 sum of CD channels after normalizing Ymir volume. */
void cdda_mix_menu(int16_t *samples, size_t stereo_frames) {
    for (size_t i = 0; i < stereo_frames; i++) {
        int32_t mono = ((int32_t)samples[2 * i] + samples[2 * i + 1]) * 3 / 8;
        samples[2 * i] = samples[2 * i + 1] = (int16_t)mono;
    }
}
