#ifndef MB_RUNWAY_SFX_H
#define MB_RUNWAY_SFX_H
#include "cdimage.h"
#include <stddef.h>
#include <stdint.h>

/*
 * RUNWAY SFX bank from /04RUNWAY/05SND1 (§12n).
 * Span 4: 20 PCM8 samples (s05/s07 ≈3748 B footsteps/rattles).
 * Span 5: 24 PCM8 samples (mid/short cues).
 * Saturn path: _SoundEvent @0x204128 walks event records (8×BE32) from SND1
 * component 0; _SoundStartSequence @0x2041d0 rotates group slots then calls
 * a start callback. Host maps phase sfx_flags → concrete PCM samples.
 */

#define RUNWAY_SFX_WALK 0u
#define RUNWAY_SFX_TALL 1u
#define RUNWAY_SFX_C 2u
#define RUNWAY_SFX_JUMP 3u
#define RUNWAY_SFX_LAND 4u
#define RUNWAY_SFX_COUNT 5u

#define RUNWAY_SFX_FLAG_WALK (1u << RUNWAY_SFX_WALK)
#define RUNWAY_SFX_FLAG_TALL (1u << RUNWAY_SFX_TALL)
#define RUNWAY_SFX_FLAG_C (1u << RUNWAY_SFX_C)
#define RUNWAY_SFX_FLAG_JUMP (1u << RUNWAY_SFX_JUMP)
#define RUNWAY_SFX_FLAG_LAND (1u << RUNWAY_SFX_LAND)

#define RUNWAY_SFX_RATE 22050u

typedef struct {
    const int8_t *pcm;
    size_t length;
} runway_sfx_sample;

typedef struct {
    void *blob;
    size_t blob_size;
    runway_sfx_sample samples[RUNWAY_SFX_COUNT];
    uint32_t seq_counters[2]; /* SoundStartSequence groups */
    unsigned event_hits[16];  /* SoundEvent(index) host tally */
} runway_sfx;

int runway_sfx_load(runway_sfx *sfx, cdimage *cd);
/* Load 05SND1 from `iso_dir` (e.g. "/04RUNWAY"). */
int runway_sfx_load_dir(runway_sfx *sfx, cdimage *cd, const char *iso_dir);
void runway_sfx_free(runway_sfx *sfx);

/* _SoundEvent stand-in: record index hit (SND1 event table). */
void runway_sfx_sound_event(runway_sfx *sfx, unsigned event_index);

/*
 * _SoundStartSequence stand-in then play mapped PCM for `id`.
 * Returns 0 if a sample was selected (caller plays via host_audio).
 */
int runway_sfx_select(runway_sfx *sfx, unsigned id,
                      const int8_t **pcm, size_t *length);

/* Fire every set bit in flags; invokes play(pcm,len,rate,user) per sample. */
typedef void (*runway_sfx_play_fn)(void *user, const int8_t *pcm, size_t length,
                                   unsigned rate);
void runway_sfx_dispatch(runway_sfx *sfx, unsigned flags,
                         runway_sfx_play_fn play, void *user);

#endif
