#include "core/pan_collision.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void) {
    uint8_t in[] = {0,0,0,10,0,4, 2,0x31, 0,0x82};
    uint8_t out[4] = {9,9,9,9}; size_t c=0,w=0;
    CHECK(pan_collision_decode(in,sizeof in,out,sizeof out,&c,&w)==0);
    CHECK(c==10 && w==4 && !memcmp(out,"111\x82",4));
    memset(out,9,sizeof out); c=w=7;
    CHECK(pan_collision_decode(in,sizeof in,out,3,&c,&w)==-1 && out[0]==9 && c==7 && w==7);
    CHECK(pan_collision_decode(in,9,out,sizeof out,&c,&w)==-1);
    in[3]=9; CHECK(pan_collision_decode(in,sizeof in,out,sizeof out,&c,&w)==-1);
    in[3]=10;
    in[5]=3; CHECK(pan_collision_decode(in,sizeof in,out,sizeof out,&c,&w)==-1);
    in[5]=0; CHECK(pan_collision_decode(in,sizeof in,out,sizeof out,&c,&w)==-1);
    CHECK(pan_collision_decode(0,sizeof in,out,sizeof out,&c,&w)==-1);
    return 0;
}

