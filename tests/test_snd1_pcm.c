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

    /* Voice layers: 8-byte records, SA (BE32) at +2..5, load base 0xB000. Unsorted
     * starts; a sample runs to the next higher start, the last one to the blob end. */
    {
        static uint8_t blob[0xB000u + 32u];
        uint8_t layers[32] = {0};
        snd1_span lspan = { layers, 24 }; /* three full records; byte 24.. ignored */
        snd1_voice_table v;
        layers[2] = 0x00; layers[3] = 0x00; layers[4] = 0xB0; layers[5] = 0x04;  /* +4 */
        layers[10] = 0x00; layers[11] = 0x00; layers[12] = 0xB0; layers[13] = 0x10; /* +16 */
        layers[18] = 0x00; layers[19] = 0x00; layers[20] = 0xB0; layers[21] = 0x0C; /* +12 */
        CHECK(snd1_voice_open(&v, &lspan, 8, blob, sizeof blob) == 0);
        CHECK(v.count == 3);
        CHECK(v.start[0] == 4 && v.start[1] == 16 && v.start[2] == 12);
        CHECK(snd1_voice_sample(&v, 0, &pcm, &len) == 0);
        CHECK(pcm == (const int8_t *)blob + 4 && len == 8);   /* 4 .. 12 */
        CHECK(snd1_voice_sample(&v, 2, &pcm, &len) == 0);
        CHECK(pcm == (const int8_t *)blob + 12 && len == 4);  /* 12 .. 16 */
        CHECK(snd1_voice_sample(&v, 1, &pcm, &len) == 0);
        CHECK(pcm == (const int8_t *)blob + 16 && len == sizeof blob - 16u); /* 16 .. blob end */
        CHECK(snd1_voice_sample(&v, 3, &pcm, &len) != 0);

        /* An SA below the base, or past the blob, ends the table at that record. */
        layers[12] = 0x00; layers[13] = 0x10; /* record 1 SA = 0x0010 < 0xB000 */
        CHECK(snd1_voice_open(&v, &lspan, 8, blob, sizeof blob) == 0);
        CHECK(v.count == 1);
        layers[11] = 0x01; layers[12] = 0x60; layers[13] = 0x20; /* SA = 0xB000 + sizeof blob */
        CHECK(snd1_voice_open(&v, &lspan, 8, blob, sizeof blob) == 0);
        CHECK(v.count == 1);
        layers[11] = 0x00; layers[12] = 0xB0; layers[13] = 0x10;

        /* rec_size 16 reads records 0 and 1 only (span holds 24 bytes). */
        CHECK(snd1_voice_open(&v, &lspan, 16, blob, sizeof blob) == 0);
        CHECK(v.count == 1 && v.start[0] == 4);
        CHECK(snd1_voice_open(&v, &lspan, 5, blob, sizeof blob) != 0);
        CHECK(snd1_voice_open(&v, NULL, 8, blob, sizeof blob) != 0);
        CHECK(snd1_voice_open(&v, &lspan, 8, NULL, 0) != 0);
    }

    /* SCSP Tone Bank (.ton) layer lookup: ptr[0]=10 (5 entries: 4 tables + 1 voice),
     * voice 0 at 10 (4-byte header + two 32-byte layers). */
    {
        uint8_t ton[96];
        snd1_span tspan = { ton, sizeof ton };
        const uint8_t *raw = NULL;
        size_t rbytes = 0, scount = 0;
        int is8 = 0;
        memset(ton, 0, sizeof ton);
        ton[0] = 0x00; ton[1] = 0x0A; /* ptr[0] = 10 -> 1 voice */
        ton[8] = 0x00; ton[9] = 0x0A; /* voice 0 @ 10 */
        ton[12] = 1;                  /* 2 layers */
        /* Layer 0: note 0x51..0x52, 16-bit BE (pcm8b=0), SA=78, LEA=2 (3 samples = 6 bytes). */
        ton[14] = 0x51; ton[15] = 0x52;
        ton[17] = 0x20; ton[19] = 78;
        ton[22] = 0x00; ton[23] = 2;
        ton[24] = (uint8_t)((15u << 3u) | (9u >> 2u));
        ton[25] = (uint8_t)(((9u & 3u) << 6u) | (1u << 5u) | 13u);
        ton[26] = (uint8_t)((3u << 2u) | (8u >> 3u));
        ton[27] = (uint8_t)(((8u & 7u) << 5u) | 12u);
        ton[30] = 48u;
        /* Layer 1: note 0x4D..0x4F, 8-bit (pcm8b=1), SA=84, LEA=3 (4 bytes). */
        ton[46] = 0x4D; ton[47] = 0x4F;
        ton[49] = 0x30; ton[51] = 84;
        ton[54] = 0x00; ton[55] = 3;
        ton[78] = 0x7f; ton[80] = 0x40; ton[82] = 0x10;
        ton[84] = 0x11; ton[85] = 0x22; ton[86] = 0x33; ton[87] = 0x44;

        CHECK(snd1_ton_note_sample(&tspan, 0, 0x4Eu, 1, &raw, &rbytes, &scount, &is8) == 0);
        CHECK(is8 == 1 && scount == 4u && rbytes == 4u && raw[0] == 0x11 && raw[3] == 0x44);
        CHECK(snd1_ton_note_sample(&tspan, 0, 0x52u, 1, &raw, &rbytes, &scount, &is8) == 0);
        CHECK(is8 == 0 && scount == 3u && rbytes == 6u && raw[0] == 0x7f && raw[2] == 0x40);
        CHECK(snd1_ton_note_sample(&tspan, 0, 0x60u, 1, &raw, &rbytes, &scount, &is8) != 0);
        CHECK(snd1_ton_note_sample(&tspan, 1, 0x4Eu, 1, &raw, &rbytes, &scount, &is8) != 0);
        CHECK(snd1_ton_is_bank(&tspan) == 1);
        {
            unsigned next = 0;
            snd1_tone decoded;
            CHECK(snd1_ton_layer(&tspan, 0, 0x52u, &next, &decoded) == 0);
            CHECK(decoded.ar == 13u && decoded.d1r == 9u && decoded.d2r == 15u &&
                  decoded.dl == 8u && decoded.rr == 12u && decoded.krs == 3u &&
                  decoded.eghold == 1u && decoded.tl == 48u);
        }
        /* Reject raw PCM offset table or truncated slice. */
        CHECK(snd1_ton_is_bank(&span) == 0);
    }

    /* SCSP .TON waveform picker: skip white noise & pick clean PCM waveform. */
    {
        static uint8_t bank[512];
        snd1_span bspan = { bank, sizeof bank };
        const uint8_t *raw = NULL;
        size_t rbytes = 0, scount = 0;
        int is8 = 0;
        memset(bank, 0, sizeof bank);
        bank[0] = 0x00; bank[1] = 0x0C; /* ptr[0] = 12 -> 2 voices */
        bank[8] = 0x00; bank[9] = 0x0C; /* voice 0 @ 12 (1 layer) */
        bank[10] = 0x00; bank[11] = 0x30; /* voice 1 @ 48 (1 layer) */
        /* Voice 0 layer 0 @ 16: pure high-delta +/-100 noise @ 128, 64 samples */
        bank[16] = 0; bank[17] = 127;
        bank[19] = 0x10; bank[21] = 128;
        bank[24] = 0; bank[25] = 63;
        for (size_t i = 0; i < 64u; i++)
            bank[128u + i] = (uint8_t)(int8_t)((i & 1u) ? 100 : -100);
        /* Voice 1 layer 0 @ 52: smooth triangle wave @ 256, 64 samples */
        bank[52] = 0; bank[53] = 127;
        bank[55] = 0x10; bank[56] = 0x01; bank[57] = 0x00;
        bank[60] = 0; bank[61] = 63;
        for (size_t i = 0; i < 64u; i++)
            bank[256u + i] = (uint8_t)(int8_t)(((int)(i % 16u) - 8) * 6);

        CHECK(snd1_ton_is_bank(&bspan) == 1);
        CHECK(snd1_ton_pick_sfx(&bspan, 0, 32u, 256u, &raw, &rbytes, &scount, &is8) == 0);
        CHECK(is8 == 1 && scount == 64u && rbytes == 64u && raw == bank + 256u);
    }
    return 0;
}
