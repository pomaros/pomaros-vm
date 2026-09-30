// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper

#ifndef AROS_APPLE_BOOT_GENERATION_H
#define AROS_APPLE_BOOT_GENERATION_H

#include <stddef.h>
#include <stdint.h>

#define AROS_BOOT_GENERATION_MAGIC UINT32_C(0x474f5241)
#define AROS_BOOT_GENERATION_VERSION UINT16_C(1)
#define AROS_BOOT_GENERATION_BYTES UINT16_C(128)
#define AROS_BOOT_GENERATION_LOADER_ABI UINT32_C(1)
#define AROS_BOOT_GENERATION_ACCEPTED UINT32_C(1)
#define AROS_BOOT_GENERATION_CANDIDATE UINT32_C(2)
#define AROS_BOOT_GENERATION_DIGEST_OFFSET 72U
#define AROS_BOOT_GENERATION_DIGEST_BYTES 32U

struct AROSBootGenerationRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint64_t sequence;
    uint32_t slot;
    uint32_t flags;
    uint64_t bundle_size;
    uint32_t loader_abi_min;
    uint32_t loader_abi_max;
    uint8_t bundle_sha256[32];
    uint8_t record_sha256[32];
    uint64_t reserved[3];
};

_Static_assert(sizeof(struct AROSBootGenerationRecord) ==
               AROS_BOOT_GENERATION_BYTES, "boot generation record size");
_Static_assert(offsetof(struct AROSBootGenerationRecord, record_sha256) ==
               AROS_BOOT_GENERATION_DIGEST_OFFSET,
               "boot generation digest offset");

#endif /* AROS_APPLE_BOOT_GENERATION_H */
