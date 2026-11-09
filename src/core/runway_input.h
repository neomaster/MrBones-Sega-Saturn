#ifndef MB_RUNWAY_INPUT_H
#define MB_RUNWAY_INPUT_H
#include <stdint.h>
typedef void (*runway_input_emit)(void *context, uint32_t message, uint32_t value);
/* One observed loop iteration at 0x0026a614. Bits are original BonesInput
 * byte offsets 0 and 4, not yet a mapping to physical controller buttons.
 * The caller schedules the next iteration, corresponding to ObjYeild. */
int runway_input_step(uint32_t *position_x, uint32_t is_tall, uint8_t input0,
                      uint8_t input4, runway_input_emit emit, void *context);
#endif
