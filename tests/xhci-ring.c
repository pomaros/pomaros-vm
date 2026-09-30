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
#include <aros/macros.h>
#include <hardware/usb/xhci.h>
#include <devices/usbhardware.h>
/* Compile the real queue and publication code, mocking only OS services. */
#define UHWCMD_H
#define XHCIPROTO_H
#define XHCICHIP_H
#define XHCI_EVENT_RING_TRBS 8
#define RINGENDCFLAG (1U << 15)
struct PCIController { void *hc_PCIDriverObject; void *hc_Device; struct List hc_TDQueue,hc_PeriodicTDQueue; };
struct pcisusbXHCIRing {
    struct xhci_trb ring[XHCI_EVENT_RING_TRBS];
    struct IORequest **ringio;
    volatile UWORD next,end;
};
struct xhciCancelPlan { UWORD first,last,next;BOOL moveDequeue,cycle; };
struct xhciControlTRBs { UWORD setup,dataFirst,dataLast,status; };
static unsigned depth,writes,translations;
static void xhciRingLock(void) { ++depth; }
static void maybe_complete(void);
static void xhciRingUnlock(void) { assert(depth);--depth;if(!depth)maybe_complete(); }
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

/* Exercise preparation ordering in the actual control scheduler wrapper. */
#define XHCICHIP_SCHEDULE_H
struct pciusbXHCIDevice { ULONG dc_SlotID; };
struct PTDNode { ULONG ptd_Flags; };
#define PTDF_ACTIVE 1
#define PTDF_BUFFER_VALID 2
struct pciusbXHCIIODevPrivate {
    struct pciusbXHCIDevice *dpDevice;
    ULONG dpEPID,dpCC,dpRemaining;
    BOOL dpCompletionRetired;
    UWORD dpSTRB,dpTxSTRB,dpTxETRB,dpSttTRB;
    APTR dpBounceBuf,dpBounceData;
    ULONG dpBounceLen;
    UWORD dpBounceDir;
};
static unsigned prep_fail,prep_short,prepares,discards;
static UWORD xhciEffectiveDataDir(const struct IOUsbHWReq *io)
{ return (io->iouh_SetupData.bmRequestType&0x80)?UHDIR_IN:UHDIR_OUT; }
static APTR xhciPrepareDMABuffer(struct PCIController *hc,struct IOUsbHWReq *io,
                               ULONG *bytes,UWORD direction,APTR *bounce)
{
    (void)hc;(void)direction;assert(!depth && !writes);++prepares;
    *bounce=NULL;if(prep_fail)return NULL;
    if(prep_short)*bytes/=2;
    return io->iouh_Data;
}
static void xhciDiscardPreparedDMABuffer(struct PCIController *hc,struct IOUsbHWReq *io,APTR cpu,ULONG bytes,UWORD direction,APTR bounce)
{ (void)hc;(void)io;(void)bytes;(void)direction;assert(cpu && !bounce && !depth && !writes);++discards; }
static unsigned doorbells,auto_complete,completion_pending,completions;
static struct IOUsbHWReq *published_io;
static struct List *published_list;
static void init_list(struct List *l)
{ memset(l,0,sizeof(*l));l->lh_Head=(struct Node *)&l->lh_Tail;l->lh_TailPred=(struct Node *)&l->lh_Head; }
static void track(struct List *l,struct Node *n)
{
    assert(depth);published_io=(struct IOUsbHWReq *)n;published_list=l;
    n->ln_Succ=(struct Node *)&l->lh_Tail;n->ln_Pred=l->lh_TailPred;
    l->lh_TailPred->ln_Succ=n;l->lh_TailPred=n;
}
#undef AddTail
#define AddTail(l,n) track(l,n)
static void xhciRingDoorbell(struct PCIController *hc,ULONG slot,ULONG epid)
{
    assert(depth && published_io && published_list->lh_Head==(struct Node *)published_io);
    struct pciusbXHCIIODevPrivate *priv=published_io->iouh_DriverPrivate1;
    assert(priv && priv->dpDevice->dc_SlotID==slot && priv->dpEPID==epid);
    assert(priv->dpBounceLen==published_io->iouh_Length && priv->dpCC==TRB_CC_INVALID);
    assert(!priv->dpRemaining && !priv->dpCompletionRetired);
    assert(!published_io->iouh_Req.io_Error && !published_io->iouh_Actual);
    UWORD cmd=published_io->iouh_Req.io_Command;
    assert(published_list==((cmd==UHCMD_INTXFER || cmd==UHCMD_ISOXFER)?&hc->hc_PeriodicTDQueue:&hc->hc_TDQueue));
    if(cmd==UHCMD_ISOXFER && published_io->iouh_DriverPrivate2)
        assert(((struct PTDNode *)published_io->iouh_DriverPrivate2)->ptd_Flags==PTDF_ACTIVE);
    ++doorbells;completion_pending=auto_complete;
}
static void maybe_complete(void)
{
    if(!completion_pending)return;
    completion_pending=0;++completions;
    struct Node *n=&published_io->iouh_Req.io_Message.mn_Node;
    n->ln_Pred->ln_Succ=n->ln_Succ;n->ln_Succ->ln_Pred=n->ln_Pred;
    free(published_io->iouh_DriverPrivate1);free(published_io);published_io=NULL;
}
#include "xhci_control.c"

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
    unsigned controls=0;
    for(unsigned next=0;next<XHCI_EVENT_RING_TRBS;++next)
    for(unsigned end=0;end<XHCI_EVENT_RING_TRBS;++end)
    for(unsigned segments=0;segments<XHCI_EVENT_RING_TRBS;++segments)
    for(unsigned input=0;input<2;++input) {
        init(&r,owners,next,end);
        unsigned capacity=0;
        while(capacity<XHCI_EVENT_RING_TRBS &&
              xhciQueueTRB_IO(&hc,&r,0x200000,16,TRBF_FLAG_TRTYPE_NORMAL,&io)>=0)
            ++capacity;
        init(&r,owners,next,end);before=r;
        struct xhciControlTRBs indices,untouched;
        memset(&indices,0xa5,sizeof(indices));untouched=indices;
        ULONG bytes=segments?(segments-1)*TRB_TPARAMS_TRBLEN_MAX+1:0;
        BOOL ok=xhciQueueControl_IO(&hc,&r,0x0807060504030201ULL,0x200000,bytes,
                                    TRBF_FLAG_CH|TRBF_FLAG_IOC|TRBF_FLAG_BEI|(input?TRBF_FLAG_DS_DIR:0),&io,&indices);
        assert(!depth);
        if(segments+2>capacity) {
            assert(!ok && !writes && memcmp(&r,&before,sizeof(r))==0);
            assert(memcmp(&indices,&untouched,sizeof(indices))==0);
        } else {
            assert(ok && indices.setup<XHCI_EVENT_RING_TRBS-1 && indices.status<XHCI_EVENT_RING_TRBS-1);
            ULONG sf=AROS_LE2LONG(r.ring[indices.setup].flags);
            ULONG st=AROS_LE2LONG(r.ring[indices.status].flags);
            assert((sf&TRB_FLAG_TYPE_MASK)==TRBF_FLAG_TRTYPE_SETUP && (sf&TRBF_FLAG_IDT));
            assert(((sf>>16)&3)==(segments?(input?3:2):0));
            assert(!(sf&(TRBF_FLAG_CH|TRBF_FLAG_IOC)));
            assert(AROS_LE2QUAD(r.ring[indices.setup].dbp.addr64)==0x0807060504030201ULL);
            assert((st&TRB_FLAG_TYPE_MASK)==TRBF_FLAG_TRTYPE_STATUS && (st&TRBF_FLAG_IOC));
            assert(!!(st&TRBF_FLAG_DS_DIR)==(!segments || !input));
            assert(!(st&TRBF_FLAG_CH));
            if(segments) {
                ULONG tail=AROS_LE2LONG(r.ring[indices.dataLast].flags);
                assert(!(tail&(TRBF_FLAG_CH|TRBF_FLAG_IOC)));
            } else assert(indices.dataFirst==indices.setup && indices.dataLast==indices.setup);
        }
        ++controls;
    }
    struct IOUsbHWReq usb={0};struct pciusbXHCIIODevPrivate priv;
    struct pciusbXHCIDevice device={.dc_SlotID=2};
    usb.iouh_Req.io_Command=UHCMD_CONTROLXFER;usb.iouh_DriverPrivate1=&priv;
    init_list(&hc.hc_TDQueue);init_list(&hc.hc_PeriodicTDQueue);
    UBYTE payload[32];usb.iouh_Data=payload;usb.iouh_Length=32;
    UBYTE setupbytes[8]={0x80,6,0x34,0x12,0x78,0x56,32,0};
    memcpy(&usb.iouh_SetupData,setupbytes,8);
    for(unsigned failure=0;failure<4;++failure) {
        init(&r,owners,0,failure==2?2:0);before=r;
        prep_fail=failure==0;prep_short=failure==1;prepares=discards=0;
        memset(&priv,0x5a,sizeof(priv));struct pciusbXHCIIODevPrivate saved=priv;
        if(failure==3)usb.iouh_Data=NULL;
        assert(!xhciQueueControlStages(&hc,&usb,&priv,&r,TRBF_FLAG_DS_DIR));
        assert(!writes && !depth && memcmp(&r,&before,sizeof(r))==0);
        assert(memcmp(&saved,&priv,sizeof(priv))==0);
        assert(discards==(failure==1 || failure==2));
        usb.iouh_Data=payload;
    }
    for(unsigned failure=0;failure<4;++failure) {
        init(&r,owners,0,failure==2?1:0);before=r;
        prep_fail=failure==0;prep_short=failure==1;prepares=discards=0;
        memset(&priv,0x5a,sizeof(priv));struct pciusbXHCIIODevPrivate saved=priv;
        if(failure==3)usb.iouh_Data=NULL;
        unsigned oldDoorbells=doorbells;
        assert(xhciQueuePayloadTRBs(&hc,&usb,&priv,&r,TRBF_FLAG_TRTYPE_NORMAL,TRUE)==-1);
        assert(!writes && !depth && memcmp(&r,&before,sizeof(r))==0 && doorbells==oldDoorbells);
        assert(memcmp(&saved,&priv,sizeof(priv))==0);
        assert(discards==(failure==1 || failure==2));
        usb.iouh_Data=payload;
    }
    init(&r,owners,0,0);prep_fail=prep_short=prepares=discards=0;
    priv.dpDevice=&device;priv.dpEPID=1;
    assert(xhciQueueControlStages(&hc,&usb,&priv,&r,TRBF_FLAG_DS_DIR));
    assert(prepares==1 && !discards && priv.dpBounceLen==32);
    assert(AROS_LE2QUAD(r.ring[priv.dpSTRB].dbp.addr64)==0x0020567812340680ULL);
    /* Complete and free at the first outer unlock. Publication helpers must
     * have tracked metadata/list/doorbell before that point and touch no freed
     * request afterwards. ASan checks both control and payload return paths. */
    for(unsigned kind=0;kind<4;++kind) {
        struct IOUsbHWReq *fast=calloc(1,sizeof(*fast));assert(fast);
        struct pciusbXHCIIODevPrivate *fp=calloc(1,sizeof(*fp));assert(fp);
        const UWORD cmds[]={UHCMD_CONTROLXFER,UHCMD_BULKXFER,UHCMD_INTXFER,UHCMD_ISOXFER};
        fast->iouh_Req.io_Command=cmds[kind];fast->iouh_Data=payload;fast->iouh_Length=32;
        fast->iouh_Req.io_Error=UHIOERR_HOSTERROR;fast->iouh_Actual=123;
        fast->iouh_DriverPrivate1=fp;fp->dpDevice=&device;fp->dpEPID=1;fp->dpCC=99;fp->dpRemaining=999;fp->dpCompletionRetired=TRUE;
        struct PTDNode ptd={.ptd_Flags=PTDF_BUFFER_VALID};
        if(kind==3)fast->iouh_DriverPrivate2=&ptd;
        init_list(&hc.hc_TDQueue);init_list(&hc.hc_PeriodicTDQueue);
        init(&r,owners,0,0);auto_complete=1;unsigned prior=doorbells,done=completions;
        if(!kind)assert(xhciQueueControlStages(&hc,fast,fp,&r,0));
        else assert(xhciQueuePayloadTRBs(&hc,fast,fp,&r,TRBF_FLAG_TRTYPE_NORMAL,TRUE)>=0);
        assert(!depth && doorbells==prior+1 && completions==done+1 && !published_io);
        assert(hc.hc_TDQueue.lh_Head==(struct Node *)&hc.hc_TDQueue.lh_Tail);
        assert(hc.hc_PeriodicTDQueue.lh_Head==(struct Node *)&hc.hc_PeriodicTDQueue.lh_Tail);
    }
    auto_complete=0;
    unsigned cancellations=0;
    const unsigned slots=XHCI_EVENT_RING_TRBS-1;
    for(unsigned first=0;first<slots;++first)
    for(unsigned count=1;count<slots;++count)
    for(unsigned stopped=0;stopped<XHCI_EVENT_RING_TRBS;++stopped)
    for(unsigned initialCycle=0;initialCycle<2;++initialCycle) {
        init(&r,owners,0,0);
        struct IORequest other={0};
        unsigned last=(first+count-1)%slots;
        for(unsigned idx=0;idx<slots;++idx) {
            r.ring[idx].dbp.addr64=0x12345000+idx*4096;
            r.ring[idx].tparams=64;
            r.ring[idx].flags=AROS_LONG2LE(TRBF_FLAG_TRTYPE_NORMAL|TRBF_FLAG_CH|TRBF_FLAG_ISP|
                ((initialCycle^(idx<first))?TRBF_FLAG_C:0));
            owners[idx]=&other;
        }
        for(unsigned n=0;n<count;++n)owners[(first+n)%slots]=&io;
        unsigned current=stopped==slots?0:stopped;
        BOOL dcs=!!(AROS_LE2LONG(r.ring[current].flags)&TRBF_FLAG_C);
        if(stopped==slots)dcs=!dcs;
        r.ring[slots].flags=AROS_LONG2LE(TRBF_FLAG_TRTYPE_LINK|TRBF_FLAG_TC|(dcs?TRBF_FLAG_C:0));
        r.ring[slots].dbp.addr64=(IPTR)&r.ring[0];
        before=r;struct IORequest *beforeOwners[XHCI_EVENT_RING_TRBS];memcpy(beforeOwners,owners,sizeof(owners));
        struct xhciCancelPlan plan;
        xhciRingLock();
        assert(xhciPrepareStoppedCancellation(&hc,&r,&io,first,last,stopped,dcs,&plan));
        assert(plan.moveDequeue==(beforeOwners[current]==&io));
        assert(writes==count && memcmp(owners,beforeOwners,sizeof(owners))==0);
        for(unsigned idx=0;idx<XHCI_EVENT_RING_TRBS;++idx) {
            if(beforeOwners[idx]==&io) {
                ULONG f=AROS_LE2LONG(r.ring[idx].flags);
                assert(f==((AROS_LE2LONG(before.ring[idx].flags)&TRBF_FLAG_C)|TRBF_FLAG_TRTYPE_NOOP|(idx==last?TRBF_FLAG_IOC:0)));
                assert(!r.ring[idx].dbp.addr64 && !r.ring[idx].tparams);
            } else assert(memcmp(&r.ring[idx],&before.ring[idx],sizeof(r.ring[idx]))==0);
        }
        if(plan.moveDequeue) {
            unsigned walk=stopped;BOOL expectedCycle=dcs;
            while(walk!=last) {
                if(walk==slots){walk=0;expectedCycle=!expectedCycle;}
                else ++walk;
            }
            if(++walk==slots){walk=0;expectedCycle=!expectedCycle;}
            assert(plan.next==walk && plan.cycle==expectedCycle);
            assert(!xhciCommitStoppedCancellation(&r,&io,&plan,FALSE));
            assert(memcmp(owners,beforeOwners,sizeof(owners))==0 && r.end==before.end);
        }
        owners[last]=NULL;
        assert(!xhciCommitStoppedCancellation(&r,&io,&plan,TRUE));
        for(unsigned n=0;n<count;++n)if((first+n)%slots!=last)
            assert(owners[(first+n)%slots]==&io);
        owners[last]=&io;
        assert(xhciCommitStoppedCancellation(&r,&io,&plan,TRUE));
        for(unsigned idx=0;idx<XHCI_EVENT_RING_TRBS;++idx)
            assert(owners[idx]==(beforeOwners[idx]==&io?NULL:beforeOwners[idx]));
        assert(r.end==(plan.moveDequeue?((before.end&RINGENDCFLAG)|plan.next):before.end));
        xhciRingUnlock();++cancellations;
    }
    /* Malformed ownership/range/cycle cannot partly neutralise a request. */
    for(unsigned bad=0;bad<4;++bad) {
        init(&r,owners,0,0);owners[0]=owners[1]=&io;
        r.ring[0].flags=r.ring[1].flags=AROS_LONG2LE(TRBF_FLAG_C|TRBF_FLAG_TRTYPE_NORMAL);
        if(!bad)owners[1]=NULL;
        before=r;struct xhciCancelPlan plan;
        xhciRingLock();
        assert(!xhciPrepareStoppedCancellation(&hc,&r,&io,0,bad==1?slots:1,
                                               bad==2?XHCI_EVENT_RING_TRBS:0,bad==3?FALSE:TRUE,&plan));
        assert(!writes && memcmp(&r,&before,sizeof(r))==0);xhciRingUnlock();
    }
    printf("xhci-cancel-ring result=PASS cases=%u preserve-others,link,cycle,ack-gated-commit,final-noop-ioc,invalid-input\n",cancellations);
    puts("xhci-publication result=PASS metadata,list,doorbell-before-unmask,immediate-free,control-bulk-int-iso");
    printf("xhci-control result=PASS cases=%u prepare-failure,short-prepare,capacity-failure,null-data,setup-bytes\n",controls);
    printf("xhci-ring result=PASS cases=%u actual-queue,wrap,nonidentity,all-or-none,zero,overflow\n",cases);
}
