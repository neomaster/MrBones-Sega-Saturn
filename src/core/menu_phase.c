#include "menu_phase.h"
#include "core/cdimage.h"
#include "core/duck_avi.h"
#include "core/mb_font.h"
#include "core/pan_palette.h"
#include "core/resource.h"
#include "core/runway_compose.h"
#include "core/scrl_map.h"
#include "core/sprite_decode.h"
#include "core/stxtr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MENU_W 320u
#define MENU_H 224u

typedef struct {
    const char *want;
    menu_phase *menu;
} walk_ctx;

/* ISO dirs like /04RUNWAY → name RUNWAY, skip empty TITL-only stubs later. */
static int collect_level(const cd_iso_entry *e, void *user) {
    walk_ctx *ctx = user;
    const char *p;
    unsigned idx;
    char name[MENU_LEVEL_NAME];
    size_t n;
    if (!e || e->is_dir == 0) return 0;
    p = e->path;
    if (p[0] != '/') return 0;
    /* Top-level /NNNAME only (no nested slash). */
    if (!(p[1] >= '0' && p[1] <= '9' && p[2] >= '0' && p[2] <= '9')) return 0;
    if (strchr(p + 1, '/')) return 0;
    if (strcmp(p, "/03MENU") == 0) return 0;
    n = strlen(p + 3);
    if (n == 0 || n >= MENU_LEVEL_NAME) return 0;
    memcpy(name, p + 3, n + 1);
    if (ctx->menu->level_count >= MENU_LEVEL_MAX) return 0;
    idx = ctx->menu->level_count++;
    memset(&ctx->menu->levels[idx], 0, sizeof ctx->menu->levels[idx]);
    snprintf(ctx->menu->levels[idx].name, sizeof ctx->menu->levels[idx].name, "%s", name);
    snprintf(ctx->menu->levels[idx].iso_dir, sizeof ctx->menu->levels[idx].iso_dir, "%s", p);
    ctx->menu->levels[idx].unlocked = mb_nvm_level_unlocked(&ctx->menu->nvm, idx);
    return 0;
}

static void fill_frame(menu_phase *menu, uint8_t r, uint8_t g, uint8_t b) {
    size_t i, n;
    if (!menu || !menu->frame_rgba) return;
    n = (size_t)MENU_W * MENU_H * 4u;
    for (i = 0; i < n; i += 4) {
        menu->frame_rgba[i + 0] = r;
        menu->frame_rgba[i + 1] = g;
        menu->frame_rgba[i + 2] = b;
        menu->frame_rgba[i + 3] = 255u;
    }
}

static void draw_bar(menu_phase *menu, int y, int selected) {
    int x, yy;
    uint8_t *px;
    if (!menu || !menu->frame_rgba || y < 0 || y + 14 >= (int)MENU_H) return;
    for (yy = y; yy < y + 14; yy++) {
        int border = (yy == y || yy == y + 13);
        for (x = 20; x < 300; x++) {
            px = menu->frame_rgba + ((size_t)yy * MENU_W + (size_t)x) * 4u;
            if (selected) {
                px[0] = border ? 240u : 48u;
                px[1] = border ? 200u : 110u;
                px[2] = border ? 80u  : 190u;
                px[3] = 255u;
            } else {
                px[0] = border ? 60u : 22u;
                px[1] = border ? 60u : 24u;
                px[2] = border ? 90u : 44u;
                px[3] = 255u;
            }
        }
    }
}

static int start_rows(const menu_phase *menu) {
    return menu && menu->original_menu_rgba ? 2 : 3;
}

static int options_rows(const menu_phase *menu) {
    (void)menu;
    return 3; /* Music / SFX / Back */
}

static int level_rows(const menu_phase *menu) {
    return menu ? (int)menu->level_count + 1 : 1; /* levels + Back */
}

static int screen_rows(const menu_phase *menu) {
    if (!menu) return 0;
    switch (menu->screen) {
    case MENU_SCR_TITLE: return 1;
    case MENU_SCR_START: return start_rows(menu);
    case MENU_SCR_OPTIONS: return options_rows(menu);
    case MENU_SCR_LEVELS: return level_rows(menu);
    }
    return 0;
}

static void clamp_cursor(menu_phase *menu) {
    int n = screen_rows(menu);
    if (n <= 0) { menu->cursor = 0; return; }
    if (menu->cursor < 0) menu->cursor = n - 1;
    if (menu->cursor >= n) menu->cursor = 0;
}

