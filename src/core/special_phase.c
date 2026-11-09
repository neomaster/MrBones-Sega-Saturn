#include "special_phase.h"
#include "core/duck_avi.h"
#include "core/mb_font.h"
#include "core/pan_palette.h"
#include "core/resource.h"
#include "core/rhythm_table.h"
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

/*
 * FINALE CRing literals from /27FINALE/03CODE (base 0x00232000).
 * Direction table @0x002b21e0; phase increments @0x002b2158; ShootRing
 * adds 0x100 before subtracting object+0x704 (docs/finale-ring-analysis.md).
 */
static const int k_finale_dir[4] = {0, 128, 192, 64};
static const int k_finale_phase_incr[16] = {
    0, 0, 204, 153, 102, 51, 0, 204, 153, 102, 51, 0, 153, 51, 102, 153};

static void finale_ring_init(special_phase *phase) {
    unsigned i;
    for (i = 0; i < FINALE_RING_SLOTS; i++)
        phase->ring_active[i] = 1; /* host: start lit; clear-all → EndLevel(0) */
    phase->ring_active_count = (int)FINALE_RING_SLOTS;
    phase->ring_phase = 0;
    phase->ring_phase_acc = 0;
    phase->ring_phase_mode = 2; /* first non-zero incr in the table */
    phase->ring_cleared = 0;
    phase->ring_shots = 0;
    phase->ring_last_index = -1;
    phase->ring_last_activated = 0;
}

/* Brain_CRing: advance phase accumulator and publish high-16 to +0x704. */
static void finale_ring_brain(special_phase *phase) {
    int incr;
    unsigned mode;
    if (!phase || phase->ring_cleared) return;
    mode = (unsigned)phase->ring_phase_mode & 15u;
    incr = k_finale_phase_incr[mode];
    phase->ring_phase_acc += incr;
    if (phase->ring_phase_acc >= FINALE_RING_PHASE_WRAP)
        phase->ring_phase_acc -= FINALE_RING_PHASE_WRAP;
    if (phase->ring_phase_acc < 0)
        phase->ring_phase_acc += FINALE_RING_PHASE_WRAP;
    /* ShootRing subtracts the published phase (high 16 of accumulator). */
    phase->ring_phase = (int)(int16_t)((unsigned)phase->ring_phase_acc >> 16);
}

static int finale_ring_select(int phase_field, unsigned dir) {
    int idx;
    dir &= 3u;
    idx = ((k_finale_dir[dir] + 0x100 - phase_field) + 8) >> 4;
    while (idx > 15)
        idx -= 16;
    while (idx < 0)
        idx += 16;
    return idx;
}

int special_phase_finale_shoot(special_phase *phase, unsigned dir) {
    int idx;
    if (!phase || phase->kind != STAGE_KIND_FINALE || phase->ring_cleared)
        return 0;
    idx = finale_ring_select(phase->ring_phase, dir);
    phase->ring_shots++;
    phase->ring_last_index = idx;
    if (!phase->ring_active[idx]) {
        /* inactive → activate, increment +0x708 */
        phase->ring_active[idx] = 1;
        phase->ring_active_count++;
        phase->ring_last_activated = 1;
        phase->sfx_flags |= RUNWAY_SFX_FLAG_C;
        return 0;
    }
    /* active → deactivate, decrement; count==0 → EndLevel(0) */
    phase->ring_active[idx] = 0;
    phase->ring_active_count--;
    phase->ring_last_activated = 0;
    phase->sfx_flags |= RUNWAY_SFX_FLAG_JUMP | RUNWAY_SFX_FLAG_C;
    if (phase->ring_active_count < 0)
        phase->ring_active_count = 0;
    if (phase->ring_active_count == 0) {
        phase->ring_cleared = 1;
        return 1;
    }
    return 0;
}

static unsigned finale_dir_from_pad(const saturn_pad *pad) {
    uint16_t h = pad ? pad->held : 0;
    if (h & 0x1000u) return 1u; /* UP → 128 */
    if (h & 0x4000u) return 2u; /* LEFT → 192 */
    if (h & 0x2000u) return 3u; /* DOWN → 64 */
    return 0u;                  /* RIGHT / default → 0 */
}

static void paint_finale_rings(const special_phase *phase, uint8_t *dst) {
    /* 16 slots on a circle; integer offsets avoid libm. */
    static const int8_t k_ox[16] = {0, 35, 64, 83, 90, 83, 64, 35,
                                    0, -35, -64, -83, -90, -83, -64, -35};
    static const int8_t k_oy[16] = {-90, -83, -64, -35, 0, 35, 64, 83,
                                    90, 83, 64, 35, 0, -35, -64, -83};
    unsigned i;
    int cx = (int)SCR_W / 2, cy = 100;
    for (i = 0; i < FINALE_RING_SLOTS; i++) {
        int x = cx + k_ox[i];
        int y = cy + k_oy[i];
        int dy, dx;
        int lit = phase->ring_active[i];
        int flash = (phase->ring_last_index == (int)i && (phase->frames & 4u) == 0);
        for (dy = -3; dy <= 3; dy++) {
            for (dx = -3; dx <= 3; dx++) {
                int px = x + dx, py = y + dy;
                uint8_t *p;
                if (px < 0 || py < 0 || px >= (int)SCR_W || py >= (int)SCR_H)
                    continue;
                if (dx * dx + dy * dy > 10) continue;
                p = dst + ((size_t)py * SCR_W + (size_t)px) * 4u;
                if (lit) {
                    p[0] = 255;
                    p[1] = flash ? 255 : 200;
                    p[2] = flash ? 80 : 40;
                } else {
                    p[0] = 40;
                    p[1] = 50;
                    p[2] = 70;
                }
                p[3] = 255;
            }
        }
    }
}

