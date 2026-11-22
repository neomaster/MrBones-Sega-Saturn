#ifndef MB_FINALE_BODY_H
#define MB_FINALE_BODY_H
#include <stddef.h>
#include <stdint.h>
#include "stxtr.h"

enum { FINALE_BODY_JOINTS = 23 };
typedef struct finale_body finale_body;
/* Red bolt, two blue bolts (including ring breaks), body impact, shot manager's face. */
enum { FINALE_FACE_SPRITE = 4, FINALE_SPRITES_MAX };
typedef struct {
    const uint8_t *pixels;
    unsigned width, height;
    int x, y;
    double depth;
} finale_sprite;
finale_body *finale_body_load(const void *code, size_t code_size, const void *textures,
                              size_t textures_size, const uint8_t random_table[256]);
void finale_body_tick(finale_body *body, unsigned charged,
                      const finale_sprite sprites[FINALE_SPRITES_MAX]);
void finale_body_command(finale_body *body, unsigned command);
void finale_body_draw_ring(const finale_body *body, const uint8_t *backdrop, uint8_t *rgba,
                           const int16_t xy[8], const stxtr_record *texture);
/* Returns the 320x224 map A coverage buffer for the stage backdrop loader. */
uint8_t *finale_body_front_mask(finale_body *body);
void finale_body_draw(finale_body *body, uint8_t *rgba, unsigned hit_age, unsigned segments);
void finale_body_draw_health(const finale_body *body, uint8_t *rgba, unsigned segments);
finale_body *ice_body_load(const void *code, size_t code_size, const void *textures,
                          size_t texture_size, uint32_t init, unsigned skate,
                          const uint8_t palette_rgba[4096]);
uint8_t *ice_body_surface(finale_body *body);
void ice_body_draw(finale_body *body, uint8_t *rgba, unsigned tick, int track_mode,
                   int heading, double world_x, double world_y, double world_z);
/* World joint centers (16.16) before projection. Used by stage collision queries.
 * Angles use the Saturn's 1024-unit turns and scale is the BonesSetScale value. */
typedef struct {
    int origin[3], rotation[3], post_rotation[3], scale;
    unsigned tick;
    uint32_t config, tracks;
} stage_body_pose;
finale_body *stage_body_load_joints(const void *code, size_t size, uint32_t init,
                                    uint32_t next_key, uint32_t loop_key);
int stage_body_joints(finale_body *body,const stage_body_pose *pose,
                      int32_t points[FINALE_BODY_JOINTS][3]);
#endif