static void render_status(menu_phase *menu) {
    const char *scr = "?";
    if (!menu) return;
    if (menu->title_avi.decode_error) {
        snprintf(menu->status, sizeof menu->status, "MENU: %s", menu->title_avi.status);
        return;
    }
    switch (menu->screen) {
    case MENU_SCR_TITLE: scr = "TITLE"; break;
    case MENU_SCR_START: scr = "START"; break;
    case MENU_SCR_OPTIONS: scr = "OPTIONS"; break;
    case MENU_SCR_LEVELS: scr = "LEVELS"; break;
    case MENU_SCR_STORY: scr = "STORY"; break;
    }
    if (menu->state == MENU_ST_START_LEVEL) {
        snprintf(menu->status, sizeof menu->status,
                 "MENU -> %s (%s)",
                 menu->levels[menu->selected_level].name,
                 menu->levels[menu->selected_level].iso_dir);
        return;
    }
    snprintf(menu->status, sizeof menu->status,
             "MENU %s cur=%d music=%d sfx=%d levels=%u gfx=%u avi=%zu titl=%zu "
             "nvm=%s",
             scr, menu->cursor, menu->music_on, menu->sfx_on, menu->level_count,
             menu->gfx_count, menu->avi_size, menu->titl_size,
             menu->nvm.loaded ? "loaded" : "default");
}

static void blit_menu_gfx(menu_phase *menu) {
    unsigned i;
    if (!menu || !menu->frame_rgba || menu->gfx_count == 0) return;
    /* Largest plate first as soft TITLE backdrop; smaller ones as accents. */
    for (i = 0; i < menu->gfx_count; i++) {
        int dx, dy;
        if (!menu->gfx_rgba[i] || !menu->gfx_w[i] || !menu->gfx_h[i]) continue;
        if (i == 0) {
            dx = ((int)MENU_W - (int)menu->gfx_w[i]) / 2;
            dy = menu->screen == MENU_SCR_TITLE ? 16 : 4;
        } else {
            dx = 8 + (int)((i - 1u) * 40u);
            dy = (int)MENU_H - (int)menu->gfx_h[i] - 8;
        }
        runway_compose_blit_sprite(menu->frame_rgba, MENU_W, MENU_H,
                                   menu->gfx_rgba[i], menu->gfx_w[i], menu->gfx_h[i],
                                   dx, dy);
    }
}

