/*
 * Integrated Mr. Bones host (§19–§25 / 1.0.0): MENU title AVI → full campaign
 * (native) → INSERTCD → FINALE → credits roll; HUD; CD-DA + 05SND1; duck_avi;
 * DirNameTable; nvmshadow.bin.
 *
 * Usage:
 *   mrbones <disc1.cue|bin> <manifest>
 *           [--disc2 cue] [--check] [--full-campaign] [--save-dir DIR]
 *           [--skip-menu] [--level ISO] [--campaign] [--version]
 *
 * --full-campaign / --check: headless playthrough; every campaign stage must be
 * native (runway/sidescrl/special), not smoke. EMPTY (TROLL) skipped by progress.
 */
#define MB_HOST_VERSION "1.0.0"
#include "core/cdimage.h"
#include "core/identity.h"
#include "core/mb_font.h"
#include "core/menu_phase.h"
#include "core/nvm_shadow.h"
#include "core/runway_sfx.h"
#include "core/saturn_pad.h"
#include "core/special_phase.h"
#include "core/stage_catalog.h"
#include "core/stage_progress.h"
#include "core/stage_runner.h"
#include "platform/config.h"
#include "platform/host_audio.h"
#include "platform/host_input.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    APP_MENU = 0,
    APP_STAGE = 1,
    APP_CREDITS = 2,
    APP_DONE = 3
} app_mode;

#define CREDITS_HOLD_FRAMES 360u
#define CREDITS_PAGES 6u
#define STAGE_MAX_FRAMES 5000

static app_mode g_mode = APP_MENU;
static menu_phase *g_menu;
static stage_runner *g_stage;
static stage_progress g_prog;
static mb_nvm_shadow g_nvm;
static runway_sfx g_sfx;
static int g_sfx_ok;
static host_audio *g_audio;
static host_audio *g_movie_audio;
static int g_movie_audio_failed;
static ULONGLONG g_movie_last_tick;
static unsigned g_movie_audio_position_ms;
static char g_movie_next_stage[STAGE_ISO_MAX];
static const struct { const char *from; const char *to; } g_fmv_routes[] = {
    {"/06VALLEY", "/07GUITAR"}, {"/07GUITAR", "/08BAT1"},
    {"/11LOGS", "/14DRUM"}, {"/14DRUM", "/15GLASS"},
    {"/16BIGBNS", "/17VORTEX"}, {"/17VORTEX", "/18RIDE"},
    {"/18RIDE", "/19VILLAGE"}, {"/19VILLAGE", "/20ICE1"},
    {"/22SHADOW", "/23MOAT"}, {"/23MOAT", "/24STAING"},
    {"/24STAING", "/25GENRM"}, {"/25GENRM", "/26LAFF"},
    {"/26LAFF", "/27FINALE"}
};
static uint32_t g_pending_keys;
static char g_title[192];
static char g_save_dir[MB_PATH_MAX];
static char g_disc2_path[MB_PATH_MAX];
static int g_mounted_disc = 1;
static stage_catalog g_catalog;
static cdimage *g_cd;
static cdimage g_cd_storage;
static cdimage g_cd2_storage;
static int g_have_disc2;
static char g_cue1[MB_PATH_MAX];
static uint8_t g_credits_rgba[320 * 224 * 4];
static unsigned g_credits_frames;
static char g_credits_status[160];

static void host_play_pcm(void *user, const int8_t *pcm, size_t length, unsigned rate) {
    (void)user;
    host_audio_play_pcm8(pcm, length, rate);
}

static int stage_is_native_live(const stage_runner *s) {
    return s && (s->runway_live || s->sidescrl_live || s->special_live);
}

static int stage_audio_track(const stage_runner *s) {
    if (!s) return 0;
    if (s->runway_live) return (int)s->runway.audio_track;
    if (s->sidescrl_live) return (int)s->sidescrl.audio_track;
    if (s->special_live) return (int)s->special.audio_track;
    return 0;
}

static void maybe_start_movie_audio(void) {
    if (g_mode == APP_MENU && g_menu && g_menu->movie_pcm_pending &&
        g_menu->title_avi.pcm && g_menu->title_avi.pcm_samples > 0) {
        host_audio_stop(g_movie_audio);
        g_movie_audio = host_audio_start_pcm16(g_menu->title_avi.pcm,
            g_menu->title_avi.pcm_samples, g_menu->title_avi.pcm_rate,
            g_menu->title_avi.pcm_channels);
        g_movie_audio_failed = !g_movie_audio;
        if (g_movie_audio) { g_movie_last_tick = GetTickCount64(); g_movie_audio_position_ms = 0; }
        else fprintf(stderr, "movie audio unavailable (menu)\n");
        g_menu->movie_pcm_pending = 0;
    }
    if (g_mode != APP_STAGE || !g_stage) return;
    if (g_stage->special_live && g_stage->special.movie_pcm_pending &&
        g_stage->special.movie.pcm && g_stage->special.movie.pcm_samples > 0) {
        host_audio_stop(g_movie_audio);
        g_movie_audio = host_audio_start_pcm16(g_stage->special.movie.pcm,
            g_stage->special.movie.pcm_samples, g_stage->special.movie.pcm_rate,
            g_stage->special.movie.pcm_channels);
        g_movie_audio_failed = !g_movie_audio;
        if (g_movie_audio) { g_movie_last_tick = GetTickCount64(); g_movie_audio_position_ms = 0; }
        else fprintf(stderr, "movie audio unavailable (%s)\n", g_stage->iso_dir);
        g_stage->special.movie_pcm_pending = 0;
    }
}

