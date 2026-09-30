// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
// Original implementation of SHA-256 from NIST FIPS 180-4.

#include "sha256.h"

static const uint32_t round_constant[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static uint32_t rotate_right(uint32_t value, unsigned int count)
{
    return (value >> count) | (value << (32U - count));
}

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static void write_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void transform(struct AROSBootSHA256 *context, const uint8_t block[64])
{
    uint32_t words[64];
    uint32_t a, b, c, d, e, f, g, h;
    unsigned int index;

    for (index = 0; index < 16; ++index)
        words[index] = read_be32(block + index * 4U);
    for (; index < 64; ++index) {
        uint32_t x = words[index - 15];
        uint32_t y = words[index - 2];
        uint32_t sigma0 = rotate_right(x, 7) ^ rotate_right(x, 18) ^ (x >> 3);
        uint32_t sigma1 = rotate_right(y, 17) ^ rotate_right(y, 19) ^ (y >> 10);
        words[index] = words[index - 16] + sigma0 + words[index - 7] + sigma1;
    }
    a = context->state[0]; b = context->state[1];
    c = context->state[2]; d = context->state[3];
    e = context->state[4]; f = context->state[5];
    g = context->state[6]; h = context->state[7];
    for (index = 0; index < 64; ++index) {
        uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t temporary1 = h + sum1 + choose + round_constant[index] + words[index];
        uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temporary2 = sum0 + majority;
        h = g; g = f; f = e; e = d + temporary1;
        d = c; c = b; b = a; a = temporary1 + temporary2;
    }
    context->state[0] += a; context->state[1] += b;
    context->state[2] += c; context->state[3] += d;
    context->state[4] += e; context->state[5] += f;
    context->state[6] += g; context->state[7] += h;
}

void AROSBootSHA256Init(struct AROSBootSHA256 *context)
{
    static const uint32_t initial[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    unsigned int index;
    for (index = 0; index < 8; ++index)
        context->state[index] = initial[index];
    context->byte_count = 0;
    context->block_size = 0;
}

void AROSBootSHA256Update(
    struct AROSBootSHA256 *context, const void *data, size_t size)
{
    const uint8_t *bytes = data;
    size_t index;
    context->byte_count += size;
    for (index = 0; index < size; ++index) {
        context->block[context->block_size++] = bytes[index];
        if (context->block_size == sizeof(context->block)) {
            transform(context, context->block);
            context->block_size = 0;
        }
    }
}

void AROSBootSHA256Final(
    struct AROSBootSHA256 *context, uint8_t digest[32])
{
    uint64_t bit_count = context->byte_count * 8U;
    unsigned int index;
    context->block[context->block_size++] = 0x80;
    if (context->block_size > 56) {
        while (context->block_size < 64)
            context->block[context->block_size++] = 0;
        transform(context, context->block);
        context->block_size = 0;
    }
    while (context->block_size < 56)
        context->block[context->block_size++] = 0;
    for (index = 0; index < 8; ++index)
        context->block[56 + index] = (uint8_t)(bit_count >> (56U - index * 8U));
    transform(context, context->block);
    for (index = 0; index < 8; ++index)
        write_be32(digest + index * 4U, context->state[index]);
}