static void render_frame(menu_phase *menu) {
    int rows, i, y;
    char line[96];
    if (!menu || !menu->frame_rgba) return;
    if (menu->screen == MENU_SCR_TITLE || menu->screen == MENU_SCR_STORY) {
        if (menu->title_avi_ok && menu->title_avi.frames_drawn &&
            duck_avi_frame_rgba(&menu->title_avi)) {
            memcpy(menu->frame_rgba, duck_avi_frame_rgba(&menu->title_avi),
                   (size_t)MENU_W * MENU_H * 4u);
            render_status(menu);
            return;
        }
        fill_frame(menu, 12, 12, 20);
        blit_menu_gfx(menu);
        if (menu->copyright_rgba) {
            runway_compose_blit_sprite(menu->frame_rgba, MENU_W, MENU_H,
                                       menu->copyright_rgba,
                                       menu->copyright_w, menu->copyright_h,
                                       (int)(MENU_W - menu->copyright_w) / 2, 192);
        }
        mb_font_draw_text_centered(menu->frame_rgba, MENU_W, MENU_H, 20,
                                   "MR. BONES",
                                   255, 220, 72, 24, 12, 4, 2);
        mb_font_draw_text_centered(menu->frame_rgba, MENU_W, MENU_H, 44,
                                   "SEGA SATURN",
                                   200, 210, 240, 16, 16, 32, 1);
        mb_font_draw_text_centered(menu->frame_rgba, MENU_W, MENU_H, 150,
                                   "- PRESS START -",
                                   255, 245, 160, 20, 16, 8, 1);
    } else if (menu->screen == MENU_SCR_START) {
        static const char *const k_items[3] = {
            "START GAME",
            "OPTIONS",
            "LEVEL SELECT"
        };
        if (menu->original_menu_rgba) {
            memcpy(menu->frame_rgba, menu->original_menu_rgba, MENU_W * MENU_H * 4u);
            runway_compose_blit_sprite(menu->frame_rgba, MENU_W, MENU_H,
                                       menu->original_items[menu->cursor == 0 ? 1 : 0],
                                       64, 14, 128, 153);
            runway_compose_blit_sprite(menu->frame_rgba, MENU_W, MENU_H,
                                       menu->original_items[menu->cursor == 1 ? 3 : 2],
                                       96, 13, 112, 174);
            render_status(menu);
            return;
        }
        fill_frame(menu, 12, 12, 32);
        blit_menu_gfx(menu);
        mb_font_draw_text_centered(menu->frame_rgba, MENU_W, MENU_H, 16,
                                   "MR. BONES",
                                   255, 220, 72, 24, 12, 4, 2);
        for (i = 0; i < 3; i++) {
            int sel = (i == menu->cursor);
            y = 58 + i * 24;
            draw_bar(menu, y, sel);
            snprintf(line, sizeof line, "%s %s", sel ? ">" : " ", k_items[i]);
            mb_font_draw_text_shadow(menu->frame_rgba, MENU_W, MENU_H, 32, y + 3,
                                     line,
                                     sel ? 255 : 190, sel ? 245 : 195, sel ? 140 : 210,
                                     12, 12, 20, 1);
        }
        mb_font_draw_text_centered(menu->frame_rgba, MENU_W, MENU_H, 204,
                                   "UP/DOWN: MOVE   START/A: SELECT",
                                   150, 170, 200, 10, 10, 20, 1);
    } else if (menu->screen == MENU_SCR_OPTIONS) {
        fill_frame(menu, 16, 24, 16);
        blit_menu_gfx(menu);
        mb_font_draw_text_centered(menu->frame_rgba, MENU_W, MENU_H, 16,
                                   "OPTIONS",
                                   180, 240, 160, 12, 24, 12, 2);
        for (i = 0; i < 3; i++) {
            int sel = (i == menu->cursor);
            y = 58 + i * 24;
            draw_bar(menu, y, sel);
            if (i == 0)
                snprintf(line, sizeof line, "%s MUSIC: %s",
                         sel ? ">" : " ", menu->music_on ? "ON" : "OFF");
            else if (i == 1)
                snprintf(line, sizeof line, "%s SFX:   %s",
                         sel ? ">" : " ", menu->sfx_on ? "ON" : "OFF");
            else
                snprintf(line, sizeof line, "%s BACK TO MAIN MENU",
                         sel ? ">" : " ");
            mb_font_draw_text_shadow(menu->frame_rgba, MENU_W, MENU_H, 32, y + 3,
                                     line,
                                     sel ? 255 : 190, sel ? 245 : 195, sel ? 140 : 210,
                                     12, 12, 20, 1);
        }
    } else if (menu->screen == MENU_SCR_LEVELS) {
        int win_start = 0;
        int visible = 9;
        fill_frame(menu, 24, 16, 8);
        blit_menu_gfx(menu);
        mb_font_draw_text_centered(menu->frame_rgba, MENU_W, MENU_H, 10,
                                   "LEVEL SELECT",
                                   255, 210, 110, 24, 12, 4, 1);
        rows = screen_rows(menu);
        if (menu->cursor >= visible)
            win_start = menu->cursor - (visible - 1);
        if (win_start + visible > rows)
            win_start = rows > visible ? (rows - visible) : 0;
        for (i = 0; i < visible && (win_start + i) < rows; i++) {
            int idx = win_start + i;
            int sel = (idx == menu->cursor);
            y = 28 + i * 18;
            draw_bar(menu, y, sel);
            if ((unsigned)idx < menu->level_count) {
                snprintf(line, sizeof line, "%s %02d %-10s %s",
                         sel ? ">" : " ", idx + 1,
                         menu->levels[idx].name,
                         menu->levels[idx].unlocked ? "[OPEN]" : "[LOCK]");
            } else {
                snprintf(line, sizeof line, "%s BACK TO MAIN MENU",
                         sel ? ">" : " ");
            }
            mb_font_draw_text_shadow(menu->frame_rgba, MENU_W, MENU_H, 28, y + 3,
                                     line,
                                     sel ? 255 : 185, sel ? 245 : 185, sel ? 140 : 195,
                                     12, 12, 20, 1);
        }
        mb_font_draw_text_centered(menu->frame_rgba, MENU_W, MENU_H, 204,
                                   "UP/DOWN: STAGE   A/START: LAUNCH",
                                   170, 160, 140, 10, 10, 10, 1);
    }
    render_status(menu);
}

