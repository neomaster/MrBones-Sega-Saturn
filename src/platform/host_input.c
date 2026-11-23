#include "host_input.h"
#include "core/saturn_pad.h"
#include <string.h>

#define MB_XINPUT_DPAD_UP        0x0001u
#define MB_XINPUT_DPAD_DOWN      0x0002u
#define MB_XINPUT_DPAD_LEFT      0x0004u
#define MB_XINPUT_DPAD_RIGHT     0x0008u
#define MB_XINPUT_START          0x0010u
#define MB_XINPUT_BACK           0x0020u
#define MB_XINPUT_LEFT_SHOULDER  0x0100u
#define MB_XINPUT_RIGHT_SHOULDER 0x0200u
#define MB_XINPUT_A              0x1000u
#define MB_XINPUT_B              0x2000u
#define MB_XINPUT_X              0x4000u
#define MB_XINPUT_Y              0x8000u
#define MB_XINPUT_STICK_DEADZONE 8000
#define MB_XINPUT_TRIG_THRESHOLD 64u

uint32_t host_input_key_button(unsigned vk) {
    switch (vk) {
    case 0x25: case 'A': return MB_PAD_LEFT;
    case 0x27: case 'D': return MB_PAD_RIGHT;
    case 0x26: case 'W': return MB_PAD_UP;
    case 0x28: case 'S': return MB_PAD_DOWN;
    case 'Z': return MB_PAD_A;
    case 'X': case 0x20: return MB_PAD_B;
    case 'C': return MB_PAD_C;
    case 0x0d: return MB_PAD_START;
    case 'V': return MB_PAD_X;
    case 'B': return MB_PAD_Y;
    case 'N': return MB_PAD_Z;
    case 'Q': return MB_PAD_L;
    case 'E': return MB_PAD_R;
    default: return 0;
    }
}

uint32_t host_input_map_xinput_state(uint16_t buttons, uint8_t left_trigger,
                                     uint8_t right_trigger, int16_t thumb_lx,
                                     int16_t thumb_ly) {
    uint32_t out = 0;
    if (buttons & MB_XINPUT_DPAD_LEFT) out |= MB_PAD_LEFT;
    if (buttons & MB_XINPUT_DPAD_RIGHT) out |= MB_PAD_RIGHT;
    if (buttons & MB_XINPUT_DPAD_UP) out |= MB_PAD_UP;
    if (buttons & MB_XINPUT_DPAD_DOWN) out |= MB_PAD_DOWN;

    if ((int32_t)thumb_lx <= -MB_XINPUT_STICK_DEADZONE) out |= MB_PAD_LEFT;
    if ((int32_t)thumb_lx >= MB_XINPUT_STICK_DEADZONE) out |= MB_PAD_RIGHT;
    if ((int32_t)thumb_ly >= MB_XINPUT_STICK_DEADZONE) out |= MB_PAD_UP;
    if ((int32_t)thumb_ly <= -MB_XINPUT_STICK_DEADZONE) out |= MB_PAD_DOWN;

    if (buttons & MB_XINPUT_X) out |= MB_PAD_A;
    if (buttons & MB_XINPUT_A) out |= MB_PAD_B;
    if (buttons & MB_XINPUT_B) out |= MB_PAD_C;
    if (buttons & MB_XINPUT_Y) out |= MB_PAD_X;
    if (left_trigger >= MB_XINPUT_TRIG_THRESHOLD) out |= MB_PAD_Y;
    if (right_trigger >= MB_XINPUT_TRIG_THRESHOLD) out |= MB_PAD_Z;
    if (buttons & (MB_XINPUT_LEFT_SHOULDER | MB_XINPUT_BACK)) out |= MB_PAD_L;
    if (buttons & MB_XINPUT_RIGHT_SHOULDER) out |= MB_PAD_R;
    if (buttons & MB_XINPUT_START) out |= MB_PAD_START;
    return out;
}

