#include "core/runway_sprite.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void) {
    uint8_t code[16] = {0x25,0xc0,0x80,0x00, 0,0,0,0, 0x04,0xa0,0,0, 0,0,0x01,0x02};
    uint8_t textures[32] = {0,8,0,2, 0,0,0,1, 0,4, 0x12,0x34,0x56,0x78,0x9a,0xbc};
    stxtr_record record; uint16_t mode = 0;
    CHECK(runway_sprite_resolve(code, sizeof code, 0x00232000, textures, sizeof textures, &record, &mode) == 0);
    CHECK(record.width == 8 && record.height == 2 && record.size == 16 && mode == 0x04a0);
    CHECK(record.data == textures + 16);
    CHECK(runway_sprite_resolve(code, sizeof code - 1, 0x00232000, textures, sizeof textures, &record, &mode) == -1);
    CHECK(runway_sprite_resolve(code, sizeof code, 0x00231fff, textures, sizeof textures, &record, &mode) == -1);
    CHECK(runway_sprite_resolve(code, sizeof code, 0xffffffff, textures, sizeof textures, &record, &mode) == -1);
    code[15] = 3;
    CHECK(runway_sprite_resolve(code, sizeof code, 0x00232000, textures, sizeof textures, &record, &mode) == -1);
    code[15] = 2; code[1] = 0xbf;
    CHECK(runway_sprite_resolve(code, sizeof code, 0x00232000, textures, sizeof textures, &record, &mode) == -1);
    code[1] = 0xc0; code[9] = 0xa8;
    CHECK(runway_sprite_resolve(code, sizeof code, 0x00232000, textures, sizeof textures, &record, &mode) == -1);
    return 0;
}

