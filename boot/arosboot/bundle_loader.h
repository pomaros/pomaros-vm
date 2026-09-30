// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper

#ifndef AROSBOOT_BUNDLE_LOADER_H
#define AROSBOOT_BUNDLE_LOADER_H

#include "uefi.h"
#include <aros/apple/bootinfo.h>

struct AROSBootBundleResult {
    uintptr_t kernel_entry;
    uint32_t module_count;
    uint64_t generation_sequence;
    uint32_t generation_slot;
    uint32_t generation_fallback;
    uint32_t generation_degraded;
    uint32_t generation_memory;
};

EFI_STATUS arosboot_load_bundle(
    EFI_HANDLE image, struct EFI_SYSTEM_TABLE *system,
    struct AROSBootInfo *boot_info, struct AROSBootBundleResult *result);

#endif /* AROSBOOT_BUNDLE_LOADER_H */
