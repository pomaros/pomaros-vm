// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
// Original implementation of SHA-256 from NIST FIPS 180-4.

#ifndef AROSBOOT_SHA256_H
#define AROSBOOT_SHA256_H

#include <stddef.h>
#include <stdint.h>

struct AROSBootSHA256 {
    uint32_t state[8];
    uint64_t byte_count;
    uint8_t block[64];
    size_t block_size;
};

void AROSBootSHA256Init(struct AROSBootSHA256 *context);
void AROSBootSHA256Update(
    struct AROSBootSHA256 *context, const void *data, size_t size);
void AROSBootSHA256Final(
    struct AROSBootSHA256 *context, uint8_t digest[32]);

static inline void AROSBootSHA256(
    const void *data, size_t size, uint8_t digest[32])
{
    struct AROSBootSHA256 context;
    AROSBootSHA256Init(&context);
    AROSBootSHA256Update(&context, data, size);
    AROSBootSHA256Final(&context, digest);
}

#endif /* AROSBOOT_SHA256_H */
