/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "page_allocator.h"
#include <exec/memory.h>
int exclusion_depth;
int main(void)
{
    const size_t page = AROS_APPLE_PAGE_SIZE, size = 16 * page;
    unsigned char *memory = aligned_alloc(page, size + page);
    struct AROSApplePageAllocatorStats stats;
    assert(memory);
    memset(memory, 0xa5, size + page);
    assert(AROSApplePageAllocatorInitialize((uintptr_t)memory, size));
    /* The bitmap is bigger than this arena: spare zero bits must not admit
       an allocation extending beyond the actual backing memory. */
    assert(!AROSApplePageAllocate(memory + size - page, 2 * page, MEMF_CLEAR));
    assert(exclusion_depth == 0);
    AROSApplePageAllocatorGetStats(&stats);
    assert(stats.pages_in_use == 0 && stats.allocation_count == 0);
    assert(AROSApplePageAllocate(memory + size - page, page, MEMF_CLEAR) == memory + size - page);
    for (size_t i = size - page; i < size; i++) assert(memory[i] == 0);
    for (size_t i = size; i < size + page; i++) assert(memory[i] == 0xa5);
    assert(!AROSApplePageAllocate(memory + size - page, page, 0));
    assert(!AROSApplePageAllocate(memory + size, page, 0));
    assert(AROSApplePageAllocate(NULL, size - page, 0) == memory);
    assert(!AROSApplePageAllocate(NULL, page, 0));
    assert(AROSApplePageFreeRaw(memory, size - page));
    assert(AROSApplePageFreeRaw(memory + size - page, page));
    assert(!AROSApplePageFreeRaw(memory + size - page, 2 * page));
    AROSApplePageAllocatorGetStats(&stats);
    assert(stats.pages_in_use == 0 && stats.allocation_count == stats.free_count);
    assert(exclusion_depth == 0);
    free(memory);
    /* Initialization is metadata-only; no access to the synthetic address. */
    assert(AROSApplePageAllocatorInitialize(0x10000000, AROS_APPLE_PAGE_ARENA_MAX_BYTES));
    assert(!AROSApplePageAllocatorInitialize(0x10000000, AROS_APPLE_PAGE_ARENA_MAX_BYTES + page));
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
    _Static_assert(AROS_APPLE_PAGE_ARENA_MAX_BYTES == 64 * 1024 * 1024, "VM capacity");
#else
    _Static_assert(AROS_APPLE_PAGE_ARENA_MAX_BYTES == 8 * 1024 * 1024, "native capacity");
#endif
    return 0;
}
