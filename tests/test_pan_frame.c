#include "core/pan_frame.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL %d: %s\n",__LINE__,#x);return 1; } } while(0)
static uint8_t pixels[PAN_BUFFER_SIZE], collision[5000], frame[256], backup[PAN_BUFFER_SIZE];
static void header(size_t len){frame[0]=len>>24;frame[1]=len>>16;frame[2]=len>>8;frame[3]=len;frame[4]=0;frame[5]=2;frame[6]=0;frame[7]=0x42;}
static size_t skips(size_t i,unsigned n){while(n>=16){frame[i++]=0xef;n-=16;}if(n)frame[i++]=0xe0|(n-1);return i;}
int main(void){size_t at=8,w=99,consumed=99;
 memset(pixels,0x7b,sizeof pixels);frame[at++]=0xf0;at=skips(at,1120);header(at);
 CHECK(pan_frame_decode(frame,at,pixels,sizeof pixels,1,collision,sizeof collision,&w,&consumed)==0);
 CHECK(w==1&&collision[0]==0x42&&consumed==at&&pixels[0]==0x7b&&pixels[PAN_PLANE_SIZE]==0&&pixels[2*PAN_PLANE_SIZE-1]==0&&pixels[2*PAN_PLANE_SIZE]==0x7b);
 memset(pixels,0x7b,sizeof pixels);at=8;frame[at++]=0x20;for(unsigned j=0;j<64;j++)frame[at++]=(uint8_t)(j+1);at=skips(at,1119);header(at);
 CHECK(pan_frame_decode(frame,at,pixels,sizeof pixels,0,collision,sizeof collision,&w,&consumed)==0);
 for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++) CHECK(pixels[y*320+x]==y*8+x+1);
 CHECK(pixels[8]==0x7b&&pixels[8*320]==0x7b);
 memset(pixels,0x7b,sizeof pixels);at=8;frame[at++]=0x10;frame[at++]=0x33;for(unsigned j=0;j<8;j++)frame[at++]=0xff;at=skips(at,1119);header(at);
 CHECK(pan_frame_decode(frame,at,pixels,sizeof pixels,0,collision,sizeof collision,&w,&consumed)==0);
 for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++) CHECK(pixels[y*320+x]==0x33);
 memset(pixels,0,sizeof pixels);for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)pixels[y*320+x]=(uint8_t)(y*8+x+1);
 at=8;frame[at++]=0x30;frame[at++]=0;frame[at++]=7;at=skips(at,1119);header(at);
 CHECK(pan_frame_decode(frame,at,pixels,sizeof pixels,1,collision,sizeof collision,&w,&consumed)==0);
 for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++) CHECK(pixels[PAN_PLANE_SIZE+y*320+x]==y*8+x+1);
 const int row2[8]={320,320,-320,-320,1,-1,1,-1};
 const int step[8]={647,633,-633,-647,-2238,2238,2242,-2242};
 const int dx[8]={-1,1,-1,1,320,-320,-320,320};
 for(unsigned op=3;op<=10;op++) {
   for(size_t i=0;i<sizeof pixels;i++) pixels[i]=(uint8_t)((i*13+i/320)%251);
   memcpy(backup,pixels,sizeof pixels);at=8;frame[at++]=(uint8_t)(op<<4);frame[at++]=0x0f;frame[at++]=0xa0;
   at=skips(at,1119);header(at);
   CHECK(pan_frame_decode(frame,at,pixels,sizeof pixels,1,collision,sizeof collision,&w,&consumed)==0);
   int a=4000,b=a+row2[op-3];
   for(unsigned y=0;y<4;y++) {
     for(unsigned x=0;x<8;x++) {
       CHECK(pixels[PAN_PLANE_SIZE+(2*y)*320+7-x]==backup[a]);
       CHECK(pixels[PAN_PLANE_SIZE+(2*y+1)*320+7-x]==backup[b]);
       if(x!=7){a+=dx[op-3];b+=dx[op-3];}
     }
     a+=step[op-3];b+=step[op-3];
   }
 }
 memcpy(backup,pixels,sizeof pixels);w=99;consumed=99;CHECK(pan_frame_decode(frame,at-1,pixels,sizeof pixels,0,collision,sizeof collision,&w,&consumed)==-1);
 CHECK(!memcmp(backup,pixels,sizeof pixels)&&w==99&&consumed==99);
 return 0;
}

