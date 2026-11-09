#include "core/bones_message.h"
#include <stdio.h>
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
static int called;
static int cb(void *c, uint32_t m, void *p) { called++; return *(const int *)c + (int)m + *(const int *)p; }
static int wrong(void *c, uint32_t m, void *p) { (void)c; (void)m; (void)p; called+=100; return 999; }
static int negative(void *c, uint32_t m, void *p) { (void)c; (void)m; (void)p; return -1; }
int main(void) {
    uint32_t delta[3]={2,3,4};
    for (unsigned axes=1; axes<8; ++axes) {
        uint32_t p[3]={0xffffffffu,10,20};
        CHECK(mb_bones_move_message(p,delta,axes)==1);
        CHECK(p[0]==((axes&1)?1u:0xffffffffu));
        CHECK(p[1]==10+((axes&2)?3u:0u));
        CHECK(p[2]==20+((axes&4)?4u:0u));
    }
    uint32_t p[3]={1,2,3};
    CHECK(!mb_bones_move_message(p,delta,0) && !mb_bones_move_message(p,delta,8));
    CHECK(p[0]==1 && p[1]==2 && p[2]==3);
    int context=7, payload=11, result=-1;
    const mb_message_entry table[]={ {0xf0,0x20,cb}, {0xf0,0x20,wrong}, {0xff,0x33,wrong}, {0,0,0} };
    CHECK(mb_message_dispatch(table,4,&context,0x25,&payload,&result)==1 && result==55 && called==1);
    CHECK(mb_message_dispatch(table,4,&context,0x44,&payload,&result)==0 && result==0 && called==1);
    CHECK(mb_message_dispatch(table,3,&context,0x44,&payload,&result)==-1 && called==1);
    CHECK(mb_message_dispatch(0,0,&context,0x44,&payload,&result)==-1);
    const mb_message_entry negative_result[]={ {1,1,negative}, {0,0,0} };
    CHECK(mb_message_dispatch(negative_result,2,&context,1,&payload,&result)==1 && result==-1);
    const mb_message_entry first_zero[]={ {0,0,cb} };
    CHECK(mb_message_dispatch(first_zero,1,&context,0,&payload,&result)==1 && result==18 && called==2);
    return 0;
}




