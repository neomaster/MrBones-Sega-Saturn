#include "core/cdimage.h"
#include "core/runway_phase.h"
#include "core/saturn_pad.h"
#include <stdio.h>
#include <string.h>
static int try_stage(const char *cue, int disc, const char *iso) {
  char err[256]; cdimage cd; runway_phase ph; saturn_pad pad={0};
  int i, rc;
  if (cd_open(&cd, cue, err, sizeof err)) { printf("%s open fail %s\n", iso, err); return -1; }
  memset(&ph,0,sizeof ph);
  rc = runway_phase_load_dir(&ph, &cd, iso);
  if (rc) { printf("%s LOAD FAIL\n", iso); cd_close(&cd); return -1; }
  printf("%s LOAD OK pan=%zu coll ok anim0=%u anim1=%u status=%s\n",
    iso, ph.pan_blob_size, ph.anim_bank_count[0], ph.anim_bank_count[1], ph.status);
  for (i=0;i<60 && !runway_phase_done(&ph);i++) {
    saturn_pad_update_host(&pad, MB_PAD_RIGHT);
    runway_phase_step(&ph, &pad);
  }
  printf("  after60 state=%d x=%u frames=%u status=%s\n", (int)ph.state, ph.bones_x/2341u, ph.frames, ph.status);
  runway_phase_free(&ph); cd_close(&cd); return 0;
}
int main(void){
  const char *d1="F:/MrBonesPort/disco1/Mr. Bones (USA) (Disc 1).cue";
  const char *d2="F:/MrBonesPort/disco2/Mr. Bones (USA) (Disc 2).cue";
  try_stage(d1,1,"/04RUNWAY");
  try_stage(d1,1,"/11LOGS");
  try_stage(d2,2,"/20ICE1");
  try_stage(d2,2,"/18RIDE");
  try_stage(d2,2,"/23MOAT");
  try_stage(d2,2,"/24STAING");
  return 0;
}
