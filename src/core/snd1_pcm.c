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

static uint32_t voice_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

int snd1_voice_open(snd1_voice_table *table, const snd1_span *layers, size_t rec_size,
                    const uint8_t *blob, size_t blob_size) {
    if (!table || !layers || !layers->data || rec_size < 6 || !blob || !blob_size)
        return -1;
    table->count = 0;
    table->blob = blob;
    table->blob_size = blob_size;
    for (unsigned i = 0; i < SND1_PCM_MAX_ENTRIES; i++) {
        size_t off = (size_t)i * rec_size;
        uint32_t sa;
        if (off + rec_size > layers->size) break;
        sa = voice_be32(layers->data + off + 2);
        if (sa < SND1_VOICE_SA_BASE || (size_t)(sa - SND1_VOICE_SA_BASE) >= blob_size)
            break;
        table->start[table->count++] = sa - SND1_VOICE_SA_BASE;
    }
    return table->count >= 1 ? 0 : -1;
}

int snd1_voice_sample(const snd1_voice_table *table, unsigned index,
                      const int8_t **pcm, size_t *length) {
    uint32_t a;
    size_t end;
    if (!table || !pcm || !length || index >= table->count) return -1;
    a = table->start[index];
    end = table->blob_size;
    for (unsigned i = 0; i < table->count; i++)
        if (table->start[i] > a && (size_t)table->start[i] < end)
            end = table->start[i];
    if ((size_t)a >= end) return -1;
    *pcm = (const int8_t *)(table->blob + a);
    *length = end - a;
    return 0;
}

static uint16_t voice_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

int snd1_ton_note_sample(const snd1_span *ton_span, unsigned voice,
                         uint8_t note, int prefer_pcm8,
                         const uint8_t **raw, size_t *raw_bytes,
                         size_t *sample_count, int *is_pcm8b) {
    uint16_t p0, voff;
    unsigned ptr_count, num_voices, num_layers, li;
    int found = 0;
    const uint8_t *best_raw = NULL;
    size_t best_bytes = 0, best_samples = 0;
    int best_pcm8 = 0;

    if (!ton_span || !ton_span->data || ton_span->size < 12u ||
        !raw || !raw_bytes || !sample_count || !is_pcm8b)
        return -1;
    p0 = voice_be16(ton_span->data);
    if (p0 < 10u || (p0 & 1u) || p0 > ton_span->size)
        return -1;
    ptr_count = p0 / 2u;
    if (ptr_count <= 4u)
        return -1;
    num_voices = ptr_count - 4u;
    if (voice >= num_voices)
        return -1;
    voff = voice_be16(ton_span->data + (4u + voice) * 2u);
    if ((size_t)voff + 4u > ton_span->size)
        return -1;
    num_layers = (unsigned)ton_span->data[voff + 2u] + 1u;
    if ((size_t)voff + 4u + (size_t)num_layers * 32u > ton_span->size)
        return -1;

    for (li = 0; li < num_layers; li++) {
        const uint8_t *rec = ton_span->data + voff + 4u + (size_t)li * 32u;
        uint32_t sa;
        uint16_t lea;
        int pcm8;
        size_t n_samples, n_bytes;
        if (note < rec[0] || note > rec[1])
            continue;
        sa = voice_be32(rec + 2) & 0xFFFFFu;
        pcm8 = (rec[3] & 0x10u) != 0;
        lea = voice_be16(rec + 8);
        n_samples = (size_t)lea + 1u;
        n_bytes = n_samples * (pcm8 ? 1u : 2u);
        if ((size_t)sa >= ton_span->size || n_bytes > ton_span->size - sa)
            continue;
        if (!found || (prefer_pcm8 && !best_pcm8 && pcm8)) {
            found = 1;
            best_raw = ton_span->data + sa;
            best_bytes = n_bytes;
            best_samples = n_samples;
            best_pcm8 = pcm8;
            if (!prefer_pcm8 || best_pcm8)
                break;
        }
    }
    if (!found)
        return -1;
    *raw = best_raw;
    *raw_bytes = best_bytes;
    *sample_count = best_samples;
    *is_pcm8b = best_pcm8;
    return 0;
}

