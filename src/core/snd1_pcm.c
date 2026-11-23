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

int snd1_ton_is_bank(const snd1_span *span) {
    uint16_t p0;
    unsigned ptr_count, num_voices, i;
    if (!span || !span->data || span->size < 12u) return 0;
    p0 = voice_be16(span->data);
    if (p0 < 10u || (p0 & 1u) || (size_t)p0 >= span->size) return 0;
    ptr_count = p0 / 2u;
    if (ptr_count <= 4u) return 0;
    num_voices = ptr_count - 4u;
    if (num_voices > 64u) return 0;
    for (i = 1u; i < 4u; i++) {
        uint16_t off = voice_be16(span->data + (size_t)i * 2u);
        if (off != 0u && (off < p0 || (size_t)off >= span->size)) return 0;
    }
    for (i = 4u; i < ptr_count; i++) {
        uint16_t off = voice_be16(span->data + (size_t)i * 2u);
        unsigned num_layers;
        if (off < p0 || (size_t)off + 4u > span->size) return 0;
        num_layers = (unsigned)span->data[off + 2u] + 1u;
        if ((size_t)off + 4u + (size_t)num_layers * 32u > span->size) return 0;
    }
    return 1;
}

int snd1_ton_layer(const snd1_span *span, unsigned voice, uint8_t note,
                   unsigned *next_layer, snd1_tone *tone) {
    if (!next_layer || !tone || !snd1_ton_is_bank(span) ||
        voice >= (unsigned)voice_be16(span->data) / 2u - 4u) return -1;
    const uint8_t *header = span->data + voice_be16(span->data + (4u + voice) * 2u);
    unsigned layers = (unsigned)header[2] + 1u;
    for (unsigned i = *next_layer; i < layers; ++i) {
        const uint8_t *layer = header + 4u + i * 32u;
        if (note < layer[0] || note > layer[1]) continue;
        size_t start = voice_be32(layer + 2u) & 0xfffffu;
        unsigned pcm8 = (layer[3] >> 4u) & 1u;
        size_t count = (size_t)voice_be16(layer + 8u) + 1u;
        size_t bytes = count * (pcm8 ? 1u : 2u);
        /* Samples must not point back into their own voice descriptor. */
        if (start < (size_t)(header - span->data) + 4u + layers * 32u ||
            start >= span->size || bytes > span->size - start)
            continue;
        unsigned pitch = voice_be16(layer + 0x12u);
        int octave = (int)((pitch >> 11u) & 15u);
        *tone = (snd1_tone){span->data + start, count, voice_be16(layer + 6u),
                            (layer[3] >> 5u) & 3u, pcm8,
                            layer[0x19] & 127u, (int8_t)layer[0x1a],
                            octave < 8 ? octave : octave - 16, pitch & 1023u};
        *next_layer = i + 1u;
        return 0;
    }
    return -1;
}

static int ton_slice_is_clean(const uint8_t *raw, size_t n_samples, int pcm8) {
    size_t probe = n_samples < 320u ? n_samples : 320u;
    int prev = 0;
    unsigned peak = 0;
    uint64_t diff_sum = 0;
    if (!raw || probe < 16u) return 0;
    for (size_t i = 0; i < probe; i++) {
        int s8;
        if (pcm8) {
            s8 = (int)(int8_t)raw[i];
        } else {
            int16_t s16 = (int16_t)(((uint16_t)raw[i * 2u] << 8) | raw[i * 2u + 1u]);
            s8 = (int)s16 >> 8;
        }
        unsigned mag = (unsigned)(s8 < 0 ? -s8 : s8);
        if (mag > peak) peak = mag;
        if (i > 0u) {
            int d = s8 - prev;
            diff_sum += (unsigned)(d < 0 ? -d : d);
        }
        prev = s8;
    }
    if (peak < 10u) return 0;
    /* Filter out pure white-noise hiss / non-PCM descriptors (avg step > 24). */
    if (diff_sum / (probe - 1u) > 24u) return 0;
    return 1;
}

typedef struct {
    uint32_t sa;
    const uint8_t *raw;
    size_t bytes;
    size_t samples;
    int pcm8;
} ton_pick_entry;

int snd1_ton_pick_sfx(const snd1_span *ton_span, unsigned slot,
                      size_t min_samples, size_t max_samples,
                      const uint8_t **raw, size_t *raw_bytes,
                      size_t *sample_count, int *is_pcm8b) {
    uint16_t p0;
    unsigned num_voices, vi;
    ton_pick_entry picks[64];
    unsigned count = 0;
    int pass;

    if (!snd1_ton_is_bank(ton_span) || !raw || !raw_bytes || !sample_count || !is_pcm8b)
        return -1;
    p0 = voice_be16(ton_span->data);
    num_voices = p0 / 2u - 4u;

    /* Pass 0: bounded duration [min_samples..max_samples]; Pass 1: relax upper bound. */
    for (pass = 0; pass < 2 && count == 0; pass++) {
        size_t lo = min_samples > 32u ? min_samples : 32u;
        size_t hi = (pass == 0 && max_samples >= lo) ? max_samples : 48000u;
        for (vi = 0; vi < num_voices && count < 64u; vi++) {
            uint16_t voff = voice_be16(ton_span->data + (4u + vi) * 2u);
            unsigned num_layers = (unsigned)ton_span->data[voff + 2u] + 1u;
            for (unsigned li = 0; li < num_layers && count < 64u; li++) {
                const uint8_t *rec = ton_span->data + voff + 4u + (size_t)li * 32u;
                uint32_t sa = voice_be32(rec + 2) & 0xFFFFFu;
                int pcm8 = (rec[3] & 0x10u) != 0;
                uint16_t lea = voice_be16(rec + 8);
                size_t n_samples = (size_t)lea + 1u;
                size_t n_bytes = n_samples * (pcm8 ? 1u : 2u);
                int dup = 0;
                if (n_samples < lo || n_samples > hi) continue;
                if ((size_t)sa >= ton_span->size || n_bytes > ton_span->size - sa) continue;
                for (unsigned k = 0; k < count; k++) {
                    if (picks[k].sa == sa) { dup = 1; break; }
                }
                if (dup) continue;
                if (!ton_slice_is_clean(ton_span->data + sa, n_samples, pcm8)) continue;
                picks[count].sa = sa;
                picks[count].raw = ton_span->data + sa;
                picks[count].bytes = n_bytes;
                picks[count].samples = n_samples;
                picks[count].pcm8 = pcm8;
                count++;
            }
        }
    }
    if (!count) return -1;
    {
        unsigned pick = slot % count;
        *raw = picks[pick].raw;
        *raw_bytes = picks[pick].bytes;
        *sample_count = picks[pick].samples;
        *is_pcm8b = picks[pick].pcm8;
    }
    return 0;
}

