/* SHA-256 (FIPS 180-4). Small, dependency-free, used for input identity checks. */
#ifndef MB_SHA256_H
#define MB_SHA256_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t h[8];
    uint64_t total;   /* bytes processed */
    uint8_t buf[64];
    size_t fill;
} sha256_ctx;

void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *data, size_t len);
void sha256_final(sha256_ctx *c, uint8_t out[32]);
void sha256_hex(const uint8_t digest[32], char out[65]);

#endif