static void maybe_play_sfx(void) {
    unsigned flags = 0;
    maybe_start_movie_audio();
    if (!g_stage || !g_sfx_ok) return;
    if (g_stage->runway_live) {
        if (g_stage->runway.sound_event_id)
            runway_sfx_sound_event(&g_sfx, g_stage->runway.sound_event_id);
        flags = g_stage->runway.sfx_flags;
    } else if (g_stage->sidescrl_live) {
        flags = g_stage->sidescrl.sfx_flags;
    } else if (g_stage->special_live) {
        flags = g_stage->special.sfx_flags;
    }
    if (flags)
        runway_sfx_dispatch(&g_sfx, flags, host_play_pcm, NULL);
}

static void restart_stage_audio(void) {
    int track;
    host_audio_stop(g_audio);
    g_audio = NULL;
    if (g_stage && g_stage->special_live &&
        g_stage->special.mode == SPECIAL_MODE_PRESENT &&
        g_stage->special.movie.pcm_samples) return;
    track = stage_audio_track(g_stage);
    if (track >= 2)
        g_audio = host_audio_start_cdda(g_cd, track);
}

static int remount_disc(int want_disc) {
    char err[512];
    if (want_disc == g_mounted_disc && g_cd) return 0;
    host_audio_stop(g_audio);
    g_audio = NULL;
    if (want_disc == 2) {
        if (!g_disc2_path[0]) return -1;
        if (!g_have_disc2) {
            if (cd_open(&g_cd2_storage, g_disc2_path, err, sizeof err)) {
                fprintf(stderr, "disc2 open: %s\n", err);
                return -1;
            }
            g_have_disc2 = 1;
        }
        g_cd = &g_cd2_storage;
    } else {
        if (g_cd_storage.ntracks <= 0 || !g_cd_storage.tracks[0].fp) {
            if (cd_open(&g_cd_storage, g_cue1, err, sizeof err)) {
                fprintf(stderr, "disc1 open: %s\n", err);
                return -1;
            }
        }
        g_cd = &g_cd_storage;
    }
    g_mounted_disc = want_disc;
    return 0;
}

static int enter_stage(const char *iso_dir) {
    g_movie_last_tick = 0;
    g_movie_audio_failed = 0;
    host_audio_stop(g_movie_audio);
    g_movie_audio = NULL;
    host_audio_stop(g_audio);
    g_audio = NULL;
    stage_runner_free(g_stage);
    memset(g_stage, 0, sizeof *g_stage);
    if (stage_runner_begin(g_stage, g_cd, g_mounted_disc, &g_catalog, iso_dir))
        return -1;
    if (g_stage->state == STAGE_RS_NEED_DISC) {
        int need = g_stage->need_disc;
        fprintf(stderr, "INSERTCD: need disc %d for %s\n", need, iso_dir);
        if (remount_disc(need)) return -1;
        if (stage_runner_resume(g_stage, g_cd, g_mounted_disc)) return -1;
    }
    if (g_stage->state != STAGE_RS_PLAYING) return -1;
    g_sfx_ok = 0;
    runway_sfx_free(&g_sfx);
    if (stage_is_native_live(g_stage))
        g_sfx_ok = runway_sfx_load_dir(&g_sfx, g_cd, iso_dir) == 0;
    restart_stage_audio();
    g_mode = APP_STAGE;
    return 0;
}

static int enter_transition_movie(const char *iso_dir, const char *next_iso) {
    g_movie_last_tick = 0;
    g_movie_audio_failed = 0;
    host_audio_stop(g_movie_audio);
    g_movie_audio = NULL;
    host_audio_stop(g_audio);
    g_audio = NULL;
    runway_sfx_free(&g_sfx);
    g_sfx_ok = 0;
    stage_runner_free(g_stage);
    if (stage_runner_begin_movie(g_stage, g_cd, g_mounted_disc, iso_dir)) return -1;
    snprintf(g_movie_next_stage, sizeof g_movie_next_stage, "%s", next_iso);
    g_mode = APP_STAGE;
    return 0;
}

static void credits_fill_bar(unsigned y0, unsigned y1, unsigned x0, unsigned x1,
                             uint8_t r, uint8_t g, uint8_t b) {
    unsigned x, y;
    for (y = y0; y < y1 && y < 224u; y++) {
        for (x = x0; x < x1 && x < 320u; x++) {
            uint8_t *p = &g_credits_rgba[(y * 320 + x) * 4];
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 255u;
        }
    }
}

/* §25: multi-page credits roll driven by DirNameTable campaign leaves. */
static void credits_build_frame(unsigned page) {
    unsigned x, y, i, n, start;
    unsigned scroll;
    char line[96];
    memset(g_credits_rgba, 0, sizeof g_credits_rgba);
    for (y = 0; y < 224; y++) {
        for (x = 0; x < 320; x++) {
            uint8_t *p = &g_credits_rgba[(y * 320 + x) * 4];
            p[0] = (uint8_t)(10 + (y >> 3));
            p[1] = (uint8_t)(8 + (page * 2u));
            p[2] = (uint8_t)(24 + (x >> 4));
            p[3] = 255u;
        }
    }
    credits_fill_bar(14, 34, 24, 296, 36, 28, 64);
    mb_font_draw_text_centered(g_credits_rgba, 320, 224, 16,
                               "MR. BONES",
                               255, 220, 72, 20, 10, 4, 2);
    mb_font_draw_text_centered(g_credits_rgba, 320, 224, 40,
                               "ZONO INC. / SEGA SATURN (1996)",
                               190, 210, 240, 12, 12, 24, 1);
    mb_font_draw_text_centered(g_credits_rgba, 320, 224, 52,
                               "CAMPAIGN STAGES (DIRNAMETABLE):",
                               240, 190, 110, 16, 12, 8, 1);

    n = stage_catalog_dir_count();
    start = 1u + (page % CREDITS_PAGES) * 5u; /* skip 03MENU; roll stages */
    scroll = (g_credits_frames / 8u) % 4u;
    for (i = 0; i < 5u; i++) {
        unsigned idx = start + i;
        unsigned yy = 68u + i * 22u + scroll;
        const char *leaf;
        if (idx >= n) break;
        leaf = stage_catalog_dir_name(idx);
        if (!leaf) continue;
        credits_fill_bar(yy, yy + 14u, 36u, 284u, 28, 34, 62);
        snprintf(line, sizeof line, "STAGE %02u: /%s", idx, leaf);
        mb_font_draw_text_shadow(g_credits_rgba, 320, 224, 48, (int)yy + 3,
                                 line,
                                 235, 240, 255, 12, 12, 24, 1);
    }
    credits_fill_bar(186, 200, 40, 280, 24, 64, 48);
    mb_font_draw_text_centered(g_credits_rgba, 320, 224, 189,
                               "THANK YOU FOR PLAYING!",
                               180, 255, 200, 10, 24, 16, 1);
    snprintf(line, sizeof line, "PAGE %u / %u   HOST %s",
             (page % CREDITS_PAGES) + 1u, CREDITS_PAGES, MB_HOST_VERSION);
    mb_font_draw_text_centered(g_credits_rgba, 320, 224, 206,
                               line,
                               210, 180, 120, 16, 12, 8, 1);

    snprintf(g_credits_status, sizeof g_credits_status,
             "CREDITS page %u/%u | DirNameTable | nvmshadow | host %s",
             (page % CREDITS_PAGES) + 1u, CREDITS_PAGES, MB_HOST_VERSION);
}

