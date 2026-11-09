#include "core/cdda.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
int cd_read_audio(cdimage *cd, int track, uint32_t frame, uint8_t out[CD_RAW_SECTOR]) {
    (void)cd;
    if (track != 2 || frame != 7) return -1;
    memset(out, 0, CD_RAW_SECTOR);
    out[0] = 0xff; out[1] = 0x7f; out[2] = 0; out[3] = 0x80;
    out[4] = 0xff; out[5] = 0xff;
    return 0;
}
int main(void) {
    cdimage cd = {0};
    int16_t samples[CDDA_SAMPLES_PER_SECTOR] = {0};
    CHECK(cdda_read_sector(&cd, 2, 7, samples) == 0);
    CHECK(samples[0] == 32767 && samples[1] == -32768 && samples[2] == -1 && samples[3] == 0);
    CHECK(cdda_read_sector(&cd, 2, 8, samples) != 0 && samples[0] == 32767);
    {
        int16_t stereo[] = {32767, 32767, -32768, -32768, 16000, -8000};
        cdda_mix_menu(stereo, 3);
        CHECK(stereo[0] == 24575 && stereo[1] == 24575);
        CHECK(stereo[2] == -24576 && stereo[3] == -24576);
        CHECK(stereo[4] == 3000 && stereo[5] == 3000);
    }
    CHECK(cdda_read_sector(NULL, 2, 7, samples) != 0);
    CHECK(cdda_read_sector(&cd, 2, 7, NULL) != 0);
    return 0;
}

