#include "bones_tall.h"

const uint32_t bones_crouch_slots[BONES_CROUCH_SLOT_COUNT] = {
    UINT32_C(0x002873EC), UINT32_C(0x002874E4), UINT32_C(0x002876D4),
    UINT32_C(0x002877CC), UINT32_C(0x002879BC), UINT32_C(0x00287AB4),
    UINT32_C(0x00287F8C), UINT32_C(0x00288084)
};

int bones_is_crouch_slot(uint32_t brain_slot) {
    unsigned i;
    for (i = 0; i < BONES_CROUCH_SLOT_COUNT; i++)
        if (bones_crouch_slots[i] == brain_slot) return 1;
    return 0;
}

int bones_tall_set(uint32_t brain_slot) {
    return bones_is_crouch_slot(brain_slot) ? 1 : 0;
}

uint32_t bones_crouch_slot_at(unsigned index) {
    if (index >= BONES_CROUCH_SLOT_COUNT) index = BONES_CROUCH_SLOT_COUNT - 1u;
    return bones_crouch_slots[index];
}
