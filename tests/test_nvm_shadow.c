#include "core/nvm_shadow.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

static void make_dir(const char *dir) {
#ifdef _WIN32
    _mkdir(dir);
#else
    mkdir(dir, 0755);
#endif
}

int main(void) {
    mb_nvm_shadow a, b;
    const char *dir = "test_save_nvm";
    unsigned i;
    make_dir(dir);

    /* LoadFirstLevel empty-save image: [0..0x1b]=0 but byte[4]=1, +0x1c=3, +0x20=0. */
    mb_nvm_init_default(&a);
    for (i = 0; i < MB_NVM_SHADOW_SIZE; i++) {
        if (i == 4u) CHECK(a.bytes[i] == 1);
        else if (i == 0x1fu) CHECK(a.bytes[i] == 3);
        else CHECK(a.bytes[i] == 0);
    }
    CHECK(mb_nvm_get_u32(&a, MB_NVM_DEFAULT_LEVEL_OFF) == 3u);
    CHECK(mb_nvm_get_u32(&a, MB_NVM_DEFAULT_EXTRA_OFF) == 0u);
    CHECK(a.loaded == 0 && a.dirty == 1);

    /* byte[4] is RUNWAY's level byte, not a fifth unlocked level. */
    CHECK(mb_nvm_level_unlocked(&a, 0) == 1);
    for (i = 1; i < MB_NVM_LEVEL_COUNT; i++)
        CHECK(mb_nvm_level_unlocked(&a, i) == 0);
    CHECK(mb_nvm_level_unlocked(&a, MB_NVM_LEVEL_COUNT) == 0);
    CHECK(mb_nvm_level_unlocked(&a, 0xffffffffu) == 0);
    CHECK(mb_nvm_level_unlocked(NULL, 0) == 0);

    /* Unlocks land in [4..0x1b] and nowhere else. */
    a.dirty = 0;
    mb_nvm_unlock_level(&a, 2);
    CHECK(a.bytes[6] == 1 && a.dirty == 1);
    mb_nvm_unlock_level(&a, MB_NVM_LEVEL_COUNT - 1u);
    CHECK(a.bytes[0x1b] == 1);
    CHECK(mb_nvm_level_unlocked(&a, 2) == 1);
    CHECK(mb_nvm_level_unlocked(&a, MB_NVM_LEVEL_COUNT - 1u) == 1);
    a.dirty = 0;
    mb_nvm_unlock_level(&a, MB_NVM_LEVEL_COUNT); /* out of table: no write */
    mb_nvm_unlock_level(NULL, 0);
    CHECK(a.dirty == 0);
    CHECK(mb_nvm_get_u32(&a, MB_NVM_DEFAULT_LEVEL_OFF) == 3u);
    for (i = 0; i < 4u; i++) CHECK(a.bytes[i] == 0);

    /* Bytes the host does not model survive a save and a load. */
    a.bytes[0] = 0xA5; a.bytes[1] = 0x01; a.bytes[2] = 0x02; a.bytes[3] = 0x5A;
    mb_nvm_set_u32(&a, MB_NVM_DEFAULT_EXTRA_OFF, 0x11223344u);
    mb_nvm_set_u32(&a, 0x24u, 0xCAFEF00Du);
    CHECK(a.bytes[0x24] == 0xCA && a.bytes[0x27] == 0x0D); /* big-endian */
    CHECK(mb_nvm_save(&a, dir) == 0);
    CHECK(mb_nvm_load(&b, dir) == 0);
    CHECK(b.loaded == 1 && b.dirty == 0);
    CHECK(memcmp(a.bytes, b.bytes, MB_NVM_SHADOW_SIZE) == 0);
    CHECK(mb_nvm_get_u32(&b, 0x24u) == 0xCAFEF00Du);
    CHECK(mb_nvm_level_unlocked(&b, 0) == 1 && mb_nvm_level_unlocked(&b, 2) == 1);
    CHECK(mb_nvm_level_unlocked(&b, 1) == 0);

    /* Accessors refuse to cross the end of the image. */
    CHECK(mb_nvm_get_u32(&a, 0x25u) == 0);
    CHECK(mb_nvm_get_u8(&a, MB_NVM_SHADOW_SIZE) == 0);
    a.dirty = 0;
    mb_nvm_set_u32(&a, 0x25u, 1u);
    mb_nvm_set_u8(&a, MB_NVM_SHADOW_SIZE, 1u);
    CHECK(a.dirty == 0);
    CHECK(memcmp(a.bytes, b.bytes, MB_NVM_SHADOW_SIZE) == 0);

    /* Missing dir → defaults, loaded=0, success. */
    {
        mb_nvm_shadow c;
        CHECK(mb_nvm_load(&c, "no_such_save_dir_xyz") == 0);
        CHECK(c.loaded == 0);
        CHECK(c.bytes[4] == 1 && mb_nvm_level_unlocked(&c, 0) == 1);
    }

    /* Short file → defaults, loaded=0, and -1 so the caller can tell. */
    {
        mb_nvm_shadow c;
        const char *sdir = "test_save_nvm_short";
        FILE *f;
        make_dir(sdir);
        f = fopen("test_save_nvm_short/nvmshadow.bin", "wb");
        CHECK(f != NULL);
        CHECK(fwrite("0123456789", 1, 10, f) == 10);
        fclose(f);
        CHECK(mb_nvm_load(&c, sdir) == -1);
        CHECK(c.loaded == 0);
        CHECK(mb_nvm_level_unlocked(&c, 0) == 1 && mb_nvm_level_unlocked(&c, 1) == 0);
        CHECK(mb_nvm_get_u32(&c, MB_NVM_DEFAULT_LEVEL_OFF) == 3u);
    }

    printf("OK nvm_shadow\n");
    return 0;
}
