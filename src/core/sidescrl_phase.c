#include "sidescrl_phase.h"
#include "core/mb_font.h"
#include "core/pan_palette.h"
#include "core/resource.h"
#include "core/runway_compose.h"
#include "core/runway_sfx.h"
#include "core/sprite_decode.h"
#include "core/stage_enemy.h"
#include "core/stxtr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCR_W 320u
#define SCR_H 224u
#define UNITS_PER_PIXEL 2341u
#define START_X (40u * UNITS_PER_PIXEL)
#define FINISH_X (START_X + 120u * UNITS_PER_PIXEL)
#define GROUND_Y (180u * UNITS_PER_PIXEL)
#define WALK_STEP (6u * UNITS_PER_PIXEL)
#define JUMP_IMPULSE (-(int32_t)(48u * UNITS_PER_PIXEL))
#define GRAVITY_Y ((int32_t)(3u * UNITS_PER_PIXEL))
#define SCROLL_STRIP_W 640u

static const char *k_layer_names[] = {
    "07ACDB", "08ATIL", "09BCDB", "10BTIL",
    "11CCDB", "12CTIL", "13DCDB", "14DTIL", NULL
};



static void build_palette(const uint8_t *code, size_t code_size,
                          const uint8_t *title, size_t title_size,
                          uint8_t *palette) {
    uint32_t words[1024], tmp[1024];
    size_t off, consumed, best = 0, i;
    const uint8_t *src[2];
    size_t src_sz[2];
    int s;
    memset(words, 0, sizeof words);
    src[0] = code;
    src_sz[0] = code_size;
    src[1] = title;
    src_sz[1] = title_size;
    for (s = 0; s < 2; s++) {
        if (!src[s] || src_sz[s] < 2048) continue;
        for (off = 0; off + 2048 <= src_sz[s] && off < 0x80000; off += 4) {
            if (src[s][off] != 0 || src[s][off + 1] != 0 ||
                src[s][off + 2] >= 4 || src[s][off + 4] != 1)
                continue;
            consumed = 0;
            memset(tmp, 0, sizeof tmp);
            if (pan_palette_apply(src[s] + off, src_sz[s] - off, tmp, 1024, &consumed))
                continue;
            if (consumed >= 2048 && consumed > best) {
                best = consumed;
                memcpy(words, tmp, sizeof words);
                if (best >= 4100) break;
            }
        }
        if (best >= 4100) break;
    }
    if (best < 64) {
        for (s = 0; s < 2; s++) {
            if (!src[s] || src_sz[s] < 16) continue;
            for (off = 0; off + 16 < src_sz[s] && off < 0x80000; off += 4) {
                consumed = 0;
                memset(tmp, 0, sizeof tmp);
                if (pan_palette_apply(src[s] + off, src_sz[s] - off, tmp, 1024, &consumed))
                    continue;
                if (consumed > best && consumed >= 64) {
                    best = consumed;
                    memcpy(words, tmp, sizeof words);
                    if (best >= 200) break;
                }
            }
            if (best >= 200) break;
        }
    }
    if (best < 64) {
        for (i = 0; i < 256; i++)
            words[i] = (uint32_t)((i << 16) | (i << 8) | i);
    }
    for (i = 0; i < 1024; i++) {
        palette[4 * i + 0] = 0;
        palette[4 * i + 1] = (uint8_t)(words[i] >> 16);
        palette[4 * i + 2] = (uint8_t)(words[i] >> 8);
        palette[4 * i + 3] = (uint8_t)words[i];
    }
}

static int decode_stxtr(const void *textures, size_t textures_size, size_t *offset,
                        const uint8_t *palette, uint8_t **out, unsigned *w,
                        unsigned *h) {
    static const uint16_t k_pmods[] = {0x0020, 0x0028, 0x00a0, 0x00a8, 0};
    stxtr_record rec;
    size_t size;
    uint8_t *pixels;
    unsigned i;
    if (!textures || !offset) return -1;
    if (stxtr_next(textures, textures_size, offset, &rec) != 1) return -1;
    size = (size_t)rec.width * rec.height * 4u;
    pixels = malloc(size);
    if (!pixels) return -1;
    for (i = 0; k_pmods[i]; i++) {
        if (mb_sprite_decode(rec.data, rec.size, rec.width, rec.height, k_pmods[i],
                             palette, 1024, pixels, size) == 0) {
            
            *out = pixels;
            *w = rec.width;
            *h = rec.height;
            return 0;
        }
    }
    free(pixels);
    return -1;
}

