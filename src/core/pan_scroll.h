#ifndef MB_PAN_SCROLL_H
#define MB_PAN_SCROLL_H
#include "pan_collision_map.h"
#include "pan_frame.h"
#include <stddef.h>
#include <stdint.h>

/*
 * Host stand-in for CPanScroll_257320 + PlayPanAnim_257968 over a 15PAN stream.
 * Saturn updates scroll shorts from world/camera; pandcomp records are chained
 * delta frames (BE32-sized). Host maps camera/bones X into a frame index and
 * decodes forward (or restarts) to keep planes + collision current (§22 live).
 */

typedef struct {
    const uint8_t *blob;
    size_t blob_size;
    unsigned count;          /* pandcomp records */
    unsigned index;          /* current decoded frame */
    uint8_t *planes;         /* PAN_BUFFER_SIZE work (owned) */
    uint8_t *bg_rgba;        /* 320×224 RGBA plane0 (owned, filled by apply) */
    uint8_t palette4[1024];  /* copy of bank-3 RGBA (256×4) */
    pan_collision_map collision;  /* §22: last decoded pandcomp collision */
    pan_collision_map collision0; /* record-0 snapshot (init / soft fallback) */
    int scroll_x;            /* CPanScroll short stand-in */
    int scroll_y;
} pan_scroll;

int pan_scroll_init(pan_scroll *ps, const uint8_t *blob, size_t blob_size,
                    const uint8_t *palette_bank4 /* 1024 bytes RGBA */);
void pan_scroll_free(pan_scroll *ps);

/* Map world/camera pixel X into [0, count-1] across start_px..finish_px. */
unsigned pan_scroll_index_for_x(const pan_scroll *ps, unsigned pixel_x,
                                unsigned start_px, unsigned finish_px);

/* Decode to target index; updates planes, bg_rgba, collision. */
int pan_scroll_seek(pan_scroll *ps, unsigned target);

/* CPanScroll-style: scroll_x = pixel_x + bias; seek from camera. */
int pan_scroll_update(pan_scroll *ps, unsigned camera_px, unsigned bones_px,
                      unsigned start_px, unsigned finish_px);

#endif
