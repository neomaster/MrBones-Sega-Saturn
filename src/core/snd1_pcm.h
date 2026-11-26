#ifndef MB_SND1_PCM_H
#define MB_SND1_PCM_H
#include "snd1.h"
#include <stddef.h>
#include <stdint.h>

/* Experimental: span N begins with a big-endian uint16 offset table into the
 * same span; entries increase until an out-of-range or non-increasing value.
 * Sample bytes are treated as signed 8-bit PCM (Saturn-like). Not SCSP. */

#define SND1_PCM_MAX_ENTRIES 256

typedef struct {
    uint16_t offsets[SND1_PCM_MAX_ENTRIES];
    unsigned count;
    const uint8_t *base;
    size_t size;
} snd1_pcm_table;

int snd1_pcm_open(snd1_pcm_table *table, const snd1_span *span);
/* Fills pointer/length for sample index; 0 ok, -1 bad index. */
int snd1_pcm_sample(const snd1_pcm_table *table, unsigned index,
                    const int8_t **pcm, size_t *length);

/*
 * Voice layer records (SA correction): a span of `rec_size`-byte records whose bytes
 * +2..5 hold the big-endian sound-RAM start address (SA) of the sample. 05SND1 is
 * loaded at sound RAM SND1_VOICE_SA_BASE, so the sample begins at
 * SA - SND1_VOICE_SA_BASE in the blob. The table stops at the first record whose SA
 * falls outside the blob. A sample runs to the next higher start in the table, or to
 * the end of the blob.
 */
#define SND1_VOICE_SA_BASE 0xB000u

typedef struct {
    uint32_t start[SND1_PCM_MAX_ENTRIES];
    unsigned count;
    const uint8_t *blob;
    size_t blob_size;
} snd1_voice_table;

int snd1_voice_open(snd1_voice_table *table, const snd1_span *layers, size_t rec_size,
                    const uint8_t *blob, size_t blob_size);
int snd1_voice_sample(const snd1_voice_table *table, unsigned index,
                      const int8_t **pcm, size_t *length);

/*
 * Decode a note from a Saturn SCSP Tone Bank (05SND1 span 4).
 * Table starts with ptr[0..3] (Mixer/VL/PEG/PLFO, where ptr[0]/2 is total
 * entry count) followed by voice headers at ptr[4 + voice]. Each voice has a
 * 4-byte header (byte +2 = num_layers - 1) and 32-byte SCSP layer records:
 *   +0 lo_note, +1 hi_note, +2..5 BE32 flags|SA20 (SA20 = offset in ton_span;
 *   byte +3 bit 4 = PCM8B), +6..7 LSA, +8..9 LEA (samples = LEA + 1).
 * When prefer_pcm8 is non-zero, selects an 8-bit layer over a 16-bit layer if
 * both cover `note`. Outputs raw slice in ton_span, sample_count, and is_pcm8b.
 */
int snd1_ton_note_sample(const snd1_span *ton_span, unsigned voice,
                         uint8_t note, int prefer_pcm8,
                         const uint8_t **raw, size_t *raw_bytes,
                         size_t *sample_count, int *is_pcm8b);

typedef struct {
    const uint8_t *data;
    size_t samples;
    unsigned loop_start;
    unsigned loop_mode;
    unsigned pcm8;
    unsigned root_note;
    int fine_tune;
    int octave;
    unsigned fine_step;
    uint8_t ar, d1r, d2r, dl, rr, krs, eghold, tl;
} snd1_tone;

int snd1_ton_layer(const snd1_span *span, unsigned voice, uint8_t note,
                   unsigned *next_layer, snd1_tone *tone);

/*
 * Return non-zero if `span` begins with a valid SCSP Tone Bank (.ton) pointer
 * table (big-endian word pointers to voice/layer descriptors inside the span).
 */
int snd1_ton_is_bank(const snd1_span *span);

/*
 * Pick a clean, decoded waveform slice from an SCSP Tone Bank (.ton) span for
 * general host SFX playback, filtering out descriptor headers, silent slices,
 * and raw white-noise percussive layers. Selects the `slot`-th clean unique
 * sample (wrapping around if `slot` >= count). Returns 0 on success.
 */
int snd1_ton_pick_sfx(const snd1_span *ton_span, unsigned slot,
                      size_t min_samples, size_t max_samples,
                      const uint8_t **raw, size_t *raw_bytes,
                      size_t *sample_count, int *is_pcm8b);

#endif
