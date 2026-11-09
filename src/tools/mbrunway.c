/*
 * Native RUNWAY phase executable.
 *
 * Criterion target: start, control, and finish RUNWAY with graphics + CD-DA.
 * This is still a progressive reconstruction with documented differences — not
 * a full SH-2 reimplementation of every brain/message.
 */
#include "core/cdimage.h"
#include "core/identity.h"
#include "core/resource.h"
#include "core/runway_phase.h"
#include "core/saturn_pad.h"
#include "core/runway_sfx.h"
#include "platform/host_input.h"
#include "platform/host_audio.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static runway_phase *g_phase;
static char g_title[160];
static runway_phase_state g_prev_state = RUNWAY_ST_STAND;
static runway_sfx g_sfx;
static int g_sfx_ok = 0;

static void host_play_pcm(void *user, const int8_t *pcm, size_t length, unsigned rate) {
    (void)user;
    host_audio_play_pcm8(pcm, length, rate);
}

static int load_runway_sfx(cdimage *cd) {
    if (runway_sfx_load(&g_sfx, cd)) return -1;
    g_sfx_ok = 1;
    return 0;
}

static void maybe_play_sfx(void) {
    if (!g_phase || !g_sfx_ok) return;
    /* §12n: multi-sample dispatch by sfx_flags (SoundStartSequence + PCM map). */
    if (g_phase->sound_event_id)
        runway_sfx_sound_event(&g_sfx, g_phase->sound_event_id);
    if (g_phase->sfx_flags)
        runway_sfx_dispatch(&g_sfx, g_phase->sfx_flags, host_play_pcm, NULL);
    g_prev_state = g_phase->state;
}

static void paint(HWND window) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(window, &ps);
    RECT rect;
    GetClientRect(window, &rect);
    FillRect(dc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(220, 220, 220));
    const char *help = "Arrows/WASD move | X=jump | Down=tall | C=msg | Enter=pause | Esc=abort";
    TextOutA(dc, 12, 8, help, (int)strlen(help));
    if (g_phase) TextOutA(dc, 12, 28, g_phase->status, (int)strlen(g_phase->status));
    if (g_phase && g_phase->frame_rgba) {
        static uint8_t bgra[320 * 224 * 4];
        size_t i;
        for (i = 0; i < 320u * 224u * 4u; i += 4u) {
            bgra[i + 0] = g_phase->frame_rgba[i + 2];
            bgra[i + 1] = g_phase->frame_rgba[i + 1];
            bgra[i + 2] = g_phase->frame_rgba[i + 0];
            bgra[i + 3] = 255u;
        }
        BITMAPINFO info = {0};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 320;
        info.bmiHeader.biHeight = -224;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        StretchDIBits(dc, 20, 52, 640, 448, 0, 0, 320, 224,
                      bgra, &info, DIB_RGB_COLORS, SRCCOPY);
    }
    EndPaint(window, &ps);
}

static LRESULT CALLBACK wndproc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == 1 && g_phase && !runway_phase_done(g_phase)) {
        saturn_pad pad = {0};
        saturn_pad_update_host(&pad, host_input_keyboard_buttons());
        runway_phase_step(g_phase, &pad);
        maybe_play_sfx();
        snprintf(g_title, sizeof g_title, "Mr. Bones RUNWAY - %s", g_phase->status);
        SetWindowTextA(window, g_title);
        InvalidateRect(window, NULL, FALSE);
        if (runway_phase_done(g_phase))
            KillTimer(window, 1);
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        if (g_phase) {
            g_phase->state = RUNWAY_ST_ABORTED;
            snprintf(g_phase->status, sizeof g_phase->status, "RUNWAY aborted");
        }
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_PAINT) { paint(window); return 0; }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(window, msg, wp, lp);
}

static void usage(void) {
    fprintf(stderr, "usage: mbrunway <disc1.cue|bin> <manifest> [--check] [--dump-ppm <path>]\n");
}

