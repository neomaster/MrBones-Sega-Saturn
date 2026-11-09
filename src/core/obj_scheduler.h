#ifndef MB_OBJ_SCHEDULER_H
#define MB_OBJ_SCHEDULER_H
#include <stddef.h>
#include <stdint.h>

/*
 * Native cooperative scheduler adapted from loader routines verified in
 * /02 (SHA-256 02faf276…d33704), not from unverified map labels alone:
 *   ObjYeild     @ 0x002050b0 — prologue sts.l/pr + register saves; PC literal
 *                0x00205058 is the ring cursor; body saves SP, loads next SP,
 *                advances cursor by two words, restores and returns.
 *   ObjNewBrain  @ 0x00205168 — resets object SP from word at +0x28, installs a
 *                brain continuation (fiber start on Windows).
 *   CObject_Delay@ 0x002051e4 — yields `count` times (map name matched by
 *                address continuity after CreateCObject).
 *
 * This is stack-cooperative logic only. It does not emulate SH-2, SMPC, or
 * prove gameplay. Brain callbacks must call mb_obj_yield to switch.
 */

typedef struct mb_scheduler mb_scheduler;
typedef struct mb_object mb_object;
typedef void (*mb_brain_fn)(mb_object *self);

struct mb_object {
    mb_scheduler *sched;
    mb_brain_fn brain;
    void *user;
    void *fiber;       /* host fiber handle; NULL if idle */
    int alive;
};

struct mb_scheduler {
    mb_object **ring;
    size_t capacity;
    size_t count;
    size_t current;
    void *main_fiber;
    int stop;
    unsigned yield_count;
};

int mb_scheduler_init(mb_scheduler *s, size_t capacity);
void mb_scheduler_fini(mb_scheduler *s);
/* Register object in the ring. object->brain must be set first. */
int mb_scheduler_add(mb_scheduler *s, mb_object *object);
/* Run until mb_scheduler_request_stop or no alive objects. */
int mb_scheduler_run(mb_scheduler *s);
void mb_scheduler_request_stop(mb_scheduler *s);

/* Switch to the next alive object (ObjYeild adaptation). */
void mb_obj_yield(mb_object *self);
/* Replace the object's brain and (re)start its fiber (ObjNewBrain adaptation). */
int mb_obj_new_brain(mb_object *self, mb_brain_fn brain);
/* Yield `frames` times (CObject_Delay adaptation). frames==0 yields once. */
void mb_obj_delay(mb_object *self, unsigned frames);

#endif
