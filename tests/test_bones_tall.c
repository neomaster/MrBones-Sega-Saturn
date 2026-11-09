#include "core/bones_tall.h"
#include <stdio.h>
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void) {
    CHECK(BONES_CROUCH_SLOT_COUNT == 8u);
    CHECK(bones_crouch_slots[0] == 0x002873ECu);
    CHECK(bones_crouch_slots[7] == 0x00288084u);
    CHECK(bones_is_crouch_slot(0x002873EC) == 1);
    CHECK(bones_is_crouch_slot(0x00288084) == 1);
    CHECK(bones_is_crouch_slot(BONES_BRAIN_STAND) == 0);
    CHECK(bones_is_crouch_slot(0x0025FF98) == 0);
    CHECK(bones_tall_set(0x002874E4) == 1);
    CHECK(bones_tall_set(0) == 0);
    CHECK(bones_crouch_slot_at(0) == 0x002873ECu);
    CHECK(bones_crouch_slot_at(99) == 0x00288084u);
    CHECK(BONES_SCHED_SLOT_DOWN == 0xcu);
    return 0;
}
