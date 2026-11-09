#include "core/menu_options_view.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do {if (!(c)) {printf("FAIL %d: %s\n",__LINE__,#c);return 1;}} while (0)

/* Pieces of the volume bar; their lengths are checked in test_pieces. */
#define BARS15 "|||||||||||||||"
#define BARS14 "||||||||||||||"
#define BARS8 "||||||||"
#define SP15 "               "
#define SP7 "       "
#define SP1 " "

static const char *const TYPE_SEL[6] = {
    "> CONTROLLER: TYPE A", "> CONTROLLER: TYPE B", "> CONTROLLER: TYPE C",
    "> CONTROLLER: TYPE D", "> CONTROLLER: TYPE E", "> CONTROLLER: TYPE F"
};
static const char *const TYPE_OFF[6] = {
    "  CONTROLLER: TYPE A", "  CONTROLLER: TYPE B", "  CONTROLLER: TYPE C",
    "  CONTROLLER: TYPE D", "  CONTROLLER: TYPE E", "  CONTROLLER: TYPE F"
};

static int expect_row(const MenuState *s, int r, bool sel, const char *want, int line) {
    char b[MENU_VIEW_ROW_BUF];
    menu_options_format_row(s, (uint8_t)r, b, sizeof b, sel);
    if (strcmp(b, want) == 0) return 1;
    printf("FAIL %d: row %d sel=%d got \"%s\" want \"%s\"\n", line, r, sel, b, want);
    return 0;
}
#define ROW(s, r, sel, want) \
    do { if (!expect_row((s), (r), (sel), (want), __LINE__)) return 1; } while (0)

static uint8_t tap(MenuState *s, uint16_t btn) { return menu_options_step(s, btn, 0); }

static int test_pieces(void) {
    CHECK(strlen(BARS15) == 15 && strlen(BARS14) == 14 && strlen(BARS8) == 8);
    CHECK(strlen(SP15) == 15 && strlen(SP7) == 7 && strlen(SP1) == 1);
    CHECK(MENU_VIEW_VOLUME_CELLS == 15 && MENU_VOLUME_MAX == 15);
    return 0;
}

/* ---- exact text of the four rows ---------------------------------------- */
static int test_rows_text(void) {
    MenuState s;

    menu_options_init(&s, MENU_AUDIO_BIT_STEREO, 0, 8);
    ROW(&s, 0, true, "> AUDIO: STEREO");
    ROW(&s, 0, false, "  AUDIO: STEREO");
    s.audio_reg = MENU_AUDIO_BIT_MONO;
    ROW(&s, 0, true, "> AUDIO: MONO");
    ROW(&s, 0, false, "  AUDIO: MONO");

    for (unsigned p = 0; p < MENU_BUTTON_PRESETS; p++) {
        s.button_preset = (uint8_t)p;
        ROW(&s, 1, true, TYPE_SEL[p]);
        ROW(&s, 1, false, TYPE_OFF[p]);
    }

    s.volume = 8;
    ROW(&s, 2, true, "> VOLUME: [" BARS8 SP7 "] 8/15");
    ROW(&s, 2, false, "  VOLUME: [" BARS8 SP7 "] 8/15");
    s.volume = 0;
    ROW(&s, 2, true, "> VOLUME: [" SP15 "] 0/15");
    s.volume = 1;
    ROW(&s, 2, false, "  VOLUME: [|" "              " "] 1/15");
    s.volume = 14;
    ROW(&s, 2, true, "> VOLUME: [" BARS14 SP1 "] 14/15");
    s.volume = 15;
    ROW(&s, 2, false, "  VOLUME: [" BARS15 "] 15/15");

    ROW(&s, 3, true, "> BACK TO MAIN MENU");
    ROW(&s, 3, false, "  BACK TO MAIN MENU");

    /* structure of the bar for every volume, independent of the literals above */
    for (unsigned v = 0; v <= MENU_VOLUME_MAX; v++) {
        char b[MENU_VIEW_ROW_BUF], suffix[8];
        s.volume = (uint8_t)v;
        menu_options_format_row(&s, MENU_ROW_VOLUME, b, sizeof b, false);
        snprintf(suffix, sizeof suffix, "%u/15", v);
        CHECK(strncmp(b, "  VOLUME: [", 11) == 0);
        for (unsigned c = 0; c < 15; c++) CHECK(b[11 + c] == (c < v ? '|' : ' '));
        CHECK(b[26] == ']' && b[27] == ' ');
        CHECK(strcmp(b + 28, suffix) == 0);
        CHECK(strlen(b) == 28 + strlen(suffix));
    }
    return 0;
}

