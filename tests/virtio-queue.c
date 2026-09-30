// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define __NOLIBBASE__
#define __OOP_NOLIBBASE__
#define __OOP_NOATTRBASES__
#define __OOP_NOMETHODBASES__
#include <proto/exec.h>
#include <hidd/pci.h>
#include "virtio_intern.h"
#include "virtio_queue.h"
#include "virtio_debug.h"
struct Mapping { void *cpu; size_t bytes; uintptr_t bus; };
static struct Mapping maps[32];
static unsigned calls, fail_alloc, translations, fail_translate, replies;
static void *pci_alloc(size_t bytes)
{
    if(++calls==fail_alloc) return NULL;
    for(unsigned i=0;i<32;++i) if(!maps[i].cpu) {
        maps[i]=(struct Mapping){calloc(1,bytes),bytes,0x10000000+0x100000*i};
        assert(maps[i].cpu);return maps[i].cpu;
    }
    abort();
}
static void pci_free(void *cpu)
{
    for(unsigned i=0;i<32;++i) if(maps[i].cpu==cpu) {
        free(cpu);maps[i].cpu=NULL;return;
    }
    abort();
}
static void *to_bus(void *cpu)
{
    if(++translations==fail_translate) return (void *)-1;
    for(unsigned i=0;i<32;++i) if(maps[i].cpu) {
        uintptr_t offset=(uintptr_t)cpu-(uintptr_t)maps[i].cpu;
        if(offset<maps[i].bytes) return (void *)(maps[i].bus+offset);
    }
    return (void *)-1;
}
static unsigned live(void) { unsigned n=0;for(unsigned i=0;i<32;++i)n+=maps[i].cpu!=NULL;return n; }
#undef HIDD_PCIDriver_AllocPCIMem
#define HIDD_PCIDriver_AllocPCIMem(o,n) ((void)VirtIOBase,(void)(o),pci_alloc(n))
#undef HIDD_PCIDriver_FreePCIMem
#define HIDD_PCIDriver_FreePCIMem(o,p) ((void)VirtIOBase,(void)(o),pci_free(p))
#undef HIDD_PCIDriver_CPUtoPCI
#define HIDD_PCIDriver_CPUtoPCI(o,p) ((void)VirtIOBase,(void)(o),to_bus(p))
#undef AllocMem
#define AllocMem(n,f) ((void)(f),calloc(1,n))
#undef FreeMem
#define FreeMem(p,n) ((void)(n),free(p))
#undef InitSemaphore
#define InitSemaphore(p) ((void)(p))
#undef ObtainSemaphore
#define ObtainSemaphore(p) ((void)(p))
#undef ReleaseSemaphore
#define ReleaseSemaphore(p) ((void)(p))
#undef ReplyMsg
#define ReplyMsg(m) ((void)(m),++replies)
#include "virtio_queue.c"
static void complete(struct virtio_queue *q,unsigned head,unsigned length,unsigned status)
{
    if(head<q->q_Size) q->q_Reqs[head].vr_Status=status;
    unsigned slot=q->q_Used->idx%q->q_Size;
    q->q_Used->ring[slot].id=head;q->q_Used->ring[slot].len=length;
    ++q->q_Used->idx;
}
int main(void)
{
    struct VirtIOBase base={0};__typeof__(*(device_t)0) dev={0};
    dev.dev_VirtIOBase=&base;dev.dev_PCIDriverObject=(void *)1;
    for(fail_alloc=1;fail_alloc<=2;++fail_alloc) {
        calls=0;assert(!virtio_alloc_queue(&dev,0,8));assert(!live());
    }
    fail_alloc=0;fail_translate=translations+1;
    assert(!virtio_alloc_queue(&dev,0,8));assert(!live());fail_translate=0;
    assert(!virtio_alloc_queue(&dev,0,65536));
    struct virtio_queue *q=virtio_alloc_queue(&dev,0,8);assert(q && live()==2);
    assert(q->q_DescPhys!=(uintptr_t)q->q_RingBase);
    UWORD notify=0xffff;q->q_Notify=&notify;
    struct IOStdReq io={0};unsigned char data[512];memset(data,0x5a,sizeof(data));
    struct virtio_request request={.vr_IORequest=(void *)&io,.vr_DataAddr=data,.vr_DataLength=512};
    for(unsigned n=1;n<=6;++n) {
        fail_translate=translations+n;
        assert(virtio_submit_blk_request(q,&request)==-1);
        assert(q->q_NumFree==8 && !q->q_Avail->idx && live()==2 && notify==0xffff);
    }
    fail_translate=0;fail_alloc=calls+1;
    assert(virtio_submit_blk_request(q,&request)==-1);assert(live()==2 && q->q_NumFree==8);fail_alloc=0;
    int head=virtio_submit_blk_request(q,&request);assert(head>=0 && live()==3 && notify==0);
    struct virtio_request *r=&q->q_Reqs[head];assert(r->vr_DMABuffer!=data);
    memset(r->vr_DMABuffer,0x33,512);complete(q,head,513,VIRTIO_BLK_S_OK);
    assert(virtio_process_used(q)==1 && !io.io_Error && io.io_Actual==512 && data[511]==0x33 && live()==2);
    /* Truncated success cannot expose zero/unwritten bounce data to caller. */
    head=virtio_submit_blk_request(q,&request);memset(data,0x77,512);
    complete(q,head,1,VIRTIO_BLK_S_OK);assert(virtio_process_used(q)==1);
    assert(io.io_Error && !io.io_Actual && data[0]==0x77 && data[511]==0x77);
    request.vr_IsWrite=1;
    head=virtio_submit_blk_request(q,&request);
    assert(!memcmp(q->q_Reqs[head].vr_DMABuffer,data,512));
    complete(q,head,1,VIRTIO_BLK_S_OK);assert(virtio_process_used(q)==1 && !io.io_Error);
    /* Two requests complete out of order. */
    int first=virtio_submit_blk_request(q,&request);
    struct IOStdReq io2={0};request.vr_IORequest=(void *)&io2;
    int second=virtio_submit_blk_request(q,&request);
    assert(first>=0 && second>=0 && first!=second);
    complete(q,second,1,VIRTIO_BLK_S_OK);complete(q,first,1,VIRTIO_BLK_S_UNSUPP);
    assert(virtio_process_used(q)==2 && io.io_Error==IOERR_NOCMD && !io2.io_Error && io2.io_Actual==512 && live()==2 && q->q_NumFree==8);
    request.vr_IORequest=(void *)&io;request.vr_IsWrite=0;
    for(unsigned length=0;length<=514;length+=514) {
        head=virtio_submit_blk_request(q,&request);
        complete(q,head,length,VIRTIO_BLK_S_OK);
        assert(virtio_process_used(q)==1 && io.io_Error && !io.io_Actual && data[0]==0x77);
    }
    request.vr_DataLength=0;request.vr_DataAddr=NULL;
    q->q_LastUsedIdx=q->q_Used->idx=q->q_Avail->idx=65535;
    head=virtio_submit_blk_request(q,&request);assert(q->q_Avail->idx==0);
    complete(q,head,1,VIRTIO_BLK_S_OK);assert(virtio_process_used(q)==1 && q->q_LastUsedIdx==0);
    head=virtio_submit_blk_request(q,&request);
    complete(q,65536+(unsigned)head,1,VIRTIO_BLK_S_OK);
    assert(!virtio_process_used(q) && q->q_Broken && q->q_Reqs[head].vr_InUse);
    assert(virtio_submit_blk_request(q,&request)==-1);
    virtio_free_queue(q);assert(!live()); /* simulated reset and stopped users */
    puts("virtio-queue result=PASS real-queue,owned-DMA,translation-failures,short-read,out-of-order,wrap,32-bit-id");
}
