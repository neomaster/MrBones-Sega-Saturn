#ifndef MB_NVM_SHADOW_H
#define MB_NVM_SHADOW_H
#include <stddef.h>
#include <stdint.h>

/*
 * Loader backup.o / global.o — _NvmShadow (§13).
 * MAP: common size 0x28 @0x228980; BackupLoad/SaveNvmShadow copy 0x28 bytes.
 * LoadFirstLevel@20b91c: on empty save, zero [0..0x1b], byte[4]=1, +0x1c=3, +0x20=0.
 * Host: persist to <save_dir>/nvmshadow.bin (config save_dir).
 */

#define MB_NVM_SHADOW_SIZE 0x28u
#define MB_NVM_DEFAULT_FLAGS_OFF 4u
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

/* Level unlock byte table stand-in: index 0 = RUNWAY. */
int mb_nvm_level_unlocked(const mb_nvm_shadow *nvm, unsigned level_index);
void mb_nvm_unlock_level(mb_nvm_shadow *nvm, unsigned level_index);

#endif
