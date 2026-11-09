#ifndef MB_HOST_INPUT_H
#define MB_HOST_INPUT_H
#include <stdint.h>

/*
 * Maps a host keyboard snapshot to Saturn-equivalent MB_PAD_* bits used by
 * saturn_pad_update_host. Default layout (US/QWERTY, Windows virtual keys):
 *   arrows / WASD → D-pad
 *   Z → A, X → B, C → C, Enter → Start
 *   V → X, B → Y, N → Z, Q → L, E → R
 * Returns the held button bitset; does not update pad state itself.
 */
uint32_t host_input_key_button(unsigned vk);
uint32_t host_input_keyboard_buttons(void);

#endif
