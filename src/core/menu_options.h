#ifndef MB_MENU_OPTIONS_H
#define MB_MENU_OPTIONS_H

/*
 * Subsystem A -- OPTIONS submenu navigation state machine.
 *
 * Clean-room interoperability specification (C99, no third-party code, no
 * system calls). Behaviour below is the *specified* behaviour of this module.
 *
 * Checked against 03MENU/03CODE (docs/ghidra-menu-options.txt,
 * docs/stage-progress-nvm.md section 8): the original OptionsScreen has three
 * entries, LevelSelect and two on/off toggles, and no audio-mode register,
 * button preset or volume row. The toggles are two 32-bit loader words
 * (0x0021d9dc, 0x0021d9e0), not bytes of _NvmShadow, and are not saved to the
 * backup. So this module is a host extension: its values have no byte in
 * nvmshadow.bin and must not be written into it.
 *
 * Rows (vertical cursor):
 *   0  MENU_ROW_AUDIO_MODE    Mono / Stereo
 *   1  MENU_ROW_BUTTON_CONFIG A/B/C button assignment preset
 *   2  MENU_ROW_VOLUME        Volume / SFX level
 *   3  MENU_ROW_BACK          Leave the submenu
 *
 * Input is edge-triggered: a button counts only on the frame where it is set
 * in `pad_buttons` and clear in `pad_prev`.
 *
 *   UP / DOWN       move the cursor, wrapping 0 <-> 3
 *   LEFT / RIGHT    adjust the current row (see below)
 *   A / C / START   activate the current row
 *
 * Opposite directions pressed on the same frame cancel each other.
 * Horizontal adjustment and activation act on the row the cursor was on at the
 * start of the frame; the cursor move is applied afterwards.
 *
 *   AUDIO_MODE     LEFT, RIGHT or activate toggles Mono <-> Stereo
 *   BUTTON_CONFIG  RIGHT/activate = next preset, LEFT = previous (wraps)
 *   VOLUME         RIGHT = +1, LEFT = -1, clamped to 0..MENU_VOLUME_MAX
 *   BACK           activate sets `done`
 */

#include <stdbool.h>
#include <stdint.h>

/* Pad bits. Same bit order as the Saturn digital pad layout used elsewhere in
 * the port; redefined here so this header has no project dependencies. */
#define MENU_PAD_LEFT  0x0001u
#define MENU_PAD_RIGHT 0x0002u
#define MENU_PAD_UP    0x0004u
#define MENU_PAD_DOWN  0x0008u
#define MENU_PAD_A     0x0010u
#define MENU_PAD_B     0x0020u
#define MENU_PAD_C     0x0040u
#define MENU_PAD_START 0x0080u

typedef enum {
    MENU_ROW_AUDIO_MODE = 0,
    MENU_ROW_BUTTON_CONFIG = 1,
    MENU_ROW_VOLUME = 2,
    MENU_ROW_BACK = 3,
    MENU_ROW_COUNT = 4 /* also the "invalid" return value of menu_options_step */
} MenuRow;

/* Audio register (host setting, not part of _NvmShadow): bits 0 and 1 select
 * the output mode.
 *   bit 0 set  -> Mono
 *   bit 1 set  -> Stereo
 * Exactly one of the two is valid. 00 and 11 are invalid and are treated as
 * the default, Stereo. Bits 2..7 belong to other settings and are preserved. */
#define MENU_AUDIO_BIT_MONO   0x01u
#define MENU_AUDIO_BIT_STEREO 0x02u
#define MENU_AUDIO_MASK       0x03u

#define MENU_VOLUME_MAX 15u
#define MENU_BUTTON_PRESETS 6u

/* Indexed by (audio_reg & MENU_AUDIO_MASK): the two-bit field after a toggle. */
extern const uint8_t menu_audio_toggle_table[4];
/* Indexed by (audio_reg & MENU_AUDIO_MASK): the two-bit field after cleaning up
 * an invalid value. Valid values map to themselves. */
extern const uint8_t menu_audio_normalize_table[4];
/* Preset p assigns logical slot menu_button_presets[p][i] to physical button
 * i (0 = A, 1 = B, 2 = C). Each row is a permutation of {0, 1, 2}. */
extern const uint8_t menu_button_presets[MENU_BUTTON_PRESETS][3];

typedef struct {
    uint8_t cursor;        /* MenuRow, 0..MENU_ROW_COUNT-1 */
    uint8_t audio_reg;     /* NVM audio register image, see MENU_AUDIO_* */
    uint8_t button_preset; /* 0..MENU_BUTTON_PRESETS-1 */
    uint8_t volume;        /* 0..MENU_VOLUME_MAX */
    bool done;             /* set when BACK was activated */
} MenuState;

/* Pure helpers on the audio register image. */
uint8_t menu_audio_toggle(uint8_t audio_reg);
uint8_t menu_audio_normalize(uint8_t audio_reg);
bool menu_audio_is_stereo(uint8_t audio_reg);

/* Cursor on row 0, `done` clear. Out-of-range values are clamped and the audio
 * register is normalised. */
void menu_options_init(MenuState *state, uint8_t audio_reg,
                       uint8_t button_preset, uint8_t volume);

/* Advance one frame. Deterministic: the result depends only on the three
 * arguments. Out-of-range fields in `state` are repaired first. Returns the
 * new cursor row, or MENU_ROW_COUNT when `state` is NULL. */
uint8_t menu_options_step(MenuState *state, uint16_t pad_buttons,
                          uint16_t pad_prev);

#endif
