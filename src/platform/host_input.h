#ifndef MB_HOST_INPUT_H
#define MB_HOST_INPUT_H
#include <stdint.h>

/*
 * Maps a host keyboard and joystick/gamepad snapshot to Saturn-equivalent
 * MB_PAD_* bits used by saturn_pad_update_host.
 * Default keyboard layout (US/QWERTY, Windows virtual keys):
 *   arrows / WASD -> D-pad
 *   Z -> A, X -> B, C -> C, Enter -> Start
 *   V -> X, B -> Y, N -> Z, Q -> L, E -> R
 * Connected Win32 joysticks (XInput and WinMM HID/DirectInput) are polled
 * automatically and ORed into host_input_keyboard_buttons().
 */
uint32_t host_input_key_button(unsigned vk);
uint32_t host_input_keyboard_buttons(void);
uint32_t host_input_joystick_buttons(void);
unsigned host_input_joystick_count(void);

uint32_t host_input_map_xinput_state(uint16_t buttons, uint8_t left_trigger,
                                     uint8_t right_trigger, int16_t thumb_lx,
                                     int16_t thumb_ly);
uint32_t host_input_map_winmm_state(uint32_t buttons, uint32_t num_buttons,
                                    uint32_t pov, uint32_t x_pos,
                                    uint32_t x_min, uint32_t x_max,
                                    uint32_t y_pos, uint32_t y_min,
                                    uint32_t y_max);

#endif
