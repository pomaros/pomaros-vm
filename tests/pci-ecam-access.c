// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ecam/access.h"
int main(void)
{
    void *mem;
    assert(!posix_memalign(&mem, 1u << 20, 2u << 20));
    memset(mem, 0xa5, 2u << 20);
    struct AROSPCIECAM h = {mem, 2u << 20, 64, 65};
    assert(AROSPCIECAMAddress(&h, 64, 0, 0, 0, 4) == mem);
    assert(AROSPCIECAMAddress(&h, 65, 31, 7, 4092, 4) == (char *)mem+(2u<<20)-4);
    assert(!AROSPCIECAMAddress(&h, 63, 0, 0, 0, 4));
    assert(!AROSPCIECAMAddress(&h, 66, 0, 0, 0, 4));
    assert(!AROSPCIECAMAddress(&h, 64, 32, 0, 0, 4));
    assert(!AROSPCIECAMAddress(&h, 64, 0, 8, 0, 4));
    assert(!AROSPCIECAMAddress(&h, 64, 0, 0, 4096, 4));
    assert(!AROSPCIECAMAddress(&h, 64, 0, 0, 1, 2));
    assert(!AROSPCIECAMAddress(&h, 64, 0, 0, 2, 4));
    assert(!AROSPCIECAMAddress(&h, 64, 0, 0, 0, 0));
    assert(!AROSPCIECAMAddress(&h, 64, 0, 0, 0, 3));
    assert(!AROSPCIECAMAddress(&h, 64, 0, 0, 0, 8));
    assert(AROSPCIECAMRead(&h, 63, 0, 0, 0, 4) == UINT32_MAX);
    assert(!AROSPCIECAMWrite(&h, 63, 0, 0, 4, 2, 0));
    assert(AROSPCIECAMWrite(&h, 64, 0, 0, 4, 2, 0x1234));
    assert(((uint8_t *)mem)[4] == 0x34 && ((uint8_t *)mem)[5] == 0x12);
    /* Adjacent PCI status is untouched by a command-word store. */
    assert(AROSPCIECAMRead(&h, 64, 0, 0, 6, 2) == 0xa5a5);
    assert(AROSPCIECAMWrite(&h, 64, 0, 0, 5, 1, 0x67));
    assert(AROSPCIECAMRead(&h, 64, 0, 0, 4, 4) == 0xa5a56734);
    assert(AROSPCIECAMWrite(&h, 65, 31, 7, 4092, 4, 0x12345678));
    assert(AROSPCIECAMRead(&h, 65, 31, 7, 4092, 4) == 0x12345678);
    h.bytes--;
    assert(!AROSPCIECAMAddress(&h, 65, 31, 7, 4092, 4));
    h.mapping = (void *)(UINTPTR_MAX & ~((uintptr_t)(1u<<20)-1));
    h.bytes = 2u << 20;
    assert(!AROSPCIECAMAddress(&h, 65, 0, 0, 0, 4));
    h.mapping = (uint8_t *)mem+1;
    assert(!AROSPCIECAMAddress(&h, 64, 0, 0, 0, 1));
    h.mapping = mem; h.last_bus = 256;
    assert(!AROSPCIECAMAddress(&h, 64, 0, 0, 0, 1));
    h.last_bus = 63;
    assert(!AROSPCIECAMAddress(&h, 64, 0, 0, 0, 1));
    free(mem);
    puts("pci-ecam-access result=PASS bounds,widths,nonzero-bus,overflow,status-preserved");
}
