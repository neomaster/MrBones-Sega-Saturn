#include "sound_event.h"
#include <string.h>
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
int mb_sound_event_open(mb_sound_event *e, const void *data, size_t size, uint32_t index) {
    if (!e) return -1;
    memset(e,0,sizeof *e);
    if (!data || size<4) return -1;
    const uint8_t *p=data;
    uint32_t count=be32(p);
    if (count>(size-4)/4 || index>=count) return -1;
    uint32_t offset=be32(p+4+(size_t)index*4);
    if (!offset) { e->done=1; return 0; }
    if (offset < 4+(size_t)count*4 || offset>size || size-offset<32 || (offset&3)) return -1;
    e->data=p;e->size=size;e->offset=offset;return 1;
}
int mb_sound_event_next(mb_sound_event *e, mb_sound_event_record *r) {
    if (!e || !r) return -1;
    if (e->done) return 0;
    if (!e->data || e->offset>e->size || e->size-e->offset<32) return -1;
    for (size_t i=0;i<8;i++) r->words[i]=be32(e->data+e->offset+i*4);
    e->offset+=32;
    if (r->words[0]&0x80000000u) e->done=1;
    return 1;
}
