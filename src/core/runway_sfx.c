#include "runway_sfx.h"
#include "resource.h"
#include "snd1.h"
#include "snd1_pcm.h"
#include "sound_event.h"
#include "sound_sequence.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Host sample map — span/index chosen from decoded PCM tables (§12n probe). */
static const struct {
    unsigned span;
    unsigned index;
} k_map[RUNWAY_SFX_COUNT] = {
    {4u, 5u},  /* WALK  — span4 s05 3748 B (prior one-shot) */
    {4u, 7u},  /* TALL  — span4 s07 3748 B alternate rattle */
    {4u, 8u},  /* C     — span4 s08 68 B short */
    {5u, 6u},  /* JUMP  — span5 s06 612 B */
    {4u, 4u},  /* LAND  — span4 s04 36 B tick */
};

static unsigned read_be16(const uint8_t *p) {
    return ((unsigned)p[0] << 8) | p[1];
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void set_midi_channel(runway_sfx *sfx, const uint32_t *word) {
    unsigned block = word[2] & 7u;
    unsigned ch = word[5] & 15u;
    if (word[4] == 4u) sfx->channel_programs[block][ch] = (uint8_t)(word[6] & 127u);
    if (word[4] == 3u && word[6] == 32u)
        sfx->channel_banks[block][ch] = (uint8_t)(word[7] & 15u);
}

int runway_sfx_load_dir(runway_sfx *sfx, cdimage *cd, const char *iso_dir) {
    char path[96];
    snd1_span spans[SND1_SPAN_COUNT];
    snd1_pcm_table tables[SND1_SPAN_COUNT];
    unsigned i;
    if (!sfx || !cd || !iso_dir) return -1;
    memset(sfx, 0, sizeof *sfx);
    snprintf(path, sizeof path, "%s/05SND1", iso_dir);
    if (resource_load(cd, path, &sfx->blob, &sfx->blob_size))
        return -1;
    if (snd1_parse(sfx->blob, sfx->blob_size, spans)) {
        runway_sfx_free(sfx);
        return -1;
    }
    snprintf(path, sizeof path, "%s/17SND2", iso_dir);
    if (resource_load(cd, path, &sfx->snd2_blob, &sfx->snd2_size) == 0 &&
        sfx->snd2_size >= 0x800u) {
        snprintf(path, sizeof path, "%s/18SND3", iso_dir);
        if (resource_load(cd, path, &sfx->snd3_blob, &sfx->snd3_size) == 0 &&
            sfx->snd3_size >= 0x800u) {
            const uint8_t *s2 = (const uint8_t *)sfx->snd2_blob;
            const uint8_t *s3 = (const uint8_t *)sfx->snd3_blob;
            for (unsigned j = 0; j < 256u; ++j) {
                size_t o2 = read_be32(s2 + j * 8u), l2 = read_be32(s2 + j * 8u + 4u);
                size_t o3 = read_be32(s3 + j * 8u), l3 = read_be32(s3 + j * 8u + 4u);
                if (!o2 || !l2 || !o3 || !l3 ||
                    o2 > sfx->snd2_size || l2 > sfx->snd2_size - o2 ||
                    o3 > sfx->snd3_size || l3 > sfx->snd3_size - o3)
                    break;
                sfx->laff_joke_count = j + 1u;
            }
        }
    }
    if (!sfx->laff_joke_count) {
        free(sfx->snd2_blob);
        sfx->snd2_blob = NULL;
        sfx->snd2_size = 0;
        free(sfx->snd3_blob);
        sfx->snd3_blob = NULL;
        sfx->snd3_size = 0;
    }
    memset(tables, 0, sizeof tables);
    for (i = 0; i < SND1_SPAN_COUNT; i++)
        (void)snd1_pcm_open(&tables[i], &spans[i]);

    for (i = 0; i < RUNWAY_SFX_COUNT; i++) {
        runway_sfx_sample *sample = &sfx->samples[i];
        (void)snd1_pcm_sample(&tables[k_map[i].span], k_map[i].index,
                             &sample->pcm, &sample->length);
    }

    mb_sound_event startup;
    mb_sound_event_record record;
    if (mb_sound_event_open(&startup, spans[0].data, spans[0].size, 0u) == 1)
        while (mb_sound_event_next(&startup, &record) == 1)
            if (record.words[1] == 1u) set_midi_channel(sfx, record.words);

    /* Decode genuine SCSP .TON waveforms for live playback (never header metadata). */
    for (i = 0; i < RUNWAY_SFX_COUNT; i++) {
        size_t min_s = (i == RUNWAY_SFX_WALK || i == RUNWAY_SFX_LAND) ? 180u : 400u;
        size_t max_s = (i == RUNWAY_SFX_WALK || i == RUNWAY_SFX_LAND) ? 4000u : 12000u;
        const uint8_t *raw = NULL;
        size_t raw_bytes = 0, sample_count = 0;
        int is_pcm8b = 0;
        int picked = -1;
        if (snd1_ton_is_bank(&spans[4]))
            picked = snd1_ton_pick_sfx(&spans[4], i, min_s, max_s,
                                       &raw, &raw_bytes, &sample_count, &is_pcm8b);
        if (picked != 0 && snd1_ton_is_bank(&spans[5]))
            picked = snd1_ton_pick_sfx(&spans[5], i, min_s, max_s,
                                       &raw, &raw_bytes, &sample_count, &is_pcm8b);
        if (picked == 0 && raw && sample_count > 0u) {
            if (is_pcm8b) {
                sfx->live_samples[i].pcm = (const int8_t *)raw;
                sfx->live_samples[i].length = sample_count;
            } else {
                int8_t *conv = (int8_t *)malloc(sample_count);
                if (conv) {
                    for (size_t k = 0; k < sample_count && k * 2u < raw_bytes; k++)
                        conv[k] = (int8_t)raw[k * 2u];
                    sfx->live_scratch[i] = conv;
                    sfx->live_samples[i].pcm = conv;
                    sfx->live_samples[i].length = sample_count;
                }
            }
        } else if (!snd1_ton_is_bank(&spans[k_map[i].span]) && sfx->samples[i].pcm) {
            sfx->live_samples[i] = sfx->samples[i];
        }
    }
    if (!sfx->live_samples[RUNWAY_SFX_WALK].pcm) {
        for (i = 0; i < RUNWAY_SFX_COUNT; i++) {
            if (sfx->live_samples[i].pcm) {
                sfx->live_samples[RUNWAY_SFX_WALK] = sfx->live_samples[i];
                break;
            }
        }
    }

    sfx->seq_counters[0] = 0;
    sfx->seq_counters[1] = 3;
    return 0;
}

int runway_sfx_load(runway_sfx *sfx, cdimage *cd) {
    return runway_sfx_load_dir(sfx, cd, "/04RUNWAY");
}

void runway_sfx_free(runway_sfx *sfx) {
    if (!sfx) return;
    free(sfx->blob);
    free(sfx->snd2_blob);
    free(sfx->snd3_blob);
    free(sfx->event_pcm_scratch);
    free(sfx->event_mix);
    for (unsigned i = 0; i < RUNWAY_SFX_COUNT; i++)
        free(sfx->live_scratch[i]);
    memset(sfx, 0, sizeof *sfx);
}

void runway_sfx_sound_event(runway_sfx *sfx, unsigned event_index) {
    if (!sfx || event_index >= 64u) return;
    sfx->event_hits[event_index]++;
}

#ifdef _MSC_VER
static __declspec(thread) runway_sfx_frame *active_sfx_frame;
#else
static _Thread_local runway_sfx_frame *active_sfx_frame;
#endif

void runway_sfx_begin_frame(runway_sfx_frame *frame) {
    if (frame) frame->count = 0;
    active_sfx_frame = frame;
}

void runway_sfx_end_frame(void) {
    active_sfx_frame = NULL;
}

static void emit_command(runway_sfx_command command) {
    if (active_sfx_frame && active_sfx_frame->count < RUNWAY_SFX_FRAME_LIMIT)
        active_sfx_frame->commands[active_sfx_frame->count++] = command;
}

enum { SFX_EVENT, SFX_CHANNEL_NOTE, SFX_BANK_NOTE, SFX_LAFF_JOKE };

void runway_sfx_emit(unsigned id) {
    if (id < 256u)
        emit_command((runway_sfx_command){SFX_EVENT, (uint8_t)id, 0, 0, 0});
}

void runway_sfx_emit_note(unsigned channel, unsigned note, unsigned velocity) {
    if (channel < 16u && note < 128u && velocity && velocity < 128u)
        emit_command((runway_sfx_command){SFX_CHANNEL_NOTE, (uint8_t)channel, 0,
                                         (uint8_t)note, (uint8_t)velocity});
}

void runway_sfx_emit_tone(unsigned bank, unsigned program, unsigned note, unsigned velocity) {
    if (bank < 4u && program < 128u && note < 128u && velocity && velocity < 128u)
        emit_command((runway_sfx_command){SFX_BANK_NOTE, (uint8_t)bank, (uint8_t)program,
                                         (uint8_t)note, (uint8_t)velocity});
}

void runway_sfx_emit_joke(unsigned joke_index, unsigned part) {
    emit_command((runway_sfx_command){SFX_LAFF_JOKE, (uint8_t)(joke_index & 0xffu),
                                     (uint8_t)(part & 0xffu), 0, 0});
}

/* A bounded one-shot renderer (6 s @ 22050 Hz covers multi-second LAFF speech). */
#define SFX_MAX_SAMPLES 132300u

static int tone_sample(const snd1_tone *tone, size_t sample) {
    if (tone->pcm8) return (int8_t)tone->data[sample] * 256;
    return (int16_t)read_be16(tone->data + sample * 2u);
}

static const snd1_span *tone_bank(const snd1_span *spans, unsigned bank) {
    if (spans[4u + bank].size) return &spans[4u + bank];
    /* A zero-sized area can alias another tone bank at the same sound RAM address. */
    const snd1_span *map = &spans[2];
    const uint8_t *area = NULL;
    for (size_t i = 0; i + 8u <= map->size; i += 8u)
        if (map->data[i] == bank) area = map->data + i;
    if (area)
        for (size_t i = 0; i + 8u <= map->size; i += 8u) {
            unsigned alias = map->data[i];
            if (alias < 4u && spans[4u + alias].size &&
                !memcmp(area + 1u, map->data + i + 1u, 3u))
                return &spans[4u + alias];
        }
    return &spans[4u + bank];
}

static double scsp_decay_step_22k(unsigned rate) {
    if (!rate) return 0.0;
    unsigned eff = rate * 2u;
    if (eff > 60u) eff = 60u;
    return 16.0 * pow(2.0, -((double)(60u - eff)) / 4.0);
}

static void mix_layer(runway_sfx *sfx, snd1_tone tone, unsigned note,
                      unsigned velocity, size_t start, size_t gate, size_t *used) {
    enum { EG_ATTACK, EG_DECAY1, EG_DECAY2, EG_RELEASE };
    if (!tone.ar || tone.tl >= 255u) return;
    double semitones = (int)note - (int)tone.root_note + tone.fine_tune / 100.0;
    double step = (44100.0 / RUNWAY_SFX_RATE) * (1.0 + tone.fine_step / 1024.0) *
                  pow(2.0, tone.octave + semitones / 12.0);
    double tl_gain = tone.tl ? pow(2.0, -(double)tone.tl / 16.0) : 1.0;
    double vel_gain = (velocity / 127.0) * tl_gain;
    double dl_target = (tone.dl == 31u) ? 1023.0 : (double)(tone.dl * 32u);
    double d1_step = scsp_decay_step_22k(tone.d1r);
    double d2_step = scsp_decay_step_22k(tone.d2r);
    double rr_step = scsp_decay_step_22k(tone.rr ? tone.rr : 14u);
    double ar_step = (tone.ar >= 30u) ? 1024.0
                                      : 48.0 * pow(2.0, -((double)(60u - tone.ar * 2u)) / 4.0);
    int eg_state = (tone.ar >= 30u) ? (dl_target > 0.0 ? EG_DECAY1 : EG_DECAY2) : EG_ATTACK;
    double atten = (eg_state == EG_ATTACK) ? 960.0 : 0.0;

    size_t max_avail = SFX_MAX_SAMPLES - start;
    size_t release_cap = RUNWAY_SFX_RATE / 2u;
    size_t limit = max_avail;
    if (gate < SIZE_MAX) {
        size_t with_rel = (gate > max_avail - release_cap) ? max_avail : (gate + release_cap);
        if (with_rel < limit) limit = with_rel;
    }
    double position = 0.0;
    size_t written = 0;
    for (; written < limit; ++written) {
        if (written >= gate) eg_state = EG_RELEASE;
        if (eg_state != EG_ATTACK && atten >= 960.0) break;
        if (position >= (double)tone.samples) {
            if (!tone.loop_mode || gate == SIZE_MAX || tone.loop_start >= tone.samples)
                break;
            position = tone.loop_start + fmod(position - tone.loop_start,
                                              (double)(tone.samples - tone.loop_start));
        }
        size_t i = (size_t)position;
        int a = tone_sample(&tone, i);
        int b = i + 1u < tone.samples ? tone_sample(&tone, i + 1u) : a;
        double pcm = a + (b - a) * (position - (double)i);
        double eg_gain = (atten <= 0.0) ? 1.0 : pow(2.0, -atten / 64.0);
        if (gate < SIZE_MAX && limit > gate && written + 64u > limit)
            eg_gain *= (double)(limit - written) / 64.0;
        sfx->event_mix[start + written] += (int32_t)(pcm * vel_gain * eg_gain);
        position += step;

        if (eg_state == EG_ATTACK) {
            atten -= (1024.0 - atten) * ar_step / 256.0 + ar_step * 0.5;
            if (atten <= 0.0) {
                atten = 0.0;
                eg_state = (dl_target > 0.0) ? EG_DECAY1 : EG_DECAY2;
            }
        } else if (eg_state == EG_DECAY1) {
            if (d1_step <= 0.0) {
                /* D1R=0 holds peak amplitude until key-off. */
            } else {
                atten += d1_step;
                if (atten >= dl_target) {
                    atten = dl_target;
                    eg_state = EG_DECAY2;
                }
            }
        } else if (eg_state == EG_DECAY2) {
            atten += d2_step;
        } else {
            atten += rr_step;
        }
    }
    if (written && start + written > *used) *used = start + written;
}

static void mix_note(runway_sfx *sfx, const snd1_span *spans, unsigned bank,
                     unsigned program, unsigned note, unsigned velocity,
                     size_t start, size_t gate, size_t *used) {
    if (bank >= 4u || note >= 128u || !velocity || start >= SFX_MAX_SAMPLES)
        return;
    const snd1_span *span = tone_bank(spans, bank);
    unsigned next_layer = 0;
    snd1_tone tone;
    while (!snd1_ton_layer(span, program, (uint8_t)note, &next_layer, &tone))
        mix_layer(sfx, tone, note, velocity, start, gate, used);
}

/* Saturn SEQ time is in ticks at the song's division, with tempo segments. */
static size_t sequence_sample(uint32_t tick, const uint8_t *tempo, unsigned count,
                              unsigned division) {
    uint64_t microseconds = 0;
    for (unsigned i = 0; i < count; ++i) {
        uint32_t segment = read_be32(tempo + 8u*i);
        uint32_t take = i + 1u == count || tick < segment ? tick : segment;
        microseconds += (uint64_t)take * read_be32(tempo + 8u*i + 4u);
        tick -= take;
        if (!tick) break;
    }
    uint64_t scaled = microseconds / division;
    if (scaled >= (uint64_t)SFX_MAX_SAMPLES * 1000000u / RUNWAY_SFX_RATE)
        return SFX_MAX_SAMPLES;
    return (size_t)(scaled * RUNWAY_SFX_RATE / 1000000u);
}

static void render_sequence(runway_sfx *sfx, const snd1_span *spans,
                            unsigned bank, unsigned ordinal, size_t *used) {
    if (bank >= 4u) return;
    const snd1_span *seq = &spans[8u + bank];
    if (seq->size < 6u) return;
    unsigned songs = read_be16(seq->data);
    size_t index_end = 2u + 4u * songs;
    if (ordinal >= songs || index_end > seq->size) return;
    size_t first = read_be32(seq->data + 2u + ordinal * 4u);
    size_t end = ordinal + 1u < songs
                 ? read_be32(seq->data + 6u + ordinal * 4u) : seq->size;
    if (first < index_end || end > seq->size || end < first || end - first < 16u)
        return;
    const uint8_t *song = seq->data + first;
    unsigned division = read_be16(song);
    unsigned ntempo = read_be16(song + 2u);
    size_t commands = first + read_be16(song + 4u);
    if (!division || !ntempo || ntempo > 64u || commands < first + 8u + 8u * ntempo ||
        commands >= end) return;
    uint8_t banks[16] = {0}, programs[16] = {0};
    uint32_t tick = 0, gate_extension = 0, step_extension = 0;
    size_t cursor = commands, resume = 0;
    unsigned remaining = 0;
    static const unsigned extensions[8] = {512, 2048, 4096, 8192, 256, 512, 2048, 4096};
    /* Bound compressed references even in a malformed cyclic stream. */
    for (unsigned i = 0; i < 8192u && cursor < end; ++i) {
        uint8_t status = seq->data[cursor++];
        unsigned ch = status & 15u;
        if (status == 0x83u) break;
        if (status >= 0x88u && status <= 0x8fu) {
            if (status < 0x8cu) gate_extension += extensions[status - 0x88u];
            else step_extension += extensions[status - 0x88u];
            continue;
        }
        const uint8_t *data = seq->data + cursor;
        uint32_t delay = 0;
        unsigned size = 0;
        if (status < 0x80u) {
            size = 4;
            if (end - cursor < size) break;
            uint32_t gate = data[2] + ((status & 0x40u) ? 256u : 0u) + gate_extension;
            delay = data[3] + ((status & 0x20u) ? 256u : 0u);
            size_t start = sequence_sample(tick, song + 8u, ntempo, division);
            if (start == SFX_MAX_SAMPLES) break;
            size_t stop = sequence_sample(tick + gate, song + 8u, ntempo, division);
            if (stop > start)
                mix_note(sfx, spans, banks[ch], programs[ch], data[0], data[1],
                         start, stop - start, used);
            gate_extension = 0;
        } else if (status == 0x81u) {
            if (end - cursor < 3u || remaining) break;
            size_t target = commands + read_be16(data);
            if (target >= end || !data[2]) break;
            resume = cursor + 3u;
            cursor = target;
            remaining = data[2];
            continue;
        } else {
            /* Loop markers are executed once for a one-shot, like the linear events. */
            unsigned high = status >> 4u;
            if (status == 0x82u) size = 1;
            else if (high == 0xau || high == 0xbu) size = 3;
            else if (high == 0xcu || high == 0xdu || high == 0xeu) size = 2;
            else break;
            if (end - cursor < size) break;
            delay = data[size - 1u];
            if (high == 0xbu && data[0] == 32u) banks[ch] = data[1];
            if (high == 0xcu) programs[ch] = data[0];
        }
        tick += delay + step_extension;
        step_extension = 0;
        cursor += size;
        if (remaining && !--remaining) cursor = resume;
    }
}

static void render_bank_event(runway_sfx *sfx, const snd1_span *spans,
                              unsigned id, size_t *used) {
    mb_sound_event cursor;
    mb_sound_event_record record;
    if (mb_sound_event_open(&cursor, spans[0].data, spans[0].size, id) != 1)
        return;
    while (mb_sound_event_next(&cursor, &record) == 1) {
        const uint32_t *word = record.words;
        unsigned block = word[2] & 7u;
        unsigned ch = word[5] & 15u;
        if (word[1] == 1u) {
            if (word[4] == 1u)
                mix_note(sfx, spans, sfx->channel_banks[block][ch],
                         sfx->channel_programs[block][ch],
                         word[6] & 127u, word[7] & 127u, 0, SIZE_MAX, used);
            else
                set_midi_channel(sfx, word);
        } else if (word[1] == 3u) {
            render_sequence(sfx, spans, word[3], word[4], used);
        }
    }
}

static void render_laff_joke(runway_sfx *sfx, const snd1_span *spans,
                             unsigned joke_index, unsigned part, size_t *used) {
    if (!sfx->laff_joke_count || !sfx->snd2_blob || !sfx->snd3_blob) return;
    unsigned j = joke_index % sfx->laff_joke_count;
    const uint8_t *s2 = (const uint8_t *)sfx->snd2_blob;
    const uint8_t *s3 = (const uint8_t *)sfx->snd3_blob;
    size_t o2 = read_be32(s2 + j * 8u), l2 = read_be32(s2 + j * 8u + 4u);
    size_t o3 = read_be32(s3 + j * 8u), l3 = read_be32(s3 + j * 8u + 4u);
    if (!o2 || !l2 || !o3 || !l3 ||
        o2 > sfx->snd2_size || l2 > sfx->snd2_size - o2 ||
        o3 > sfx->snd3_size || l3 > sfx->snd3_size - o3)
        return;
    snd1_span joke_spans[SND1_SPAN_COUNT];
    memcpy(joke_spans, spans, sizeof joke_spans);
    joke_spans[4] = (snd1_span){s3 + o3, l3};
    joke_spans[8] = (snd1_span){s2 + o2, l2};
    if (part < 1u || part > 3u) part = 1u;
    render_sequence(sfx, joke_spans, 0u, part, used);
}

static int render_commands(runway_sfx *sfx, const runway_sfx_frame *frame,
                           const int8_t **pcm, size_t *length, int tally) {
    snd1_span spans[SND1_SPAN_COUNT];
    size_t used = 0;
    if (!sfx || !frame || !pcm || !length ||
        snd1_parse(sfx->blob, sfx->blob_size, spans)) return -1;
    *pcm = NULL;
    *length = 0;
    if (!frame->count) return -1;
    if (!sfx->event_mix) sfx->event_mix = malloc(SFX_MAX_SAMPLES * sizeof *sfx->event_mix);
    if (!sfx->event_pcm_scratch) sfx->event_pcm_scratch = malloc(SFX_MAX_SAMPLES);
    if (!sfx->event_mix || !sfx->event_pcm_scratch) return -1;
    memset(sfx->event_mix, 0, SFX_MAX_SAMPLES * sizeof *sfx->event_mix);
    for (unsigned i = 0; i < frame->count; ++i) {
        const runway_sfx_command *command = &frame->commands[i];
        unsigned selector = command->bank;
        if (command->kind == SFX_EVENT) {
            if (tally) runway_sfx_sound_event(sfx, selector);
            render_bank_event(sfx, spans, selector, &used);
        } else if (command->kind == SFX_LAFF_JOKE) {
            render_laff_joke(sfx, spans, selector, command->program, &used);
        } else {
            unsigned bank = selector, program = command->program;
            if (command->kind == SFX_CHANNEL_NOTE) {
                unsigned ch = selector & 15u;
                bank = sfx->channel_banks[0][ch];
                program = sfx->channel_programs[0][ch];
            }
            mix_note(sfx, spans, bank, program, command->note, command->velocity,
                     0, SIZE_MAX, &used);
        }
    }
    if (!used) return -1;
    for (size_t i = 0; i < used; ++i) {
        int32_t value = sfx->event_mix[i] / 256;
        sfx->event_pcm_scratch[i] = (int8_t)(value < -128 ? -128 : value > 127 ? 127 : value);
    }
    *pcm = sfx->event_pcm_scratch;
    *length = used;
    return 0;
}

int runway_sfx_event_pcm(runway_sfx *sfx, unsigned event_id,
                         const int8_t **pcm, size_t *length) {
    if (event_id >= 256u) return -1;
    const runway_sfx_frame frame = {{{SFX_EVENT, (uint8_t)event_id, 0, 0, 0}}, 1};
    return render_commands(sfx, &frame, pcm, length, 0);
}

unsigned runway_sfx_dispatch_events(runway_sfx *sfx, const runway_sfx_frame *frame,
                                    runway_sfx_play_fn play, void *user) {
    const int8_t *pcm;
    size_t length;
    if (!play || render_commands(sfx, frame, &pcm, &length, 1)) return 0;
    play(user, pcm, length, RUNWAY_SFX_RATE);
    return 1;
}

static unsigned unemitted_sfx_flags(const runway_sfx_frame *frame, unsigned flags) {
    if (!frame || !flags) return flags;
    for (unsigned i = 0; i < frame->count; ++i) {
        const runway_sfx_command *cmd = &frame->commands[i];
        if (cmd->kind != SFX_EVENT) continue;
        switch (cmd->bank) {
        case 0x0cu:
        case 0x2eu:
            flags &= ~RUNWAY_SFX_FLAG_TALL;
            break;
        case 0x22u:
        case 0x2au:
        case 0x30u:
            flags &= ~RUNWAY_SFX_FLAG_JUMP;
            break;
        case 0x23u:
            if (flags & RUNWAY_SFX_FLAG_JUMP) flags &= ~RUNWAY_SFX_FLAG_JUMP;
            else flags &= ~RUNWAY_SFX_FLAG_C;
            break;
        case 0x17u:
        case 0x29u:
        case 0x35u:
            flags &= ~RUNWAY_SFX_FLAG_C;
            break;
        case 0x2du:
            flags &= ~RUNWAY_SFX_FLAG_WALK;
            break;
        default:
            break;
        }
    }
    return flags;
}

unsigned runway_sfx_dispatch_frame(runway_sfx *sfx, const runway_sfx_frame *frame,
                                   unsigned flags, runway_sfx_play_fn play, void *user) {
    unsigned sent = 0;
    if (!sfx || !play) return 0;
    if (frame && frame->count)
        sent = runway_sfx_dispatch_events(sfx, frame, play, user);
    unsigned rem = sent ? unemitted_sfx_flags(frame, flags) : flags;
    if (rem) {
        runway_sfx_dispatch_pcm(sfx, rem, play, user);
        sent++;
    }
    return sent;
}

static uint8_t seq_cb(void *context, uint8_t slot, uint8_t a, uint8_t b, uint8_t c) {
    (void)a; (void)b; (void)c;
    /* Slot becomes the sample pick within the id's alternate set — host keeps
     * one PCM per id; slot is recorded via context as last sequence slot. */
    if (context) *(uint8_t *)context = slot;
    return slot;
}

int runway_sfx_select(runway_sfx *sfx, unsigned id,
                      const int8_t **pcm, size_t *length) {
    uint8_t slot = 0;
    int group;
    if (!sfx || !pcm || !length || id >= RUNWAY_SFX_COUNT) return -1;
    if (!sfx->samples[id].pcm || !sfx->samples[id].length) return -1;
    /* Walk/tall use sequence group 0 (3 slots); jump/C/land use group 1. */
    group = (id == RUNWAY_SFX_WALK || id == RUNWAY_SFX_TALL) ? 0 : 1;
    (void)mb_sound_start_sequence(sfx->seq_counters, group, (uint8_t)id, 0, 0,
                                  seq_cb, &slot);
    *pcm = sfx->samples[id].pcm;
    *length = sfx->samples[id].length;
    (void)slot;
    return 0;
}

void runway_sfx_dispatch(runway_sfx *sfx, unsigned flags,
                         runway_sfx_play_fn play, void *user) {
    unsigned id;
    if (!sfx || !play || !flags) return;
    for (id = 0; id < RUNWAY_SFX_COUNT; id++) {
        const int8_t *pcm = NULL;
        size_t len = 0;
        if (!(flags & (1u << id))) continue;
        if (runway_sfx_select(sfx, id, &pcm, &len) == 0)
            play(user, pcm, len, RUNWAY_SFX_RATE);
    }
}

int runway_sfx_select_pcm(runway_sfx *sfx, unsigned id,
                          const int8_t **pcm, size_t *length) {
    uint8_t slot = 0;
    int group;
    if (!sfx || !pcm || !length || id >= RUNWAY_SFX_COUNT) return -1;
    if (!sfx->live_samples[id].pcm || !sfx->live_samples[id].length) return -1;
    group = (id == RUNWAY_SFX_WALK || id == RUNWAY_SFX_TALL) ? 0 : 1;
    (void)mb_sound_start_sequence(sfx->seq_counters, group, (uint8_t)id, 0, 0,
                                  seq_cb, &slot);
    *pcm = sfx->live_samples[id].pcm;
    *length = sfx->live_samples[id].length;
    (void)slot;
    return 0;
}

void runway_sfx_dispatch_pcm(runway_sfx *sfx, unsigned flags,
                             runway_sfx_play_fn play, void *user) {
    unsigned id;
    if (!sfx || !play || !flags) return;
    for (id = 0; id < RUNWAY_SFX_COUNT; id++) {
        const int8_t *pcm = NULL;
        size_t len = 0;
        if (!(flags & (1u << id))) continue;
        if (runway_sfx_select_pcm(sfx, id, &pcm, &len) == 0)
            play(user, pcm, len, RUNWAY_SFX_RATE);
    }
}
