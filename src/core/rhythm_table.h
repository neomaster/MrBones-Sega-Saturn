#ifndef MB_RHYTHM_TABLE_H
#define MB_RHYTHM_TABLE_H
#include <stdint.h>

/*
 * §24 rhythm note tables extracted from stage 03CODE.
 * DRUM:  _DrumSequence @0x0026b58c in drumtab.o (.data 0x4ae0) — 16-byte BE
 *        records {time, kind 0..3, flags, meta}. Kinds map to N/S/E/W pads
 *        (SouthDrum/NorthCable/EastCable/WestCable).
 * GUITAR: period 16 from host §20; phrase lengths from SequenceTable@28c3ac
 *        header {5,6,0xA,0xC}; note kinds mirrored from DrumSequence scaled
 *        to guitar period (guitar.o lick path StartLick/StopLick on CGBones).
 * Hit window: SPECIAL_BEAT_WINDOW; Saturn buttons A/B/C/RIGHT ↔ kind 0..3.
 */

#define RHYTHM_KIND_A 0u     /* → MB_PAD_A / held 0x0400 */
#define RHYTHM_KIND_B 1u     /* → MB_PAD_B / held 0x0100 */
#define RHYTHM_KIND_C 2u     /* → MB_PAD_C / held 0x0200 */
#define RHYTHM_KIND_RIGHT 3u /* → MB_PAD_RIGHT / held 0x8000 */

typedef struct {
    uint16_t time;  /* absolute time in DrumSequence units */
    uint8_t kind;   /* 0..3 */
    uint8_t flags;  /* DrumSequence word2 */
} rhythm_note;

typedef struct {
    const rhythm_note *notes;
    unsigned count;
    unsigned period;      /* GUITAR=16 / DRUM=12 */
    unsigned window;      /* frames at beat start accepting a hit */
    unsigned hits_to_win;
    const char *name;
    const char *source;   /* VA / obj provenance */
} rhythm_table;

/* Returns static table for "GUITAR" / "DRUM", or NULL. */
const rhythm_table *rhythm_table_for_stage(const char *stage_name);

/* Saturn held/pressed bit for kind 0..3. */
uint16_t rhythm_kind_button_mask(unsigned kind);

/*
 * Map absolute DrumSequence time → host frame index via period scaling.
 * host_frame ≈ time * period / RHYTHM_DRUM_TIME_STEP (median delta ≈ 7).
 */
unsigned rhythm_note_host_frame(const rhythm_table *t, unsigned note_index);

/* 1 if `buttons` (saturn held|pressed) matches expected kind (or RIGHT strum). */
int rhythm_buttons_match(unsigned kind, uint16_t buttons);

#endif
