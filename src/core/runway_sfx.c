#include "runway_sfx.h"
#include "resource.h"
#include "snd1.h"
#include "snd1_pcm.h"
#include "sound_event.h"
#include "sound_sequence.h"
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
    memset(tables, 0, sizeof tables);
    for (i = 0; i < SND1_SPAN_COUNT; i++)
        (void)snd1_pcm_open(&tables[i], &spans[i]);

    {
        unsigned got = 0;
        for (i = 0; i < RUNWAY_SFX_COUNT; i++) {
            unsigned sp = k_map[i].span;
            unsigned ix = k_map[i].index;
            const int8_t *pcm = NULL;
            size_t len = 0;
            if (sp >= SND1_SPAN_COUNT ||
                snd1_pcm_sample(&tables[sp], ix, &pcm, &len)) {
                /* §16: other stages vary span layout — leave slot empty. */
                continue;
            }
            sfx->samples[i].pcm = pcm;
            sfx->samples[i].length = len;
            got++;
        }
        if (got == 0) {
            /* Fallback: first available PCM in any span. */
            for (i = 0; i < SND1_SPAN_COUNT && got == 0; i++) {
                const int8_t *pcm = NULL;
                size_t len = 0;
                if (snd1_pcm_sample(&tables[i], 0, &pcm, &len) == 0 && pcm && len) {
                    sfx->samples[RUNWAY_SFX_WALK].pcm = pcm;
                    sfx->samples[RUNWAY_SFX_WALK].length = len;
                    got = 1;
                }
            }
        }
        if (got == 0) {
            runway_sfx_free(sfx);
            return -1;
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
    memset(sfx, 0, sizeof *sfx);
}

void runway_sfx_sound_event(runway_sfx *sfx, unsigned event_index) {
    if (!sfx || event_index >= 64u) return;
    sfx->event_hits[event_index]++;
}

int runway_sfx_event_pcm(runway_sfx *sfx, unsigned event_id,
                         const int8_t **pcm, size_t *length) {
    mb_sound_event ev;
    mb_sound_event_record rec;
    snd1_span spans[SND1_SPAN_COUNT];
    snd1_pcm_table table;
    unsigned sample = 0;
    int got_sample = 0, rc;
    if (!sfx || !pcm || !length || !sfx->blob || !sfx->blob_size) return -1;
    *pcm = NULL;
    *length = 0;
    rc = mb_sound_event_open(&ev, sfx->blob, sfx->blob_size, event_id);
    if (rc != 1) return -1;
    while (mb_sound_event_next(&ev, &rec) == 1) {
        /* Type 3 oneshot: word[4] holds the span-4 sample index. */
        if (rec.words[1] == 3u) {
            sample = rec.words[4];
            got_sample = 1;
            break;
        }
    }
    if (!got_sample) return -1;
    if (snd1_parse(sfx->blob, sfx->blob_size, spans) || !spans[4].size)
        return -1;
    memset(&table, 0, sizeof table);
    if (snd1_pcm_open(&table, &spans[4])) return -1;
    if (snd1_pcm_sample(&table, sample, pcm, length) == 0 && *pcm && *length)
        return 0;
    /* FINALE activate IDs cite 0x13..0x15 past span4 count — use s15 body. */
    if (sample >= table.count &&
        snd1_pcm_sample(&table, 15u, pcm, length) == 0 && *pcm && *length)
        return 0;
    return -1;
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
