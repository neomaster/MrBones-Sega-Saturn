#include "core/duck_avi.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    unsigned char avi[100] = {0};
    duck_avi movie;
    memcpy(avi, "RIFF", 4); avi[4] = 92;
    memcpy(avi + 8, "AVI LIST", 8); avi[16] = 80;
    memcpy(avi + 20, "hdrlLIST", 8); avi[28] = 68;
    memcpy(avi + 32, "strlstrh", 8); avi[40] = 8;
    memcpy(avi + 44, "vidsDUCKstrf", 12); avi[56] = 40;
    memcpy(avi + 76, "DUCK", 4);
    if (duck_avi_open(&movie, avi, sizeof avi) == 0 ||
        !strstr(movie.status, "TrueMotion1 decoder unavailable")) {
        fprintf(stderr, "missing decoder was not reported: %s\n", movie.status);
        return 1;
    }
    return 0;
}
