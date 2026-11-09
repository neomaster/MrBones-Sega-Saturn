#include "core/mover.h"
#include <stdio.h>
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
int main(void) {
    mb_mover m = {{10, 0xffffffffu, 0}, {5, 2, 0xffffffffu}, {3, 0xffffffffu, 2}};
    mb_mover_step(&m);
    CHECK(m.position[0] == 15 && m.position[1] == 1 && m.position[2] == 0xffffffffu);
    CHECK(m.velocity[0] == 8 && m.velocity[1] == 1 && m.velocity[2] == 1);
    CHECK(m.acceleration[0] == 3 && m.acceleration[1] == 0xffffffffu && m.acceleration[2] == 2);
    mb_mover_step(&m);
    CHECK(m.position[0] == 23 && m.position[1] == 2 && m.position[2] == 0);
    CHECK(m.velocity[0] == 11 && m.velocity[1] == 0 && m.velocity[2] == 3);
    return 0;
}

