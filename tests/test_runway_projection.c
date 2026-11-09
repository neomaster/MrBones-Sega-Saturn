#include "core/runway_projection.h"
#include <stdio.h>
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
int main(void) {
    int32_t x, y;
    CHECK(runway_project_center(0,0,0,0,&x,&y) == 1 && x == 0 && y == 0);
    CHECK(runway_project_center(65536,0,0,65536,&x,&y) == 1 && x == 28 && y == -28);
    CHECK(runway_project_center(515000,0,0,0,&x,&y) == 1 && x == 220);
    CHECK(runway_project_center(518000,0,0,0,&x,&y) == 0 && x == 220 + 1);
    CHECK(runway_project_center(0,471000,0,0,&x,&y) == 0 && y == 201);
    CHECK(runway_project_center(0,0,1,1,&x,&y) == 1 && x == -1 && y == -1);
    CHECK(runway_project_center(0,0,0,0,0,&y) == -1);
    return 0;
}

