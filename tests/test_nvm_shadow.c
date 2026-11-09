#include "core/nvm_shadow.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void) {
    mb_nvm_shadow a, b;
    const char *dir = "test_save_nvm";
#ifdef _WIN32
    _mkdir(dir);
#else
    mkdir(dir, 0755);
#endif
    mb_nvm_init_default(&a);
    CHECK(a.bytes[4] == 1);
    CHECK(mb_nvm_get_u32(&a, 0x1c) == 3u);
    CHECK(mb_nvm_level_unlocked(&a, 0) == 1);
    mb_nvm_unlock_level(&a, 2);
    CHECK(mb_nvm_level_unlocked(&a, 2) == 1);
    CHECK(mb_nvm_save(&a, dir) == 0);
    CHECK(mb_nvm_load(&b, dir) == 0);
    CHECK(b.loaded == 1);
    CHECK(memcmp(a.bytes, b.bytes, MB_NVM_SHADOW_SIZE) == 0);
    CHECK(mb_nvm_get_u32(&b, 0x1c) == 3u);
    /* Missing dir → defaults. */
    {
        mb_nvm_shadow c;
        CHECK(mb_nvm_load(&c, "no_such_save_dir_xyz") == 0);
        CHECK(c.loaded == 0);
        CHECK(c.bytes[4] == 1);
    }
    printf("OK nvm_shadow\n");
    return 0;
}
