#include "core/cdimage.h"
#include "core/resource.h"
#include <stdio.h>
#include <stdlib.h>
static const char *dirs[]={
 "/04RUNWAY","/05MAUSO","/06VALLEY","/08BAT1","/09BAT2","/10BAT3","/11LOGS",
 "/15GLASS","/17VORTEX","/18RIDE","/19VILLAGE","/20ICE1","/21ICE2","/22SHADOW",
 "/23MOAT","/24STAING","/26LAFF",NULL};
static const char *files[]={"02TITL","03CODE","04STXT","05SND1","15PAN",NULL};
static void one(cdimage *cd,const char *label){
  int i,j; char path[128]; void *d; size_t n;
  printf("=== %s ===\n", label);
  for(i=0;dirs[i];i++){
    printf("%s", dirs[i]);
    for(j=0;files[j];j++){
      snprintf(path,sizeof path,"%s/%s",dirs[i],files[j]);
      d=NULL;n=0;
      if(resource_load(cd,path,&d,&n)==0){ printf(" %s=%zu", files[j], n); free(d);}
      else printf(" %s=-", files[j]);
    }
    printf("\n");
  }
}
int main(void){char err[256];cdimage a,b;
 if(cd_open(&a,"F:/MrBonesPort/disco1/Mr. Bones (USA) (Disc 1).cue",err,sizeof err)){fprintf(stderr,"%s\n",err);return 1;}
 if(cd_open(&b,"F:/MrBonesPort/disco2/Mr. Bones (USA) (Disc 2).cue",err,sizeof err)){fprintf(stderr,"%s\n",err);return 1;}
 one(&a,"DISC1"); one(&b,"DISC2");
 cd_close(&a); cd_close(&b); return 0;}
