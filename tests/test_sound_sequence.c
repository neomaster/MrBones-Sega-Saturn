#include "core/sound_sequence.h"
#include <stdio.h>
#define CHECK(c) do { if (!(c)) {printf("FAIL %d\n", __LINE__);return 1;} } while (0)
typedef struct { unsigned n; uint8_t slots[12],args[3]; } log_t;
static uint8_t call(void *p, uint8_t slot, uint8_t a, uint8_t b, uint8_t c) {
 log_t *l=p; l->slots[l->n++]=slot; l->args[0]=a; l->args[1]=b; l->args[2]=c; return 0x7d;
}
int main(void) { uint32_t counters[2]={0,3}; log_t l={0};
 for (int i=0;i<4;i++) CHECK(mb_sound_start_sequence(counters,0,5,6,7,call,&l)==0x7d);
 for (int i=0;i<3;i++) CHECK(mb_sound_start_sequence(counters,1,5,6,7,call,&l)==0x7d);
 CHECK(l.n==7 && l.slots[0]==0 && l.slots[1]==1 && l.slots[2]==2 && l.slots[3]==0);
 CHECK(l.slots[4]==3 && l.slots[5]==4 && l.slots[6]==3);
 CHECK(l.args[0]==5 && l.args[1]==6 && l.args[2]==7 && counters[0]==1 && counters[1]==4);
 counters[0]=0xffffffffu;mb_sound_start_sequence(counters,0,0,0,0,call,&l);
 CHECK(l.slots[7]==255 && counters[0]==0);return 0;}

