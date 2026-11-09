#include "sound_sequence.h"

uint8_t mb_sound_start_sequence(uint32_t counters[2], int group, uint8_t arg2,
                                uint8_t arg3, uint8_t arg4, mb_start_sequence_cb start, void *context) {
    unsigned selected = group != 0;
    uint32_t slot = counters[selected];
    counters[selected] = slot == (selected ? 4u : 2u) ? (selected ? 3u : 0u) : slot + 1u;
    return start(context, (uint8_t)slot, arg2, arg3, arg4);
}
