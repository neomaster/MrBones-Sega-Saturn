#ifndef MB_RUNWAY_HAZARD_H
#define MB_RUNWAY_HAZARD_H
#include "runway_skel.h"
#include "pan_collision_map.h"
#include <stdint.h>

/*
 * RUNWAY obstacles / damage (§12o) — host stand-in for gang.o / tbones.o / poofs.
 * CheckForHit_CThrown@26da50: |bones_x − thrown_x| ≤ 0xB6DB (~20px) then
 * BonesObjHitChk@25c108 → damage path. CheckObstacle_CGangMember@26bbac switches
 * on coll class. Host uses fixed hazard zones + optional thrown projectiles.
 */

#define RUNWAY_HAZARD_ZONES 3
#define RUNWAY_THROWN_MAX 4
#define RUNWAY_THROWN_RANGE 0xB6DBu /* CheckForHit DAT_0026daa4 */

typedef struct {
    uint32_t x0, x1; /* world units inclusive span */
    int32_t damage;
} runway_hazard_zone;

typedef struct {
    uint32_t x, y;
    int32_t vx;
    int32_t damage;
    unsigned ttl;
    int active;
} runway_thrown;

typedef struct {
    runway_hazard_zone zones[RUNWAY_HAZARD_ZONES];
    runway_thrown thrown[RUNWAY_THROWN_MAX];
    unsigned zone_count;
    unsigned thrown_count;
    unsigned hits_applied;
} runway_hazards;

/* RIDE CChannel entries are pairs of PAN record index and signed strength. */
typedef struct {
    const uint8_t *keys;
    unsigned count, next, previous_record;
    int value, delta;
} ride_channel;
typedef struct {
    ride_channel channels[8];
    uint16_t wall_angle[16];
} ride_current;
int ride_current_init(ride_current *current, const uint8_t *code, size_t size);
void ride_current_step(ride_current *current, unsigned record, int *dx, int *dy);
unsigned ride_wall_coverage(const pan_collision_map *map, int x, int y);

void runway_hazards_init_runway(runway_hazards *h, unsigned units_per_pixel);
/* §22: per-stage hazard pads for all 7 15PAN stages (RUNWAY/LOGS/ICE1/ICE2/MOAT/RIDE/STAING). */
void runway_hazards_init_pan_stage(runway_hazards *h, const char *stage_name,
                                   unsigned units_per_pixel);
/* Apply zone + thrown hits against bones world X; returns 1 if death. */
int runway_hazards_step(runway_hazards *h, runway_skel *skel, uint32_t bones_x);
/* Height-aware variant: airborne_high skips ground-level zones and low throws. */
int runway_hazards_step_ex(runway_hazards *h, runway_skel *skel, uint32_t bones_x,
                           int airborne_high);
void runway_hazards_spawn_thrown(runway_hazards *h, uint32_t x, uint32_t y,
                                 int32_t damage);
void runway_hazards_spawn_thrown_vel(runway_hazards *h, uint32_t x, uint32_t y,
                                     int32_t vx, int32_t damage);

#endif
