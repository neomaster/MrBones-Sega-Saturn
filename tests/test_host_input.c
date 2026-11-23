#include "platform/host_input.h"
#include "core/saturn_pad.h"
#include "core/runway_input.h"
#include <limits.h>
#include <stdio.h>
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

typedef struct { uint32_t id[4], value[4]; unsigned n; } log_t;
static void emit(void *p, uint32_t id, uint32_t value) {
    log_t *l = p;
    if (l->n < 4) { l->id[l->n] = id; l->value[l->n] = value; l->n++; }
}

/* Without a live keyboard or gamepad, host_input_*_buttons returns whatever the OS
 * reports; the contract under test is that returned bits stay within MB_PAD_*,
 * XInput/WinMM mappings match Saturn controls cleanly, and feeding them through
 * saturn_pad + runway_input_step stays coherent. */
int main(void) {
    CHECK(host_input_key_button(0x0d) == MB_PAD_START);
    CHECK(host_input_key_button(0x25) == MB_PAD_LEFT);
    CHECK(host_input_key_button('D') == MB_PAD_RIGHT);
    CHECK(host_input_key_button('X') == MB_PAD_B);
    CHECK(host_input_key_button(0xff) == 0);
    uint32_t buttons = host_input_keyboard_buttons();
    uint32_t joy_buttons = host_input_joystick_buttons();
    unsigned joy_count = host_input_joystick_count();
    CHECK((buttons & ~((1u << 13) - 1u)) == 0);
    CHECK((joy_buttons & ~((1u << 13) - 1u)) == 0);
    CHECK(joy_count <= 20u);

    /* XInput mapping: deadzone, INT16_MIN safety, D-Pad, face buttons, shoulders & triggers. */
    CHECK(host_input_map_xinput_state(0u, 10u, 20u, 3000, -3000) == 0u);
    CHECK(host_input_map_xinput_state(0x0001u | 0x0010u | 0x1000u | 0x2000u | 0x4000u | 0x8000u,
                                      80u, 90u, 0, 0) ==
          (MB_PAD_UP | MB_PAD_START | MB_PAD_B | MB_PAD_C | MB_PAD_A | MB_PAD_X |
           MB_PAD_Y | MB_PAD_Z));
    CHECK(host_input_map_xinput_state(0x0100u | 0x0200u, 0u, 0u, INT16_MIN, INT16_MIN) ==
          (MB_PAD_L | MB_PAD_R | MB_PAD_LEFT | MB_PAD_DOWN));
    CHECK(host_input_map_xinput_state(0x0020u, 0u, 0u, 20000, 20000) ==
          (MB_PAD_L | MB_PAD_RIGHT | MB_PAD_UP));

    /* WinMM HID / Saturn USB mapping: calibrated axes, 8-way POV hat & 9/10+ button Start. */
    CHECK(host_input_map_winmm_state(0u, 10u, 0xFFFFu, 32768u, 0u, 65535u, 32768u, 0u, 65535u) == 0u);
    CHECK(host_input_map_winmm_state(0x0007u, 9u, 4500u, 32768u, 0u, 65535u, 32768u, 0u, 65535u) ==
          (MB_PAD_UP | MB_PAD_RIGHT | MB_PAD_A | MB_PAD_B | MB_PAD_C));
    CHECK(host_input_map_winmm_state(0x0100u, 9u, 22500u, 32768u, 0u, 65535u, 32768u, 0u, 65535u) ==
          (MB_PAD_DOWN | MB_PAD_LEFT | MB_PAD_START));
    CHECK(host_input_map_winmm_state(0x0100u | 0x0200u, 12u, 0xFFFFu, 0u, 0u, 65535u, 65535u, 0u, 65535u) ==
          (MB_PAD_LEFT | MB_PAD_DOWN | MB_PAD_L | MB_PAD_START));

    saturn_pad pad = {0};
    saturn_pad_update_host(&pad, MB_PAD_LEFT | MB_PAD_C);
    uint32_t x = 100;
    log_t log = {0};
    CHECK(runway_input_step(&x, 0, (uint8_t)(pad.held >> 8), (uint8_t)(pad.pressed >> 8),
                            emit, &log));
    CHECK(x == 100u + 3932u);
    CHECK(log.n == 2);
    return 0;
}