/* ---- a corrupt state is shown the way the state machine would repair it -- */
static int test_corrupt_display(void) {
    MenuState s;

    menu_options_init(&s, MENU_AUDIO_BIT_STEREO, 0, 8);
    s.volume = 250;
    ROW(&s, 2, false, "  VOLUME: [" BARS15 "] 15/15");
    s.button_preset = 77;
    ROW(&s, 1, false, "  CONTROLLER: TYPE A");
    s.audio_reg = 0x00; ROW(&s, 0, false, "  AUDIO: STEREO");
    s.audio_reg = 0x03; ROW(&s, 0, false, "  AUDIO: STEREO");
    s.audio_reg = 0x01; ROW(&s, 0, false, "  AUDIO: MONO");
    s.audio_reg = 0xA1; ROW(&s, 0, false, "  AUDIO: MONO");   /* upper bits ignored */
    s.audio_reg = 0xA2; ROW(&s, 0, false, "  AUDIO: STEREO");
    return 0;
}

/* ---- buffer handling ----------------------------------------------------- */
static int test_buffers(void) {
    MenuState s;
    char buf[64];
    const char *full = "> AUDIO: STEREO"; /* 15 characters */

    menu_options_init(&s, MENU_AUDIO_BIT_STEREO, 0, 8);

    for (size_t n = 1; n <= 20; n++) { /* truncated, terminated, never overflowed */
        size_t want = n - 1 < 15 ? n - 1 : 15;
        memset(buf, 'Z', sizeof buf);
        menu_options_format_row(&s, 0, buf, n, true);
        CHECK(strncmp(buf, full, want) == 0 && buf[want] == '\0');
        for (size_t i = n; i < sizeof buf; i++) CHECK(buf[i] == 'Z');
    }

    memset(buf, 'X', sizeof buf); /* out_sz == 0 writes nothing */
    menu_options_format_row(&s, 0, buf, 0, true);
    CHECK(buf[0] == 'X');
    menu_options_format_row(&s, 0, NULL, 10, true);
    menu_options_format_row(&s, 0, NULL, 0, true);

    memset(buf, 'X', sizeof buf); /* invalid row and NULL state give an empty string */
    menu_options_format_row(&s, MENU_ROW_COUNT, buf, sizeof buf, true);
    CHECK(buf[0] == '\0');
    memset(buf, 'X', sizeof buf);
    menu_options_format_row(&s, 255, buf, sizeof buf, false);
    CHECK(buf[0] == '\0');
    memset(buf, 'X', sizeof buf);
    menu_options_format_row(NULL, 0, buf, sizeof buf, true);
    CHECK(buf[0] == '\0');

    /* MENU_VIEW_ROW_BUF fits every row; the longest is 33 characters */
    {
        size_t longest = 0;
        for (unsigned v = 0; v <= MENU_VOLUME_MAX; v++) {
            s.volume = (uint8_t)v;
            for (uint8_t r = 0; r < MENU_ROW_COUNT; r++) {
                char b[MENU_VIEW_ROW_BUF];
                menu_options_format_row(&s, r, b, sizeof b, true);
                CHECK(strlen(b) < MENU_VIEW_ROW_BUF);
                if (strlen(b) > longest) longest = strlen(b);
            }
        }
        CHECK(longest == 33);
    }
    return 0;
}

/* ---- the screen follows the state machine -------------------------------- */
static int test_screen_flow(void) {
    MenuState s;

    menu_options_init(&s, MENU_AUDIO_BIT_STEREO, 0, 5);
    for (uint8_t cur = 0; cur < MENU_ROW_COUNT; cur++) { /* exactly one row is marked */
        s.cursor = cur;
        for (uint8_t r = 0; r < MENU_ROW_COUNT; r++) {
            char b[MENU_VIEW_ROW_BUF];
            menu_options_format_row(&s, r, b, sizeof b, r == s.cursor);
            CHECK(b[0] == (r == cur ? '>' : ' ') && b[1] == ' ');
        }
    }

    menu_options_init(&s, MENU_AUDIO_BIT_STEREO, 0, 5);
    tap(&s, MENU_PAD_RIGHT);
    ROW(&s, 0, true, "> AUDIO: MONO");
    tap(&s, MENU_PAD_A);
    ROW(&s, 0, true, "> AUDIO: STEREO");

    tap(&s, MENU_PAD_DOWN);
    tap(&s, MENU_PAD_RIGHT); tap(&s, MENU_PAD_RIGHT);
    ROW(&s, 1, true, "> CONTROLLER: TYPE C");
    ROW(&s, 0, false, "  AUDIO: STEREO");
    for (int i = 0; i < 3; i++) tap(&s, MENU_PAD_LEFT);
    ROW(&s, 1, true, "> CONTROLLER: TYPE F");

    tap(&s, MENU_PAD_DOWN);
    for (int i = 0; i < 3; i++) tap(&s, MENU_PAD_RIGHT);
    ROW(&s, 2, true, "> VOLUME: [" BARS8 SP7 "] 8/15");
    for (int i = 0; i < 8; i++) tap(&s, MENU_PAD_LEFT);
    ROW(&s, 2, true, "> VOLUME: [" SP15 "] 0/15");

    tap(&s, MENU_PAD_DOWN);
    ROW(&s, 3, true, "> BACK TO MAIN MENU");
    CHECK(!s.done);
    tap(&s, MENU_PAD_START);
    CHECK(s.done);
    return 0;
}

