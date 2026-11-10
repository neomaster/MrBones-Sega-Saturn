#ifndef MB_NVM_SHADOW_H
#define MB_NVM_SHADOW_H
#include <stddef.h>
#include <stdint.h>

/*
 * Loader backup.o / global.o — _NvmShadow (§13), 0x28 bytes at 0x00228950.
 * Evidence (docs/stage-progress-nvm.md):
 *   - LoadFirstLevel@20b91c zeroes [0..0x1b] of the block its literal 0020b9c0
 *     names (0x00228950), sets byte[4]=1, u32@0x1c=3, u32@0x20=0, then reads
 *     u32@0x1c as the level to load and u32@0x20 as the movie origin: 3 means
 *     INTRO movie then MENU (docs/ghidra-loader-backup.txt).
 *   - BackupLoadNvmShadow@20bce0 copies 0x28 bytes to the same block and
 *     stores "BONE" at +0x24.
 *   - UpdateLevels_CMenuSelector@23c5a8 reads one byte per level-select entry
 *     at 0x00228950 + offset, offsets 4..0xa and 0x12..0x17: the directory ID
 *     of the stage (04RUNWAY..10BAT3, 18RIDE..23MOAT)
 *     (docs/ghidra-menu-level-table.txt).
 *   - FINALE's ShootRing writes 1 to byte +1 when the ring is cleared
 *     (docs/finale-ring-analysis.md).
 * Layout:
 *   [0]          unknown, kept as found
 *   [1]          campaign completed flag (FINALE)
 *   [2..3]       unknown, kept as found
 *   [4..0x1b]    level bytes indexed by directory ID: byte[ID] for
 *                04RUNWAY..27FINALE, i.e. byte[4 + campaign_index]; 0 = locked.
 *                byte[4]=1 on an empty save is RUNWAY unlocked.
 *   [0x1c..0x1f] u32 BE directory ID of the level to load at boot (3 = MENU)
 *   [0x20..0x23] u32 BE directory ID of the movie origin (0 = none)
 *   [0x24..0x27] "BONE" tag, written by the loader at load; kept as found
 * Bytes the host does not model survive load, win, save and disc switch.
 * Host: persist to <save_dir>/nvmshadow.bin (config save_dir).
 */

#define MB_NVM_SHADOW_SIZE 0x28u
#define MB_NVM_COMPLETED_OFF 1u
#define MB_NVM_LEVEL_TABLE_OFF 4u
#define MB_NVM_LEVEL_COUNT 24u
#define MB_NVM_DEFAULT_LEVEL_OFF 0x1cu
#define MB_NVM_DEFAULT_EXTRA_OFF 0x20u /* movie origin directory ID */
#define MB_NVM_TAG_OFF 0x24u
#define MB_NVM_MENU_DIR_ID 3u
/* Directory ID (DirNameTable prefix) of a campaign index: 0 → 4 (04RUNWAY). */
#define MB_NVM_LEVEL_ID(campaign_index) ((uint32_t)(campaign_index) + MB_NVM_LEVEL_TABLE_OFF)

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

/* Level byte table by campaign index: 0 = RUNWAY .. 23 = FINALE, at byte
 * [4 + index] = byte[directory ID]. Indices from MB_NVM_LEVEL_COUNT on read as
 * locked and are never written. */
int mb_nvm_level_unlocked(const mb_nvm_shadow *nvm, unsigned level_index);
void mb_nvm_unlock_level(mb_nvm_shadow *nvm, unsigned level_index);

#endif
