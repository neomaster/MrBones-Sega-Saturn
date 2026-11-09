#include "core/cdimage.h"
#include "core/resource.h"
#include "core/runway_assets.h"
#include "core/pan_palette.h"
#include "core/pan_scroll.h"
#include "core/runway_sprite.h"
#include "core/stxtr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void diag(const char *cue, const char *iso) {
  char err[256], path[96]; cdimage cd; runway_assets a; 
  uint32_t pw[1024]; uint8_t pal[4096]={0}; size_t cons=0; int e;
  void *pan=NULL; size_t pansz=0; pan_scroll ps;
  printf("==== %s ====\n", iso);
  if(cd_open(&cd,cue,err,sizeof err)){printf("open %s\n",err);return;}
  if(runway_assets_load_dir(&cd,iso,&a)){printf("assets FAIL\n");cd_close(&cd);return;}
  printf("assets ok code=%zu stxt=%zu snd=%zu title=%zu\n", a.code_size,a.textures_size,a.sound_size,a.title_size);
  e = a.code_size<=0x1ff50 || pan_palette_apply((const uint8_t*)a.code+0x1ff50, a.code_size-0x1ff50, pw,1024,&cons);
  printf("palette@1ff50 err=%d cons=%zu\n", e, cons);
  /* scan for palette */
  { size_t off; int found=0;
    for(off=0; off+8 < a.code_size && off < 0x80000; off+=4) {
      memset(pw,0,sizeof pw); cons=0;
      if(pan_palette_apply((const uint8_t*)a.code+off, a.code_size-off, pw,1024,&cons)==0 && cons>8) {
        printf("palette hit off=0x%zx cons=%zu first=%08X\n", off, cons, pw[0]);
        found=1; break;
      }
    }
    if(!found) printf("no palette in first 512k\n");
  }
  snprintf(path,sizeof path,"%s/15PAN",iso);
  if(resource_load(&cd,path,&pan,&pansz)){printf("15PAN miss\n");}
  else {
    printf("15PAN size=%zu\n", pansz);
    /* try with zero palette bank */
    memset(pal+3*1024, 0x80, 1024);
    if(pan_scroll_init(&ps,(const uint8_t*)pan,pansz, pal+3*1024)) printf("pan_scroll_init FAIL\n");
    else { printf("pan_scroll OK count=%u\n", ps.count); pan_scroll_free(&ps); }
    free(pan);
  }
  /* try sprite resolve RUNWAY addrs */
  { stxtr_record r; uint16_t pmod; 
    int r1=runway_sprite_resolve(a.code,a.code_size,0x23204c,a.textures,a.textures_size,&r,&pmod);
    printf("sprite morph0 resolve=%d\n", r1);
  }
  /* count stxtr records */
  { size_t off=0; int n=0, rc; stxtr_record r;
    while((rc=stxtr_next(a.textures,a.textures_size,&off,&r))==1) n++;
    printf("stxtr records=%d endrc=%d\n", n, rc);
  }
  runway_assets_free(&a); cd_close(&cd);
}
int main(void){
  diag("F:/MrBonesPort/disco1/Mr. Bones (USA) (Disc 1).cue","/11LOGS");
  diag("F:/MrBonesPort/disco2/Mr. Bones (USA) (Disc 2).cue","/20ICE1");
  diag("F:/MrBonesPort/disco2/Mr. Bones (USA) (Disc 2).cue","/18RIDE");
  return 0;
}
