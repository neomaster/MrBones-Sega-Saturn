#include "saturn_pad.h"
void saturn_pad_update(saturn_pad *pad, uint8_t raw_high, uint8_t raw_low) {
    uint16_t raw = (uint16_t)(((uint16_t)raw_high << 8) | raw_low);
    uint16_t before = pad->held;
    uint16_t current = (uint16_t)(~raw & 0xfff8u);
    pad->previous = before;
    pad->held = current;
    pad->pressed = (uint16_t)((before ^ current) & current);
    pad->released = (uint16_t)((before ^ current) & before);
}
void saturn_pad_update_host(saturn_pad *pad, uint32_t buttons) {
    static const uint16_t mask[] = {0x4000,0x8000,0x1000,0x2000,0x0400,0x0100,0x0200,
                                    0x0800,0x0040,0x0020,0x0010,0x0008,0x0080};
    uint16_t held=0;
    for (unsigned i=0;i<13;i++) if (buttons & (1u << i)) held |= mask[i];
    uint16_t raw=(uint16_t)~held;
    saturn_pad_update(pad,(uint8_t)(raw>>8),(uint8_t)raw);
}
