// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define __NOLIBBASE__
#define _AROS_TYPES_TIMEVAL_S_H_
#include <proto/exec.h>
#include <aros/macros.h>
#include <hardware/usb/xhci.h>
/* Compile the real queue and publication code, mocking only OS services. */
#define UHWCMD_H
#define XHCIPROTO_H
#define XHCICHIP_H
#define XHCI_EVENT_RING_TRBS 8
#define RINGENDCFLAG (1U << 15)
struct PCIController { void *hc_PCIDriverObject; void *hc_Device; };
struct pcisusbXHCIRing {
    struct xhci_trb ring[XHCI_EVENT_RING_TRBS];
    struct IORequest **ringio;
    volatile UWORD next,end;
};
static unsigned depth,writes,translations;
static void xhciRingLock(void) { ++depth; }
static void xhciRingUnlock(void) { assert(depth);--depth; }
static APTR translate(APTR p) { assert(depth);++translations;return (APTR)((IPTR)p+0x100000000ULL); }
static void cache(APTR p,ULONG bytes,ULONG flags)
{ (void)p;(void)flags;assert(depth && bytes==sizeof(struct xhci_trb));++writes; }
#define CPUTOPCI(h,o,p) ((void)(h),(void)(o),translate(p))
#define xhciSetPointer(h,d,p) do { (void)(h);(d).addr64=AROS_QUAD2LE(p); } while(0)
#undef CacheClearE
#define CacheClearE(p,n,f) cache(p,n,f)
#define pciusbXHCIDebugTRBV(...) ((void)0)
#define pciusbXHCIDebugRIO(...) ((void)0)
#define pciusbError(...) ((void)0)
#include "xhci_ring.c"