static void begin_level(menu_phase *menu, int level_index) {
    if (!menu || level_index < 0 || (unsigned)level_index >= menu->level_count)
        return;
    if (!menu->levels[level_index].unlocked) {
        snprintf(menu->status, sizeof menu->status, "MENU locked: %s",
                 menu->levels[level_index].name);
        return;
    }
    menu->selected_level = level_index;
    menu->state = MENU_ST_START_LEVEL;
    mb_nvm_set_u32(&menu->nvm, MB_NVM_DEFAULT_LEVEL_OFF, MB_NVM_LEVEL_ID(level_index));
    mb_nvm_save(&menu->nvm, menu->save_dir);
    render_status(menu);
}

static void start_story(menu_phase *menu, int level_index) {
    begin_level(menu, level_index);
    if (menu->state != MENU_ST_START_LEVEL) return;
    menu->state = MENU_ST_ACTIVE;
    duck_avi_free(&menu->title_avi);
    free(menu->avi_blob); menu->avi_blob = NULL; menu->avi_size = 0;
    free(menu->duck_blob); menu->duck_blob = NULL; menu->duck_size = 0;
    (void)resource_load(menu->cd, "/03MENU/20AVI", &menu->avi_blob, &menu->avi_size);
    (void)resource_load(menu->cd, "/03MENU/19DUCK", &menu->duck_blob, &menu->duck_size);
    if (!menu->avi_size ||
        duck_avi_open_with_duck(&menu->title_avi, menu->avi_blob, menu->avi_size,
                                menu->duck_blob, menu->duck_size) != 0 ||
        !duck_avi_step(&menu->title_avi)) {
        menu->state = MENU_ST_QUIT;
        fill_frame(menu, 0, 0, 0);
        snprintf(menu->status, sizeof menu->status, "03MENU/20AVI: %.125s",
                 menu->avi_size ? menu->title_avi.status : "resource missing or unreadable");
        return;
    }
    menu->screen = MENU_SCR_STORY;
    menu->title_avi_ok = 1;
    menu->title_avi_frames = 1;
    menu->movie_pcm_pending = menu->title_avi.pcm_samples > 0;
    render_frame(menu);
}

static void activate(menu_phase *menu) {
    int i;
    if (!menu) return;
    switch (menu->screen) {
    case MENU_SCR_TITLE:
        menu->screen = MENU_SCR_START;
        menu->movie_pcm_pending = 0;
        menu->cursor = 0;
        break;
    case MENU_SCR_START:
        if (menu->cursor == 0) {
            /* Start Game → first unlocked (RUNWAY). SelectMenuEntry case 1. */
            for (i = 0; i < (int)menu->level_count; i++) {
                if (menu->levels[i].unlocked) {
                    start_story(menu, i);
                    return;
                }
            }
        } else if (menu->cursor == 1) {
            menu->screen = MENU_SCR_OPTIONS; /* case 2 */
            menu->cursor = 0;
        } else if (menu->cursor == 2) {
            menu->screen = MENU_SCR_LEVELS; /* case 3 */
            menu->cursor = 0;
        }
        break;
    case MENU_SCR_STORY:
        menu->movie_pcm_pending = 0;
        menu->state = MENU_ST_START_LEVEL;
        render_status(menu);
        return;
    case MENU_SCR_OPTIONS:
        if (menu->cursor == 0) {
            menu->music_on ^= 1; /* case 4 */
        } else if (menu->cursor == 1) {
            menu->sfx_on ^= 1; /* case 5 */
        } else {
            menu->screen = MENU_SCR_START; /* BackTrak */
            menu->cursor = 1;
        }
        break;
    case MENU_SCR_LEVELS:
        if (menu->cursor >= 0 && (unsigned)menu->cursor < menu->level_count) {
            begin_level(menu, menu->cursor);
            return;
        }
        menu->screen = MENU_SCR_START;
        menu->cursor = 2;
        break;
    }
    clamp_cursor(menu);
    render_frame(menu);
}

static void go_back(menu_phase *menu) {
    if (!menu) return;
    if (menu->screen == MENU_SCR_TITLE) return;
    if (menu->screen == MENU_SCR_START) {
        menu->screen = MENU_SCR_TITLE;
        menu->cursor = 0;
    } else {
        menu->screen = MENU_SCR_START;
        menu->cursor = 0;
    }
    render_frame(menu);
}



