#include "core/menu_options_view.h"

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} Writer;

static void w_char(Writer *w, char c) {
    if (w->len + 1 < w->cap) {
        w->buf[w->len] = c;
        w->len++;
    }
}

static void w_str(Writer *w, const char *s) {
    while (*s != '\0') {
        w_char(w, *s);
        s++;
    }
}

static void w_uint(Writer *w, unsigned v) {
    char digits[12];
    size_t n = 0;

    do {
        digits[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0 && n < sizeof digits);
    while (n > 0)
        w_char(w, digits[--n]);
}

static unsigned display_volume(const MenuState *s) {
    return s->volume <= MENU_VOLUME_MAX ? s->volume : (unsigned)MENU_VOLUME_MAX;
}

static void w_volume_bar(Writer *w, unsigned volume) {
    unsigned cell;

    w_char(w, '[');
    for (cell = 0; cell < MENU_VIEW_VOLUME_CELLS; cell++)
        w_char(w, cell < volume ? '|' : ' ');
    w_char(w, ']');
}

void menu_options_format_row(const MenuState *state, uint8_t row_index,
                             char *out_buf, size_t out_sz, bool selected) {
    Writer w;

    if (out_buf == NULL || out_sz == 0)
        return;
    w.buf = out_buf;
    w.cap = out_sz;
    w.len = 0;

    if (state != NULL && row_index < MENU_ROW_COUNT) {
        w_str(&w, selected ? "> " : "  ");
        switch (row_index) {
        case MENU_ROW_AUDIO_MODE:
            w_str(&w, "AUDIO: ");
            w_str(&w, menu_audio_is_stereo(state->audio_reg) ? "STEREO" : "MONO");
            break;
        case MENU_ROW_BUTTON_CONFIG:
            w_str(&w, "CONTROLLER: TYPE ");
            w_char(&w, (char)('A' + (state->button_preset < MENU_BUTTON_PRESETS
                                         ? state->button_preset
                                         : 0)));
            break;
        case MENU_ROW_VOLUME:
            w_str(&w, "VOLUME: ");
            w_volume_bar(&w, display_volume(state));
            w_char(&w, ' ');
            w_uint(&w, display_volume(state));
            w_char(&w, '/');
            w_uint(&w, MENU_VOLUME_MAX);
            break;
        default: /* MENU_ROW_BACK */
            w_str(&w, "BACK TO MAIN MENU");
            break;
        }
    }
    out_buf[w.len] = '\0';
}

void menu_options_sync_nvm(const MenuState *state, uint8_t *nvm_audio_reg,
                           uint8_t *nvm_volume) {
    if (state == NULL)
        return;
    if (nvm_audio_reg != NULL) {
        uint8_t mode = (uint8_t)(menu_audio_normalize(state->audio_reg) & MENU_AUDIO_MASK);
        *nvm_audio_reg = (uint8_t)((*nvm_audio_reg & ~MENU_AUDIO_MASK) | mode);
    }
    if (nvm_volume != NULL)
        *nvm_volume = (uint8_t)display_volume(state);
}

void menu_options_load_nvm(MenuState *state, uint8_t nvm_audio_reg,
                           uint8_t nvm_volume) {
    if (state == NULL)
        return;
    state->audio_reg = menu_audio_normalize(nvm_audio_reg);
    state->volume = nvm_volume <= MENU_VOLUME_MAX ? nvm_volume : (uint8_t)MENU_VOLUME_MAX;
}