/* ---- screen -> NVM ------------------------------------------------------- */
static int test_sync_to_nvm(void) {
    MenuState s, before;
    uint8_t nvm_a, nvm_v;

    menu_options_init(&s, MENU_AUDIO_BIT_STEREO, 3, 8);
    nvm_a = 0xA5; nvm_v = 0;                         /* NVM holds mono, volume 0 */
    menu_options_sync_nvm(&s, &nvm_a, &nvm_v);
    CHECK(nvm_a == 0xA6 && nvm_v == 8);              /* stereo, upper bits kept */

    s.audio_reg = MENU_AUDIO_BIT_MONO;
    menu_options_sync_nvm(&s, &nvm_a, &nvm_v);
    CHECK(nvm_a == 0xA5 && nvm_v == 8);

    nvm_a = 0xA0; s.audio_reg = MENU_AUDIO_BIT_STEREO;   /* invalid bits in NVM */
    menu_options_sync_nvm(&s, &nvm_a, &nvm_v);
    CHECK(nvm_a == 0xA2);
    nvm_a = 0xA3; s.audio_reg = MENU_AUDIO_BIT_MONO;
    menu_options_sync_nvm(&s, &nvm_a, &nvm_v);
    CHECK(nvm_a == 0xA1);

    for (unsigned hi = 0; hi < 64; hi++) {           /* bits 2..7 are never touched */
        for (unsigned lo = 0; lo < 4; lo++) {
            for (unsigned mode = 1; mode <= 2; mode++) {
                s.audio_reg = (uint8_t)mode;
                nvm_a = (uint8_t)((hi << 2) | lo);
                menu_options_sync_nvm(&s, &nvm_a, NULL);
                CHECK((nvm_a & 0xFCu) == (hi << 2) && (nvm_a & 3u) == mode);
            }
        }
    }

    menu_options_init(&s, MENU_AUDIO_BIT_STEREO, 0, 0);
    nvm_v = 99;
    menu_options_sync_nvm(&s, NULL, &nvm_v);
    CHECK(nvm_v == 0);
    s.volume = MENU_VOLUME_MAX;
    menu_options_sync_nvm(&s, NULL, &nvm_v);
    CHECK(nvm_v == 15);
    s.volume = 250;                                  /* corrupt volume is clamped */
    menu_options_sync_nvm(&s, NULL, &nvm_v);
    CHECK(nvm_v == 15);
    s.audio_reg = 0x00; nvm_a = 0x40;                /* corrupt audio -> stereo */
    menu_options_sync_nvm(&s, &nvm_a, NULL);
    CHECK(nvm_a == 0x42);
    s.audio_reg = 0x03; nvm_a = 0x40;
    menu_options_sync_nvm(&s, &nvm_a, NULL);
    CHECK(nvm_a == 0x42);

    nvm_a = 0x77; nvm_v = 0x66;                      /* NULL state writes nothing */
    menu_options_sync_nvm(NULL, &nvm_a, &nvm_v);
    CHECK(nvm_a == 0x77 && nvm_v == 0x66);
    menu_options_sync_nvm(&s, NULL, NULL);

    menu_options_init(&s, MENU_AUDIO_BIT_MONO, 4, 9); /* the state is not modified */
    before = s;
    nvm_a = 0; nvm_v = 0;
    menu_options_sync_nvm(&s, &nvm_a, &nvm_v);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    return 0;
}

