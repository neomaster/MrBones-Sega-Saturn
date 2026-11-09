#include "core/stxtr.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void) {
    uint8_t data[48] = {0, 32, 0, 13, 0, 0, 0, 1, 0, 5,
                        0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
    size_t offset = 0;
    stxtr_record record;
    CHECK(stxtr_next(data, sizeof data, &offset, &record) == 1);
    CHECK(record.width == 32 && record.height == 13 && record.header_word == 1);
    CHECK(record.data == data + 16 && record.size == 20 && offset == 48);
    CHECK(stxtr_next(data, sizeof data, &offset, &record) == 0);
    offset = 0;
    CHECK(stxtr_next(data, 47, &offset, &record) == -1 && offset == 0);
    data[10] = 0;
    CHECK(stxtr_next(data, sizeof data, &offset, &record) == -1 && offset == 0);
    data[10] = 0x12; data[0] = 0; data[1] = 0;
    CHECK(stxtr_next(data, sizeof data, &offset, &record) == -1);
    return 0;
}
