/* SHA-256 (FIPS 180-4), for identifying disc images by their hash. */
#ifndef RAC_SHA256_H
#define RAC_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t length;            /* bytes hashed so far */
    uint8_t block[64];
    size_t block_used;
} Sha256;

void sha256_start(Sha256 *hash);
void sha256_add(Sha256 *hash, const void *data, size_t size);
void sha256_finish(Sha256 *hash, uint8_t digest[32]);

/* DIGEST as 64 lowercase hex digits and a terminating zero. */
void sha256_hex(const uint8_t digest[32], char hex[65]);

#endif
