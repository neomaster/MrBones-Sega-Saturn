#ifndef MB_NVM_SHADOW_H
#define MB_NVM_SHADOW_H
#include <stddef.h>
#include <stdint.h>

/*
 * Loader backup.o / global.o — _NvmShadow (§13).
 * MAP: common size 0x28 @0x228980; BackupLoad/SaveNvmShadow copy 0x28 bytes.
 * LoadFirstLevel@20b91c (docs/ghidra-loader-backup.txt): on an empty backup it
 * zeroes bytes [0..0x1b], sets byte[4]=1, u32@0x1c=3 and u32@0x20=0.
 *
 * Host layout — a reading of those facts, not a decoded structure
 * (docs/stage-progress-nvm.md):
 *   [0..3]       header, kept as found
 *   [4..0x1b]    24 level bytes, one per campaign dir 04RUNWAY..27FINALE in
 *                DirNameTable order, 0 = locked. byte[4]=1 on an empty save is
 *                RUNWAY unlocked. The menu's UpdateLevels reads one byte per
 *                entry at a per-entry offset (docs/ghidra-menu.txt); those
 *                offsets were not recovered, so this order is the host's.
 *   [0x1c..0x1f] u32 BE level word: campaign index of the next stage to play
 *                (menu_phase writes the selected one, stage_progress the next)
 *   [0x20..0x27] two u32, kept as found (0x20 is 0 on an empty save)
 * Bytes the host does not model survive load, win, save and disc switch.
 * Host: persist to <save_dir>/nvmshadow.bin (config save_dir).
 */

#define MB_NVM_SHADOW_SIZE 0x28u
#define MB_NVM_LEVEL_TABLE_OFF 4u
#define MB_NVM_LEVEL_COUNT 24u
#define MB_NVM_DEFAULT_LEVEL_OFF 0x1cu
#define MB_NVM_DEFAULT_EXTRA_OFF 0x20u

typedef struct {
    uint8_t bytes[MB_NVM_SHADOW_SIZE];
    int loaded; /* 1 if file existed */
    int dirty;
} mb_nvm_shadow;

void mb_nvm_init_default(mb_nvm_shadow *nvm);
/* 0=ok (missing file → defaults, loaded=0), -1=IO error on existing file. */
int mb_nvm_load(mb_nvm_shadow *nvm, const char *save_dir);
int mb_nvm_save(const mb_nvm_shadow *nvm, const char *save_dir);

uint32_t mb_nvm_get_u32(const mb_nvm_shadow *nvm, unsigned off);
void mb_nvm_set_u32(mb_nvm_shadow *nvm, unsigned off, uint32_t v);
uint8_t mb_nvm_get_u8(const mb_nvm_shadow *nvm, unsigned off);
void mb_nvm_set_u8(mb_nvm_shadow *nvm, unsigned off, uint8_t v);

/* Level byte table: index 0 = RUNWAY .. 23 = FINALE, at [4 + index]. Indices
 * from MB_NVM_LEVEL_COUNT on read as locked and are never written. */
int mb_nvm_level_unlocked(const mb_nvm_shadow *nvm, unsigned level_index);
void mb_nvm_unlock_level(mb_nvm_shadow *nvm, unsigned level_index);

#endif
