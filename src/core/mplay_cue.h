#ifndef MB_MPLAY_CUE_H
#define MB_MPLAY_CUE_H
#include <stddef.h>
#include <stdint.h>

/*
 * Mplaynext @0x25df78 / Mnext @0x25dea4 — cue table stride 0xc:
 *   +0 s16 duration-ish, +2 s16 param, +4 u32 mid, +8 code/fn word.
 * Mplaynext temporarily peeks counter+1 and calls helper 0x25dbe0 with
 * (entry.word2, (short)word0, (short)word1, 0). Mnext increments the ushort
 * counter when (*flag & 3) == 3.
 *
 * Host sequencer: hold `duration` frames then bump index (Mnext stand-in).
 */

#define MPLAY_CUE_STRIDE 0xcu
#define MPLAY_DEFAULT_DURATION 8u /* observed at cue tables near 0x233D8C */

typedef struct {
    unsigned index;     /* ushort counter (param+8 in Saturn object) */
    unsigned hold;      /* frames held on current cue */
    unsigned duration;  /* frames per cue step */
    unsigned count;     /* cues / wrap modulus (bank length) */
} mplay_seq;

void mplay_seq_init(mplay_seq *s, unsigned count, unsigned duration);
/* Advance one display frame; returns 1 if index changed. */
int mplay_seq_step(mplay_seq *s);
/* Frame within 0..count-1. */
unsigned mplay_seq_frame(const mplay_seq *s);

#endif
