#include "snd1_pcm.h"

int snd1_pcm_open(snd1_pcm_table *table, const snd1_span *span) {
    if (!table || !span || !span->data || span->size < 4) return -1;
    table->base = span->data;
    table->size = span->size;
    table->count = 0;
    for (unsigned i = 0; i < SND1_PCM_MAX_ENTRIES; i++) {
        if ((size_t)i * 2 + 1 >= span->size) break;
        uint16_t off = (uint16_t)((span->data[i * 2] << 8) | span->data[i * 2 + 1]);
        if (off >= span->size) break;
        if (table->count && off < table->offsets[table->count - 1]) break;
        if (table->count && off == table->offsets[table->count - 1]) break;
        table->offsets[table->count++] = off;
    }
    return table->count >= 2 ? 0 : -1;
}

int snd1_pcm_sample(const snd1_pcm_table *table, unsigned index,
                    const int8_t **pcm, size_t *length) {
    if (!table || !pcm || !length || index + 1 >= table->count) return -1;
    uint16_t a = table->offsets[index];
    uint16_t b = table->offsets[index + 1];
    if (b <= a || b > table->size) return -1;
    *pcm = (const int8_t *)(table->base + a);
    *length = (size_t)(b - a);
    return 0;
}