static int make_placeholder(uint8_t **out, unsigned *w, unsigned *h) {
    unsigned x, y;
    uint8_t *p = malloc(16u * 32u * 4u);
    if (!p) return -1;
    for (y = 0; y < 32; y++) {
        for (x = 0; x < 16; x++) {
            size_t i = ((size_t)y * 16u + x) * 4u;
            int edge = (x == 0 || y == 0 || x == 15 || y == 31);
            p[i + 0] = edge ? 255 : 200;
            p[i + 1] = edge ? 255 : 180;
            p[i + 2] = edge ? 255 : 60;
            p[i + 3] = 255;
        }
    }
    *out = p;
    *w = 16;
    *h = 32;
    return 0;
}

static const sidescrl_layer *find_layer(const sidescrl_phase *phase, const char *name) {
    unsigned i;
    for (i = 0; i < phase->layer_count; i++)
        if (strcmp(phase->layers[i].name, name) == 0)
            return &phase->layers[i];
    return NULL;
}

static const void *layer_data(const sidescrl_phase *phase, const char *name, size_t *sz) {
    const sidescrl_layer *L = find_layer(phase, name);
    if (!L) {
        *sz = 0;
        return NULL;
    }
    *sz = L->size;
    return L->data;
}

/* §23: LoadCDB/LoadMap/DrawMap + SCRLCOLLInitGetFloor via scrl_map. */
static int build_scroll_from_map(sidescrl_phase *phase) {
    size_t acdb_sz = 0, atil_sz = 0, bcdb_sz = 0, btil_sz = 0;
    size_t ccdb_sz = 0, ctil_sz = 0, dcdb_sz = 0, dtil_sz = 0;
    const void *acdb = layer_data(phase, "07ACDB", &acdb_sz);
    const void *atil = layer_data(phase, "08ATIL", &atil_sz);
    const void *bcdb = layer_data(phase, "09BCDB", &bcdb_sz);
    const void *btil = layer_data(phase, "10BTIL", &btil_sz);
    const void *ccdb = layer_data(phase, "11CCDB", &ccdb_sz);
    const void *ctil = layer_data(phase, "12CTIL", &ctil_sz);
    const void *dcdb = layer_data(phase, "13DCDB", &dcdb_sz);
    const void *dtil = layer_data(phase, "14DTIL", &dtil_sz);
    unsigned strip_w = SCROLL_STRIP_W;
    uint8_t *strip;

    if (scrl_map_init(&phase->map, acdb, acdb_sz, atil, atil_sz, bcdb, bcdb_sz,
                      btil, btil_sz, ccdb, ccdb_sz, ctil, ctil_sz, dcdb, dcdb_sz,
                      dtil, dtil_sz, phase->amap, phase->amap_size) &&
        !phase->map.has_amap) {
        /* Soft path still OK with empty planes if STXT present. */
    }

    if (phase->map.plane_count > 0 && phase->map.planes[0].pixel_w > strip_w &&
        phase->map.planes[0].pixel_w <= SCRL_STRIP_MAX)
        strip_w = phase->map.planes[0].pixel_w > 1280u
                      ? 1280u
                      : phase->map.planes[0].pixel_w;

    scrl_map_build_collision(&phase->map, strip_w);

    strip = malloc((size_t)strip_w * SCR_H * 4u);
    if (!strip) return -1;
    if (scrl_map_draw(&phase->map, phase->palette, strip, strip_w, SCR_H, 0)) {
        free(strip);
        return -1;
    }
    phase->scroll_rgba = strip;
    phase->scroll_w = strip_w;
    phase->scroll_h = SCR_H;
    return 0;
}

