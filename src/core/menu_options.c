#include "core/menu_options.h"

#include <stddef.h>

const uint8_t menu_audio_toggle_table[4] = {
    MENU_AUDIO_BIT_STEREO, /* 00 invalid -> default */
    MENU_AUDIO_BIT_STEREO, /* 01 mono    -> stereo  */
    MENU_AUDIO_BIT_MONO,   /* 10 stereo  -> mono    */
    MENU_AUDIO_BIT_STEREO  /* 11 invalid -> default */
};

const uint8_t menu_audio_normalize_table[4] = {
    MENU_AUDIO_BIT_STEREO, /* 00 invalid -> default */
    MENU_AUDIO_BIT_MONO,   /* 01 mono               */
    MENU_AUDIO_BIT_STEREO, /* 10 stereo             */
    MENU_AUDIO_BIT_STEREO  /* 11 invalid -> default */
};

const uint8_t menu_button_presets[MENU_BUTTON_PRESETS][3] = {
    {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}
};

uint8_t menu_audio_toggle(uint8_t audio_reg) {
    return (uint8_t)((audio_reg & ~MENU_AUDIO_MASK) |
                     menu_audio_toggle_table[audio_reg & MENU_AUDIO_MASK]);
}

uint8_t menu_audio_normalize(uint8_t audio_reg) {
    return (uint8_t)((audio_reg & ~MENU_AUDIO_MASK) |
                     menu_audio_normalize_table[audio_reg & MENU_AUDIO_MASK]);
}

bool menu_audio_is_stereo(uint8_t audio_reg) {
    return (menu_audio_normalize(audio_reg) & MENU_AUDIO_BIT_STEREO) != 0;
}

void menu_options_init(MenuState *state, uint8_t audio_reg,
                       uint8_t button_preset, uint8_t volume) {
    if (state == NULL)
        return;
    state->cursor = MENU_ROW_AUDIO_MODE;
    state->audio_reg = menu_audio_normalize(audio_reg);
    state->button_preset = button_preset < MENU_BUTTON_PRESETS ? button_preset : 0;
    state->volume = volume <= MENU_VOLUME_MAX ? volume : (uint8_t)MENU_VOLUME_MAX;
    state->done = false;
}

static void menu_repair(MenuState *state) {
    if (state->cursor >= MENU_ROW_COUNT)
        state->cursor = MENU_ROW_AUDIO_MODE;
    state->audio_reg = menu_audio_normalize(state->audio_reg);
    if (state->button_preset >= MENU_BUTTON_PRESETS)
        state->button_preset = 0;
    if (state->volume > MENU_VOLUME_MAX)
        state->volume = (uint8_t)MENU_VOLUME_MAX;
}

uint8_t menu_options_step(MenuState *state, uint16_t pad_buttons,
                          uint16_t pad_prev) {
    uint16_t pressed;
    int dx, dy;
    bool activate;
    uint8_t row;

    if (state == NULL)
        return (uint8_t)MENU_ROW_COUNT;
    menu_repair(state);

    pressed = (uint16_t)(pad_buttons & (uint16_t)~pad_prev);
    dx = ((pressed & MENU_PAD_RIGHT) ? 1 : 0) - ((pressed & MENU_PAD_LEFT) ? 1 : 0);
    dy = ((pressed & MENU_PAD_DOWN) ? 1 : 0) - ((pressed & MENU_PAD_UP) ? 1 : 0);
    activate = (pressed & (MENU_PAD_A | MENU_PAD_C | MENU_PAD_START)) != 0;
    row = state->cursor;

    /* Value changes act on the row the cursor started the frame on. */
    switch (row) {
    case MENU_ROW_AUDIO_MODE:
        if (dx != 0 || activate)
            state->audio_reg = menu_audio_toggle(state->audio_reg);
        break;
    case MENU_ROW_BUTTON_CONFIG: {
        int step = dx != 0 ? dx : (activate ? 1 : 0);
        if (step != 0)
            state->button_preset = (uint8_t)((state->button_preset +
                                              (int)MENU_BUTTON_PRESETS + step) %
                                             (int)MENU_BUTTON_PRESETS);
        break;
    }
    case MENU_ROW_VOLUME:
        if (dx > 0 && state->volume < MENU_VOLUME_MAX)
            state->volume++;
        else if (dx < 0 && state->volume > 0)
            state->volume--;
        break;
    case MENU_ROW_BACK:
        if (activate)
            state->done = true;
        break;
    default:
        break;
    }

    /* Cursor move, wrapping over MENU_ROW_COUNT rows. */
    if (dy != 0)
        state->cursor = (uint8_t)((state->cursor + MENU_ROW_COUNT + dy) %
                                  MENU_ROW_COUNT);
    return state->cursor;
}
