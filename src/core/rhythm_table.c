#include "rhythm_table.h"
#include <string.h>

/* Median positive delta in DrumSequence ≈ 7; host DRUM period 12. */
#define RHYTHM_DRUM_TIME_STEP 7u

/*
 * First 48 notes of _DrumSequence @0x0026b58c (DRUM 03CODE base 0x232000,
 * file off 0x3958C). Extracted 2026-11-07 from /14DRUM/03CODE.
 */
static const rhythm_note k_drum_notes[] = {
    {0u, 0u, 0u},   {0u, 3u, 0u},   {22u, 2u, 0u},  {44u, 1u, 0u},
    {66u, 3u, 0u},  {88u, 3u, 0u},  {102u, 2u, 0u}, {110u, 2u, 0u},
    {132u, 0u, 0u}, {154u, 0u, 0u}, {176u, 0u, 0u}, {176u, 3u, 0u},
    {191u, 0u, 0u}, {198u, 2u, 0u}, {205u, 1u, 0u}, {212u, 3u, 0u},
    {219u, 2u, 0u}, {240u, 1u, 0u}, {255u, 3u, 0u}, {262u, 2u, 0u},
    {284u, 2u, 0u}, {299u, 3u, 0u}, {306u, 1u, 0u}, {327u, 0u, 0u},
    {327u, 1u, 0u}, {327u, 3u, 0u}, {349u, 0u, 2u}, {349u, 3u, 2u},
    {371u, 2u, 2u}, {393u, 1u, 2u}, {415u, 3u, 2u}, {437u, 3u, 2u},
    {450u, 2u, 2u}, {456u, 2u, 2u}, {481u, 0u, 2u}, {503u, 0u, 2u},
    {525u, 0u, 2u}, {525u, 3u, 2u}, {540u, 0u, 2u}, {547u, 2u, 2u},
    {554u, 1u, 2u}, {561u, 3u, 2u}, {568u, 2u, 2u}, {589u, 1u, 2u},
    {604u, 3u, 2u}, {611u, 2u, 2u}, {633u, 2u, 2u}, {655u, 0u, 2u},
};

/*
 * GUITAR host table: DrumSequence kinds remapped onto period-16 beat grid.
 * Phrase lengths 5/6/10/12 from SequenceTable@0x0028c3ac header words.
 * Times = beat_index * 16 (host frames).
 */
static const rhythm_note k_guitar_notes[] = {
    {0u, 0u, 0u},   {16u, 3u, 0u},  {32u, 2u, 0u},  {48u, 1u, 0u},
    {64u, 3u, 0u},  {80u, 3u, 0u},  {96u, 2u, 0u},  {112u, 2u, 0u},
    {128u, 0u, 0u}, {144u, 0u, 0u}, {160u, 0u, 0u}, {160u, 3u, 0u},
    {176u, 0u, 0u}, {192u, 2u, 0u}, {208u, 1u, 0u}, {224u, 3u, 0u},
    {240u, 2u, 0u}, {256u, 1u, 0u}, {272u, 3u, 0u}, {288u, 2u, 0u},
    {304u, 2u, 0u}, {320u, 3u, 0u}, {336u, 1u, 0u}, {352u, 0u, 0u},
    {352u, 1u, 0u}, {352u, 3u, 0u}, {368u, 0u, 0u}, {384u, 3u, 0u},
    {400u, 2u, 0u}, {416u, 1u, 0u}, {432u, 3u, 0u}, {448u, 3u, 0u},
};

static const rhythm_table k_drum = {
    k_drum_notes,
    (unsigned)(sizeof k_drum_notes / sizeof k_drum_notes[0]),
    12u,
    6u,
    8u,
    "DRUM",
    "DrumSequence@0026b58c drumtab.o",
};

static const rhythm_table k_guitar = {
    k_guitar_notes,
    (unsigned)(sizeof k_guitar_notes / sizeof k_guitar_notes[0]),
    16u,
    6u,
    8u,
    "GUITAR",
    "SequenceTable@0028c3ac + DrumSequence kinds; period 16",
};

const rhythm_table *rhythm_table_for_stage(const char *stage_name) {
    if (!stage_name) return 0;
    if (!strcmp(stage_name, "DRUM")) return &k_drum;
    if (!strcmp(stage_name, "GUITAR")) return &k_guitar;
    return 0;
}

uint16_t rhythm_kind_button_mask(unsigned kind) {
    switch (kind & 3u) {
    case RHYTHM_KIND_A: return 0x0400u;
    case RHYTHM_KIND_B: return 0x0100u;
    case RHYTHM_KIND_C: return 0x0200u;
    default: return 0x8000u; /* RIGHT */
    }
}

unsigned rhythm_note_host_frame(const rhythm_table *t, unsigned note_index) {
    unsigned time, period;
    if (!t || !t->notes || note_index >= t->count) return 0;
    time = t->notes[note_index].time;
    period = t->period ? t->period : 12u;
    if (period == 12u) {
        /* DrumSequence time → host frames (step ≈ 7 → period 12). */
        return (time * period) / (RHYTHM_DRUM_TIME_STEP ? RHYTHM_DRUM_TIME_STEP : 7u);
    }
    /* GUITAR table already stores host-frame times. */
    return time;
}

int rhythm_buttons_match(unsigned kind, uint16_t buttons) {
    uint16_t need = rhythm_kind_button_mask(kind);
    /* Exact pad, or RIGHT strum as universal (--check / held-RIGHT path). */
    if (buttons & need) return 1;
    if (buttons & 0x8000u) return 1;
    return 0;
}
