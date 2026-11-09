#include "core/menu_options.h"
#include <stdio.h>
#define CHECK(c) do {if (!(c)) {printf("FAIL %d: %s\n",__LINE__,#c);return 1;}} while (0)

/* One frame where `btn` is newly pressed (prev = 0). */
static uint8_t tap(MenuState *s, uint16_t btn) { return menu_options_step(s, btn, 0); }

int main(void) {
    MenuState s;

    /* audio register table */
    CHECK(menu_audio_toggle(0x01) == 0x02);
    CHECK(menu_audio_toggle(0x02) == 0x01);
    CHECK(menu_audio_toggle(0x00) == 0x02);
    CHECK(menu_audio_toggle(0x03) == 0x02);
    CHECK(menu_audio_toggle(0xFD) == 0xFE); /* upper bits preserved */
    CHECK(menu_audio_toggle(0xFE) == 0xFD);
    CHECK(menu_audio_normalize(0x00) == 0x02 && menu_audio_normalize(0x03) == 0x02);
    CHECK(menu_audio_normalize(0x01) == 0x01 && menu_audio_normalize(0x02) == 0x02);
    CHECK(menu_audio_normalize(0xA5) == 0xA5 && menu_audio_normalize(0xA7) == 0xA6);
    CHECK(menu_audio_is_stereo(0x02) && !menu_audio_is_stereo(0x01));
    CHECK(menu_audio_is_stereo(0x00) && menu_audio_is_stereo(0x03));
    for (unsigned r = 0; r < 256; r++) { /* toggling twice returns a valid value */
        uint8_t t = menu_audio_toggle((uint8_t)r);
        CHECK((t & 3) == 1 || (t & 3) == 2);
        CHECK((unsigned)(t & ~3) == (r & ~3u));
        CHECK(menu_audio_toggle(t) != t);
    }

    /* button presets are permutations of {0,1,2} */
    for (unsigned p = 0; p < MENU_BUTTON_PRESETS; p++) {
        unsigned seen = 0;
        for (unsigned i = 0; i < 3; i++) seen |= 1u << menu_button_presets[p][i];
        CHECK(seen == 7u);
        for (unsigned q = p + 1; q < MENU_BUTTON_PRESETS; q++)
            CHECK(menu_button_presets[p][0] != menu_button_presets[q][0] ||
                  menu_button_presets[p][1] != menu_button_presets[q][1]);
    }

    /* init clamps and normalises */
    menu_options_init(&s, 0x03, 99, 200);
    CHECK(s.cursor == 0 && s.audio_reg == 0x02 && s.button_preset == 0 &&
          s.volume == MENU_VOLUME_MAX && !s.done);

    /* cursor wraps both ways, and returns the new row */
    menu_options_init(&s, 0x02, 0, 5);
    CHECK(tap(&s, MENU_PAD_UP) == MENU_ROW_BACK);
    CHECK(tap(&s, MENU_PAD_DOWN) == MENU_ROW_AUDIO_MODE);
    CHECK(tap(&s, MENU_PAD_DOWN) == MENU_ROW_BUTTON_CONFIG);
    CHECK(tap(&s, MENU_PAD_DOWN) == MENU_ROW_VOLUME);
    CHECK(tap(&s, MENU_PAD_DOWN) == MENU_ROW_BACK);
    CHECK(tap(&s, MENU_PAD_DOWN) == MENU_ROW_AUDIO_MODE);

    /* edge detection: a held button does not repeat */
    menu_options_init(&s, 0x02, 0, 5);
    CHECK(menu_options_step(&s, MENU_PAD_DOWN, 0) == 1);
    CHECK(menu_options_step(&s, MENU_PAD_DOWN, MENU_PAD_DOWN) == 1);
    CHECK(menu_options_step(&s, 0, MENU_PAD_DOWN) == 1);
    CHECK(menu_options_step(&s, MENU_PAD_DOWN, 0) == 2);
    /* a different button newly pressed while DOWN stays held: DOWN is not new */
    CHECK(menu_options_step(&s, MENU_PAD_DOWN | MENU_PAD_UP, MENU_PAD_DOWN) == 1);

    /* opposite directions cancel */
    menu_options_init(&s, 0x02, 0, 5);
    CHECK(tap(&s, MENU_PAD_UP | MENU_PAD_DOWN) == 0);
    CHECK(tap(&s, MENU_PAD_LEFT | MENU_PAD_RIGHT) == 0 && s.audio_reg == 0x02);

    /* row 0: Mono <-> Stereo via LEFT, RIGHT, A, C, START; B does nothing */
    menu_options_init(&s, 0x02, 0, 5);
    tap(&s, MENU_PAD_RIGHT); CHECK(s.audio_reg == 0x01);
    tap(&s, MENU_PAD_LEFT);  CHECK(s.audio_reg == 0x02);
    tap(&s, MENU_PAD_A);     CHECK(s.audio_reg == 0x01);
    tap(&s, MENU_PAD_C);     CHECK(s.audio_reg == 0x02);
    tap(&s, MENU_PAD_START); CHECK(s.audio_reg == 0x01);
    tap(&s, MENU_PAD_B);     CHECK(s.audio_reg == 0x01 && !s.done);
    tap(&s, MENU_PAD_RIGHT | MENU_PAD_A); CHECK(s.audio_reg == 0x02); /* one toggle */

    /* row 1: preset cycles with wrap in both directions */
    menu_options_init(&s, 0x02, 0, 5);
    tap(&s, MENU_PAD_DOWN); CHECK(s.cursor == MENU_ROW_BUTTON_CONFIG);
    tap(&s, MENU_PAD_LEFT); CHECK(s.button_preset == MENU_BUTTON_PRESETS - 1);
    tap(&s, MENU_PAD_RIGHT); CHECK(s.button_preset == 0);
    for (unsigned i = 0; i < MENU_BUTTON_PRESETS; i++) tap(&s, MENU_PAD_A);
    CHECK(s.button_preset == 0);
    tap(&s, MENU_PAD_A); CHECK(s.button_preset == 1);
    CHECK(s.audio_reg == 0x02); /* other rows untouched */

    /* row 2: volume clamps at both ends */
    menu_options_init(&s, 0x02, 0, MENU_VOLUME_MAX - 1);
    tap(&s, MENU_PAD_DOWN); tap(&s, MENU_PAD_DOWN);
    CHECK(s.cursor == MENU_ROW_VOLUME);
    tap(&s, MENU_PAD_RIGHT); CHECK(s.volume == MENU_VOLUME_MAX);
    tap(&s, MENU_PAD_RIGHT); CHECK(s.volume == MENU_VOLUME_MAX);
    tap(&s, MENU_PAD_A);     CHECK(s.volume == MENU_VOLUME_MAX); /* activate: no-op */
    for (unsigned i = 0; i < 40; i++) tap(&s, MENU_PAD_LEFT);
    CHECK(s.volume == 0);
    tap(&s, MENU_PAD_RIGHT); CHECK(s.volume == 1);

    /* row 3: only activation sets done; LEFT/RIGHT do nothing */
    menu_options_init(&s, 0x02, 0, 5);
    tap(&s, MENU_PAD_UP); CHECK(s.cursor == MENU_ROW_BACK);
    tap(&s, MENU_PAD_LEFT); tap(&s, MENU_PAD_RIGHT); tap(&s, MENU_PAD_B);
    CHECK(!s.done);
    tap(&s, MENU_PAD_START); CHECK(s.done);

    /* value change acts on the row the frame started on, then the cursor moves */
    menu_options_init(&s, 0x02, 0, 5);
    CHECK(tap(&s, MENU_PAD_DOWN | MENU_PAD_RIGHT) == MENU_ROW_BUTTON_CONFIG);
    CHECK(s.audio_reg == 0x01 && s.button_preset == 0);

    /* corrupt state is repaired, NULL is rejected */
    s.cursor = 200; s.audio_reg = 0x00; s.button_preset = 77; s.volume = 250; s.done = false;
    CHECK(menu_options_step(&s, 0, 0) == 0);
    CHECK(s.audio_reg == 0x02 && s.button_preset == 0 && s.volume == MENU_VOLUME_MAX);
    CHECK(menu_options_step(NULL, MENU_PAD_UP, 0) == MENU_ROW_COUNT);

    /* determinism: same inputs, same outputs */
    {
        MenuState a, b;
        uint16_t prev = 0;
        uint32_t seed = 12345;
        menu_options_init(&a, 0x01, 2, 7);
        menu_options_init(&b, 0x01, 2, 7);
        for (unsigned i = 0; i < 2000; i++) {
            uint16_t cur;
            seed = seed * 1664525u + 1013904223u;
            cur = (uint16_t)((seed >> 16) & 0x00FFu);
            CHECK(menu_options_step(&a, cur, prev) == menu_options_step(&b, cur, prev));
            CHECK(a.cursor < MENU_ROW_COUNT && a.volume <= MENU_VOLUME_MAX &&
                  a.button_preset < MENU_BUTTON_PRESETS);
            CHECK(menu_audio_normalize(a.audio_reg) == a.audio_reg);
            CHECK(a.cursor == b.cursor && a.audio_reg == b.audio_reg &&
                  a.button_preset == b.button_preset && a.volume == b.volume &&
                  a.done == b.done);
            prev = cur;
        }
    }
    return 0;
}
