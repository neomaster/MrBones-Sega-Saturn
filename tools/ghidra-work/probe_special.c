#include "core/cdimage.h"
#include "core/resource.h"
#include "core/stxtr.h"
#include <stdio.h>
#include <stdlib.h>
static size_t sz(cdimage *cd,const char *d,const char *f){char p[96];void *x=NULL;size_t n=0;snprintf(p,sizeof p,"%s/%s",d,f);if(resource_load(cd,p,&x,&n)==0){free(x);return n;}return 0;}
int main(void){
  char err[256];cdimage d1,d2;int i;
  const char *dirs[]={"/07GUITAR","/14DRUM","/16BIGBNS","/25GENRM","/27FINALE","/28LOSE",NULL};
  cd_open(&d1,"F:/MrBonesPort/disco1/Mr. Bones (USA) (Disc 1).cue",err,sizeof err);
  cd_open(&d2,"F:/MrBonesPort/disco2/Mr. Bones (USA) (Disc 2).cue",err,sizeof err);
  printf("%-12s %8s %8s %8s %8s %8s %8s | d2code d2stxt d2avi\n","dir","code","stxt","snd","pan","duck","avi");
  for(i=0;dirs[i];i++){
    printf("%-12s %8zu %8zu %8zu %8zu %8zu %8zu | %8zu %8zu %8zu\n",dirs[i],
      sz(&d1,dirs[i],"03CODE"),sz(&d1,dirs[i],"04STXT"),sz(&d1,dirs[i],"05SND1"),
      sz(&d1,dirs[i],"15PAN"),sz(&d1,dirs[i],"19DUCK"),sz(&d1,dirs[i],"20AVI"),
      sz(&d2,dirs[i],"03CODE"),sz(&d2,dirs[i],"04STXT"),sz(&d2,dirs[i],"20AVI"));
  }
  /* GUITAR STXT sample */
  {void *t=NULL;size_t n=0,off=0;stxtr_record r;int c=0;
   resource_load(&d1,"/07GUITAR/04STXT",&t,&n);
   printf("GUITAR STXT %zu: ",n);
   while(stxtr_next(t,n,&off,&r)==1 && c<8){printf("%ux%u ",r.width,r.height);c++;}
   printf("\n");free(t);}
  cd_close(&d1);cd_close(&d2);return 0;
}