static void write_ppm(const char *path, const uint8_t *rgba, unsigned w, unsigned h) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        fputc(rgba[4 * i + 0], f);
        fputc(rgba[4 * i + 1], f);
        fputc(rgba[4 * i + 2], f);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    int check_only = 0;
    const char *dump_ppm = NULL;
    const char *disc1_path = "F:/MrBonesPort/disco1/Mr. Bones (USA) (Disc 1).cue";
    const char *manifest_path = "F:/MrBonesPort/manifest/versions.txt";
    if (argc == 2) { usage(); return 2; }
    if (argc >= 3) {
        disc1_path = argv[1];
        manifest_path = argv[2];
    }
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--check")) check_only = 1;
        else if (!strcmp(argv[i], "--dump-ppm") && i + 1 < argc) dump_ppm = argv[++i];
        else { usage(); return 2; }
    }

    char err[512];
    cdimage cd;
    id_manifest manifest;
    id_result result;
    runway_phase phase;
    host_audio *audio = NULL;
    int error = 0;
    int sfx_ok = 0;

    if (manifest_load(&manifest, manifest_path, err, sizeof err)) {
        fprintf(stderr, "manifest: %s\n", err); return 1;
    }
    if (cd_open(&cd, disc1_path, err, sizeof err)) {
        fprintf(stderr, "disc: %s\n", err); manifest_free(&manifest); return 1;
    }
    if (!identity_check(&manifest, &cd, 1, ID_QUICK, &result)) {
        fprintf(stderr, "identity: %s\n", result.message);
        cd_close(&cd); manifest_free(&manifest); return 1;
    }
    manifest_free(&manifest);

    if (runway_phase_load(&phase, &cd)) {
        fprintf(stderr, "RUNWAY phase load failed\n");
        cd_close(&cd);
        return 1;
    }
    g_phase = &phase;
    g_prev_state = phase.state;
    sfx_ok = load_runway_sfx(&cd) == 0;

    /* Smoke: walk right until EndLevel win (goal X) or step cap. */
    if (check_only) {
        saturn_pad pad = {0};
        int dumped = 0;
        for (int i = 0; i < 5000 && !runway_phase_done(&phase); i++) {
            saturn_pad_update_host(&pad, MB_PAD_RIGHT);
            runway_phase_step(&phase, &pad);
            maybe_play_sfx();
            if (dump_ppm && !dumped && (phase.bones_x / 256u) >= 138u) {
                saturn_pad idle_pad = {0};
                runway_phase_step(&phase, &idle_pad);
                write_ppm(dump_ppm, phase.frame_rgba, 320, 224);
                dumped = 1;
            }
        }
        printf("mbrunway phase: state=%d frames=%u status=%s\n",
               (int)phase.state, phase.frames, phase.status);
        printf("graphics=320x224 audio_track=%u sfx_bank=%u samples=%zu/%zu/%zu/%zu/%zu%s\n",
               phase.audio_track, RUNWAY_SFX_COUNT,
               g_sfx.samples[RUNWAY_SFX_WALK].length,
               g_sfx.samples[RUNWAY_SFX_TALL].length,
               g_sfx.samples[RUNWAY_SFX_C].length,
               g_sfx.samples[RUNWAY_SFX_JUMP].length,
               g_sfx.samples[RUNWAY_SFX_LAND].length,
               sfx_ok ? "" : " (load failed)");
        printf("skel=%d/%d seg=%d hits=%u paused=%d\n",
               phase.skel.skeletism, RUNWAY_SKEL_MAX, phase.skel.segments,
               phase.hazards.hits_applied, phase.skel.paused);
        if (phase.state != RUNWAY_ST_FINISHED) {
            fprintf(stderr, "check: phase did not finish (EndLevel win)\n");
            error = 1;
        } else if (!sfx_ok) {
            fprintf(stderr, "check: SND1 multi-SFX bank load failed\n");
            error = 1;
        } else {
            printf("check: RUNWAY skel/hazards/EndLevel + SND1 multi-SFX OK (reconstruction)\n");
        }
        goto done;
    }

    audio = host_audio_start_cdda(&cd, (int)phase.audio_track);
    phase.audio_started = audio != NULL;

    HINSTANCE instance = GetModuleHandleA(NULL);
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "MrBonesRunwayPhase";
    if (!RegisterClassA(&wc)) { error = 1; goto done; }
    HWND window = CreateWindowA(wc.lpszClassName, "Mr. Bones RUNWAY",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 700, 560,
                                NULL, NULL, instance, NULL);
    if (!window) { error = 1; goto done; }
    SetTimer(window, 1, 16, NULL);
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
        if (runway_phase_done(&phase) && phase.state == RUNWAY_ST_FINISHED) {
            Sleep(800);
            break;
        }
    }

done:
    host_audio_stop(audio);
    runway_phase_free(&phase);
    g_phase = NULL;
    runway_sfx_free(&g_sfx);
    g_sfx_ok = 0;
    cd_close(&cd);
    return error ? 1 : 0;
}
