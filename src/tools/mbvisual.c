/* Diagnostic viewer for RUNWAY: textures, pan frame, composed preview and
 * keyboard→pad→input_step. This is not gameplay. */
#include "core/cdimage.h"
#include "core/identity.h"
#include "core/runway_assets.h"
#include "core/runway_sprite.h"
#include "core/runway_compose.h"
#include "core/runway_input.h"
#include "core/sprite_decode.h"
#include "core/resource.h"
#include "core/pan_frame.h"
#include "core/pan_palette.h"
#include "core/saturn_pad.h"
#include "platform/host_input.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *pixels;
    unsigned width, height;
} image;

enum { PIC_TEX0 = 0, PIC_TEX1 = 1, PIC_BG = 2, PIC_COMPOSE = 3, PIC_COUNT = 4 };

static image pictures[PIC_COUNT];
static unsigned selected;
static saturn_pad pad;
static uint32_t bones_x;
static uint32_t last_message;
static unsigned message_count;
static char status_line[160];

static void on_bbs(void *context, uint32_t message, uint32_t value) {
    (void)context;
    (void)value;
    last_message = message;
    message_count++;
}

static int decode_image(const runway_assets *assets, const uint8_t *palette,
                        uint32_t address, image *out) {
    stxtr_record record;
    uint16_t pmod;
    if (runway_sprite_resolve(assets->code, assets->code_size, address,
                              assets->textures, assets->textures_size, &record, &pmod))
        return -1;
    size_t size = (size_t)record.width * record.height * 4;
    uint8_t *pixels = malloc(size);
    if (!pixels) return -1;
    if (mb_sprite_decode(record.data, record.size, record.width, record.height, pmod,
                         palette, 1024, pixels, size)) {
        free(pixels);
        return -1;
    }
    for (size_t i = 0; i < size; i += 4) {
        uint8_t red = pixels[i];
        pixels[i] = pixels[i + 2];
        pixels[i + 2] = red;
        if (!pixels[i + 3]) pixels[i] = pixels[i + 1] = pixels[i + 2] = 0;
    }
    out->pixels = pixels;
    out->width = record.width;
    out->height = record.height;
    return 0;
}

static int decode_background(cdimage *cd, const uint8_t *palette, image *out) {
    void *frame = NULL;
    size_t frame_size = 0, collision_size, consumed;
    uint8_t collision[4960];
    uint8_t *planes = calloc(1, PAN_BUFFER_SIZE);
    if (!planes) return -1;
    if (resource_load(cd, "/04RUNWAY/15PAN", &frame, &frame_size) ||
        pan_frame_decode(frame, frame_size, planes, PAN_BUFFER_SIZE, 0,
                         collision, sizeof collision, &collision_size, &consumed)) {
        free(frame);
        free(planes);
        return -1;
    }
    uint8_t *pixels = malloc(PAN_PLANE_SIZE * 4);
    if (!pixels) { free(frame); free(planes); return -1; }
    for (size_t i = 0; i < PAN_PLANE_SIZE; i++) {
        size_t c = (size_t)planes[i] * 4;
        pixels[4 * i] = palette[c + 3];
        pixels[4 * i + 1] = palette[c + 2];
        pixels[4 * i + 2] = palette[c + 1];
        pixels[4 * i + 3] = 255;
    }
    free(frame);
    free(planes);
    out->pixels = pixels;
    out->width = PAN_WIDTH;
    out->height = PAN_HEIGHT;
    return 0;
}

static int build_compose(void) {
    image *bg = &pictures[PIC_BG];
    image *a = &pictures[PIC_TEX0];
    image *b = &pictures[PIC_TEX1];
    size_t size = (size_t)bg->width * bg->height * 4;
    uint8_t *pixels = malloc(size);
    if (!pixels) return -1;
    if (runway_compose_blit_opaque(pixels, bg->width, bg->height,
                                   bg->pixels, bg->width, bg->height, 0, 0) ||
        runway_compose_blit_sprite(pixels, bg->width, bg->height,
                                   a->pixels, a->width, a->height, 24, 40) ||
        runway_compose_blit_sprite(pixels, bg->width, bg->height,
                                   b->pixels, b->width, b->height, 160, 96)) {
        free(pixels);
        return -1;
    }
    pictures[PIC_COMPOSE].pixels = pixels;
    pictures[PIC_COMPOSE].width = bg->width;
    pictures[PIC_COMPOSE].height = bg->height;
    return 0;
}

static void refresh_status(void) {
    snprintf(status_line, sizeof status_line,
             "img %u/%u  x=%u  held=0x%04X  pressed=0x%04X  msgs=%u last=0x%08X",
             selected + 1, PIC_COUNT, bones_x, pad.held, pad.pressed,
             message_count, last_message);
}

static void step_input(void) {
    uint32_t buttons = host_input_keyboard_buttons();
    saturn_pad_update_host(&pad, buttons);
    runway_input_step(&bones_x, 0, (uint8_t)(pad.held >> 8), (uint8_t)(pad.pressed >> 8),
                      on_bbs, NULL);
    refresh_status();
}

