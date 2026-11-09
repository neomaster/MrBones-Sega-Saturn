#ifndef MB_SOUND_SEQUENCE_H
#define MB_SOUND_SEQUENCE_H
#include <stdint.h>

typedef uint8_t (*mb_start_sequence_cb)(void *context, uint8_t slot, uint8_t arg2, uint8_t arg3, uint8_t arg4);
/* Native adaptation of _SoundStartSequence at 0x002041d0. */
uint8_t mb_sound_start_sequence(uint32_t counters[2], int group, uint8_t arg2,
                                uint8_t arg3, uint8_t arg4, mb_start_sequence_cb start, void *context);
#endif
