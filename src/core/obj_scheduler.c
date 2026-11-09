#include "obj_scheduler.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

static mb_object *alive_next(mb_scheduler *s, size_t from) {
    if (!s || !s->count) return NULL;
    for (size_t n = 0; n < s->count; n++) {
        size_t i = (from + 1 + n) % s->count;
        mb_object *o = s->ring[i];
        if (o && o->alive) {
            s->current = i;
            return o;
        }
    }
    return NULL;
}

#ifdef _WIN32
static void WINAPI fiber_entry(void *param) {
    mb_object *self = (mb_object *)param;
    if (self && self->brain) self->brain(self);
    self->alive = 0;
    /* Return control to whatever is current; prefer main if stopping. */
    mb_object *next = alive_next(self->sched, self->sched->current);
    if (next && next->fiber)
        SwitchToFiber(next->fiber);
    else if (self->sched->main_fiber)
        SwitchToFiber(self->sched->main_fiber);
}
#endif

int mb_scheduler_init(mb_scheduler *s, size_t capacity) {
    if (!s || capacity == 0) return -1;
    memset(s, 0, sizeof *s);
    s->ring = (mb_object **)calloc(capacity, sizeof(mb_object *));
    if (!s->ring) return -1;
    s->capacity = capacity;
#ifdef _WIN32
    s->main_fiber = ConvertThreadToFiber(NULL);
    if (!s->main_fiber) {
        free(s->ring);
        s->ring = NULL;
        return -1;
    }
#endif
    return 0;
}

void mb_scheduler_fini(mb_scheduler *s) {
    if (!s) return;
    for (size_t i = 0; i < s->count; i++) {
        mb_object *o = s->ring[i];
        if (!o) continue;
#ifdef _WIN32
        if (o->fiber) {
            DeleteFiber(o->fiber);
            o->fiber = NULL;
        }
#endif
        o->alive = 0;
    }
    free(s->ring);
#ifdef _WIN32
    if (s->main_fiber) {
        ConvertFiberToThread();
        s->main_fiber = NULL;
    }
#endif
    memset(s, 0, sizeof *s);
}

int mb_scheduler_add(mb_scheduler *s, mb_object *object) {
    if (!s || !object || s->count >= s->capacity || !object->brain) return -1;
    object->sched = s;
    object->alive = 1;
#ifdef _WIN32
    if (object->fiber) {
        DeleteFiber(object->fiber);
        object->fiber = NULL;
    }
    object->fiber = CreateFiber(0, fiber_entry, object);
    if (!object->fiber) {
        object->alive = 0;
        return -1;
    }
#endif
    s->ring[s->count++] = object;
    return 0;
}

void mb_scheduler_request_stop(mb_scheduler *s) {
    if (s) s->stop = 1;
}

void mb_obj_yield(mb_object *self) {
    if (!self || !self->sched) return;
    mb_scheduler *s = self->sched;
    s->yield_count++;
    if (s->stop) {
#ifdef _WIN32
        if (s->main_fiber) SwitchToFiber(s->main_fiber);
#endif
        return;
    }
    mb_object *next = alive_next(s, s->current);
#ifdef _WIN32
    /* Switching to the currently running fiber is invalid on Windows. With a
     * single runnable object, return cooperatively (Delay busy-frames). */
    if (next && next != self && next->fiber) SwitchToFiber(next->fiber);
    else if ((!next || next == self) && s->main_fiber && s->stop)
        SwitchToFiber(s->main_fiber);
#else
    (void)next;
#endif
}

int mb_obj_new_brain(mb_object *self, mb_brain_fn brain) {
    if (!self || !brain || !self->sched) return -1;
    self->brain = brain;
#ifdef _WIN32
    if (self->fiber) {
        DeleteFiber(self->fiber);
        self->fiber = NULL;
    }
    self->fiber = CreateFiber(0, fiber_entry, self);
    if (!self->fiber) return -1;
#endif
    self->alive = 1;
    return 0;
}

void mb_obj_delay(mb_object *self, unsigned frames) {
    unsigned n = frames ? frames : 1;
    while (n--) mb_obj_yield(self);
}

int mb_scheduler_run(mb_scheduler *s) {
    if (!s || !s->count) return -1;
    s->stop = 0;
    s->current = 0;
    mb_object *first = s->ring[0];
    for (size_t i = 0; i < s->count; i++) {
        if (s->ring[i] && s->ring[i]->alive) { first = s->ring[i]; s->current = i; break; }
    }
    if (!first || !first->alive) return -1;
#ifdef _WIN32
    if (!first->fiber) return -1;
    SwitchToFiber(first->fiber);
#else
    /* Non-Windows: cooperative polling without stack switch. */
    while (!s->stop) {
        int any = 0;
        for (size_t i = 0; i < s->count; i++) {
            mb_object *o = s->ring[i];
            if (!o || !o->alive || !o->brain) continue;
            any = 1;
            s->current = i;
            o->brain(o);
        }
        if (!any) break;
    }
#endif
    return 0;
}
