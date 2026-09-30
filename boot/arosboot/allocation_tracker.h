// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper

#ifndef AROSBOOT_ALLOCATION_TRACKER_H
#define AROSBOOT_ALLOCATION_TRACKER_H

#include "uefi.h"
#include <aros/apple/bootinfo.h>

void arosboot_allocation_tracker_reset(void);

EFI_STATUS arosboot_allocate_owned(
    struct EFI_BOOT_SERVICES *boot, size_t byte_count, size_t alignment,
    uint32_t type, uint32_t flags, uint32_t module_index, void **result);

void arosboot_free_owned(
    struct EFI_BOOT_SERVICES *boot, void *used_address);

EFI_STATUS arosboot_publish_allocations(
    struct EFI_BOOT_SERVICES *boot, struct AROSBootInfo *boot_info);

#endif /* AROSBOOT_ALLOCATION_TRACKER_H */
