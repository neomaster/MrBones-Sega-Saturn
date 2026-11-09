#include "core/cdimage.h"
#include "core/resource.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *files[] = {"02TITL","03CODE","04STXT","05SND1","15PAN",NULL};
static void probe(cdimage *cd, const char *dir) {
  char path[128]; int i; void *d; size_t n;
  printf("%s\n", dir);
  for (i=0; files[i]; i++) {
    snprintf(path,sizeof path,"%s/%s",dir,files[i]);
    d=NULL;n=0;
    if (resource_load(cd,path,&d,&n)==0) { printf("  %s size=%zu\n", files[i], n); free(d); }
    else printf("  %s MISSING\n", files[i]);
  }
}
int main(int argc,char**argv){
  char err[256]; cdimage cd;
  const char *dirs[]={"/04RUNWAY","/05MAUSO","/06VALLEY","/08BAT1","/11LOGS","/17VORTEX","/28LOSE","/18RIDE",NULL};
  int i;
  if(cd_open(&cd,argv[1],err,sizeof err)){fprintf(stderr,"%s\n",err);return 1;}
  for(i=0;dirs[i];i++) probe(&cd,dirs[i]);
  cd_close(&cd); return 0;
}