static uint32_t floor_units_at(const sidescrl_phase *phase, uint32_t bones_x) {
    unsigned world_px = bones_x / UNITS_PER_PIXEL;
    int fy = scrl_map_find_floor(&phase->map, (int)world_px);
    if (fy < 0) fy = 180;
    return (unsigned)fy * UNITS_PER_PIXEL;
}

static int move_blocked_wall(const sidescrl_phase *phase, uint32_t world_x) {
    unsigned px = world_x / UNITS_PER_PIXEL;
    return scrl_map_wall_at(&phase->map, (int)px);
}

static void stage_name_from_iso(char *out, size_t n, const char *iso_dir) {
    const char *p = strrchr(iso_dir, '/');
    p = p ? p + 1 : iso_dir;
    if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9')
        p += 2;
    snprintf(out, n, "%s", p);
}

static int load_opt(cdimage *cd, const char *iso_dir, const char *file,
                    void **out, size_t *sz) {
    char path[96];
    snprintf(path, sizeof path, "%s/%s", iso_dir, file);
    if (resource_load(cd, path, out, sz)) {
        *out = NULL;
        *sz = 0;
        return -1;
    }
    return 0;
}

int sidescrl_phase_load(sidescrl_phase *phase, cdimage *cd, const char *iso_dir) {
    unsigned i;
    size_t off;
    char path[96];
    if (!phase || !cd || !iso_dir) return -1;
    memset(phase, 0, sizeof *phase);
    snprintf(phase->iso_dir, sizeof phase->iso_dir, "%s", iso_dir);
    stage_name_from_iso(phase->stage_name, sizeof phase->stage_name, iso_dir);
    phase->facing = 1;
    phase->bones_x = START_X;
    phase->bones_y = GROUND_Y;
    phase->ground_y = GROUND_Y;
    phase->camera_x = START_X;
    phase->finish_x = FINISH_X;
    phase->state = SIDESCRL_ST_STAND;
    phase->audio_track = 3;
    runway_skel_init(&phase->skel);
    /* §24: poofs on all sidescrl; denser on BAT*. */
    stage_enemies_init_sidescrl(&phase->enemies, phase->stage_name,
                                UNITS_PER_PIXEL);
    stage_enemies_load_disc_sprites(cd);

    snprintf(path, sizeof path, "%s/03CODE", iso_dir);
    if (resource_load(cd, path, &phase->code, &phase->code_size) ||
        phase->code_size <= 20000)
        goto fail;
    snprintf(path, sizeof path, "%s/04STXT", iso_dir);
    if (resource_load(cd, path, &phase->textures, &phase->textures_size) ||
        phase->textures_size == 0)
        goto fail;
    (void)load_opt(cd, iso_dir, "02TITL", &phase->title, &phase->title_size);
    (void)load_opt(cd, iso_dir, "06AMAP", &phase->amap, &phase->amap_size);

    for (i = 0; k_layer_names[i] && phase->layer_count < SIDESCRL_LAYER_MAX; i++) {
        void *data = NULL;
        size_t sz = 0;
        if (load_opt(cd, iso_dir, k_layer_names[i], &data, &sz) || sz == 0)
            continue;
        snprintf(phase->layers[phase->layer_count].name,
                 sizeof phase->layers[phase->layer_count].name, "%s",
                 k_layer_names[i]);
        phase->layers[phase->layer_count].data = data;
        phase->layers[phase->layer_count].size = sz;
        phase->layer_count++;
    }

    build_palette((const uint8_t *)phase->code, phase->code_size,
                  (const uint8_t *)phase->title, phase->title_size, phase->palette);
    if (build_scroll_from_map(phase)) goto fail;

    {
        unsigned goal_px = (phase->scroll_w > 360u) ? (phase->scroll_w - 60u) : 260u;
        if (goal_px > 580u) goal_px = 580u;
        phase->finish_x = goal_px * UNITS_PER_PIXEL;
    }
    stage_enemies_populate_sidescrl_strip(&phase->enemies, phase->scroll_w);

    phase->bones_y = floor_units_at(phase, phase->bones_x);
    phase->ground_y = phase->bones_y;

    off = 0;
    while (phase->anim_count < SIDESCRL_ANIM_MAX) {
        size_t before = off;
        uint8_t *px = NULL;
        unsigned w = 0, h = 0;
        if (decode_stxtr(phase->textures, phase->textures_size, &off, phase->palette,
                         &px, &w, &h)) {
            if (off == before) break;
            continue;
        }
        if (w >= 8 && w <= 96 && h >= 12 && h <= 96) {
            phase->anim_rgba[phase->anim_count] = px;
            phase->anim_w[phase->anim_count] = w;
            phase->anim_h[phase->anim_count] = h;
            phase->anim_count++;
        } else {
            free(px);
        }
    }
    if (phase->anim_count == 0) {
        if (make_placeholder(&phase->anim_rgba[0], &phase->anim_w[0],
                             &phase->anim_h[0]))
            goto fail;
        phase->anim_count = 1;
    }
    phase->anim_index = 0;
    phase->sprite_rgba = phase->anim_rgba[0];
    phase->sprite_w = phase->anim_w[0];
    phase->sprite_h = phase->anim_h[0];

    phase->frame_rgba = calloc((size_t)SCR_W * SCR_H * 4u, 1);
    if (!phase->frame_rgba) goto fail;

    snprintf(phase->status, sizeof phase->status,
             "%s stand layers=%u planes=%u amap=%u", phase->stage_name,
             phase->layer_count, phase->map.plane_count, phase->map.amap_n);
    return sidescrl_phase_render(phase);

fail:
    sidescrl_phase_free(phase);
    return -1;
}

