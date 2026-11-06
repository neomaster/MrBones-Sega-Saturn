#include "core/sha256.h"
#include <stdio.h>
#include <string.h>

static int check(const char *label, const char *data, size_t reps, const char *want) {
    sha256_ctx c; uint8_t d[32]; char hex[65];
    sha256_init(&c);
    for (size_t i = 0; i < reps; i++) sha256_update(&c, data, strlen(data));
    sha256_final(&c, d);
    sha256_hex(d, hex);
    if (strcmp(hex, want)) { printf("FAIL %s: %s\n", label, hex); return 1; }
    return 0;
}

int main(void) {
    int bad = 0;
    bad += check("empty", "", 1, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    bad += check("abc", "abc", 1, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    bad += check("two blocks", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 1,
                 "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    bad += check("million a", "a", 1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    /* chunked update must equal one-shot */
    sha256_ctx c; uint8_t d[32]; char hex[65];
    sha256_init(&c);
    const char *s = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    for (size_t i = 0; s[i]; i++) sha256_update(&c, s + i, 1);
    sha256_final(&c, d); sha256_hex(d, hex);
    if (strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1")) { printf("FAIL byte-wise\n"); bad++; }
    printf(bad ? "sha256: %d failure(s)\n" : "sha256: all vectors pass\n", bad);
    return bad != 0;
}
