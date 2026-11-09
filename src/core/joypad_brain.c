#include "joypad_brain.h"
#include <string.h>

void mb_joypad_brain_init(mb_joypad_brain *j, mb_pad_read_fn read, void *context) {
    memset(j, 0, sizeof *j);
    j->read = read;
    j->read_context = context;
}

int mb_joypad_brain_step(mb_joypad_brain *j) {
    if (!j || !j->read) return 0;
    const saturn_pad *pad = j->read(j->read_context, 0);
    if (!pad) {
        j->fail_streak++;
        if (j->fail_streak == 30)
            j->bones_input[4] = (uint8_t)(j->bones_input[4] | 0x08);
        return 0;
    }
    j->fail_streak = 0;
    j->bones_input[0] = (uint8_t)(pad->held >> 8);
    j->bones_input[1] = (uint8_t)(pad->held);
    j->bones_input[2] = (uint8_t)(pad->released >> 8);
    j->bones_input[3] = (uint8_t)(pad->released);
    j->bones_input[4] = (uint8_t)(pad->pressed >> 8);
    j->bones_input[5] = (uint8_t)(pad->pressed);
    j->bones_input[6] = (uint8_t)(pad->previous >> 8);
    j->bones_input[7] = (uint8_t)(pad->previous);
    if (j->on_start && j->start_mask && pad->held == j->start_mask)
        j->on_start(j->on_start_context);
    return 1;
}

void mb_joypad_brain_fiber(mb_object *self) {
    mb_joypad_brain *j = (mb_joypad_brain *)self->user;
    while (self->alive && self->sched && !self->sched->stop) {
        mb_joypad_brain_step(j);
        mb_obj_yield(self);
    }
}
