#ifndef MB_JOYPAD_BRAIN_H
#define MB_JOYPAD_BRAIN_H
#include "obj_scheduler.h"
#include "saturn_pad.h"
#include <stdint.h>

/*
 * One iteration of RUNWAY JoypadBrain (0x00269774) without the infinite loop:
 * call host pad read, copy held/released/pressed/previous into BonesInput[8],
 * after 30 consecutive NULL reads set bit 0x08 on BonesInput byte 4 (pressed
 * high byte). Yielding is the caller's job (original calls ObjYeild each lap).
 *
 * pad_read returns a saturn_pad snapshot, or NULL on failure (no device).
 */

typedef const saturn_pad *(*mb_pad_read_fn)(void *context, unsigned port);

typedef struct {
    uint8_t bones_input[8];
    unsigned fail_streak;
    uint16_t start_mask; /* compared to held; original literal unresolved here */
    mb_pad_read_fn read;
    void *read_context;
    void (*on_start)(void *context);
    void *on_start_context;
} mb_joypad_brain;

void mb_joypad_brain_init(mb_joypad_brain *j, mb_pad_read_fn read, void *context);
/* Returns 1 on success copy, 0 on NULL pad (fail streak updated). */
int mb_joypad_brain_step(mb_joypad_brain *j);

/* Fiber brain: step once then yield, forever until scheduler stops. */
void mb_joypad_brain_fiber(mb_object *self);

#endif
