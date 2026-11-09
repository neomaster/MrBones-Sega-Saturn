#include "core/cdimage.h"
#include <stdio.h>
#include <string.h>
static int cb(const cd_iso_entry *e, void *u) {
  (void)u;
  if (!e->is_dir) return 0;
  if (e->path[0]!='/' || !(e->path[1]>='0'&&e->path[1]<='9')) return 0;
  if (strchr(e->path+1,'/')) return 0;
  printf("%s\n", e->path);
  return 0;
}
int main(int argc,char**argv){char err[256];cdimage cd;
 if(cd_open(&cd,argv[1],err,sizeof err)){fprintf(stderr,"%s\n",err);return 1;}
 cd_iso_walk(&cd,cb,NULL); cd_close(&cd); return 0;}
