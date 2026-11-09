#include "core/snd1_pcm.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void) {
    /* Synthetic span: BE16 table [6, 10, 14] then 8 PCM bytes. */
    uint8_t span_data[16] = {
        0x00, 0x06, 0x00, 0x0a, 0x00, 0x0e,
        0x10, 0x20, 0x30, 0x40, /* sample 0: 4 bytes */
        0x7f, (uint8_t)-128, 0x00, 0x01 /* sample 1: 4 bytes */
    };
    snd1_span span = { span_data, sizeof span_data };
    snd1_pcm_table table;
    const int8_t *pcm = NULL;
    size_t len = 0;

    CHECK(snd1_pcm_open(&table, &span) == 0);
    CHECK(table.count == 3);
    CHECK(snd1_pcm_sample(&table, 0, &pcm, &len) == 0);
    CHECK(len == 4 && pcm[0] == 0x10 && pcm[3] == 0x40);
    CHECK(snd1_pcm_sample(&table, 1, &pcm, &len) == 0);
    CHECK(len == 4 && pcm[0] == 0x7f && pcm[1] == (int8_t)-128);
    CHECK(snd1_pcm_sample(&table, 2, &pcm, &len) != 0);

    /* Non-increasing offset stops the table. */
    span_data[4] = 0x00; span_data[5] = 0x08; /* 8 < 10 */
    CHECK(snd1_pcm_open(&table, &span) == 0);
    CHECK(table.count == 2);

    CHECK(snd1_pcm_open(&table, NULL) != 0);
    return 0;
}
