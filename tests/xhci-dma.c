// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define __NOLIBBASE__
#define _AROS_TYPES_TIMEVAL_S_H_
#include <proto/exec.h>
#include <exec/memory.h>
#include <devices/usbhardware.h>
/* Exercise the real DMA implementation; mock only Exec/PCI services. */
#define UHWCMD_H
#define XHCICHIP_SCHEDULE_H
#define HCF_ADDR64 1
#define HCF_PLATFORM 2
struct PCIController { ULONG hc_Flags; void *hc_PCIDriverObject; void *hc_Device; };
static void *owned;
static ULONG owned_bytes;
static uintptr_t bus_base=0x10000000;
static unsigned metadata,allocations,frees,pre,post,translations;
static unsigned fail_meta,fail_pci,fail_translation,split_mapping,short_cache;
static ULONG last_post,pre_flags,post_flags;
static void *meta_alloc(size_t n) { if(fail_meta)return NULL; ++metadata;return calloc(1,n); }
static void meta_free(void *p) { assert(metadata);--metadata;free(p); }
static void *pci_alloc(ULONG bytes)
{
    assert(!owned);
    if(fail_pci)return NULL;
    owned=calloc(1,bytes);assert(owned);owned_bytes=bytes;++allocations;return owned;
}
static void pci_free(void *p) { assert(p==owned);free(p);owned=NULL;++frees; }
static void *translate(void *p)
{
    ++translations;
    if(translations==fail_translation)return (void *)-1;
    uintptr_t offset=(uintptr_t)p-(uintptr_t)owned;
    assert(owned && offset<owned_bytes);
    return (void *)(bus_base+offset+(split_mapping && offset>=4096?4096:0));
}
static void *cache_pre(void *p,ULONG *n,ULONG flags)
{
    pre_flags=flags;assert(p);++pre;
    if(short_cache)*n/=2;
    return p;
}
static void cache_post(void *p,ULONG *n,ULONG flags)
{ post_flags=flags;assert(p);++post;last_post=*n; }
#define ALLOCPCIMEM(h,o,n) ((void)(h),(void)(o),pci_alloc(n))
#define FREEPCIMEM(h,o,p) ((void)(h),(void)(o),pci_free(p))
#define CPUTOPCI(h,o,p) ((void)(h),(void)(o),translate(p))
#undef AllocMem
#define AllocMem(n,f) ((void)(f),meta_alloc(n))
#undef FreeMem
#define FreeMem(p,n) ((void)(n),meta_free(p))
#undef AllocVec
#define AllocVec(n,f) ((void)(f),calloc(1,n))
#undef FreeVec
#define FreeVec(p) free(p)
#undef CopyMem
#define CopyMem(s,d,n) memcpy(d,s,n)
#undef CachePreDMA
#define CachePreDMA(p,n,f) cache_pre(p,n,f)
#undef CachePostDMA
#define CachePostDMA(p,n,f) cache_post(p,n,f)
#include "xhci_dma.c"
static void reset(void)
{
    assert(!owned && !metadata && allocations==frees);
    fail_meta=fail_pci=fail_translation=split_mapping=short_cache=0;
    translations=pre=post=last_post=pre_flags=post_flags=0;bus_base=0x10000000;
}
int main(void)
{
    struct PCIController hc={.hc_Flags=HCF_ADDR64};
    unsigned char data[9000],saved[9000];
    for(unsigned i=0;i<sizeof(data);++i)data[i]=(unsigned char)(i*7);
    memcpy(saved,data,sizeof(data));
    struct IOUsbHWReq io={0};io.iouh_Data=data;io.iouh_Length=sizeof(data);
    ULONG n=sizeof(data);void *cookie=NULL;
    void *dma=xhciPreparePCIPayload(&hc,&io,&n,UHDIR_OUT,&cookie);
    assert(dma==(void *)bus_base && dma!=owned && cookie && pre==1 && !post);
    assert(translations>=3 && memcmp(owned,data,n)==0);
    xhciReleasePCIPayload(&hc,&io,n,UHDIR_OUT,cookie);
    assert(pre_flags==DMA_ReadFromRAM && post_flags==((1UL<<31)|DMA_ReadFromRAM));
    assert(post==1 && last_post==n);reset();

    n=sizeof(data);dma=xhciPreparePCIPayload(&hc,&io,&n,UHDIR_IN,&cookie);
    assert(dma==(void *)bus_base && cookie);
    memset(owned,0xa5,n);
    xhciReleasePCIPayload(&hc,&io,17,UHDIR_IN,cookie);
    for(unsigned i=0;i<17;++i)assert(data[i]==0xa5);
    assert(memcmp(data+17,saved+17,sizeof(data)-17)==0);
    assert(pre_flags==0 && post_flags==(1UL<<31));
    assert(post==1 && last_post==n);reset();

    /* Failed submission / acknowledged abort still balances cache ownership. */
    n=64;dma=xhciPreparePCIPayload(&hc,&io,&n,UHDIR_IN,&cookie);assert(dma);
    memcpy(saved,data,sizeof(data));memset(owned,0xcc,n);
    xhciReleasePCIPayload(&hc,&io,0,UHDIR_IN,cookie);
    assert(memcmp(saved,data,sizeof(data))==0 && post==1 && last_post==64);reset();

    /* Oversized device completion must not overrun either buffer. */
    n=64;dma=xhciPreparePCIPayload(&hc,&io,&n,UHDIR_IN,&cookie);assert(dma);
    xhciReleasePCIPayload(&hc,&io,65,UHDIR_IN,cookie);
    assert(io.iouh_Req.io_Error==UHIOERR_HOSTERROR);
    assert(memcmp(saved,data,sizeof(data))==0);reset();

    /* Changed request extent or direction cannot authorize a copy. */
    for(unsigned bad=0;bad<2;++bad) {
        io.iouh_Req.io_Error=0;n=64;
        assert(xhciPreparePCIPayload(&hc,&io,&n,UHDIR_IN,&cookie));
        memset(owned,0x33,n);
        if(!bad)io.iouh_Length=16;
        xhciReleasePCIPayload(&hc,&io,32,bad?UHDIR_OUT:UHDIR_IN,cookie);
        assert(io.iouh_Req.io_Error==UHIOERR_HOSTERROR);
        assert(memcmp(saved,data,sizeof(data))==0 && post==1);
        io.iouh_Length=sizeof(data);reset();
    }

    for(unsigned failure=0;failure<6;++failure) {
        n=sizeof(data);cookie=(void *)1;
        if(failure==0)fail_meta=1;
        if(failure==1)fail_pci=1;
        if(failure==2)fail_translation=1;
        if(failure==3)fail_translation=4; /* final byte, after two interior checks */
        if(failure==4)split_mapping=1;
        if(failure==5)short_cache=1;
        assert(!xhciPreparePCIPayload(&hc,&io,&n,UHDIR_OUT,&cookie));
        assert(!cookie && !owned && !metadata);
        if(failure==5)assert(pre==1 && post==1);
        reset();
    }
    hc.hc_Flags=0;bus_base=0xfffffff0;n=64;
    assert(!xhciPreparePCIPayload(&hc,&io,&n,UHDIR_OUT,&cookie));reset();
    hc.hc_Flags=HCF_ADDR64;bus_base=UINTPTR_MAX-32;n=64;
    assert(!xhciPreparePCIPayload(&hc,&io,&n,UHDIR_OUT,&cookie));reset();
    bus_base=UINT64_C(0x12345678000);n=64;
    assert(xhciPreparePCIPayload(&hc,&io,&n,UHDIR_OUT,&cookie)==(void *)bus_base);
    xhciReleasePCIPayload(&hc,&io,64,UHDIR_OUT,cookie);reset();

    n=0;assert(!xhciPreparePCIPayload(&hc,&io,&n,UHDIR_IN,&cookie) && !cookie);
    n=sizeof(data)+1;assert(!xhciPreparePCIPayload(&hc,&io,&n,UHDIR_IN,&cookie));
    n=64;assert(!xhciPreparePCIPayload(&hc,&io,&n,UHDIR_IN,NULL));
    assert(!xhciPreparePCIPayload(&hc,&io,NULL,UHDIR_IN,&cookie));
    assert(!xhciPreparePCIPayload(NULL,&io,&n,UHDIR_IN,&cookie));
    assert(!xhciPreparePCIPayload(&hc,NULL,&n,UHDIR_IN,&cookie));
    assert(!xhciPreparePCIPayload(&hc,&io,&n,0xffff,&cookie));
    io.iouh_Data=NULL;
    assert(!xhciPreparePCIPayload(&hc,&io,&n,UHDIR_IN,&cookie));
    io.iouh_Data=data;
    xhciReleasePCIPayload(&hc,&io,0,UHDIR_IN,NULL);
    assert(!pre && !post);reset();

    /* Platform devices must use their own address-space contract. */
    hc.hc_Flags=HCF_PLATFORM|HCF_ADDR64;n=64;
    assert(!xhciPreparePCIPayload(&hc,&io,&n,UHDIR_OUT,&cookie) && !cookie);
    assert(!pre && !post);reset();
    /* Unpublished CPU-addressed preparations balance cache, without copy-back. */
    void *unpublished=calloc(1,64);assert(unpublished);
    xhciDiscardPreparedDMABuffer(unpublished,64,UHDIR_IN,unpublished);
    assert(post==1 && last_post==64 && post_flags==(1UL<<31));reset();
    xhciDiscardPreparedDMABuffer(data,32,UHDIR_OUT,NULL);
    assert(post==1 && last_post==32 && post_flags==((1UL<<31)|DMA_ReadFromRAM));reset();
    puts("xhci-dma result=PASS real-payload,nonidentity,partial-IN,zero-actual,oversize,failures,32bit,platform-rejected");
    return 0;
}
