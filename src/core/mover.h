#ifndef MB_MOVER_H
#define MB_MOVER_H
#include <stdint.h>

typedef struct {
    uint32_t position[3];
    uint32_t velocity[3];
    uint32_t acceleration[3];
} mb_mover;

/* Native adaptation of RUNWAY CMover::Move at 0x00259d28. */
void mb_mover_step(mb_mover *mover);
#endif
