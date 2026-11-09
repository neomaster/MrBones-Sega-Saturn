#include "platform/host_input.h"
#include "core/saturn_pad.h"
#include "core/runway_input.h"
#include <stdio.h>
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

typedef struct { uint32_t id[4], value[4]; unsigned n; } log_t;
static void emit(void *p, uint32_t id, uint32_t value) {
    log_t *l = p;
    if (l->n < 4) { l->id[l->n] = id; l->value[l->n] = value; l->n++; }
}

/* Without a live keyboard, host_input_keyboard_buttons returns whatever the OS
 * reports; the contract under test is that the returned bits are only MB_PAD_*
 * and that feeding them through saturn_pad + runway_input_step stays coherent. */
int main(void) {
    CHECK(host_input_key_button(0x0d) == MB_PAD_START);
    CHECK(host_input_key_button(0x25) == MB_PAD_LEFT);
    CHECK(host_input_key_button('D') == MB_PAD_RIGHT);
    CHECK(host_input_key_button('X') == MB_PAD_B);
    CHECK(host_input_key_button(0xff) == 0);
    uint32_t buttons = host_input_keyboard_buttons();
    CHECK((buttons & ~((1u << 13) - 1u)) == 0);

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
