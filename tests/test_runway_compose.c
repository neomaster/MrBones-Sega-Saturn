#include "core/runway_compose.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void) {
    uint8_t dst[4 * 4 * 4];
    uint8_t bg[2 * 2 * 4] = {
        10, 20, 30, 255,  11, 21, 31, 255,
        12, 22, 32, 255,  13, 23, 33, 255
    };
    uint8_t spr[2 * 2 * 4] = {
        200, 0, 0, 255,  0, 0, 0, 0,
        0, 0, 0, 0,      0, 255, 0, 128
    };

    CHECK(runway_compose_clear(dst, 4, 4, 1, 2, 3, 4) == 0);
    CHECK(dst[0] == 1 && dst[1] == 2 && dst[2] == 3 && dst[3] == 4);
    CHECK(dst[4 * 15 + 0] == 1);

    CHECK(runway_compose_blit_opaque(dst, 4, 4, bg, 2, 2, 1, 1) == 0);
    CHECK(dst[4 * (1 * 4 + 1) + 0] == 10);
    CHECK(dst[4 * (2 * 4 + 2) + 2] == 33);

    memset(dst, 0, sizeof dst);
    runway_compose_clear(dst, 4, 4, 9, 9, 9, 255);
    CHECK(runway_compose_blit_sprite(dst, 4, 4, spr, 2, 2, 0, 0) == 0);
    CHECK(dst[0] == 200 && dst[1] == 0 && dst[2] == 0 && dst[3] == 255);
    CHECK(dst[4] == 9 && dst[5] == 9); /* transparent texel left background */
    CHECK(dst[4 * (1 * 4 + 1) + 1] == 255 && dst[4 * (1 * 4 + 1) + 3] == 255);

    /* Partial off-screen blit must not crash and must write the visible part. */
    memset(dst, 0, sizeof dst);
    CHECK(runway_compose_blit_sprite(dst, 4, 4, spr, 2, 2, -1, -1) == 0);
    CHECK(dst[0] == 0 && dst[1] == 255 && dst[2] == 0 && dst[3] == 255);

    /* §12l: VDP1 H-flip — opaque red texel moves from (0,0) to (1,0). */
    {
        uint8_t flip_spr[2 * 2 * 4] = {
            200, 0, 0, 255,  0, 0, 0, 0,
            0, 0, 0, 0,      0, 0, 0, 0
        };
        memset(dst, 0, sizeof dst);
        runway_compose_clear(dst, 4, 4, 9, 9, 9, 255);
        CHECK(runway_compose_blit_sprite_ex(dst, 4, 4, flip_spr, 2, 2, 0, 0, 1) == 0);
        CHECK(dst[0] == 9 && dst[1] == 9); /* (0,0) was transparent after flip */
        CHECK(dst[4] == 200 && dst[5] == 0 && dst[6] == 0 && dst[7] == 255);
    }

    CHECK(runway_compose_clear(NULL, 1, 1, 0, 0, 0, 0) == -1);
    CHECK(runway_compose_blit_opaque(dst, 4, 4, NULL, 2, 2, 0, 0) == -1);

    /* §16 HUD skelbar 14 segs + pause fallback plate. */
    {
        uint8_t hud[320 * 224 * 4];
        memset(hud, 0, sizeof hud);
        CHECK(runway_compose_hud_skelbar(hud, 320, 224, 7, 14, 8, 8) == 0);
        CHECK(hud[(8 * 320 + 8) * 4 + 0] == 40);  /* filled R */
        CHECK(hud[(8 * 320 + 8) * 4 + 1] == 200); /* filled G */
        CHECK(runway_compose_hud_pause(hud, 320, 224, NULL, 0, 0, 1) == 0);
        CHECK(runway_compose_hud_pause(hud, 320, 224, NULL, 0, 0, 0) == 0);
    }
    return 0;
}
