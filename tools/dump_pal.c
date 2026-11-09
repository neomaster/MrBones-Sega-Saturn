
#include "core/cdimage.h"
#include "core/resource.h"
#include "core/pan_palette.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    cdimage cd;
    if (cd_open(&cd, "F:/MrBonesPort/disco1/Mr. Bones (USA) (Disc 1).cue")) return 1;
    void *code = NULL; size_t sz = 0;
    if (resource_load(&cd, "/04RUNWAY/03CODE", &code, &sz)) return 2;
    printf("03CODE size: %zu\n", sz);
    uint32_t words[1024]; size_t consumed = 0;
    for (size_t off = 0; off + 2048 <= sz && off < 0x100000; off += 4) {
        const uint8_t *c = (const uint8_t *)code + off;
        if (c[0] != 0 || c[1] != 0 || c[2] >= 4 || c[4] != 1) continue;
        if (pan_palette_apply(c, sz - off, words, 1024, &consumed) == 0 && consumed >= 2048) {
            printf("Found palette at off=0x%zx, consumed=%zu\n", off, consumed);
            for (int b = 0; b < 4; b++) {
                printf("--- Bank %d (entries %d..%d) ---\n", b, b*256, (b+1)*256-1);
                for (int i = 0; i < 16; i++) {
                    uint32_t w = words[b*256 + i];
                    printf("  [%3d] = 0x%06x (b1=%02x b2=%02x b3=%02x)\n",
                           i, w, (w>>16)&0xff, (w>>8)&0xff, w&0xff);
                }
            }
            break;
        }
    }
    return 0;
}
