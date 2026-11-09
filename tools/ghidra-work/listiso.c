#include "core/cdimage.h"
#include <stdio.h>
#include <string.h>
static int cb(const cd_iso_entry *e, void *u) {
  (void)u;
  if (strstr(e->path, "MENU") || strstr(e->path, "/02") || strstr(e->path, "RUNWAY"))
    printf("%s dir=%d size=%u\n", e->path, e->is_dir, (unsigned)e->size);
  return 0;
}
int main(void) {
  char err[256]; cdimage cd;
  if (cd_open(&cd, "F:/MrBonesPort/disco1/Mr. Bones (USA) (Disc 1).cue", err, sizeof err)) {
    fprintf(stderr, "%s\n", err); return 1;
  }
  cd_iso_walk(&cd, cb, NULL);
  cd_close(&cd); return 0;
}
