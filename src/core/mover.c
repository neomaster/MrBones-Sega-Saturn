#include "mover.h"

void mb_mover_step(mb_mover *mover) {
    for (unsigned axis = 0; axis < 3; ++axis) mover->position[axis] += mover->velocity[axis];
    for (unsigned axis = 0; axis < 3; ++axis) mover->velocity[axis] += mover->acceleration[axis];
}