static void enter_credits(void) {
    g_credits_frames = 0;
    credits_build_frame(0);
    host_audio_stop(g_audio);
    g_audio = NULL;
    g_mode = APP_CREDITS;
    snprintf(g_title, sizeof g_title, "Mr. Bones - CREDITS");
}

/* After stage done: NvmShadow + next route. Returns 1 if another stage/credits. */
static int advance_after_stage(void) {
    stage_outcome oc;
    if (!g_stage) return 0;
    if (g_stage->special_live && g_stage->special.movie.decode_error) {
        fprintf(stderr, "movie decode failed (%s): %s\n", g_stage->iso_dir,
                g_stage->special.movie.status);
        g_movie_next_stage[0] = 0;
        g_mode = APP_DONE;
        return 0;
    }
    oc = stage_runner_outcome(g_stage);
    if (oc == STAGE_OUTCOME_NONE)
        oc = (g_stage->state == STAGE_RS_FINISHED) ? STAGE_OUTCOME_WIN
                                                   : STAGE_OUTCOME_LOSE;
    if (g_movie_next_stage[0]) {
        char next[STAGE_ISO_MAX];
        snprintf(next, sizeof next, "%s", g_movie_next_stage);
        g_movie_next_stage[0] = 0;
        if (oc == STAGE_OUTCOME_WIN && enter_stage(next) == 0) return 1;
        fprintf(stderr, "movie transition failed: %s\n", next);
        g_mode = APP_DONE;
        return 0;
    }
    {
        char movie_dir[STAGE_ISO_MAX] = {0};
        const char *expected_next = NULL;
        if (oc == STAGE_OUTCOME_WIN) {
            for (size_t i = 0; i < sizeof g_fmv_routes / sizeof g_fmv_routes[0]; i++) {
                if (!strcmp(g_stage->iso_dir, g_fmv_routes[i].from)) {
                    snprintf(movie_dir, sizeof movie_dir, "%s", g_fmv_routes[i].from);
                    expected_next = g_fmv_routes[i].to;
                    break;
                }
            }
        }
        stage_progress_on_outcome(&g_prog, g_stage->iso_dir, oc);
        if (expected_next && g_prog.route == STAGE_ROUTE_STAGE &&
            !strcmp(g_prog.next_iso, expected_next)) {
            if (enter_transition_movie(movie_dir, g_prog.next_iso) == 0) return 1;
            fprintf(stderr, "movie transition failed: %s: %s\n", movie_dir, g_stage->status);
            g_mode = APP_DONE;
            return 0;
        }
    }
    printf("%s\n", g_prog.status);
    if (g_prog.route == STAGE_ROUTE_COMPLETE) {
        enter_credits();
        return 1;
    }
    if (g_prog.route == STAGE_ROUTE_STAGE && g_prog.next_iso[0]) {
        if (enter_stage(g_prog.next_iso) == 0) return 1;
        fprintf(stderr, "advance failed: %s\n", g_prog.next_iso);
    }
    if (g_prog.route == STAGE_ROUTE_LOSE && g_prog.next_iso[0]) {
        if (enter_stage(g_prog.next_iso) == 0) return 1;
    }
    if (g_prog.route == STAGE_ROUTE_MENU && g_menu) {
        host_audio_stop(g_audio);
        g_audio = NULL;
        stage_runner_free(g_stage);
        menu_phase_free(g_menu);
        if (menu_phase_load(g_menu, g_cd, g_save_dir) == 0) {
            menu_phase_show_start(g_menu);
            g_mode = APP_MENU;
            g_movie_last_tick = 0;
            return 1;
        }
    }
    g_mode = APP_DONE;
    return 0;
}

