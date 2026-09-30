// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper

#include "allocation_tracker.h"

#define AROSBOOT_MAX_TRACKED_ALLOCATIONS 2080U
#define AROSBOOT_TEMPORARY_ALLOCATION UINT32_C(0)

static struct AROSBootAllocation tracked[AROSBOOT_MAX_TRACKED_ALLOCATIONS];
static uint32_t tracked_count;
static struct AROSBootAllocation *published;

static int power_of_two(size_t value)
{
    return value != 0 && (value & (value - 1U)) == 0;
}

void arosboot_allocation_tracker_reset(void)
{
    tracked_count = 0;
    published = NULL;
}

EFI_STATUS arosboot_allocate_owned(
    struct EFI_BOOT_SERVICES *boot, size_t byte_count, size_t alignment,
    uint32_t type, uint32_t flags, uint32_t module_index, void **result)
{
    EFI_PHYSICAL_ADDRESS address = 0;
    uintptr_t used;
    size_t allocation_size;
    EFI_UINTN pages;
    EFI_STATUS status;
    struct AROSBootAllocation *record;

    if (boot == NULL || result == NULL || byte_count == 0 ||
        !power_of_two(alignment) || tracked_count >= AROSBOOT_MAX_TRACKED_ALLOCATIONS ||
        byte_count > SIZE_MAX - (alignment - 1U))
        return EFI_OUT_OF_RESOURCES;
    allocation_size = byte_count + alignment - 1U;
    if (allocation_size > SIZE_MAX - (EFI_PAGE_SIZE - 1U))
        return EFI_OUT_OF_RESOURCES;
    pages = (allocation_size + EFI_PAGE_SIZE - 1U) / EFI_PAGE_SIZE;
    status = boot->allocate_pages(
        EFI_ALLOCATE_ANY_PAGES, EFI_LOADER_DATA, pages, &address);
    if (EFI_ERROR(status))
        return status;
    if (address > UINTPTR_MAX ||
        (uintptr_t)address > UINTPTR_MAX - (alignment - 1U)) {
        boot->free_pages(address, pages);
        return EFI_OUT_OF_RESOURCES;
    }
    used = ((uintptr_t)address + alignment - 1U) & ~(alignment - 1U);
    boot->set_mem((void *)(uintptr_t)address, pages * EFI_PAGE_SIZE, 0);

    record = &tracked[tracked_count++];
    record->physical_address = address;
    record->size = (uint64_t)pages * EFI_PAGE_SIZE;
    record->used_physical = used;
    record->used_size = byte_count;
    record->type = type;
    record->flags = flags;
    record->module_index = module_index;
    record->reserved = 0;
    *result = (void *)used;
    return EFI_SUCCESS;
}

void arosboot_free_owned(
    struct EFI_BOOT_SERVICES *boot, void *used_address)
{
    uint32_t index;

    if (boot == NULL || used_address == NULL)
        return;
    for (index = 0; index < tracked_count; ++index) {
        if (tracked[index].used_physical != (uintptr_t)used_address)
            continue;
        boot->free_pages(tracked[index].physical_address,
            (EFI_UINTN)(tracked[index].size / EFI_PAGE_SIZE));
        --tracked_count;
        tracked[index] = tracked[tracked_count];
        return;
    }
}

EFI_STATUS arosboot_publish_allocations(
    struct EFI_BOOT_SERVICES *boot, struct AROSBootInfo *boot_info)
{
    EFI_STATUS status;

    if (published == NULL) {
        status = arosboot_allocate_owned(boot,
            sizeof(tracked), 16U, AROS_BOOT_ALLOCATION_TABLE,
            AROS_BOOT_ALLOCATION_FLAG_OWNED |
                AROS_BOOT_ALLOCATION_FLAG_RELEASE_AFTER_STAGE2,
            AROS_BOOT_ALLOCATION_NO_MODULE, (void **)&published);
        if (EFI_ERROR(status))
            return status;
    }
    boot->copy_mem(published, tracked,
        (EFI_UINTN)tracked_count * sizeof(*published));
    boot_info->allocations_physical = (uint64_t)(uintptr_t)published;
    boot_info->allocation_count = tracked_count;
    boot_info->allocation_entry_size = sizeof(*published);
    boot_info->flags |= AROS_BOOTINFO_FLAG_ALLOCATIONS;
    return EFI_SUCCESS;
}
