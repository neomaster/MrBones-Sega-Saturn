#ifndef MB_BONES_TALL_H
#define MB_BONES_TALL_H
#include <stdint.h>

/*
 * TallSet_26D784 / TallBrain_26D68C path (§12m).
 * Current config/brain slot lives at *0x28589C; TallSet writes *0x28DB8C.
 * Crouch bnscfg set (8 slots) from TallSet PC literals @0x26d7d4..f0:
 *   0x2873EC, 0x2874E4, 0x2876D4, 0x2877CC,
 *   0x2879BC, 0x287AB4, 0x287F8C, 0x288084
 * Stand peeks MSG_DOWN 0x10700 → loader 0x204128(0xc) enqueue + BrainSwitch_25d534.
 */

#define BONES_CROUCH_SLOT_COUNT 8u
#define BONES_BRAIN_STAND 0u /* host sentinel: not in crouch set */
#define BONES_CROUCH_SLOT0 UINT32_C(0x002873EC)
#define BONES_SCHED_SLOT_DOWN 0xcu /* 0x204128(0xc) table index */

extern const uint32_t bones_crouch_slots[BONES_CROUCH_SLOT_COUNT];

/* TallSet_26D784: 1 iff brain_slot ∈ crouch set. */
int bones_tall_set(uint32_t brain_slot);
int bones_is_crouch_slot(uint32_t brain_slot);

/* Pick crouch slot by index 0..7 (TallBrain table walk stand-in). */
uint32_t bones_crouch_slot_at(unsigned index);

#endif