uint32_t host_input_map_winmm_state(uint32_t buttons, uint32_t num_buttons,
                                    uint32_t pov, uint32_t x_pos,
                                    uint32_t x_min, uint32_t x_max,
                                    uint32_t y_pos, uint32_t y_min,
                                    uint32_t y_max) {
    uint32_t out = 0;
    if (x_max > x_min) {
        int64_t mid = ((int64_t)x_min + (int64_t)x_max) / 2;
        int64_t span = ((int64_t)x_max - (int64_t)x_min) / 2;
        int64_t dz = span / 3;
        if (span >= 4) {
            if ((int64_t)x_pos <= mid - dz) out |= MB_PAD_LEFT;
            if ((int64_t)x_pos >= mid + dz) out |= MB_PAD_RIGHT;
        }
    }
    if (y_max > y_min) {
        int64_t mid = ((int64_t)y_min + (int64_t)y_max) / 2;
        int64_t span = ((int64_t)y_max - (int64_t)y_min) / 2;
        int64_t dz = span / 3;
        if (span >= 4) {
            if ((int64_t)y_pos <= mid - dz) out |= MB_PAD_UP;
            if ((int64_t)y_pos >= mid + dz) out |= MB_PAD_DOWN;
        }
    }
    if (pov <= 36000u) {
        if (pov <= 6750u || pov >= 29250u) out |= MB_PAD_UP;
        if (pov >= 2250u && pov <= 15750u) out |= MB_PAD_RIGHT;
        if (pov >= 11250u && pov <= 24750u) out |= MB_PAD_DOWN;
        if (pov >= 20250u && pov <= 33750u) out |= MB_PAD_LEFT;
    }

    if (buttons & (1u << 0)) out |= MB_PAD_A;
    if (buttons & (1u << 1)) out |= MB_PAD_B;
    if (buttons & (1u << 2)) out |= MB_PAD_C;
    if (buttons & (1u << 3)) out |= MB_PAD_X;
    if (buttons & (1u << 4)) out |= MB_PAD_Y;
    if (buttons & (1u << 5)) out |= MB_PAD_Z;
    if (buttons & (1u << 6)) out |= MB_PAD_L;
    if (buttons & (1u << 7)) out |= MB_PAD_R;
    if (buttons & (1u << 8))
        out |= (num_buttons > 0u && num_buttons <= 9u) ? MB_PAD_START : MB_PAD_L;
    if (buttons & (1u << 9)) out |= MB_PAD_START;
    return out;
}

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>

typedef struct {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
} mb_xinput_gamepad;

typedef struct {
    DWORD dwPacketNumber;
    mb_xinput_gamepad Gamepad;
} mb_xinput_state;

typedef DWORD (WINAPI *pfn_xinput_get_state)(DWORD, mb_xinput_state *);
typedef UINT (WINAPI *pfn_joy_get_num_devs)(void);
typedef MMRESULT (WINAPI *pfn_joy_get_dev_caps_a)(UINT_PTR, LPJOYCAPSA, UINT);
typedef MMRESULT (WINAPI *pfn_joy_get_pos_ex)(UINT, LPJOYINFOEX);

#define MB_XINPUT_MAX_SLOTS 4u
#define MB_WINMM_MAX_SLOTS  16u
#define MB_RESCAN_MS        750ull

typedef struct {
    int loaded;
    pfn_xinput_get_state xinput_get_state;
    pfn_joy_get_num_devs joy_get_num_devs;
    pfn_joy_get_dev_caps_a joy_get_dev_caps_a;
    pfn_joy_get_pos_ex joy_get_pos_ex;
    uint8_t xinput_active[MB_XINPUT_MAX_SLOTS];
    uint8_t winmm_active[MB_WINMM_MAX_SLOTS];
    JOYCAPSA winmm_caps[MB_WINMM_MAX_SLOTS];
    ULONGLONG next_rescan_ms;
} mb_joy_host;

static mb_joy_host g_joy;

static void init_joy_apis(void) {
    static const char *const k_xinput_dlls[] = {
        "xinput1_4.dll", "xinput9_1_0.dll", "xinput1_3.dll", NULL
    };
    HMODULE mod = NULL;
    FARPROC proc = NULL;
    unsigned i;
    if (g_joy.loaded) return;
    g_joy.loaded = 1;
    for (i = 0; k_xinput_dlls[i]; i++) {
        mod = LoadLibraryA(k_xinput_dlls[i]);
        if (mod) break;
    }
    if (mod) {
        proc = GetProcAddress(mod, "XInputGetState");
        if (proc) memcpy(&g_joy.xinput_get_state, &proc, sizeof proc);
    }
    mod = LoadLibraryA("winmm.dll");
    if (mod) {
        proc = GetProcAddress(mod, "joyGetNumDevs");
        if (proc) memcpy(&g_joy.joy_get_num_devs, &proc, sizeof proc);
        proc = GetProcAddress(mod, "joyGetDevCapsA");
        if (proc) memcpy(&g_joy.joy_get_dev_caps_a, &proc, sizeof proc);
        proc = GetProcAddress(mod, "joyGetPosEx");
        if (proc) memcpy(&g_joy.joy_get_pos_ex, &proc, sizeof proc);
    }
}

