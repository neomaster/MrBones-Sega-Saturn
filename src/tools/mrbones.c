/*
 * Integrated Mr. Bones host (§19–§25 / 1.0.0): MENU title AVI → full campaign
 * (native) → INSERTCD → FINALE → MENU; HUD; CD-DA + 05SND1; duck_avi;
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
#include "core/stage_enemy.h"
#include "core/stage_progress.h"
#include "core/stage_runner.h"
#include "platform/config.h"
#include "platform/host_audio.h"
#include "platform/host_input.h"
#include <errno.h>
#include <windows.h>
#include <mmsystem.h>
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
#define FINALE_MENU_MUSIC_DELAY_MS 4540u

static app_mode g_mode = APP_MENU;
/* --check / --full-campaign: FINALE keeps movie_ok from load (hashes). */
static int g_headless_finale_movie;
static int g_mute_audio;
static int g_autoplay_mode;
static char g_capture_dir[MB_PATH_MAX];
static unsigned g_capture_seq;
static menu_phase *g_menu;
static stage_runner *g_stage;
static stage_progress g_prog;
static mb_nvm_shadow g_nvm;
static runway_sfx g_sfx;
static int g_sfx_ok;
static host_audio *g_audio;
/* Host menu options. They persist in mrbones.ini, never in nvmshadow.bin: the
 * original keeps its option toggles in loader RAM and does not save them
 * (docs/stage-progress-nvm.md section 8). */
static int g_opt_music = 1, g_opt_stereo = 0;
static unsigned g_opt_volume = MB_CONFIG_VOLUME_MAX;
static char g_config_path[MB_PATH_MAX];

static void apply_host_options(menu_phase *menu) {
    menu->music_on = g_opt_music;
    menu->stereo_on = g_opt_stereo;
}

/* After each interactive menu step: write the ini only when a value changed. */
static void store_host_options(const menu_phase *menu, unsigned volume) {
    if (!!menu->music_on == g_opt_music && !!menu->stereo_on == g_opt_stereo &&
        volume == g_opt_volume)
        return;
    g_opt_music = !!menu->music_on;
    g_opt_stereo = !!menu->stereo_on;
    g_opt_volume = volume;
    if (g_config_path[0] &&
        config_save_options(g_config_path, g_opt_music, g_opt_stereo, g_opt_volume))
        fprintf(stderr, "options not saved: cannot write %s\n", g_config_path);
}
static unsigned g_menu_volume = 15;
static ULONGLONG g_menu_music_after;
static host_audio *g_movie_audio;
static int g_movie_audio_failed;
static int g_reset_qpc = 0;
/* Set only around the --check corrupt-movie negative tests: silences the
 * expected movie failure lines; status/control flow are unchanged. */
static int g_expect_movie_error;
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
static mb_upscale_mode g_upscale_mode = MB_UPSCALE_NEAREST;
static unsigned g_upscale_factor = 3u;
static int g_hd_bones = 0;
static int g_fullscreen = 0;
static WINDOWPLACEMENT g_wp_prev;
static char g_hd_bones_dir[MB_PATH_MAX];