static void paint(HWND window) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(window, &ps);
    RECT rect;
    int movie = (g_mode == APP_MENU && g_menu &&
                 (g_menu->screen == MENU_SCR_TITLE || g_menu->screen == MENU_SCR_STORY)) ||
                (g_mode == APP_STAGE && g_stage && g_stage->special_live &&
                 g_stage->special.mode == SPECIAL_MODE_PRESENT && g_stage->special.movie_ok);
    GetClientRect(window, &rect);
    FillRect(dc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(220, 220, 220));
    const char *help =
        g_mode == APP_MENU
            ? "MENU: Arrows | Enter select | Esc quit"
            : (g_mode == APP_CREDITS
                   ? "CREDITS: Esc quit"
                   : "STAGE: Arrows | X=jump | Down=tall | Enter=pause | Esc abort");
    if (!movie) TextOutA(dc, 12, 8, help, (int)strlen(help));
    const char *status = NULL;
    const uint8_t *frame = NULL;
    if (g_mode == APP_MENU && g_menu) {
        status = g_menu->status;
        frame = g_menu->frame_rgba;
    } else if (g_mode == APP_STAGE && g_stage) {
        status = stage_runner_status(g_stage);
        frame = stage_runner_frame(g_stage);
    } else if (g_mode == APP_CREDITS) {
        status = g_credits_status;
        frame = g_credits_rgba;
    }
    if (!movie && status) TextOutA(dc, 12, 28, status, (int)strlen(status));
    if (!movie && g_prog.status[0])
        TextOutA(dc, 12, 48, g_prog.status, (int)strlen(g_prog.status));
    if (frame) {
        static uint8_t bgra[320 * 224 * 4];
        size_t i;
        for (i = 0; i < 320u * 224u * 4u; i += 4u) {
            bgra[i + 0] = frame[i + 2];
            bgra[i + 1] = frame[i + 1];
            bgra[i + 2] = frame[i + 0];
            bgra[i + 3] = 255u;
        }
        BITMAPINFO info = {0};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 320;
        info.bmiHeader.biHeight = -224;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        int width = 640, height = 448, left = 20, top = 72;
        if (movie) {
            int cw = rect.right - rect.left, ch = rect.bottom - rect.top;
            width = cw;
            height = (int)((int64_t)cw * 224 / 320);
            if (height > ch) { height = ch; width = (int)((int64_t)ch * 320 / 224); }
            left = (cw - width) / 2;
            top = (ch - height) / 2;
        }
        StretchDIBits(dc, left, top, width, height, 0, 0, 320, 224,
                      bgra, &info, DIB_RGB_COLORS, SRCCOPY);
    }
    EndPaint(window, &ps);
}

static LRESULT CALLBACK wndproc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == 1) {
        static saturn_pad pad;
        ULONGLONG now;
        maybe_start_movie_audio();
        now = GetTickCount64();
        ULONGLONG delta = g_movie_last_tick ? now - g_movie_last_tick : 16;
        unsigned elapsed_us = (unsigned)(delta > 4294967u ? 4294967u : delta) * 1000u;
        if (!elapsed_us) elapsed_us = 1;
        g_movie_last_tick = now;
        if (g_movie_audio) {
            unsigned position_ms = 0;
            int state = host_audio_movie_clock(g_movie_audio, &position_ms);
            if (state < 0) {
                fprintf(stderr, "movie audio device or clock failed\n");
                host_audio_stop(g_movie_audio);
                g_movie_audio = NULL;
                g_movie_audio_failed = 1;
            } else if (state == 0) elapsed_us = 0;
            else if (state == 1 || (state == 2 && position_ms > g_movie_audio_position_ms)) {
                unsigned change = position_ms >= g_movie_audio_position_ms
                                      ? position_ms - g_movie_audio_position_ms : 0;
                elapsed_us = change > 4294967u ? 4294967000u : change * 1000u;
                g_movie_audio_position_ms = position_ms;
            }
        }
        saturn_pad_update_host(&pad, GetForegroundWindow() == window
            ? host_input_keyboard_buttons() | g_pending_keys : 0);
        g_pending_keys = 0;
        if (g_mode == APP_MENU && g_menu && !menu_phase_done(g_menu)) {
            g_menu->title_avi.tick_us = elapsed_us;
            g_menu->title_avi.tick_external = 1;
            menu_phase_step(g_menu, &pad);
            if (g_menu->title_avi.decode_error) {
                fprintf(stderr, "movie decode failed (menu): %s\n", g_menu->title_avi.status);
                if (g_movie_audio) { host_audio_stop(g_movie_audio); g_movie_audio = NULL; }
            }
            if (g_menu->screen == MENU_SCR_TITLE || g_menu->screen == MENU_SCR_STORY) {
                if (g_audio) { host_audio_stop(g_audio); g_audio = NULL; }
                maybe_play_sfx();
            } else {
                if (g_movie_audio) { host_audio_stop(g_movie_audio); g_movie_audio = NULL; }
                g_movie_audio_failed = 0;
                if (g_menu->music_on && !g_audio)
                    g_audio = host_audio_start_menu_cdda(g_cd, 2);
                else if (!g_menu->music_on && g_audio) {
                    host_audio_stop(g_audio);
                    g_audio = NULL;
                }
            }
            snprintf(g_title, sizeof g_title, "Mr. Bones - %s%s",
                     g_movie_audio_failed ? "[FMV audio unavailable] " : "", g_menu->status);
            SetWindowTextA(window, g_title);
            if (menu_phase_done(g_menu) && g_menu->state == MENU_ST_START_LEVEL)
                PostMessageA(window, WM_USER + 1, 0, 0);
        } else if (g_mode == APP_STAGE && g_stage && !stage_runner_done(g_stage)) {
            if (g_stage->special_live) {
                g_stage->special.movie.tick_us = elapsed_us;
                g_stage->special.movie.tick_external = 1;
            }
            stage_runner_step(g_stage, &pad);
            maybe_play_sfx();
            if (g_movie_audio && g_stage->special_live)
                host_audio_movie_pause(g_movie_audio, g_stage->special.state == SPECIAL_ST_PAUSED);
            snprintf(g_title, sizeof g_title, "Mr. Bones - %s%s",
                     g_movie_audio_failed ? "[FMV audio unavailable] " : "", stage_runner_status(g_stage));
            SetWindowTextA(window, g_title);
            if (stage_runner_done(g_stage)) {
                host_audio_stop(g_movie_audio);
                g_movie_audio = NULL;
                PostMessageA(window, WM_USER + 2, 0, 0);
            }
        } else if (g_mode == APP_CREDITS) {
            g_credits_frames++;
            credits_build_frame(g_credits_frames / (CREDITS_HOLD_FRAMES / CREDITS_PAGES));
            snprintf(g_title, sizeof g_title, "Mr. Bones - CREDITS %u/%u",
                     g_credits_frames, CREDITS_HOLD_FRAMES);
            SetWindowTextA(window, g_title);
            if (g_credits_frames >= CREDITS_HOLD_FRAMES) {
                g_mode = APP_DONE;
                KillTimer(window, 1);
            }
        }
        InvalidateRect(window, NULL, FALSE);
        return 0;
    }
    if (msg == WM_USER + 1) { SetTimer(window, 1, 16, NULL); return 0; }
    if (msg == WM_USER + 2) {
        if (!advance_after_stage())
            KillTimer(window, 1);
        else
            SetTimer(window, 1, 16, NULL);
        return 0;
    }
    if (msg == WM_KEYDOWN && !(lp & (1L << 30)))
        g_pending_keys |= host_input_key_button((unsigned)wp);
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_PAINT) { paint(window); return 0; }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(window, msg, wp, lp);
}

