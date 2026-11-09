#include "core/cdimage.h"
#include <stdio.h>
#include <string.h>
int cb(const cd_iso_entry *e, void *u) {
  int *n = u;
  const char *p;
  if (!e || !e->is_dir) return 0;
  p = e->path;
  if (p[0]!='/' || strchr(p+1,'/')) return 0;
  printf("%d %s\n", (*n)++, p);
  return 0;
}
int main(void) {
  char err[256]; cdimage cd; int n=0;
  if (cd_open(&cd, "F:/MrBonesPort/disco1/Mr. Bones (USA) (Disc 1).cue", err, sizeof err)) {
    fprintf(stderr,"%s\n",err); return 1;
  }
  cd_iso_walk(&cd, cb, &n);
  cd_close(&cd);
  printf("total_top=%d\n", n);
  return 0;
}