static void build_palette(const uint8_t *code, size_t code_size, uint8_t *palette) {
    uint32_t words[1024], tmp[1024];
    size_t off, consumed, best = 0, i;
    memset(words, 0, sizeof words);
    if (code && code_size > 2048) {
        for (off = 0; off + 2048 <= code_size && off < 0x80000; off += 4) {
            if (code[off] != 0 || code[off + 1] != 0 || code[off + 2] >= 4 || code[off + 4] != 1)
                continue;
            consumed = 0;
            memset(tmp, 0, sizeof tmp);
            if (pan_palette_apply(code + off, code_size - off, tmp, 1024, &consumed))
                continue;
            if (consumed >= 2048 && consumed > best) {
                best = consumed;
                memcpy(words, tmp, sizeof words);
                if (best >= 4100) break;
            }
        }
    }
    if (best < 64 && code && code_size > 16) {
        for (off = 0; off + 16 < code_size && off < 0x80000; off += 4) {
            consumed = 0;
            memset(tmp, 0, sizeof tmp);
            if (pan_palette_apply(code + off, code_size - off, tmp, 1024, &consumed))
                continue;
            if (consumed > best && consumed >= 64) {
                best = consumed;
                memcpy(words, tmp, sizeof words);
                if (best >= 200) break;
            }
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

static void stage_name_from_iso(char *out, size_t n, const char *iso_dir) {
    const char *p = strrchr(iso_dir, '/');
    p = p ? p + 1 : iso_dir;
    if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9')
        p += 2;
    snprintf(out, n, "%s", p);
}

static special_mode mode_from_kind(stage_kind kind) {
    switch (kind) {
    case STAGE_KIND_RHYTHM: return SPECIAL_MODE_RHYTHM;
    case STAGE_KIND_BIGBONES:
    case STAGE_KIND_GENROOM: return SPECIAL_MODE_ACTION;
    case STAGE_KIND_FINALE:
    case STAGE_KIND_LOSE:
    case STAGE_KIND_MOVIE:
    case STAGE_KIND_INTRO: return SPECIAL_MODE_PRESENT;
    default: return SPECIAL_MODE_PRESENT;
    }
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

static void fill_bg(uint8_t *bg, uint8_t r, uint8_t g, uint8_t b) {
    size_t i, n = (size_t)SCR_W * SCR_H * 4u;
    for (i = 0; i < n; i += 4) {
        bg[i + 0] = r;
        bg[i + 1] = g;
        bg[i + 2] = b;
        bg[i + 3] = 255;
    }
}

static void paint_beat_lane(const special_phase *phase, uint8_t *dst,
                            int in_window, unsigned hits) {
    static const char *const k_btn_lbl[4] = {"A", "B", "C", "R"};
    static const uint8_t k_btn_col[4][3] = {
        {255,  90,  90}, /* A: red */
        {255, 220,  60}, /* B: gold */
        { 80, 190, 255}, /* C: cyan */
        {120, 245, 130}  /* RIGHT: green */
    };
    int x, y, i;
    uint8_t *px;
    char hud[64];

    /* Scrolling note highway track (y = 182..214, x = 32..288). */
    for (y = 182; y < 214; y++) {
        int border = (y == 182 || y == 213);
        for (x = 32; x < 288; x++) {
            px = dst + ((size_t)y * SCR_W + (size_t)x) * 4u;
            if (border) {
                px[0] = 180; px[1] = 150; px[2] = 90; px[3] = 255;
            } else {
                px[0] = (uint8_t)((px[0] + 16u) / 3u);
                px[1] = (uint8_t)((px[1] + 20u) / 3u);
                px[2] = (uint8_t)((px[2] + 36u) / 3u);
                px[3] = 255;
            }
        }
    }
    /* Hit zone box at x = 44..72. */
    for (y = 184; y < 212; y++) {
        for (x = 44; x < 72; x++) {
            px = dst + ((size_t)y * SCR_W + (size_t)x) * 4u;
            px[0] = in_window ? 60 : 35;
            px[1] = in_window ? 210 : 50;
            px[2] = in_window ? 80 : 65;
            px[3] = 255;
        }
    }
    if (phase) {
        unsigned ek = phase->expected_kind & 3u;
        mb_font_draw_text_shadow(dst, SCR_W, SCR_H, 54, 194,
                                 k_btn_lbl[ek],
                                 k_btn_col[ek][0], k_btn_col[ek][1], k_btn_col[ek][2],
                                 10, 10, 18, 1);
    }

    /* Upcoming notes from rhythm_table scrolling right -> left toward x=56. */
    if (phase && phase->rhythm && phase->rhythm->notes) {
        const rhythm_table *rt = phase->rhythm;
        unsigned k;
        for (k = 0; k < rt->count; k++) {
            unsigned ht = rhythm_note_host_frame(rt, k);
            int dt = (int)ht - (int)phase->frames;
            int nx;
            unsigned nk = rt->notes[k].kind & 3u;
            if (dt < -4 || dt > 100) continue;
            nx = 56 + dt * 2;
            if (nx < 40 || nx > 276) continue;
            for (y = 188; y < 206; y++) {
                for (x = nx - 6; x < nx + 6; x++) {
                    if (x < 34 || x >= 286) continue;
                    px = dst + ((size_t)y * SCR_W + (size_t)x) * 4u;
                    px[0] = k_btn_col[nk][0];
                    px[1] = k_btn_col[nk][1];
                    px[2] = k_btn_col[nk][2];
                    px[3] = 255;
                }
            }
            mb_font_draw_text_shadow(dst, SCR_W, SCR_H, nx - 3, 193,
                                     k_btn_lbl[nk], 255, 255, 255, 12, 12, 16, 1);
        }
    }

    /* Hit pips. */
    for (i = 0; i < (int)SPECIAL_HITS_TO_WIN; i++) {
        int filled = i < (int)hits;
        int ox = 48 + i * 28;
        for (y = 162; y < 176; y++)
            for (x = ox; x < ox + 16; x++) {
                px = dst + ((size_t)y * SCR_W + (size_t)x) * 4u;
                px[0] = filled ? 235 : 40;
                px[1] = filled ? 195 : 40;
                px[2] = filled ? 55 : 48;
                px[3] = 255;
            }
    }
    if (phase && phase->stage_name[0]) {
        snprintf(hud, sizeof hud, "%s SOLO  HITS %u/%u",
                 phase->stage_name, hits, (unsigned)SPECIAL_HITS_TO_WIN);
        mb_font_draw_text_centered(dst, SCR_W, SCR_H, 10, hud,
                                   255, 225, 90, 16, 12, 8, 1);
    }
}

int special_phase_load(special_phase *phase, cdimage *cd, const char *iso_dir,
                       stage_kind kind) {
    uint8_t palette[4096];
    size_t off;
    char path[96];
    unsigned guard;
    if (!phase || !cd || !iso_dir) return -1;
    memset(phase, 0, sizeof *phase);
    snprintf(phase->iso_dir, sizeof phase->iso_dir, "%s", iso_dir);
    stage_name_from_iso(phase->stage_name, sizeof phase->stage_name, iso_dir);
    phase->kind = kind;
    phase->mode = mode_from_kind(kind);
    phase->state = SPECIAL_ST_PLAY;
    phase->facing = 1;
    phase->bones_x = START_X;
    phase->bones_y = GROUND_Y;
    phase->finish_x = FINISH_X;
    phase->pages_needed = SPECIAL_PRESENT_PAGES;
    phase->beat_period = SPECIAL_BEAT_PERIOD;
    phase->rhythm = rhythm_table_for_stage(phase->stage_name);
    if (phase->rhythm) {
        phase->beat_period = phase->rhythm->period;
    } else if (strcmp(phase->stage_name, "DRUM") == 0) {
        phase->beat_period = 12;
    } else if (strcmp(phase->stage_name, "GUITAR") == 0) {
        phase->beat_period = 16;
    }
    runway_skel_init(&phase->skel);
    /* §24: GENRM reds/blues/zap; BIGBNS lord/gang; FINALE ring/bolt boss fx. */
    stage_enemies_init_special(&phase->enemies, phase->stage_name,
                               UNITS_PER_PIXEL);
    stage_enemies_load_disc_sprites(cd);
    if (kind == STAGE_KIND_FINALE)
        finale_ring_init(phase);

    /* LOSE / movie: AVI(+DUCK) required; CODE optional. */
    if (kind == STAGE_KIND_LOSE || kind == STAGE_KIND_MOVIE ||
        kind == STAGE_KIND_INTRO) {
        (void)load_opt(cd, iso_dir, "20AVI", &phase->avi, &phase->avi_size);
        (void)load_opt(cd, iso_dir, "19DUCK", &phase->duck, &phase->duck_size);
        if (phase->avi_size == 0) {
            snprintf(phase->status, sizeof phase->status, "missing %s/20AVI", iso_dir);
            goto fail;
        }
        phase->mode = SPECIAL_MODE_PRESENT;
        phase->audio_track = 4;
        phase->pages_needed = 4;
    } else {
        snprintf(path, sizeof path, "%s/03CODE", iso_dir);
        if (resource_load(cd, path, &phase->code, &phase->code_size) ||
            phase->code_size <= 20000)
            goto fail;
        (void)load_opt(cd, iso_dir, "04STXT", &phase->textures, &phase->textures_size);
        (void)load_opt(cd, iso_dir, "02TITL", &phase->title, &phase->title_size);
        (void)load_opt(cd, iso_dir, "05SND1", &phase->sound, &phase->sound_size);
        (void)load_opt(cd, iso_dir, "15PAN", &phase->pan, &phase->pan_size);
        /* These AVIs are transitions, not their playable scenes. */
        if (strcmp(phase->stage_name, "GUITAR") != 0 &&
            strcmp(phase->stage_name, "DRUM") != 0 &&
            strcmp(phase->stage_name, "BIGBNS") != 0 &&
            strcmp(phase->stage_name, "GENRM") != 0) {
            (void)load_opt(cd, iso_dir, "19DUCK", &phase->duck, &phase->duck_size);
            (void)load_opt(cd, iso_dir, "20AVI", &phase->avi, &phase->avi_size);
        }
        if (phase->mode == SPECIAL_MODE_RHYTHM)
            phase->audio_track = 5;
        else if (phase->mode == SPECIAL_MODE_ACTION)
            phase->audio_track = 6;
        else
            phase->audio_track = 7;
        if (phase->textures_size == 0 && phase->avi_size == 0) goto fail;
    }

    /* §20/§22: open DUCK AVI with 19DUCK tables when present. */
    if (phase->avi_size > 0 &&
        duck_avi_open_with_duck(&phase->movie, phase->avi, phase->avi_size,
                                phase->duck, phase->duck_size) == 0) {
        phase->movie_ok = 1;
        if (phase->mode == SPECIAL_MODE_PRESENT) {
            unsigned need = phase->movie.frame_count;
            if (need < 4) need = 4;
            phase->pages_needed = need;
        }
        if (!duck_avi_step(&phase->movie)) goto fail;
        if (phase->movie.pcm_samples > 0)
            phase->movie_pcm_pending = 1;
    }

    if (phase->mode == SPECIAL_MODE_PRESENT && !phase->movie_ok) {
        if (!phase->avi_size)
            snprintf(phase->status, sizeof phase->status, "missing %s/20AVI", iso_dir);
        goto fail;
    }

    phase->bg_rgba = malloc((size_t)SCR_W * SCR_H * 4u);
    phase->frame_rgba = malloc((size_t)SCR_W * SCR_H * 4u);
    if (!phase->bg_rgba || !phase->frame_rgba) goto fail;

    if (phase->mode == SPECIAL_MODE_RHYTHM)
        fill_bg(phase->bg_rgba, 16, 8, 32);
    else if (phase->mode == SPECIAL_MODE_ACTION)
        fill_bg(phase->bg_rgba, 24, 40, 24);
    else if (kind == STAGE_KIND_LOSE)
        fill_bg(phase->bg_rgba, 48, 8, 8);
    else
        fill_bg(phase->bg_rgba, 8, 16, 40);

    build_palette((const uint8_t *)phase->code, phase->code_size, palette);
    off = 0;
    guard = 0;
    while (phase->gfx_count < SPECIAL_GFX_MAX && guard++ < 128 &&
           phase->textures_size > 0) {
        uint8_t *px = NULL;
        unsigned w = 0, h = 0;
        size_t before = off;
        if (decode_stxtr(phase->textures, phase->textures_size, &off, palette,
                         &px, &w, &h)) {
            if (off == before) break;
            continue;
        }
        if (w >= 8 && h >= 8 && w <= SCR_W && h <= SCR_H) {
            phase->gfx_rgba[phase->gfx_count] = px;
            phase->gfx_w[phase->gfx_count] = w;
            phase->gfx_h[phase->gfx_count] = h;
            phase->gfx_count++;
        } else {
            free(px);
        }
    }

    /* Backdrop plate: largest gfx centered. */
    if (phase->gfx_count > 0) {
        unsigned best = 0, i, area, best_area = 0;
        for (i = 0; i < phase->gfx_count; i++) {
            area = phase->gfx_w[i] * phase->gfx_h[i];
            if (area > best_area) {
                best_area = area;
                best = i;
            }
        }
        runway_compose_blit_sprite(
            phase->bg_rgba, SCR_W, SCR_H, phase->gfx_rgba[best],
            phase->gfx_w[best], phase->gfx_h[best],
            ((int)SCR_W - (int)phase->gfx_w[best]) / 2,
            ((int)SCR_H - (int)phase->gfx_h[best]) / 2);
        phase->gfx_index = best;
    }

    snprintf(phase->status, sizeof phase->status,
             "%s special mode=%d gfx=%u avi=%zu snd=%zu",
             phase->stage_name, (int)phase->mode, phase->gfx_count,
             phase->avi_size, phase->sound_size);
    return special_phase_render(phase);

fail:
    {
        char error[sizeof phase->status];
        snprintf(error, sizeof error, "%s", phase->movie.decode_error || !phase->movie_ok
                 ? (phase->movie.status[0] ? phase->movie.status : phase->status)
                 : "special phase allocation failed");
        special_phase_free(phase);
        snprintf(phase->status, sizeof phase->status, "%s", error);
    }
    return -1;
}

void special_phase_free(special_phase *phase) {
    unsigned i;
    if (!phase) return;
    for (i = 0; i < phase->gfx_count && i < SPECIAL_GFX_MAX; i++)
        free(phase->gfx_rgba[i]);
    duck_avi_free(&phase->movie);
    free(phase->title);
    free(phase->textures);
    free(phase->code);
    free(phase->sound);
    free(phase->duck);
    free(phase->avi);
    free(phase->pan);
    free(phase->bg_rgba);
    free(phase->frame_rgba);
    memset(phase, 0, sizeof *phase);
}

static void rhythm_advance_note(special_phase *phase) {
    const rhythm_table *rt = phase->rhythm;
    unsigned host_t;
    if (!rt || !rt->notes || rt->count == 0) {
        phase->expected_kind = RHYTHM_KIND_A;
        return;
    }
    while (phase->note_cursor < rt->count) {
        host_t = rhythm_note_host_frame(rt, phase->note_cursor);
        if (host_t > phase->frames) break;
        phase->expected_kind = rt->notes[phase->note_cursor].kind & 3u;
        phase->note_cursor++;
    }
    if (phase->note_cursor >= rt->count)
        phase->expected_kind = rt->notes[rt->count - 1u].kind & 3u;
}

static void step_rhythm(special_phase *phase, const saturn_pad *pad) {
    unsigned period = phase->beat_period ? phase->beat_period : SPECIAL_BEAT_PERIOD;
    unsigned window =
        (phase->rhythm && phase->rhythm->window) ? phase->rhythm->window
                                                 : SPECIAL_BEAT_WINDOW;
    unsigned hits_need =
        (phase->rhythm && phase->rhythm->hits_to_win) ? phase->rhythm->hits_to_win
                                                      : SPECIAL_HITS_TO_WIN;
    unsigned phase_in_beat = phase->frames % period;
    uint16_t buttons = (uint16_t)(pad->pressed | pad->held);
    int pressed_hit = 0;
    int matched = 0;

    rhythm_advance_note(phase);
    if (phase->movie_ok && duck_avi_tick(&phase->movie,
        phase->movie.tick_external ? phase->movie.tick_us : 16000) < 0) {
        phase->movie_pcm_pending = 0;
        phase->state = SPECIAL_ST_ABORTED;
        snprintf(phase->status, sizeof phase->status, "%s", phase->movie.status);
        return;
    }
    phase->in_window = phase_in_beat < window;
    if (phase_in_beat == 0) {
        phase->beat_index++;
        phase->hit_this_beat = 0;
        phase->sfx_flags |= RUNWAY_SFX_FLAG_LAND; /* metronome tick */
    }

    /* §24: match DrumSequence/guitar kind (A/B/C/RIGHT); RIGHT also universal. */
    if (pad->pressed & (0x0400u | 0x0100u | 0x0200u | 0x8000u)) {
        pressed_hit = 1;
        matched = rhythm_buttons_match(phase->expected_kind, buttons);
    }
    if ((pad->held & 0x8000u) && phase->in_window && !phase->hit_this_beat &&
        phase_in_beat == 1) {
        pressed_hit = 1;
        matched = 1; /* --check RIGHT strum */
    }
    if (pressed_hit) {
        if (phase->in_window && !phase->hit_this_beat && matched) {
            static const unsigned k_lick_sfx[4] = {
                RUNWAY_SFX_FLAG_C,
                RUNWAY_SFX_FLAG_JUMP | RUNWAY_SFX_FLAG_C,
                RUNWAY_SFX_FLAG_WALK | RUNWAY_SFX_FLAG_C,
                RUNWAY_SFX_FLAG_LAND | RUNWAY_SFX_FLAG_C
            };
            phase->hits++;
            phase->hit_this_beat = 1;
            phase->sfx_flags |= k_lick_sfx[phase->expected_kind & 3u];
        } else if (!phase->in_window || !matched) {
            phase->misses++;
            (void)runway_skel_hit(&phase->skel, RUNWAY_DAMAGE_DEFAULT / 2);
            phase->sfx_flags |= RUNWAY_SFX_FLAG_TALL;
        }
    }
    if (phase->hits >= hits_need) {
        phase->state = SPECIAL_ST_FINISHED;
        snprintf(phase->status, sizeof phase->status,
                 "%s finished (rhythm win) hits=%u misses=%u beats=%u notes=%u "
                 "src=%s",
                 phase->stage_name, phase->hits, phase->misses, phase->beat_index,
                 phase->rhythm ? phase->rhythm->count : 0u,
                 phase->rhythm && phase->rhythm->source ? phase->rhythm->source
                                                        : "soft");
        return;
    }
    if (phase->skel.died || phase->misses >= 12) {
        phase->state = SPECIAL_ST_LOST;
        snprintf(phase->status, sizeof phase->status,
                 "%s lost (rhythm) hits=%u misses=%u",
                 phase->stage_name, phase->hits, phase->misses);
        return;
    }
    snprintf(phase->status, sizeof phase->status,
             "%s rhythm beat=%u hits=%u/%u kind=%u win=%d miss=%u notes=%u",
             phase->stage_name, phase->beat_index, phase->hits, hits_need,
             phase->expected_kind, phase->in_window, phase->misses,
             phase->rhythm ? phase->rhythm->count : 0u);
}

static void step_action(special_phase *phase, const saturn_pad *pad) {
    uint32_t held = pad->held;
    phase->vel_x = 0;
    if (held & 0x8000u) {
        phase->facing = 1;
        phase->vel_x = (int32_t)WALK_STEP;
    } else if (held & 0x4000u) {
        phase->facing = -1;
        phase->vel_x = -(int32_t)WALK_STEP;
    }
    if ((pad->pressed & 0x0100u) && !phase->airborne) {
        phase->vel_y = JUMP_IMPULSE;
        phase->airborne = 1;
        phase->sfx_flags |= RUNWAY_SFX_FLAG_JUMP;
    }
    if (phase->airborne) {
        phase->vel_y += GRAVITY_Y;
        {
            int64_t ny = (int64_t)phase->bones_y + (int64_t)phase->vel_y;
            if (ny < 0) {
                ny = 0;
                phase->vel_y = 0;
            }
            if (phase->vel_y >= 0 && (uint32_t)ny >= GROUND_Y) {
                phase->bones_y = GROUND_Y;
                phase->vel_y = 0;
                phase->airborne = 0;
                phase->sfx_flags |= RUNWAY_SFX_FLAG_LAND;
            } else {
                phase->bones_y = (uint32_t)ny;
            }
        }
    } else {
        phase->bones_y = GROUND_Y;
    }
    if (phase->vel_x) {
        int64_t nx = (int64_t)phase->bones_x + (int64_t)phase->vel_x;
        unsigned px;
        if (nx < 4 * (int64_t)UNITS_PER_PIXEL)
            nx = 4 * (int64_t)UNITS_PER_PIXEL;
        px = (unsigned)(nx / UNITS_PER_PIXEL);
        if (px >= SCR_W - 4)
            nx = (int64_t)(SCR_W - 4) * UNITS_PER_PIXEL;
        phase->bones_x = (uint32_t)nx;
        phase->sfx_flags |= RUNWAY_SFX_FLAG_WALK;
    }
    if (phase->gfx_count > 1 && (phase->frames & 3u) == 0)
        phase->gfx_index = (phase->gfx_index + 1) % phase->gfx_count;
    if (phase->movie_ok && duck_avi_tick(&phase->movie,
        phase->movie.tick_external ? phase->movie.tick_us : 16000) < 0) {
        phase->movie_pcm_pending = 0;
        phase->state = SPECIAL_ST_ABORTED;
        snprintf(phase->status, sizeof phase->status, "%s", phase->movie.status);
        return;
    }

    /* §24: GENRM reds/blues/zap or BIGBNS lord/gang. */
    if (phase->enemies.count > 0 &&
        stage_enemies_step(&phase->enemies, &phase->skel, phase->bones_x, 0,
                           1)) {
        phase->hazard_hits = phase->enemies.hits_applied;
        phase->state = SPECIAL_ST_LOST;
        snprintf(phase->status, sizeof phase->status,
                 "%s lost (enemy) hits=%u reds=%u blues=%u zaps=%u",
                 phase->stage_name, phase->hazard_hits,
                 stage_enemies_count_kind(&phase->enemies, STAGE_ENEMY_RED),
                 stage_enemies_count_kind(&phase->enemies, STAGE_ENEMY_BLUE),
                 phase->enemies.zaps_fired);
        return;
    }
    phase->hazard_hits = phase->enemies.hits_applied;

    if (phase->bones_x >= phase->finish_x) {
        phase->state = SPECIAL_ST_FINISHED;
        snprintf(phase->status, sizeof phase->status,
                 "%s finished (action win) skel=%d hits=%u pan=%zu avi=%zu "
                 "enemies=%u zaps=%u",
                 phase->stage_name, phase->skel.skeletism, phase->hazard_hits,
                 phase->pan_size, phase->avi_size, phase->enemies.count,
                 phase->enemies.zaps_fired);
        return;
    }
    snprintf(phase->status, sizeof phase->status,
             "%s action x=%u skel=%d/%d pan=%zu",
             phase->stage_name, phase->bones_x / UNITS_PER_PIXEL,
             phase->skel.skeletism, RUNWAY_SKEL_MAX, phase->pan_size);
}

static void step_present(special_phase *phase, const saturn_pad *pad) {
    int advance = 0;
    if (phase->movie_ok) {
        advance = duck_avi_tick(&phase->movie,
            phase->movie.tick_external ? phase->movie.tick_us : 16000);
        if (advance < 0) {
            phase->movie_pcm_pending = 0;
            phase->state = SPECIAL_ST_ABORTED;
            snprintf(phase->status, sizeof phase->status, "%s", phase->movie.status);
            return;
        }
    } else {
        if (pad->pressed & (0x0800u | 0x0400u | 0x0100u | 0x0200u | 0x8000u))
            advance = 1;
        if ((pad->held & 0x8000u) && (phase->frames % 8u) == 0)
            advance = 1;
    }
    /* §24 FINALE: ring/bolt boss fx (RingDefinitionTable) — no skel damage in PRESENT. */
    if (phase->enemies.count > 0)
        (void)stage_enemies_step(&phase->enemies, &phase->skel, phase->bones_x,
                                 0, 0);

    /* FINALE arena: Brain_CRing + CBlueShot→ShootRing (docs/finale-ring-analysis.md). */
    if (phase->kind == STAGE_KIND_FINALE && !phase->ring_cleared) {
        finale_ring_brain(phase);
        /* A / B / C fire a blue shot; D-pad selects direction 0..3. */
        if (pad->pressed & (0x0400u | 0x0100u | 0x0200u)) {
            if (special_phase_finale_shoot(phase, finale_dir_from_pad(pad))) {
                /* EndLevel(0) → PlayDuckMovie(3): finish now; AVI may still be up. */
                if (phase->movie_ok)
                    phase->movie.playback_done = 1;
                phase->state = SPECIAL_ST_FINISHED;
                snprintf(phase->status, sizeof phase->status,
                         "%s finished (ring clear) shots=%u phase=%d "
                         "enemies_rings=%u",
                         phase->stage_name, phase->ring_shots, phase->ring_phase,
                         phase->enemies.rings_fired);
                return;
            }
        }
    }

    if (advance > 0) {
        phase->page += (unsigned)advance;
        if (!phase->movie_ok) phase->sfx_flags |= RUNWAY_SFX_FLAG_C;
        if (phase->gfx_count)
            phase->gfx_index = phase->page % phase->gfx_count;
    }
    if ((phase->movie_ok && duck_avi_playback_done(&phase->movie)) ||
        (!phase->movie_ok && phase->page >= phase->pages_needed)) {
        if (phase->kind == STAGE_KIND_LOSE) {
            phase->state = SPECIAL_ST_LOST;
            snprintf(phase->status, sizeof phase->status,
                     "%s finished (DUKMOVIE/LOSE) avi=%zu frames=%u duck=%zu",
                     phase->stage_name, phase->avi_size, phase->movie.frames_drawn,
                     phase->duck_size);
        } else {
            phase->state = SPECIAL_ST_FINISHED;
            snprintf(phase->status, sizeof phase->status,
                     "%s finished (present win) pages=%u avi_frames=%u gfx=%u "
                     "rings=%u bolts=%u ring_slots=%d/%u shots=%u",
                     phase->stage_name, phase->page, phase->movie.frames_drawn,
                     phase->gfx_count, phase->enemies.rings_fired,
                     phase->enemies.bolts_fired, phase->ring_active_count,
                     FINALE_RING_SLOTS, phase->ring_shots);
        }
        return;
    }
    snprintf(phase->status, sizeof phase->status,
             "%s present %u/%u duck=%s avi=%zu gfx=%u rings=%u bolts=%u "
             "arena=%d/%u shot=%d",
             phase->stage_name, phase->page, phase->pages_needed,
             phase->movie_ok ? phase->movie.status : "off", phase->avi_size,
             phase->gfx_count, phase->enemies.rings_fired,
             phase->enemies.bolts_fired, phase->ring_active_count,
             FINALE_RING_SLOTS, phase->ring_last_index);
}

int special_phase_step(special_phase *phase, const saturn_pad *pad) {
    if (!phase || !pad || special_phase_done(phase)) return 0;
    phase->frames++;
    phase->sfx_flags = 0;

    if (pad->pressed & 0x0800u) {
        if (phase->mode != SPECIAL_MODE_PRESENT) {
            if (phase->state == SPECIAL_ST_PAUSED) {
                runway_skel_set_paused(&phase->skel, 0);
                phase->state = phase->pre_pause_state;
            } else {
                phase->pre_pause_state = phase->state;
                runway_skel_set_paused(&phase->skel, 1);
                phase->state = SPECIAL_ST_PAUSED;
            }
            return special_phase_render(phase);
        }
    }
    if (phase->state == SPECIAL_ST_PAUSED)
        return special_phase_render(phase);

    runway_skel_tick_cooldown(&phase->skel);

    if (phase->kind == STAGE_KIND_MOVIE && phase->movie_ok && (pad->pressed & 0x0800u))
        phase->movie.playback_done = 1;

    if (phase->mode == SPECIAL_MODE_RHYTHM)
        step_rhythm(phase, pad);
    else if (phase->mode == SPECIAL_MODE_ACTION)
        step_action(phase, pad);
    else
        step_present(phase, pad);

    return special_phase_render(phase);
}

int special_phase_render(special_phase *phase) {
    if (!phase || !phase->frame_rgba || !phase->bg_rgba) return -1;
    memcpy(phase->frame_rgba, phase->bg_rgba, (size_t)SCR_W * SCR_H * 4u);

    if (phase->mode == SPECIAL_MODE_RHYTHM) {
        if (phase->movie_ok && duck_avi_frame_rgba(&phase->movie)) {
            memcpy(phase->frame_rgba, duck_avi_frame_rgba(&phase->movie),
                    (size_t)SCR_W * SCR_H * 4u);
        }
        stage_enemies_draw_bones(phase->frame_rgba, SCR_W, SCR_H,
                                 (int)(SCR_W / 2), 92, 1, 0, 0,
                                 phase->frames >> 2);
        paint_beat_lane(phase, phase->frame_rgba, phase->in_window, phase->hits);
    } else if (phase->mode == SPECIAL_MODE_ACTION) {
        int dx, dy;
        if (phase->movie_ok && duck_avi_frame_rgba(&phase->movie)) {
            memcpy(phase->frame_rgba, duck_avi_frame_rgba(&phase->movie),
                    (size_t)SCR_W * SCR_H * 4u);
        }
        stage_enemies_render(&phase->enemies, NULL, phase->frame_rgba,
                             SCR_W, SCR_H, 0, 0,
                             (int)(GROUND_Y / UNITS_PER_PIXEL));
        dx = (int)(phase->bones_x / UNITS_PER_PIXEL);
        dy = (int)(phase->bones_y / UNITS_PER_PIXEL);
        stage_enemies_draw_bones(phase->frame_rgba, SCR_W, SCR_H, dx, dy,
                                 phase->facing, 0,
                                 phase->airborne,
                                 phase->frames >> 2);
        runway_compose_hud_skelbar(phase->frame_rgba, SCR_W, SCR_H,
                                   phase->skel.segments, RUNWAY_SKEL_SEGMENTS,
                                   8, 8);
        if (phase->stage_name[0])
            mb_font_draw_text_shadow(phase->frame_rgba, SCR_W, SCR_H, 200, 8,
                                     phase->stage_name,
                                     245, 225, 130, 16, 12, 8, 1);
    } else {
        /* Present: DUCK AVI frame, else STXT plates. */
        if (phase->movie_ok && duck_avi_frame_rgba(&phase->movie)) {
            memcpy(phase->frame_rgba, duck_avi_frame_rgba(&phase->movie),
                   (size_t)SCR_W * SCR_H * 4u);
        } else if (phase->gfx_count) {
            unsigned i = phase->gfx_index % phase->gfx_count;
            runway_compose_blit_sprite(
                phase->frame_rgba, SCR_W, SCR_H, phase->gfx_rgba[i],
                phase->gfx_w[i], phase->gfx_h[i],
                ((int)SCR_W - (int)phase->gfx_w[i]) / 2,
                ((int)SCR_H - (int)phase->gfx_h[i]) / 2);
        }
        if (phase->kind == STAGE_KIND_FINALE && !phase->movie_ok) {
            /* Ring HUD only without AVI — Ymir frame hashes need a clean movie blit. */
            char ring_hud[48];
            paint_finale_rings(phase, phase->frame_rgba);
            if (phase->enemies.count > 0)
                stage_enemies_render(&phase->enemies, NULL, phase->frame_rgba,
                                     SCR_W, SCR_H, 0, 0, 180);
            runway_compose_hud_skelbar(phase->frame_rgba, SCR_W, SCR_H,
                                       phase->skel.segments, RUNWAY_SKEL_SEGMENTS,
                                       8, 8);
            snprintf(ring_hud, sizeof ring_hud, "RING %d/%u",
                     phase->ring_active_count, FINALE_RING_SLOTS);
            mb_font_draw_text_shadow(phase->frame_rgba, SCR_W, SCR_H, 168, 8,
                                     ring_hud, 255, 210, 80, 20, 10, 8, 1);
        } else if (!phase->movie_ok && phase->stage_name[0]) {
            mb_font_draw_text_shadow(phase->frame_rgba, SCR_W, SCR_H, 16, 10,
                                     phase->stage_name,
                                     235, 225, 180, 16, 12, 8, 1);
        }
        /* Progress bar for pages / movie. */
        if (!phase->movie_ok) {
            int x, y, fill = (int)((phase->page * 240u) /
                                   (phase->pages_needed ? phase->pages_needed : 1u));
            if (fill > 240) fill = 240;
            for (y = 204; y < 212; y++)
                for (x = 40; x < 40 + fill; x++) {
                    uint8_t *px = phase->frame_rgba + ((size_t)y * SCR_W + (size_t)x) * 4u;
                    px[0] = 200; px[1] = 160; px[2] = 40; px[3] = 255;
                }
        }
    }
    runway_compose_hud_pause(phase->frame_rgba, SCR_W, SCR_H, NULL, 0, 0,
                             phase->state == SPECIAL_ST_PAUSED);
    return 0;
}

int special_phase_done(const special_phase *phase) {
    return phase && (phase->state == SPECIAL_ST_FINISHED ||
                     phase->state == SPECIAL_ST_LOST ||
                     phase->state == SPECIAL_ST_ABORTED);
}