static void usage(void) {
    fprintf(stderr,
            "mrbones host %s — native Mr. Bones campaign (no SH-2 emu)\n"
            "usage: mrbones <disc1.cue|bin> <manifest>\n"
            "              [--disc2 cue] [--check] [--full-campaign]\n"
            "              [--save-dir DIR] [--skip-menu] [--level /NNNAME]\n"
            "              [--campaign] [--version]\n",
            MB_HOST_VERSION);
}

/* Drive one native stage to EndLevel. Rejects smoke. */
static int play_native_stage(stage_runner *stage, const char *iso, int max_frames) {
    saturn_pad pad = {0};
    int i;
    if (enter_stage(iso)) {
        fprintf(stderr, "full: begin failed %s\n", iso);
        return -1;
    }
    if (stage->kind == STAGE_KIND_EMPTY) {
        /* Instant skip — progress should already skip these. */
        printf("  SKIP %s [empty]\n", iso);
        return 0;
    }
    if (!stage_is_native_live(stage)) {
        fprintf(stderr, "full: %s is smoke/non-native (%s)\n", iso, stage->status);
        return -1;
    }
    if (stage->special_live && stage->special.mode == SPECIAL_MODE_PRESENT &&
        stage->special.movie_ok) {
        stage->special.movie.tick_external = 1;
        stage->special.movie.tick_us = stage->special.movie.us_per_frame + 1u;
        if (stage->special.movie.frame_count < 1000000u &&
            stage->special.movie.frame_count + 2u > (unsigned)max_frames)
            max_frames = (int)stage->special.movie.frame_count + 2;
    }
    for (i = 0; i < max_frames && !stage_runner_done(stage); i++) {
        if (stage->runway_live || stage->sidescrl_live) {
            saturn_pad_update_host(&pad, MB_PAD_RIGHT);
        } else if (stage->special_live) {
            unsigned btn = MB_PAD_RIGHT;
            if (stage->special.mode == SPECIAL_MODE_RHYTHM) {
                int period = stage->special.beat_period
                                 ? (int)stage->special.beat_period
                                 : (int)SPECIAL_BEAT_PERIOD;
                if ((i % period) <= 2) btn |= MB_PAD_A;
            }
            saturn_pad_update_host(&pad, btn);
        }
        stage_runner_step(stage, &pad);
        maybe_play_sfx();
    }
    if (!stage_runner_done(stage)) {
        fprintf(stderr, "full: %s did not finish (%s)\n", iso, stage->status);
        return -1;
    }
    printf("  OK %s disc=%d live=%s outcome=%d %s\n", iso, g_mounted_disc,
           stage->runway_live ? "runway"
                              : (stage->sidescrl_live ? "sidescrl" : "special"),
           (int)stage_runner_outcome(stage), stage->status);
    return 0;
}

