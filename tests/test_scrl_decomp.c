#include "core/scrl_decomp.h"
#include "core/scrl_map.h"
#include "lz_test_enc.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

static unsigned g_seed = 20240607u;
static unsigned rnd(void) {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

/* The one vector every reader of the format agrees on: "BONE", then 3 bytes from 4 back. */
static int test_vector(void) {
    const uint8_t stream[] = {0x0C, 'B', 'O', 'N', 'E', 0x00, 0x42, 0x00, 0x00};
    uint8_t out[16] = {0};
    scrl_decomp_info info;
    CHECK(scrl_decomp_byte_ex(stream, sizeof stream, out, sizeof out, &info) == 7u);
    CHECK(memcmp(out, "BONEBON", 7) == 0);
    CHECK(info.terminated == 1u && info.src_used == sizeof stream && info.bad_matches == 0u);
    CHECK(scrl_decomp_byte(stream, sizeof stream, out, sizeof out) == 7u); /* the old name */
    return 0;
}

/* Every nibble 1..15 copies nibble + 1 bytes; a short distance repeats the pattern. */
static int test_nibbles(void) {
    unsigned n;
    for (n = 1; n <= 15; n++) {
        uint8_t buf[64], out[64];
        lz_enc e;
        scrl_decomp_info info;
        size_t got, i;
        lz_enc_init(&e, buf, sizeof buf);
        lz_enc_literal(&e, 'A');
        lz_enc_literal(&e, 'B');
        lz_enc_match(&e, 2, n + 1u); /* overlaps itself: ABABAB... */
        lz_enc_end(&e);
        got = scrl_decomp_byte_ex(buf, e.len, out, sizeof out, &info);
        CHECK(got == 2u + n + 1u);
        for (i = 0; i < got; i++) CHECK(out[i] == (i % 2u ? 'B' : 'A'));
        CHECK(info.terminated == 1u && info.bad_matches == 0u && info.src_used == e.len);
    }
    {
        uint8_t buf[16], out[32];
        lz_enc e;
        lz_enc_init(&e, buf, sizeof buf);
        lz_enc_literal(&e, 7);
        lz_enc_match(&e, 1, 16); /* a run */
        lz_enc_end(&e);
        CHECK(scrl_decomp_byte_ex(buf, e.len, out, sizeof out, NULL) == 17u);
        CHECK(out[0] == 7 && out[16] == 7);
    }
    return 0;
}

/* The control byte is read MSB first and a new one starts every eight items. */
static int test_control_bytes(void) {
    uint8_t buf[64], out[64];
    lz_enc e;
    unsigned i;
    scrl_decomp_info info;
    lz_enc_init(&e, buf, sizeof buf);
    for (i = 0; i < 20; i++) lz_enc_literal(&e, (uint8_t)(100u + i)); /* 8 + 8 + 4 items */
    lz_enc_end(&e);
    CHECK(e.len == 3u + 20u + 2u); /* three control bytes, 20 literals, the terminator */
    CHECK(buf[0] == 0x00 && buf[9] == 0x00); /* all literals: no bit set */
    CHECK(scrl_decomp_byte_ex(buf, e.len, out, sizeof out, &info) == 20u);
    for (i = 0; i < 20; i++) CHECK(out[i] == 100u + i);
    CHECK(info.terminated == 1u);
    return 0;
}

/* Nothing after the terminator is read; a stream that fills dst first is not "terminated". */
static int test_stops(void) {
    uint8_t buf[64], out[64];
    lz_enc e;
    scrl_decomp_info info;
    lz_enc_init(&e, buf, sizeof buf);
    lz_enc_literal(&e, 1);
    lz_enc_literal(&e, 2);
    lz_enc_end(&e);
    buf[e.len] = 0xFF; /* garbage after the terminator */
    buf[e.len + 1] = 0xFF;
    CHECK(scrl_decomp_byte_ex(buf, e.len + 2u, out, sizeof out, &info) == 2u);
    CHECK(info.terminated == 1u && info.src_used == e.len);

    lz_enc_init(&e, buf, sizeof buf); /* four bytes in, ask for exactly four */
    lz_enc_literal(&e, 9);
    lz_enc_match(&e, 1, 3);
    lz_enc_end(&e);
    memset(out, 0xEE, sizeof out);
    CHECK(scrl_decomp_byte_ex(buf, e.len, out, 4u, &info) == 4u);
    CHECK(info.terminated == 0u); /* the terminator was never reached */
    CHECK(out[3] == 9 && out[4] == 0xEE); /* and nothing written past dst_cap */

    memset(out, 0xEE, sizeof out); /* a match that would run past dst_cap is cut */
    CHECK(scrl_decomp_byte_ex(buf, e.len, out, 3u, &info) == 3u);
    CHECK(out[2] == 9 && out[3] == 0xEE);
    return 0;
}

/* Cutting the source anywhere gives a prefix of the full output, never more, never garbage. */
static int test_truncated_source(void) {
    uint8_t in[300], buf[700], full[300], out[300];
    size_t n = sizeof in, len, cut, full_n, got;
    unsigned i;
    for (i = 0; i < n; i++) in[i] = (uint8_t)(i % 7u == 0 ? rnd() : (i / 5u));
    len = lz_encode(in, n, buf, sizeof buf);
    CHECK(len > 0);
    full_n = scrl_decomp_byte_ex(buf, len, full, sizeof full, NULL);
    CHECK(full_n == n && memcmp(full, in, n) == 0);
    for (cut = 0; cut < len; cut++) {
        scrl_decomp_info info;
        got = scrl_decomp_byte_ex(buf, cut, out, sizeof out, &info);
        CHECK(got <= full_n && memcmp(out, full, got) == 0);
        CHECK(info.src_used <= cut && info.terminated == 0u);
    }
    return 0;
}

/* Matches the format forbids are counted, and decoded the way the old function did. */
static int test_bad_matches(void) {
    uint8_t buf[32], out[32];
    lz_enc e;
    scrl_decomp_info info;

    lz_enc_init(&e, buf, sizeof buf); /* distance 0 counts as 1 */
    lz_enc_literal(&e, 5);
    lz_enc_code(&e, (0u << 4) | 3u);
    lz_enc_end(&e);
    CHECK(scrl_decomp_byte_ex(buf, e.len, out, sizeof out, &info) == 5u);
    CHECK(out[1] == 5 && out[4] == 5 && info.bad_matches == 1u && info.terminated == 1u);

    lz_enc_init(&e, buf, sizeof buf); /* reaches before the start: those bytes read as 0 */
    lz_enc_literal(&e, 9);
    lz_enc_code(&e, (3u << 4) | 4u); /* 5 bytes from 3 back, with only 1 written */
    lz_enc_end(&e);
    CHECK(scrl_decomp_byte_ex(buf, e.len, out, sizeof out, &info) == 6u);
    CHECK(info.bad_matches == 1u);
    CHECK(out[0] == 9 && out[1] == 0 && out[2] == 0 && out[3] == 9 && out[4] == 0);

    lz_enc_init(&e, buf, sizeof buf); /* exactly the start of the output is allowed */
    lz_enc_literal(&e, 1);
    lz_enc_literal(&e, 2);
    lz_enc_match(&e, 2, 2);
    lz_enc_end(&e);
    CHECK(scrl_decomp_byte_ex(buf, e.len, out, sizeof out, &info) == 4u);
    CHECK(info.bad_matches == 0u && out[2] == 1 && out[3] == 2);
    return 0;
}

static int test_degenerate(void) {
    uint8_t out[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint8_t one[] = {0x00, 'x', 0x00};
    scrl_decomp_info info;
    info.src_used = 99;
    info.terminated = 9;
    info.bad_matches = 9;
    CHECK(scrl_decomp_byte_ex(NULL, 4, out, sizeof out, &info) == 0u);
    CHECK(info.src_used == 0u && info.terminated == 0u && info.bad_matches == 0u);
    CHECK(scrl_decomp_byte_ex(one, sizeof one, NULL, 8, &info) == 0u);
    CHECK(scrl_decomp_byte_ex(one, 0, out, sizeof out, &info) == 0u);
    CHECK(scrl_decomp_byte_ex(one, sizeof one, out, 0, &info) == 0u);
    CHECK(scrl_decomp_byte(NULL, 0, NULL, 0) == 0u);
    CHECK(out[0] == 1 && out[7] == 8); /* untouched */
    return 0;
}

/* Encode random data of several shapes, decode it, and get the same bytes back. */
static int test_round_trip(void) {
    static uint8_t in[3000], buf[3000 * 2 + 64], out[3000];
    unsigned trial;
    for (trial = 0; trial < 150; trial++) {
        const size_t n = 1u + rnd() % 3000u;
        const unsigned alphabet = trial % 5u == 0 ? 256u : (trial % 5u == 1 ? 2u : 4u);
        const int runs = trial % 3 == 0;
        size_t i, len, got;
        scrl_decomp_info info;
        for (i = 0; i < n; i++) {
            if (runs && i > 0 && rnd() % 8u != 0)
                in[i] = in[i - 1];
            else
                in[i] = (uint8_t)(rnd() % alphabet);
        }
        len = lz_encode(in, n, buf, sizeof buf);
        CHECK(len > 0u);
        got = scrl_decomp_byte_ex(buf, len, out, sizeof out, &info);
        CHECK(got == n && memcmp(out, in, n) == 0);
        CHECK(info.terminated == 1u && info.src_used == len && info.bad_matches == 0u);
        CHECK(scrl_decomp_byte(buf, len, out, sizeof out) == n);
    }
    return 0;
}

/* 4095 bytes back is the farthest the 12-bit field reaches; it is valid exactly when that
 * many bytes have been written. */
static int test_window(void) {
    static uint8_t buf[4095 + 600], out[4095 + 64]; /* room: dst must not fill before the end */
    static uint8_t lit[4095];
    unsigned i;
    scrl_decomp_info info;
    lz_enc e;
    for (i = 0; i < sizeof lit; i++) lit[i] = (uint8_t)(i * 31u + (i >> 5));

    lz_enc_init(&e, buf, sizeof buf); /* 4095 literals, then 16 bytes from 4095 back */
    for (i = 0; i < sizeof lit; i++) lz_enc_literal(&e, lit[i]);
    lz_enc_match(&e, 4095, 16);
    lz_enc_end(&e);
    CHECK(!e.overflow);
    CHECK(scrl_decomp_byte_ex(buf, e.len, out, sizeof out, &info) == 4095u + 16u);
    CHECK(memcmp(out, lit, sizeof lit) == 0);
    CHECK(memcmp(out + 4095, lit, 16) == 0); /* the first 16 bytes again */
    CHECK(info.bad_matches == 0u && info.terminated == 1u);

    lz_enc_init(&e, buf, sizeof buf); /* one byte short of that: it reaches before the start */
    for (i = 0; i < sizeof lit - 1u; i++) lz_enc_literal(&e, lit[i]);
    lz_enc_match(&e, 4095, 2);
    lz_enc_end(&e);
    CHECK(!e.overflow);
    CHECK(scrl_decomp_byte_ex(buf, e.len, out, sizeof out, &info) == 4094u + 2u);
    CHECK(info.bad_matches == 1u);
    return 0;
}

int main(void) {
    if (test_vector() || test_nibbles() || test_control_bytes() || test_stops() ||
        test_truncated_source() || test_bad_matches() || test_degenerate() ||
        test_round_trip() || test_window())
        return 1;
    printf("OK scrl_decomp\n");
    return 0;
}