static LRESULT CALLBACK window_proc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && (wp == VK_LEFT || wp == VK_RIGHT) &&
        !(GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
        /* Unshifted arrows only change the diagnostic page; shifted arrows feed the pad. */
        selected = (selected + (wp == VK_RIGHT ? 1 : PIC_COUNT - 1)) % PIC_COUNT;
        refresh_status();
        InvalidateRect(window, NULL, TRUE);
        return 0;
    }
    if (msg == WM_TIMER && wp == 1) {
        step_input();
        InvalidateRect(window, NULL, FALSE);
        return 0;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(window, &ps);
        RECT rect;
        GetClientRect(window, &rect);
        FillRect(dc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkMode(dc, TRANSPARENT);
        const char *help =
            "RUNWAY diagnostic - arrows: page - Shift+arrows/WASD/ZXC: pad - not gameplay";
        TextOutA(dc, 12, 8, help, (int)strlen(help));
        TextOutA(dc, 12, 28, status_line, (int)strlen(status_line));
        image *pic = &pictures[selected];
        if (pic->pixels && pic->width && pic->height) {
            BITMAPINFO info = {0};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = (LONG)pic->width;
            info.bmiHeader.biHeight = -(LONG)pic->height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, COLORONCOLOR);
            int sx = (rect.right - 64) / (int)pic->width;
            int sy = (rect.bottom - 88) / (int)pic->height;
            int scale = sx < sy ? sx : sy;
            if (scale < 1) scale = 1;
            if (scale > 4) scale = 4;
            StretchDIBits(dc, 32, 56, (int)pic->width * scale, (int)pic->height * scale,
                          0, 0, (int)pic->width, (int)pic->height, pic->pixels,
                          &info, DIB_RGB_COLORS, SRCCOPY);
        }
        EndPaint(window, &ps);
        return 0;
    }
    if (msg == WM_DESTROY) {
        KillTimer(window, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, msg, wp, lp);
}

int main(int argc, char **argv) {
    int check_only = argc == 4 && !strcmp(argv[3], "--check");
    if (argc != 3 && !check_only) {
        fprintf(stderr, "usage: mbvisual <disc1.cue|bin> <manifest> [--check]\n");
        return 2;
    }
    char err[512];
    cdimage cd;
    id_manifest manifest;
    id_result result;
    runway_assets assets;
    int error = 0;
    memset(pictures, 0, sizeof pictures);
    memset(&pad, 0, sizeof pad);
    bones_x = 0;
    last_message = 0;
    message_count = 0;
    refresh_status();

    if (manifest_load(&manifest, argv[2], err, sizeof err)) {
        fprintf(stderr, "manifest: %s\n", err); return 1;
    }
    if (cd_open(&cd, argv[1], err, sizeof err)) {
        fprintf(stderr, "disc: %s\n", err); manifest_free(&manifest); return 1;
    }
    if (!identity_check(&manifest, &cd, 1, ID_QUICK, &result)) {
        fprintf(stderr, "identity: %s\n", result.message);
        cd_close(&cd); manifest_free(&manifest); return 1;
    }
    manifest_free(&manifest);
    if (runway_assets_load(&cd, &assets)) {
        fprintf(stderr, "unable to load RUNWAY resources\n"); cd_close(&cd); return 1;
    }
    uint32_t palette_words[1024] = {0};
    uint8_t palette[4096] = {0};
    size_t palette_consumed;
    error = assets.code_size <= 0x1ff50 ||
            pan_palette_apply((const uint8_t *)assets.code + 0x1ff50,
                              assets.code_size - 0x1ff50, palette_words, 1024,
                              &palette_consumed);
    if (!error) {
        for (size_t i = 0; i < 1024; i++) {
            palette[4 * i + 1] = (uint8_t)(palette_words[i] >> 16);
            palette[4 * i + 2] = (uint8_t)(palette_words[i] >> 8);
            palette[4 * i + 3] = (uint8_t)palette_words[i];
        }
        error = decode_image(&assets, palette + 1 * 1024, 0x0023204c, &pictures[PIC_TEX0]) ||
                decode_image(&assets, palette + 2 * 1024, 0x0024fd98, &pictures[PIC_TEX1]) ||
                decode_background(&cd, palette + 1 * 1024, &pictures[PIC_BG]) ||
                build_compose();
    }
    runway_assets_free(&assets);
    cd_close(&cd);
    if (error) {
        fprintf(stderr, "unable to decode RUNWAY images\n");
        goto cleanup;
    }
    if (check_only) {
        printf("RUNWAY images: %ux%u, %ux%u, %ux%u, compose %ux%u\n",
               pictures[0].width, pictures[0].height,
               pictures[1].width, pictures[1].height,
               pictures[2].width, pictures[2].height,
               pictures[3].width, pictures[3].height);
        /* Smoke the host keyboard → pad → input path without a window. */
        saturn_pad_update_host(&pad, MB_PAD_LEFT | MB_PAD_C);
        runway_input_step(&bones_x, 0, (uint8_t)(pad.held >> 8),
                          (uint8_t)(pad.pressed >> 8), on_bbs, NULL);
        printf("input smoke: x=%u msgs=%u last=0x%08X\n", bones_x, message_count, last_message);
        goto cleanup;
    }

    HINSTANCE instance = GetModuleHandleA(NULL);
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "MrBonesDiagnostic";
    if (!RegisterClassA(&wc)) {
        fprintf(stderr, "RegisterClass failed\n");
        error = 1;
        goto cleanup;
    }
    HWND window = CreateWindowA(wc.lpszClassName, "Mr. Bones - RUNWAY diagnostic",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                                720, 560, NULL, NULL, instance, NULL);
    if (!window) {
        fprintf(stderr, "CreateWindow failed\n");
        error = 1;
        goto cleanup;
    }
    SetTimer(window, 1, 16, NULL);
    selected = PIC_COMPOSE;
    refresh_status();
    MSG message;
    int status;
    while ((status = GetMessageA(&message, NULL, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    if (status < 0) error = 1;
cleanup:
    for (unsigned i = 0; i < PIC_COUNT; i++) free(pictures[i].pixels);
    return error ? 1 : 0;
}
