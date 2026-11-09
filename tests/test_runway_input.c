#include "core/runway_input.h"
#include <stdio.h>
#define CHECK(c) do {if (!(c)) {printf("FAIL %d: %s\n",__LINE__,#c);return 1;}} while (0)
typedef struct {uint32_t id[2], value[2];unsigned n;} log_t;
static void on_msg(void *p,uint32_t id,uint32_t value){log_t *l=p;l->id[l->n]=id;l->value[l->n++]=value;}
int main(void) {
 uint32_t p;log_t l={0};
 p=0;CHECK(runway_input_step(&p,0,0x40,0,on_msg,&l) && p==3932 && !l.n);
 p=0;CHECK(runway_input_step(&p,1,0x40,0,on_msg,&l) && p==1310);
 p=0;CHECK(runway_input_step(&p,0,0x80,0,on_msg,&l) && p==0);
 p=0;CHECK(runway_input_step(&p,1,0x80,0,on_msg,&l) && p==(uint32_t)-2621);
 p=0;CHECK(runway_input_step(&p,0,0,0,on_msg,&l) && p==1966);
 p=0;CHECK(runway_input_step(&p,1,0,0,on_msg,&l) && p==0);
 p=0;CHECK(runway_input_step(&p,1,0xc0,0,on_msg,&l) && p==1310);
 p=0;CHECK(runway_input_step(&p,1,0x20,2,on_msg,&l) && l.n==1 && l.id[0]==0x10700 && l.value[0]==1);
 l.n=0;CHECK(runway_input_step(&p,1,0,2,on_msg,&l) && l.n==2 && l.id[0]==0x10600 && l.value[0]==1 && l.id[1]==0x20600 && l.value[1]==17);
 CHECK(!runway_input_step(0,1,0,0,on_msg,&l));CHECK(!runway_input_step(&p,1,0,0,0,&l));
 return 0;
}

