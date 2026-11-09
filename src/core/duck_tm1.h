#ifndef MB_DUCK_TM1_H
#define MB_DUCK_TM1_H
#include <stddef.h>
#include <stdint.h>

/*
 * §22 Duck TrueMotion 1 decoder using real 19DUCK delta + vector tables.
 * Algorithm follows public TM1 structure (MultimediaWiki / interoperable tables).
 */

typedef struct {
    int16_t ydt[8], cdt[8], fat_ydt[8], fat_cdt[8];
    uint32_t y_pred[1024], c_pred[1024];
    uint32_t fat_y_pred[1024], fat_c_pred[1024];
    const uint8_t *vec_tbl[4];
    size_t vec_tbl_size[4];
    int tables_from_duck; /* 1 if 19DUCK supplied deltas/vectors */
    int last_deltaset, last_vectable;
    uint32_t *vert_pred; /* width/2 entries */
    unsigned vert_pred_w;
    /* Output scratch RGB565 or RGB24 → converted by caller */
    uint16_t *rgb565;
    uint8_t *rgb24;
    unsigned width, height;
} duck_tm1;

/* Parse 19DUCK blob for BE ydt/cdt + vector tables. Falls back to built-in
 * deltas if signature missing. Returns 0 always (soft). */
int duck_tm1_init(duck_tm1 *tm, const void *duck19, size_t duck_size);
void duck_tm1_free(duck_tm1 *tm);

/* Decode one TM1 frame payload (after 00dc size) into tm->rgb565 or rgb24.
 * out_rgba optional 320×224 destination (scaled/centered). */
int duck_tm1_decode_frame(duck_tm1 *tm, const uint8_t *frame, unsigned frame_size,
                          uint8_t *out_rgba_320x224);

#endif