/* ---- NVM -> screen ------------------------------------------------------- */
static int test_load_from_nvm(void) {
    MenuState s;

    menu_options_init(&s, MENU_AUDIO_BIT_STEREO, 0, 0);
    menu_options_load_nvm(&s, 0x01, 3);
    CHECK(s.audio_reg == 0x01 && s.volume == 3);
    menu_options_load_nvm(&s, 0x02, 15);
    CHECK(s.audio_reg == 0x02 && s.volume == 15);
    menu_options_load_nvm(&s, 0x00, 0);              /* invalid audio -> stereo */
    CHECK(s.audio_reg == 0x02 && s.volume == 0);
    menu_options_load_nvm(&s, 0x03, 7);
    CHECK(s.audio_reg == 0x02 && s.volume == 7);
    menu_options_load_nvm(&s, 0xA5, 200);            /* other bits kept, volume clamped */
    CHECK(s.audio_reg == 0xA5 && s.volume == 15);
    menu_options_load_nvm(&s, 0xA3, 1);
    CHECK(s.audio_reg == 0xA2 && s.volume == 1);

    s.cursor = MENU_ROW_VOLUME; s.button_preset = 4; s.done = true;
    menu_options_load_nvm(&s, 0x01, 9);              /* only audio and volume change */
    CHECK(s.cursor == MENU_ROW_VOLUME && s.button_preset == 4 && s.done);
    CHECK(s.audio_reg == 0x01 && s.volume == 9);

    menu_options_load_nvm(NULL, 0x01, 9);
    return 0;
}

/* ---- both directions together ------------------------------------------- */
static int test_roundtrip(void) {
    for (unsigned a = 0; a < 256; a++) {
        for (unsigned v = 0; v <= 20; v++) {
            MenuState s, t;
            uint8_t na = (uint8_t)a, nv = 0xFF;
            unsigned want_v = v < MENU_VOLUME_MAX ? v : (unsigned)MENU_VOLUME_MAX;

            menu_options_init(&s, MENU_AUDIO_BIT_STEREO, 2, 0);
            menu_options_load_nvm(&s, (uint8_t)a, (uint8_t)v);
            menu_options_sync_nvm(&s, &na, &nv);
            CHECK((na & 0xFCu) == (a & 0xFCu));
            CHECK((na & 3u) == (menu_audio_normalize((uint8_t)a) & 3u));
            CHECK(nv == want_v);
            if ((a & 3u) == 1u || (a & 3u) == 2u) /* valid input: lossless */
                CHECK(na == a);
            if (v <= MENU_VOLUME_MAX)
                CHECK(nv == v);

            menu_options_init(&t, MENU_AUDIO_BIT_MONO, 0, 0); /* load back: same state */
            menu_options_load_nvm(&t, na, nv);
            CHECK(t.audio_reg == s.audio_reg && t.volume == s.volume);
            menu_options_sync_nvm(&t, &na, &nv);              /* and it is stable */
            CHECK(menu_audio_normalize(na) == na && nv == want_v);
        }
    }
    return 0;
}

/* ---- the menu as the player uses it: open, change, close, reopen -------- */
static int test_persist_scenario(void) {
    MenuState s, again;
    uint8_t nvm_a = 0x42, nvm_v = 15; /* stereo, full volume, another setting in bit 6 */

    menu_options_init(&s, 0, 0, 0);
    menu_options_load_nvm(&s, nvm_a, nvm_v);
    ROW(&s, 0, true, "> AUDIO: STEREO");
    ROW(&s, 2, false, "  VOLUME: [" BARS15 "] 15/15");

    tap(&s, MENU_PAD_LEFT);                          /* stereo -> mono */
    tap(&s, MENU_PAD_DOWN); tap(&s, MENU_PAD_DOWN);
    for (int i = 0; i < 7; i++) tap(&s, MENU_PAD_LEFT); /* volume 15 -> 8 */
    ROW(&s, 0, false, "  AUDIO: MONO");
    ROW(&s, 2, true, "> VOLUME: [" BARS8 SP7 "] 8/15");

    nvm_a |= 0x08;                                   /* another module changes bit 3 meanwhile */
    tap(&s, MENU_PAD_DOWN);
    tap(&s, MENU_PAD_START);
    CHECK(s.done);
    menu_options_sync_nvm(&s, &nvm_a, &nvm_v);
    CHECK(nvm_a == 0x49 && nvm_v == 8);              /* 0x40 | 0x08 kept, mono bit set */

    menu_options_init(&again, 0, 0, 0);              /* reopen */
    menu_options_load_nvm(&again, nvm_a, nvm_v);
    ROW(&again, 0, true, "> AUDIO: MONO");
    ROW(&again, 2, false, "  VOLUME: [" BARS8 SP7 "] 8/15");
    return 0;
}

int main(void) {
    if (test_pieces()) return 1;
    if (test_rows_text()) return 1;
    if (test_corrupt_display()) return 1;
    if (test_buffers()) return 1;
    if (test_screen_flow()) return 1;
    if (test_sync_to_nvm()) return 1;
    if (test_load_from_nvm()) return 1;
    if (test_roundtrip()) return 1;
    if (test_persist_scenario()) return 1;
    return 0;
}
