#include "nvm_shadow.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

void mb_nvm_init_default(mb_nvm_shadow *nvm) {
    if (!nvm) return;
    memset(nvm, 0, sizeof *nvm);
    /* LoadFirstLevel empty-save init: [0..0x1b]=0, byte[4]=1, +0x1c=3, +0x20=0.
     * byte[4] is the first level byte: RUNWAY unlocked, nothing else. */
    nvm->bytes[MB_NVM_LEVEL_TABLE_OFF] = 1;
    mb_nvm_set_u32(nvm, MB_NVM_DEFAULT_LEVEL_OFF, 3u);
    mb_nvm_set_u32(nvm, MB_NVM_DEFAULT_EXTRA_OFF, 0u);
    nvm->loaded = 0;
    nvm->dirty = 1;
}

uint32_t mb_nvm_get_u32(const mb_nvm_shadow *nvm, unsigned off) {
    const uint8_t *p;
    if (!nvm || off + 4u > MB_NVM_SHADOW_SIZE) return 0;
    p = nvm->bytes + off;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void mb_nvm_set_u32(mb_nvm_shadow *nvm, unsigned off, uint32_t v) {
    uint8_t *p;
    if (!nvm || off + 4u > MB_NVM_SHADOW_SIZE) return;
    p = nvm->bytes + off;
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
    nvm->dirty = 1;
}

uint8_t mb_nvm_get_u8(const mb_nvm_shadow *nvm, unsigned off) {
    if (!nvm || off >= MB_NVM_SHADOW_SIZE) return 0;
    return nvm->bytes[off];
}

void mb_nvm_set_u8(mb_nvm_shadow *nvm, unsigned off, uint8_t v) {
    if (!nvm || off >= MB_NVM_SHADOW_SIZE) return;
    nvm->bytes[off] = v;
    nvm->dirty = 1;
}

int mb_nvm_level_unlocked(const mb_nvm_shadow *nvm, unsigned level_index) {
    if (!nvm || level_index >= MB_NVM_LEVEL_COUNT) return 0;
    return nvm->bytes[MB_NVM_LEVEL_TABLE_OFF + level_index] != 0;
}

void mb_nvm_unlock_level(mb_nvm_shadow *nvm, unsigned level_index) {
    if (!nvm || level_index >= MB_NVM_LEVEL_COUNT) return;
    nvm->bytes[MB_NVM_LEVEL_TABLE_OFF + level_index] = 1;
    nvm->dirty = 1;
}

static void path_join(char *out, size_t n, const char *dir, const char *file) {
    size_t len;
    if (!out || n == 0) return;
    out[0] = 0;
    if (!dir || !dir[0]) {
        snprintf(out, n, "%s", file ? file : "");
        return;
    }
    len = strlen(dir);
    if (len > 0 && (dir[len - 1] == '\\' || dir[len - 1] == '/'))
        snprintf(out, n, "%s%s", dir, file);
    else
        snprintf(out, n, "%s\\%s", dir, file);
}

int mb_nvm_load(mb_nvm_shadow *nvm, const char *save_dir) {
    char path[1024];
    FILE *f;
    size_t nread;
    if (!nvm) return -1;
    mb_nvm_init_default(nvm);
    path_join(path, sizeof path, save_dir, "nvmshadow.bin");
    f = fopen(path, "rb");
    if (!f) {
        nvm->loaded = 0;
        return 0; /* missing → defaults */
    }
    nread = fread(nvm->bytes, 1, MB_NVM_SHADOW_SIZE, f);
    fclose(f);
    if (nread != MB_NVM_SHADOW_SIZE) {
        mb_nvm_init_default(nvm);
        return -1;
    }
    nvm->loaded = 1;
    nvm->dirty = 0;
    return 0;
}

static void ensure_dir(const char *dir) {
    char tmp[1024];
    char *p;
    if (!dir || !dir[0]) return;
    snprintf(tmp, sizeof tmp, "%s", dir);
    for (p = tmp; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char c = *p;
            *p = 0;
            if (p > tmp && *(p - 1) != ':') {
#ifdef _WIN32
                _mkdir(tmp);
#else
                mkdir(tmp, 0755);
#endif
            }
            *p = c;
        }
    }
#ifdef _WIN32
    _mkdir(tmp);
#else
    mkdir(tmp, 0755);
#endif
}

int mb_nvm_save(const mb_nvm_shadow *nvm, const char *save_dir) {
    char path[1024];
    FILE *f;
    if (!nvm || !save_dir || !save_dir[0]) return -1;
    ensure_dir(save_dir);
    path_join(path, sizeof path, save_dir, "nvmshadow.bin");
    f = fopen(path, "wb");
    if (!f) return -1;
    if (fwrite(nvm->bytes, 1, MB_NVM_SHADOW_SIZE, f) != MB_NVM_SHADOW_SIZE) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}
