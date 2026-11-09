#ifndef MB_MENU_OPTIONS_VIEW_H
#define MB_MENU_OPTIONS_VIEW_H

/*
 * Text view and NVM sync for the 4-row OPTIONS submenu (state machine in
 * core/menu_options.h).
 *
 * Clean-room specification (C99, no allocation, no globals). The row texts
 * below are the specified texts of this module; they were not recovered from
 * the original binary.
 *
 * Every row is a 2-character selection prefix followed by the row text:
 *
 *   selected    "> "      not selected    "  "
 *
 *   row 0  AUDIO: STEREO            AUDIO: MONO
 *   row 1  CONTROLLER: TYPE A       ... TYPE F        (preset 0..5)
 *   row 2  VOLUME: [||||||||       ] 8/15
 *   row 3  BACK TO MAIN MENU
 *
 * The volume bar has MENU_VIEW_VOLUME_CELLS cells, one per volume step: `volume`
 * bars '|' followed by blanks, inside brackets, then " volume/MENU_VOLUME_MAX".
 *
 * A corrupt state is displayed the way menu_options_step would repair it:
 * volume above the maximum shows as the maximum, a preset out of range shows as
 * TYPE A, an invalid audio register (00 or 11 in bits 0..1) shows as STEREO.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/menu_options.h"

#define MENU_VIEW_VOLUME_CELLS MENU_VOLUME_MAX
/* Buffer size that fits any row including the terminating NUL (longest: 33 + 1). */
#define MENU_VIEW_ROW_BUF 40u

/*
 * Write the text of row `row_index` (0..MENU_ROW_COUNT-1) with the selection
 * prefix chosen by `selected`. The result is always NUL-terminated when
 * out_sz > 0 and is truncated, never overflowed, when out_sz is too small.
 * A NULL state or an out-of-range row yields an empty string. A NULL out_buf or
 * out_sz == 0 writes nothing.
 */
void menu_options_format_row(const MenuState *state, uint8_t row_index,
                             char *out_buf, size_t out_sz, bool selected);

/*
 * Screen -> NVM. Writes the audio mode into bits 0..1 of *nvm_audio_reg (bits
 * 2..7 of the byte are other settings and are left as they are) and the volume,
 * clamped to 0..MENU_VOLUME_MAX, into *nvm_volume. Either pointer may be NULL
 * and is then skipped; a NULL state writes nothing. The controller preset is
 * not part of this call: it has no byte in the signature.
 */
void menu_options_sync_nvm(const MenuState *state, uint8_t *nvm_audio_reg,
                           uint8_t *nvm_volume);

/*
 * NVM -> screen. Loads the audio register (normalised) and the volume (clamped)
 * into the state. Cursor, controller preset and `done` are left as they are, so
 * it can be called while the menu is open.
 */
void menu_options_load_nvm(MenuState *state, uint8_t nvm_audio_reg,
                           uint8_t nvm_volume);

#endif
