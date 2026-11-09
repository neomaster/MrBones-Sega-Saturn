#include "core/obj_scheduler.h"
#include "core/joypad_brain.h"
#include "core/saturn_pad.h"
#include "core/runway_input.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

typedef struct {
    unsigned ticks;
    unsigned stop_after;
} counter_user;

static void counter_brain(mb_object *self) {
    counter_user *u = (counter_user *)self->user;
    while (self->alive && !self->sched->stop) {
        u->ticks++;
        if (u->ticks >= u->stop_after)
            mb_scheduler_request_stop(self->sched);
        mb_obj_yield(self);
    }
}

static void delay_brain(mb_object *self) {
    counter_user *u = (counter_user *)self->user;
    mb_obj_delay(self, 3);
    u->ticks = 3;
    mb_scheduler_request_stop(self->sched);
}

static saturn_pad g_pad;
static int g_nulls;
static const saturn_pad *test_read(void *ctx, unsigned port) {
    (void)ctx; (void)port;
    if (g_nulls > 0) { g_nulls--; return NULL; }
    return &g_pad;
}

typedef struct { uint32_t id[4]; unsigned n; } log_t;
static void emit(void *p, uint32_t id, uint32_t value) {
    (void)value;
    log_t *l = p;
    if (l->n < 4) l->id[l->n++] = id;
}

int main(void) {
    mb_scheduler sched;
    mb_object a = {0}, b = {0};
    counter_user ua = {0, 5}, ub = {0, 100};

    CHECK(mb_scheduler_init(&sched, 4) == 0);
    a.brain = counter_brain; a.user = &ua;
    b.brain = counter_brain; b.user = &ub;
    CHECK(mb_scheduler_add(&sched, &a) == 0);
    CHECK(mb_scheduler_add(&sched, &b) == 0);
    CHECK(mb_scheduler_run(&sched) == 0);
    CHECK(ua.ticks == 5);
    CHECK(ub.ticks >= 4); /* interleaved yields */
    CHECK(sched.yield_count >= 5);
    mb_scheduler_fini(&sched);

    /* Delay adaptation */
    CHECK(mb_scheduler_init(&sched, 2) == 0);
    ua.ticks = 0; ua.stop_after = 0;
    a = (mb_object){0}; a.brain = delay_brain; a.user = &ua;
    CHECK(mb_scheduler_add(&sched, &a) == 0);
    CHECK(mb_scheduler_run(&sched) == 0);
    CHECK(ua.ticks == 3);
    CHECK(sched.yield_count >= 3);
    mb_scheduler_fini(&sched);

    /* Joypad brain step + fail streak bit */
    mb_joypad_brain joy;
    memset(&g_pad, 0, sizeof g_pad);
    saturn_pad_update_host(&g_pad, MB_PAD_LEFT | MB_PAD_C);
    mb_joypad_brain_init(&joy, test_read, NULL);
    CHECK(mb_joypad_brain_step(&joy) == 1);
    CHECK(joy.bones_input[0] == (uint8_t)(g_pad.held >> 8));
    CHECK(joy.bones_input[4] == (uint8_t)(g_pad.pressed >> 8));
    g_nulls = 30;
    for (int i = 0; i < 29; i++) CHECK(mb_joypad_brain_step(&joy) == 0);
    CHECK(!(joy.bones_input[4] & 0x08));
    CHECK(mb_joypad_brain_step(&joy) == 0);
    CHECK(joy.bones_input[4] & 0x08);

    /* BonesInput high bytes drive runway_input_step (release then press for edges). */
    uint32_t x = 0;
    log_t log = {0};
    saturn_pad_update_host(&g_pad, 0);
    mb_joypad_brain_step(&joy);
    saturn_pad_update_host(&g_pad, MB_PAD_LEFT | MB_PAD_C);
    mb_joypad_brain_step(&joy);
    CHECK(runway_input_step(&x, 0, joy.bones_input[0], joy.bones_input[4], emit, &log));
    CHECK(x == 3932 && log.n == 2);

    /* New brain replacement */
    CHECK(mb_scheduler_init(&sched, 2) == 0);
    ua.ticks = 0; ua.stop_after = 2;
    a = (mb_object){0}; a.brain = counter_brain; a.user = &ua;
    CHECK(mb_scheduler_add(&sched, &a) == 0);
    CHECK(mb_obj_new_brain(&a, counter_brain) == 0);
    CHECK(mb_scheduler_run(&sched) == 0);
    CHECK(ua.ticks == 2);
    mb_scheduler_fini(&sched);
    return 0;
}
