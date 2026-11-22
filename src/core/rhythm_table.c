#include "special_phase.h"
#include <stdlib.h>
#include <string.h>

#define CODE_BASE 0x232000u
#define DRUM_RECORDS (0x26b58cu - CODE_BASE)
#define LICK_RECORDS (0x283bc0u - CODE_BASE)

static const rhythm_table drum = {NULL, 0, 1, 10, 0, "DRUM", "CheckJoy 24897c / DrumSequence 26b58c"};
static const rhythm_table guitar = {NULL, 0, 1, 9, 0, "GUITAR", "CGBones 261368 / LickStruct 283bc0"};

const rhythm_table *rhythm_table_for_stage(const char *name) {
    if (name && !strcmp(name, "DRUM")) return &drum;
    if (name && !strcmp(name, "GUITAR")) return &guitar;
    return NULL;
}

static uint32_t word(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

int special_phase_read_music(special_phase *p) {
    const uint8_t *code = p->code;
    const rhythm_table *type = rhythm_table_for_stage(p->stage_name);
    if (!type) return -1;
    p->score = *type;
    p->rhythm = &p->score;
    p->beat_period = 1;
    if (type == &guitar) {
        if (p->code_size < LICK_RECORDS + 6u * 16u) return -1;
        for (unsigned i = 0; i < 6; ++i) {
            const uint8_t *lick = code + LICK_RECORDS + 16u*i;
            uint32_t mask = word(lick + 12) >> 16;
            if (!word(lick) || word(lick + 8) < CODE_BASE ||
                word(lick + 8) - CODE_BASE >= p->code_size ||
                !(mask & 0x0770u) || (mask & (mask - 1u))) return -1;
            p->music_masks[i] = (uint16_t)mask;
            p->lick_durations[i] = word(lick);
        }
        p->music_wait = 0x500;
        p->active_lick = -1;
        runway_skel_loose(&p->skel, 0x15555);
        return 0;
    }
    if (p->code_size < DRUM_RECORDS + 16u) return -1;
    unsigned records = 0, groups = 0, previous = 0;
    for (size_t off = DRUM_RECORDS; ; off += 16u) {
        if (off > p->code_size - 16u) return -1;
        const uint8_t *entry = code + off;
        uint32_t at = word(entry);
        if (at == UINT32_MAX) break;
        if (at > UINT16_MAX || at < previous || word(entry + 4) > 3 || word(entry + 8) > 3) return -1;
        if (!records || at != previous) ++groups;
        previous = at;
        ++records;
    }
    if (!groups) return -1;
    p->drum_notes = calloc(groups, sizeof *p->drum_notes);
    if (!p->drum_notes) return -1;
    for (unsigned i = 0; i < records; ++i) {
        const uint8_t *entry = code + DRUM_RECORDS + 16u*i;
        uint16_t at = (uint16_t)word(entry);
        if (i && p->drum_notes[p->score.count-1].time == at) continue;
        /* CheckJoy takes the animation and group from the first simultaneous record. */
        p->drum_notes[p->score.count++] = (rhythm_note){at, (uint8_t)word(entry+8), (uint8_t)word(entry+4)};
    }
    for (unsigned i = 0; i < 4; ++i) {
        const uint8_t *mask = code + (0x26b574u - CODE_BASE) + 2u*i;
        p->music_masks[i] = (uint16_t)((mask[0] << 8) | mask[1]);
        if (p->music_masks[i] != rhythm_kind_button_mask(i)) return -1;
    }
    p->score.notes = p->drum_notes;
    return 0;
}

unsigned special_phase_autoplay_frame_limit(const special_phase *phase) {
    if (phase->kind==STAGE_KIND_BIGBONES || phase->kind==STAGE_KIND_GENROOM) return 12000;
    if (phase->mode!=SPECIAL_MODE_RHYTHM) return 0;
    const rhythm_table *score = phase->rhythm;
    if (score && score->count)
        return rhythm_note_host_frame(score, score->count - 1u);
    /* Guitar's last CD sector is 0x38a4 at 75 sectors/s. */
    return 0x38a4u * 60u / 75u + 2u;
}

uint16_t rhythm_kind_button_mask(unsigned group) {
    static const uint16_t mask[4] = {0x1000u, 0x2000u, 0x8000u, 0x4000u};
    return group < 4 ? mask[group] : 0;
}

unsigned rhythm_note_host_frame(const rhythm_table *t, unsigned index) {
    return t && t->notes && index < t->count ? 206u + t->notes[index].time : 0;
}

int rhythm_buttons_match(unsigned group, uint16_t buttons) {
    return (rhythm_kind_button_mask(group) & buttons) != 0;
}
