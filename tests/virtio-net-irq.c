// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _AROS_TYPES_TIMEVAL_S_H_
#define __NOLIBBASE__
#define __OOP_NOLIBBASE__
#define __OOP_NOATTRBASES__
#define __OOP_NOMETHODBASES__
#include <aros/symbolsets.h>
#undef AROS_LIBREQ
#define AROS_LIBREQ(base,version)
extern struct ExecBase *SysBase;
#include <proto/oop.h>
#include <hidd/pci.h>
#include "virtio_intern.h"
#include "virtio_hw.h"
#include "virtio_queue.h"
static unsigned failure,reset_fails,attached,removed,stops,unmapped,freed,restored,released,routes;
static BOOL add_irq(void) { if(failure==1)return FALSE;++attached;return TRUE; }
static void remove_irq(void) { assert(attached && stops && !reset_fails && !freed);++removed; }
static void free_dma(void *p) { assert(stops && !reset_fails && removed==attached);++freed;free(p); }
#undef HIDD_PCIDriver_AddInterrupt
#define HIDD_PCIDriver_AddInterrupt(p,d,i) ((void)VirtIOBase,(void)(p),(void)(d),(void)(i),add_irq())
#undef HIDD_PCIDriver_RemoveInterrupt
#define HIDD_PCIDriver_RemoveInterrupt(p,d,i) ((void)VirtIOBase,(void)(p),(void)(d),(void)(i),remove_irq())
#undef HIDD_PCIDriver_FreePCIMem
#define HIDD_PCIDriver_FreePCIMem(p,m) ((void)VirtIOBase,(void)(p),free_dma(m))
#undef HIDD_PCIDevice_WriteConfigWord
#define HIDD_PCIDevice_WriteConfigWord(d,r,v) ((void)VirtIOBase,(void)(d),(void)(r),(void)(v),++restored)
#undef OOP_ReleaseAttrBase
#define OOP_ReleaseAttrBase(n) ((void)VirtIOBase,(void)(n),++released)
#undef FreeMem
#define FreeMem(p,n) ((void)(n),free(p))
#include "net_transport.c"
BOOL virtio_hw_route_queue_irq(device_t d,UWORD queue)
{
    (void)d;assert(attached==1 && !removed && queue==routes);++routes;
    return failure!=queue+2;
}
BOOL virtio_hw_stop(device_t d) { (void)d;++stops;return !reset_fails; }
void virtio_hw_unmap(device_t d) { (void)d;assert(freed==3 && removed==attached);++unmapped; }
void virtio_free_queue(struct virtio_queue *q)
{
    assert(stops && !reset_fails && removed==attached);++freed;
    free((void *)q->q_Avail);free(q);
}
static UWORD notify;
static void callback(void) {}
int main(void)
{
    unsigned cases=0;
    for(failure=0;failure<4;++failure)for(unsigned stuck=0;stuck<2;++stuck) {
        attached=removed=stops=unmapped=freed=restored=released=routes=0;reset_fails=stuck;
        struct VirtioNet *net=calloc(1,sizeof(*net));
        net->mapped=net->ready=TRUE;net->base.virtio_HiddPCIDeviceAttrBase=1;
        net->memory=malloc(32);net->rx=calloc(1,sizeof(*net->rx));net->tx=calloc(1,sizeof(*net->tx));
        net->rx->q_Avail=calloc(1,64);net->tx->q_Avail=calloc(1,64);
        net->rx->q_Avail->flags=net->tx->q_Avail->flags=1;net->rx->q_Notify=&notify;
        struct Interrupt interrupt={0};interrupt.is_Code=callback;
        assert(!VirtioNetEnableInterrupt(net,NULL));
        assert(!attached && !net->failed);
        BOOL okay=VirtioNetEnableInterrupt(net,&interrupt);
        assert(okay==(failure==0));
        assert(attached==(failure!=1));
        assert(routes==(failure==1?0:failure==2?1:2));
        assert(net->rx->q_Avail->flags==(failure!=0));
        assert(net->tx->q_Avail->flags==(failure!=0));
        if(okay) { assert(!VirtioNetEnableInterrupt(net,&interrupt));assert(attached==1); }
        if(stuck) {
            assert(!VirtioNetClose(net));
            assert(!net->ready && net->failed && !removed && !freed && !unmapped && !released && !restored);
            assert(net->interrupt==(attached?&interrupt:NULL));
            reset_fails=0;
        }
        assert(VirtioNetClose(net));
        assert(removed==attached && freed==3 && unmapped==1 && released==1 && restored==1);
        ++cases;
    }
    printf("virtio-net-irq result=PASS cases=%u registration,queue-routing,reset-quarantine,detach-before-free\n",cases);
}
