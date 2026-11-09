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

#endif
