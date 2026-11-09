#include "core/bones_state.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
static char events[32];
static unsigned n;
static void enter(bones_state_context *c) { (void)c; events[n++]='E'; }
static void leave(bones_state_context *c) { (void)c; events[n++]='X'; }
static void yielded(bones_state_context *c) { (void)c; events[n++]='Y'; }
int main(void) {
    const bones_state a={enter,leave}, b={enter,leave};
    bones_state_context c={0};c.current=&a;c.blocked=&b;c.yield=yielded;c.hit_timeout=1;
    assert(!bones_change_state(&c,&b) && c.current==&a && n==0);
    c.hit_timeout=0;c.previous_clock=1;c.current_clock=2;c.repeat_count=8;
    assert(bones_change_state(&c,&b));assert(!memcmp(events,"XE",2));
    assert(c.last==&a && c.current==&b && c.previous_clock==2 && c.repeat_count==0);
    n=0;c.repeat_count=16;c.position_x=10;c.position_y=5;
    assert(bones_change_state(&c,&a));assert(!memcmp(events,"XE",2));assert(c.repeat_count==17 && c.position_x==10);
    n=0;assert(bones_change_state(&c,&b));assert(!memcmp(events,"XYE",3));
    assert(c.repeat_count==0 && c.position_x==665 && c.position_y==(uint32_t)-650);
    n=0;c.repeat_count=UINT32_MAX;assert(bones_change_state(&c,&a));
    assert(!memcmp(events,"XE",2) && c.repeat_count==0);
    n=0;c.last=&a;bones_first_state(&c,&b);
    assert(n==1 && events[0]=='E' && c.current==&b && c.last==0);
    const uint32_t position[3]={0x01234567,0x89abcdef,0xffffffff};
    bones_set_latch(&c,0xfedcba98,position);
    assert(c.latch_target==0xfedcba98 && c.latched==1);
    assert(!memcmp(c.latch_position,position,sizeof position));
    bones_clear_latch(&c);
    assert(c.latched==0 && c.latch_target==0xfedcba98);
    assert(!memcmp(c.latch_position,position,sizeof position));
    for (unsigned i=0;i<3;i++) { c.velocity[i]=i+1;c.acceleration[i]=i+4; }
    c.position_x=123;c.position_y=456;
    bones_kill_velocity_acceleration(&c);
    for (unsigned i=0;i<3;i++) assert(c.velocity[i]==0 && c.acceleration[i]==0);
    assert(c.position_x==123 && c.position_y==456 && c.latch_target==0xfedcba98);
    return 0;
}