static void toggle_fullscreen(HWND window) {
    LONG_PTR style = GetWindowLongPtrA(window, GWL_STYLE);
    if (!g_fullscreen) {
        MONITORINFO mi;
        memset(&mi, 0, sizeof mi);
        mi.cbSize = sizeof(mi);
        g_wp_prev.length = sizeof(g_wp_prev);
        if (GetWindowPlacement(window, &g_wp_prev) &&
            GetMonitorInfoA(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLongPtrA(window, GWL_STYLE, style & ~(LONG_PTR)WS_OVERLAPPEDWINDOW);
            SetWindowPos(window, HWND_TOP,
                         mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            g_fullscreen = 1;
        }
    } else {
        SetWindowLongPtrA(window, GWL_STYLE, style | (LONG_PTR)WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(window, &g_wp_prev);
        SetWindowPos(window, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_fullscreen = 0;
    }
}

static unsigned effective_upscale_scale(void) {
    if (g_upscale_mode == MB_UPSCALE_NEAREST) {
        return (g_upscale_factor >= 1u && g_upscale_factor <= 4u)
                   ? g_upscale_factor : 3u;
    }
    if ((g_upscale_mode == MB_UPSCALE_SCALE2X ||
         g_upscale_mode == MB_UPSCALE_XBR2X) && g_upscale_factor == 4u) {
        return 4u;
    }
    return mb_upscale_mode_factor(g_upscale_mode);
}

static int find_bundled_entry(char *out, size_t size, const char *relative,
                              int want_dir) {
    char dir[MB_PATH_MAX], *slash;
    DWORD len = GetModuleFileNameA(NULL, dir, (DWORD)sizeof dir);
    if (!len || len >= sizeof dir) return -1;
    slash = strrchr(dir, '\\');
    if (!slash) return -1;
    *slash = 0;
    for (int i = 0; i < 2; i++) {
        int n = snprintf(out, size, "%s\\%s", dir, relative);
        DWORD attrs;
        if (n < 0 || (size_t)n >= size) return -1;
        attrs = GetFileAttributesA(out);
        if (attrs != INVALID_FILE_ATTRIBUTES &&
            (((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) == (want_dir != 0)))
            return 0;
        slash = strrchr(dir, '\\');
        if (!slash) break;
        *slash = 0;
    }
    return -1;
}

static int find_bundled_file(char *out, size_t size, const char *relative) {
    return find_bundled_entry(out, size, relative, 0);
}

static int find_bundled_dir(char *out, size_t size, const char *relative) {
    return find_bundled_entry(out, size, relative, 1);
}
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
        /* Title FMV replaces any leftover stage CD-DA. */
        host_audio_stop(g_audio);
        g_audio = NULL;
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
    if (g_audio && g_stage->special_live && g_stage->special.finale_movie_preroll) {
        /* The CD drive leaves the arena track to read 19DUCK at the request.
         * The AVI's PCM is armed independently when duckAudioStart is reached. */
        host_audio_stop(g_audio);
        g_audio = NULL;
    }
    if (g_stage->special_live && g_stage->special.movie_pcm_pending &&
        g_stage->special.movie.pcm && g_stage->special.movie.pcm_samples > 0) {
        /* Arena→PlayDuckMovie(3): stop FINALE CD-DA before ending-movie PCM. */
        host_audio_stop(g_audio);
        g_audio = NULL;
        host_audio_stop(g_movie_audio);
        g_movie_audio = host_audio_start_pcm16(g_stage->special.movie.pcm,
            g_stage->special.movie.pcm_samples, g_stage->special.movie.pcm_rate,
            g_stage->special.movie.pcm_channels);
        g_movie_audio_failed = !g_movie_audio;
        if (g_movie_audio) { g_movie_last_tick = GetTickCount64(); g_movie_audio_position_ms = 0; }
        else if (!g_expect_movie_error)
            fprintf(stderr, "movie audio unavailable (%s)\n", g_stage->iso_dir);
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
        /* FINALE ShootRing: prefer Saturn SoundEvent id → span4 PCM. */
        if (g_stage->special.ring_sfx_event) {
            const int8_t *pcm = NULL;
            size_t len = 0;
            unsigned id = g_stage->special.ring_sfx_event;
            g_stage->special.ring_sfx_event = 0;
            runway_sfx_sound_event(&g_sfx, id);
            if (runway_sfx_event_pcm(&g_sfx, id, &pcm, &len) == 0)
                host_play_pcm(NULL, pcm, len, RUNWAY_SFX_RATE);
            else
                flags = g_stage->special.sfx_flags;
        } else {
            flags = g_stage->special.sfx_flags;
        }
    }
    if (flags)
        runway_sfx_dispatch_pcm(&g_sfx, flags, host_play_pcm, NULL);
}

static void restart_stage_audio(void) {
    int track;
    host_audio_stop(g_audio);
    g_audio = NULL;
    if (!g_opt_music) return;
    /* Skip CD-DA only while an ending/present movie is actually playing.
     * Deferred FINALE (!movie_ok) still has pcm_samples armed — keep arena BGM. */
    if (g_stage && g_stage->special_live &&
        g_stage->special.mode == SPECIAL_MODE_PRESENT &&
        g_stage->special.movie_ok &&
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
    g_menu_music_after = 0;
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
    /* Interactive FINALE: arena first, then PlayDuckMovie(3) on ring clear. */
    if (!g_headless_finale_movie && g_stage->special_live &&
        g_stage->kind == STAGE_KIND_FINALE)
        (void)special_phase_finale_defer_ending(&g_stage->special);
    g_sfx_ok = 0;
    runway_sfx_free(&g_sfx);
    if (stage_is_native_live(g_stage))
        g_sfx_ok = runway_sfx_load_dir(&g_sfx, g_cd, iso_dir) == 0;
    restart_stage_audio();
    g_mode = APP_STAGE;
    g_reset_qpc = 1;
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
    int finale_to_menu = 0;
    if (!g_stage) return 0;
    if (g_stage->special_live && g_stage->special.movie.decode_error) {
        if (!g_expect_movie_error)
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
    if (g_prog.route == STAGE_ROUTE_COMPLETE && g_stage->kind == STAGE_KIND_FINALE) {
        g_prog.route = STAGE_ROUTE_MENU;
        finale_to_menu = 1;
        snprintf(g_prog.status, sizeof g_prog.status, "PROGRESS FINALE -> MENU");
    }
    printf("%s\n", g_prog.status);
    /* Unreachable in a real playthrough: stage_progress_on_outcome only ever
     * returns STAGE_ROUTE_COMPLETE for STAGE_KIND_FINALE (28LOSE now routes
     * straight to MENU, docs/stage-progress-nvm.md section 9), and that case
     * is always caught by the override above. Kept only for the defensive
     * "no next stage after a non-FINALE win" fallback in stage_progress.c,
     * which the hardcoded 28-entry catalog never actually produces. See
     * docs/fmv-routes.md section 8 for why enter_credits() must not run on a
     * real FINALE win: the original's end credits are rendered frames inside
     * /27FINALE/20AVI itself, not a separate data table this host screen
     * could reconstruct. */
    if (g_prog.route == STAGE_ROUTE_COMPLETE) {
        enter_credits();
        return 1;
    }
    if (g_prog.route == STAGE_ROUTE_STAGE && g_prog.next_iso[0]) {
        if (enter_stage(g_prog.next_iso) == 0) return 1;
        fprintf(stderr, "advance failed: %s\n", g_prog.next_iso);
    }
    if (g_prog.route == STAGE_ROUTE_LOSE && g_prog.next_iso[0]) {
        host_audio_fade_stop(g_audio, 280);
        g_audio = NULL;
        if (enter_stage(g_prog.next_iso) == 0) return 1;
    }
    if (g_prog.route == STAGE_ROUTE_MENU && g_menu) {
        host_audio_stop(g_audio);
        g_audio = NULL;
        stage_runner_free(g_stage);
        menu_phase_free(g_menu);
        if (menu_phase_load(g_menu, g_cd, g_save_dir) == 0) {
            apply_host_options(g_menu);
            g_menu->volume = g_menu_volume;
            menu_phase_show_start(g_menu);
            g_mode = APP_MENU;
            g_movie_last_tick = 0;
            g_reset_qpc = 1;
            /* Match the Ymir-observed pause after the FINALE soundtrack. */
            g_menu_music_after = finale_to_menu
                ? GetTickCount64() + FINALE_MENU_MUSIC_DELAY_MS : 0;
            return 1;
        }
    }
    g_mode = APP_DONE;
    return 0;
}

static int current_host_frame(int *out_movie, const char **out_help,
                              const char **out_status, const uint8_t **out_frame) {
    int movie = (g_mode == APP_MENU && g_menu &&
                 (g_menu->screen == MENU_SCR_TITLE || g_menu->screen == MENU_SCR_STORY)) ||
                (g_mode == APP_STAGE && g_stage && g_stage->special_live &&
                 g_stage->special.mode == SPECIAL_MODE_PRESENT && g_stage->special.movie_ok);
    const char *help =
        g_mode == APP_MENU
            ? "MENU: Arrows | Enter select | Esc quit"
            : (g_mode == APP_CREDITS
                   ? "CREDITS: Esc quit"
                   : "STAGE: Arrows/WASD | Space/X=Jump | Down=Duck | C=Blues | Z=Pulse | Enter=Pause");
    if (g_mode == APP_STAGE && g_stage && g_stage->sidescrl_live && g_stage->sidescrl.body_config)
        help = "BATWOOD: C=Jump/Hold ledge | Up=Climb | X/Space=Blues | Z=Attract bones | Enter=Pause";
    if (g_mode == APP_STAGE && g_stage && g_stage->kind == STAGE_KIND_FINALE && !movie)
        help = "FINALE: Arrows/WASD=Face bolt | X/Space=Fire stored charge";
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
    *out_movie = movie;
    *out_help = help;
    *out_status = status;
    *out_frame = frame;
    return 0;
}

static void render_host_surface(HDC mem_dc, int cw, int ch) {
    RECT rect = {0, 0, cw, ch};
    int movie = 0;
    const char *help = NULL;
    const char *status = NULL;
    const uint8_t *frame = NULL;
    int show_bar;
    HBRUSH bg_br;
    current_host_frame(&movie, &help, &status, &frame);
    show_bar = (!movie && !g_fullscreen);
    bg_br = CreateSolidBrush(show_bar ? RGB(10, 13, 20) : RGB(0, 0, 0));
    FillRect(mem_dc, &rect, bg_br);
    DeleteObject(bg_br);
    SetBkMode(mem_dc, TRANSPARENT);
    SetTextColor(mem_dc, RGB(220, 220, 220));
    if (show_bar && help) TextOutA(mem_dc, 12, 4, help, (int)strlen(help));
    if (show_bar && status) TextOutA(mem_dc, 12, 20, status, (int)strlen(status));
    if (show_bar && g_prog.status[0])
        TextOutA(mem_dc, 12, 36, g_prog.status, (int)strlen(g_prog.status));
    if (show_bar && cw >= 780) {
        char gfx_hud[112];
        unsigned sc = effective_upscale_scale();
        snprintf(gfx_hud, sizeof gfx_hud,
                 "[F5] %s %ux  [F6] HD: %s  [F11] Full",
                 mb_upscale_mode_name(g_upscale_mode), sc,
                 (g_hd_bones && sc >= 2u) ? "ON" : "OFF");
        SetTextColor(mem_dc, RGB(170, 215, 255));
        TextOutA(mem_dc, cw - 320, 4, gfx_hud, (int)strlen(gfx_hud));
        SetTextColor(mem_dc, RGB(220, 220, 220));
    }
    if (frame) {
        static uint8_t up_rgba[1280 * 896 * 4];
        static uint8_t bgra[1280 * 896 * 4];
        BITMAPINFO info = {0};
        unsigned sc = effective_upscale_scale();
        unsigned src_w = 320u, src_h = 224u;
        const uint8_t *src_rgba = frame;
        int top_margin = show_bar ? 54 : 0;
        int avail_w = (show_bar && cw > 24) ? (cw - 24) : cw;
        int avail_h = (show_bar && ch > top_margin + 10) ? (ch - top_margin - 10) : ch;
        int width = avail_w;
        int height = (int)((int64_t)width * 224 / 320);
        int left, top;
        if (height > avail_h) {
            height = avail_h;
            width = (int)((int64_t)height * 320 / 224);
        }
        left = (cw - width) / 2;
        top = (!show_bar) ? ((ch - height) / 2) : (top_margin + (avail_h - height) / 2);

        if (!movie && (g_hd_bones || g_upscale_mode != MB_UPSCALE_NEAREST) &&
            sc >= 2u && sc <= 4u &&
            mb_upscale_rgba(frame, 320u, 224u, g_upscale_mode, sc, up_rgba,
                            sizeof up_rgba) == 0) {
            src_w = 320u * sc;
            src_h = 224u * sc;
            if (g_hd_bones && stage_is_native_live(g_stage))
                (void)stage_enemies_draw_bones_hd(up_rgba, src_w, src_h, sc);
            src_rgba = up_rgba;
        }
        {
            size_t count = (size_t)src_w * src_h;
            const uint32_t *s32 = (const uint32_t *)src_rgba;
            uint32_t *d32 = (uint32_t *)bgra;
            for (size_t i = 0; i < count; i++) {
                uint32_t px = s32[i];
                d32[i] = (px & 0xFF00FF00u) | ((px & 0x00FF0000u) >> 16) |
                         ((px & 0x000000FFu) << 16) | 0xFF000000u;
            }
        }
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = (LONG)src_w;
        info.bmiHeader.biHeight = -(LONG)src_h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(mem_dc, COLORONCOLOR);
        if (show_bar && left > 1 && top > 1) {
            RECT frame_r = {left - 1, top - 1, left + width + 1, top + height + 1};
            HBRUSH fr_br = CreateSolidBrush(RGB(34, 46, 66));
            FrameRect(mem_dc, &frame_r, fr_br);
            DeleteObject(fr_br);
        }
        StretchDIBits(mem_dc, left, top, width, height, 0, 0, (int)src_w,
                      (int)src_h, bgra, &info, DIB_RGB_COLORS, SRCCOPY);
    }
}

static void capture_host_ppm(const char *tag) {
    char safe[64], path[MB_PATH_MAX + 96];
    size_t si = 0;
    int movie = 0;
    const char *help = NULL, *status = NULL;
    const uint8_t *frame = NULL;
    unsigned seq;
    if (!g_capture_dir[0] || !tag) return;
    CreateDirectoryA(g_capture_dir, NULL);
    for (const char *p = tag; *p && si + 1 < sizeof safe; p++) {
        char ch = *p;
        if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') || ch == '_' || ch == '-')
            safe[si++] = ch;
    }
    safe[si] = 0;
    if (!safe[0]) snprintf(safe, sizeof safe, "frame");
    seq = g_capture_seq++;
    current_host_frame(&movie, &help, &status, &frame);
    if (frame) {
        static uint8_t raw1x[320 * 224 * 4];
        const uint8_t *out1x = frame;
        stage_bones_pose pose;
        FILE *fp;
        if (!movie && g_hd_bones && effective_upscale_scale() >= 2u &&
            stage_enemies_last_bones_pose(&pose) == 0 && pose.valid) {
            memcpy(raw1x, frame, sizeof raw1x);
            stage_enemies_set_hd_overlay(0);
            /* Recover HD-capable enemies + thrown bones the overlay suppressed
             * from the live 1x surface, then composite Mr. Bones on top. */
            stage_enemies_redraw_suppressed_1x(raw1x, 320u, 224u);
            stage_enemies_draw_bones_ex(raw1x, 320u, 224u, pose.foot_x, pose.foot_y,
                                        pose.facing, pose.tall, pose.airborne,
                                        pose.anim_frame, pose.scale_num,
                                        pose.scale_den);
            stage_enemies_set_hd_overlay(1);
            out1x = raw1x;
        }
        snprintf(path, sizeof path, "%s\\%03u_%s.ppm", g_capture_dir, seq, safe);
        fp = fopen(path, "wb");
        if (fp) {
            fprintf(fp, "P6\n320 224\n255\n");
            for (size_t i = 0; i < 320u * 224u; i++)
                fwrite(&out1x[i * 4u], 1, 3, fp);
            fclose(fp);
        }
    }
    {
        unsigned sc = effective_upscale_scale();
        const int cw = (int)(320u * sc + 24u);
        const int ch = (int)(224u * sc + 64u);
        BITMAPINFO bmi = {0};
        void *bits = NULL;
        HDC mem_dc = CreateCompatibleDC(NULL);
        HBITMAP dib = NULL;
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = cw;
        bmi.bmiHeader.biHeight = -ch;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        dib = CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (mem_dc && dib && bits) {
            HGDIOBJ old = SelectObject(mem_dc, dib);
            const uint8_t *bgra = (const uint8_t *)bits;
            FILE *fp;
            render_host_surface(mem_dc, cw, ch);
            GdiFlush();
            snprintf(path, sizeof path, "%s\\%03u_%s_win.ppm", g_capture_dir, seq, safe);
            fp = fopen(path, "wb");
            if (fp) {
                fprintf(fp, "P6\n%d %d\n255\n", cw, ch);
                for (int i = 0; i < cw * ch; i++) {
                    uint8_t rgb[3] = {bgra[i * 4 + 2], bgra[i * 4 + 1], bgra[i * 4 + 0]};
                    fwrite(rgb, 1, 3, fp);
                }
                fclose(fp);
            }
            SelectObject(mem_dc, old);
        }
        if (dib) DeleteObject(dib);
        if (mem_dc) DeleteDC(mem_dc);
    }
}

static HDC g_paint_dc;
static HBITMAP g_paint_dib;
static HGDIOBJ g_paint_old_bmp;
static int g_paint_w;
static int g_paint_h;

static void free_paint_backbuffer(void) {
    if (g_paint_dc) {
        if (g_paint_old_bmp) {
            SelectObject(g_paint_dc, g_paint_old_bmp);
            g_paint_old_bmp = NULL;
        }
        DeleteDC(g_paint_dc);
        g_paint_dc = NULL;
    }
    if (g_paint_dib) {
        DeleteObject(g_paint_dib);
        g_paint_dib = NULL;
    }
    g_paint_w = 0;
    g_paint_h = 0;
}

static void paint(HWND window) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(window, &ps);
    RECT rect;
    int cw, ch;
    GetClientRect(window, &rect);
    cw = rect.right - rect.left;
    ch = rect.bottom - rect.top;
    if (cw <= 0 || ch <= 0) { EndPaint(window, &ps); return; }

    if (!g_paint_dc || !g_paint_dib || g_paint_w != cw || g_paint_h != ch) {
        BITMAPINFO bmi = {0};
        void *bits = NULL;
        free_paint_backbuffer();
        g_paint_dc = CreateCompatibleDC(dc);
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = cw;
        bmi.bmiHeader.biHeight = -ch;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        if (g_paint_dc)
            g_paint_dib = CreateDIBSection(g_paint_dc, &bmi, DIB_RGB_COLORS,
                                           &bits, NULL, 0);
        if (g_paint_dc && g_paint_dib) {
            g_paint_old_bmp = SelectObject(g_paint_dc, g_paint_dib);
            g_paint_w = cw;
            g_paint_h = ch;
        } else {
            free_paint_backbuffer();
        }
    }
    if (g_paint_dc && g_paint_dib) {
        render_host_surface(g_paint_dc, cw, ch);
        BitBlt(dc, 0, 0, cw, ch, g_paint_dc, 0, 0, SRCCOPY);
    }
    EndPaint(window, &ps);
}

static void update_window_title(HWND window, const char *text, int force) {
    static char s_last_title[256];
    static unsigned s_throttle;
    if (!window || !text) return;
    if (!force && ++s_throttle < 10u) return;
    s_throttle = 0;
    if (strcmp(s_last_title, text) == 0) return;
    snprintf(s_last_title, sizeof s_last_title, "%s", text);
    SetWindowTextA(window, text);
}

static int step_menu_with_pad(menu_phase *menu, const saturn_pad *pad) {
    saturn_pad input = *pad;
    if (menu->title_avi_ok &&
        (menu->screen == MENU_SCR_TITLE || menu->screen == MENU_SCR_STORY)) {
        input.pressed = (uint16_t)(input.held & 0x0800u);
        if (input.pressed) {
            unsigned tick = menu->title_avi.tick_us;
            int external = menu->title_avi.tick_external;
            int result;
            menu->title_avi.tick_external = 1;
            menu->title_avi.tick_us = 0;
            result = menu_phase_step(menu, &input);
            menu->title_avi.tick_us = tick;
            menu->title_avi.tick_external = external;
            return result;
        }
    }
    return menu_phase_step(menu, &input);
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
            step_menu_with_pad(g_menu, &pad);
            g_menu_volume = g_menu->volume;
            store_host_options(g_menu, g_menu_volume);
            host_audio_set_volume(g_menu_volume);
            host_audio_set_stereo(g_menu->stereo_on);
            if (g_menu->state == MENU_ST_QUIT) {
                if (!g_expect_movie_error)
                    fprintf(stderr, "movie failed (menu): %s\n", g_menu->status);
                if (g_movie_audio) { host_audio_stop(g_movie_audio); g_movie_audio = NULL; }
                g_mode = APP_DONE;
                return 0;
            }
            if (g_menu->screen == MENU_SCR_TITLE || g_menu->screen == MENU_SCR_STORY) {
                if (g_audio) { host_audio_stop(g_audio); g_audio = NULL; }
                maybe_play_sfx();
            } else {
                if (g_movie_audio) { host_audio_stop(g_movie_audio); g_movie_audio = NULL; }
                g_movie_audio_failed = 0;
                if (g_menu->music_on && !g_audio && now >= g_menu_music_after)
                    g_audio = host_audio_start_menu_cdda(g_cd, 2);
                else if (!g_menu->music_on && g_audio) {
                    host_audio_stop(g_audio);
                    g_audio = NULL;
                }
            }
            snprintf(g_title, sizeof g_title, "Mr. Bones - %s%s",
                     g_movie_audio_failed ? "[FMV audio unavailable] " : "", g_menu->status);
            update_window_title(window, g_title, 0);
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
            update_window_title(window, g_title, stage_runner_done(g_stage) ? 1 : 0);
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
            update_window_title(window, g_title, 0);
            if (g_credits_frames >= CREDITS_HOLD_FRAMES) {
                g_mode = APP_DONE;
            }
        }
        if (lp == 0) {
            InvalidateRect(window, NULL, FALSE);
            UpdateWindow(window);
        }
        return 0;
    }
    if (msg == WM_USER + 1) { return 0; }
    if (msg == WM_USER + 2) {
        (void)advance_after_stage();
        return 0;
    }
    if (msg == WM_KEYDOWN && !(lp & (1L << 30))) {
        g_pending_keys |= host_input_key_button((unsigned)wp);
        if (wp == VK_F5) {
            g_upscale_mode = (mb_upscale_mode)(((int)g_upscale_mode + 1) % 5);
            stage_enemies_prepare_hd_bones(g_upscale_mode,
                                           g_hd_bones_dir[0] ? g_hd_bones_dir : NULL);
            stage_enemies_set_hd_overlay(g_hd_bones && effective_upscale_scale() >= 2u);
            InvalidateRect(window, NULL, FALSE);
            return 0;
        }
        if (wp == VK_F6) {
            g_hd_bones = !g_hd_bones;
            stage_enemies_set_hd_overlay(g_hd_bones && effective_upscale_scale() >= 2u);
            InvalidateRect(window, NULL, FALSE);
            return 0;
        }
        if (wp == VK_F11) {
            toggle_fullscreen(window);
            InvalidateRect(window, NULL, FALSE);
            return 0;
        }
    }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
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
            "              [--campaign] [--mute] [--autoplay] [--capture-dir DIR]\n"
            "              [--upscale MODE] [--scale N] [--benchmark-frames N] [--hd-dir DIR] [--no-hd-bones]\n"
            "              [--finale-replay CSV [--capture-ticks N,N,... --capture-dir DIR]] [--version]\n",
            MB_HOST_VERSION);
}

/* Drive one native stage to EndLevel. Rejects smoke. */
static int play_native_stage(stage_runner *stage, const char *iso, int max_frames) {
    saturn_pad pad = {0};
    int i;
    int cap_a = 0, cap_b = 0, cap_c = 0;
    if (enter_stage(iso)) {
        fprintf(stderr, "full: begin failed %s\n", iso);
        return -1;
    }
    if (stage->kind == STAGE_KIND_EMPTY) {
        /* Instant skip — progress should already skip these. */
        printf("  SKIP %s [empty]\n", iso);
        return 0;
    }
    if (stage->runway_live && stage->runway.runway_win_record)
        max_frames = (int)(stage->runway.runway_win_record * 4u + 1u);
    if (stage->runway_live && (!strcmp(iso, "/11LOGS") || !strcmp(iso, "/18RIDE") || !strcmp(iso, "/24STAING"))) {
        unsigned duration = stage->runway.pan.count * 4u + 120u;
        if (duration > (unsigned)max_frames) max_frames = (int)duration;
    }
    if (stage->sidescrl_live && stage->sidescrl.scroll_w > (unsigned)max_frames)
        max_frames = (int)stage->sidescrl.scroll_w;
    if (stage->special_live) {
        unsigned duration = special_phase_autoplay_frame_limit(&stage->special);
        if (duration > (unsigned)max_frames) max_frames = (int)duration;
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
        if (stage->special_live && stage->special.mode == SPECIAL_MODE_PRESENT &&
            stage->special.movie_ok && !stage->special.movie.tick_external) {
            stage->special.movie.tick_external = 1;
            stage->special.movie.tick_us = stage->special.movie.us_per_frame + 1u;
            unsigned remaining = stage->special.movie.frame_count + stage->special.finale_movie_preroll + 16u;
            if ((unsigned)i + remaining > (unsigned)max_frames)
                max_frames = i + (int)remaining;
        }
        if (stage->runway_live) {
            saturn_pad_update_host(
                &pad, runway_phase_autoplay_buttons(&stage->runway, (unsigned)i));
        } else if (stage->sidescrl_live) {
            saturn_pad_update_host(
                &pad, sidescrl_phase_autoplay_buttons(&stage->sidescrl, (unsigned)i));
        } else if (stage->special_live) {
            unsigned btn = MB_PAD_RIGHT;
            if (stage->special.mode == SPECIAL_MODE_RHYTHM ||
                stage->special.mode == SPECIAL_MODE_ACTION ||
                       (stage->kind == STAGE_KIND_FINALE && !stage->special.movie_ok)) {
                btn = special_phase_autoplay_buttons(&stage->special,
                                                     (unsigned)i);
            }
            saturn_pad_update_host(&pad, btn);
        }
        stage_runner_step(stage, &pad);
        maybe_play_sfx();
        if (g_capture_dir[0]) {
            int cap_a_frame = (stage->special_live &&
                               stage->special.mode == SPECIAL_MODE_ACTION)
                                  ? 18
                                  : 60;
            if (!cap_a && i == cap_a_frame) {
                capture_host_ppm(iso);
                cap_a = 1;
            } else if (!cap_b && i > 120) {
                int interesting = 0;
                if (stage->runway_live &&
                    (stage->runway.enemies.shots[0].active ||
                     stage->runway.bones_x > 160u * stage->runway.enemies.units_per_pixel))
                    interesting = 1;
                else if (stage->sidescrl_live &&
                         ((!strcmp(stage->sidescrl.stage_name, "LAFF") &&
                           stage->sidescrl.camera_x > 1620u * stage->sidescrl.enemies.units_per_pixel) ||
                          (strcmp(stage->sidescrl.stage_name, "LAFF") != 0 &&
                           (stage->sidescrl.enemies.shots[0].active || i == 220))))
                    interesting = 1;
                else if (stage->special_live &&
                         ((stage->special.mode == SPECIAL_MODE_PRESENT &&
                           stage->kind == STAGE_KIND_FINALE &&
                           !stage->special.movie_ok &&
                           stage->special.ring_active_count > 0 &&
                           stage->special.ring_active_count < 14 &&
                           stage->special.ring_cleared == 0 && i >= 140) ||
                          (stage->special.mode != SPECIAL_MODE_PRESENT && i == 220)))
                    interesting = 1;
                if (interesting || (i == 750 && !stage->sidescrl_live) || i == 900) {
                    capture_host_ppm(iso);
                    cap_b = 1;
                }
            }
            if (!cap_c && stage->special_live &&
                stage->special.mode == SPECIAL_MODE_PRESENT &&
                stage->special.movie_ok &&
                stage->special.movie.frames_drawn == 45u) {
                capture_host_ppm("FINALE_ending_avi");
                cap_c = 1;
            }
        }
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

/* Columns after held are reference observations; only tick and the physical
 * pad mask are inputs. All game state after the entrance seed comes from play. */
static int replay_uint(const char *s, char **end, int base, unsigned long *value) {
    if (*s < '0' || *s > '9') return -1;
    errno = 0;
    *value = strtoul(s, end, base);
    return errno || *end == s ? -1 : 0;
}

static int replay_finale(stage_runner *stage, const char *path, const char *capture_ticks) {
    FILE *file = fopen(path, "r");
    char line[4096], *end;
    unsigned long seed, expected = 1, movie_tick = 0, first_visible = 0, next_capture = 0;
    unsigned ntsc_remainder = 0;
    saturn_pad pad = {0};
    int failed = 1;
    if (!file) {
        fprintf(stderr, "replay: cannot open %s\n", path);
        return 1;
    }
    if (!fgets(line, sizeof line, file) || strncmp(line, "# seed=0x", 9) ||
        replay_uint(line + 7, &end, 16, &seed) || *end != '\n' ||
        !fgets(line, sizeof line, file) || strncmp(line, "tick,held", 9) ||
        (line[9] != ',' && line[9] != '\n') || !strchr(line, '\n')) {
        fprintf(stderr, "replay: expected # seed=0xHEX followed by tick,held\n");
        goto close_file;
    }
    if (stage_progress_begin(&g_prog, "/27FINALE") || enter_stage("/27FINALE") ||
        !stage->special_live || stage->special.movie_ok) {
        fprintf(stderr, "replay: failed to enter FINALE arena\n");
        goto close_file;
    }
    special_phase *phase = &stage->special;
    if (seed != phase->finale_rng) {
        fprintf(stderr, "replay: seed 0x%08lx differs from FINALE entry state 0x%08x\n",
                seed, phase->finale_rng);
        goto close_file;
    }
    if (g_capture_dir[0])
        puts("replay_tick,tick,held,face,charge,slots,slot_mask,ring_phase,energy,shots,movie");
    while (fgets(line, sizeof line, file)) {
        unsigned long tick, held;
        if ((!strchr(line, '\n') && !feof(file)) ||
            replay_uint(line, &end, 10, &tick) || tick != expected || *end != ',' ||
            replay_uint(end + 1, &end, 10, &held) || held > 0x1ffful ||
            (*end && *end != ',' && *end != '\n')) {
            fprintf(stderr, "replay: invalid row at tick %lu\n", expected);
            goto close_file;
        }
        if (!next_capture && capture_ticks && *capture_ticks) {
            if (!g_capture_dir[0] || replay_uint(capture_ticks, &end, 10, &next_capture) ||
                next_capture < tick || (*end && (*end != ',' || !end[1]))) {
                fprintf(stderr, "replay: --capture-ticks requires ordered ticks and --capture-dir\n");
                goto close_file;
            }
            capture_ticks = *end ? end + 1 : end;
        }
        saturn_pad_update_host(&pad, (uint32_t)held);
        /* One physical US Saturn retrace is 1001/60000 s = 50050/3 us. */
        ntsc_remainder += 50050u;
        if (phase->movie_ok) {
            phase->movie.tick_external = 1;
            phase->movie.tick_us = ntsc_remainder / 3u;
        }
        ntsc_remainder %= 3u;
        stage_runner_step(stage, &pad);
        maybe_play_sfx();
        if (g_capture_dir[0]) {
            unsigned mask = 0;
            for (unsigned i = 0; i < FINALE_RING_SLOTS; ++i)
                if (phase->ring_active[i]) mask |= 1u << i;
            printf("replay_tick,%lu,%lu,%u,%u,%d,%04x,%d,%d,%u,%d\n", tick, held,
                   phase->finale_facing, phase->finale_charged, phase->ring_active_count,
                   mask, phase->ring_phase, phase->skel.skeletism, phase->ring_shots, phase->movie_ok);
        }
        if (next_capture == tick) {
            char tag[48];
            snprintf(tag, sizeof tag, "FINALE_tick_%06lu", tick);
            capture_host_ppm(tag);
            next_capture = 0;
        }
        if (!movie_tick && phase->ring_cleared && phase->movie_ok) {
            movie_tick = tick;
        }
        if (movie_tick && !phase->finale_movie_preroll && !first_visible && !stage_runner_done(stage)) {
            first_visible = tick;
            if (g_capture_dir[0]) capture_host_ppm("FINALE_replay_ending");
        }
        if (stage_runner_done(stage) && !movie_tick) {
            fprintf(stderr, "replay: arena ended without a win at tick %lu\n", tick);
            goto close_file;
        }
        ++expected;
    }
    if (next_capture || (capture_ticks && *capture_ticks)) {
        fprintf(stderr, "replay: capture tick exceeds tape length\n");
        goto close_file;
    }
    if (ferror(file) || !movie_tick || phase->ring_active_count != 0 ||
        phase->ring_shots < FINALE_RING_SLOTS) {
        fprintf(stderr, "replay: missing victory or incomplete tape at tick %lu (%d sectors left)\n",
                expected - 1, phase->ring_active_count);
        goto close_file;
    }
    printf("FINALE replay WIN: seed=0x%08lx ticks=%lu clear_tick=%u movie_tick=%lu shots=%u hits=%u energy=%d movie_frames=%u first_visible=%lu\n",
           seed, expected - 1, phase->finale_clear_tick, movie_tick, phase->ring_shots, phase->hazard_hits,
           phase->skel.skeletism, phase->movie.frame_count, first_visible);
    failed = 0;
close_file:
    fclose(file);
    return failed;
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
 * §19 headless: MENU → campaign chain → INSERTCD → FINALE → MENU.
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
    /* The full-campaign verifier always exercises New Game, even after a prior run. */
    mb_nvm_set_u32(&menu->nvm, MB_NVM_DEFAULT_LEVEL_OFF, 4u);
    /* Hold MENU so title AVI fades in before capture, then capture menu states. */
    {
        int title_ticks = g_capture_dir[0] ? 75 : 12;
        for (menu_frames = 0; menu_frames < title_ticks; menu_frames++) {
            saturn_pad_update_host(&pad, 0);
            menu_phase_step(menu, &pad);
        }
    }
    capture_host_ppm("03MENU_title");
    saturn_pad_update_host(&pad, MB_PAD_START);
    menu_phase_step(menu, &pad);
    capture_host_ppm("03MENU_start");
    if (g_capture_dir[0]) {
        saturn_pad_update_host(&pad, 0); menu_phase_step(menu, &pad);
        saturn_pad_update_host(&pad, MB_PAD_DOWN); menu_phase_step(menu, &pad);
        saturn_pad_update_host(&pad, 0); menu_phase_step(menu, &pad);
        saturn_pad_update_host(&pad, MB_PAD_START); menu_phase_step(menu, &pad);
        capture_host_ppm("03MENU_options");
        saturn_pad_update_host(&pad, 0); menu_phase_step(menu, &pad);
        saturn_pad_update_host(&pad, MB_PAD_L); menu_phase_step(menu, &pad);
        menu_phase_show_levels(menu);
        capture_host_ppm("03MENU_levels");
        menu_phase_show_start(menu);
    }
    saturn_pad_update_host(&pad, 0);
    menu_phase_step(menu, &pad);
    saturn_pad_update_host(&pad, MB_PAD_START);
    menu_phase_step(menu, &pad);
    if (g_capture_dir[0]) {
        for (menu_frames = 0; menu_frames < 45 && !menu_phase_done(menu) &&
             menu->screen == MENU_SCR_STORY; menu_frames++) {
            saturn_pad_update_host(&pad, 0);
            menu_phase_step(menu, &pad);
        }
    }
    capture_host_ppm("03MENU_story");
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
        if (stage->kind == STAGE_KIND_FINALE && !g_headless_finale_movie &&
            (!stage->special.ring_cleared || !stage->special.movie.playback_done)) {
            fprintf(stderr, "full: FINALE did not clear the arena and finish the film\n");
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

    if (!advance_after_stage() || g_mode != APP_MENU || menu->screen != MENU_SCR_START) {
        fprintf(stderr, "full: FINALE did not return to menu\n");
        error = 1;
        goto done;
    }

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
    printf("full-campaign: MENU→%u stages→INSERTCD→FINALE→MENU OK\n", played);

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

    {
        saturn_pad pad = {0};
        if (menu_phase_load(menu, g_cd, g_save_dir) || !menu->title_avi_ok) return 1;
        menu->title_avi.tick_external = 1;
        menu->title_avi.tick_us = 0;
        saturn_pad_update_host(&pad, MB_PAD_A);
        step_menu_with_pad(menu, &pad);
        if (menu->screen != MENU_SCR_TITLE) {
            fprintf(stderr, "check: A skipped the opening movie\n");
            return 1;
        }
        saturn_pad_update_host(&pad, MB_PAD_START);
        step_menu_with_pad(menu, &pad);
        if (menu->screen != MENU_SCR_START) return 1;
        saturn_pad_update_host(&pad, MB_PAD_START);
        step_menu_with_pad(menu, &pad);
        if (menu->screen != MENU_SCR_START) return 1;
        saturn_pad_update_host(&pad, 0);
        step_menu_with_pad(menu, &pad);
        saturn_pad_update_host(&pad, MB_PAD_START);
        step_menu_with_pad(menu, &pad);
        if (menu->screen != MENU_SCR_STORY || menu->state != MENU_ST_ACTIVE) return 1;
        saturn_pad_update_host(&pad, MB_PAD_START);
        step_menu_with_pad(menu, &pad);
        if (menu->state != MENU_ST_START_LEVEL) {
            fprintf(stderr, "check: held Start did not skip the story movie\n");
            return 1;
        }
        menu_phase_free(menu);
    }

    {
        saturn_pad pad = {0};
        if (menu_phase_load(menu, g_cd, g_save_dir) || !menu->title_avi_ok) return 1;
        menu->title_avi.cur = menu->title_avi.frame_count;
        menu->title_avi.elapsed_us = 0;
        menu->title_avi.tick_external = 1;
        menu->title_avi.tick_us = 1000000;
        saturn_pad_update_host(&pad, MB_PAD_START);
        step_menu_with_pad(menu, &pad);
        if (menu->screen != MENU_SCR_START || menu->state != MENU_ST_ACTIVE) {
            fprintf(stderr, "check: Start at opening end skipped the menu\n");
            return 1;
        }
        menu_phase_free(menu);
        if (menu_phase_load(menu, g_cd, g_save_dir) || !menu->title_avi_ok) return 1;
        menu->title_avi.cur = menu->title_avi.frame_count;
        menu->title_avi.elapsed_us = 0;
        menu->title_avi.tick_external = 1;
        menu->title_avi.tick_us = 1000000;
        saturn_pad_update_host(&pad, 0);
        step_menu_with_pad(menu, &pad);
        if (menu->screen != MENU_SCR_START || menu->state != MENU_ST_ACTIVE ||
            !menu->title_avi.playback_done) {
            fprintf(stderr, "check: opening did not end naturally at the menu\n");
            return 1;
        }
        menu_phase_free(menu);
    }

    {
        static const uint8_t invalid[] = {0, 0, 0};
        if (menu_phase_load(menu, g_cd, g_save_dir) ||
            menu->title_avi.cur >= menu->title_avi.frame_count) return 1;
        menu->movie_pcm_pending = 0;
        g_expect_movie_error = 1;
        menu->title_avi.frames[menu->title_avi.cur].data = invalid;
        menu->title_avi.frames[menu->title_avi.cur].size = sizeof invalid;
        g_menu = menu;
        g_mode = APP_MENU;
        g_movie_last_tick = GetTickCount64() - 1000;
        wndproc(NULL, WM_TIMER, 1, 0);
        g_expect_movie_error = 0;
        if (!menu->title_avi.decode_error || g_mode != APP_DONE) {
            fprintf(stderr, "check: invalid menu movie was not stopped\n");
            return 1;
        }
        printf("check: corrupt MENU movie rejection OK\n");
        menu_phase_free(menu);
        g_menu = NULL;
    }

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

    if (g_disc2_path[0]) {
        saturn_pad pad = {0};
        stage_runner_free(stage);
        g_menu = menu;
        if (enter_stage("/27FINALE") || !stage->special_live ||
            !stage->special.movie_ok) return 1;
        stage->special.movie.tick_external = 1;
        stage->special.movie.tick_us = 400000000u;
        stage_runner_step(stage, &pad);
        if (!stage_runner_done(stage) || !stage->special.movie.playback_done ||
            stage->special.movie.frames_drawn != stage->special.movie.frame_count ||
            !advance_after_stage() || g_mode != APP_MENU ||
            menu->screen != MENU_SCR_START || !menu->frame_rgba) {
            fprintf(stderr, "check: FINALE movie did not return to menu\n");
            return 1;
        }
        menu->music_on = 1;
        wndproc(NULL, WM_TIMER, 1, 0);
        if (g_audio) {
            fprintf(stderr, "check: FINALE menu music started too early\n");
            return 1;
        }
        g_menu_music_after = GetTickCount64() - 1;
        wndproc(NULL, WM_TIMER, 1, 0);
        if (!g_audio) {
            fprintf(stderr, "check: FINALE menu music did not start after delay\n");
            return 1;
        }
        host_audio_stop(g_audio);
        g_audio = NULL;

        /* Interactive FINALE: the arena keeps its CD-DA while the ending is
         * deferred; clearing the rings swaps it for the ending PCM. */
        g_headless_finale_movie = 0;
        if (enter_stage("/27FINALE") || stage->special.movie_ok ||
            !stage->special.finale_movie_armed || !g_audio || g_movie_audio) {
            g_headless_finale_movie = 1;
            fprintf(stderr, "check: deferred FINALE did not keep the arena CD-DA\n");
            return 1;
        }
        g_headless_finale_movie = 1;
        memset(&pad, 0, sizeof pad);
        for (unsigned k = 0; k < STAGE_MAX_FRAMES && !stage->special.movie_ok; k++) {
            saturn_pad_update_host(&pad, special_phase_autoplay_buttons(&stage->special, k));
            stage_runner_step(stage, &pad);
            if (!stage->special.movie_ok && (!g_audio || g_movie_audio)) {
                fprintf(stderr, "check: FINALE arena lost its CD-DA before the clear\n");
                return 1;
            }
        }
        if (!stage->special.ring_cleared || !stage->special.movie_ok ||
            !stage->special.finale_movie_preroll || stage->special.movie_pcm_pending) {
            fprintf(stderr, "check: FINALE ring clear did not arm the ending\n");
            return 1;
        }
        maybe_start_movie_audio();
        if (g_audio || g_movie_audio) {
            fprintf(stderr, "check: FINALE CD seek did not silence arena audio\n");
            return 1;
        }
        while (stage->special.finale_movie_preroll && !stage->special.movie_pcm_pending) {
            saturn_pad_update_host(&pad, 0);
            stage_runner_step(stage, &pad);
        }
        maybe_start_movie_audio();
        if (g_audio || !g_movie_audio || stage->special.movie_pcm_pending) {
            fprintf(stderr, "check: FINALE ending PCM did not replace the arena CD-DA\n");
            return 1;
        }
        host_audio_stop(g_movie_audio);
        g_movie_audio = NULL;
        printf("check: FINALE arena CD-DA -> ending PCM OK\n");

        menu_phase_free(menu);
        g_menu = NULL;
        if (remount_disc(1)) return 1;
    }

    {
        static const uint8_t invalid[] = {0, 0, 0};
        saturn_pad pad = {0};
        stage_runner_free(stage);
        if (enter_stage("/28LOSE") || !stage->special_live ||
            stage->special.movie.cur >= stage->special.movie.frame_count) return 1;
        {
            unsigned rate = stage->special.movie.pcm_rate;
            stage->special.movie.pcm_rate = 1;
            g_expect_movie_error = 1;
            maybe_start_movie_audio();
            g_expect_movie_error = 0;
            stage->special.movie.pcm_rate = rate;
            if (!g_movie_audio_failed || g_movie_audio || stage->special.movie_pcm_pending) {
                fprintf(stderr, "check: movie audio failure was not reported\n");
                return 1;
            }
        }
        {
            int not_stopped;
            g_expect_movie_error = 1;
            stage->special.movie.frames[stage->special.movie.cur].data = invalid;
            stage->special.movie.frames[stage->special.movie.cur].size = sizeof invalid;
            stage->special.movie.tick_external = 1;
            stage->special.movie.tick_us = stage->special.movie.us_per_frame + 1u;
            stage_runner_step(stage, &pad);
            not_stopped = !stage->special.movie.decode_error || !stage_runner_done(stage) ||
                          advance_after_stage() || g_mode != APP_DONE;
            g_expect_movie_error = 0;
            if (not_stopped) {
                fprintf(stderr, "check: invalid LOSE movie was not stopped\n");
                return 1;
            }
        }
        printf("check: corrupt LOSE movie rejection OK\n");
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
    const char *finale_tape = NULL, *capture_ticks = NULL;
    unsigned benchmark_frames = 0;
    int check_only = 0, full_campaign = 0, skip_menu = 0, campaign = 0, error = 0;
    int i;
    char err[512];
    char level_override[32];
    char bundled_manifest[MB_PATH_MAX];
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
    g_opt_music = cfg.music_on;
    g_opt_stereo = cfg.stereo_on;
    g_opt_volume = cfg.volume;
    host_audio_set_stereo(cfg.stereo_on);
    host_audio_set_volume(cfg.volume);
    snprintf(g_config_path, sizeof g_config_path, "%s", cfg.config_path);
    g_menu_volume = cfg.volume;
    snprintf(g_save_dir, sizeof g_save_dir, "%s", cfg.save_dir);
    snprintf(g_disc2_path, sizeof g_disc2_path, "%s", cfg.disc[1]);
    if (argc == 1) {
        if (cfg.disc[0][0]) snprintf(g_cue1, sizeof g_cue1, "%s", cfg.disc[0]);
        else if (find_bundled_file(g_cue1, sizeof g_cue1,
                                   "disco1\\Mr. Bones (USA) (Disc 1).cue"))
            snprintf(g_cue1, sizeof g_cue1, "disco1\\Mr. Bones (USA) (Disc 1).cue");
        if (!g_disc2_path[0] && find_bundled_file(g_disc2_path, sizeof g_disc2_path,
                                   "disco2\\Mr. Bones (USA) (Disc 2).cue"))
            g_disc2_path[0] = 0;
    } else if (argc < 3) {
        usage();
        return 2;
    } else {
        snprintf(g_cue1, sizeof g_cue1, "%s", argv[1]);
    }
    int upscale_explicit = 0, hd_dir_explicit = 0;
    for (i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--check")) check_only = 1;
        else if (!strcmp(argv[i], "--full-campaign")) full_campaign = 1;
        else if (!strcmp(argv[i], "--skip-menu")) skip_menu = 1;
        else if (!strcmp(argv[i], "--campaign")) campaign = 1;
        else if (!strcmp(argv[i], "--mute")) { g_mute_audio = 1; host_audio_set_muted(1); }
        else if (!strcmp(argv[i], "--benchmark-frames") && i + 1 < argc) {
            char *end;
            long n = strtol(argv[++i], &end, 10);
            if (*end || n < 1 || n > 1000000) { usage(); return 2; }
            benchmark_frames = (unsigned)n;
        }
        else if (!strcmp(argv[i], "--capture-ticks") && i + 1 < argc) capture_ticks = argv[++i];
        else if (!strcmp(argv[i], "--finale-replay") && i + 1 < argc) finale_tape = argv[++i];
        else if (!strcmp(argv[i], "--autoplay")) g_autoplay_mode = 1;
        else if (!strcmp(argv[i], "--capture-dir") && i + 1 < argc)
            snprintf(g_capture_dir, sizeof g_capture_dir, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--upscale") && i + 1 < argc) {
            if (mb_upscale_mode_parse(argv[++i], &g_upscale_mode)) {
                usage();
                return 2;
            }
            upscale_explicit = 1;
        } else if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
            long sc = strtol(argv[++i], NULL, 10);
            if (sc < 1 || sc > 4) { usage(); return 2; }
            g_upscale_factor = (unsigned)sc;
            if (!upscale_explicit) {
                if (sc == 1) g_upscale_mode = MB_UPSCALE_NEAREST;
                else if (sc == 2 || sc == 4) g_upscale_mode = MB_UPSCALE_XBR2X;
                else g_upscale_mode = MB_UPSCALE_XBR3X;
            }
            upscale_explicit = 1;
        } else if (!strcmp(argv[i], "--hd-dir") && i + 1 < argc) {
            snprintf(g_hd_bones_dir, sizeof g_hd_bones_dir, "%s", argv[++i]);
            hd_dir_explicit = 1;
        } else if (!strcmp(argv[i], "--no-hd-bones")) {
            g_hd_bones = 0;
        }
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
    if (finale_tape && (check_only || full_campaign || g_autoplay_mode || benchmark_frames ||
                        campaign || (level_override[0] && strcmp(level_override, "/27FINALE")))) {
        usage();
        return 2;
    }
    if (g_mute_audio) host_audio_set_muted(1);

    {
        const char *manifest_path = (argc >= 3) ? argv[2] : cfg.manifest;
        if (argc == 1 && !strcmp(cfg.manifest, "manifest\\versions.txt") &&
            !find_bundled_file(bundled_manifest, sizeof bundled_manifest, cfg.manifest))
            manifest_path = bundled_manifest;
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
    stage_enemies_load_disc_sprites(g_cd);
    if (g_have_disc2)
        stage_enemies_load_disc_sprites(&g_cd2_storage);
    if (!hd_dir_explicit)
        (void)find_bundled_dir(g_hd_bones_dir, sizeof g_hd_bones_dir,
                               "assets\\sprites_bones\\hd_3x");
    if ((!check_only && !full_campaign) || g_capture_dir[0] ||
        upscale_explicit || hd_dir_explicit) {
        unsigned sc = effective_upscale_scale();
        stage_enemies_prepare_hd_bones(g_upscale_mode,
                                       g_hd_bones_dir[0] ? g_hd_bones_dir : NULL);
        stage_enemies_set_hd_overlay(g_hd_bones && sc >= 2u);
        printf("upscale: %s %ux (%ux%u), hd_bones=%s (custom=%d, dir=%s)\n",
               mb_upscale_mode_name(g_upscale_mode), sc, 320u * sc, 224u * sc,
               (g_hd_bones && sc >= 2u) ? "ON" : "OFF",
               stage_enemies_hd_custom_count(),
               g_hd_bones_dir[0] ? g_hd_bones_dir : "<auto>");
    }
    printf("catalog: %u stages, campaign=%u\n", g_catalog.count,
           g_catalog.campaign_count);

    mb_nvm_load(&g_nvm, g_save_dir);
    stage_progress_init(&g_prog, &g_catalog, &g_nvm, g_save_dir);

    memset(&menu, 0, sizeof menu);
    memset(&stage, 0, sizeof stage);
    g_menu = &menu;
    g_stage = &stage;

    if (finale_tape) {
        error = replay_finale(&stage, finale_tape, capture_ticks);
        goto shutdown;
    }
    if (check_only || full_campaign) {
        g_headless_finale_movie = check_only;
        if (full_campaign && !check_only)
            error = full_campaign_mode(&menu, &stage);
        else
            error = check_mode(&menu, &stage);
        goto shutdown;
    }

    if (g_autoplay_mode && (skip_menu || level_override[0] || campaign)) {
        const char *iso = level_override[0] ? level_override : NULL;
        if (stage_progress_begin(&g_prog, iso) ||
            play_native_stage(&stage, g_prog.next_iso, STAGE_MAX_FRAMES)) {
            error = 1;
            goto shutdown;
        }
        if (stage.kind == STAGE_KIND_FINALE && stage.special_live &&
            stage.special.movie_ok && g_capture_dir[0]) {
            capture_host_ppm("27FINALE_ending_movie");
        }
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
        apply_host_options(&menu);
        menu.volume = g_menu_volume;
        host_audio_set_volume(g_menu_volume);
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
        {
            unsigned sc = effective_upscale_scale();
            RECT wr = {0, 0, (LONG)(320u * sc + 24u), (LONG)(224u * sc + 64u)};
            int max_w = GetSystemMetrics(SM_CXSCREEN) - 40;
            int max_h = GetSystemMetrics(SM_CYSCREEN) - 60;
            AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
            {
                int win_w = wr.right - wr.left;
                int win_h = wr.bottom - wr.top;
                if (max_w > 360 && win_w > max_w) win_w = max_w;
                if (max_h > 300 && win_h > max_h) win_h = max_h;
                window = CreateWindowA(wc.lpszClassName, "Mr. Bones",
                                       WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                       CW_USEDEFAULT, CW_USEDEFAULT, win_w, win_h,
                                       NULL, NULL, instance, NULL);
            }
        }
        if (!window) { error = 1; goto shutdown; }
        timeBeginPeriod(1);
        LARGE_INTEGER qpc_freq, qpc_last, qpc_now;
        QueryPerformanceFrequency(&qpc_freq);
        QueryPerformanceCounter(&qpc_last);
        int64_t target_ticks = qpc_freq.QuadPart / 60;

        unsigned presented = 0;
        LARGE_INTEGER previous_present = qpc_last;
        while (g_mode != APP_DONE) {
            while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    g_mode = APP_DONE;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
                if (g_mode == APP_MENU && g_menu &&
                    g_menu->state == MENU_ST_START_LEVEL) {
                    const char *sel = menu_phase_selected_iso(g_menu);
                    g_nvm = g_menu->nvm;
                    stage_progress_init(&g_prog, &g_catalog, &g_nvm, g_save_dir);
                    if (sel && stage_progress_begin(&g_prog, sel) == 0 &&
                        enter_stage(g_prog.next_iso) == 0) {
                        /* QPC loop drives 60 Hz pacing directly. */
                    } else if (g_menu) {
                        snprintf(g_menu->status, sizeof g_menu->status,
                                 "MENU level load failed: %s", sel ? sel : "?");
                        g_menu->state = MENU_ST_ACTIVE;
                        g_menu->screen = MENU_SCR_LEVELS;
                    }
                }
            }
            if (g_mode == APP_DONE) {
                if ((g_menu && g_menu->state == MENU_ST_QUIT) ||
                    (g_stage && g_stage->special_live && g_stage->special.movie.decode_error))
                    error = 1;
                Sleep(200);
                break;
            }
            if (g_reset_qpc) {
                QueryPerformanceCounter(&qpc_last);
                g_reset_qpc = 0;
            }

            QueryPerformanceCounter(&qpc_now);
            int64_t delta = qpc_now.QuadPart - qpc_last.QuadPart;
            if (delta >= target_ticks) {
                int steps = 0;
                int64_t dropped = 0;
                if (delta >= target_ticks * 4) {
                    dropped = delta / target_ticks - 3;
                    qpc_last.QuadPart = qpc_now.QuadPart - target_ticks * 3;
                    delta = target_ticks * 3;
                }
                while (delta >= target_ticks && steps < 3 && g_mode != APP_DONE) {
                    qpc_last.QuadPart += target_ticks;
                    delta -= target_ticks;
                    steps++;
                    int more = (delta >= target_ticks && steps < 3) ? 1 : 0;
                    SendMessageA(window, WM_TIMER, 1, (LPARAM)more);
                }
                if (benchmark_frames) {
                    LARGE_INTEGER end;
                    GdiFlush();
                    QueryPerformanceCounter(&end);
                    printf("frame_time,%u,%.4f,%.4f,%d,%lld\n", ++presented,
                           1000.0 * (double)(end.QuadPart - qpc_now.QuadPart) / (double)qpc_freq.QuadPart,
                           1000.0 * (double)(end.QuadPart - previous_present.QuadPart) / (double)qpc_freq.QuadPart,
                           steps, (long long)dropped);
                    previous_present = end;
                    if (presented == benchmark_frames) break;
                }
            } else {
                int64_t remaining_ms = ((target_ticks - delta) * 1000) / qpc_freq.QuadPart;
                if (remaining_ms > 2) {
                    Sleep((DWORD)(remaining_ms - 1));
                } else {
                    Sleep(0);
                }
            }
        }
        if (benchmark_frames && presented != benchmark_frames) error = 1;
        timeEndPeriod(1);
    }

shutdown:
    free_paint_backbuffer();
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