static void init(struct pcisusbXHCIRing *r, struct IORequest **owners,unsigned next,unsigned end)
{
    memset(r,0,sizeof(*r));
    memset(owners,0,sizeof(*owners)*XHCI_EVENT_RING_TRBS);
    r->ringio=owners;r->next=next;r->end=end|RINGENDCFLAG;
    writes=translations=0;assert(!depth);
}
int main(void)
{
    struct PCIController hc={0};struct IORequest io={0};
    struct pcisusbXHCIRing r,before;struct IORequest *owners[XHCI_EVENT_RING_TRBS];
    unsigned cases=0;
    /* Reproduce the old failure: two descriptors escape before the third
     * hits the guard slot. The public batch path must publish none. */
    init(&r,owners,0,3);
    assert(xhciQueueDataIOUnchecked(&hc,&r,0x200000,2*TRB_TPARAMS_TRBLEN_MAX+1,
                                   TRBF_FLAG_TRTYPE_NORMAL,TRUE,&io)==-1);
    assert(writes==2);
    init(&r,owners,0,3);
    assert(xhciQueueData_IO(&hc,&r,0x200000,2*TRB_TPARAMS_TRBLEN_MAX+1,
                           TRBF_FLAG_TRTYPE_NORMAL,TRUE,&io)==-1);
    assert(!writes && !translations && !depth);
    /* Compare whole-payload admission against the actual scalar primitive at
     * every producer/consumer position, including the Link slot and wrap. */
    for(unsigned next=0;next<XHCI_EVENT_RING_TRBS;++next)
    for(unsigned end=0;end<XHCI_EVENT_RING_TRBS;++end)
    for(unsigned segments=1;segments<=XHCI_EVENT_RING_TRBS+1;++segments)
    for(unsigned with_io=0;with_io<2;++with_io) {
        init(&r,owners,next,end);
        unsigned capacity=0;
        while(capacity<XHCI_EVENT_RING_TRBS &&
              xhciQueueTRB_IO(&hc,&r,0x200000,16,TRBF_FLAG_TRTYPE_NORMAL,&io)>=0)
            ++capacity;
        assert(capacity<XHCI_EVENT_RING_TRBS);
        init(&r,owners,next,end);before=r;
        ULONG bytes=(segments-1)*TRB_TPARAMS_TRBLEN_MAX+1;
        WORD first=with_io ? xhciQueueData_IO(&hc,&r,0x200000,bytes,TRBF_FLAG_TRTYPE_DATA|TRBF_FLAG_DS_DIR,TRUE,&io)
                          : xhciQueueData(&hc,&r,0x200000,bytes,TRBF_FLAG_TRTYPE_DATA|TRBF_FLAG_DS_DIR,TRUE);
        assert(!depth);
        if(segments>capacity) {
            assert(first==-1 && !writes && !translations && memcmp(&r,&before,sizeof(r))==0);
            for(unsigned j=0;j<XHCI_EVENT_RING_TRBS;++j)assert(!owners[j]);
        } else {
            unsigned idx=next;ULONG remaining=bytes;unsigned links=0;
            for(unsigned j=0;j<segments;++j) {
                if(idx==XHCI_EVENT_RING_TRBS-1) {
                    assert(((AROS_LE2LONG(r.ring[idx].flags)&TRB_FLAG_TYPE_MASK))==TRBF_FLAG_TRTYPE_LINK);
                    assert(AROS_LE2QUAD(r.ring[idx].dbp.addr64)==(IPTR)&r.ring[0]+0x100000000ULL);
                    assert(!owners[idx]);idx=0;++links;
                }
                if(!j)assert(first==(WORD)idx);
                ULONG flags=AROS_LE2LONG(r.ring[idx].flags);
                ULONG n=remaining>TRB_TPARAMS_TRBLEN_MAX?TRB_TPARAMS_TRBLEN_MAX:remaining;
                assert((flags&TRB_FLAG_TYPE_MASK)==(j?TRBF_FLAG_TRTYPE_NORMAL:TRBF_FLAG_TRTYPE_DATA));
                assert(!!(flags&TRBF_FLAG_C)==!links);
                assert(!!(flags&TRBF_FLAG_DS_DIR)==!j);
                assert(!!(flags&TRBF_FLAG_CH)==(j+1<segments));
                assert(!!(flags&TRBF_FLAG_IOC)==(j+1==segments));
                assert(AROS_LE2LONG(r.ring[idx].tparams)==n);
                assert(AROS_LE2QUAD(r.ring[idx].dbp.addr64)==0x100200000ULL+bytes-remaining);
                assert(owners[idx]==(with_io?&io:NULL));
                remaining-=n;++idx;
            }
            assert(writes==segments+links && translations==writes && r.next==idx);
        }
        ++cases;
    }
    init(&r,owners,0,0);
    assert(xhciQueueData_IO(&hc,&r,0,0,TRBF_FLAG_TRTYPE_NORMAL,TRUE,&io)==0);
    assert(writes==1 && !translations && r.ring[0].tparams==0);
    init(&r,owners,0,0);before=r;
    assert(xhciQueueData_IO(&hc,&r,~(UQUAD)0-4,16,0,TRUE,&io)==-1);
    assert(xhciQueueData_IO(&hc,&r,0,0xffffffffU,0,TRUE,&io)==-1);
    assert(!writes && memcmp(&r,&before,sizeof(r))==0 && !depth);
    r.next=XHCI_EVENT_RING_TRBS;
    assert(xhciQueueData(&hc,&r,0,1,0,FALSE)==-1 && !writes);
    assert(xhciQueueData(&hc,NULL,0,1,0,FALSE)==-1 && !depth);
    init(&r,owners,0,XHCI_EVENT_RING_TRBS);
    assert(xhciQueueData(&hc,&r,0,1,0,FALSE)==-1 && !writes);
    init(&r,owners,0,0);r.ringio=NULL;
    assert(xhciQueueData(&hc,&r,0,1,0,FALSE)==-1 && !writes && !depth);
    printf("xhci-ring result=PASS cases=%u actual-queue,wrap,nonidentity,all-or-none,zero,overflow\n",cases);
}
