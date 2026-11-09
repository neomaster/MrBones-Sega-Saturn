#include "pan_palette.h"
#include <stdlib.h>
#include <string.h>

static uint32_t read32(const uint8_t *p){return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];}
int pan_palette_apply(const uint8_t *src,size_t size,uint32_t *palette,size_t entries,size_t *consumed){
 if(!src||!palette||!consumed||!entries||size<8||entries>SIZE_MAX/sizeof *palette)return -1;
 uint32_t *tmp=malloc(entries*sizeof *tmp);if(!tmp)return -1;
 memcpy(tmp,palette,entries*sizeof *tmp);size_t index=read32(src),p=4;int result=-1;
 if(index>=entries)goto done;
 while(size-p>=4){uint32_t value=read32(src+p);p+=4;
  if(value&0x80000000u){if(value&0x40000000u){memcpy(palette,tmp,entries*sizeof *tmp);*consumed=p;result=0;goto done;}index=value&0xffffu;if(index>=entries)goto done;}
  else {if(index>=entries)goto done;if(value)tmp[index]=value&0xffffffu;index++;}}
 done:free(tmp);return result;
}
