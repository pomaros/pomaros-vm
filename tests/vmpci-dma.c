// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define __NOLIBBASE__
#include <proto/exec.h>
#include <proto/kernel.h>
#include <aros/kernel.h>
#include "vmpci.h"
static unsigned live, fragmented;
static void *page_memory;
static void *pages(uintptr_t bytes)
{ assert(!posix_memalign(&page_memory,4096,bytes));memset(page_memory,0,bytes);++live;return page_memory; }
static void free_pages(void *p) { assert(live);--live;free(p); }
static void *physical(void *p)
{ uintptr_t n=(uintptr_t)p;return (void *)(n+0x100000+(fragmented && n>(uintptr_t)page_memory ? 4096:0)); }
#undef AllocMem
#define AllocMem(n,f) ((void)(f),calloc(1,n))
#undef FreeMem
#define FreeMem(p,n) ((void)(n),free(p))
#undef KrnAllocPages
#define KrnAllocPages(a,n,f) ((void)KernelBase,(void)(a),(void)(f),pages(n))
#undef KrnFreePages
#define KrnFreePages(p,n) ((void)KernelBase,(void)(n),free_pages(p))
#undef KrnVirtualToPhysical
#define KrnVirtualToPhysical(p) physical(p)
#undef KrnSetProtection
#define KrnSetProtection(p,n,f) ((void)(p),(void)(n),(void)(f))
#undef AddTail
#define AddTail(l,n) ADDTAIL(l,n)
#undef Remove
#define Remove(n) REMOVE(n)
#undef ObtainSemaphore
#define ObtainSemaphore(s) ((void)(s))
#undef ObtainSemaphoreShared
#define ObtainSemaphoreShared(s) ((void)(s))
#undef ReleaseSemaphore
#define ReleaseSemaphore(s) ((void)(s))
#undef InitSemaphore
#define InitSemaphore(s) ((void)(s))
#include "vmpci_dma.c"
int main(void)
{
    struct VMPciBase base={0};VMPciDMAInit(&base);
    struct PCIECAMDMA *d=&base.host.dma;
    assert(!d->allocate(d->context,0));assert(!d->allocate(d->context,UINT32_MAX));
    uint8_t *cpu=d->allocate(d->context,4097);assert(cpu && live==1);
    uintptr_t bus=(uintptr_t)d->to_bus(d->context,cpu);assert(bus==(uintptr_t)cpu+0x100000);
    assert(d->to_bus(d->context,cpu+4096)==(void *)(bus+4096));
    assert(d->to_cpu(d->context,(void *)(bus+4096))==cpu+4096);
    assert(d->to_bus(d->context,cpu+4097)==(void *)-1);
    assert(d->to_bus(d->context,(void *)((uintptr_t)cpu-1))==(void *)-1);
    assert(d->to_cpu(d->context,(void *)(bus+4097))==(void *)-1);
    d->release(d->context,cpu+1);assert(live==1); /* exact base ownership */
    d->release(d->context,cpu);assert(!live);
    assert(d->to_bus(d->context,cpu)==(void *)-1);
    d->release(d->context,cpu);assert(!live);
    fragmented=1;assert(!d->allocate(d->context,8192));assert(!live);
    puts("vmpci-dma result=PASS actual-policy,nonidentity,bounds,free,noncontiguous-rejection");
}
