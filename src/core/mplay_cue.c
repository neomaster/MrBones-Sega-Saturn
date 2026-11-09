#include "mplay_cue.h"

void mplay_seq_init(mplay_seq *s, unsigned count, unsigned duration) {
    if (!s) return;
    s->index = 0;
    s->hold = 0;
    s->duration = duration ? duration : MPLAY_DEFAULT_DURATION;
    s->count = count ? count : 1;
}

int mplay_seq_step(mplay_seq *s) {
    if (!s || s->count == 0) return 0;
    s->hold++;
    if (s->hold < s->duration) return 0;
    s->hold = 0;
    s->index++;
    if (s->index >= s->count) s->index = 0;
    return 1;
}

unsigned mplay_seq_frame(const mplay_seq *s) {
    if (!s || s->count == 0) return 0;
    return s->index % s->count;
}
