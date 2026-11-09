#include "core/snd1.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
static void be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
int main(void) {
    uint8_t file[112] = {0};
    snd1_span spans[SND1_SPAN_COUNT];
    for (int i = 0; i < SND1_SPAN_COUNT; i++) be32(file + i * 8, 96);
    be32(file + 4, 4);
    for (int i = 1; i < SND1_SPAN_COUNT; i++) be32(file + i * 8, 100);
    be32(file + 12, 12);
    for (int i = 2; i < SND1_SPAN_COUNT; i++) be32(file + i * 8, 112);
    CHECK(snd1_parse(file, sizeof file, spans) == 0);
    CHECK(spans[0].data == file + 96 && spans[0].size == 4);
    CHECK(spans[1].data == file + 100 && spans[1].size == 12);
    CHECK(spans[11].data == file + 112 && spans[11].size == 0);
    CHECK(snd1_parse(file, 95, spans) != 0);
    be32(file + 12, 13);
    CHECK(snd1_parse(file, sizeof file, spans) != 0);
    be32(file + 12, 12); be32(file + 16, 108);
    CHECK(snd1_parse(file, sizeof file, spans) != 0);
    be32(file + 16, 111);
    CHECK(snd1_parse(file, sizeof file, spans) != 0);
    return 0;
}

