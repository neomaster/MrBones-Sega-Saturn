#include "core/cdimage.h"
#include "core/resource.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t sz(cdimage *cd, const char *dir, const char *f) {
  char p[96]; void *d=NULL; size_t n=0;
  snprintf(p,sizeof p,"%s/%s",dir,f);
  if(resource_load(cd,p,&d,&n)==0){free(d);return n;} return 0;
}
int main(void){
  char err[256]; cdimage d1,d2;
  const char *dirs[]={"/04RUNWAY","/11LOGS","/20ICE1","/21ICE2","/23MOAT","/18RIDE","/24STAING","/16BIGBNS","/25GENRM",NULL};
  cd_open(&d1,"F:/MrBonesPort/disco1/Mr. Bones (USA) (Disc 1).cue",err,sizeof err);
  cd_open(&d2,"F:/MrBonesPort/disco2/Mr. Bones (USA) (Disc 2).cue",err,sizeof err);
  printf("%-12s %10s %10s %10s %10s %10s | d2code d2pan\n","dir","code","stxt","snd1","pan","avi");
  for(int i=0;dirs[i];i++){
    printf("%-12s %10zu %10zu %10zu %10zu %10zu | %10zu %10zu\n", dirs[i],
      sz(&d1,dirs[i],"03CODE"), sz(&d1,dirs[i],"04STXT"), sz(&d1,dirs[i],"05SND1"),
      sz(&d1,dirs[i],"15PAN"), sz(&d1,dirs[i],"20AVI"),
      sz(&d2,dirs[i],"03CODE"), sz(&d2,dirs[i],"15PAN"));
  }
  /* MENU STXTR */
  printf("MENU 04STXT d1=%zu\n", sz(&d1,"/03MENU","04STXT"));
  cd_close(&d1); cd_close(&d2); return 0;
}
