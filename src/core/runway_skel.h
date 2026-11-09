#ifndef MB_RUNWAY_SKEL_H
#define MB_RUNWAY_SKEL_H
#include <stdint.h>

/*
 * CSkelBar / HEALTH.O stand-in (§12o).
 * MAP: skelbar.o Brain@268494 Update@2683a0 Loose@26877c Add@2687c8
 *      PauseBrain@2685dc DisablePause@2687f8 BonesLoose@2686c0 → EndLevel@2563d4
 * Object +0x48 = base skeletism, +0x4c = pump; max literal 0x20000.
 * UpdateSkelBar maps base into 1..14 UI segments (step ≈ −9314).
 * PauseBrain sets bPaused (loader ~0x2143EC) until Start edge.
 */

#define RUNWAY_SKEL_MAX 0x20000
#define RUNWAY_SKEL_SEGMENTS 14
#define RUNWAY_SKEL_SEG_STEP (-9314) /* signed word @268478 */
#define RUNWAY_HIT_TIMEOUT 20       /* HitTimeout .data init 0x14 */
#define RUNWAY_DAMAGE_DEFAULT 0x4000

typedef struct {
    int32_t skeletism; /* CSkelBar+0x48 */
    int32_t pump;      /* CSkelBar+0x4c */
    int segments;      /* 1..14 UI */
    int paused;
    int hit_cooldown;
    int died; /* EndLevel(1) path */
} runway_skel;

void runway_skel_init(runway_skel *s);
void runway_skel_update_bar(runway_skel *s);
/* BonesAddSkeletism — clamp to max, refresh bar. */
void runway_skel_add(runway_skel *s, int32_t amount);
/* BonesLooseSkeletism — subtract; returns 1 if death (EndLevel fail). */
int runway_skel_loose(runway_skel *s, int32_t amount);
/* Hit with cooldown (HitTimeout). Returns 1 if death. */
int runway_skel_hit(runway_skel *s, int32_t amount);
void runway_skel_tick_cooldown(runway_skel *s);
/* PauseBrain / DisablePause stand-in. */
void runway_skel_set_paused(runway_skel *s, int paused);

#endif