static uint32_t poll_joysticks_internal(unsigned *out_count) {
    ULONGLONG now;
    int do_rescan;
    unsigned count = 0;
    unsigned xinput_count = 0;
    uint32_t buttons = 0;
    unsigned i;

    init_joy_apis();
    now = GetTickCount64();
    do_rescan = (now >= g_joy.next_rescan_ms);
    if (do_rescan)
        g_joy.next_rescan_ms = now + MB_RESCAN_MS;

    if (g_joy.xinput_get_state) {
        for (i = 0; i < MB_XINPUT_MAX_SLOTS; i++) {
            mb_xinput_state xs;
            if (!g_joy.xinput_active[i] && !do_rescan) continue;
            memset(&xs, 0, sizeof xs);
            if (g_joy.xinput_get_state((DWORD)i, &xs) == ERROR_SUCCESS) {
                g_joy.xinput_active[i] = 1;
                xinput_count++;
                buttons |= host_input_map_xinput_state(
                    xs.Gamepad.wButtons, xs.Gamepad.bLeftTrigger,
                    xs.Gamepad.bRightTrigger, xs.Gamepad.sThumbLX,
                    xs.Gamepad.sThumbLY);
            } else {
                g_joy.xinput_active[i] = 0;
            }
        }
    }
    count = xinput_count;

    if (g_joy.joy_get_num_devs && g_joy.joy_get_dev_caps_a && g_joy.joy_get_pos_ex) {
        UINT max_devs = g_joy.joy_get_num_devs();
        if (max_devs > MB_WINMM_MAX_SLOTS) max_devs = MB_WINMM_MAX_SLOTS;
        for (i = 0; i < (unsigned)max_devs; i++) {
            JOYINFOEX ji;
            if (!g_joy.winmm_active[i]) {
                if (!do_rescan) continue;
                memset(&g_joy.winmm_caps[i], 0, sizeof g_joy.winmm_caps[i]);
                if (g_joy.joy_get_dev_caps_a((UINT_PTR)i, &g_joy.winmm_caps[i],
                                             (UINT)sizeof g_joy.winmm_caps[i]) != JOYERR_NOERROR)
                    continue;
            }
            /* Skip WinMM's legacy wrapper for Microsoft/XInput controllers when XInput is active. */
            if (xinput_count > 0 && g_joy.winmm_caps[i].wMid == 0x045Eu) {
                if (do_rescan) {
                    memset(&ji, 0, sizeof ji);
                    ji.dwSize = (DWORD)sizeof ji;
                    ji.dwFlags = JOY_RETURNBUTTONS;
                    g_joy.winmm_active[i] =
                        (g_joy.joy_get_pos_ex((UINT)i, &ji) == JOYERR_NOERROR) ? 1u : 0u;
                }
                continue;
            }
            memset(&ji, 0, sizeof ji);
            ji.dwSize = (DWORD)sizeof ji;
            ji.dwFlags = JOY_RETURNX | JOY_RETURNY | JOY_RETURNPOV | JOY_RETURNBUTTONS;
            if (g_joy.joy_get_pos_ex((UINT)i, &ji) == JOYERR_NOERROR) {
                const JOYCAPSA *caps = &g_joy.winmm_caps[i];
                uint32_t pov = ((caps->wCaps & JOYCAPS_HASPOV) || ji.dwPOV <= 36000u)
                                   ? (uint32_t)ji.dwPOV : 0xFFFFu;
                uint32_t x_min = (caps->wXmax > caps->wXmin) ? (uint32_t)caps->wXmin : 0u;
                uint32_t x_max = (caps->wXmax > caps->wXmin) ? (uint32_t)caps->wXmax : 0u;
                uint32_t y_min = (caps->wYmax > caps->wYmin) ? (uint32_t)caps->wYmin : 0u;
                uint32_t y_max = (caps->wYmax > caps->wYmin) ? (uint32_t)caps->wYmax : 0u;
                g_joy.winmm_active[i] = 1;
                count++;
                buttons |= host_input_map_winmm_state(
                    (uint32_t)ji.dwButtons, (uint32_t)caps->wNumButtons, pov,
                    (uint32_t)ji.dwXpos, x_min, x_max,
                    (uint32_t)ji.dwYpos, y_min, y_max);
            } else {
                g_joy.winmm_active[i] = 0;
            }
        }
    }

    if (out_count) *out_count = count;
    return buttons;
}

static int key_down(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

uint32_t host_input_joystick_buttons(void) {
    return poll_joysticks_internal(NULL);
}

unsigned host_input_joystick_count(void) {
    unsigned count = 0;
    (void)poll_joysticks_internal(&count);
    return count;
}

uint32_t host_input_keyboard_buttons(void) {
    uint32_t buttons = 0;
    if (key_down(VK_LEFT) || key_down('A')) buttons |= MB_PAD_LEFT;
    if (key_down(VK_RIGHT) || key_down('D')) buttons |= MB_PAD_RIGHT;
    if (key_down(VK_UP) || key_down('W')) buttons |= MB_PAD_UP;
    if (key_down(VK_DOWN) || key_down('S')) buttons |= MB_PAD_DOWN;
    if (key_down('Z')) buttons |= MB_PAD_A;
    if (key_down('X') || key_down(VK_SPACE)) buttons |= MB_PAD_B;
    if (key_down('C')) buttons |= MB_PAD_C;
    if (key_down(VK_RETURN)) buttons |= MB_PAD_START;
    if (key_down('V')) buttons |= MB_PAD_X;
    if (key_down('B')) buttons |= MB_PAD_Y;
    if (key_down('N')) buttons |= MB_PAD_Z;
    if (key_down('Q')) buttons |= MB_PAD_L;
    if (key_down('E')) buttons |= MB_PAD_R;
    buttons |= host_input_joystick_buttons();
    return buttons;
}
#else
uint32_t host_input_joystick_buttons(void) {
    return 0;
}
unsigned host_input_joystick_count(void) {
    return 0;
}
uint32_t host_input_keyboard_buttons(void) {
    return 0;
}
#endif
