#include "runway_input.h"
int runway_input_step(uint32_t *position_x, uint32_t is_tall, uint8_t input0,
                      uint8_t input4, runway_input_emit emit, void *context) {
    if (!position_x || !emit) return 0;
    if (input0 & 0x40) *position_x += is_tall ? 1310u : 3932u;
    else if (input0 & 0x80) { if (is_tall) *position_x -= 2621u; }
    else if (!is_tall) *position_x += 1966u;
    if (input0 & 0x20) emit(context, 0x00010700u, 1);
    else if (input4 & 0x02) {
        emit(context, 0x00010600u, 1);
        emit(context, 0x00020600u, 0x11);
    }
    return 1;
}
