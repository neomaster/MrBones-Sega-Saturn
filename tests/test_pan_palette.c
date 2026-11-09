#include "core/pan_palette.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do {if(!(c)){printf("FAIL %d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(void){
 uint8_t data[]={0,0,0,1, 1,0x11,0x22,0x33, 0,0,0,0, 0x80,0,0,4, 1,0x44,0x55,0x66, 0xff,0xff,0xff,0xff};
 uint32_t out[]={9,9,9,9,9};size_t n=99;
 CHECK(pan_palette_apply(data,sizeof data,out,5,&n)==0);
 CHECK(n==sizeof data&&out[0]==9&&out[1]==0x112233&&out[2]==9&&out[3]==9&&out[4]==0x445566);
 uint32_t old[5];memcpy(old,out,sizeof out);n=99;
 CHECK(pan_palette_apply(data,sizeof data-1,out,5,&n)==-1&&n==99&&!memcmp(old,out,sizeof out));
 data[15]=5;CHECK(pan_palette_apply(data,sizeof data,out,5,&n)==-1&&!memcmp(old,out,sizeof out));
 CHECK(pan_palette_apply(NULL,sizeof data,out,5,&n)==-1);
 return 0;
}

