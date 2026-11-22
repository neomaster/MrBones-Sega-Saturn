#include "host_input.h"
#include "core/saturn_pad.h"

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

#ifdef _WIN32
#include <windows.h>

static int key_down(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
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
    return buttons;
}
#else
uint32_t host_input_keyboard_buttons(void) {
    return 0;
}
#endif
