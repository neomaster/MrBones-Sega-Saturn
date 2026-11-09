#include "core/sprite_decode.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

int main(void) {
    const uint8_t rgb[] = {0x00,0x00, 0x80,0x1f, 0x83,0xe0, 0xfc,0x00, 0xff,0xff};
    const uint8_t expected[] = {0,0,0,0, 255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
    uint8_t out[20];
    assert(mb_sprite_decode(rgb,sizeof rgb,5,1,0x04a8,0,0,out,sizeof out)==0);
    assert(!memcmp(out,expected,sizeof out));
    uint8_t palette[1024] = {0};
    palette[4] = 1; palette[5] = 0x21; palette[6] = 0x43; palette[7] = 0x65;
    const uint8_t indices[] = {0,1};
    assert(mb_sprite_decode(indices,2,2,1,0x04a0,palette,sizeof palette,out,sizeof out)==0);
    const uint8_t expected_indexed[] = {0,0,0,0, 0x21,0x43,0x65,255};
    assert(!memcmp(out,expected_indexed,sizeof expected_indexed));
    memset(out, 0x77, sizeof out);
    assert(mb_sprite_decode(rgb,1,5,1,0x04a8,0,0,out,sizeof out)!=0 && out[0]==0x77);
    assert(mb_sprite_decode(indices,2,2,1,0x04e0,palette,sizeof palette,out,sizeof out)!=0);
    assert(mb_sprite_decode(indices,2,2,1,0x0420,palette,sizeof palette,out,sizeof out)!=0);
    assert(mb_sprite_decode(indices,2,2,1,0x04a0,palette,1023,out,sizeof out)!=0);
    const uint8_t bad[] = {0x01,0x00};
    assert(mb_sprite_decode(bad,2,1,1,0x04a8,0,0,out,sizeof out)!=0 && out[0]==0x77);
    assert(mb_sprite_decode(indices,2,2,1,0x04a0,palette,sizeof palette,out,7)!=0);
    return 0;
}