static int nvmshadow_file_exists(void) {
    char path[MB_PATH_MAX + 32];
    FILE *f;
    snprintf(path, sizeof path, "%s/nvmshadow.bin", g_save_dir);
    f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/*
 * §19 headless: MENU STXT → campaign chain (native only) → INSERTCD → FINALE →
 * credits → nvmshadow.bin. Returns 0 on success.
 */
static int full_campaign_mode(menu_phase *menu, stage_runner *stage) {
    saturn_pad pad = {0};
    const char *iso;
    int error = 0;
    unsigned played = 0;
    int saw_disc1 = 0, saw_disc2 = 0;
    int menu_frames;

    if (menu_phase_load(menu, g_cd, g_save_dir)) {
        fprintf(stderr, "full: menu load failed\n");
        return 1;
    }
    g_menu = menu;
    if (!menu->frame_rgba || menu->gfx_count == 0) {
        /* Soft: STXT may fail on odd dumps; still require a frame buffer. */
        if (!menu->frame_rgba) {
            fprintf(stderr, "full: MENU missing visual frame\n");
            error = 1;
            goto done;
        }
    }
    /* Hold MENU a few frames so STXT blit is exercised. */
    for (menu_frames = 0; menu_frames < 12; menu_frames++) {
        saturn_pad_update_host(&pad, 0);
        menu_phase_step(menu, &pad);
    }
    saturn_pad_update_host(&pad, MB_PAD_START);
    menu_phase_step(menu, &pad);
    saturn_pad_update_host(&pad, 0);
    menu_phase_step(menu, &pad);
    saturn_pad_update_host(&pad, MB_PAD_START);
    menu_phase_step(menu, &pad);
    saturn_pad_update_host(&pad, 0);
    menu_phase_step(menu, &pad);
    saturn_pad_update_host(&pad, MB_PAD_START);
    menu_phase_step(menu, &pad);
    iso = menu_phase_selected_iso(menu);
    printf("mrbones MENU visual: %s gfx=%u\n", menu->status, menu->gfx_count);
    if (menu->state != MENU_ST_START_LEVEL || !iso || !strstr(iso, "RUNWAY")) {
        fprintf(stderr, "full: menu did not select RUNWAY\n");
        error = 1;
        goto done;
    }

    g_nvm = menu->nvm;
    stage_progress_init(&g_prog, &g_catalog, &g_nvm, g_save_dir);
    if (stage_progress_begin(&g_prog, iso)) {
        fprintf(stderr, "full: progress begin failed\n");
        error = 1;
        goto done;
    }

    printf("full-campaign walk (native phases only, skip EMPTY):\n");
    while (g_prog.route == STAGE_ROUTE_STAGE && g_prog.next_iso[0]) {
        const stage_info *info = stage_catalog_find(&g_catalog, g_prog.next_iso);
        if (play_native_stage(stage, g_prog.next_iso, STAGE_MAX_FRAMES)) {
            error = 1;
            goto done;
        }
        if (info && info->kind != STAGE_KIND_EMPTY && !stage_is_native_live(stage)) {
            fprintf(stderr, "full: non-native stage %s\n", g_prog.next_iso);
            error = 1;
            goto done;
        }
        if (g_mounted_disc == 1) saw_disc1 = 1;
        if (g_mounted_disc == 2) saw_disc2 = 1;
        if (stage_runner_outcome(stage) != STAGE_OUTCOME_WIN &&
            stage->kind != STAGE_KIND_EMPTY) {
            fprintf(stderr, "full: expected WIN on %s\n", stage->iso_dir);
            error = 1;
            goto done;
        }
        played++;
        stage_progress_on_outcome(&g_prog, stage->iso_dir, STAGE_OUTCOME_WIN);
        printf("  %s\n", g_prog.status);
    }

    if (g_prog.route != STAGE_ROUTE_COMPLETE) {
        fprintf(stderr, "full: expected COMPLETE after FINALE (route=%d)\n",
                (int)g_prog.route);
        error = 1;
        goto done;
    }
    if (played < 20) {
        fprintf(stderr, "full: too few stages played (%u)\n", played);
        error = 1;
        goto done;
    }
    if (!saw_disc1) {
        fprintf(stderr, "full: never played on disc 1\n");
        error = 1;
        goto done;
    }
    if (g_disc2_path[0] && !saw_disc2) {
        fprintf(stderr, "full: never INSERTCD to disc 2\n");
        error = 1;
        goto done;
    }

    enter_credits();
    {
        unsigned c;
        for (c = 0; c < CREDITS_HOLD_FRAMES; c++) {
            g_credits_frames = c;
            credits_build_frame(c / (CREDITS_HOLD_FRAMES / CREDITS_PAGES));
        }
    }
    printf("CREDITS roll %u frames × %u pages (DirNameTable)\n",
           CREDITS_HOLD_FRAMES, CREDITS_PAGES);
    g_mode = APP_DONE;

    if (!nvmshadow_file_exists()) {
        /* Force final persist if dirty flag cleared oddly. */
        g_nvm.dirty = 1;
        if (mb_nvm_save(&g_nvm, g_save_dir) || !nvmshadow_file_exists()) {
            fprintf(stderr, "full: nvmshadow.bin missing under %s\n", g_save_dir);
            error = 1;
            goto done;
        }
    }

    printf("nvm save_dir=%s level_word=%u unlocks_ok file=nvmshadow.bin\n",
           g_save_dir,
           (unsigned)mb_nvm_get_u32(&g_nvm, MB_NVM_DEFAULT_LEVEL_OFF));
    printf("full-campaign: MENU→%u stages→INSERTCD→FINALE→CREDITS OK\n", played);

done:
    host_audio_stop(g_audio);
    g_audio = NULL;
    runway_sfx_free(&g_sfx);
    stage_runner_free(stage);
    menu_phase_free(menu);
    g_menu = NULL;
    g_stage = NULL;
    return error;
}

static int check_mode(menu_phase *menu, stage_runner *stage) {
    int error = 0;

    /* §19 primary path: complete native campaign. */
    error = full_campaign_mode(menu, stage);
    if (error) return error;

    /* Re-init stage/menu for LOSE / DUKMOVIE stand-in. */
    memset(menu, 0, sizeof *menu);
    memset(stage, 0, sizeof *stage);
    g_stage = stage;
    g_menu = NULL;
    if (remount_disc(1)) return 1;
    mb_nvm_load(&g_nvm, g_save_dir);
    stage_progress_init(&g_prog, &g_catalog, &g_nvm, g_save_dir);

    /* Each transition movie belongs to the stage just completed. */
    {
        static const struct { const char *movie; const char *next; int disc; } movies[] = {
            {"/06VALLEY", "/07GUITAR", 1}, {"/07GUITAR", "/08BAT1", 1},
            {"/11LOGS", "/14DRUM", 1}, {"/14DRUM", "/15GLASS", 1},
            {"/16BIGBNS", "/17VORTEX", 1}, {"/17VORTEX", "/18RIDE", 1},
            {"/18RIDE", "/19VILLAGE", 2}, {"/19VILLAGE", "/20ICE1", 2},
            {"/22SHADOW", "/23MOAT", 2}, {"/23MOAT", "/24STAING", 2},
            {"/24STAING", "/25GENRM", 2}, {"/25GENRM", "/26LAFF", 2},
            {"/26LAFF", "/27FINALE", 2}
        };
        for (unsigned i = 0; i < sizeof movies / sizeof movies[0]; i++) {
            saturn_pad pad = {0};
            unsigned limit;
            stage_runner_free(stage);
            if (remount_disc(movies[i].disc)) return 1;
            stage->state = STAGE_RS_FINISHED;
            stage->outcome = STAGE_OUTCOME_WIN;
            snprintf(stage->iso_dir, sizeof stage->iso_dir, "%s", movies[i].movie);
            g_mode = APP_STAGE;
            if (!advance_after_stage() || !stage->special_live || stage->kind != STAGE_KIND_MOVIE ||
                strcmp(stage->iso_dir, movies[i].movie) || !stage->special.movie.pcm_samples ||
                strcmp(g_movie_next_stage, movies[i].next)) {
                fprintf(stderr, "check: %s movie transition failed\n", movies[i].movie);
                return 1;
            }
            if (i < 2) {
                limit = stage->special.movie.frame_count + 2;
                stage->special.movie.tick_external = 1;
                stage->special.movie.tick_us = stage->special.movie.us_per_frame + 1u;
                while (limit-- && !stage_runner_done(stage)) stage_runner_step(stage, &pad);
                if (stage->special.movie.frames_drawn != stage->special.movie.frame_count) {
                    fprintf(stderr, "check: %s movie incomplete\n", movies[i].movie);
                    return 1;
                }
            } else {
                saturn_pad_update_host(&pad, MB_PAD_START);
                stage_runner_step(stage, &pad);
            }
            if (!stage_runner_done(stage) || !advance_after_stage() || !stage_is_native_live(stage) ||
                strcmp(stage->iso_dir, movies[i].next)) {
                fprintf(stderr, "check: movie to %s transition failed\n", movies[i].next);
                return 1;
            }
        }
        if (remount_disc(1)) return 1;
    }
    printf("check: 13 FMV transitions OK\n");

    {
        static const uint8_t invalid[] = {0, 0, 0};
        saturn_pad pad = {0};
        stage_runner_free(stage);
        if (enter_stage("/28LOSE") || !stage->special_live ||
            stage->special.movie.cur >= stage->special.movie.frame_count) return 1;
        {
            unsigned rate = stage->special.movie.pcm_rate;
            stage->special.movie.pcm_rate = 1;
            maybe_start_movie_audio();
            stage->special.movie.pcm_rate = rate;
            if (!g_movie_audio_failed || g_movie_audio || stage->special.movie_pcm_pending) {
                fprintf(stderr, "check: movie audio failure was not reported\n");
                return 1;
            }
        }
        stage->special.movie.frames[stage->special.movie.cur].data = invalid;
        stage->special.movie.frames[stage->special.movie.cur].size = sizeof invalid;
        stage->special.movie.tick_external = 1;
        stage->special.movie.tick_us = stage->special.movie.us_per_frame + 1u;
        stage_runner_step(stage, &pad);
        if (!stage->special.movie.decode_error || !stage_runner_done(stage) ||
            advance_after_stage() || g_mode != APP_DONE) {
            fprintf(stderr, "check: invalid LOSE movie was not stopped\n");
            return 1;
        }
        stage_runner_free(stage);
    }

    if (play_native_stage(stage, "/28LOSE", STAGE_MAX_FRAMES)) {
        fprintf(stderr, "check: LOSE special_phase failed\n");
        return 1;
    }
    {
        size_t avi = stage->special_live ? stage->special.avi_size : 0;
        size_t duck = stage->special_live ? stage->special.duck_size : 0;
        if (avi == 0) {
            fprintf(stderr, "check: LOSE missing 20AVI\n");
            return 1;
        }
        printf("DUKMOVIE stand-in: LOSE avi=%zu duck=%zu\n", avi, duck);
    }

    stage_progress_on_outcome(&g_prog, "/04RUNWAY", STAGE_OUTCOME_LOSE);
    if (g_prog.route != STAGE_ROUTE_LOSE || strcmp(g_prog.next_iso, "/28LOSE")) {
        fprintf(stderr, "check: lose route failed\n");
        return 1;
    }

    printf("check: §25 host %s DirNameTable + title AVI + credits 1.0 OK\n",
           MB_HOST_VERSION);
    runway_sfx_free(&g_sfx);
    stage_runner_free(stage);
    g_stage = NULL;
    return 0;
}

int main(int argc, char **argv) {
    int check_only = 0, full_campaign = 0, skip_menu = 0, campaign = 0, error = 0;
    int i;
    char err[512];
    char level_override[32];
    id_manifest manifest;
    id_result result;
    menu_phase menu;
    stage_runner stage;
    mb_config cfg;

    level_override[0] = 0;
    g_disc2_path[0] = 0;
    g_have_disc2 = 0;
    g_audio = NULL;
    memset(&g_cd2_storage, 0, sizeof g_cd2_storage);

    if (argc >= 2 && !strcmp(argv[1], "--version")) {
        printf("mrbones %s\n", MB_HOST_VERSION);
        return 0;
    }
    config_load(&cfg, NULL);
    snprintf(g_save_dir, sizeof g_save_dir, "%s", cfg.save_dir);
    if (argc == 1) {
        snprintf(g_cue1, sizeof g_cue1, "%s",
                 "F:/MrBonesPort/disco1/Mr. Bones (USA) (Disc 1).cue");
        snprintf(g_disc2_path, sizeof g_disc2_path, "%s",
                 "F:/MrBonesPort/disco2/Mr. Bones (USA) (Disc 2).cue");
    } else if (argc < 3) {
        usage();
        return 2;
    } else {
        snprintf(g_cue1, sizeof g_cue1, "%s", argv[1]);
    }
    for (i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--check")) check_only = 1;
        else if (!strcmp(argv[i], "--full-campaign")) full_campaign = 1;
        else if (!strcmp(argv[i], "--skip-menu")) skip_menu = 1;
        else if (!strcmp(argv[i], "--campaign")) campaign = 1;
        else if (!strcmp(argv[i], "--version")) {
            printf("mrbones %s\n", MB_HOST_VERSION);
            return 0;
        }
        else if (!strcmp(argv[i], "--save-dir") && i + 1 < argc)
            snprintf(g_save_dir, sizeof g_save_dir, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--disc2") && i + 1 < argc)
            snprintf(g_disc2_path, sizeof g_disc2_path, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--level") && i + 1 < argc)
            snprintf(level_override, sizeof level_override, "%s", argv[++i]);
        else { usage(); return 2; }
    }

    {
        const char *manifest_path =
            (argc >= 3) ? argv[2] : "F:/MrBonesPort/manifest/versions.txt";
        if (manifest_load(&manifest, manifest_path, err, sizeof err)) {
            fprintf(stderr, "manifest: %s\n", err);
            return 1;
        }
    }
    if (cd_open(&g_cd_storage, g_cue1, err, sizeof err)) {
        fprintf(stderr, "disc: %s\n", err); manifest_free(&manifest); return 1;
    }
    g_cd = &g_cd_storage;
    g_mounted_disc = 1;
    if (!identity_check(&manifest, g_cd, 1, ID_QUICK, &result)) {
        fprintf(stderr, "identity: %s\n", result.message);
        cd_close(g_cd); manifest_free(&manifest); return 1;
    }
    if (g_disc2_path[0]) {
        if (cd_open(&g_cd2_storage, g_disc2_path, err, sizeof err)) {
            fprintf(stderr, "disc2: %s\n", err);
            cd_close(g_cd); manifest_free(&manifest); return 1;
        }
        if (!identity_check(&manifest, &g_cd2_storage, 2, ID_QUICK, &result)) {
            fprintf(stderr, "identity disc2: %s\n", result.message);
            cd_close(&g_cd2_storage); cd_close(g_cd);
            manifest_free(&manifest); return 1;
        }
        g_have_disc2 = 1;
    }
    manifest_free(&manifest);

    if (stage_catalog_build(&g_catalog, g_cd,
                            g_have_disc2 ? &g_cd2_storage : NULL)) {
        fprintf(stderr, "stage catalog empty\n");
        error = 1;
        goto shutdown;
    }
    printf("catalog: %u stages, campaign=%u\n", g_catalog.count,
           g_catalog.campaign_count);

    mb_nvm_load(&g_nvm, g_save_dir);
    stage_progress_init(&g_prog, &g_catalog, &g_nvm, g_save_dir);

    memset(&menu, 0, sizeof menu);
    memset(&stage, 0, sizeof stage);
    g_menu = &menu;
    g_stage = &stage;

    if (check_only || full_campaign) {
        if (full_campaign && !check_only)
            error = full_campaign_mode(&menu, &stage);
        else
            error = check_mode(&menu, &stage);
        goto shutdown;
    }

    if (skip_menu || level_override[0] || campaign) {
        const char *iso = level_override[0] ? level_override : NULL;
        if (stage_progress_begin(&g_prog, iso)) {
            fprintf(stderr, "progress begin failed\n");
            error = 1;
            goto shutdown;
        }
        if (enter_stage(g_prog.next_iso)) {
            fprintf(stderr, "stage %s load failed\n", g_prog.next_iso);
            error = 1;
            goto shutdown;
        }
    } else {
        if (menu_phase_load(&menu, g_cd, g_save_dir)) {
            fprintf(stderr, "MENU load failed: %s\n", menu.status);
            error = 1;
            goto shutdown;
        }
        g_menu = &menu;
        g_nvm = menu.nvm;
        stage_progress_init(&g_prog, &g_catalog, &g_nvm, g_save_dir);
        g_mode = APP_MENU;
    }

    {
        HINSTANCE instance = GetModuleHandleA(NULL);
        WNDCLASSA wc = {0};
        HWND window;
        MSG msg;
        wc.lpfnWndProc = wndproc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = "MrBonesIntegrated";
        if (!RegisterClassA(&wc)) { error = 1; goto shutdown; }
        window = CreateWindowA(wc.lpszClassName, "Mr. Bones",
                               WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               CW_USEDEFAULT, CW_USEDEFAULT, 700, 580,
                               NULL, NULL, instance, NULL);
        if (!window) { error = 1; goto shutdown; }
        SetTimer(window, 1, 16, NULL);
        while (GetMessageA(&msg, NULL, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            if (g_mode == APP_MENU && g_menu &&
                g_menu->state == MENU_ST_START_LEVEL) {
                const char *sel = menu_phase_selected_iso(g_menu);
                g_nvm = g_menu->nvm;
                stage_progress_init(&g_prog, &g_catalog, &g_nvm, g_save_dir);
                if (sel && stage_progress_begin(&g_prog, sel) == 0 &&
                    enter_stage(g_prog.next_iso) == 0) {
                    SetTimer(window, 1, 16, NULL);
                } else if (g_menu) {
                    snprintf(g_menu->status, sizeof g_menu->status,
                             "MENU level load failed: %s", sel ? sel : "?");
                    g_menu->state = MENU_ST_ACTIVE;
                    g_menu->screen = MENU_SCR_LEVELS;
                }
            }
            if (g_mode == APP_DONE) {
                Sleep(200);
                break;
            }
        }
    }

shutdown:
    host_audio_stop(g_movie_audio);
    g_movie_audio = NULL;
    host_audio_stop(g_audio);
    g_audio = NULL;
    runway_sfx_free(&g_sfx);
    stage_runner_free(&stage);
    menu_phase_free(&menu);
    if (g_nvm.dirty)
        mb_nvm_save(&g_nvm, g_save_dir);
    g_menu = NULL;
    g_stage = NULL;
    if (g_have_disc2) cd_close(&g_cd2_storage);
    cd_close(&g_cd_storage);
    g_cd = NULL;
    return error ? 1 : 0;
}
