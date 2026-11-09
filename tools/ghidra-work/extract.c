#include "core/cdimage.h"
#include "core/resource.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
  char err[256]; cdimage cd; void *data=NULL; size_t size=0;
  const char *cue = argc>1?argv[1]:"F:/MrBonesPort/disco1/Mr. Bones (USA) (Disc 1).cue";
  const char *path = argc>2?argv[2]:"/03MENU/03CODE";
  const char *out = argc>3?argv[3]:"F:/MrBonesPort/tools/ghidra-work/03MENU_03CODE.bin";
  if (cd_open(&cd, cue, err, sizeof err)) { fprintf(stderr,"%s\n",err); return 1; }
  if (resource_load(&cd, path, &data, &size)) { fprintf(stderr,"load fail\n"); return 1; }
  FILE *f=fopen(out,"wb"); if(!f){perror(out);return 1;}
  fwrite(data,1,size,f); fclose(f); free(data); cd_close(&cd);
  printf("wrote %s (%zu)\n", out, size); return 0;
}