static void build_menu_palette(const uint8_t *code, size_t code_size, uint8_t *palette) {
    uint32_t words[1024];
    size_t off, consumed, best = 0, i;
    uint32_t tmp[1024];
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
                if (best >= 3080) break;
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

static int decode_menu_record(const stxtr_record *rec, const uint8_t *palette,
                              uint8_t **out, unsigned *w, unsigned *h) {
    static const uint16_t k_pmods[] = {0x0020, 0x0028, 0x00a0, 0x00a8, 0};
    size_t size;
    uint8_t *pixels;
    unsigned i;
    if (!rec || !out || !w || !h) return -1;
    size = (size_t)rec->width * rec->height * 4u;
    pixels = malloc(size);
    if (!pixels) return -1;
    if (rec->size == (size_t)rec->width * rec->height) {
        for (size_t j = 0; j < (size_t)rec->width * rec->height; j++) {
            uint8_t idx = rec->data[j];
            if (idx == 0) {
                pixels[4 * j + 0] = pixels[4 * j + 1] = pixels[4 * j + 2] = pixels[4 * j + 3] = 0;
            } else {
                pixels[4 * j + 0] = palette[4 * idx + 1];
                pixels[4 * j + 1] = palette[4 * idx + 2];
                pixels[4 * j + 2] = palette[4 * idx + 3];
                pixels[4 * j + 3] = 255;
            }
        }
        *out = pixels;
        *w = rec->width;
        *h = rec->height;
        return 0;
    }
    for (i = 0; k_pmods[i]; i++) {
        if (mb_sprite_decode(rec->data, rec->size, rec->width, rec->height, k_pmods[i],
                             palette, 1024, pixels, size) == 0) {
            
            *out = pixels;
            *w = rec->width;
            *h = rec->height;
            return 0;
        }
    }
    free(pixels);
    return -1;
}

static unsigned menu_be16(const uint8_t *p) {
    return (unsigned)p[0] << 8 | p[1];
}

static unsigned menu_be32(const uint8_t *p) {
    return menu_be16(p) << 16 | menu_be16(p + 2);
}

static int decode_menu_plane(const uint8_t *cdb, size_t cdb_size,
                             const uint8_t *til, size_t til_size, uint8_t *pixels) {
    uint8_t chars[16 * 64], seen[70] = {0};
    size_t pos = 0;
    unsigned block;
    if (!cdb || cdb_size < 10 || !til || !pixels ||
        menu_be16(cdb) != MENU_W || menu_be16(cdb + 2) != MENU_H ||
        menu_be16(cdb + 4) != 10 || menu_be16(cdb + 6) != 7 ||
        menu_be16(cdb + 8) > (cdb_size - 10) / 64) return -1;
    memset(pixels, 0, MENU_W * MENU_H);
    for (block = 0; block < 70; block++) {
        unsigned size, count, id, cell;
        if (pos > til_size || til_size - pos < 72) return -1;
        size = menu_be16(til + pos);
        count = menu_be16(til + pos + 2);
        id = menu_be16(til + pos + 4);
        if (size < 72 || size > til_size - pos || count > 16 || id >= 70 || seen[id]) return -1;
        seen[id] = 1;
        if (count && scrl_decomp_byte(til + pos + 72, size - 72, chars, count * 64) != count * 64)
            return -1;
        for (cell = 0; cell < 16; cell++) {
            unsigned descriptor = menu_be32(til + pos + 8 + cell * 4);
            size_t off = (size_t)(descriptor & 0xffffu) * 32u;
            const uint8_t *src;
            unsigned row, col, x = (id % 10) * 32 + (cell % 4) * 8;
            unsigned y = (id / 10) * 32 + (cell / 4) * 8;
            const int hflip = (descriptor & 0x40000000u) != 0;
            const int vflip = (descriptor & 0x80000000u) != 0;
            if (descriptor & 0x08000000u) {
                if (off > cdb_size - 10 || cdb_size - 10 - off < 64) return -1;
                src = cdb + 10 + off;
            } else {
                if (off > count * 64u || count * 64u - off < 64) return -1;
                src = chars + off;
            }
            for (row = 0; row < 8; row++) {
                const unsigned sr = vflip ? (7u - row) : row;
                for (col = 0; col < 8; col++) {
                    const unsigned sc = hflip ? (7u - col) : col;
                    pixels[(y + row) * MENU_W + x + col] = src[sr * 8u + sc];
                }
            }
        }
        pos += size;
    }
    return pos == til_size ? 0 : -1;
}

static void load_original_menu(menu_phase *menu, cdimage *cd) {
    static const char *const names[4] = {
        "/03MENU/07ACDB", "/03MENU/08ATIL", "/03MENU/09BCDB", "/03MENU/10BTIL"
    };
    static const size_t offsets[4] = {0x24810, 0x24ba0, 0x24f30, 0x25420};
    void *blob[4] = {0};
    size_t sizes[4] = {0}, consumed = 0, i;
    uint32_t colors[1024] = {0};
    uint8_t palette[1024], *planes = NULL, *background = NULL, *items[4] = {0};
    stxtr_record rec;
    unsigned w, h;
    if (!menu->code_blob || menu->code_size <= 0x5100 ||
        menu_be32(menu->code_blob + 0x5100) != 0x100 ||
        pan_palette_apply(menu->code_blob + 0x5100, menu->code_size - 0x5100,
                          colors, 1024, &consumed) || consumed != 0xc08) return;
    for (i = 0; i < 4; i++)
        if (resource_load(cd, names[i], &blob[i], &sizes[i])) goto done;
    planes = malloc(MENU_W * MENU_H * 2u);
    background = malloc(MENU_W * MENU_H * 4u);
    if (!planes || !background ||
        decode_menu_plane(blob[0], sizes[0], blob[1], sizes[1], planes) ||
        decode_menu_plane(blob[2], sizes[2], blob[3], sizes[3], planes + MENU_W * MENU_H)) goto done;
    /* shortcut: sky is static until the original VDP2 scrolling is reconstructed. */
    for (i = 0; i < MENU_W * MENU_H; i++) {
        size_t y = i / MENU_W;
        unsigned index = y + 19 < MENU_H ? planes[MENU_W * MENU_H + i + 19 * MENU_W] : 0;
        uint32_t rgb = index ? colors[512 + index] : colors[256 + planes[i]];
        background[4 * i] = (uint8_t)(rgb >> 16);
        background[4 * i + 1] = (uint8_t)(rgb >> 8);
        background[4 * i + 2] = (uint8_t)rgb;
        background[4 * i + 3] = 255;
    }
    for (i = 0; i < 256; i++) {
        uint32_t rgb = colors[768 + i];
        palette[4 * i] = 0;
        palette[4 * i + 1] = (uint8_t)(rgb >> 16);
        palette[4 * i + 2] = (uint8_t)(rgb >> 8);
        palette[4 * i + 3] = (uint8_t)rgb;
    }
    for (i = 0; i < 4; i++) {
        size_t off = offsets[i];
        if (stxtr_next(menu->stxt_blob, menu->stxt_size, &off, &rec) != 1 ||
            rec.width != (i < 2 ? 64u : 96u) || rec.height != (i < 2 ? 14u : 13u) ||
            decode_menu_record(&rec, palette, &items[i], &w, &h)) goto done;
    }
    menu->original_menu_rgba = background;
    background = NULL;
    for (i = 0; i < 4; i++) { menu->original_items[i] = items[i]; items[i] = NULL; }
done:
    for (i = 0; i < 4; i++) { free(blob[i]); free(items[i]); }
    free(planes);
    free(background);
}

/* Prefer wide plates (TITLE) then medium accents from /03MENU/04STXT. */
static void load_menu_stxt_gfx(menu_phase *menu) {
    uint8_t palette[4096];
    size_t off = 0;
    stxtr_record rec;
    const uint8_t *bank1;
    if (!menu || !menu->stxt_blob || menu->stxt_size == 0) return;
    build_menu_palette(menu->code_blob, menu->code_size, palette);
    bank1 = palette + 1024;
    while (stxtr_next(menu->stxt_blob, menu->stxt_size, &off, &rec) == 1) {
        uint8_t *px = NULL;
        unsigned w = 0, h = 0;
        unsigned area;
        if (rec.width == 216 && rec.height == 24 && !menu->copyright_rgba) {
            if (decode_menu_record(&rec, bank1, &px, &w, &h) == 0) {
                menu->copyright_rgba = px;
                menu->copyright_w = w;
                menu->copyright_h = h;
            }
            continue;
        }
        if (rec.width < 16 || rec.height < 8) continue;
        if (rec.width > MENU_W || rec.height > MENU_H) continue;
        if (menu->gfx_count < 8 && decode_menu_record(&rec, bank1, &px, &w, &h) == 0) {
            area = w * h;
            /* Keep largest first: insert-sort by area descending into gfx slots. */
            unsigned slot = menu->gfx_count;
            unsigned j;
            while (slot > 0) {
                unsigned prev = menu->gfx_w[slot - 1] * menu->gfx_h[slot - 1];
                if (prev >= area) break;
                slot--;
            }
            for (j = menu->gfx_count; j > slot; j--) {
                menu->gfx_rgba[j] = menu->gfx_rgba[j - 1];
                menu->gfx_w[j] = menu->gfx_w[j - 1];
                menu->gfx_h[j] = menu->gfx_h[j - 1];
            }
            menu->gfx_rgba[slot] = px;
            menu->gfx_w[slot] = w;
            menu->gfx_h[slot] = h;
            menu->gfx_count++;
        }
    }
}

int menu_phase_load(menu_phase *menu, cdimage *cd, const char *save_dir) {
    walk_ctx ctx;
    unsigned i;
    if (!menu || !cd) return -1;
    memset(menu, 0, sizeof *menu);
    menu->cd = cd;
    menu->music_on = 1;
    menu->sfx_on = 1;
    menu->screen = MENU_SCR_TITLE;
    menu->state = MENU_ST_ACTIVE;
    if (save_dir && save_dir[0])
        snprintf(menu->save_dir, sizeof menu->save_dir, "%s", save_dir);
    else
        snprintf(menu->save_dir, sizeof menu->save_dir, ".");
    (void)mb_nvm_load(&menu->nvm, menu->save_dir);

    ctx.want = NULL;
    ctx.menu = menu;
    if (cd_iso_walk(cd, collect_level, &ctx) != 0 && menu->level_count == 0)
        return -1;
    /* Prefer RUNWAY first in list for Start Game. */
    for (i = 0; i < menu->level_count; i++) {
        if (strcmp(menu->levels[i].name, "RUNWAY") == 0) {
            menu_level tmp = menu->levels[0];
            menu->levels[0] = menu->levels[i];
            menu->levels[i] = tmp;
            break;
        }
    }
    for (i = 0; i < menu->level_count; i++)
        menu->levels[i].unlocked = mb_nvm_level_unlocked(&menu->nvm, i);
    if (menu->level_count > 0 && !menu->levels[0].unlocked) {
        mb_nvm_unlock_level(&menu->nvm, 0);
        menu->levels[0].unlocked = 1;
        mb_nvm_save(&menu->nvm, menu->save_dir);
    }

    /* The boot movie comes from INTRO; MENU/20AVI is a different cinematic. */
    (void)resource_load(cd, "/03MENU/03CODE", (void **)&menu->code_blob,
                        &menu->code_size);
    (void)resource_load(cd, "/03MENU/04STXT", (void **)&menu->stxt_blob,
                        &menu->stxt_size);
    (void)resource_load(cd, "/03MENU/02TITL", &menu->titl_blob, &menu->titl_size);
    (void)resource_load(cd, "/29INTRO/20AVI", &menu->avi_blob, &menu->avi_size);
    (void)resource_load(cd, "/29INTRO/19DUCK", &menu->duck_blob, &menu->duck_size);
    load_menu_stxt_gfx(menu);
    load_original_menu(menu, cd);

    if (!menu->avi_size ||
        duck_avi_open_with_duck(&menu->title_avi, menu->avi_blob, menu->avi_size,
                                menu->duck_blob, menu->duck_size) != 0 ||
        !duck_avi_step(&menu->title_avi)) {
        char reason[160];
        snprintf(reason, sizeof reason, "INTRO/20AVI: %.125s",
                 menu->avi_size ? menu->title_avi.status : "resource missing or unreadable");
        menu_phase_free(menu);
        snprintf(menu->status, sizeof menu->status, "%s", reason);
        return -1;
    }
    menu->title_avi_ok = 1;
    menu->title_avi_frames = 1;
    if (menu->title_avi.pcm_samples > 0)
        menu->movie_pcm_pending = 1;

    menu->frame_rgba = calloc((size_t)MENU_W * MENU_H * 4u, 1);
    if (!menu->frame_rgba) return -1;
    render_frame(menu);
    return 0;
}

void menu_phase_free(menu_phase *menu) {
    unsigned i;
    if (!menu) return;
    if (menu->nvm.dirty)
        mb_nvm_save(&menu->nvm, menu->save_dir);
    for (i = 0; i < menu->gfx_count && i < 8; i++)
        free(menu->gfx_rgba[i]);
    free(menu->copyright_rgba);
    free(menu->original_menu_rgba);
    for (i = 0; i < 4; i++) free(menu->original_items[i]);
    duck_avi_free(&menu->title_avi);
    free(menu->stxt_blob);
    free(menu->code_blob);
    free(menu->titl_blob);
    free(menu->avi_blob);
    free(menu->duck_blob);
    free(menu->frame_rgba);
    memset(menu, 0, sizeof *menu);
}

void menu_phase_show_start(menu_phase *menu) {
    if (!menu) return;
    menu->screen = MENU_SCR_START;
    menu->cursor = 0;
    menu->movie_pcm_pending = 0;
    render_frame(menu);
}

int menu_phase_step(menu_phase *menu, const saturn_pad *pad) {
    if (!menu || !pad || menu_phase_done(menu)) return 0;
    menu->frames++;

    /* The original proceeds to its title menu when the opening ends. */
    if ((menu->screen == MENU_SCR_TITLE || menu->screen == MENU_SCR_STORY) && menu->title_avi_ok &&
        !duck_avi_playback_done(&menu->title_avi)) {
        int advanced = duck_avi_tick(&menu->title_avi,
            menu->title_avi.tick_external ? menu->title_avi.tick_us : 16000);
        if (advanced < 0) {
            menu->movie_pcm_pending = 0;
            menu->state = MENU_ST_QUIT;
            fill_frame(menu, 0, 0, 0);
            snprintf(menu->status, sizeof menu->status, "MENU: %.145s", menu->title_avi.status);
            return 1;
        }
        if (advanced > 0 || duck_avi_playback_done(&menu->title_avi)) {
            menu->title_avi_frames += (unsigned)advanced;
            if (duck_avi_playback_done(&menu->title_avi)) {
                if (menu->screen == MENU_SCR_STORY)
                    menu->state = MENU_ST_START_LEVEL;
                else {
                    menu->screen = MENU_SCR_START;
                    menu->cursor = 0;
                }
                menu->movie_pcm_pending = 0;
            }
            render_frame(menu);
        }
    }

    if (pad->pressed & 0x0800u) { /* Start */
        if (menu->screen == MENU_SCR_TITLE)
            activate(menu);
        else if (menu->screen == MENU_SCR_START && menu->cursor == 0)
            activate(menu);
        else
            activate(menu);
        return 1;
    }
    /* D-pad: Brain MoveSelectorH/V stand-in. */
    if (pad->pressed & 0x1000u) { /* Up */
        menu->cursor--;
        clamp_cursor(menu);
        render_frame(menu);
    }
    if (pad->pressed & 0x2000u) { /* Down */
        menu->cursor++;
        clamp_cursor(menu);
        render_frame(menu);
    }
    if (pad->pressed & 0x4000u) { /* Left */
        menu->cursor--;
        clamp_cursor(menu);
        render_frame(menu);
    }
    if (pad->pressed & 0x8000u) { /* Right */
        menu->cursor++;
        clamp_cursor(menu);
        render_frame(menu);
    }
    /* A/B/C → SelectMenuEntry */
    if (pad->pressed & (0x0400u | 0x0100u | 0x0200u))
        activate(menu);
    /* X / Z-ish → BackTrak (use L trigger bit if present; also Esc via tool) */
    if (pad->pressed & 0x0008u)
        go_back(menu);

    render_status(menu);
    return 1;
}

int menu_phase_done(const menu_phase *menu) {
    return menu && (menu->state == MENU_ST_START_LEVEL || menu->state == MENU_ST_QUIT);
}

const char *menu_phase_selected_iso(const menu_phase *menu) {
    if (!menu || menu->state != MENU_ST_START_LEVEL) return NULL;
    if (menu->selected_level < 0 ||
        (unsigned)menu->selected_level >= menu->level_count)
        return NULL;
    return menu->levels[menu->selected_level].iso_dir;
}
