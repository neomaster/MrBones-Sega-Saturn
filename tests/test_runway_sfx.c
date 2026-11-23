#include "core/cdimage.h"
#include "core/identity.h"
#include "core/runway_phase.h"
#include "core/runway_sfx.h"
#include "core/saturn_pad.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

typedef struct {
    unsigned n;
    unsigned ids[8];
    size_t lens[8];
} play_log;

static void play_cb(void *user, const int8_t *pcm, size_t length, unsigned rate) {
    play_log *log = (play_log *)user;
    (void)pcm;
    if (rate != RUNWAY_SFX_RATE) return;
    if (log->n < 8u) {
        log->lens[log->n] = length;
        log->n++;
    }
}

int main(void) {
    const char *disc = getenv("MB_DISC1");
    const char *man = getenv("MB_MANIFEST");
    if (!disc || !man) {
        printf("SKIP runway_sfx (set MB_DISC1 and MB_MANIFEST)\n");
        return 0;
    }

    char err[512];
    cdimage cd;
    id_manifest manifest;
    id_result result;
    runway_sfx sfx;
    runway_phase phase;

    CHECK(manifest_load(&manifest, man, err, sizeof err) == 0);
    CHECK(cd_open(&cd, disc, err, sizeof err) == 0);
    CHECK(identity_check(&manifest, &cd, 1, ID_QUICK, &result));
    manifest_free(&manifest);

    /* §12n: load 05SND1 multi-sample bank (span4/span5 map). */
    CHECK(runway_sfx_load(&sfx, &cd) == 0);
    CHECK(sfx.samples[RUNWAY_SFX_WALK].pcm && sfx.samples[RUNWAY_SFX_WALK].length == 3748u);
    CHECK(sfx.samples[RUNWAY_SFX_TALL].pcm && sfx.samples[RUNWAY_SFX_TALL].length == 3748u);
    CHECK(sfx.samples[RUNWAY_SFX_C].pcm && sfx.samples[RUNWAY_SFX_C].length == 68u);
    CHECK(sfx.samples[RUNWAY_SFX_JUMP].pcm && sfx.samples[RUNWAY_SFX_JUMP].length == 612u);
    CHECK(sfx.samples[RUNWAY_SFX_LAND].pcm && sfx.samples[RUNWAY_SFX_LAND].length == 36u);
    CHECK(sfx.seq_counters[0] == 0u && sfx.seq_counters[1] == 3u);

    /* SoundEvent(0xc) host tally (Stand Down path). */
    runway_sfx_sound_event(&sfx, 0xcu);
    runway_sfx_sound_event(&sfx, 0xcu);
    CHECK(sfx.event_hits[0xc] == 2u);

    /* Multi-flag dispatch: walk+jump → 2 plays; StartSequence rotates groups. */
    {
        play_log log = {0};
        unsigned flags = RUNWAY_SFX_FLAG_WALK | RUNWAY_SFX_FLAG_JUMP;
        runway_sfx_dispatch(&sfx, flags, play_cb, &log);
        CHECK(log.n == 2u);
        CHECK(log.lens[0] == 3748u);
        CHECK(log.lens[1] == 612u);
        CHECK(sfx.seq_counters[0] == 1u); /* group 0 advanced (walk) */
        CHECK(sfx.seq_counters[1] == 4u); /* group 1 advanced (jump) */
    }

    /* Live SCSP .TON decoded PCM path: must avoid raw .TON header metadata. */
    {
        play_log log = {0};
        const int8_t *ev_pcm = NULL;
        size_t ev_len = 0;
        CHECK(sfx.live_samples[RUNWAY_SFX_WALK].pcm != NULL);
        CHECK(sfx.live_samples[RUNWAY_SFX_WALK].length >= 180u);
        CHECK(sfx.live_samples[RUNWAY_SFX_WALK].length != 3748u);
        CHECK(sfx.live_samples[RUNWAY_SFX_JUMP].length != 612u);
        runway_sfx_dispatch_pcm(&sfx, RUNWAY_SFX_FLAG_WALK | RUNWAY_SFX_FLAG_JUMP,
                                play_cb, &log);
        CHECK(log.n == 2u);
        CHECK(log.lens[0] == sfx.live_samples[RUNWAY_SFX_WALK].length);
        CHECK(log.lens[1] == sfx.live_samples[RUNWAY_SFX_JUMP].length);
        CHECK(runway_sfx_event_pcm(&sfx, 0x0cu, &ev_pcm, &ev_len) == 0);
        CHECK(ev_pcm != NULL && ev_len >= 180u && ev_len != 3748u && ev_len != 68u);
    }

    /* Multi-stage .TON banks: /05MAUSO (span 4) and /14DRUM (span 5 only). */
    {
        runway_sfx mauso_sfx, drum_sfx;
        CHECK(runway_sfx_load_dir(&mauso_sfx, &cd, "/05MAUSO") == 0);
        CHECK(mauso_sfx.live_samples[RUNWAY_SFX_WALK].pcm != NULL &&
              mauso_sfx.live_samples[RUNWAY_SFX_WALK].length >= 180u);
        runway_sfx_free(&mauso_sfx);
        CHECK(runway_sfx_load_dir(&drum_sfx, &cd, "/14DRUM") == 0);
        CHECK(drum_sfx.live_samples[RUNWAY_SFX_WALK].pcm != NULL &&
              drum_sfx.live_samples[RUNWAY_SFX_WALK].length >= 180u);
        runway_sfx_free(&drum_sfx);
    }

    /* Phase sync: crouch, footsteps in the running cycle, jump and landing. */
    CHECK(runway_phase_load(&phase, &cd) == 0);
    {
        saturn_pad pad = {0};
        saturn_pad_update_host(&pad, 0);
        runway_phase_step(&phase, &pad); /* boot consume */
        saturn_pad_update_host(&pad, MB_PAD_DOWN);
        runway_phase_step(&phase, &pad);
        CHECK(phase.sound_event_id == 0xcu);
        CHECK(phase.sched_enqueued_0c == 1);
        CHECK(phase.sfx_flags & RUNWAY_SFX_FLAG_TALL);

        saturn_pad_update_host(&pad, 0);
        runway_phase_step(&phase, &pad);
        CHECK(phase.sound_event_id == 0u);

        saturn_pad_update_host(&pad, MB_PAD_RIGHT);
        runway_phase_step(&phase, &pad);
        CHECK(phase.state == RUNWAY_ST_WALK && !(phase.sfx_flags & RUNWAY_SFX_FLAG_WALK));

        saturn_pad_update_host(&pad, 0);
        unsigned steps_seen = 0;
        for (int i = 0; i < 64; i++) {
            unsigned previous = phase.mplay.index;
            runway_phase_step(&phase, &pad);
            int footfall = phase.state == RUNWAY_ST_WALK && phase.mplay.index != previous &&
                           (phase.mplay.index == 3 || phase.mplay.index == 13);
            CHECK(!!(phase.sfx_flags & RUNWAY_SFX_FLAG_WALK) == footfall);
            if (footfall) steps_seen |= phase.mplay.index == 3 ? 1u : 2u;
        }
        CHECK(steps_seen == 3u);
        saturn_pad_update_host(&pad, MB_PAD_B);
        runway_phase_step(&phase, &pad);
        if (phase.airborne)
            CHECK(phase.sfx_flags & RUNWAY_SFX_FLAG_JUMP);

        saturn_pad_update_host(&pad, 0);
        {
            int saw_land = 0;
            for (int i = 0; i < 120; i++) {
                runway_phase_step(&phase, &pad);
                if (phase.sfx_flags & RUNWAY_SFX_FLAG_LAND) {
                    saw_land = 1;
                    break;
                }
            }
            CHECK(saw_land);
        }
    }

    runway_phase_free(&phase);
    runway_sfx_free(&sfx);
    cd_close(&cd);
    printf("OK runway_sfx sequence\n");
    return 0;
}
