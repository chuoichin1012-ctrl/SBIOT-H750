// SHA-256 (FIPS 180-4) for image checks in the bootloader and the app (spec §11.1).
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t h[8];
    uint64_t bytes;     // total length fed so far
    uint8_t block[64];  // partial block
    uint32_t fill;      // bytes in block
} sha256_t;

void sha256_init(sha256_t *s);
void sha256_update(sha256_t *s, const void *data, size_t len);
void sha256_final(sha256_t *s, uint8_t out[32]);

// HMAC-SHA256 (RFC 2104): out = H((K ^ opad) || H((K ^ ipad) || msg)); a key over 64 bytes is hashed first.
void hmac_sha256(const void *key, size_t klen, const void *msg, size_t mlen, uint8_t out[32]);