void sidescrl_phase_free(sidescrl_phase *phase) {
    unsigned i;
    if (!phase) return;
    for (i = 0; i < phase->anim_count && i < SIDESCRL_ANIM_MAX; i++)
        free(phase->anim_rgba[i]);
    for (i = 0; i < phase->layer_count && i < SIDESCRL_LAYER_MAX; i++)
        free(phase->layers[i].data);
    free(phase->title);
    free(phase->textures);
    free(phase->code);
    free(phase->amap);
    free(phase->scroll_rgba);
    free(phase->frame_rgba);
    memset(phase, 0, sizeof *phase);
}

int sidescrl_phase_step(sidescrl_phase *phase, const saturn_pad *pad) {
    uint32_t held;
    if (!phase || !pad || sidescrl_phase_done(phase)) return 0;
    phase->frames++;
    phase->sfx_flags = 0;
    held = pad->held;

    if (pad->pressed & 0x0800u) {
        if (phase->state == SIDESCRL_ST_PAUSED) {
            runway_skel_set_paused(&phase->skel, 0);
            phase->state = phase->pre_pause_state;
        } else {
            phase->pre_pause_state = phase->state;
            runway_skel_set_paused(&phase->skel, 1);
            phase->state = SIDESCRL_ST_PAUSED;
        }
        return sidescrl_phase_render(phase);
    }
    if (phase->state == SIDESCRL_ST_PAUSED)
        return sidescrl_phase_render(phase);

    runway_skel_tick_cooldown(&phase->skel);

    phase->vel_x = 0;
    if (held & 0x8000u) {
        phase->facing = 1;
        phase->vel_x = (int32_t)WALK_STEP;
    } else if (held & 0x4000u) {
        phase->facing = -1;
        phase->vel_x = -(int32_t)WALK_STEP;
    }

    if ((pad->pressed & 0x0100u) && !phase->airborne) {
        int hix = scrl_map_get_hixel(&phase->map,
                                     (int)(phase->bones_x / UNITS_PER_PIXEL),
                                     (int)(phase->bones_y / UNITS_PER_PIXEL) - 8);
        if (hix != SCRL_HIXEL_WALL) {
            phase->vel_y = JUMP_IMPULSE;
            phase->airborne = 1;
            phase->state = SIDESCRL_ST_AIR;
            phase->sfx_flags |= RUNWAY_SFX_FLAG_JUMP;
        }
    }

    phase->ground_y = floor_units_at(phase, phase->bones_x);

    if (phase->airborne) {
        phase->vel_y += GRAVITY_Y;
        {
            int64_t ny = (int64_t)phase->bones_y + (int64_t)phase->vel_y;
            if (ny < 0) {
                ny = 0;
                phase->vel_y = 0;
            }
            if (phase->vel_y >= 0 && (uint32_t)ny >= phase->ground_y) {
                phase->bones_y = phase->ground_y;
                phase->vel_y = 0;
                phase->airborne = 0;
                phase->sfx_flags |= RUNWAY_SFX_FLAG_LAND;
                phase->state = phase->vel_x ? SIDESCRL_ST_WALK : SIDESCRL_ST_STAND;
            } else {
                phase->bones_y = (uint32_t)ny;
                phase->state = SIDESCRL_ST_AIR;
            }
        }
    } else {
        phase->bones_y = phase->ground_y;
        if (phase->vel_x)
            phase->state = SIDESCRL_ST_WALK;
        else
            phase->state = SIDESCRL_ST_STAND;
    }

    if (phase->vel_x) {
        int64_t nx = (int64_t)phase->bones_x + (int64_t)phase->vel_x;
        unsigned px;
        if (nx < 4 * (int64_t)UNITS_PER_PIXEL)
            nx = 4 * (int64_t)UNITS_PER_PIXEL;
        px = (unsigned)(nx / UNITS_PER_PIXEL);
        {
            unsigned max_px = (phase->scroll_w > SCR_W)
                                  ? (phase->scroll_w - 8u)
                                  : (SCR_W - 4u);
            if (px >= max_px)
                nx = (int64_t)max_px * UNITS_PER_PIXEL;
        }
        /* §23 SCRLCOLL wall: block step into solid column. */
        if (!move_blocked_wall(phase, (uint32_t)nx)) {
            if (!phase->airborne && phase->state != SIDESCRL_ST_WALK)
                phase->sfx_flags |= RUNWAY_SFX_FLAG_WALK;
            phase->bones_x = (uint32_t)nx;
        } else {
            phase->vel_x = 0;
            if (!phase->airborne)
                phase->state = SIDESCRL_ST_STAND;
        }
    }

    {
        int32_t dx = (int32_t)(phase->bones_x - phase->camera_x) / 8;
        phase->camera_x += (uint32_t)dx;
    }

    /* Redraw strip with camera parallax (DrawMap/CheckScroll stand-in). */
    if (phase->scroll_rgba && phase->map.plane_count > 0) {
        scrl_map_draw(&phase->map, phase->palette, phase->scroll_rgba,
                      phase->scroll_w, phase->scroll_h,
                      phase->camera_x / UNITS_PER_PIXEL);
    }

    if (phase->anim_count > 0) {
        if (phase->state == SIDESCRL_ST_WALK) {
            if ((phase->frames & 3u) == 0)
                phase->anim_index = (phase->anim_index + 1) % phase->anim_count;
            if (phase->anim_index == 0)
                phase->sfx_flags |= RUNWAY_SFX_FLAG_WALK;
        }
        phase->sprite_rgba = phase->anim_rgba[phase->anim_index];
        phase->sprite_w = phase->anim_w[phase->anim_index];
        phase->sprite_h = phase->anim_h[phase->anim_index];
    }

    /* §24: poofs / sparkles contact → skel hit. */
    if (stage_enemies_step(&phase->enemies, &phase->skel, phase->bones_x, 0,
                           1)) {
        phase->hazard_hits += phase->enemies.hits_applied;
        phase->state = SIDESCRL_ST_LOST;
        snprintf(phase->status, sizeof phase->status,
                 "%s lost (poof/skel) hits=%u enemies=%u", phase->stage_name,
                 phase->hazard_hits, phase->enemies.count);
        return sidescrl_phase_render(phase);
    }
    phase->hazard_hits = phase->enemies.hits_applied;

    if (phase->bones_x >= phase->finish_x) {
        phase->state = SIDESCRL_ST_FINISHED;
        snprintf(phase->status, sizeof phase->status,
                 "%s finished (EndLevel win) skel=%d seg=%d hits=%u layers=%u "
                 "planes=%u amap=%u enemies=%u",
                 phase->stage_name, phase->skel.skeletism, phase->skel.segments,
                 phase->hazard_hits, phase->layer_count, phase->map.plane_count,
                 phase->map.amap_n, phase->enemies.count);
    } else {
        const char *st =
            phase->state == SIDESCRL_ST_AIR ? "air" :
            phase->state == SIDESCRL_ST_WALK ? "walk" : "stand";
        snprintf(phase->status, sizeof phase->status,
                 "%s %s x=%u y=%u skel=%d/%d seg=%d layers=%u planes=%u "
                 "enemies=%u",
                 phase->stage_name, st, phase->bones_x / UNITS_PER_PIXEL,
                 phase->bones_y / UNITS_PER_PIXEL, phase->skel.skeletism,
                 RUNWAY_SKEL_MAX, phase->skel.segments, phase->layer_count,
                 phase->map.plane_count, phase->enemies.count);
    }
    return sidescrl_phase_render(phase);
}

