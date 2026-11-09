#include "core/saturn_pad.h"
#include "core/runway_input.h"
#include <stdio.h>
#define CHECK(c) do {if (!(c)) {printf("FAIL %d: %s\n",__LINE__,#c);return 1;}} while (0)
typedef struct {uint32_t id[2], value[2];unsigned n;} log_t;
static void emit(void *p,uint32_t id,uint32_t value){log_t *l=p;l->id[l->n]=id;l->value[l->n++]=value;}
int main(void) {
 saturn_pad p={0};saturn_pad_update(&p,0xff,0xff);CHECK(!p.held && !p.pressed && !p.released);
 saturn_pad_update(&p,0xbf,0xff);CHECK(p.held==0x4000 && p.pressed==0x4000 && !p.released && !p.previous);
 saturn_pad_update(&p,0xbf,0xff);CHECK(p.held==0x4000 && !p.pressed && !p.released && p.previous==0x4000);
 saturn_pad_update(&p,0x7d,0xff);CHECK(p.held==0x8200 && p.pressed==0x8200 && p.released==0x4000);
 saturn_pad_update(&p,0xff,0xf8);CHECK(!p.held && !p.pressed && p.released==0x8200);
 saturn_pad_update(&p,0xff,0x07);CHECK(p.held==0x00f8 && p.pressed==0x00f8);
 static const uint16_t expected[]={0x4000,0x8000,0x1000,0x2000,0x0400,0x0100,0x0200,0x0800,0x0040,0x0020,0x0010,0x0008,0x0080};
 for(unsigned i=0;i<13;i++){p=(saturn_pad){0};saturn_pad_update_host(&p,1u<<i);CHECK(p.held==expected[i] && p.pressed==expected[i]);}
 p=(saturn_pad){0};saturn_pad_update_host(&p,MB_PAD_LEFT|MB_PAD_C);uint32_t x=0;log_t log={0};
 CHECK(runway_input_step(&x,0,(uint8_t)(p.held>>8),(uint8_t)(p.pressed>>8),emit,&log));
 CHECK(x==3932 && log.n==2 && log.id[0]==0x10600 && log.id[1]==0x20600);
 saturn_pad_update_host(&p,MB_PAD_LEFT|MB_PAD_C); log.n=0;
 CHECK(runway_input_step(&x,0,(uint8_t)(p.held>>8),(uint8_t)(p.pressed>>8),emit,&log) && !log.n);
 saturn_pad_update_host(&p,0);CHECK(p.released==0x4200 && !p.held);
 return 0;
}

