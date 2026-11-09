#include "pan_frame.h"
#include "pan_collision.h"
#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}

int pan_frame_decode(const uint8_t *frame, size_t size, uint8_t *pixels,
                     size_t pixels_size, unsigned plane, uint8_t *collision,
                     size_t collision_capacity, size_t *collision_size,
                     size_t *consumed) {
    static const int16_t row2[8] = {320,320,-320,-320,1,-1,1,-1};
    static const int16_t step[8] = {647,633,-633,-647,-2238,2238,2242,-2242};
    static const int16_t dx[8] = {-1,1,-1,1,320,-320,-320,320};
    uint8_t *work = NULL, *map = NULL;
    size_t at, map_size, tile = 0, frame_size;
    int result = -1;
    if (!frame || !pixels || !collision || !collision_size || !consumed ||
        size < 6 || pixels_size < PAN_BUFFER_SIZE || plane >= 4 || !collision_capacity)
        return -1;
    frame_size = be32(frame);
    if (frame_size < 6 || frame_size > size) return -1;
    work = malloc(PAN_BUFFER_SIZE);
    map = malloc(collision_capacity);
    if (!work || !map || pan_collision_decode(frame, frame_size, map,
                                              collision_capacity, &at, &map_size))
        goto done;
    memcpy(work,pixels,PAN_BUFFER_SIZE);
    while (tile < 40u*28u) {
        if (at >= frame_size) goto done;
        unsigned tag=frame[at++], op=tag>>4, sub=tag&15u;
        size_t base=(size_t)plane*PAN_PLANE_SIZE+(tile/40u)*8u*PAN_WIDTH+(tile%40u)*8u;
        if (op == 15) { memset(work+(size_t)plane*PAN_PLANE_SIZE,0,PAN_PLANE_SIZE); continue; }
        if (op >= 11) { size_t skip=sub+1u;if (skip>1120u-tile) goto done;tile+=skip;continue; }
        if (op == 0) {
            for (size_t y=0;y<8;y++) memset(work+base+y*PAN_WIDTH,0,8);
        } else if (op == 2) {
            if (frame_size-at<64) goto done;
            for (size_t y=0;y<8;y++){memcpy(work+base+y*PAN_WIDTH,frame+at,8);at+=8;}
        } else if (op == 1) {
            if (at>=frame_size) goto done;
            uint8_t common=frame[at++];
            for (size_t y=0;y<8;y++) {
                if (at>=frame_size) goto done;
                unsigned mask=frame[at++];
                for (size_t x=0;x<8;x++) {
                    if (!(mask & (1u<<x)) && at>=frame_size) goto done;
                    work[base+y*PAN_WIDTH+x]=(mask & (1u<<x)) ? common : frame[at++];
                }
            }
        } else {
            if (frame_size-at<2) goto done;
            int32_t a=(int32_t)((sub<<16)|((unsigned)frame[at]<<8)|frame[at+1]);at+=2;
            int32_t b=a+row2[op-3];
            size_t w=base+8,z=w+PAN_WIDTH;
            for (size_t y=0;y<4;y++) {
                for (size_t x=0;x<8;x++) {
                    if (a<0 || b<0 || (size_t)a>=PAN_BUFFER_SIZE || (size_t)b>=PAN_BUFFER_SIZE) goto done;
                    uint8_t va=work[a], vb=work[b];work[--w]=va;work[--z]=vb;
                    if (x!=7){a+=dx[op-3];b+=dx[op-3];}
                }
                a+=step[op-3];b+=step[op-3];w+=648;z+=648;
            }
        }
        tile++;
    }
    memcpy(pixels,work,PAN_BUFFER_SIZE);
    memcpy(collision,map,map_size);
    *collision_size=map_size;
    /* Prefer declared BE32 size so chained 15PAN records align; fall back to at. */
    *consumed = (frame_size >= at) ? (size_t)frame_size : at;
    result=0;
done:
    free(map);free(work);return result;
}

uint32_t pan_frame_byte_size(const uint8_t *frame, size_t size) {
    uint32_t sz;
    if (!frame || size < 4) return 0;
    sz = be32(frame);
    if (sz < 6 || sz > size) return 0;
    return sz;
}

int pan_stream_count(const uint8_t *blob, size_t size, unsigned *out_count) {
    size_t off = 0;
    unsigned n = 0;
    if (!blob || !out_count) return -1;
    while (off + 4 <= size) {
        uint32_t sz = pan_frame_byte_size(blob + off, size - off);
        if (!sz) break;
        off += sz;
        n++;
        if (n > 100000u) return -1;
    }
    *out_count = n;
    return n > 0 ? 0 : -1;
}

int pan_stream_record(const uint8_t *blob, size_t size, unsigned index,
                      size_t *out_off, uint32_t *out_sz) {
    size_t off = 0;
    unsigned i;
    if (!blob || !out_off || !out_sz) return -1;
    for (i = 0; i <= index; i++) {
        uint32_t sz;
        if (off + 4 > size) return -1;
        sz = pan_frame_byte_size(blob + off, size - off);
        if (!sz) return -1;
        if (i == index) {
            *out_off = off;
            *out_sz = sz;
            return 0;
        }
        off += sz;
    }
    return -1;
}