int sidescrl_phase_render(sidescrl_phase *phase) {
    unsigned cam_px, src_x;
    int dx, dy;
    char hud_txt[64];
    if (!phase || !phase->frame_rgba || !phase->scroll_rgba || !phase->sprite_rgba)
        return -1;
    cam_px = phase->camera_x / UNITS_PER_PIXEL;
    if (phase->scroll_w > SCR_W) {
        int target = (int)cam_px - (int)(SCR_W / 3);
        if (target < 0) target = 0;
        if ((unsigned)target > phase->scroll_w - SCR_W)
            target = (int)(phase->scroll_w - SCR_W);
        src_x = (unsigned)target;
    } else {
        src_x = 0;
    }
    {
        unsigned y, x;
        for (y = 0; y < SCR_H; y++) {
            for (x = 0; x < SCR_W; x++) {
                size_t si = ((size_t)y * phase->scroll_w + src_x + x) * 4u;
                size_t di = ((size_t)y * SCR_W + x) * 4u;
                phase->frame_rgba[di + 0] = phase->scroll_rgba[si + 0];
                phase->frame_rgba[di + 1] = phase->scroll_rgba[si + 1];
                phase->frame_rgba[di + 2] = phase->scroll_rgba[si + 2];
                phase->frame_rgba[di + 3] = 255;
            }
        }
    }
    stage_enemies_render(&phase->enemies, NULL, phase->frame_rgba, SCR_W, SCR_H,
                         src_x, 0, (int)(phase->ground_y / UNITS_PER_PIXEL));
    dx = (int)(phase->bones_x / UNITS_PER_PIXEL) - (int)src_x;
    if (dx < 12) dx = 12;
    if (dx > (int)SCR_W - 12) dx = (int)SCR_W - 12;
    dy = (int)(phase->bones_y / UNITS_PER_PIXEL);
    stage_enemies_draw_bones(phase->frame_rgba, SCR_W, SCR_H, dx, dy,
                             phase->facing, 0,
                             phase->state == SIDESCRL_ST_AIR,
                             (phase->state == SIDESCRL_ST_WALK)
                                 ? (phase->anim_index + 1u)
                                 : 0u);
    runway_compose_hud_skelbar(phase->frame_rgba, SCR_W, SCR_H,
                               phase->skel.segments, RUNWAY_SKEL_SEGMENTS, 8, 8);
    if (phase->stage_name[0]) {
        snprintf(hud_txt, sizeof hud_txt, "%s", phase->stage_name);
        mb_font_draw_text_shadow(phase->frame_rgba, SCR_W, SCR_H, 200, 8,
                                 hud_txt, 245, 225, 130, 16, 12, 8, 1);
    }
    runway_compose_hud_pause(phase->frame_rgba, SCR_W, SCR_H, NULL, 0, 0,
                             phase->state == SIDESCRL_ST_PAUSED);
    return 0;
}

int sidescrl_phase_done(const sidescrl_phase *phase) {
    return phase && (phase->state == SIDESCRL_ST_FINISHED ||
                     phase->state == SIDESCRL_ST_ABORTED ||
                     phase->state == SIDESCRL_ST_LOST);
}
