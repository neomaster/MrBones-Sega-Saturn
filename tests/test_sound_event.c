#include "core/sound_event.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) {printf("FAIL %d: %s\n", __LINE__,#c);return 1;} } while (0)
static void be32(uint8_t *p, uint32_t v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
int main(void){uint8_t b[76]={0};mb_sound_event e;mb_sound_event_record r;be32(b,2);be32(b+4,12);be32(b+8,0);be32(b+12+4,7);be32(b+44,0x80000000);be32(b+44+4,9);
 CHECK(mb_sound_event_open(&e,b,sizeof b,0)==1);
 CHECK(mb_sound_event_next(&e,&r)==1 && r.words[1]==7);
 CHECK(mb_sound_event_next(&e,&r)==1 && r.words[0]==0x80000000 && r.words[1]==9);
 CHECK(mb_sound_event_next(&e,&r)==0);
 CHECK(mb_sound_event_open(&e,b,sizeof b,1)==0 && mb_sound_event_next(&e,&r)==0);
 CHECK(mb_sound_event_open(&e,b,sizeof b,2)==-1);
 be32(b+4,4);CHECK(mb_sound_event_open(&e,b,sizeof b,0)==-1);
 be32(b+4,12);CHECK(mb_sound_event_open(&e,b,43,0)==-1);
 be32(b+44,0);CHECK(mb_sound_event_open(&e,b,sizeof b,0)==1);CHECK(mb_sound_event_next(&e,&r)==1);CHECK(mb_sound_event_next(&e,&r)==1);CHECK(mb_sound_event_next(&e,&r)==-1);
 return 0;}

