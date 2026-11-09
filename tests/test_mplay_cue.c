#include "core/mplay_cue.h"
#include <stdio.h>
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void) {
    mplay_seq s;
    unsigned i, changed = 0;
    mplay_seq_init(&s, 13, 8);
    CHECK(s.count == 13 && s.duration == 8);
    CHECK(mplay_seq_frame(&s) == 0);
    for (i = 0; i < 7; i++) CHECK(mplay_seq_step(&s) == 0);
    CHECK(mplay_seq_step(&s) == 1);
    CHECK(mplay_seq_frame(&s) == 1);
    for (i = 0; i < 13 * 8; i++)
        if (mplay_seq_step(&s)) changed++;
    CHECK(changed == 13);
    CHECK(mplay_seq_frame(&s) == 1); /* wrapped once back toward 1 after 13 steps from 1 */
    return 0;
}
